/* em_menu_hover - original 0020D930(t, mode), the status screens' stick
 * hover quantizer: the one translation of it (the hub, the ITEM root and
 * the SPR4 part selector call it).
 *
 * 0020D930 samples the stick through 001B62C0 (normalized magnitude and
 * angle; the callers pass that output here) and selects only when the
 * magnitude, promoted to double, is at least 0.8 (the soft-double compare
 * 00100130 of 00128350's promotion). Then the angle picks a hover from one
 * of three tables: mode 0 (the status hub, hovers 1..4), mode 1 (the ITEM
 * root, 1..5) and any other mode (the SPR4 part selector 002125B0 passes 2:
 * hovers 1..6). A nonzero hover that differs from t[0x11] plays cue 5
 * (001FB9F0(5, 0x1000, 0x1000, 0x1000)); below the gate t[0x11] becomes 0
 * without a sound. Verified against the original instructions by
 * tools/test_menu_hover_source_reference.py (every table). */
#ifndef EM_MENU_HOVER_H
#define EM_MENU_HOVER_H

#include <stdint.h>

/* Stores the new hover in *hover (t[0x11]); returns 1 when cue 5 plays,
 * else 0. */
int em_menu_hover_0020D930(uint8_t *hover, int32_t mode, float magnitude, float angle);

#endif
