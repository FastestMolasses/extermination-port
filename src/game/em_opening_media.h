/* The AREA11 opening's fade track (opening.emfx, exported locally from the
 * original timing table by tools/export_opening_media.py), sampled on the
 * opening camera's clock.
 *
 * The opening's streamed audio is not played here any more: the stream
 * request of line 0x66 starts cue 63 on the original stream lanes
 * (001FD4C0 -> 001FA790(0, 63) with the D_008106F4 hold; em_stream_live,
 * WP-8b), and the end of the opening resumes cue 25 there (001FAE70(0)). */
#ifndef EM_OPENING_MEDIA_H
#define EM_OPENING_MEDIA_H

/* Game-thread service. prepare loads the fade track once; reuse is allowed.
 * 0, or -1 on a missing or malformed opening.emfx. */
int em_opening_media_prepare(const char *directory);
/* The fade track runs from the opening's stream request (line 0x66, the
 * point where the former stream stand-in armed it) until the opening's
 * teardown: restart arms it at its first value, stop disarms it. */
void em_opening_media_restart(void);
void em_opening_media_stop(void);
void em_opening_media_camera_tick(float camera_time);
void em_opening_media_shutdown(void);
#endif
