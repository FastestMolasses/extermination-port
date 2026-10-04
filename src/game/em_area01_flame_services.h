/* Flame projection/build services over existing render and effect owners. */
#ifndef EM_AREA01_FLAME_SERVICES_H
#define EM_AREA01_FLAME_SERVICES_H
#include "game/em_area01_runtime.h"

int em_area01_flame_services_handles(uint32_t function);
/* CD070/CD2B0/CFAE0/F4A10/E9E60/F4CC0/F4BF0/CD520 run inside an active
 * canonical byte transaction. F4CC0/F4BF0 forward reached workers through
 * host.worker; CD520 borrows existing aim/RCL owners (runtime must be attached). No
 * mutable game state is retained here. 0 handled, 1 other, -1 refusal. */
int em_area01_flame_services_call(const EmArea01RuntimeHost *, EmArea01Call *, uint32_t *fault);

typedef struct {
    int32_t key, kind, copy;
    uint32_t source;
    uint8_t descriptor[0x90], transform[0x60];
} EmArea01FlamePacket;
/* CFBE0: prepare while views are active, suspend native transactions,
 * invoke, then resume even after failure. Only call-local inputs are copied;
 * the effect owner writes its actual RCL packet/chain storage. */
int em_area01_flame_packet_prepare(const EmArea01RuntimeHost *, const EmArea01Call *,
                                   EmArea01FlamePacket *, uint32_t *fault);
int em_area01_flame_packet_invoke(const EmArea01FlamePacket *, EmArea01Call *);
#endif
