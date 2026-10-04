#ifndef EM_AREA01_CAMERA_SERVICES_H
#define EM_AREA01_CAMERA_SERVICES_H
#include "game/em_area01_runtime.h"
/* Existing camera vector leaves over canonical active byte views. No
 * suspend/resume: they only read/write the exact XYZ operands, call no
 * other owner and preserve original overlapping-operand store order.
 * 0 handled, 1 unknown entry, -1 refused span/arguments or worker fault. */
int em_area01_camera_services_call(const EmArea01RuntimeHost *, EmArea01Call *);
#endif
