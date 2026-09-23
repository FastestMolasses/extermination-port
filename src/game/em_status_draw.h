/* Original normal-status display workers. All coordinates are the original
 * GS units; the graphics adapter owns canvas conversion and ordered draws. */
#ifndef EM_STATUS_DRAW_H
#define EM_STATUS_DRAW_H

#include <stdint.h>

typedef struct {
    void *context;
    int (*blend)(void *, unsigned mode);
    int (*rectangle)(void *, int x0, int y0, int x1, int y1, uint32_t rgba);
    int (*text)(void *, int proportional, int x, int y, int w, int h, const char *value,
                uint64_t style);
    int (*arc)(void *, const float descriptor[24]);
    int (*sprite)(void *, int x, int y, int w, int h, uint32_t rgba, uint64_t tex0);
} EmStatusDrawWorkers;

typedef struct {
    float arcs[4][24];   /* Original00265390/3F0/450/4B0 records. */
    uint64_t white, red; /* Original00265510/528 text style records. */
    int label_width;     /* Original001CC170 result for the resident label. */
    const char *label;
    const char *warning_max, *normal_max, *separator; /* Original273558/560/568. */
} EmStatusHealthData;

/* Original00208AD0. Advances UI+20 once and emits its actual ordered health
 * gauge/text calls. Text/string resources come from the user's own assets.
 * Returns1 accepted,0 invalid data or a failed renderer worker. The caller
 * must retain the status page on failure, not close or skip the draw. */
int em_status_health_draw(uint32_t *counter, float health, uint8_t warning, int x, int y,
                          const EmStatusHealthData *data, const EmStatusDrawWorkers *workers);

typedef struct {
    uint64_t white;
    const char *label, *separator;
} EmStatusBatteryData;

/* Original00209280: charge/capacity are half-units, equipped is C7F.
 * compact == 0 draws the small labeled grid; other values select the large grid.
 * The caller supplies the original cell TEX0 from its calling record. */
int em_status_battery_draw(uint16_t charge, uint8_t capacity, uint8_t equipped, int x, int y,
                           uint64_t tex0, int compact, const EmStatusBatteryData *data,
                           const EmStatusDrawWorkers *workers);

typedef struct {
    uint64_t white;
    const char *label, *percent;
} EmStatusAmmoData;

typedef struct {
    uint8_t primary, secondary; /* OriginalCA4/CA6 selectors. */
    int16_t amount[5];          /* OriginalCA8/CAA/CAC/CAE/CB0. */
    int16_t reserve;            /* OriginalCB4. */
} EmStatusAmmoInventory;

/* Original00209860: the original signature includes an unused UI argument
 * before x/y. The resident strings/styles and all inventory fields are explicit.
 * Rejects unknown secondary selectors unless primary2 overrides them: original
 * invalid selectors use an incoming saved register as TEX0, not a valid asset. */
int em_status_ammo_draw(const EmStatusAmmoInventory *inventory, int x, int y,
                        const EmStatusAmmoData *data, const EmStatusDrawWorkers *workers);

#endif
