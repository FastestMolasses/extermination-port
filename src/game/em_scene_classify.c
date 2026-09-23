#include "game/em_scene_classify.h"

/* 001AE7E0 (byte-matched). The test order is the original's; do not reorder:
 * CE and C5/B0 are tested before D_0028A9A0 and 3B8D, so screen and status
 * requests win during fades and cutscenes. */
int em_sf_001AE7E0(const EmSceneState *s, int16_t d0028A9A0)
{
    if (s->req[EM_SCENE_REQ_B8] != 0)
        return 0;
    if (s->req[EM_SCENE_REQ_B9] != 0)
        return 0;
    if (s->req[EM_SCENE_REQ_CE] != 0)
        return 3;
    if (s->req[EM_SCENE_REQ_C5] != 0 || s->req[EM_SCENE_REQ_B0] != 0)
        return 2;
    if (d0028A9A0 != 0)
        return 0;
    if (s->spad3B8D != 0)
        return 0;
    if ((s->d810E74 & 0x100) != 0 || s->d810E50 != 4)
        return 1;
    if (s->req[EM_SCENE_REQ_B3] != 0)
        return 0;
    if ((s->d810E74 & 0x800) != 0 || (s->d810E74 & 0x10) != 0)
        return 2;
    return 0;
}

/* Lead decision Q1 (design section 9): not original behaviour, see header. */
int em_scene_classify_q1(const EmSceneState *s, int16_t d0028A9A0, int *select_withheld)
{
    EmSceneState view = *s;
    view.d810E74 = em_scene_q1_classifier_e74(s->d810E74);
    if (select_withheld)
        *select_withheld = (s->d810E74 & EM_SCENE_Q1_SELECT) != 0;
    return em_sf_001AE7E0(&view, d0028A9A0);
}
