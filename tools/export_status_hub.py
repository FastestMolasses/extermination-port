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
from test_point_light_reference import number, signed, RETURN

ROOT = Path(__file__).resolve().parents[1]
UI = 0x810130


class Original(SDK):
    def __init__(self, elf, ram, hover, infection, expand_dynamic=False, record_blend=False):
        super().__init__(elf)
        self.record_blend = record_blend
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
        if self.record_blend:
            # Runtime records keep every original00207D00 call, including
            # repeated modes; the JSON fixture layouts carry only 'mode'.
            self.append('blend')

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
                    null_style=not self.r[10],dynamic='infection' if pointer==0x2862c0 else None)

    def text_fixed(self, _): self.text(False)
    def text_proportional(self, _): self.text(True)


class Presenter(Original):
    """Original001FCB90 help presenter over the complete captured EE RAM.

    Group0 lines reach001FE070/001FC770/001FC7B0 and the tall font call
    001CC1E0, which is recorded like the209DF0 text calls. Standard memset
    and byte string length are host boundaries; everything else executes.
    """

    def __init__(self, elf, ram):
        super().__init__(elf, ram, 0, 0)
        self.ram = ram
        self.calls[0x121a28] = lambda o: o.write(o.r[4], bytes([o.r[5] & 255]) * (o.r[6] & 0xffffffff))

    def load(self, address, size=4):
        return sum((self.mem[address + i] if address + i in self.mem else self.ram[address + i]) << (8 * i)
                   for i in range(size))

    def run(self, entry, args=(), floats=(), stop=RETURN):
        # The presenter uses REGIMM branch-likely forms, absent from the SDK runner.
        self.r[31] = RETURN
        for i, value in enumerate(args):
            self.r[4 + i] = value
        pc = entry
        for _ in range(400000):
            if pc == stop:
                return
            word = self.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = signed(word & 65535, 16) * 4
            if op in (2, 3):
                target = (word & 0x3ffffff) * 4
                if op == 3:
                    self.r[31] = pc + 8
                self.plain(self.load(pc + 4))
                if target in self.calls:
                    self.calls[target](self)
                    pc = self.r[31]
                else:
                    pc = target
                continue
            if op == 0 and word & 63 == 8:
                self.plain(self.load(pc + 4))
                pc = self.r[rs] & 0xffffffff
                continue
            if op in (4, 5, 20, 21):
                taken, likely = (self.r[rs] == self.r[rt]) == (op in (4, 20)), op >= 20
            elif op in (6, 7, 22, 23):
                value = signed(self.r[rs], 64)
                taken, likely = (value <= 0 if op in (6, 22) else value > 0), op >= 22
            elif op == 1:
                assert rt in (0, 1, 2, 3), hex(pc)
                value = signed(self.r[rs], 64)
                taken, likely = (value < 0 if rt in (0, 2) else value >= 0), rt >= 2
            elif op == 17 and rs == 8:
                taken, likely = self.condition == bool(rt & 1), bool(rt & 2)
            else:
                assert not (op == 0 and word & 63 == 9), hex(pc)
                self.plain(word)
                pc += 4
                continue
            if likely and not taken:
                pc += 8
                continue
            self.plain(self.load(pc + 4))
            pc = pc + 4 + offset if taken else pc + 8
        raise AssertionError(('Original presenter exceeded bounded instruction limit', hex(pc)))

    def help_line(self, line):
        """Original001FCA10 mode4 path: 001FCB90(0x8A,0xA8,D_00282240=0,line)."""
        self.commands = []
        self.run(0x1fcb90, (0x8a, 0xa8, 0, line))
        assert self.commands and all(c['kind'] == 'text' for c in self.commands)
        return self.commands


RECORD_KINDS = {'sprite': 1, 'rectangle': 2, 'arc': 3, 'line_strips': 4, 'analog_trail': 5,
                'health': 6, 'battery': 7, 'ammo': 8, 'text': 9, 'blend': 10}


def runtime_record(command):
    """One typed original call record: <kind mode size 0> followed by its body."""
    def string(value):
        raw = value.encode('ascii') if isinstance(value, str) else value
        assert b'\0' not in raw and len(raw) < 1024
        return struct.pack('<I', len(raw) + 1) + raw + b'\0'
    kind = command['kind']
    if kind == 'sprite':
        record = struct.pack('<4iIQ', *command['xywh'], command['rgba'], command['tex0'])
    elif kind == 'rectangle':
        record = struct.pack('<4iI', *command['xyxy'], command['rgba'])
    elif kind == 'arc':
        record = struct.pack('<24f', *command['descriptor'])
    elif kind == 'line_strips':
        assert len(command['strips']) == 9 and all(len(s) == 16 for s in command['strips'])
        record = b''.join(struct.pack('<6I', *vertex) for strip in command['strips'] for vertex in strip)
    elif kind in ('health', 'ammo'):
        record = struct.pack('<2i', *command['xy'])
    elif kind == 'analog_trail':
        record = struct.pack('<2f', *command['xy'])
    elif kind == 'battery':
        record = struct.pack('<2iQi', *command['xy'], command['tex0'], command['compact'])
    elif kind == 'text':
        dynamic = command['dynamic'] == 'infection'
        flags = int(dynamic) | int(command['null_style']) << 1
        style = command['style'][0] | command['style'][1] << 32
        # The dynamic209DF0 value is rebuilt from81085C by the adapter.
        record = struct.pack('<6iQ', int(command['proportional']), *command['xywh'], flags, style)
        record += string('' if dynamic else command['value'])
    elif kind == 'blend':
        record = b''
    else:
        raise AssertionError(kind)
    return struct.pack('<4I', RECORD_KINDS[kind], command['mode'], len(record), 0) + record


def runtime_bundle(original, layouts, helps):
    """Typed original resource records for the live 2D renderer, without captures.

    Version2 keeps every original00207D00 call and the executed001FCB90
    help-line text calls; the adapter owns no layout or placement data.
    """
    def string(value):
        assert b'\0' not in value and len(value) < 1024
        return struct.pack('<I', len(value) + 1) + value + b'\0'

    payload = bytearray(original.read(0x265390, 4 * 96))
    payload += original.read(0x265510, 8) + original.read(0x265528, 8)
    label = original.load(0x267298)
    original.run(0x1cc170, (label,))
    payload += struct.pack('<I', original.r[2])
    # Health label/max/separator, battery and ammunition labels, percent.
    for at in (label, 0x273558, 0x273560, 0x273568, original.load(0x26729c),
               original.load(0x2672a0), 0x273570):
        payload += string(original.string_bytes(at))
    for help_line in helps:
        payload += struct.pack('<I', len(help_line['commands']))
        payload += b''.join(runtime_record(command) for command in help_line['commands'])
    for layout in layouts:
        index = layout['hover'] * 2 + int(layout['infection_terminal'])
        payload += struct.pack('<2I', index, len(layout['commands']))
        payload += b''.join(runtime_record(command) for command in layout['commands'])
    return struct.pack('<4s5I', b'EMHS', 2, len(payload), len(layouts), len(helps), 0) + payload


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
    from export_ui import decode_token_lm, pack_shelf
    from gs_vram import read_localmem
    _, local = read_localmem(capture/'gs.bin')
    # The live display can select every original secondary icon, even though
    # the captured first-level equipment has no secondary weapon equipped.
    tokens = list(dict.fromkeys([0x20045ee59d421e40, 0x20045385554221c2,
                                0x20045305554221a6, 0x200451a5554221a2,
                                0x20045325554221b2]+[
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
    presenter=Presenter(elf,ram)
    helps=[{'group':0,'line':line,'commands':presenter.help_line(line)} for line in range(10)]
    args.out.mkdir(parents=True,exist_ok=True)
    header=struct.pack('<4s6I',b'EMHA',1,1024,height,len(sprites),white_index,len(pixels))
    records=b''.join(struct.pack('<Q4I',s['tex0'],*s['xywh']) for s in sprites)
    (args.out/'status_hub_atlas.emha').write_bytes(header+records+pixels)
    output={'elf_sha256':ELF_SHA,'capture_sha256':hashlib.sha256(ram).hexdigest(),
            'callback':'00209DF0','layouts':layouts,'fixture':fixture.commands,'sprites':sprites,'help':helps,
            'atlas':{'width':1024,'height':height,'white_index':white_index},
            'dynamic_workers':['00208AD0 health','00209280 battery','00209860 ammo','0020AC70 trail'],
            'help_source':'executed original 001FCB90 group0 over captured RAM',
            'boundaries':['dynamic worker bodies','original font packet helpers','standard byte string copy/append/memset workers','final GS/Metal rasterization']}
    (args.out/'status_hub_commands.json').write_text(json.dumps(output,indent=2)+'\n')
    bundle_layouts=[]
    for hover in range(5):
        for infection in (0,100):
            original=Original(elf,ram,hover,infection,record_blend=True)
            original.run(0x209df0,(UI,))
            bundle_layouts.append({'hover':hover,'infection_terminal':infection==100,'commands':original.commands})
    (args.out/'status_hub.emhs').write_bytes(runtime_bundle(Original(elf, ram, 0, 0), bundle_layouts, helps))
    print(json.dumps({'original_layouts':len(layouts),'command_counts':[len(x['commands']) for x in layouts],
                      'textures':len(sprites)-1,'help_lines':len(helps),'source':str(capture)}))

if __name__=='__main__':main()
