#ifndef EM_PLAYER_MOTOR_H
#define EM_PLAYER_MOTOR_H

#include <stdint.h>

/* The scalar state consumed by original 0017BC40. Speeds are units/tick;
 * rate and blend are the actor's +204/+208 animation outputs. */
typedef struct EmPlayerMotor {
    float speed, target, rate, blend;
    uint8_t mode, substate, tier, gait, obstruction;
} EmPlayerMotor;

/* 0017BC40 over the mirror above (the legacy callbacks of the scenes
 * without an original world). A thin adapter over em_player_motor_0017BC40
 * with the tier tables below, fixed at their EE addresses; a table word
 * outside them (the original indexes past a table at tier 3 / tier 0) leaves
 * the mirror unchanged. */
void em_player_motor_tick(EmPlayerMotor *motor);

/* ---- 0017BC40 over the raw player record --------------------------------
 * The one translation of 0017BC40 (byte-matched C, src/func_0017BC40.c): the
 * +1F0 switch over +38 / +204 / +208 / +1F1 / +25C / +240 / +23F / +314 of
 * the record bytes, the tier tables D_0024886C / D_00248870 / D_00248874 /
 * D_00248880 / D_00248890 read by EE address through `read` (the binder maps
 * the exported span 0x248740..0x248ACC; nothing is embedded for the live
 * path), and the scratch word 0x70003A20 (raw bits) the original writes the
 * blend fraction to before copying it. Every COP1 operation and compare goes
 * through em_ee_float.h. Every table word a case reads is read before its
 * first store, so a word `read` refuses returns -1 with nothing written.
 * Oracle: tools/test_player_loco_workers_reference.py (record bytes and
 * 0x70003A20 against the executed original). */
typedef int (*EmPlayerMotorRead)(void *context, uint32_t address, uint32_t *word);
int em_player_motor_0017BC40(uint8_t *record, EmPlayerMotorRead read, void *context,
                             uint32_t *spad3A20);

/* Non-looping run-stop clip: request with a six-tick blend at frame
 * (length-6), then return through idle's twelve-tick blend. Phase3 is
 * the callback between observing the end flag and idle initialization. */
typedef struct EmPlayerStop {
    unsigned phase, blend_left, frame, last_frame;
} EmPlayerStop;
void em_player_stop_begin(EmPlayerStop *stop, unsigned clip_frames);
void em_player_stop_tick(EmPlayerStop *stop);

/* 0017C440 requests the previous gait tier during a run-stop interruption.
 * Phase2 is the callback that clears the blend flag; normal scalar updates
 * resume on the following callback. */
typedef struct EmPlayerReentry {
    unsigned phase, blend_left, frame;
} EmPlayerReentry;
int em_player_reentry_begin(EmPlayerReentry *reentry, EmPlayerMotor *motor,
                           unsigned gait, unsigned clip_frames);
void em_player_reentry_tick(EmPlayerReentry *reentry, EmPlayerMotor *motor);

#endif
