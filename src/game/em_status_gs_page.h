/* em_status_gs_page.h - the status pages' GS packets, built live into the
 * render context's slot-1 arena (docs/STATUS_FIELD.md section 5, step 3).
 * Prefix em_sgp_.
 *
 * The original builds a status frame's whole 2D layer as slot-1 packets at
 * the time its page code calls the leaf builders (the background 0020A7A0,
 * 00207D00 / 00207E40 / 00207F80, the text runs 001CBA50 / 001CC1E0 and the
 * message service's glyph uploads 001CC8A0 and flushes 001CC3B0); step V
 * closes and kicks the chain. This module binds those calls to the byte
 * translations (em_ui_packet_original) over the one render context
 * (em_render_context_live): the slot cursor D_00275670 + 0x14 and the slot-1
 * buffer of the frame (D_0028F700 + 0xC9000 + index * 0x95760, the frame
 * head's), which are the only bytes it may write. The packets are the
 * original's memory in both profiles; the Original profile draws the BATTERY
 * page's and the ITEM root's frames from them (below).
 *
 * Bound so far: the BATTERY page (em_battery_ui's sink: the leaves of
 * 002149F0's draw and the background, which now runs at its call time), the
 * ITEM root (em_status_runtime's item sink: 0020F170 / 0020F2A0's leaves,
 * their background at its call time, and 0020AC70's 00207D00(1, 1) and
 * sixteen 001D66A0 fans; STATUS_FIELD.md 3e), the ITEM children EQUIPMENT
 * and EVENT (em_status_pages_live's sink: their leaves and background at
 * their call time; STATUS_FIELD.md 3f) and the message service's
 * glyph runs (em_message_live's glyph sink), in every frame. The fonts are
 * the resources' bytes rebuilt from assets/font.emfn (em_ui_font_raw);
 * D_0026E350 is em_ufr_nibbles.
 *
 * Fail-stop: a builder's refusal (a packet past the frame's slot-1 buffer
 * among them) latches here, is reported once, and refuses the caller's
 * call, which fails its page or the message service.
 *
 * The GS field (Original profile; STATUS_FIELD.md sections 3d, 3e): with the
 * GS frame enabled (em_gs_frame_live), a frame whose status draw is the
 * BATTERY page, the ITEM root or a bound page (EQUIPMENT, EVENT;
 * em_status_runtime_render) is not drawn by
 * the GPU: at step V the
 * kicked list (em_rcl_kick: the head, channels 0 and 1, the page; all VIF
 * DIRECT data) is walked into its GIF data and run whole through the CPU GS
 * model (em_gfx_gs_gif_frame), which presents its field; the message
 * service's GPU glyph strips are not drawn over it (their packets are in
 * the list). The model's local memory has what the list reads: the boot's
 * 001CCB10 (em_sgp_001CCB10, the glyph strip page and its CLUT), the page
 * module uploads and the 00200970(1) restore (em_status_runtime).
 *
 * EM_STATUS_GS_DUMP=<file> (developer / test): every frame in which a bound
 * caller built packets appends one record at step V, before the close:
 * "SGPF", u32 frame counter, u32 the slot-1 cursor at the frame head, u32
 * the cursor now, u32 flags (1: the BATTERY page drew; 2: the ITEM root
 * drew; 8: a bound status page (EQUIPMENT, EVENT) drew; 4: a 0020A7A0
 * ran), then the bytes
 * between the two cursors, then (flag 4) the 96 bytes of D_002655A0 before
 * the frame's first 0020A7A0 (tools/test_status_gs_page.py). At most
 * EM_SGP_DUMP_FRAMES records. EM_PRESENT_LOG (em_gfx_metal.m) names the
 * frames presented as fields, with each field's hash. */
#ifndef EM_STATUS_GS_PAGE_H
#define EM_STATUS_GS_PAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SGP_FONT_PATH "assets/font.emfn"
#define EM_SGP_DUMP_FRAMES 2000u

struct EmGfx;

/* Load the fonts and bind the sinks (em_battery_ui, em_message_live; call
 * after em_message_live_install). 0, or -1 (reported: the font export is
 * missing or malformed). */
int em_sgp_install(const char *font_path);
/* Main-loop step B, after 001D1AE0: the frame's slot-1 start. */
void em_sgp_frame_begin(void);
/* Main-loop step V, before 001D2300: the dump record. */
void em_sgp_frame_end(void);
/* The boot's 001CCB10 into the Original profile's GS model (em_gfx_gs_upload:
 * the glyph strip page 0x1B00..0x1B80 zeroed, the grey-ramp CLUT at 0x1B67).
 * em_sgp_install calls it when the GS frame is enabled. 0, or -1. */
int em_sgp_001CCB10(struct EmGfx *gfx);
/* em_status_runtime_render's BATTERY, ITEM root and bound page draws: 1, the GS frame
 * draws this frame (the caller draws nothing); 0, the GPU renderer draws it
 * (the GS frame is off); -1, a fault (the page built no packets this frame;
 * reported). */
int em_sgp_field_claim(void);
/* em_status_runtime_render's draw of a bound page (EQUIPMENT, EVENT;
 * STATUS_FIELD.md 3f): as em_sgp_field_claim, but the page may have built
 * no packets: its code returns before any leaf on the frame it leaves
 * (EQUIPMENT state 1's Circle, for example), and the original then kicks
 * the list without them. Marks the frame a bound page's (the dump's flag
 * 8) in both profiles. 1 or 0, as em_sgp_field_claim. */
int em_sgp_field_claim_page(void);
/* 1 in a frame em_sgp_field_claim claimed, until its kick. */
int em_sgp_field_frame(void);
/* Main-loop step V after 001D2300's kick (and the load veil's frame): a
 * claimed frame's list through the GS model. 0 (done, or not claimed), or
 * -1 (reported). */
int em_sgp_kick(struct EmGfx *gfx);
/* The latched fault, or NULL. */
const char *em_sgp_fault(void);
/* Packets built so far (all builders), for the tests. */
unsigned long em_sgp_packets(void);

#ifdef __cplusplus
}
#endif

#endif
