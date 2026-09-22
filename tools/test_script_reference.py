#!/usr/bin/env python3
"""Compare native script sequencing with original EE instructions.

The user-owned executable is read locally; neither original machine code
nor disassembly is printed or embedded in a source artifact. A small EE
integer interpreter runs only 001BA1F0 with synthetic command handlers.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ENTRY, END = 0x1BA1F0, 0x1BA50C
ACTOR, PROGRAM, HANDLER = 0x800000, 0x600000, 0x3000000
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'


def signed(value: int, bits: int = 32) -> int:
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value


def candidate_text(path: Path) -> bytes:
    """Relocate this one-function MWCC object for the integer oracle."""
    b=path.read_bytes()
    if b[:6]!=b'\x7fELF\x01\x01': raise ValueError('Expected ELF32 little-endian object')
    shoff=struct.unpack_from('<I',b,0x20)[0]
    ents,count,names=struct.unpack_from('<HHH',b,0x2E)
    sections=[struct.unpack_from('<10I',b,shoff+i*ents) for i in range(count)]
    table=sections[names]
    strings=b[table[4]:table[4]+table[5]]
    def name(section): return strings[section[0]:strings.index(b'\0',section[0])]
    section=next(s for s in sections if name(s)==b'.text')
    code=bytearray(b[section[4]:section[4]+section[5]])
    addresses={'ftab_0024D880':0x24D880,'D_70003B91':0x70003B91}
    for reloc in sections:
        if name(reloc)!=b'.rel.text': continue
        symbols=sections[reloc[6]]; table=sections[symbols[6]]
        strings2=b[table[4]:table[4]+table[5]]
        for off in range(reloc[4],reloc[4]+reloc[5],8):
            target,info=struct.unpack_from('<II',b,off)
            n=struct.unpack_from('<I',b,symbols[4]+(info>>8)*16)[0]
            address=addresses[strings2[n:strings2.index(b'\0',n)].decode()]
            old=struct.unpack_from('<I',code,target)[0]
            if old&65535: raise ValueError('Unexpected relocation addend')
            kind=info&255
            if kind==5: value=(address+0x8000)>>16
            elif kind==6: value=address
            else: raise ValueError(f'Unexpected relocation type {kind}')
            struct.pack_into('<I',code,target,(old&0xFFFF0000)|(value&65535))
    return bytes(code)


def oracle(elf: bytes, records: list[list[int]], skip: int, phase: int,
           code: bytes | None = None):
    memory: dict[int, int] = {}
    registers = [0] * 32
    calls = []
    def load(address, size=4):
        return sum(memory.get(address+i,0) << (8*i) for i in range(size))
    def save(address, value, size=4):
        for i in range(size): memory[address+i] = value >> (8*i) & 255
    def instruction(pc):
        end=ENTRY+len(code) if code is not None else END
        if not ENTRY <= pc < end: raise AssertionError(f'PC escaped function: {pc:x}')
        if code is not None: return struct.unpack_from('<I',code,pc-ENTRY)[0]
        return struct.unpack_from('<I', elf, pc-0x100000+0x300)[0]
    for n, record in enumerate(records):
        for i, value in enumerate(record): save(PROGRAM+n*64+i*4,value)
    for n in range(27): save(0x24D880+n*4,HANDLER+n*4)
    save(ACTOR+0x1F0,1); save(ACTOR+0x1F8,PROGRAM)
    save(ACTOR+0x1FC,phase,1); save(0x70003B91,skip,1)
    registers[4],registers[29],registers[31]=ACTOR,0x2000000,0xBADF00D
    def plain(word):
        op,rs,rt,rd=word>>26,word>>21&31,word>>16&31,word>>11&31
        imm=signed(word&65535,16)
        address=(registers[rs]+imm)&0xFFFFFFFF
        if op==0:
            fn=word&63
            if fn==0: registers[rd]=(registers[rt]<<(word>>6&31))&0xFFFFFFFF
            elif fn==33: registers[rd]=(registers[rs]+registers[rt])&0xFFFFFFFF
            elif fn==36: registers[rd]=registers[rs]&registers[rt]
            else: raise AssertionError(('special',fn))
        elif op==9: registers[rt]=address
        elif op==10: registers[rt]=int(signed(registers[rs])<imm)
        elif op==12: registers[rt]=registers[rs]&(word&65535)
        elif op==15: registers[rt]=(word&65535)<<16
        elif op==28 and word&63==40:
            # PADDUB: componentwise byte sums. Used for zero-copy here.
            registers[rd]=sum((((registers[rs]>>(8*i)&255)+
                               (registers[rt]>>(8*i)&255))&255)<<(8*i)
                              for i in range(16))
        elif op in (30,32,35,36):
            size={30:16,32:1,35:4,36:1}[op]
            value=load(address,size)
            registers[rt]=signed(value,8)&0xFFFFFFFF if op==32 else value
        elif op in (31,40,43): save(address,registers[rt],{31:16,40:1,43:4}[op])
        else: raise AssertionError(('opcode',op))
        registers[0]=0
    pc=ENTRY
    for _ in range(10000):
        if pc==0xBADF00D:
            return (signed(registers[2]),signed(load(ACTOR+0x1F0)),
                    signed(load(ACTOR+0x1F4)),load(ACTOR+0x1F8),
                    signed(load(ACTOR+0x1FC,1),8),calls)
        if HANDLER<=pc<HANDLER+27*4:
            record=registers[6]
            calls.append(record)
            state=registers[5]
            old=load(state+4)
            save(state+4,old+1)
            registers[2]=load(record+8)
            # Synthetic STOP handler can wait twice before advancing.
            if load(record+12) and old<load(record+12): registers[2]=0
            pc=registers[31]
            continue
        word=instruction(pc)
        op,rs,rt=word>>26,word>>21&31,word>>16&31
        if op in (4,5,7,20):
            condition=(registers[rs]==registers[rt]) if op in (4,20) else \
                      ((registers[rs]!=registers[rt]) if op==5 else signed(registers[rs])>0)
            target=pc+4+signed(word&65535,16)*4 if condition else pc+8
            if op!=20 or condition: plain(instruction(pc+4))
            pc=target
        elif op==0 and word&63 in (8,9):
            target=registers[rs]
            if word&63==9: registers[word>>11&31]=pc+8
            plain(instruction(pc+4)); pc=target
        else:
            plain(word); pc+=4
    raise AssertionError(f'Original interpreter failed to yield pc={pc:x} request={skip} phase={phase} calls={calls[:15]} state={(load(ACTOR+0x1F0),load(ACTOR+0x1F4),load(ACTOR+0x1F8))}')


def record(flags, result=1, waits=0, jump=0):
    return [flags,jump,result,waits]+[0]*12


def cases():
    stop=record(0x80000007)
    for phase in (-1,0,1,2,3,127):
        for request in (0,1,2):
            yield ([record(0xD,0),record(6),record(0x18,2),stop],request,phase)
            yield ([record(6),record(0xD,0),record(0x18,2),stop],request,phase)
    yield ([record(0x20000006),record(0xD,0),stop],0,0)
    yield ([record(0xE0000006,jump=PROGRAM+128),stop,stop],0,0)
    yield ([record(0x60000006,jump=PROGRAM+128),record(0xD,0),stop],0,0)
    yield ([record(0x4000000D,0,jump=PROGRAM+128),record(6),record(0x18,2),stop],2,1)
    yield ([record(0xD,0),record(0x80000007,1,2)],2,1)
    yield ([record(6),record(0x80000007),record(0x80000007)],2,1)
    yield ([record(7,3),stop],0,0)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp',type=Path,default=ROOT.parent/'Extermination')
    parser.add_argument('--candidate-object',type=Path,
                        help='Also execute a locally compiled readable PS2 candidate')
    args=parser.parse_args()
    elf=(args.decomp/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA256
    candidate=candidate_text(args.candidate_object) if args.candidate_object else None
    source=['''#include "game/em_script.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char bytes[2048];
static unsigned calls[64], nc;
static unsigned char *resolve(void *ctx,unsigned addr) {
 (void)ctx; return addr>=0x600000 && addr-0x600000<=sizeof bytes-64 ? bytes+(addr-0x600000) : NULL;
}
static EmScriptCommandResult execute(void *ctx,EmScript *s,unsigned char *r) {
 (void)ctx; calls[nc++]=s->pc; int old=s->phase++;
 if(em_script_u32(r,12) && old<(int)em_script_u32(r,12)) return EM_SCRIPT_WAIT;
 return (EmScriptCommandResult)em_script_u32(r,8);
}
static void word(unsigned at,unsigned value) {
 for(unsigned i=0;i<4;i++) bytes[at+i]=(unsigned char)(value>>(8*i));
}
int main(void) { EmScript s; int result;
''']
    count=0
    for records,request,phase in cases():
        expected=oracle(elf,records,request,phase)
        if candidate is not None:
            actual=oracle(elf,records,request,phase,candidate)
            assert actual==expected,(count,actual,expected)
        source.append('memset(bytes,0,sizeof bytes); nc=0;')
        for n,r in enumerate(records):
            for i,v in enumerate(r):
                if v: source.append(f'word({n*64+i*4},0x{v:x}u);')
        source.append(f'em_script_start(&s,0x600000); s.skip_request={request}; s.skip_phase={phase};')
        source.append('result=em_script_tick(&s,resolve,execute,NULL);')
        result,active,cursor,pc,skip,trace=expected
        source.append(f'assert(result=={result} && s.active=={active} && s.phase=={cursor} && s.pc=={pc}u && s.skip_phase=={skip});')
        source.append(f'assert(nc=={len(trace)});')
        source.extend(f'assert(calls[{i}]=={v}u);' for i,v in enumerate(trace))
        count+=1
    source.append(f'puts("script_reference: PASS ({count} original EE cases, normal/skip/STOP/JUMP)"); return 0; }}')
    output=ROOT/'build/area11_original';output.mkdir(parents=True,exist_ok=True)
    generated=output/'script_oracle.c';generated.write_text('\n'.join(source))
    binary=output/'script_oracle'
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I'+str(ROOT/'src'),
                    str(generated),str(ROOT/'src/game/em_script.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    if candidate is not None: print(f'compiled readable C: PASS ({count} original EE cases)')

if __name__=='__main__': main()
