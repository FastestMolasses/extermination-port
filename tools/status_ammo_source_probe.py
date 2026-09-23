"""Host shim for the actual readable 209860 C, with pointer-width adaptation.

The source's PS2 pointer-through-int ABI is widened on a 64-bit host. Tested
scalar values and all intermediate coordinate/count arithmetic fit signed32.
Graphics/font/string workers are the same explicit boundaries as the native
display-core oracle; the command body is included from the canonical source.
"""
import ctypes
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def compile_readable_ammo(output):
    source = ROOT.parent / 'Extermination/src/func_00209860.c'
    shim = output / 'canonical_ammo.c'
    shim.write_text(r'''#include <stdint.h>
#include <string.h>
#include "game/em_status_draw.c"

#define int intptr_t
#define D_00273570 (*percent_text)
#define D_002862C0 (*format_text)
#include "''' + str(source) + r'''"
#undef D_002862C0
#undef D_00273570
#undef int

intptr_t D_002672A0;
char D_00265510[8];
char *percent_text;
static char format_storage[80], digit_storage[16];
char *format_text = format_storage;
unsigned char D_00810CA4, D_00810CA6;
short D_00810CA8, D_00810CAA, D_00810CAC, D_00810CAE, D_00810CB0, D_00810CB4;
static const EmStatusDrawWorkers *workers;
static int accepted;

intptr_t float_to_int(float value) { return (intptr_t)value; }
void func_00122EF0(void *to, void *from) { strcat(to, from); }
void func_00123168(intptr_t to, intptr_t from) { strcpy((char *)to, (const char *)from); }
char *func_001C5FB0(intptr_t value, intptr_t width, intptr_t blank)
{
    digits(digit_storage, (int)value, (unsigned)width, (int)blank);
    return digit_storage;
}
void func_001CBA50(intptr_t unused, intptr_t x, intptr_t y, intptr_t width,
                  intptr_t height, void *value, void *style)
{
    (void)unused;
    uint64_t encoded;
    memcpy(&encoded, style, sizeof encoded);
    accepted &= workers->text(workers->context, 0, (int)x, (int)y, (int)width,
                              (int)height, value, encoded) == 1;
}
void func_00207D00(intptr_t unused, intptr_t mode)
{
    (void)unused;
    accepted &= workers->blend(workers->context, (unsigned)mode) == 1;
}
void func_00207E40(intptr_t unused, intptr_t x, intptr_t y, intptr_t width,
                  intptr_t height, intptr_t rgba, unsigned long long tex0)
{
    (void)unused;
    accepted &= workers->sprite(workers->context, (int)x, (int)y, (int)width,
                                (int)height, (uint32_t)rgba, tex0) == 1;
}
void func_00207F80(intptr_t unused, intptr_t x, intptr_t y, intptr_t x1,
                  intptr_t y1, intptr_t rgba)
{
    (void)unused;
    accepted &= workers->rectangle(workers->context, (int)x, (int)y, (int)x1,
                                   (int)y1, (uint32_t)rgba) == 1;
}
int canonical_ammo_draw(const EmStatusAmmoInventory *inventory, int x, int y,
                        const EmStatusAmmoData *data, const EmStatusDrawWorkers *draw)
{
    workers = draw;
    accepted = 1;
    D_002672A0 = (intptr_t)data->label;
    memcpy(D_00265510, &data->white, sizeof data->white);
    percent_text = (char *)data->percent;
    D_00810CA4 = inventory->primary;
    D_00810CA6 = inventory->secondary;
    D_00810CA8 = inventory->amount[0];
    D_00810CAA = inventory->amount[1];
    D_00810CAC = inventory->amount[2];
    D_00810CAE = inventory->amount[3];
    D_00810CB0 = inventory->amount[4];
    D_00810CB4 = inventory->reserve;
    func_00209860(NULL, x, y);
    return accepted;
}
''')
    library = output / ('canonical_ammo.dylib' if sys.platform == 'darwin' else 'canonical_ammo.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-ffp-contract=off', '-fPIC',
                    '-Wno-uninitialized',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared',
                    '-I' + str(ROOT / 'src'), str(shim), '-o', str(library)], check=True)
    return ctypes.CDLL(str(library))
