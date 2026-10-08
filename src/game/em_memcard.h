/* em_memcard.h - the memory card: the platform boundary under the
 * memory-card screen 00225AC0 (docs/OPTIONS.md section 5).
 *
 * The original reaches the card through the SDK's libmc over SIF RPC to
 * the IOP's card server: 00114988 issues GetInfo(port, slot, &type,
 * &free, &format) (its RPC end callback 00114930 stores the three words),
 * 00114848 is Sync(mode, &cmd, &result). The port answers them from the
 * host, at host speed (the IOP and the card answer within the frame, as
 * the recordings show them: opt_07 and dmg_05 see the type word stored on
 * the frame GetInfo is issued and the result on the next Sync):
 *
 *   port 0 (slot 1 on the screen) is the host directory data/memcard/slot1,
 *   port 1 (slot 2) data/memcard/slot2 (relative to the working directory:
 *   the macOS app's directory, the iOS app's Documents). A directory holds
 *   the card's files as the game writes them, in the original's data
 *   layout (the card's root: the game's save directory and its files, byte
 *   for byte); the first level writes and reads none (docs/OPTIONS.md
 *   section 1: it has no save path, and its load row stops at the slot
 *   choice).
 *
 * GetInfo: type 2 (a formatted PS2 card) for a slot whose directory
 * exists; the result is 0 (the same card as the port's last GetInfo) once
 * the port has been asked since the boot, else -1 (a formatted card new to
 * the port). The boot's card check (the startup's 001AB9D0 state 3, the
 * native stand-in em_frontend's) creates both directories and asks both
 * ports, as the original's boot check 0022A460 does: the recordings' GetInfo
 * results in the options' load row are 0 on both ports. A free or format
 * pointer (no first-level caller passes one), a slot other than 0, a port
 * other than 0 / 1 or a missing directory faults: no recording shows what
 * the card server returns then. The store of a result goes to the caller
 * (the binder writes the words at the addresses the original passes).
 *
 * One storage for the card helpers' globals 001FE9A0 / 001FECB0 / 001FE920
 * / 001FE8D0 read and write: D_00275C58..D_00275C6F, D_00264E30..D_00264E3B
 * and D_00275840..D_00275847 (em_memcard_globals). */
#ifndef EM_MEMCARD_H
#define EM_MEMCARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_MEMCARD_DIR "data/memcard"

/* A word the SDK stores through a pointer its caller passed. */
typedef struct {
    uint32_t address;
    int32_t value;
} EmMemcardStore;

/* The boot's card check (em_frontend's stand-in for 001AB9D0 state 3):
 * creates data/memcard/slot1 and slot2 and asks both ports (GetInfo's
 * known state). 1 ok, 0 a directory could not be made. */
int em_memcard_boot_check(void);

/* The bytes of the card helpers' globals at an original address (see the
 * header), or NULL outside them. */
uint8_t *em_memcard_globals(uint32_t address, uint32_t size);

/* 00114988(port, slot, type, free, format): *v0 the original's return
 * (0 issued; the pending command when one is pending); stores[*n] the
 * words the RPC end callback stores (at most 3). 0, or -1 on a fault
 * (reported). */
int em_memcard_00114988(int32_t port, int32_t slot, uint32_t type, uint32_t free_words, uint32_t format,
                        int32_t *v0, EmMemcardStore stores[3], unsigned *n);
/* 00114848(mode, cmd, result): *v0 -1 (no command), 0 (busy; never at host
 * speed) or 1 (done: the command and result stored). 0, or -1 on a fault. */
int em_memcard_00114848(int32_t mode, uint32_t cmd, uint32_t result, int32_t *v0, EmMemcardStore stores[2],
                        unsigned *n);

/* Back to the boot state (no command pending, no port asked, the globals
 * as the boot leaves them). */
void em_memcard_reset(void);
/* The last fault text, or NULL. */
const char *em_memcard_fault(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_MEMCARD_H */
