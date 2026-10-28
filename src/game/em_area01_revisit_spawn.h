/* The AREA01 revisit's op14 placement nodes (a01r_01 f1139): script
 * 0x82AD90's op14 001BAC00 places group 0x82A900, whose three 0x270E
 * entries spawn through 001C8140 a class-0xC node running 001C7EB0, which
 * calls the entry's +0x28 behaviour (0x8237D0 / 0x823900 / 0x8239C0) through
 * its +0x2EC every frame.
 *
 * Translations of the byte-matched decomp C (src/func_001C8140.c,
 * src/func_001C7EB0.c), over the AREA01 runtime's host: every original byte
 * through `bytes`, every callee (001AFA90, 001AF780, 001CB5B0, 001C63E0,
 * 001C64F0, 001C68C0, 001AFC10 and the +0x2EC behaviour) through `worker`
 * by its original address. They own no state. A refused byte or a failing
 * callee stops the routine there: -1 with *fault = the address.
 *
 * Bound in em_area01_runtime's revisit dispatch (001BAC00's 0x270E entries
 * and the nodes' +0x10); checked against the original instructions by
 * tools/test_area01_revisit_spawn_reference.py and, through the runtime, by
 * tools/test_area01_revisit_runtime_reference.py.
 */
#ifndef EM_AREA01_REVISIT_SPAWN_H
#define EM_AREA01_REVISIT_SPAWN_H
#include "game/em_area01_runtime.h"

/* 001C8140(bank, index, behaviour): 001AFA90(0xC); when it gives a node,
 * +0x03 = 0xC, +0x0D = 0x63, +0x94 = index (halfword), +0x40 = bank,
 * +0x10 = 001C7EB0, +0x24 = 0, +0x2E8 = 0.5, +0x2EC = behaviour. *v0: the
 * allocation's register (the original leaves it in v0). */
int em_area01_revisit_spawn_001C8140(const EmArea01RuntimeHost *h, uint32_t sp, uint32_t bank, int16_t index,
                                     uint32_t behaviour, uint64_t *v0, uint32_t *fault);

/* 001C7EB0(self), the node's behaviour: +0x04 0 binds one bone slot and its
 * pose (calling +0x2EC once), 1 advances the clip and follows the shared pose
 * D_00275B40 (calling +0x2EC as the decomp C says), 2 / 3 free it. */
int em_area01_revisit_spawn_001C7EB0(const EmArea01RuntimeHost *h, uint32_t sp, uint32_t self, uint32_t *fault);
#endif
