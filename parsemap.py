#!/usr/bin/env python3

import sys, subprocess
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

def mkst(elf: ELF, mmap: Sequence[MMap]) -> Sequence[SymOff]:
    lll = []
    for x in mmap:
        off = mem2off(elf, x.org)
        if off is None: # not found?
            # -> normal for eg *_size syms defined in a linnker script
            #print("W: sym %s$%s @ 0x%x not found!" % (x.sect, x.sym, x.org))
            continue

        lll.append(SymOff(x.sym, abs(off), off < 0))

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
    return sorted(lll, key=lambda x: x.wwgt, reverse=True)

def main(argv):
    if len(argv) < 4:
        print("""Usage: TODO""")
        return 1

    lzma = argv[1]
    elfn = argv[2]
    lmap = argv[3]

    weights = [float(x.strip()) for x in \
               subprocess.check_output(['./LzmaSpec', lzma]) \
                    .decode('utf-8').split('\n') \
               if len(x) > 0]

    elfb = open(elfn, 'rb').read()
    maps = open(lmap, 'r' ).read()
    elf  = hackyelf.parse(elfb)
    mmap = linkmap.parse(maps).mmap

    symtab = mkst(elf, mmap)

    #print(weights)
    #print('\n'.join(str(x) for x in mmap))
    #print(repr(elf))
    #print('\n'.join(repr(x) for x in symtab))

    assert len(weights) == len(elfb), "LZMA compressed file doesn't belong to the given ELF!"

    print("Symbol                  \tU. Size\t\tPerplexity\tPerplexity/Size")
    print("-"*79)
    for x in weightsyms(symtab, weights):
        symn = x.sof.sym
        symn = symn + (' ' * (24 - len(symn)))[:24]
        print("%s\t%7d\t\t%10.2f\t%15.2f" % (symn, x.dsize, x.wgt, x.wwgt))
    #print('\n'.join(repr(x) for x in weightsyms(symtab, weights)))

    return 0

exit(main(sys.argv))

