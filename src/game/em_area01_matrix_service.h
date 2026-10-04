/* Borrowed inputs for the shared single-matrix upload owner. */
#ifndef EM_AREA01_MATRIX_SERVICE_H
#define EM_AREA01_MATRIX_SERVICE_H
#include "game/em_area01_runtime.h"
typedef struct {
    uint32_t matrix[16], token;
    uint8_t token_bytes[16];
    int32_t vuaddr, channel;
} EmArea01MatrixService;
/* Prepare inside the active byte transaction, suspend native views, invoke,
 * then resume even on failure. Only these call-local read inputs are copied;
 * all lighting, scratch and packet writes use the existing live owners.
 * Return 0 handled, 1 another function, -1 refusal; latch the first fault. */
int em_area01_matrix_service_prepare(const EmArea01RuntimeHost *, const EmArea01Call *,
                                    EmArea01MatrixService *, uint32_t *fault);
int em_area01_matrix_service_invoke(const EmArea01MatrixService *, EmArea01Call *);
/* F4A10 page bookkeeping only: read the channel-3 cursor before running
 * the original caller, then finish after its successful original CB760.
 * No transaction suspension is needed: finish borrows immutable REF data
 * plus existing RCL bytes and writes only the shared parsed-unit cache. */
int em_area01_matrix_service_page_begin(uint32_t *start, uint32_t *fault);
int em_area01_matrix_service_page_finish(const EmArea01RuntimeHost *, uint32_t start, uint32_t *fault);
#endif
