/* Composition of the existing AREA01 owner translations. The host owns
 * every byte and every shared worker; missing mappings/workers fail. */
#ifndef EM_AREA01_RUNTIME_H
#define EM_AREA01_RUNTIME_H
#include "game/em_area01_math_core.h"
#include "game/em_area01_overlay.h"
#include "game/em_area01_sys.h"
#include "game/em_area01_exita.h"
#include "game/em_area01_exitb.h"
#include "game/em_area01_room.h"
#include "game/em_area01_side.h"

typedef struct {
    uint32_t function, sp;
    uint64_t a[8];
    uint32_t f[8], na, nf;
    uint64_t v0;
    uint32_t f0;
} EmArea01Call;

typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *, uint32_t address, uint32_t size, int write);
    /* A shared native worker, with the argument lanes the caller supplied.
     * The host commits serialized actors before native access and refreshes
     * them afterwards. The supplied stack span must remain valid throughout
     * a nested call. Return 0 on success, -1 on a missing/failing worker. */
    int (*worker)(void *, EmArea01Call *);
} EmArea01RuntimeHost;

typedef struct {
    EmArea01RuntimeHost host;
    EmA01Math math;
    EmArea01OvlHooks overlay;
    EmArea01OvlFault overlay_fault;
    EmArea01Sys sys;
    EmArea01Exita exita;
    EmArea01Exitb exitb;
    EmArea01Room room;
    EmArea01Side side;
    uint32_t sp, fault_address;
    int fault;
} EmArea01Runtime;

/* Bind a fresh/reset runtime. It allocates and seeds no original memory. */
int em_area01_runtime_bind(EmArea01Runtime *r, const EmArea01RuntimeHost *host);
/* Named original entry or worker; recursive module calls use this same
 * dispatcher. The first failure stays latched until a new binding. */
int em_area01_runtime_call(EmArea01Runtime *r, EmArea01Call *call);
#endif
