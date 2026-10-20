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
 * original's memory in both profiles; drawing them as the field is the next
 * step (STATUS_FIELD.md section 5).
 *
 * Bound so far: the BATTERY page (em_battery_ui's sink: the leaves of
 * 002149F0's draw and the background, which now runs at its call time) and
 * the message service's glyph runs (em_message_live's glyph sink), in every
 * frame. The fonts are the resources' bytes rebuilt from assets/font.emfn
 * (em_ui_font_raw); D_0026E350 is em_ufr_nibbles.
 *
 * Fail-stop: a builder's refusal (a packet past the frame's slot-1 buffer
 * among them) latches here, is reported once, and refuses the caller's
 * call, which fails its page or the message service.
 *
 * EM_STATUS_GS_DUMP=<file> (developer / test): every frame in which a bound
 * caller built packets appends one record at step V, before the close:
 * "SGPF", u32 frame counter, u32 the slot-1 cursor at the frame head, u32
 * the cursor now, u32 flags (1: the BATTERY page drew), then the bytes
 * between the two cursors (tools/test_status_gs_page.py). At most
 * EM_SGP_DUMP_FRAMES records. */
#ifndef EM_STATUS_GS_PAGE_H
#define EM_STATUS_GS_PAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SGP_FONT_PATH "assets/font.emfn"
#define EM_SGP_DUMP_FRAMES 2000u

/* Load the fonts and bind the sinks (em_battery_ui, em_message_live; call
 * after em_message_live_install). 0, or -1 (reported: the font export is
 * missing or malformed). */
int em_sgp_install(const char *font_path);
/* Main-loop step B, after 001D1AE0: the frame's slot-1 start. */
void em_sgp_frame_begin(void);
/* Main-loop step V, before 001D2300: the dump record. */
void em_sgp_frame_end(void);
/* The latched fault, or NULL. */
const char *em_sgp_fault(void);
/* Packets built so far (all builders), for the tests. */
unsigned long em_sgp_packets(void);

#ifdef __cplusplus
}
#endif

#endif
