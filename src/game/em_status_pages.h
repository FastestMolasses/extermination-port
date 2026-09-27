/* Status pages lane (docs/STATUS_PAGES.md): the status-screen pages the
 * first level (AREA11) can open and that had no translation, plus the
 * helpers the committed AREA01 translations of MAP 0020F950 and DATABASE
 * 00214020 (em_area01_ui_*) still reached as callees. Nothing is wired.
 *
 * Hand translations of the original functions (boot ELF SCUS-97112):
 *
 *   MAP helpers (callees of em_area01_ui_00211400 / 0020F950)
 *     00211240(k)            one item-marker sprite of the list mode
 *     00211310(v)            one item-marker sprite of the map view
 *     002117D0(a0, v, m, f)  a world position turned into map-view space
 *   DATABASE helpers (callees of em_area01_ui_00214020 / 002131B0)
 *     00213A00(t, a1)        the category list: cursor, window, 8 lines
 *     00213C50(t, sel)       the 9-entry window refill
 *     00213CC0(t)            the list scroll step (returns 1 at its end)
 *     001FCF30(a0, a1, a2)   a help-bank line through 001FE070
 *   ITEM children (0020EE50 states 4..7; BATTERY 002149F0 is em_spr_)
 *     00214570(t)            EQUIPMENT (module 0x20)
 *     00215870(t)            EVENT (module 0x22)
 *     002160B0(t)            HEALING (module 0x23): the 0x1E/0x1F takes
 *     00215FE0(t)            HEALING's list builder
 *     0020BBE0(t, n)         the list's 5-row window refill
 *     0020BC50(t, tbl, tex, flags)  the list scroll animation
 *   SPR4 (0020CDC0 page 2, module 0x2C; the 0x10 take)
 *     00211970(t)            the SPR4 page: hub, part pages, take notice,
 *                            magazine refill
 *     002121A0(a0)           frame and ammunition readouts
 *     002125B0(t, a1)        the part selector (0020D930 mode 2)
 *     00212B60(t)            the part markers
 *     00212F30(id, ctx)      one part marker
 *     0020BF20(tbl, mode, sel)  the four-row counter panel
 *     00218D90 / 00217090 / 00218640 / 002177B0 / 00217FA0(t)
 *                            the five part pages (modules 0x2D..0x31)
 *
 * The contract is the AREA01 UI lane's (em_area01_ui.h), reused as is: the
 * state is an EmArea01Ui; every original byte is reached through its views
 * by original address; each routine carves its original frame below
 * s->sp; every callee outside this lane goes through s->call by original
 * address with the 64-bit argument register images (a0..a3, t0..t3) and the
 * float argument registers f12..; the first fault latches (codes 1 NULL
 * call, 2 a callee failed, 4 no view, 6 an unmeasured register) and every
 * entry then returns -1. Calls between routines of this lane are direct.
 * Arithmetic through em_ee_float.h on binary32 bit patterns. */
#ifndef EM_STATUS_PAGES_H
#define EM_STATUS_PAGES_H

#include <stdint.h>

#include "game/em_area01_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All entries: 0 on success, -1 on a fault (latched). v0 (when present)
 * receives the original's return value (low 32 bits). */

/* MAP and DATABASE helpers */
int em_status_pages_00211240(EmArea01Ui *s, int32_t k);
int em_status_pages_00211310(EmArea01Ui *s, uint32_t v);
int em_status_pages_002117D0(EmArea01Ui *s, uint64_t a0, uint32_t v, int32_t map, int32_t floor);
int em_status_pages_00213A00(EmArea01Ui *s, uint32_t t, int32_t a1, uint32_t *v0);
int em_status_pages_00213C50(EmArea01Ui *s, uint32_t t, int32_t sel);
int em_status_pages_00213CC0(EmArea01Ui *s, uint32_t t, uint32_t *v0);
int em_status_pages_001FCF30(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint32_t *v0);

/* ITEM children */
int em_status_pages_00214570(EmArea01Ui *s, uint32_t t);
int em_status_pages_00215870(EmArea01Ui *s, uint32_t t);
int em_status_pages_002160B0(EmArea01Ui *s, uint32_t t, uint32_t s0); /* s0: the caller's s0 (0020EE50: t) */
int em_status_pages_00215FE0(EmArea01Ui *s, uint32_t t);
int em_status_pages_0020BBE0(EmArea01Ui *s, uint32_t t, int32_t n);
int em_status_pages_0020BC50(EmArea01Ui *s, uint32_t t, uint32_t table, uint64_t tex, int32_t flags,
                             uint32_t *v0);

/* SPR4 */
int em_status_pages_00211970(EmArea01Ui *s, uint32_t t);
int em_status_pages_002121A0(EmArea01Ui *s, int32_t a0);
int em_status_pages_002125B0(EmArea01Ui *s, uint32_t t, int32_t a1);
int em_status_pages_00212B60(EmArea01Ui *s, uint32_t t);
int em_status_pages_00212F30(EmArea01Ui *s, int32_t id, uint64_t ctx);
int em_status_pages_0020BF20(EmArea01Ui *s, uint32_t table, int32_t mode, int32_t sel);
int em_status_pages_00218D90(EmArea01Ui *s, uint32_t t);
int em_status_pages_00217090(EmArea01Ui *s, uint32_t t);
int em_status_pages_00218640(EmArea01Ui *s, uint32_t t);
int em_status_pages_002177B0(EmArea01Ui *s, uint32_t t);
int em_status_pages_00217FA0(EmArea01Ui *s, uint32_t t);

#ifdef __cplusplus
}
#endif

#endif /* EM_STATUS_PAGES_H */
