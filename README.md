# LZMA Vizualizer

Visualises per-byte perplexity of the compressed data in an LZMA file.

## Usage

```
./LzmaSpec foo.lzma
```

## Example output

![example](/small-lzma.png)

## Analysing the compression ratios of symbols in an ELF file

`contrib/parsemap.py` can be used to show the compression ratio of separate
symbols in an ELF file, given the LZMA-compressed ELF file and the
corresponding linker map file. (See the `cc(1)` and `ld(1)` manpages for notes
on how to output these when linking.)

### Usage

```
usage: parsemap.py [-h] [--recurse RECURSE] [--lzmaspec LZMASPEC] lzma_file
                   map_file

Shows a summary of the compression stats of every symbol in an ELF, given the
compressed and uncompressed ELF files, as well as a linker map file.
(`ld -Map', `cc -Wl,-Map', see respective manpages.)

positional arguments:
  lzma_file            The LZMA-compressed ELF file
  map_file             The linker map file

optional arguments:
  -h, --help           show this help message and exit
  --recurse RECURSE    Recursively analyse data in symbols. Syntax:
                       --recurse symname=linker.map
  --lzmaspec LZMASPEC  LzmaSpec binary to use (default: ./LzmaSpec)
```

### Example output

```
$ contrib/parsemap.py --recurse dtn_snd=introtree/snd/bin/snd.ld.map \
                      introtree/bin/main.smol{.vndh.z,.map}
Symbol                    Uncompr. Size  Perplexity Perplexity/Size
-------------------------------------------------------------------
dtn_snd -> *(.igot)               10195      591.26            0.06
main                                595      256.30            0.43
dtn_snd -> Clinkster_GenerateMusic  707      247.99            0.35
dtn_snd -> _start                   540      201.28            0.37
dtn_frag_glsl                       385      144.75            0.38
_DYNAMIC                            312      126.19            0.40
dtn_snd -> Clinkster_WavFileHeader  164       62.93            0.38
SDL_GL_CreateContext                164       36.57            0.22
_smol_origin                        224       35.54            0.16
_start                               39       21.01            0.54
dtn_snd -> .text.clinkster.genMus    26       14.26            0.55
dtn_snd -> *(.elf.end)             2502       11.09            0.00
glClearColor                          8        3.50            0.44
glCreateShaderProgramv                8        3.46            0.43
__libc_start_main                     8        3.44            0.43
glViewport                            8        3.22            0.40
glUseProgram                          8        3.20            0.40
glClear                               8        3.16            0.40
glRecti                               8        3.10            0.39
SDL_Init                              8        3.07            0.38
SDL_DestroyWindow                     8        3.06            0.38
SDL_CreateWindow                      8        3.05            0.38
SDL_GetTicks                          8        3.00            0.37
glUniform1f                           8        2.98            0.37
SDL_GL_SetAttribute                   8        2.97            0.37
SDL_GL_DeleteContext                  8        2.96            0.37
SDL_GL_SwapWindow                     8        2.87            0.36
SDL_PollEvent                         8        2.81            0.35
SDL_Quit                              8        2.57            0.32
_smol_data_start                     12        1.21            0.10
glUniform2i                           8        1.05            0.13
dtn_snd -> Clinkster_MusicBuffer      8        0.92            0.12
dtn_snd -> *(.text.unlikely_.text.    1        0.78            0.78
dtn_snd -> *(.plt.sec)                1        0.77            0.77
dtn_snd -> *(.text.hot_.text.hot.*    1        0.70            0.70
dtn_snd -> *(.text.exit_.text.exit    1        0.66            0.66
dtn_snd -> [0x08048094]               1        0.62            0.62
dtn_snd -> *(.text.startup_.text.s    1        0.61            0.61
_smol_text_start                      1        0.57            0.57
dtn_snd -> *(.exception_ranges_.ex    1        0.49            0.49
dtn_snd -> *(.text)                   1        0.48            0.48
dtn_snd -> .rodata.clinkster.velfa    1        0.32            0.32
_smol_text_end                        1        0.27            0.27
dtn_snd -> __bss_start                2        0.23            0.12
dtn_snd -> _edata                     1        0.12            0.12
dtn_snd -> *(.data1)                  1        0.12            0.12
dtn_snd -> *(.gnu.warning)            1        0.01            0.01
```

