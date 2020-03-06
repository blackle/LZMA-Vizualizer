#!/usr/bin/env python3

import sys, subprocess

import linkmap

if len(sys.argv) < 4:
    print("""Usage: TODO""")
    exit(1)

lzma = sys.argv[1]
elf  = sys.argv[2]
lmap = sys.argv[3]

weights = [float(x.strip()) for x in \
           subprocess.check_output(['./LzmaSpec', lzma]) \
                .decode('utf-8').split('\n') \
           if len(x) > 0]

#print(weights)
print('\n'.join(str(x) for x in linkmap.parse(open(lmap, 'r').read()).mmap))

