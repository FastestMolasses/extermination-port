/* em_message_presenters_live.h - the mode-3 / mode-4 message presenters
 * bound live on the step-F message service (docs/CENSUS_STANDINS.md 3,
 * docs/MESSAGE_PRESENTER_REST.md 3; WP-8 decision (b)).
 *
 * This module adds no behaviour of its own. Over the service's own
 * EmMessageDraw (em_message_live_draw) it binds:
 *   001FCB90  the help presenter            em_cs_001FCB90
 *   001FCF60  the record title presenter    em_cs_001FCF60
 *   001FCF90  the record list presenter     em_cs_001FCF90
 *   001FD0E0  the mode-3 cue presenter      em_cs_001FD0E0, whose workers
 *             are 001FC9B0 (em_message_live_reset: the block it clears is
 *             the service's) and the cue line walker 001FDDB0
 *             (em_mpr_001FDDB0, its gate sound 001FB9F0 through em_sfx_play)
 * with the data tools/export_message_data.py writes next to the .emmd
 * (assets/message/message_presenters.emmp: the containers *D_0028A498 /
 * *D_0028A49C, the cue bank *D_0028A4EC at its captured address 0x011739C0,
 * the configs D_00264CF0 / D_00264C90 and their styles, D_00264DB0).
 * D_00282228 is the block's +0x78 word.
 *
 * Fail-stop: without the data nothing is bound, so the first request that
 * reaches a presenter faults in the service (em_message_live). */
#ifndef EM_MESSAGE_PRESENTERS_LIVE_H
#define EM_MESSAGE_PRESENTERS_LIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#define EM_MESSAGE_PRESENTERS_PATH "assets/message/message_presenters.emmp"

/* After em_message_live_install (which clears the hook). 1 bound, 0 the data
 * is missing or malformed or the service has no draw module (reported;
 * nothing bound). */
int em_message_presenters_live_install(const char *path);
void em_message_presenters_live_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_MESSAGE_PRESENTERS_LIVE_H */
