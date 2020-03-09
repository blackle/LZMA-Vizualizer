/* LzmaSpec.c -- LZMA Reference Decoder
2015-06-14 : Igor Pavlov : Public domain */

// This code implements LZMA file decoding according to LZMA specification.
// This code is not optimized for speed.

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include "realcolor.hpp"

#ifdef _MSC_VER
  #pragma warning(disable : 4710) // function not inlined
  #pragma warning(disable : 4996) // This function or variable may be unsafe
#endif

typedef unsigned char Byte;
typedef unsigned short UInt16;

#ifdef _LZMA_UINT32_IS_ULONG
  typedef unsigned long UInt32;
#else
  typedef unsigned int UInt32;
#endif

#if defined(_MSC_VER) || defined(__BORLANDC__)
  typedef unsigned __int64 UInt64;
#else
  typedef unsigned long long int UInt64;
#endif


struct CInputStream
{
  FILE *File;
  UInt64 Processed;
  
  void Init() { Processed = 0; }

  Byte ReadByte()
  {
    int c = getc(File);
    if (c < 0)
      throw "Unexpected end of file";
    Processed++;
    return (Byte)c;
  }
};


struct COutStream
{
  std::vector<Byte> Data;

  void WriteByte(Byte b)
  {
    Data.push_back(b);
  }
};


class COutWindow
{
  Byte *Buf;
  UInt32 Pos;
  UInt32 Size;
  bool IsFull;

public:
  unsigned TotalPos;
  COutStream OutStream;

  COutWindow(): Buf(NULL) {}
  ~COutWindow() { delete []Buf; }
 
  void Create(UInt32 dictSize)
  {
    Buf = new Byte[dictSize];
    Pos = 0;
    Size = dictSize;
    IsFull = false;
    TotalPos = 0;
  }

  void PutByte(Byte b)
  {
    TotalPos++;
    Buf[Pos++] = b;
    if (Pos == Size)
    {
      Pos = 0;
      IsFull = true;
    }
    OutStream.WriteByte(b);
  }

  Byte GetByte(UInt32 dist) const
  {
    return Buf[dist <= Pos ? Pos - dist : Size - dist + Pos];
  }

  void CopyMatch(UInt32 dist, unsigned len)
  {
    for (; len > 0; len--)
      PutByte(GetByte(dist));
  }

  bool CheckDistance(UInt32 dist) const
  {
    return dist <= Pos || IsFull;
  }

  bool IsEmpty() const
  {
    return Pos == 0 && !IsFull;
  }
};


#define kNumBitModelTotalBits 11
#define kNumMoveBits 5

typedef UInt16 CProb;

#define PROB_INIT_VAL ((1 << kNumBitModelTotalBits) / 2)

#define INIT_PROBS(p) \
 { for (unsigned i = 0; i < sizeof(p) / sizeof(p[0]); i++) p[i] = PROB_INIT_VAL; }

class CRangeDecoder
{
  UInt32 Range;
  UInt32 Code;

  void Normalize();

public:

  CInputStream *InStream;
  bool Corrupted;
  float Perplexity;

  bool Init();
  bool IsFinishedOK() const { return Code == 0; }

  UInt32 DecodeDirectBits(unsigned numBits);
  unsigned DecodeBit(CProb *prob);
};

bool CRangeDecoder::Init()
{
  Corrupted = false;
  Range = 0xFFFFFFFF;
  Code = 0;
  Perplexity = 0.f;

  Byte b = InStream->ReadByte();
  
  for (int i = 0; i < 4; i++)
    Code = (Code << 8) | InStream->ReadByte();
  
  if (b != 0 || Code == Range)
    Corrupted = true;
  return b == 0;
}

#define kTopValue ((UInt32)1 << 24)

void CRangeDecoder::Normalize()
{
  if (Range < kTopValue)
  {
    Range <<= 8;
    Code = (Code << 8) | InStream->ReadByte();
  }
}

UInt32 CRangeDecoder::DecodeDirectBits(unsigned numBits)
{
  Perplexity += numBits;
  UInt32 res = 0;
  do
  {
    Range >>= 1;
    Code -= Range;
    UInt32 t = 0 - ((UInt32)Code >> 31);
    Code += Range & t;
    
    if (Code == Range)
      Corrupted = true;
    
    Normalize();
    res <<= 1;
    res += t + 1;
  }
  while (--numBits);
  return res;
}

unsigned CRangeDecoder::DecodeBit(CProb *prob)
{
  unsigned v = *prob;
  UInt32 bound = (Range >> kNumBitModelTotalBits) * v;
  unsigned symbol;
  if (Code < bound)
  {
    Perplexity += -log2(v/2048.f);
    v += ((1 << kNumBitModelTotalBits) - v) >> kNumMoveBits;
    Range = bound;
    symbol = 0;
  }
  else
  {
    Perplexity += -log2(1.f-v/2048.f);
    v -= v >> kNumMoveBits;
    Code -= bound;
    Range -= bound;
    symbol = 1;
  }
  *prob = (CProb)v;
  Normalize();
  return symbol;
}


unsigned BitTreeReverseDecode(CProb *probs, unsigned numBits, CRangeDecoder *rc)
{
  unsigned m = 1;
  unsigned symbol = 0;
  for (unsigned i = 0; i < numBits; i++)
  {
    unsigned bit = rc->DecodeBit(&probs[m]);
    m <<= 1;
    m += bit;
    symbol |= (bit << i);
  }
  return symbol;
}

template <unsigned NumBits>
class CBitTreeDecoder
{
  CProb Probs[(unsigned)1 << NumBits];

public:

  void Init()
  {
    INIT_PROBS(Probs);
  }

  unsigned Decode(CRangeDecoder *rc)
  {
    unsigned m = 1;
    for (unsigned i = 0; i < NumBits; i++)
      m = (m << 1) + rc->DecodeBit(&Probs[m]);
    return m - ((unsigned)1 << NumBits);
  }

  unsigned ReverseDecode(CRangeDecoder *rc)
  {
    return BitTreeReverseDecode(Probs, NumBits, rc);
  }
};

#define kNumPosBitsMax 4

#define kNumStates 12
#define kNumLenToPosStates 4
#define kNumAlignBits 4
#define kStartPosModelIndex 4
#define kEndPosModelIndex 14
#define kNumFullDistances (1 << (kEndPosModelIndex >> 1))
#define kMatchMinLen 2

class CLenDecoder
{
  CProb Choice;
  CProb Choice2;
  CBitTreeDecoder<3> LowCoder[1 << kNumPosBitsMax];
  CBitTreeDecoder<3> MidCoder[1 << kNumPosBitsMax];
  CBitTreeDecoder<8> HighCoder;

public:

  void Init()
  {
    Choice = PROB_INIT_VAL;
    Choice2 = PROB_INIT_VAL;
    HighCoder.Init();
    for (unsigned i = 0; i < (1 << kNumPosBitsMax); i++)
    {
      LowCoder[i].Init();
      MidCoder[i].Init();
    }
  }

  unsigned Decode(CRangeDecoder *rc, unsigned posState)
  {
    if (rc->DecodeBit(&Choice) == 0)
      return LowCoder[posState].Decode(rc);
    if (rc->DecodeBit(&Choice2) == 0)
      return 8 + MidCoder[posState].Decode(rc);
    return 16 + HighCoder.Decode(rc);
  }
};

unsigned UpdateState_Literal(unsigned state)
{
  if (state < 4) return 0;
  else if (state < 10) return state - 3;
  else return state - 6;
}
unsigned UpdateState_Match   (unsigned state) { return state < 7 ? 7 : 10; }
unsigned UpdateState_Rep     (unsigned state) { return state < 7 ? 8 : 11; }
unsigned UpdateState_ShortRep(unsigned state) { return state < 7 ? 9 : 11; }

#define LZMA_DIC_MIN (1 << 12)

class CLzmaDecoder
{
public:
  CRangeDecoder RangeDec;
  COutWindow OutWindow;
  std::vector<float> Perplexities;
  std::vector<bool> Literals;

  bool markerIsMandatory;
  unsigned lc, pb, lp;
  UInt32 dictSize;
  UInt32 dictSizeInProperties;

  void DecodeProperties(const Byte *properties)
  {
    unsigned d = properties[0];
    if (d >= (9 * 5 * 5))
      throw "Incorrect LZMA properties";
    lc = d % 9;
    d /= 9;
    pb = d / 5;
    lp = d % 5;
    dictSizeInProperties = 0;
    for (int i = 0; i < 4; i++)
      dictSizeInProperties |= (UInt32)properties[i + 1] << (8 * i);
    dictSize = dictSizeInProperties;
    if (dictSize < LZMA_DIC_MIN)
      dictSize = LZMA_DIC_MIN;
  }

  CLzmaDecoder(): LitProbs(NULL) {}
  ~CLzmaDecoder() { delete []LitProbs; }

  void Create()
  {
    OutWindow.Create(dictSize);
    CreateLiterals();
  }

  int Decode(bool unpackSizeDefined, UInt64 unpackSize);
  
private:

  CProb *LitProbs;

  void CreateLiterals()
  {
    LitProbs = new CProb[(UInt32)0x300 << (lc + lp)];
  }
  
  void InitLiterals()
  {
    UInt32 num = (UInt32)0x300 << (lc + lp);
    for (UInt32 i = 0; i < num; i++)
      LitProbs[i] = PROB_INIT_VAL;
  }
  
  void DecodeLiteral(unsigned state, UInt32 rep0)
  {
    unsigned prevByte = 0;
    if (!OutWindow.IsEmpty())
      prevByte = OutWindow.GetByte(1);
    
    unsigned symbol = 1;
    unsigned litState = ((OutWindow.TotalPos & ((1 << lp) - 1)) << lc) + (prevByte >> (8 - lc));
    CProb *probs = &LitProbs[(UInt32)0x300 * litState];
    
    if (state >= 7)
    {
      unsigned matchByte = OutWindow.GetByte(rep0 + 1);
      do
      {
        unsigned matchBit = (matchByte >> 7) & 1;
        matchByte <<= 1;
        unsigned bit = RangeDec.DecodeBit(&probs[((1 + matchBit) << 8) + symbol]);
        symbol = (symbol << 1) | bit;
        if (matchBit != bit)
          break;
      }
      while (symbol < 0x100);
    }
    while (symbol < 0x100)
      symbol = (symbol << 1) | RangeDec.DecodeBit(&probs[symbol]);
    OutWindow.PutByte((Byte)(symbol - 0x100));
  }

  CBitTreeDecoder<6> PosSlotDecoder[kNumLenToPosStates];
  CBitTreeDecoder<kNumAlignBits> AlignDecoder;
  CProb PosDecoders[1 + kNumFullDistances - kEndPosModelIndex];
  
  void InitDist()
  {
    for (unsigned i = 0; i < kNumLenToPosStates; i++)
      PosSlotDecoder[i].Init();
    AlignDecoder.Init();
    INIT_PROBS(PosDecoders);
  }
  
  unsigned DecodeDistance(unsigned len)
  {
    unsigned lenState = len;
    if (lenState > kNumLenToPosStates - 1)
      lenState = kNumLenToPosStates - 1;
    
    unsigned posSlot = PosSlotDecoder[lenState].Decode(&RangeDec);
    if (posSlot < 4)
      return posSlot;
    
    unsigned numDirectBits = (unsigned)((posSlot >> 1) - 1);
    UInt32 dist = ((2 | (posSlot & 1)) << numDirectBits);
    if (posSlot < kEndPosModelIndex)
      dist += BitTreeReverseDecode(PosDecoders + dist - posSlot, numDirectBits, &RangeDec);
    else
    {
      dist += RangeDec.DecodeDirectBits(numDirectBits - kNumAlignBits) << kNumAlignBits;
      dist += AlignDecoder.ReverseDecode(&RangeDec);
    }
    return dist;
  }

  void PushPerplexities(unsigned len)
  {
    for (int i = 0; i < len; i++) {
      Perplexities.push_back(RangeDec.Perplexity/len);
    }
    RangeDec.Perplexity = 0.f;
  }

  CProb IsMatch[kNumStates << kNumPosBitsMax];
  CProb IsRep[kNumStates];
  CProb IsRepG0[kNumStates];
  CProb IsRepG1[kNumStates];
  CProb IsRepG2[kNumStates];
  CProb IsRep0Long[kNumStates << kNumPosBitsMax];

  CLenDecoder LenDecoder;
  CLenDecoder RepLenDecoder;

  void Init()
  {
    InitLiterals();
    InitDist();

    INIT_PROBS(IsMatch);
    INIT_PROBS(IsRep);
    INIT_PROBS(IsRepG0);
    INIT_PROBS(IsRepG1);
    INIT_PROBS(IsRepG2);
    INIT_PROBS(IsRep0Long);

    LenDecoder.Init();
    RepLenDecoder.Init();
  }
};
    

#define LZMA_RES_ERROR                   0
#define LZMA_RES_FINISHED_WITH_MARKER    1
#define LZMA_RES_FINISHED_WITHOUT_MARKER 2

int CLzmaDecoder::Decode(bool unpackSizeDefined, UInt64 unpackSize)
{
  if (!RangeDec.Init())
    return LZMA_RES_ERROR;

  Init();

  UInt32 rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
  unsigned state = 0;
  
  for (;;)
  {
    if (unpackSizeDefined && unpackSize == 0 && !markerIsMandatory)
      if (RangeDec.IsFinishedOK())
        return LZMA_RES_FINISHED_WITHOUT_MARKER;

    unsigned posState = OutWindow.TotalPos & ((1 << pb) - 1);

    if (RangeDec.DecodeBit(&IsMatch[(state << kNumPosBitsMax) + posState]) == 0)
    {
      if (unpackSizeDefined && unpackSize == 0)
        return LZMA_RES_ERROR;
      DecodeLiteral(state, rep0);
      PushPerplexities(1);
      Literals.push_back(true);
      state = UpdateState_Literal(state);
      unpackSize--;
      continue;
    }
    
    unsigned len;
    
    if (RangeDec.DecodeBit(&IsRep[state]) != 0)
    {
      if (unpackSizeDefined && unpackSize == 0)
        return LZMA_RES_ERROR;
      if (OutWindow.IsEmpty())
        return LZMA_RES_ERROR;
      if (RangeDec.DecodeBit(&IsRepG0[state]) == 0)
      {
        if (RangeDec.DecodeBit(&IsRep0Long[(state << kNumPosBitsMax) + posState]) == 0)
        {
          state = UpdateState_ShortRep(state);
          OutWindow.PutByte(OutWindow.GetByte(rep0 + 1));
          PushPerplexities(1);
          Literals.push_back(false);
          unpackSize--;
          continue;
        }
      }
      else
      {
        UInt32 dist;
        if (RangeDec.DecodeBit(&IsRepG1[state]) == 0)
          dist = rep1;
        else
        {
          if (RangeDec.DecodeBit(&IsRepG2[state]) == 0)
            dist = rep2;
          else
          {
            dist = rep3;
            rep3 = rep2;
          }
          rep2 = rep1;
        }
        rep1 = rep0;
        rep0 = dist;
      }
      len = RepLenDecoder.Decode(&RangeDec, posState);
      state = UpdateState_Rep(state);
    }
    else
    {
      rep3 = rep2;
      rep2 = rep1;
      rep1 = rep0;
      len = LenDecoder.Decode(&RangeDec, posState);
      state = UpdateState_Match(state);
      rep0 = DecodeDistance(len);
      if (rep0 == 0xFFFFFFFF)
        return RangeDec.IsFinishedOK() ?
            LZMA_RES_FINISHED_WITH_MARKER :
            LZMA_RES_ERROR;

      if (unpackSizeDefined && unpackSize == 0)
        return LZMA_RES_ERROR;
      if (rep0 >= dictSize || !OutWindow.CheckDistance(rep0))
        return LZMA_RES_ERROR;
    }
    len += kMatchMinLen;
    bool isError = false;
    if (unpackSizeDefined && unpackSize < len)
    {
      len = (unsigned)unpackSize;
      isError = true;
    }
    OutWindow.CopyMatch(rep0 + 1, len);
    Literals.insert(Literals.end(), len, false);
    PushPerplexities(len);
    unpackSize -= len;
    if (isError)
      return LZMA_RES_ERROR;
  }
}

//https://www.andrewnoske.com/wiki/Code_-_heatmaps_and_color_gradients
class ColorGradient
{
private:
  struct ColorPoint  // Internal class used to store colors at different points in the gradient.
  {
    float r,g,b;      // Red, green and blue values of our color.
    float val;        // Position of our color along the gradient (between 0 and 1).
    ColorPoint(float red, float green, float blue, float value)
      : r(red), g(green), b(blue), val(value) {}
  };
  std::vector<ColorPoint> color;      // An array of color points in ascending value.
  
public:
  //-- Default constructor:
  ColorGradient()  {  createDefaultHeatMapGradient();  }
  
  //-- Inserts a new color point into its correct position:
  void addColorPoint(float red, float green, float blue, float value)
  {
    for(int i=0; i<color.size(); i++)  {
      if(value < color[i].val) {
        color.insert(color.begin()+i, ColorPoint(red,green,blue, value));
        return;  }}
    color.push_back(ColorPoint(red,green,blue, value));
  }
  
  //-- Inserts a new color point into its correct position:
  void clearGradient() { color.clear(); }
 
  //-- Places a 5 color heapmap gradient into the "color" vector:
  void createDefaultHeatMapGradient()
  {
    color.clear();
    color.push_back(ColorPoint(0, 0, 0,   0.0f));      // Blue.
    color.push_back(ColorPoint(0, 0, 1,   0.2f));     // Cyan.
    color.push_back(ColorPoint(0, 1, 0,   0.5f));      // Green.
    color.push_back(ColorPoint(1, 1, 0,   0.7f));     // Yellow.
    color.push_back(ColorPoint(1, 0, 0,   0.9f));      // Red.
  }
  void createViridisHeatMapGradient()
  {
    color.clear();
    color.push_back(ColorPoint(0x44/255.f,0x02/255.f,0x55/255.f,   0.0f));
    color.push_back(ColorPoint(0x2C/255.f,0x70/255.f,0x8E/255.f,   0.33f));
    color.push_back(ColorPoint(0x3D/255.f,0xBB/255.f,0x74/255.f,   0.66f));
    color.push_back(ColorPoint(0xFA/255.f,0xE6/255.f,0x22/255.f,   1.f));
  }
  
  //-- Inputs a (value) between 0 and 1 and outputs the (red), (green) and (blue)
  //-- values representing that position in the gradient.
  void getColorAtValue(const float value, float &red, float &green, float &blue)
  {
    if(color.size()==0)
      return;
    
    for(int i=0; i<color.size(); i++)
    {
      ColorPoint &currC = color[i];
      if(value < currC.val)
      {
        ColorPoint &prevC  = color[ std::max(0,i-1) ];
        float valueDiff    = (prevC.val - currC.val);
        float fractBetween = (valueDiff==0) ? 0 : (value - currC.val) / valueDiff;
        red   = (prevC.r - currC.r)*fractBetween + currC.r;
        green = (prevC.g - currC.g)*fractBetween + currC.g;
        blue  = (prevC.b - currC.b)*fractBetween + currC.b;
        return;
      }
    }
    red   = color.back().r;
    green = color.back().g;
    blue  = color.back().b;
    return;
  }

  std::string get(const float value)
  {
    float r,g,b;
    getColorAtValue(value, r,g,b);
    std::stringstream ss;
    ss << realcolor::bg(r,g,b) << realcolor::fg(1.f-r,1.f-g,1.f-b);
    return ss.str();
  }

  std::string printScale(int width)
  {
    std::stringstream ss;
    for (int i = 0; i < width; i++) {
      float val = (i*1.f)/width;
      float r,g,b;
      getColorAtValue(val, r,g,b);
      ss << realcolor::fg(r,g,b) << "━";
    }
    ss << realcolor::reset;
    return ss.str();
  }
};

static void usage(char** argv) {
  std::cerr << "usage: " << argv[0] << " [--raw] [--jet] [--help] file.lzma" << std::endl;
}

int main(int argc, char** argv)
{
#ifdef _MSC_VER
  bool pretty = true;
#else
  bool pretty = isatty(STDOUT_FILENO);
#endif
  bool jet = false;
  bool literals = false;

  if (argc < 2) {
    usage(argv);
    return 1;
  }

  int fileargind = 1;
  if (!strcmp(argv[fileargind], "--raw")) {
    fileargind++;
    pretty = false;
  }

  if (!strcmp(argv[fileargind], "--jet")) {
    fileargind++;
    jet = true;
  }

  if (!strcmp(argv[fileargind], "--lits")) {
    fileargind++;
    literals = true;
  }

  if (!strcmp(argv[fileargind], "--help")) {
    usage(argv);
    return 0;
  }

  CInputStream inStream;
  inStream.File = fopen(argv[fileargind], "rb");
  inStream.Init();
  if (inStream.File == 0)
    throw "Can't open input file";

  CLzmaDecoder lzmaDecoder;

  Byte header[13];
  int i;
  for (i = 0; i < 13; i++)
    header[i] = inStream.ReadByte();

  lzmaDecoder.DecodeProperties(header);

  UInt64 unpackSize = 0;
  bool unpackSizeDefined = false;
  for (i = 0; i < 8; i++)
  {
    Byte b = header[5 + i];
    if (b != 0xFF)
      unpackSizeDefined = true;
    unpackSize |= (UInt64)b << (8 * i);
  }

  lzmaDecoder.markerIsMandatory = !unpackSizeDefined;

  lzmaDecoder.RangeDec.InStream = &inStream;

  lzmaDecoder.Create();

  int res = lzmaDecoder.Decode(unpackSizeDefined, unpackSize);

  if (res == LZMA_RES_ERROR)
    throw "LZMA decoding error";

  if (lzmaDecoder.RangeDec.Corrupted)
  {
    std::cerr << "Warning: LZMA stream is corrupted" << std::endl;
  }

  ColorGradient grad;
  if (jet) {
    grad.createDefaultHeatMapGradient();
  } else {
    grad.createViridisHeatMapGradient();
  }
  double maxPerplexity = *std::max_element(lzmaDecoder.Perplexities.begin(), lzmaDecoder.Perplexities.end());
  int colWidth = 64;
  int scaleFreq = 16;
  float minForCol = 1.;
  float maxForCol = 0;
  float avgForCol = 0;
  for (int j = 0; j < lzmaDecoder.OutWindow.OutStream.Data.size(); j++) {
    if (!pretty) {
      std::cout << lzmaDecoder.Perplexities[j]/maxPerplexity << std::endl;
      continue;
    }
    if (j % colWidth == 0 && (j / colWidth)%scaleFreq == 0) {
      std::cout << grad.printScale(colWidth) << std::endl;
    }
    bool literal = lzmaDecoder.Literals[j];
    float heat = sqrt(lzmaDecoder.Perplexities[j]/maxPerplexity);
    avgForCol += heat;
    maxForCol = std::max(maxForCol, heat);
    minForCol = std::min(minForCol, heat);
    if (literals) heat = literal ? 1. : 0.;

    char byte = lzmaDecoder.OutWindow.OutStream.Data[j];
    if (!std::isprint(byte)) {
      byte = '.';
    }
    std::cout
      << grad.get(heat)
      // << std::setfill('0')
      // << std::setw(2)
      // << std::right
      // << std::hex
      << byte
      << realcolor::reset;
    // if (j % 16 == 16-1) {
    //   std::cout << " ";
    // }
    if (j % colWidth == colWidth-1) {
      std::cout << " "
        << grad.get(minForCol) << " "
        << grad.get(avgForCol/colWidth) << " "
        << grad.get(maxForCol) << " "
        << realcolor::reset << std::endl;
      minForCol = 1;
      maxForCol = 0;
      avgForCol = 0;
    }
  }
  std::cout << std::endl;

  return 0;
}
