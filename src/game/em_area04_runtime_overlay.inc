/* AREA04 typed hooks into the shared runtime (em_area04_port.h's
 * EmArea04PortHooks): each re-enters em_area01_runtime_call by its original
 * address with the argument registers the original sets (the register map
 * of tools/test_area04_port_reference.py's HOOKS, which checks them against
 * the original instructions). No game state or behaviour is owned here.
 * Generated from that table; one adapter per hook. */

static int a04_w_0011DE90(void *ctx, float f0, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011DE90u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(f0);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}

static int a04_w_0011DF78(void *ctx, float f0, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011DF78u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(f0);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}

static int a04_w_0011E2A8(void *ctx, float f0, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011E2A8u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(f0);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}

static int a04_w_0011E748(void *ctx, float f0, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011E748u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(f0);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}

static int a04_w_001028D0(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001028D0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)(int32_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_00102900(void *ctx, uint32_t a0, uint32_t a1, float f2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102900u, .sp = r->sp, .na = 2, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.f[0] = em_ee_bits(f2);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_00102948(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102948u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_00121870(void *ctx, uint32_t a0, uint32_t a1, int32_t n2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00121870u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)n2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_00122BB8(void *ctx, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00122BB8u, .sp = r->sp, .na = 0, .nf = 0};
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_00182F90(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00182F90u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_00183010(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00183010u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_00187EC0(void *ctx, int32_t n0, int32_t n1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00187EC0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)n0;
    c.a[1] = (uint64_t)(int64_t)n1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001A2370(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001A2370u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001AF890(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001AF890u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001AFC10(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001AFC10u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001B0FD0(void *ctx, uint32_t a0, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B0FD0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001B10B0(void *ctx, uint32_t a0, int32_t n1, int32_t n2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B10B0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)n1;
    c.a[2] = (uint64_t)(int64_t)n2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001B1240(void *ctx, uint32_t a0, float f1, float f2, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1240u, .sp = r->sp, .na = 1, .nf = 2};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.f[0] = em_ee_bits(f1);
    c.f[1] = em_ee_bits(f2);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}

static int a04_w_001B1470(void *ctx, float f0, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1470u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(f0);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}

static int a04_w_001B17A0(void *ctx, uint32_t a0, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B17A0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001B1B70(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1B70u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001B1DE0(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1DE0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001B1E20(void *ctx, int32_t n0, int32_t n1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1E20u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)n0;
    c.a[1] = (uint64_t)(int64_t)n1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001B1EA0(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1EA0u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)n0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)(int32_t)a2;
    c.a[3] = (uint64_t)(int64_t)n3;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001B6660(void *ctx, uint32_t a0, uint32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B6660u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (uint32_t)c.v0;
    return rc;
}

static int a04_w_001B6F80(void *ctx, uint32_t a0, float f1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B6F80u, .sp = r->sp, .na = 1, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.f[0] = em_ee_bits(f1);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001BA1A0(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA1A0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001BA1C0(void *ctx, uint32_t a0, int32_t n1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA1C0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)n1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001BA1F0(void *ctx, uint32_t a0, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA1F0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001BA580(void *ctx, uint32_t a0, int32_t n1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA580u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)n1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001BB520(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BB520u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001BB560(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BB560u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)n2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001BB7C0(void *ctx, uint32_t a0, uint32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BB7C0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001BB7F0(void *ctx, uint32_t a0, uint32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BB7F0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001BC150(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BC150u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001C4760(void *ctx, int32_t n0, int32_t n1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C4760u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)n0;
    c.a[1] = (uint64_t)(int64_t)n1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001C47A0(void *ctx, int32_t n0, int32_t n1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C47A0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)n0;
    c.a[1] = (uint64_t)(int64_t)n1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001C5570(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, uint32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C5570u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)n2;
    c.a[3] = (uint64_t)(int64_t)n3;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (uint32_t)c.v0;
    return rc;
}

static int a04_w_001C6120(void *ctx, uint32_t a0, int32_t n1, uint32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C6120u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)n1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (uint32_t)c.v0;
    return rc;
}

static int a04_w_001C62C0(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C62C0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001C6380(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C6380u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001C63E0(void *ctx, uint32_t a0, int32_t n1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C63E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)n1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001C64F0(void *ctx, uint32_t a0, float f1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C64F0u, .sp = r->sp, .na = 1, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.f[0] = em_ee_bits(f1);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}

static int a04_w_001C67E0(void *ctx, uint32_t a0, int32_t n1, float f2, float f3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C67E0u, .sp = r->sp, .na = 2, .nf = 2};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)n1;
    c.f[0] = em_ee_bits(f2);
    c.f[1] = em_ee_bits(f3);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001C68C0(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C68C0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001CA5E0(void *ctx, uint32_t a0, uint32_t a1, int32_t n2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CA5E0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)n2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001CA6E0(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CA6E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001D0C80(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001D0C80u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001D0D40(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001D0D40u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)n2;
    c.a[3] = (uint64_t)(int64_t)n3;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001D0D60(void *ctx, uint32_t a0, float f1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001D0D60u, .sp = r->sp, .na = 1, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.f[0] = em_ee_bits(f1);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001EFD20(void *ctx, int32_t n0, uint32_t a1, uint32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001EFD20u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)n0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (uint32_t)c.v0;
    return rc;
}

static int a04_w_001F4BF0(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001F4BF0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001FABB0(void *ctx)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FABB0u, .sp = r->sp, .na = 0, .nf = 0};
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001FAE70(void *ctx, int32_t n0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FAE70u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)n0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001FB0B0(void *ctx, int32_t n0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FB0B0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)n0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001FB9F0(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FB9F0u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)n0;
    c.a[1] = (uint64_t)(int64_t)n1;
    c.a[2] = (uint64_t)(int64_t)n2;
    c.a[3] = (uint64_t)(int64_t)n3;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001FBD50(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FBD50u, .sp = r->sp, .na = 3, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)n1;
    c.a[2] = (uint64_t)(int64_t)n2;
    c.f[0] = em_ee_bits(f3);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001BA540(void *ctx, uint32_t self)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA540u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)self;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001BA8E0(void *ctx, uint32_t self, int32_t kind)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA8E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)self;
    c.a[1] = (uint64_t)(int64_t)kind;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_001CA6F0(void *ctx, uint32_t self, int32_t flags)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CA6F0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)self;
    c.a[1] = (uint64_t)(int64_t)flags;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static int a04_w_008245F0(void *ctx, uint32_t self)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x008245F0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)self;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static uint8_t *a04_view(void *ctx, uint32_t address, uint32_t size)
{
    return bytes(ctx, address, size, 0);
}

static void overlay04_workers(EmArea01Runtime *r)
{
    r->overlay04.ctx = r;
    r->overlay04.bytes = a04_view;
    r->overlay04.w_0011DE90 = a04_w_0011DE90;
    r->overlay04.w_0011DF78 = a04_w_0011DF78;
    r->overlay04.w_0011E2A8 = a04_w_0011E2A8;
    r->overlay04.w_0011E748 = a04_w_0011E748;
    r->overlay04.w_001028D0 = a04_w_001028D0;
    r->overlay04.w_00102900 = a04_w_00102900;
    r->overlay04.w_00102948 = a04_w_00102948;
    r->overlay04.w_00121870 = a04_w_00121870;
    r->overlay04.w_00122BB8 = a04_w_00122BB8;
    r->overlay04.w_00182F90 = a04_w_00182F90;
    r->overlay04.w_00183010 = a04_w_00183010;
    r->overlay04.w_00187EC0 = a04_w_00187EC0;
    r->overlay04.w_001A2370 = a04_w_001A2370;
    r->overlay04.w_001AF890 = a04_w_001AF890;
    r->overlay04.w_001AFC10 = a04_w_001AFC10;
    r->overlay04.w_001B0FD0 = a04_w_001B0FD0;
    r->overlay04.w_001B10B0 = a04_w_001B10B0;
    r->overlay04.w_001B1240 = a04_w_001B1240;
    r->overlay04.w_001B1470 = a04_w_001B1470;
    r->overlay04.w_001B17A0 = a04_w_001B17A0;
    r->overlay04.w_001B1B70 = a04_w_001B1B70;
    r->overlay04.w_001B1DE0 = a04_w_001B1DE0;
    r->overlay04.w_001B1E20 = a04_w_001B1E20;
    r->overlay04.w_001B1EA0 = a04_w_001B1EA0;
    r->overlay04.w_001B6660 = a04_w_001B6660;
    r->overlay04.w_001B6F80 = a04_w_001B6F80;
    r->overlay04.w_001BA1A0 = a04_w_001BA1A0;
    r->overlay04.w_001BA1C0 = a04_w_001BA1C0;
    r->overlay04.w_001BA1F0 = a04_w_001BA1F0;
    r->overlay04.w_001BA580 = a04_w_001BA580;
    r->overlay04.w_001BB520 = a04_w_001BB520;
    r->overlay04.w_001BB560 = a04_w_001BB560;
    r->overlay04.w_001BB7C0 = a04_w_001BB7C0;
    r->overlay04.w_001BB7F0 = a04_w_001BB7F0;
    r->overlay04.w_001BC150 = a04_w_001BC150;
    r->overlay04.w_001C4760 = a04_w_001C4760;
    r->overlay04.w_001C47A0 = a04_w_001C47A0;
    r->overlay04.w_001C5570 = a04_w_001C5570;
    r->overlay04.w_001C6120 = a04_w_001C6120;
    r->overlay04.w_001C62C0 = a04_w_001C62C0;
    r->overlay04.w_001C6380 = a04_w_001C6380;
    r->overlay04.w_001C63E0 = a04_w_001C63E0;
    r->overlay04.w_001C64F0 = a04_w_001C64F0;
    r->overlay04.w_001C67E0 = a04_w_001C67E0;
    r->overlay04.w_001C68C0 = a04_w_001C68C0;
    r->overlay04.w_001CA5E0 = a04_w_001CA5E0;
    r->overlay04.w_001CA6E0 = a04_w_001CA6E0;
    r->overlay04.w_001D0C80 = a04_w_001D0C80;
    r->overlay04.w_001D0D40 = a04_w_001D0D40;
    r->overlay04.w_001D0D60 = a04_w_001D0D60;
    r->overlay04.w_001EFD20 = a04_w_001EFD20;
    r->overlay04.w_001F4BF0 = a04_w_001F4BF0;
    r->overlay04.w_001FABB0 = a04_w_001FABB0;
    r->overlay04.w_001FAE70 = a04_w_001FAE70;
    r->overlay04.w_001FB0B0 = a04_w_001FB0B0;
    r->overlay04.w_001FB9F0 = a04_w_001FB9F0;
    r->overlay04.w_001FBD50 = a04_w_001FBD50;
    r->overlay04.w_001BA540 = a04_w_001BA540;
    r->overlay04.w_001BA8E0 = a04_w_001BA8E0;
    r->overlay04.w_001CA6F0 = a04_w_001CA6F0;
    r->overlay04.w_008245F0 = a04_w_008245F0;
}
