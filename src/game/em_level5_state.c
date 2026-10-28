/* Full 001E7780 area/sub-area dispatch, translated from its decomp C.
 * Kept separate from the live AREA00/AREA01 owner until main integrates it. */
#include "em_level5_state.h"
#include <stddef.h>
#include <string.h>
typedef struct { const EmLevel5StateHooks *h; EmLevel5StateFault *f; uint32_t sink; } State;
static int fail(State *s, uint32_t a, int code)
{
    if (!s->f->code) { s->f->address=a; s->f->code=code; }
    return -1;
}
static uint8_t *at(State *s, uint32_t a, uint32_t n)
{
    uint8_t *p=NULL;
    if (!s->f->code) {
        p=s->h->bytes?s->h->bytes(s->h->ctx,a,n):NULL;
        if (!p) fail(s,a,5);
    }
    if (!p) { s->sink=0; p=(uint8_t *)&s->sink; }
    return p;
}
static uint32_t byte(State *s,uint32_t a) { return *at(s,a,1); }
static uint32_t word(State *s,uint32_t a) { uint32_t v; memcpy(&v,at(s,a,4),4); return v; }
static void zero(State *s,uint32_t a) { uint32_t v=0; memcpy(at(s,a,4),&v,4); }
#define CALL(a) do { \
    if (s.f->code) return -1; \
    if (!h->w_##a) return fail(&s,0x##a,1); \
    if (h->w_##a(h->ctx,key,area)<0) return fail(&s,0x##a,2); \
} while (0)
int em_level5_state_001E7780(const EmLevel5StateHooks *h, EmLevel5StateFault *fault)
{
    if (!h || !fault || fault->code) return -1;
    State s={h,fault,0};
    uint32_t area=byte(&s,0x810700)<<8, key, offset=0, i=0;
    zero(&s,0x275C24); zero(&s,0x275C28); zero(&s,0x275C2C);
    key=area+byte(&s,0x810701);
    zero(&s,0x275C18); zero(&s,0x275C1C); zero(&s,0x275C20);
    switch (key) {
    case 0: key=1; CALL(00824E00); break;
    case 1: case 2: CALL(00824E00); break;
    case 0x100: case 0x101: CALL(00823A50); break;
    case 0x200: case 0x201: case 0x202: CALL(00823900); break;
    case 0x300: case 0x301: case 0xF00: case 0xF01: case 0x1200: case 0x1600: CALL(00823580); break;
    case 0x400: case 0x401: CALL(00823B10); break;
    case 0x600: case 0x601: CALL(00824290); break;
    case 0x700: case 0x701: case 0x702: case 0x703: case 0x704: CALL(00823970); break;
    case 0x800: case 0x801: case 0x802: case 0x803: case 0x804: case 0x805: case 0x806: CALL(008241F0); break;
    case 0xB00: CALL(008237C0); break;
    case 0xD00: CALL(008236E0); break;
    case 0xE00: CALL(00823740); break;
    case 0x1000: case 0x1001: CALL(00824420); break;
    case 0x1100: CALL(00823B80); break;
    case 0x1300: case 0x1301: CALL(008250C0); break;
    case 0x1400: CALL(00823990); break;
    case 0x1500: CALL(00826180); break;
    default: break;
    }
    while (!fault->code && (int32_t)i<(int32_t)word(&s,0x275C2C)) {
        uint32_t rec=word(&s,0x275C20)+offset;
        zero(&s,rec+0x54); zero(&s,rec+0x58);
        ++i; offset+=0xA060;
    }
    return fault->code?-1:0;
}

int em_level5_state_001D0660(const EmLevel5StateHooks *h, EmLevel5StateFault *fault)
{
    if (!h || !fault || fault->code) return -1;
    State s={h,fault,0};
    if (!h->w_001F0310) return fail(&s,0x001F0310,1);
    if (h->w_001F0310(h->ctx)<0) return fail(&s,0x001F0310,2);
    return em_level5_state_001E7780(h,fault);
}
