#!/usr/bin/env python3
"""Check recovered area-title C against the user's original branch instructions.

Only the tiny selector gate is interpreted; no original bytes/disassembly
are printed. The generated host harness stays in ignored build/. This test
caught a branch-delay semantic inversion in the old readable NEARMISS C.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = "ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a"


def original_suppresses(elf: bytes, selector: int) -> int:
    registers = [0] * 32
    registers[3], registers[4] = selector, 1

    def instruction(pc: int) -> int:
        return struct.unpack_from("<I", elf, pc - 0x100000 + 0x300)[0]

    def execute(word: int) -> None:
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        immediate = word & 0xffff
        signed = immediate - 0x10000 if immediate & 0x8000 else immediate
        if op == 9: registers[rt] = (registers[rs] + signed) & 0xffffffff
        elif op == 11: registers[rt] = int(registers[rs] < (signed & 0xffffffff))
        elif op == 0x1c and word & 0x7ff == 0x628:  # paddub (zero-copy here)
            rd = word >> 11 & 31
            registers[rd] = registers[rs]  # every relevant rt is zero
        elif op == 0x24: registers[rt] = 0  # title substate load at gate exit
        elif word != 0: raise ValueError(f"Unexpected selector-gate instruction at opcode {op}")
        registers[0] = 0

    pc = 0x1c59e8
    for _ in range(16):
        if pc == 0x1c5a14: return registers[17]
        word = instruction(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op == 4:
            immediate = word & 0xffff
            if immediate & 0x8000: immediate -= 0x10000
            target = pc + 4 + immediate * 4 if registers[rs] == registers[rt] else pc + 8
            execute(instruction(pc + 4))
            pc = target
        else:
            execute(word)
            pc += 4
    raise ValueError("Selector gate did not reach its expected exit")


HARNESS = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
volatile unsigned char D_70003B8D;
unsigned char D_00810700, D_00810701, D_008106B8;
short D_00289B40[1][2];
int *D_002671C0[1], *D_0026726C[6];
static int draws;
void func_001AFC10(unsigned char *p) {(void)p;}
int func_001C5860(void) {return 0;}
int func_001CC170(int p) {(void)p; return 100;}
int func_001CC1E0(int a,int b,int c,int d,int e,int f,int *g) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
    ++draws; return 0;
}
'''
DRIVER = r'''
int main(void) {
    union { max_align_t alignment; unsigned char bytes[0x220]; } actor;
    for (int selector=0;selector<256;++selector) {
        memset(&actor,0,sizeof actor);
        D_70003B8D=(unsigned char)selector; draws=0;
        func_001C5930(actor.bytes);
        if (draws) return 1; /* init never draws */
        for (int tick=0;tick<301;++tick) func_001C5930(actor.bytes);
        int expected=original_suppressed[selector] ? 0 : 300;
        if(draws!=expected) {
            fprintf(stderr,"selector %d: got %d draws, original expects %d\n",selector,draws,expected);
            return 1;
        }
        if (*(short *)(actor.bytes+0x28)!=0 || actor.bytes[5]!=1) return 1;
    }
    puts("area_title_reference: PASS (256 selectors, 300-tick expiration)");
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decomp",type=Path,default=ROOT.parent/"Extermination")
    args=parser.parse_args()
    elf=(args.decomp/"config/SCUS_971.12").read_bytes()
    if hashlib.sha256(elf).hexdigest()!=ELF_SHA256:
        raise ValueError("Unexpected original executable")
    expected=[original_suppresses(elf,i) for i in range(256)]
    source=(args.decomp/"src/func_001C5930.c").read_text()
    start=re.search(r"^void func_001C5930\(",source,re.MULTILINE)
    if start is None: raise ValueError("Readable title function missing")
    output=ROOT/"build/area11_original"
    output.mkdir(parents=True,exist_ok=True)
    generated=output/"title_oracle.c"
    generated.write_text(HARNESS+source[start.start():]+"\nstatic const int original_suppressed[]={"+
                         ",".join(map(str,expected))+"};\n"+DRIVER)
    binary=output/"title_oracle"
    subprocess.run(["clang","-std=c11","-Wall","-Wextra","-Werror",
                    "-Wno-pointer-to-int-cast","-fsanitize=address,undefined",
                    str(generated),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)


if __name__=="__main__": main()
