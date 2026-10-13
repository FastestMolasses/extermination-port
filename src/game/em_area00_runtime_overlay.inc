/* AREA00 typed hooks into the shared runtime. No game state or
 * original behavior is owned here. Matching AREA01 boot adapters
 * are reused; every call re-enters the selected dispatcher. */

static int a00_worker_001026A0(void *ctx, uint32_t dst, uint32_t matrix, uint32_t vector)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001026A0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)matrix;
    c.a[2] = (uint64_t)(int64_t)(int32_t)vector;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001028B8(void *ctx, uint32_t dst, uint32_t a, uint32_t b)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001028B8u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a;
    c.a[2] = (uint64_t)(int64_t)(int32_t)b;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001029C0(void *ctx, uint32_t matrix)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001029C0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)matrix;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001281C0(void *ctx, float x, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001281C0u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(x);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a00_worker_00129780(void *ctx, uint32_t actor, uint32_t block, int32_t sel, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00129780u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    c.a[2] = (uint64_t)(int64_t)sel;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a00_worker_00182F90(void *ctx, uint32_t actor, uint32_t point)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00182F90u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)point;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_0019C6F0(void *ctx, int32_t a0, int32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0019C6F0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a00_worker_001AF800(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001AF800u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001B0C60(void *ctx, int32_t a0, int32_t a1, int32_t a2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B0C60u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001B0F60(void *ctx, uint32_t actor, int32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B0F60u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a00_worker_001B1E20(void *ctx, int32_t a0, int32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1E20u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001B6660(void *ctx, uint32_t record, uint32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B6660u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)record;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (uint32_t)c.v0;
    return rc;
}

static int a00_worker_001C2770(void *ctx, uint32_t actor, uint32_t block, int32_t a2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C2770u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a00_worker_001C3D60(void *ctx, uint32_t actor, uint32_t block)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C3D60u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001C5570(void *ctx, uint32_t actor, uint32_t vector, int32_t a2, int32_t a3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C5570u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)vector;
    c.a[2] = (uint64_t)(int64_t)a2;
    c.a[3] = (uint64_t)(int64_t)a3;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001C69A0(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C69A0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001CB5B0(void *ctx, int32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CB5B0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001CCF70(void *ctx, uint32_t point, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CCF70u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)point;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a00_worker_001CD520(void *ctx, int32_t bucket, int32_t mode, uint32_t point, uint64_t giftag, uint32_t rgba, float w, float h, float zbias, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CD520u, .sp = r->sp, .na = 5, .nf = 3};
    c.a[0] = (uint64_t)(int64_t)bucket;
    c.a[1] = (uint64_t)(int64_t)mode;
    c.a[2] = (uint64_t)(int64_t)(int32_t)point;
    c.a[3] = giftag;
    c.a[4] = (uint64_t)(int64_t)(int32_t)rgba;
    c.f[0] = em_ee_bits(w);
    c.f[1] = em_ee_bits(h);
    c.f[2] = em_ee_bits(zbias);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a00_worker_001CFA60(void *ctx, uint32_t packet, uint32_t matrix, float f12, float f13)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CFA60u, .sp = r->sp, .na = 2, .nf = 2};
    c.a[0] = (uint64_t)(int64_t)(int32_t)packet;
    c.a[1] = (uint64_t)(int64_t)(int32_t)matrix;
    c.f[0] = em_ee_bits(f12);
    c.f[1] = em_ee_bits(f13);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001CFBE0(void *ctx, int32_t a0, int32_t a1, uint32_t a2, uint32_t a3, int32_t t0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CFBE0u, .sp = r->sp, .na = 5, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    c.a[2] = (uint64_t)(int64_t)(int32_t)a2;
    c.a[3] = (uint64_t)(int64_t)(int32_t)a3;
    c.a[4] = (uint64_t)(int64_t)t0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001EFD20(void *ctx, int32_t a0, uint32_t point)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001EFD20u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)point;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001F4E20(void *ctx, uint32_t point, uint32_t colour, float f12)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001F4E20u, .sp = r->sp, .na = 2, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)point;
    c.a[1] = (uint64_t)(int64_t)(int32_t)colour;
    c.f[0] = em_ee_bits(f12);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001F5940(void *ctx, int32_t a0, uint32_t point, int32_t a2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001F5940u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)point;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a00_worker_001FB9F0(void *ctx, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FB9F0u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)id;
    c.a[1] = (uint64_t)(int64_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    c.a[3] = (uint64_t)(int64_t)a3;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static void overlay00_workers(EmArea01Runtime *r)
{
    r->overlay00.w_001026A0 = a00_worker_001026A0;
    r->overlay00.w_001028B8 = a00_worker_001028B8;
    r->overlay00.w_001028D0 = ovl_001028D0;
    r->overlay00.w_00102948 = ovl_00102948;
    r->overlay00.w_00102958 = ovl_00102958;
    r->overlay00.w_001029C0 = a00_worker_001029C0;
    r->overlay00.w_00122BB8 = ovl_00122BB8;
    r->overlay00.w_001281C0 = a00_worker_001281C0;
    r->overlay00.w_00129780 = a00_worker_00129780;
    r->overlay00.w_00182F90 = a00_worker_00182F90;
    r->overlay00.w_0019C6F0 = a00_worker_0019C6F0;
    r->overlay00.w_001A2370 = ovl_001A2370;
    r->overlay00.w_001AF800 = a00_worker_001AF800;
    r->overlay00.w_001AFC10 = ovl_001AFC10;
    r->overlay00.w_001B0C60 = a00_worker_001B0C60;
    r->overlay00.w_001B0F60 = a00_worker_001B0F60;
    r->overlay00.w_001B0FD0 = ovl_001B0FD0;
    r->overlay00.w_001B10B0 = ovl_001B10B0;
    r->overlay00.w_001B17A0 = ovl_001B17A0;
    r->overlay00.w_001B1B70 = ovl_001B1B70;
    r->overlay00.w_001B1E20 = a00_worker_001B1E20;
    r->overlay00.w_001B6660 = a00_worker_001B6660;
    r->overlay00.w_001BA1A0 = ovl_001BA1A0;
    r->overlay00.w_001BA1C0 = ovl_001BA1C0;
    r->overlay00.w_001BA1F0 = ovl_001BA1F0;
    r->overlay00.w_001BBDA0 = ovl_001BBDA0;
    r->overlay00.w_001BBE40 = ovl_001BBE40;
    r->overlay00.w_001BC0E0 = ovl_001BC0E0;
    r->overlay00.w_001BC240 = ovl_001BC240;
    r->overlay00.w_001BC290 = ovl_001BC290;
    r->overlay00.w_001BC300 = ovl_001BC300;
    r->overlay00.w_001C2770 = a00_worker_001C2770;
    r->overlay00.w_001C3D60 = a00_worker_001C3D60;
    r->overlay00.w_001C4760 = ovl_001C4760;
    r->overlay00.w_001C5570 = a00_worker_001C5570;
    r->overlay00.w_001C6380 = ovl_001C6380;
    r->overlay00.w_001C63E0 = ovl_001C63E0;
    r->overlay00.w_001C64F0 = ovl_001C64F0;
    r->overlay00.w_001C67E0 = ovl_001C67E0;
    r->overlay00.w_001C68C0 = ovl_001C68C0;
    r->overlay00.w_001C69A0 = a00_worker_001C69A0;
    r->overlay00.w_001CB5B0 = a00_worker_001CB5B0;
    r->overlay00.w_001CCF70 = a00_worker_001CCF70;
    r->overlay00.w_001CD520 = a00_worker_001CD520;
    r->overlay00.w_001CFA60 = a00_worker_001CFA60;
    r->overlay00.w_001CFBE0 = a00_worker_001CFBE0;
    r->overlay00.w_001EFD20 = a00_worker_001EFD20;
    r->overlay00.w_001F4E20 = a00_worker_001F4E20;
    r->overlay00.w_001F5940 = a00_worker_001F5940;
    r->overlay00.w_001FB9F0 = a00_worker_001FB9F0;
    r->overlay00.w_001FC3C0 = ovl_001FC3C0;
}
