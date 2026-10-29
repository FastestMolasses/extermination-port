/* Composition of existing shared and selected AREA00/01 translations. The host owns
 * every byte and every shared worker; missing mappings/workers fail. */
#ifndef EM_AREA01_RUNTIME_H
#define EM_AREA01_RUNTIME_H
#include "game/em_area01_math_core.h"
#include "game/em_area01_overlay.h"
#include "game/em_area00_overlay.h"
#include "game/em_area01_sys.h"
#include "game/em_area01_exita.h"
#include "game/em_area01_exitb.h"
#include "game/em_area01_room.h"
#include "game/em_area01_side.h"
#include "game/em_area01_revisit.h"
#include "game/em_area00_return.h"
#include "game/em_area02_overlay.h"

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
    EmArea00OvlHooks overlay00;
    EmArea00OvlFault overlay00_fault;
    EmArea01Sys sys;
    EmArea01Exita exita;
    EmArea01Exitb exitb;
    EmArea01Room room;
    EmArea01Side side;
    /* The AREA01 revisit owners and the AREA01 routines of the AREA00
     * return (em_area01_runtime_revisit.inc). */
    EmArea01RevisitHooks revisit;
    EmArea01RevisitFault revisit_fault;
    EmArea00ReturnHooks ret;
    EmArea00ReturnFault ret_fault;
    /* AREA02's overlay owners (em_area02_overlay; em_area02_runtime_overlay.inc). */
    EmArea02OvlHooks overlay02;
    EmArea02OvlFault overlay02_fault;
    uint32_t sp, fault_address;
    int fault;
    unsigned area;
} EmArea01Runtime;

/* Bind a fresh/reset runtime. It allocates and seeds no original memory.
 * The original entry defaults to AREA01; bind_area accepts 0 (AREA00), 1
 * (AREA01) and 2 (AREA02). */
int em_area01_runtime_bind(EmArea01Runtime *r, const EmArea01RuntimeHost *host);
int em_area01_runtime_bind_area(EmArea01Runtime *r, const EmArea01RuntimeHost *host, unsigned area);
/* Named original entry or worker; recursive module calls use this same
 * dispatcher. The first failure stays latched until a new binding. */
int em_area01_runtime_call(EmArea01Runtime *r, EmArea01Call *call);
#endif
