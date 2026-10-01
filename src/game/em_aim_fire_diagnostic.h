#ifndef EM_AIM_FIRE_DIAGNOSTIC_H
#define EM_AIM_FIRE_DIAGNOSTIC_H
#include <stdlib.h>
#include <string.h>
/* Quarantined integration fixture. The original workers stay out of ordinary
 * play until the complete world/render/camera closure passes end to end. */
static inline int em_aim_fire_diagnostic(void)
{
    const char *enable=getenv("EM_AIM_FIRE_ORIGINAL");
    const char *fixture=getenv("EM_AIM_FIRE_TEST");
    return enable && strcmp(enable,"1")==0 && fixture && *fixture;
}
#endif
