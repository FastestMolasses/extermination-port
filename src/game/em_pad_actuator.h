/* em_pad_actuator.h - the pad actuator block D_00810E40 and its original
 * services on the live path (census L23: the truck's rumbles; frame step I).
 *
 * One storage for the original bytes these routines read and write:
 *   D_00810E40..D_00810E69   the pad block's actuator fields in their
 *                            original layout (+0x04 port word, +0x08 slot
 *                            word, +0x0C libpad state word, +0x10/+0x11/+0x12
 *                            the phase, actuator and ready bytes, +0x14 the
 *                            mode id, +0x16 the active byte, +0x18..+0x1D the
 *                            actuator block, +0x28 the duration halfword).
 *                            Bytes +0x24..+0x27 are the analog bytes, whose
 *                            storage is em_frame's EmPadUnpack (em_input.h);
 *                            they are never read or written here.
 *   D_00810119               the vibration option byte (001AB430 stores 1 at
 *                            boot; only the options screen, outside the
 *                            first level, changes it)
 *   D_0024D6F0               the rumble records (from the user's boot ELF,
 *                            assets/pad_rumble.emrg, tools/export_pad_tables.py)
 *
 * The block starts in the state the native pad reports at step C (em_frame.c
 * frame_input_read: a connected DualShock in libpad state 6 on port 0, slot
 * 0), the state 001B5F40 reaches through its phases 0 -> 1 -> 0 -> 2 -> 4
 * (em_startup_load_gaps.c em_slg_001B5F40: phase 2 stores +0x12 = 1): state
 * word 6, phase 4, actuator byte 1, ready 1, mode id 7. Every PCSX2 capture
 * (startup-reference opening/playable, route beats 04..08) holds exactly
 * these bytes at those offsets.
 *
 * Translations bound here (called, not re-translated):
 *   001B1E20  em_owner_services_001B1E20 (rumble dispatch by record)
 *   001B5B70  em_owner_services_001B5B70 (frame step I countdown)
 *   001B61C0  em_player_rumble_001B61C0 (actuator request; its typed view
 *             EmPlayerRumblePad is loaded from and stored to the block)
 *   001B6250  em_script_host_001B6250 (actuator stop, on the raw block)
 * The libpad actuator write 00111018(port, slot, &block[0x18]) is the platform
 * boundary: em_gamepad_rumble with the two motor bytes, held until the next
 * write (the game's own countdown stops it).
 *
 * Fail-stop: a missing export or a latched services fault makes every entry
 * return -1; nothing is substituted. */
#ifndef EM_PAD_ACTUATOR_H
#define EM_PAD_ACTUATOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_PAD_ACTUATOR_BLOCK 0x00810E40u
#define EM_PAD_ACTUATOR_BLOCK_SIZE 0x2Au
#define EM_PAD_ACTUATOR_TABLES_PATH "assets/pad_rumble.emrg"

/* The block in its original layout (EM_PAD_ACTUATOR_BLOCK_SIZE bytes). */
uint8_t *em_pad_actuator_block(void);
/* 001B6250(&D_00810E40), the actuator stop (001B6BF0's skip landing,
 * census L22): 0, or -1 on a fault (reported). */
int em_pad_actuator_001B6250(uint32_t address);
/* Back to the start state above (game start). */
void em_pad_actuator_reset(void);
/* 001B61C0(big, small, duration, force) over the block (the player states'
 * cue / rumble workers): 0, or -1 on a fault (reported). */
int em_pad_actuator_001B61C0(uint8_t big, uint8_t small, int duration, int force);
/* 001B1E20(effect, duration): 0, or -1 on a fault (reported). */
int em_pad_actuator_001B1E20(int32_t effect, int64_t duration);
/* 001B5B70 at main-loop step I (0x1AAF6C): 0, or -1 on a fault. The frame
 * calls it through em_frame_set_step_i. */
int em_pad_actuator_step_i(void *context);

#ifdef __cplusplus
}
#endif

#endif
