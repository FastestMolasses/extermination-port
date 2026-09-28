/* em_settings.h — the port's switches (docs/PORT_PROFILES.md "Profile switch
 * plumbing"; the registry of every option is docs/LAUNCHER_OPTIONS.md).
 *
 * One struct holds every switch. em_settings_original is the Original
 * profile: every switch at its Original value. The switches are chosen once
 * at launch, before the engine boots; the launcher will set them later, and
 * until then (and for the tests) em_settings_from_env reads them from the
 * environment. Game code reads them through em_settings() and never changes
 * them while a game runs.
 *
 * Built so far:
 *   ps2_disc_drive_timing  LAUNCHER_OPTIONS.md "PS2 disc-drive timing".
 *                          Original value 0: the disc answers at host speed
 *                          (the code is the oracle; PS2 hardware timing is
 *                          not reproduced). 1: the IOP stream backend's
 *                          drive model measured from the PCSX2 recordings
 *                          (docs/IOP_STREAM.md "Drive model"), so voiced
 *                          lines start and end on the PS2's frames.
 *                          Environment: EM_PS2_DISC_DRIVE_TIMING=0|1.
 */
#ifndef EM_SETTINGS_H
#define EM_SETTINGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmSettings {
    uint8_t ps2_disc_drive_timing;
} EmSettings;

/* The Original profile: every switch at its Original value. */
extern const EmSettings em_settings_original;

/* The switches in force (the Original profile until something sets them). */
const EmSettings *em_settings(void);
void em_settings_set(const EmSettings *s);

/* Launch-time selection from the environment (the launcher's stand-in):
 * starts from the Original profile and applies every EM_* switch variable
 * that is set. 0, or -1 with a message on stderr for a malformed value
 * (the settings are then left unchanged). */
int em_settings_from_env(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_SETTINGS_H */
