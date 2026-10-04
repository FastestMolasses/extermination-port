/* AREA01 scene 2 / 35 event binding around the one camera playback core. */
#ifndef EM_AREA01_TIMELINE_H
#define EM_AREA01_TIMELINE_H
#include "game/em_area01_runtime.h"
/* Active canonical views; workers may bracket native state as usual. The
 * resource view supplies the raw loader camera track, sparse boot tables,
 * and shared script-host globals. Unknown scenes/track state fail. */
int em_area01_timeline_call(const EmArea01RuntimeHost *,EmArea01Call *,uint32_t *fault_address);
#endif
