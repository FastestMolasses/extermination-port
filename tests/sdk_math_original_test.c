/* Contract test for em_sdk_math_original (the boot ELF's SDK float math).
 * Equality with the original instructions is established by
 * tools/test_sdk_math_original_reference.py; this test pins the native
 * contract under ASan/UBSan: the table loader, fail-stop faults, the
 * wrappers' worker protocol, the adapters, a few values that follow from
 * the EE float model alone, and an undefined-behaviour sweep over random
 * words for every entry point.
 *
 * Usage: sdk_math_original_test <user's SCUS_971.12> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_sdk_math_original.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static float f(uint32_t b) { float v; memcpy(&v, &b, 4); return v; }
static uint32_t w(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = n > 0 ? malloc((size_t)n) : NULL;
    if (data && fread(data, 1, (size_t)n, fp) != (size_t)n) { free(data); data = NULL; }
    fclose(fp);
    *size = data ? (size_t)n : 0;
    return data;
}

/* ---- recorded wrapper workers ---- */
typedef struct {
    char order[16];
    int n;
    int matherr_result, fail;         /* fail: 1 todouble 2 matherr 3 errno 4 tofloat */
    int32_t cell;
    EmSdkMathException seen;
    int32_t set_err;
    int32_t *flip_cell, flip_to;     /* 00128350 rewrites D_0026C5D0 when set */
} Log;

static void note(Log *l, char c) { if (l->n < 15) l->order[l->n++] = c; }
static int w_d(void *c, float x, uint64_t *out)
{
    Log *l = c; note(l, 'd'); *out = (uint64_t)w(x) << 29;
    if (l->flip_cell) *l->flip_cell = l->flip_to;
    return l->fail == 1 ? -1 : 0;
}
static int w_m(void *c, EmSdkMathException *e, int32_t *r)
{
    Log *l = c; note(l, 'm'); l->seen = *e; e->err = l->set_err; *r = l->matherr_result;
    return l->fail == 2 ? -1 : 0;
}
static int w_e(void *c, int32_t **cell) { Log *l = c; note(l, 'e'); *cell = &l->cell; return l->fail == 3 ? -1 : 0; }
static int w_f(void *c, uint64_t v, float *out) { Log *l = c; note(l, 'f'); *out = f((uint32_t)(v >> 29) ^ 0x40000000u); return l->fail == 4 ? -1 : 0; }

static EmSdkMathWorkers workers(Log *l)
{
    EmSdkMathWorkers k = { l, w_d, w_m, w_e, w_f };
    return k;
}

static void test_loader(const uint8_t *elf, size_t size, EmSdkMathTables *t)
{
    CHECK(em_sdk_math_original_load_tables(NULL, size, t) == -1);
    CHECK(em_sdk_math_original_load_tables(elf, size - 1, t) == -1);
    CHECK(em_sdk_math_original_load_tables(elf, size, NULL) == -1);
    uint8_t *copy = malloc(size);
    memcpy(copy, elf, size);
    copy[1] = 'X';
    CHECK(em_sdk_math_original_load_tables(copy, size, t) == -1);
    free(copy);
    CHECK(em_sdk_math_original_load_tables(elf, size, t) == 0);
    /* Structure of the loaded data, not its values: init_jk is ascending
     * over prec 0..2 and PIo2 descends. */
    CHECK(t->init_jk[0] > 0 && t->init_jk[0] < t->init_jk[1] && t->init_jk[1] < t->init_jk[2]);
    for (int i = 1; i < 11; ++i) CHECK(t->pio2[i] < t->pio2[i - 1]);
}

static void test_values(const EmSdkMathTables *t)
{
    uint32_t fault = 0;
    float r;
    /* Leaves. */
    CHECK(w(em_sdk_math_original_0011DF78(f(0xBF800000u))) == 0x3F800000u);
    CHECK(em_sdk_math_original_0011E080(f(0x7F800001u)) == 1 && em_sdk_math_original_0011E080(f(0x7F800000u)) == 0);
    CHECK(w(em_sdk_math_original_0011DE60(2.0f, -0.0f)) == w(-2.0f));
    CHECK(em_sdk_math_original_0011DF98(2.5f) == 2.0f && em_sdk_math_original_0011DF98(-2.5f) == -3.0f);
    CHECK(w(em_sdk_math_original_0011DF98(-0.25f)) == 0xBF800000u && w(em_sdk_math_original_0011DF98(0.25f)) == 0);
    CHECK(em_sdk_math_original_0011E148(1.0f, 3) == 8.0f && em_sdk_math_original_0011E148(8.0f, -3) == 1.0f);
    /* EE: a denormal is read as zero by MUL.S, so scalbnf cannot rescale it. */
    CHECK(w(em_sdk_math_original_0011E148(f(0x00000005u), 30)) == 0x02800000u);
    /* sqrt kernel: exact squares; zeros keep their sign; negative -> 0/0 = +MAX on the EE. */
    CHECK(em_sdk_math_original_0011CB90(4.0f) == 2.0f && em_sdk_math_original_0011CB90(1.0f) == 1.0f);
    CHECK(w(em_sdk_math_original_0011CB90(-0.0f)) == 0x80000000u && w(em_sdk_math_original_0011CB90(0.0f)) == 0);
    CHECK(w(em_sdk_math_original_0011CB90(-1.0f)) == 0x7F7FFFFFu);
    CHECK(w(em_sdk_math_original_0011CB90(f(0x7F800000u))) == 0x7F7FFFFFu);   /* Inf*Inf + Inf saturates */
    /* Tiny arguments come back unchanged (0x31FFFFFF / 0x30FFFFFF gates). */
    CHECK(em_sdk_math_original_0011E2A8(t, f(0x31000000u), &r, &fault) == 0 && w(r) == 0x31000000u);
    CHECK(em_sdk_math_original_0011DBB8(t, f(0x30000000u), &r, &fault) == 0 && w(r) == 0x30000000u);
    CHECK(em_sdk_math_original_0011DE90(t, 0.0f, &r, &fault) == 0 && r == 1.0f);
    /* Inf/NaN: x - x saturates on the EE. */
    CHECK(em_sdk_math_original_0011E2A8(t, f(0x7F800000u), &r, &fault) == 0 && w(r) == 0x7F7FFFFFu);
    CHECK(em_sdk_math_original_0011E398(t, f(0xFFC00000u), &r, &fault) == 0 && w(r) == 0x7F7FFFFFu);
    /* atan2 kernel: axis cases are instruction immediates. */
    CHECK(em_sdk_math_original_0011C4C8(t, 1.0f, 0.0f, &r, &fault) == 0 && w(r) == 0x3FC90FDBu);
    CHECK(em_sdk_math_original_0011C4C8(t, 0.0f, -1.0f, &r, &fault) == 0 && w(r) == 0x40490FDAu);
    CHECK(em_sdk_math_original_0011C4C8(t, -0.0f, 2.0f, &r, &fault) == 0 && w(r) == 0x80000000u);
    /* Odd symmetry of the sine and tangent paths, even symmetry of cosine. */
    for (uint32_t i = 0; i < 4000; ++i) {
        const uint32_t x = 0x3C000000u + i * 0x0001F3A1u;
        float a, b;
        CHECK(em_sdk_math_original_0011E2A8(t, f(x), &a, &fault) == 0);
        CHECK(em_sdk_math_original_0011E2A8(t, f(x | 0x80000000u), &b, &fault) == 0);
        CHECK(w(a) == (w(b) ^ 0x80000000u));
        CHECK(em_sdk_math_original_0011DE90(t, f(x), &a, &fault) == 0);
        CHECK(em_sdk_math_original_0011DE90(t, f(x | 0x80000000u), &b, &fault) == 0);
        CHECK(w(a) == w(b));
    }
    /* 0011C7B0: n = +-1 around pi/2, 0 below pi/4. */
    float y[2];
    int32_t n = 9;
    CHECK(em_sdk_math_original_0011C7B0(t, 0.5f, y, &n, &fault) == 0 && n == 0 && y[0] == 0.5f && w(y[1]) == 0);
    CHECK(em_sdk_math_original_0011C7B0(t, 1.6f, y, &n, &fault) == 0 && n == 1);
    CHECK(em_sdk_math_original_0011C7B0(t, -1.6f, y, &n, &fault) == 0 && n == -1);
    CHECK(em_sdk_math_original_0011C7B0(t, 1.0e6f, y, &n, &fault) == 0);
}

static void test_fail_stop(const EmSdkMathTables *t)
{
    uint32_t fault = 0;
    float r, y[3];
    int32_t n;
    /* Table reads with no tables fault at the reading instruction. */
    CHECK(em_sdk_math_original_0011E2A8(NULL, 0.5f, &r, &fault) == 0);          /* no table on this path */
    CHECK(em_sdk_math_original_0011E2A8(NULL, 10.0f, &r, &fault) == -1 && fault == 0x0011C98Cu);
    CHECK(em_sdk_math_original_0011DE90(NULL, 1.0e7f, &r, &fault) == -1 && fault == 0x0011CEA0u);
    CHECK(em_sdk_math_original_0011E398(NULL, 0.3f, &r, &fault) == -1 && fault == 0x0011D974u);
    CHECK(em_sdk_math_original_0011DBB8(NULL, 0.7f, &r, &fault) == -1 && fault == 0x0011DD78u);
    CHECK(em_sdk_math_original_0011DBB8(NULL, 1.0e20f, &r, &fault) == -1 && fault == 0x0011DC14u);
    CHECK(em_sdk_math_original_0011C4C8(NULL, -1.0f, f(0x7F800000u), &r, &fault) == -1 && fault == 0x0011C684u);
    /* NULL outputs fault at the entry. */
    CHECK(em_sdk_math_original_0011E2A8(t, 1.0f, NULL, &fault) == -1 && fault == 0x0011E2A8u);
    CHECK(em_sdk_math_original_0011DE90(t, 1.0f, NULL, &fault) == -1 && fault == 0x0011DE90u);
    CHECK(em_sdk_math_original_0011E398(t, 1.0f, NULL, &fault) == -1 && fault == 0x0011E398u);
    CHECK(em_sdk_math_original_0011DBB8(t, 1.0f, NULL, &fault) == -1 && fault == 0x0011DBB8u);
    CHECK(em_sdk_math_original_0011C4C8(t, 1.0f, 1.0f, NULL, &fault) == -1 && fault == 0x0011C4C8u);
    CHECK(em_sdk_math_original_0011D878(t, 1.0f, 0.0f, 1, NULL, &fault) == -1 && fault == 0x0011D878u);
    CHECK(em_sdk_math_original_0011C7B0(t, 1.0f, NULL, &n, &fault) == -1 && fault == 0x0011C7B0u);
    /* 0011CE20: prec and index bounds. */
    const float x[3] = {200.0f, 3.0f, 17.0f};
    CHECK(em_sdk_math_original_0011CE20(t, x, y, 20, 3, 4, t->two_over_pi, EM_SDK_MATH_TWO_OVER_PI_COUNT,
                                        &n, &fault) == -1 && fault == 0x0011CE20u);
    CHECK(em_sdk_math_original_0011CE20(t, x, y, 100000, 3, 2, t->two_over_pi, EM_SDK_MATH_TWO_OVER_PI_COUNT,
                                        &n, &fault) == -1 && fault == 0x0011CE20u);
    CHECK(em_sdk_math_original_0011CE20(NULL, x, y, 20, 3, 2, NULL, 0, &n, &fault) == -1 && fault == 0x0011CEA0u);
    CHECK(em_sdk_math_original_0011CE20(t, x, y, 20, 3, 2, t->two_over_pi, EM_SDK_MATH_TWO_OVER_PI_COUNT,
                                        &n, &fault) == 0 && n >= 0 && n < 8);
}

static void test_wrappers(const EmSdkMathTables *t)
{
    uint32_t fault;
    float r;
    int32_t mode = 1;
    EmSdkMathWorld world = { &mode };

    /* Off the domain path: the kernel's value, no worker called. */
    Log quiet = {0};
    EmSdkMathWorkers k = workers(&quiet);
    CHECK(em_sdk_math_original_0011E620(t, &world, &k, 1.0f, 1.0f, &r, &fault) == 0 && quiet.n == 0);
    CHECK(em_sdk_math_original_0011E748(t, &world, &k, 9.0f, &r, &fault) == 0 && r == 3.0f && quiet.n == 0);
    /* D_0026C5D0 = -1 (IEEE mode): never the error path. */
    mode = -1;
    CHECK(em_sdk_math_original_0011E620(t, &world, &k, 0.0f, 0.0f, &r, &fault) == 0 && quiet.n == 0);
    CHECK(em_sdk_math_original_0011E748(t, &world, &k, -4.0f, &r, &fault) == 0 && quiet.n == 0 &&
          w(r) == 0x7F7FFFFFu);

    /* Mode 1, zero vector: todouble(y), todouble(x), matherr, errno = 0x21, tofloat(0). */
    mode = 1;
    Log a = {0};
    a.cell = 0x5EED;
    k = workers(&a);
    CHECK(em_sdk_math_original_0011E620(t, &world, &k, -0.0f, 0.0f, &r, &fault) == 0);
    CHECK(strcmp(a.order, "ddmef") == 0 && a.cell == 0x21);
    CHECK(a.seen.type == 1 && a.seen.name == 0x0026C640u && a.seen.retval == 0 && a.seen.err == 0);
    CHECK(a.seen.arg1 == ((uint64_t)0x80000000u << 29) && a.seen.arg2 == 0);
    CHECK(w(r) == 0x40000000u);                       /* the tofloat worker's value, unchanged */
    /* EE compare: a denormal pair is a zero vector too. */
    Log dn = {0};
    k = workers(&dn);
    CHECK(em_sdk_math_original_0011E620(t, &world, &k, f(1u), f(0x80000001u), &r, &fault) == 0 && dn.n == 5);

    /* matherr != 0 and an err it writes: errno is set once, to err. */
    Log b = {0};
    b.matherr_result = 1;
    b.set_err = 7;
    k = workers(&b);
    CHECK(em_sdk_math_original_0011E748(t, &world, &k, -2.0f, &r, &fault) == 0);
    CHECK(strcmp(b.order, "dmef") == 0 && b.cell == 7);
    CHECK(b.seen.name == 0x0026C648u && b.seen.arg1 == b.seen.arg2 && b.seen.retval == t->d26C650);

    /* Mode 2: no matherr; mode 0: retval 0 for sqrtf. */
    mode = 2;
    Log c = {0};
    k = workers(&c);
    CHECK(em_sdk_math_original_0011E748(t, &world, &k, -2.0f, &r, &fault) == 0 && strcmp(c.order, "def") == 0);
    mode = 0;
    Log d = {0};
    k = workers(&d);
    CHECK(em_sdk_math_original_0011E748(t, &world, &k, -2.0f, &r, &fault) == 0 && d.seen.retval == 0);
    /* 0011E748 reads D_0026C5D0 again at 0x11E7E0; 0011E620 does not. */
    mode = 1;
    Log fl = {0};
    fl.flip_cell = &mode;
    fl.flip_to = 2;
    k = workers(&fl);
    CHECK(em_sdk_math_original_0011E748(t, &world, &k, -2.0f, &r, &fault) == 0 && strcmp(fl.order, "def") == 0);
    mode = 1;
    Log fa = {0};
    fa.flip_cell = &mode;
    fa.flip_to = 2;
    k = workers(&fa);
    CHECK(em_sdk_math_original_0011E620(t, &world, &k, 0.0f, 0.0f, &r, &fault) == 0 && strcmp(fa.order, "ddmef") == 0);
    mode = 0;
    /* The EE compare reads a negative denormal as zero: not < 0, no error. */
    Log e = {0};
    k = workers(&e);
    CHECK(em_sdk_math_original_0011E748(t, &world, &k, f(0x80000001u), &r, &fault) == 0 && e.n == 0);

    /* Every worker and the world cell fail-stop at their addresses. */
    mode = 1;
    static const uint32_t address[5] = {0, 0x00128350u, 0x0011DB90u, 0x0011FD78u, 0x00127758u};
    for (int which = 1; which <= 4; ++which) {
        Log g = {0};
        g.fail = which;
        k = workers(&g);
        fault = 0;
        CHECK(em_sdk_math_original_0011E620(t, &world, &k, 0.0f, 0.0f, &r, &fault) == -1 && fault == address[which]);
        Log h = {0};
        k = workers(&h);
        if (which == 1) k.w_00128350 = NULL;
        if (which == 2) k.w_0011DB90 = NULL;
        if (which == 3) k.w_0011FD78 = NULL;
        if (which == 4) k.w_00127758 = NULL;
        fault = 0;
        CHECK(em_sdk_math_original_0011E748(t, &world, &k, -1.0f, &r, &fault) == -1 && fault == address[which]);
    }
    EmSdkMathWorld none = { NULL };
    CHECK(em_sdk_math_original_0011E620(t, &none, &k, 0.0f, 0.0f, &r, &fault) == -1 && fault == 0x0011E648u);
    CHECK(em_sdk_math_original_0011E748(t, NULL, &k, 1.0f, &r, &fault) == -1 && fault == 0x0011E76Cu);
    Log z = {0};
    k = workers(&z);
    CHECK(em_sdk_math_original_0011E748(NULL, &world, &k, -1.0f, &r, &fault) == -1 && fault == 0x0011E7D8u);
    CHECK(em_sdk_math_original_0011E620(t, &world, NULL, 0.0f, 0.0f, &r, &fault) == -1 && fault == 0x00128350u);
}

static void test_adapters(const EmSdkMathTables *t)
{
    int32_t mode = 1;
    EmSdkMathContext c;
    memset(&c, 0, sizeof c);
    c.tables = t;
    c.world.d26C5D0 = &mode;
    float r, ref;
    uint32_t fault;
    CHECK(em_sdk_math_original_0011E2A8(t, 2.5f, &ref, &fault) == 0);
    CHECK(w(em_sdk_math_original_float_0011E2A8(&c, 2.5f)) == w(ref) && c.fault == 0);
    CHECK(em_sdk_math_original_w_0011DE90(&c, 2.5f, &r) == 0);
    CHECK(em_sdk_math_original_0011DE90(t, 2.5f, &ref, &fault) == 0 && w(r) == w(ref));
    CHECK(em_sdk_math_original_float_0011E748(&c, 16.0f) == 4.0f && c.fault == 0);
    /* A fault returns +0 and keeps the first address. */
    c.tables = NULL;
    CHECK(w(em_sdk_math_original_float_0011E398(&c, 0.3f)) == 0 && c.fault == 0x0011D974u);
    CHECK(w(em_sdk_math_original_float_0011DBB8(&c, 0.7f)) == 0 && c.fault == 0x0011D974u);
    c.fault = 0;
    CHECK(em_sdk_math_original_w_0011E2A8(&c, 10.0f, &r) == -1 && c.fault == 0x0011C98Cu);
    c.fault = 0;
    c.tables = t;
    CHECK(w(em_sdk_math_original_float_0011E620(&c, 0.0f, 0.0f)) == 0 && c.fault == 0x00128350u);
    CHECK(w(em_sdk_math_original_float_0011E2A8(NULL, 1.0f)) == 0);
}

/* Every entry point over random words: sanitizers catch any undefined
 * shift, overflow or out-of-bounds access. */
static void test_sweep(const EmSdkMathTables *t)
{
    uint32_t s = 0x2545F491u;
    for (int i = 0; i < 200000; ++i) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        const uint32_t x = s;
        uint32_t s2 = s * 0x9E3779B1u;
        float r, y[3];
        int32_t n;
        uint32_t fault;
        (void)em_sdk_math_original_0011E2A8(t, f(x), &r, &fault);
        (void)em_sdk_math_original_0011DE90(t, f(x), &r, &fault);
        (void)em_sdk_math_original_0011E398(t, f(x), &r, &fault);
        (void)em_sdk_math_original_0011DBB8(t, f(x), &r, &fault);
        (void)em_sdk_math_original_0011C4C8(t, f(x), f(s2), &r, &fault);
        (void)em_sdk_math_original_0011C7B0(t, f(x), y, &n, &fault);
        (void)em_sdk_math_original_0011D878(t, f(x), f(s2), (int32_t)(s2 & 1) ? 1 : -1, &r, &fault);
        (void)em_sdk_math_original_0011CB90(f(x));
        (void)em_sdk_math_original_0011DF98(f(x));
        (void)em_sdk_math_original_0011E148(f(x), (int32_t)s2);
        (void)em_sdk_math_original_0011D770(f(x), f(s2), (int32_t)(s2 & 1));
        (void)em_sdk_math_original_0011CCC8(f(x), f(s2));
        if ((i & 63) == 0) {
            const float xs[3] = {f(x), f(s2), f(x ^ s2)};
            (void)em_sdk_math_original_0011CE20(t, xs, y, (int32_t)(s2 % 400u) - 100, 1 + (int32_t)(x % 3u),
                                                 (int32_t)(s2 >> 30), t->two_over_pi,
                                                 EM_SDK_MATH_TWO_OVER_PI_COUNT, &n, &fault);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <SCUS_971.12>\n", argv[0]);
        return 2;
    }
    size_t size;
    uint8_t *elf = read_file(argv[1], &size);
    if (!elf) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    static EmSdkMathTables t;
    test_loader(elf, size, &t);
    free(elf);
    test_values(&t);
    test_fail_stop(&t);
    test_wrappers(&t);
    test_adapters(&t);
    test_sweep(&t);
    if (failures) {
        fprintf(stderr, "sdk_math_original_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("sdk_math_original_test: PASS\n");
    return 0;
}
