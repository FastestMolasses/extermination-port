/* AREA11 event-manager gates, recovered from runtime 0x008253F0..8257A0.
 * The polygons are loaded from the user's local overlay export.  They
 * are not bounding boxes: the second trigger is a slanted quadrilateral.
 * This is only the event-selection layer; camera/script execution remains
 * the responsibility of the director. */
#ifndef EM_AREA11_FLOW_H
#define EM_AREA11_FLOW_H

#include <stdint.h>

typedef struct {
    float polygon[3][4][2]; /* X,Z in original vertex order */
} EmArea11Triggers;

int em_area11_triggers_load(EmArea11Triggers *triggers, const char *path);
int em_area11_beat_for_step(uint8_t step);
int em_area11_trigger_contains(const EmArea11Triggers *triggers,
                              int beat, const float position[3]);
uint8_t em_area11_step_after_beat(int beat);

#endif
