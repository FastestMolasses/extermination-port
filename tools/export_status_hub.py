#!/usr/bin/env python3
"""Recover original normal-status draw commands and resident artwork.

The original209DF0 executes from the local ELF. Arc/marker descriptors and
actual draw-worker order are exported, with health/battery/ammo/trail kept
as required dynamic workers. This is not an invented replacement menu.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

from test_item_sdk_math_reference import Original as SDK, ELF_SHA
from test_point_light_reference import number, signed

ROOT = Path(__file__).resolve().parents[1]
UI = 0x810130


class Original(SDK):
    def __init__(self, elf, ram, hover, infection, expand_dynamic=False):
        super().__init__(elf)
        self.write(0x2651b0, ram[0x2651b0:0x2655a0])
        # Runtime-localized label pointers and their exact resident strings.
        self.write(0x267290, ram[0x267290:0x2672b4])
        for pointer in struct.unpack_from('<9I', ram, 0x267290):
            if pointer:
                end = ram.index(0, pointer)
                assert end - pointer < 1024
                self.write(pointer, ram[pointer:end + 1])
        self.write(UI,ram[UI:UI+0xa0])
        self.write(0x8104e4,ram[0x8104e4:0x8104e5])
        self.write(0x810858,ram[0x810858:0x810860])
        self.write(0x810c60,ram[0x810c60:0x810cc4])
        self.save(UI + 0x11, hover, 1)
        self.save(0x81085c, struct.unpack('<I',struct.pack('<f',infection))[0])
        self.commands = []
        self.mode = 0
        self.lo = self.hi = 0
        self.calls.update({0x207d00:self.blend,0x207e40:self.sprite,0x207f80:self.rectangle,
                           0x2082b0:self.arc,0x208750:self.marker,0x20ac70:self.trail,
                           0x208ad0:self.health,0x209280:self.battery,0x209860:self.ammo,
                           0x1cba50:self.text_fixed,0x1cc1e0:self.text_proportional,
                           0x123168:self.copy_string,0x122ef0:self.append_string,
                           0x1232e0:self.string_length})
        if expand_dynamic:
            for callback in (0x208ad0,0x209280,0x209860):
                del self.calls[callback]

    def plain(self, word):
        op, rs, rt, rd, fn = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31, word & 63
        if op == 0 and fn in (24,25):
            a = signed(self.r[rs]) if fn == 24 else self.r[rs] & 0xffffffff
            b = signed(self.r[rt]) if fn == 24 else self.r[rt] & 0xffffffff
            value = a*b & 0xffffffffffffffff
            self.lo = signed(value) & 0xffffffffffffffff
            self.hi = signed(value >> 32) & 0xffffffffffffffff
            self.r[rd] = self.lo
        elif op == 0 and fn in (26,27):
            a = signed(self.r[rs]) if fn == 26 else self.r[rs] & 0xffffffff
            b = signed(self.r[rt]) if fn == 26 else self.r[rt] & 0xffffffff
            assert b != 0
            quotient = abs(a) // abs(b)
            if (a < 0) != (b < 0): quotient = -quotient
            self.lo, self.hi = quotient & 0xffffffffffffffff, (a - quotient*b) & 0xffffffffffffffff
        elif op == 0 and fn in (16,18):
            self.r[rd] = self.hi if fn == 16 else self.lo
        else:
            super().plain(word)
        self.r[0] = 0

    def string_bytes(self, pointer):
        length = 0
        while self.load(pointer+length,1):
            length += 1
            assert length < 1024
        return self.read(pointer,length)

    def string_length(self, _):
        self.r[2] = len(self.string_bytes(self.r[4]))

    def copy_string(self, _):
        destination, source = self.r[4:6]
        self.write(destination,self.string_bytes(source)+b"\0")
        self.r[2] = destination

    def append_string(self, _):
        destination, source = self.r[4:6]
        self.write(destination,self.string_bytes(destination)+self.string_bytes(source)+b"\0")
        self.r[2] = destination

    def blend(self, _):
        assert self.r[4] == 1 and self.r[5] in range(4)
        self.mode = self.r[5]

    def append(self, kind, **fields):
        self.commands.append({'kind':kind,'mode':self.mode,**fields})

    def sprite(self, _):
        assert self.r[4] == 1
        self.append('sprite',xywh=[x&0xffffffff for x in self.r[5:9]],
                    rgba=self.r[9]&0xffffffff,tex0=self.r[10]&0xffffffffffffffff)

    def rectangle(self, _):
        assert self.r[4] == 1
        self.append('rectangle',xyxy=[x&0xffffffff for x in self.r[5:9]],
                    rgba=self.r[9]&0xffffffff)

    def arc(self, _):
        assert self.r[4] == 1
        self.append('arc',source=self.r[5],descriptor=list(struct.unpack('<24f',self.read(self.r[5],96))))

    def marker(self, _):
        assert self.r[4] == 16
        # Execute the static original marker worker into its own disposable
        # packet context, preserving the outer209DF0 registers and stack.
        marker = SDK(self.elf)
        marker.write(0x980000,self.read(self.r[5],48))
        marker.write(0x981000,self.read(self.r[6],48))
        marker.save(0x275670,0x990000);marker.save(0x990014,0xa00000)
        marker.run(0x208750,(16,0x980000,0x981000))
        assert marker.load(0x990014) == 0xa00000+9*560
        strips = []
        for strip in range(9):
            base = 0xa00000+strip*560
            tag = marker.load(base+0x20,8)
            assert tag & 0x7fff == 16 and (tag>>47&0x7ff) == 0x4a
            values = []
            for vertex in range(16):
                at = base+0x30+vertex*32
                color = list(struct.unpack('<4I',marker.read(at,16)))
                xy = list(struct.unpack('<2I',marker.read(at+16,8)))
                values.append(color+xy)
            strips.append(values)
        self.append('line_strips',strips=strips)

    def trail(self, _):
        assert self.r[4] == UI and self.r[6] == 0
        self.append('analog_trail',xy=list(struct.unpack('<2f',self.read(self.r[5],8))))
        self.mode = 1

    def health(self, _):
        assert self.r[4:7] == [UI,208,196]
        self.append('health',xy=self.r[5:7])
        self.mode = 0

    def battery(self, _):
        assert self.r[4:7] == [UI,16,118]
        self.append('battery',xy=self.r[5:7],tex0=self.r[7],compact=self.r[8])
        self.mode = 3

    def ammo(self, _):
        assert self.r[4:7] == [UI,16,190], self.r[4:7]
        self.append('ammo',xy=self.r[5:7])
        self.mode = 3

    def text(self, proportional):
        assert self.r[4] == 1
        pointer = self.r[9]
        length = 0
        while self.load(pointer+length,1):
            length += 1
            assert length < 1024
        style = self.read(self.r[10],8) if self.r[10] else bytes(8)
        self.append('text',proportional=proportional,xywh=[x&0xffffffff for x in self.r[5:9]],
                    value=self.read(pointer,length).decode('ascii'),style=list(struct.unpack('<2I',style)),
                    dynamic='infection' if pointer==0x2862c0 else None)

    def text_fixed(self, _): self.text(False)
    def text_proportional(self, _): self.text(True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp',type=Path,default=ROOT.parent/'Extermination')
    parser.add_argument('--capture',type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'assets/scene_snow/panel')
    args = parser.parse_args()
    capture = args.capture or args.decomp/'build/startup-reference/status-hub'
    elf = (args.decomp/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest()==ELF_SHA
    ram=(capture/'eeMemory.bin').read_bytes()
    assert len(ram)==0x2000000 and ram[0x810131:0x810133]==bytes((1,1))
    layouts=[]
    for hover in range(5):
        for infection in (0,100):
            original=Original(elf,ram,hover,infection)
            original.run(0x209df0,(UI,))
            layouts.append({'hover':hover,'infection_terminal':infection==100,'commands':original.commands})
    fixture=Original(elf,ram,ram[UI+0x11],number(struct.unpack_from('<I',ram,0x81085c)[0]),True)
    fixture.run(0x209df0,(UI,))
    sys.path.insert(0,str(args.decomp/'tools'))
    from export_ui import decode_token_lm, pack_shelf, parse_outer
    from gs_vram import read_localmem
    _, local = read_localmem(capture/'gs.bin')
    tokens = list(dict.fromkeys([0x20045ee59d421e40]+[
        c['tex0'] for layout in layouts+ [{'commands':fixture.commands}] for c in layout['commands'] if 'tex0' in c]))
    decoded = [decode_token_lm(local,token & 0xffffffff,token >> 32) for token in tokens]
    white_index = len(tokens)
    tokens.append(0)
    decoded.append((bytes((255,255,255,255)),{'w':1,'h':1}))
    positions,height=pack_shelf([(m['w'],m['h']) for _,m in decoded],1024)
    pixels=bytearray(1024*height*4)
    sprites=[]
    for token,(data,meta),(x,y) in zip(tokens,decoded,positions):
        w,h=meta['w'],meta['h']
        sprites.append({'tex0':token,'xywh':[x,y,w,h]})
        for row in range(h):
            at=((y+row)*1024+x)*4
            pixels[at:at+w*4]=data[row*w*4:(row+1)*w*4]
    strings_source=(args.decomp/'extract/chunk00/f02_id02.bin').read_bytes()
    directory,_,_,directory_offset=struct.unpack_from('<4I',strings_source)
    outer=directory+struct.unpack_from('<I',strings_source,directory_offset)[0]
    strings,_=parse_outer(strings_source,outer,'normal status help group0')
    helps=[]
    for line in range(10):
        offset,_,_,size=struct.unpack_from('<4I',strings_source,outer+16+line*16)
        base=outer+struct.unpack_from('<I',strings_source,outer)[0]+offset
        spans=[]
        for i in range(size//16):
            tag,color,at,_=struct.unpack_from('<4I',strings_source,base+i*16)
            assert tag==2,'Unsupported normal status help markup'
            rgb=struct.unpack_from('<I',elf,0x26ec10+color*4-0x100000+0x300)[0]
            spans.append([at,rgb])
        helps.append({'group':0,'line':line,'value':strings[line].decode('ascii'),'spans':spans})
    args.out.mkdir(parents=True,exist_ok=True)
    header=struct.pack('<4s6I',b'EMHA',1,1024,height,len(sprites),white_index,len(pixels))
    records=b''.join(struct.pack('<Q4I',s['tex0'],*s['xywh']) for s in sprites)
    (args.out/'status_hub_atlas.emha').write_bytes(header+records+pixels)
    output={'elf_sha256':ELF_SHA,'capture_sha256':hashlib.sha256(ram).hexdigest(),
            'callback':'00209DF0','layouts':layouts,'fixture':fixture.commands,'sprites':sprites,'help':helps,
            'atlas':{'width':1024,'height':height,'white_index':white_index},
            'dynamic_workers':['00208AD0 health','00209280 battery','00209860 ammo','0020AC70 trail'],
            'boundaries':['dynamic worker bodies','original font packet helpers','standard byte string copy/append workers','final GS/Metal rasterization']}
    (args.out/'status_hub_commands.json').write_text(json.dumps(output,indent=2)+'\n')
    print(json.dumps({'original_layouts':len(layouts),'command_counts':[len(x['commands']) for x in layouts],
                      'textures':len(sprites)-1,'help_lines':len(helps),'source':str(capture)}))

if __name__=='__main__':main()
