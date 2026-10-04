/* Typed AREA01 overlay worker adapters. No game state is owned here. */
static int ovl_00102760(void *ctx, uint32_t dst, uint32_t src)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102760u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001028D0(void *ctx, uint32_t dst, uint32_t a, uint32_t b)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001028D0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a;
    c.a[2] = (uint64_t)(int64_t)(int32_t)b;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_00102948(void *ctx, uint32_t dst, uint32_t src)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102948u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_00102958(void *ctx, uint32_t dst, uint32_t src)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102958u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_0011DBB8(void *ctx, float x, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011DBB8u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(x);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int ovl_0011DF78(void *ctx, float x, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011DF78u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(x);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int ovl_0011E2A8(void *ctx, float x, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011E2A8u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(x);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int ovl_0011E520(void *ctx, float x, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011E520u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(x);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int ovl_0011E748(void *ctx, float x, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011E748u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(x);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int ovl_00122BB8(void *ctx, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00122BB8u, .sp = r->sp, .na = 0, .nf = 0};
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_00182BF0(void *ctx, uint32_t actor, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00182BF0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_0019B6C0(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0019B6C0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)(int32_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001A2370(void *ctx, uint32_t actor, uint32_t matrix)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001A2370u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)matrix;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001AFA90(void *ctx, int32_t cls, uint32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001AFA90u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)cls;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (uint32_t)c.v0;
    return rc;
}
static int ovl_001AFC10(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001AFC10u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001B0FD0(void *ctx, uint32_t actor, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B0FD0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001B10B0(void *ctx, uint32_t actor, int32_t a1, int32_t a2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B10B0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001B1190(void *ctx, int32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1190u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001B11E0(void *ctx, int32_t a0, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B11E0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001B1240(void *ctx, uint32_t origin, float x, float z, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1240u, .sp = r->sp, .na = 1, .nf = 2};
    c.a[0] = (uint64_t)(int64_t)(int32_t)origin;
    c.f[0] = em_ee_bits(x);
    c.f[1] = em_ee_bits(z);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int ovl_001B12B0(void *ctx, float goal, float current, float rate, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B12B0u, .sp = r->sp, .na = 0, .nf = 3};
    c.f[0] = em_ee_bits(goal);
    c.f[1] = em_ee_bits(current);
    c.f[2] = em_ee_bits(rate);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int ovl_001B1380(void *ctx, uint32_t a0, uint32_t a1, float f12, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1380u, .sp = r->sp, .na = 2, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.f[0] = em_ee_bits(f12);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001B1470(void *ctx, float angle, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1470u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(angle);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int ovl_001B17A0(void *ctx, uint32_t actor, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B17A0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001B1B70(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1B70u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001B1EA0(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1EA0u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)(int32_t)a2;
    c.a[3] = (uint64_t)(int64_t)a3;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001BA1A0(void *ctx, uint32_t block, uint32_t script)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA1A0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)block;
    c.a[1] = (uint64_t)(int64_t)(int32_t)script;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001BA1C0(void *ctx, uint32_t actor, int32_t index, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA1C0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)index;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001BA1F0(void *ctx, uint32_t actor, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA1F0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001BA540(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA540u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001BA580(void *ctx, uint32_t actor, int32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA580u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001BA8E0(void *ctx, uint32_t actor, int32_t type)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA8E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)type;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001BBDA0(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BBDA0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001BBE40(void *ctx, uint32_t actor, uint32_t block, int32_t mode, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BBE40u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    c.a[2] = (uint64_t)(int64_t)mode;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001BC0E0(void *ctx, uint32_t actor, uint32_t block, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BC0E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001BC240(void *ctx, uint32_t actor, uint32_t block)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BC240u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001BC290(void *ctx, uint32_t actor, uint32_t block, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BC290u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001BC300(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BC300u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001C4760(void *ctx, int32_t a0, int32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C4760u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001C4820(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C4820u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001C5C90(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C5C90u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001C6380(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C6380u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001C63E0(void *ctx, uint32_t actor, int32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C63E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001C64F0(void *ctx, uint32_t actor, float dt, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C64F0u, .sp = r->sp, .na = 1, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.f[0] = em_ee_bits(dt);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001C67E0(void *ctx, uint32_t actor, int32_t clip, float blend, float frame)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C67E0u, .sp = r->sp, .na = 2, .nf = 2};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)clip;
    c.f[0] = em_ee_bits(blend);
    c.f[1] = em_ee_bits(frame);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001C68C0(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C68C0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001EFD90(void *ctx, int32_t id, uint32_t position, uint32_t rotation, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001EFD90u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)id;
    c.a[1] = (uint64_t)(int64_t)(int32_t)position;
    c.a[2] = (uint64_t)(int64_t)(int32_t)rotation;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001EFE00(void *ctx, int32_t a0, uint32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001EFE00u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001FBD50(void *ctx, uint32_t actor, int32_t cue, int32_t a2, float f12, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FBD50u, .sp = r->sp, .na = 3, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)cue;
    c.a[2] = (uint64_t)(int64_t)a2;
    c.f[0] = em_ee_bits(f12);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_001FC3C0(void *ctx, uint32_t actor, uint32_t handle, int32_t cue, float f12, float f13)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FC3C0u, .sp = r->sp, .na = 3, .nf = 2};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)handle;
    c.a[2] = (uint64_t)(int64_t)cue;
    c.f[0] = em_ee_bits(f12);
    c.f[1] = em_ee_bits(f13);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_001FC520(void *ctx, uint32_t handle)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FC520u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)handle;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int ovl_008282F0(void *ctx, uint32_t actor, uint32_t matrix, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x008282F0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)matrix;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int ovl_008287C0(void *ctx, uint32_t matrix)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x008287C0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)matrix;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static void overlay_workers(EmArea01Runtime *r)
{
    r->overlay.w_00102760 = ovl_00102760;
    r->overlay.w_001028D0 = ovl_001028D0;
    r->overlay.w_00102948 = ovl_00102948;
    r->overlay.w_00102958 = ovl_00102958;
    r->overlay.w_0011DBB8 = ovl_0011DBB8;
    r->overlay.w_0011DF78 = ovl_0011DF78;
    r->overlay.w_0011E2A8 = ovl_0011E2A8;
    r->overlay.w_0011E520 = ovl_0011E520;
    r->overlay.w_0011E748 = ovl_0011E748;
    r->overlay.w_00122BB8 = ovl_00122BB8;
    r->overlay.w_00182BF0 = ovl_00182BF0;
    r->overlay.w_0019B6C0 = ovl_0019B6C0;
    r->overlay.w_001A2370 = ovl_001A2370;
    r->overlay.w_001AFA90 = ovl_001AFA90;
    r->overlay.w_001AFC10 = ovl_001AFC10;
    r->overlay.w_001B0FD0 = ovl_001B0FD0;
    r->overlay.w_001B10B0 = ovl_001B10B0;
    r->overlay.w_001B1190 = ovl_001B1190;
    r->overlay.w_001B11E0 = ovl_001B11E0;
    r->overlay.w_001B1240 = ovl_001B1240;
    r->overlay.w_001B12B0 = ovl_001B12B0;
    r->overlay.w_001B1380 = ovl_001B1380;
    r->overlay.w_001B1470 = ovl_001B1470;
    r->overlay.w_001B17A0 = ovl_001B17A0;
    r->overlay.w_001B1B70 = ovl_001B1B70;
    r->overlay.w_001B1EA0 = ovl_001B1EA0;
    r->overlay.w_001BA1A0 = ovl_001BA1A0;
    r->overlay.w_001BA1C0 = ovl_001BA1C0;
    r->overlay.w_001BA1F0 = ovl_001BA1F0;
    r->overlay.w_001BA540 = ovl_001BA540;
    r->overlay.w_001BA580 = ovl_001BA580;
    r->overlay.w_001BA8E0 = ovl_001BA8E0;
    r->overlay.w_001BBDA0 = ovl_001BBDA0;
    r->overlay.w_001BBE40 = ovl_001BBE40;
    r->overlay.w_001BC0E0 = ovl_001BC0E0;
    r->overlay.w_001BC240 = ovl_001BC240;
    r->overlay.w_001BC290 = ovl_001BC290;
    r->overlay.w_001BC300 = ovl_001BC300;
    r->overlay.w_001C4760 = ovl_001C4760;
    r->overlay.w_001C4820 = ovl_001C4820;
    r->overlay.w_001C5C90 = ovl_001C5C90;
    r->overlay.w_001C6380 = ovl_001C6380;
    r->overlay.w_001C63E0 = ovl_001C63E0;
    r->overlay.w_001C64F0 = ovl_001C64F0;
    r->overlay.w_001C67E0 = ovl_001C67E0;
    r->overlay.w_001C68C0 = ovl_001C68C0;
    r->overlay.w_001EFD90 = ovl_001EFD90;
    r->overlay.w_001EFE00 = ovl_001EFE00;
    r->overlay.w_001FBD50 = ovl_001FBD50;
    r->overlay.w_001FC3C0 = ovl_001FC3C0;
    r->overlay.w_001FC520 = ovl_001FC520;
    r->overlay.w_008282F0 = ovl_008282F0;
    r->overlay.w_008287C0 = ovl_008287C0;
}
