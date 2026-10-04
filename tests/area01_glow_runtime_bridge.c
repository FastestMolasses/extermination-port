/* Expose only the existing runtime's borrowed sprite boundary and VU owner.
 * Unrelated runtime workers are dead-stripped; no substitute sprite logic. */
#include "game/em_aim_fire_runtime.c"
void fs_glow_reset(void)
{
    memset(&R,0,sizeof R);
    R.render.vu=&R.vu;R.render.vf23_valid=&R.vf23_valid;
}
int fs_glow_valid(void){return R.vf23_valid;}
uint32_t fs_glow_fault(void){return R.render.fault_address;}
