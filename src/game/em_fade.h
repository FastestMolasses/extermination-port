/* OS-free state of the two separate original screen effects. Fields are
 * native semantic representations; no hardware packet addresses live here. */
#ifndef EM_FADE_H
#define EM_FADE_H

#include <stdint.h>

enum {
    EM_FADE_BLACK = 0,  /* GS 0xA1: destination minus level */
    EM_FADE_WHITE = 1   /* GS 0x68: destination plus level */
};

/* 001AEE70 transition fields, original D_0028A8E0 + 0xC0..0xC7. */
typedef struct {
    int16_t substate; /* 0 clear, 1 fading in, 2 full, 3 fading out */
    uint8_t colour;  /* zero subtracts; any nonzero value adds */
    int8_t mode;     /* original seven-way dispatch selector */
    int16_t level;
    int16_t step;
} EmTransitionFade;

void em_transition_fade_init(EmTransitionFade *fade);
void em_transition_fade_clear(EmTransitionFade *fade, uint8_t colour); /* 001AED80 */
void em_transition_fade_full(EmTransitionFade *fade, uint8_t colour);  /* 001AEDB0 */
void em_transition_fade_out(EmTransitionFade *fade, int16_t step,
                            uint8_t colour);                       /* 001AEDE0 */
void em_transition_fade_in(EmTransitionFade *fade, int16_t step,
                           uint8_t colour);                        /* 001AEE10 */
void em_transition_fade_flash(EmTransitionFade *fade, int16_t step); /* 001AEE40 */
/* One 001AEE70 invocation. Returns whether the ORIGINAL mode requests
 * a draw this invocation; state changes precede rendering the level. */
int em_transition_fade_tick(EmTransitionFade *fade);

/* 001AEBE0 fields, original D_0028A8D0/D2/D4. Unlike the transition,
 * these arms reset level and no-op at the requested settled endpoint. */
typedef struct {
    int16_t state;
    int16_t step;
    int32_t level;
} EmScreenFade;

void em_screen_fade_init(EmScreenFade *fade);
void em_screen_fade_out(EmScreenFade *fade, int16_t step); /* 001AEB60 */
void em_screen_fade_in(EmScreenFade *fade, int16_t step);  /* 001AEBA0 */
/* One 001AEBE0 invocation. Drawing is suppressed for request==2 when
 * the original D_008106C4 gate is nonzero; updates still take place. */
int em_screen_fade_tick(EmScreenFade *fade, uint8_t request,
                        uint8_t suppress_when_requested);

#endif
