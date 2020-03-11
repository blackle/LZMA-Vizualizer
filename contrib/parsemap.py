#!/usr/bin/env python3

import sys, subprocess, argparse, lzma
from typing import *

import linkmap, hackyelf
from linkmap import MMap, LinkMap
from hackyelf import ELF, Phdr, Dyn

class SymOff(NamedTuple):
    sym: str
    off: int
    bss: bool
class SymWeight(NamedTuple):
    sof: SymOff
    wgt: float
    start: int
    dsize: int
    wwgt: float

def mem2off(elf: ELF, memaddr: int) -> Optional[int]:
    for p in elf.phdrs:
        if memaddr >= p.vaddr and memaddr < p.vaddr + p.filesz:
            offoff = memaddr - p.vaddr
            return p.off + offoff
        elif memaddr >= p.vaddr and memaddr < p.vaddr + p.memsz:
            offoff = memaddr - p.vaddr
            return -(p.off + offoff) # BSS

    return None

def mkst(recs: Dict[str, str], elf: ELF, mmap: Sequence[MMap], pfix="") -> Sequence[SymOff]:
    lll = []
    for x in mmap:
        off = mem2off(elf, x.org)
        if off is None: # not found?
            # -> normal for eg *_size syms defined in a linnker script
            #print("W: sym %s$%s @ 0x%x not found!" % (x.sect, x.sym, x.org))
            continue

        if x.sym in recs:
            elf2 = hackyelf.parse(elf.data[abs(off):])
            mmap2 = None
            with open(recs[x.sym], 'r') as rcsm:
                mmap2 = linkmap.parse(rcsm.read()).mmap

            for y in mkst(recs, elf2, mmap2, x.sym+' -> '):
                lll.append(SymOff(pfix+y.sym, abs(off)+y.off, \
                                  off<0 or y.off<0))
        else:
            xn = x.sym if x.sym != '.' else (x.sect or ("[0x%08x]"%x.org))
            lll.append(SymOff(pfix+xn, abs(off), off < 0))

    return sorted(lll, key=lambda s: s.off)

def weightsyms(symtab: Sequence[SymOff], weights: Sequence[float]):
    curs = 0
    totalw = 0.0
    lll = []
    start = 0
    for i in range(len(weights)):
        # next sym!
        if len(symtab) > curs+1 and i >= symtab[curs+1].off:
            if i-start > 0:
                lll.append(SymWeight(symtab[curs], totalw, start, i-start, totalw/(i-start)))
            start = i
            curs = curs + 1
            #print("going to %s, was %f" % (symtab[curs].sym, totalw))
            totalw = 0.0

        totalw = totalw + weights[i]

    if start < len(weights):
        lll.append(SymWeight(symtab[curs], totalw, start, len(weights)-start, totalw/(len(weights)-start)))
    return sorted(lll, key=lambda x: x.wgt, reverse=True)

def splitr(s):
    assert '=' in s, "Bad --recurse format %s, see --help" % repr(s)

    i = s.index('=')
    return s[:i], s[i+1:]

def main(opts):
    recs = dict([splitr(s) for s in (opts.recurse or [])])

    weights = [float(x.strip()) for x in \
               subprocess.check_output([opts.lzmaspec, "--raw", opts.lzma_file]) \
                    .decode('utf-8').split('\n') \
               if len(x) > 0]

    elfb = None
    with lzma.open(opts.lzma_file, 'rb') as lf: elfb = lf.read()

    maps = opts.map_file.read()
    elf  = hackyelf.parse(elfb)
    mmap = linkmap.parse(maps).mmap

    symtab = mkst(recs, elf, mmap)

    print("Symbol                    Uncompr. Size\t\tPerplexity\tPerplexity/Size")
    print("-"*79)
    totals, totalw, totalww = 0,0,0
    for x in weightsyms(symtab, weights):
        symn = x.sof.sym
        symn = (symn + ' ' * (34 - len(symn)))[:34] # 34 is good enough
        print("%s%5d\t\t%10.2f\t%15.2f" % (symn, x.dsize, x.wgt, x.wwgt))
        totals += x.dsize
        totalw += x.wgt
        totalww+= x.wwgt
    print("-"*79)
    print("Total:%33d\t\t%10.2f\t%14.1f%%" % (totals, totalw, 100*totalw/totals))
 
    return 0

if __name__ == '__main__':
    p = argparse.ArgumentParser(description="""\
Shows a summary of the compression stats of every symbol in an ELF,
given the compressed and uncompressed ELF files, as well as a linker
map file. (`ld -Map', `cc -Wl,-Map', see respective manpages.)
""")

    p.add_argument("lzma_file", type=str, \
                   help="The LZMA-compressed ELF file")
    p.add_argument("map_file", type=argparse.FileType('r'), \
                   help="The linker map file")

    p.add_argument("--recurse", action='append', help="Recursively analyse "+\
                   "data in symbols. Syntax: --recurse symname=linker.map")

    p.add_argument("--lzmaspec", type=str, default="./LzmaSpec", \
                   help="LzmaSpec binary to use (default: ./LzmaSpec)")

    exit(main(p.parse_args(sys.argv[1:])))

