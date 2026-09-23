/* Original 00209DF0 normal-status 2D resource and dynamic display adapter.
 * Every layout call, coordinate, colour, TEX0 and text comes from the
 * user's own original resources (tools/export_status_hub.py records). */
#ifndef EM_STATUS_HUB_UI_H
#define EM_STATUS_HUB_UI_H

#include "em_gfx.h"
#include "game/em_item_trail.h"
#include "game/em_status_draw.h"

typedef struct EmStatusHubUI EmStatusHubUI;

typedef struct {
    float health, infection;           /* Original 810858/81085C, each 0..100. */
    uint8_t warning, battery_equipped; /* Original 8104E4/810C7F. */
    uint16_t charge;                   /* Original 810CB2, half-units. */
    uint8_t capacity, hover;           /* Original 810CB7; UI+11 after 0020D930. */
    EmStatusAmmoInventory ammo;
} EmStatusHubDisplay;

/* Prepared original calls, in the exact 209DF0 order, including the expanded
 * 208AD0/209280/209860 bodies and every 00207D00 mode call. */
typedef enum {
    EM_STATUS_HUB_UI_BLEND = 1, /* 00207D00(1,mode) */
    EM_STATUS_HUB_UI_SPRITE,    /* 00207E40 */
    EM_STATUS_HUB_UI_RECTANGLE, /* 00207F80 */
    EM_STATUS_HUB_UI_ARC,       /* 002082B0 */
    EM_STATUS_HUB_UI_MARKER,    /* 00208750: nine 16-vertex line strips */
    EM_STATUS_HUB_UI_TRAIL,     /* 0020AC70: sets mode 1, 512 triangles */
    EM_STATUS_HUB_UI_TEXT       /* 001CBA50 fixed / 001CC1E0 proportional */
} EmStatusHubUIKind;

typedef struct {
    EmStatusHubUIKind kind;
    unsigned mode;          /* mode in effect; BLEND carries the new mode */
    int32_t xy[4];          /* sprite/text x,y,w,h; rectangle x0,y0,x1,y1 */
    uint32_t rgba;          /* sprite/rectangle colour */
    uint64_t tex0;          /* sprite TEX0, or the text style record */
    int proportional, null_style;
    const float *arc;       /* 24 descriptor floats */
    const uint32_t *marker; /* 9 x 16 x {r,g,b,a,x,y} */
    float trail_xy[2];      /* original 20AC70 base, before its +1792/+1824 */
    const int32_t *trail;   /* trail_count x {x0,y0,x1,y1,x2,y2,intensity} */
    unsigned trail_count;
    const char *text;
} EmStatusHubUICommand;

/* Loads version-2 EMHS records and the EMHA atlas. Every TEX0 the records or
 * the dynamic workers can select must be present. math supplies the trail
 * transcendentals (explicit SDK workers). NULL on any malformed input. */
EmStatusHubUI *em_status_hub_ui_load(const char *records_path, const char *atlas_path,
                                     const EmItemMath *math);
void em_status_hub_ui_free(EmStatusHubUI *ui);
/* Invalidate after another page replaces the shared UI texture slot. */
void em_status_hub_ui_deactivate(EmStatusHubUI *ui);

/* Called once per original 209DF0 call (CDC0 phase1 step1 and phase2 step2),
 * after the status actors and 0020D930. ui_clock is the shared UI+20 word:
 * 208AD0 advances it here and in 0020AE40 flag-8 pages; only the UI memsets
 * 0020E060/001AF690 clear it. trail is the shared D_00821300 ring and
 * D_00275C90 cursor: 0020E020 resets it on hub, ITEM and other page entry,
 * and the 20AC70 call advances it here. Both are caller-owned status UI
 * state, never private to this page. stick is this frame's 001B62C0 sample.
 * Returns 1 when prepared; 0 latches failure for the adapter lifetime. */
int em_status_hub_ui_prepare(EmStatusHubUI *ui, const EmStatusHubDisplay *display,
                             const EmItemStick *stick, uint32_t *ui_clock, EmItemTrail *trail);

/* Submits the last prepared stream on the 512x448 status canvas, then the
 * 001FCA10 mode-4 presenter line (001FCB90(0x8A,0xA8,0,line)) when help_line
 * is 0..9; pass -1 while the presenter is inactive. The clock and trail never
 * advance here. The 0020A7A0 background and the status models are separate
 * required workers and are not substituted. Returns 1 on success; 0 for a
 * missing or failed preparation or a renderer failure (latched). */
int em_status_hub_ui_render(EmStatusHubUI *ui, EmGfx *gfx, int help_line);

/* Read-only view of the prepared stream and the original help-line calls. */
unsigned em_status_hub_ui_command_count(const EmStatusHubUI *ui);
int em_status_hub_ui_command(const EmStatusHubUI *ui, unsigned index, EmStatusHubUICommand *out);
unsigned em_status_hub_ui_help_count(const EmStatusHubUI *ui, unsigned line);
int em_status_hub_ui_help(const EmStatusHubUI *ui, unsigned line, unsigned index,
                          EmStatusHubUICommand *out);

#endif
