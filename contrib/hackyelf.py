
# custom elf parser because a standard one wouldn't be trustable because the
# ELFs we're parsing will be a bit wonky anyway

from struct import unpack
from typing import *


ELFCLASS32 = 1
ELFCLASS64 = 2

EM_386    =  3
EM_X86_64 = 62

PT_NULL    = 0
PT_LOAD    = 1
PT_DYNAMIC = 2
PT_INTERP  = 3

DT_NULL    =  0
DT_NEEDED  =  1
DT_PLTGOT  =  3
DT_STRTAB  =  5
DT_SYMTAB  =  6
DT_RELA    =  7
DT_RELASZ  =  8
DT_RELAENT =  9
DT_STRSZ   = 10
DT_SYMENT  = 11
DT_SONAME  = 14
DT_REL     = 17
DT_RELSZ   = 18
DT_RELENT  = 19
DT_PLTREL  = 20
DT_DEBUG   = 21
DT_TEXTREL = 22
DT_JMPREL  = 23
DT_BIND_NOW= 24

SHT_NULL     =  0
SHT_PROGBITS =  1
SHT_SYMTAB   =  2
SHT_STRTAB   =  3
SHT_RELA     =  4
SHT_DYNAMIC  =  6
SHT_NOBITS   =  8
SHT_REL      =  9
SHT_DYNSYM   = 11

SHF_WRITE     = 1<<0
SHF_ALLOC     = 1<<1
SHF_EXECINSTR = 1<<2
SHF_MERGE     = 1<<4
SHF_STRINGS   = 1<<5
SHF_INFO_LINK = 1<<6

STB_LOCAL  = 0
STB_GLOBAL = 1
STB_WEAK   = 2

STT_NOTYPE = 0
STT_OBJECT = 1
STT_FUNC   = 2
STT_SECTION= 3
STT_FILE   = 4
STT_COMMON = 5
STT_TLS    = 6
STT_GNU_IFUNC = 10

STV_DEFAULT   = 0
STV_INTERNAL  = 1
STV_HIDDEN    = 2
STV_PROTECTED = 3

class Phdr(NamedTuple):
    ptype: int
    off  : int
    vaddr: int
    paddr: int
    filesz: int
    memsz: int
    flags: int
    align: int

class Dyn(NamedTuple):
    tag: int
    val: int

class Shdr(NamedTuple):
    name: Union[int, str]
    type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    addralign: int
    entsize: int

class Sym(NamedTuple):
    name: str
    value: int
    size: int
    type: int
    binding: int
    visibility: int
    shndx: int

class Rel(NamedTuple):
    offset: int
    symbol: Sym
    type: int
class Rela(NamedTuple):
    offset: int
    symbol: Sym
    type: int
    addend: int
Reloc = Union[Rel, Rela]

class ELF(NamedTuple):
    data  : bytes
    ident : bytes
    eclass: int
    mach  : int
    entry : int
    phdrs : Sequence[Phdr]
    dyn   : Sequence[Dyn]
    shdrs : Sequence[Shdr]
    symtab: Sequence[Sym]
    dynsym: Sequence[Sym]
    relocs: Sequence[Reloc]
    is32bit: bool

def readstr(data: bytes, off: int) -> str:
    strb = bytearray()
    while data[off] != 0 and off < len(data):
        strb.append(data[off])
        off = off + 1
    return strb.decode('utf-8')

# yeah, there's some code duplication here
# idgaf

def parse_phdr32(data: bytes, phoff:int, phentsz:int, phnum:int) -> Sequence[Phdr]:
    ps = []
    for off in range(phoff, phoff+phentsz*phnum, phentsz):
        ptype, off, vaddr, paddr, filesz, memsz, flags, align = \
            unpack('<IIIIIIII', data[off:off+8*4])
        p = Phdr(ptype, off, vaddr, paddr, filesz, memsz, flags, align)
        ps.append(p)

    return ps

def parse_dyn32(data: bytes, dynp: Phdr) -> Dyn:
    ds = []

    off = dynp.off
    while True:
        tag, val = unpack('<II', data[off:off+2*4])
        ds.append(Dyn(tag, val))

        if tag == DT_NULL: break
        off = off + 2*4

    return ds

def parse_reloc32(data: bytes, reloff: int, nrel: int, entsz: int, syms: Sequence[Sym], rela: bool) -> Reloc:
    rr=[]

    for off in range(reloff, reloff+entsz*nrel, entsz):
        off, inf, add = unpack('<IIi', data[off:(off+12)]) if rela \
            else (*unpack('<Ii', data[off:(off+8)]),0)

        sym = syms[inf >> 8]
        type = inf & 0xff
        rr.append(Rela(off, sym, type, add) if rela else Rel(off, sym, type))

    return rr

def parse_shdr32(data: bytes, shoff: int, shentsz: int, shnum: int,
                 shstrndx: int) -> Sequence[Shdr]:
    if shnum*shentsz+shoff > len(data) or shentsz==0 or shnum==0 or shoff==0:
        print("snum*shentsz+shoff",shnum*shentsz+shoff)
        print("len(data)",len(data))
        print("shentsz",shentsz)
        print("shnum",shnum)
        print("shoff",shoff)
        return []

    ss = []
    for off in range(shoff, shoff+shentsz*shnum, shentsz):
        noff, typ, flags, addr, off, size, link, info, align, entsz = \
            unpack('<IIIIIIIIII', data[off:off+10*4])
        s = Shdr(noff, typ, flags, addr, off, size, link, info, align, entsz)
        ss.append(s)

    if shstrndx < shnum:
        shstr = ss[shstrndx]
        for i in range(len(ss)):
            sname = readstr(data, shstr.offset + ss[i].name) \
                if ss[i].name < shstr.size else None
            ss[i] = Shdr(sname, ss[i].type, ss[i].flags, ss[i].addr,
                         ss[i].offset, ss[i].size, ss[i].link, ss[i].info,
                         ss[i].addralign, ss[i].entsize)

    return ss

def parse_sym32(data: bytes, sym: Shdr, strt: Shdr) -> Sequence[Sym]:
    ss = []
    for off in range(sym.offset, sym.offset+sym.size, sym.entsize):
        noff, val, sz, info, other, shndx = \
            unpack('<IIIBBH', data[off:off+3*4+2+2])

        sn = readstr(data, strt.offset + noff) \
            if noff < strt.size else None
        s = Sym(sn, val, sz, (info & 15), (info >> 4), other, shndx)
        ss.append(s)
    return ss#sorted(ss, key=lambda x:x.value)

def parse_32(data: bytes) -> ELF:
    ident  = data[:16]
    eclass = data[4]
    mach   = unpack('<H', data[18:18+2])[0]
    entry  = unpack('<I', data[24:24+4])[0]

    phoff   = unpack('<I', data[28:28+4])[0]
    shoff   = unpack('<I', data[32:32+4])[0]
    phentsz = unpack('<H', data[42:42+2])[0]
    phnum   = unpack('<H', data[44:44+2])[0]
    shentsz = unpack('<H', data[46:46+2])[0]
    shnum   = unpack('<H', data[48:48+2])[0]
    shstrndx= unpack('<H', data[50:50+2])[0]

    phdrs = [] if phentsz == 0 else parse_phdr32(data, phoff, phentsz, phnum)
    dyn   = None

    for p in phdrs:
        if p.ptype == PT_DYNAMIC:
            dyn = parse_dyn32(data, p)
            break

    shdrs = parse_shdr32(data, shoff, shentsz, shnum, shstrndx)
    #print("shdrs",shdrs)

    symtabsh = [s for s in shdrs if s.type == SHT_SYMTAB and s.name == ".symtab"]
    strtabsh = [s for s in shdrs if s.type == SHT_STRTAB and s.name == ".strtab"]
    dynsymsh = [s for s in shdrs if s.type == SHT_SYMTAB and s.name == ".dynsym"]
    dynstrsh = [s for s in shdrs if s.type == SHT_STRTAB and s.name == ".dynstr"]
    relash   = [s for s in shdrs if s.type == SHT_RELA]
    relsh    = [s for s in shdrs if s.type == SHT_REL]

    #print("symtab",symtabsh)
    #print("strtab",strtabsh)

    assert len(symtabsh) < 2
    assert len(strtabsh) < 2
    assert len(dynsymsh) < 2
    assert len(dynstrsh) < 2

    symtab, dynsym = None, None
    if len(symtabsh) and len(strtabsh):
        symtab = parse_sym32(data, symtabsh[0], strtabsh[0]) \
            if len(shdrs) > 0 else []
    if len(dynsymsh) and len(dynstrsh):
        dynsym = parse_sym32(data, symtabsh[0], strtabsh[0]) \
            if len(shdrs) > 0 else []

    relocs = []

    # TODO: use sh.link to use the correct symbol table
    for sh in relash:
        relocs += parse_reloc32(data, sh.offset, sh.size//sh.entsize,
                                sh.entsize, symtab, True)
    for sh in relsh:
        relocs += parse_reloc32(data, sh.offset, sh.size//sh.entsize,
                                sh.entsize, symtab, False)
    # TODO: relocs from DT_RELA, DT_REL

    return ELF(data, ident, eclass, mach, entry, phdrs, dyn, shdrs,
               symtab, dynsym, relocs, True)

def parse_phdr64(data: bytes, phoff:int, phentsz:int, phnum:int) -> Sequence[Phdr]:
    ps = []
    for off in range(phoff, phoff+phentsz*phnum, phentsz):
        # TODO # what is TODO exactly??
        ptype, flags, off, vaddr, paddr, filesz, memsz, align = \
            unpack('<IIQQQQQQ', data[off:off+2*4+6*8])
        p = Phdr(ptype, off, vaddr, paddr, filesz, memsz, flags, align)
        ps.append(p)

    return ps

def parse_dyn64(data: bytes, dynp: Phdr) -> Dyn:
    ds = []

    off = dynp.off
    while True:
        tag, val = unpack('<QQ', data[off:off+2*8])
        ds.append(Dyn(tag, val))

        if tag == DT_NULL: break
        off = off + 2*8

    return ds

def parse_reloc64(data: bytes, reloff: int, nrel: int, entsz: int, syms: Sequence[Sym], rela: bool) -> Reloc:
    rr=[]

    for off in range(reloff, reloff+entsz*nrel, entsz):
        off, inf, add = unpack('<QQq', data[off:(off+24)]) if rela \
            else (*unpack('<Qq', data[off:(off+16)]),0)

        sym = syms[inf >> 32]
        type = inf & 0xffffffff
        rr.append(Rela(off, sym, type, add) if rela else Rel(off, sym, type))

    return rr

def parse_shdr64(data: bytes, shoff: int, shentsz: int, shnum: int,
                 shstrndx: int) -> Sequence[Shdr]:

    if shnum*shentsz+shoff > len(data) or shentsz==0 or shnum==0 or shoff==0:
        return []

    ss = []
    for off in range(shoff, shoff+shentsz*shnum, shentsz):
        noff, typ, flags, addr, off, size, link, info, align, entsz = \
            unpack('<IIQQQQIIQQ', data[off:off+4*4+6*8])
        s = Shdr(noff, typ, flags, addr, off, size, link, info, align, entsz)
        ss.append(s)

    if shstrndx < shnum:
        shstr = ss[shstrndx]
        for i in range(len(ss)):
            sname = readstr(data, shstr.offset + ss[i].name) \
                if ss[i].name < shstr.size else None
            ss[i] = Shdr(sname, ss[i].type, ss[i].flags, ss[i].addr,
                         ss[i].offset, ss[i].size, ss[i].link, ss[i].info,
                         ss[i].addralign, ss[i].entsize)

    return ss

def parse_sym64(data: bytes, sym: Shdr, strt: Shdr) -> Sequence[Sym]:
    ss = []
    for off in range(sym.offset, sym.offset+sym.size, sym.entsize):
        noff, info, other, shndx, value, sz = \
            unpack('<IBBHQQ', data[off:off+4+2+2+8*2])

        sn = readstr(data, strt.offset + noff) \
            if noff < strt.size else None
        s = Sym(sn, value, sz, (info & 15), (info >> 4), other, shndx)
        ss.append(s)
    return ss#sorted(ss, key=lambda x:x.value)

def parse_64(data: bytes) -> ELF:
    ident  = data[:16]
    eclass = data[4]
    mach   = unpack('<H', data[18:18+2])[0]
    entry  = unpack('<Q', data[24:24+8])[0]

    phoff   = unpack('<Q', data[32:32+8])[0]
    shoff   = unpack('<Q', data[40:40+8])[0]
    phentsz = unpack('<H', data[54:54+2])[0]
    phnum   = unpack('<H', data[56:56+2])[0]
    shentsz = unpack('<H', data[58:58+2])[0]
    shnum   = unpack('<H', data[60:60+2])[0]
    shstrndx= unpack('<H', data[62:62+2])[0]

    phdrs = [] if phentsz == 0 else parse_phdr64(data, phoff, phentsz, phnum)
    dyn   = None

    for p in phdrs:
        if p.ptype == PT_DYNAMIC:
            dyn = parse_dyn64(data, p)
            break

    shdrs = [] if shentsz == 0 else parse_shdr64(data, shoff, shentsz, shnum, shstrndx)

    symtabsh = [s for s in shdrs if s.type == SHT_SYMTAB and s.name == ".symtab"]
    strtabsh = [s for s in shdrs if s.type == SHT_STRTAB and s.name == ".strtab"]
    dynsymsh = [s for s in shdrs if s.type == SHT_SYMTAB and s.name == ".dynsym"]
    dynstrsh = [s for s in shdrs if s.type == SHT_STRTAB and s.name == ".dynstr"]
    relash   = [s for s in shdrs if s.type == SHT_RELA]
    relsh    = [s for s in shdrs if s.type == SHT_REL]

    assert len(symtabsh) < 2
    assert len(strtabsh) < 2
    assert len(dynsymsh) < 2
    assert len(dynstrsh) < 2

    symtab, dynsym = None, None
    if len(symtabsh) and len(strtabsh):
        symtab = parse_sym64(data, symtabsh[0], strtabsh[0]) \
            if len(shdrs) > 0 else []
    if len(dynsymsh) and len(dynstrsh):
        dynsym = parse_sym64(data, symtabsh[0], strtabsh[0]) \
            if len(shdrs) > 0 else []

    relocs = []

    # TODO: use sh.link to use the correct symbol table
    for sh in relash:
        relocs += parse_reloc32(data, sh.offset, sh.size//sh.entsize,
                                sh.entsize, symtab, True)
    for sh in relsh:
        relocs += parse_reloc32(data, sh.offset, sh.size//sh.entsize,
                                sh.entsize, symtab, False)
    # TODO: relocs from DT_RELA, DT_REL

    return ELF(data, ident, eclass, mach, entry, phdrs, dyn, shdrs,
               symtab, dynsym, relocs, False)

def parse(data: bytes) -> ELF:
    assert data[:4] == b'\x7FELF', "Not a valid ELF file" # good enough

    ecls  = data[4]
    if ecls == ELFCLASS32: return parse_32(data)
    elif ecls == ELFCLASS64: return parse_64(data)
    else:
        emch = unpack('<H', data[18:18+2])[0]
        if emch == EM_386: return parse_32(data)
        elif emch == EM_X86_64: return parse_64(data)

        assert False, "bad E_CLASS %d" % ecls

