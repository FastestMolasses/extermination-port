/* object_unit_test.c - ASan/UBSan fixture for src/game/em_object_unit.c:
 * the unit parser's refusals (one rule broken at a time), a synthetic
 * plain unit and a synthetic clip unit through em_object_unit_run, and the
 * run's fail-stop refusals. The original-instruction proof is
 * tools/test_object_unit_reference.py (the captured owner units through the
 * original VU1 microcode); this fixture pins the contract only. Synthetic
 * values only: no original data. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_object_unit.h"

static int failures;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static uint32_t fb(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static void put(uint8_t *p, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    const uint32_t w[4] = {w0, w1, w2, w3};
    memcpy(p, w, 16);
}

/* ---- the memory the resolver serves: skin records, the GS state, the
 * arena qword and one 1-block model, at their original addresses. */
enum { SKIN0 = 0x00816440, SKIN1 = 0x00816540, MODEL = 0x01000040 };
static uint8_t skin0[128], skin1[128], gs[144], arena[16];
static uint8_t model[16 * 0x82 * 2];

static const uint8_t *resolve(void *ctx, uint32_t address, uint32_t bytes)
{
    (void)ctx;
    struct { uint32_t a; const uint8_t *p; uint32_t n; } r[] = {
        {SKIN0, skin0, sizeof skin0}, {SKIN1, skin1, sizeof skin1}, {EM_OBJECT_UNIT_GS_STATE, gs, sizeof gs},
        {EM_OBJECT_UNIT_ARENA, arena, sizeof arena}, {MODEL, model, sizeof model}};
    for (unsigned i = 0; i < sizeof r / sizeof r[0]; ++i)
        if (address == r[i].a && bytes <= r[i].n) return r[i].p;
    return NULL;
}

static const uint64_t TEX0 = 0x2000000123456789ull;   /* synthetic; CLD bit 61 set */

static void build_memory(void)
{
    /* Skin record 0: STCYCL 4,4 + UNPACK 7 to 0x3F9; dmem 1020 = PRE PRIM
     * 0x03C, NREG 4, REGS TEX0 ST RGBAQ XYZF2, NLOOP 32, EOP; 1021 = fog
     * (255, 2048, 200, 0); 1022 / 1023 = guard rows g = (0, 0, 0, w). */
    memset(skin0, 0, sizeof skin0);
    put(skin0, 0, 0, 0x01000404u, 0x6C0703F9u);
    const uint64_t tmpl = 32u | 1ull << 15 | 1ull << 46 | (uint64_t)0x03C << 47 | 4ull << 60;
    put(skin0 + 16 * 4, (uint32_t)tmpl, (uint32_t)(tmpl >> 32), 0x4126u, 0);
    put(skin0 + 16 * 5, fb(255.0f), fb(2048.0f), fb(200.0f), 0);
    put(skin0 + 16 * 6, 0, 0, 0, 0);
    put(skin0 + 16 * 7, 0, 0, 0, fb(1.0f));
    /* Skin record 1 for the clip pass: 1017 a triangle-list tag (PRIM 0x03B,
     * NREG 9), 1018 a TEX0_1 tag (NLOOP 1, EOP, NREG 1), the rest as 0. */
    memcpy(skin1, skin0, sizeof skin1);
    const uint64_t list = 1ull << 46 | (uint64_t)0x03B << 47 | 9ull << 60;
    put(skin1 + 16 * 1, (uint32_t)list, (uint32_t)(list >> 32), 0x12412412u, 0x4u);
    const uint64_t one = 1u | 1ull << 15 | 1ull << 60;
    put(skin1 + 16 * 2, (uint32_t)one, (uint32_t)(one >> 32), 0x6u, 0);
    /* The class-0 GS state (em_object_unit_gs_state_check's form). */
    memset(gs, 0, sizeof gs);
    put(gs, 0, 0, 0x11000000u, 0x50000008u);
    put(gs + 16, 0x8007u, 0x10000000u, 0xEu, 0);
    const uint64_t ad[7][2] = {{0x100, 0x00}, {0x60, 0x14}, {0x5000D, 0x47}, {0x1000070, 0x4E},
                               {0x80000000A8ull, 0x42}, {0, 0x08}, {1, 0x46}};
    for (unsigned i = 0; i < 7; ++i)
        put(gs + 32 + 16 * i, (uint32_t)ad[i][0], (uint32_t)(ad[i][0] >> 32), (uint32_t)ad[i][1], 0);
    put(arena, 0, 0, 0, 0x11000000u);
    /* One block: 32 vertices on node 0 (data word 0), a strip in x. */
    memset(model, 0, sizeof model);
    put(model, 0, 0, 0x01000404u, 0x6C808000u);
    for (unsigned i = 0; i < 32; ++i) {
        uint8_t *v = model + 16 + 64 * i;
        put(v, (uint32_t)TEX0, (uint32_t)(TEX0 >> 32), 0, 0);
        put(v + 16, fb((float)i / 32.0f), fb((float)(i & 1)), fb(1.0f), 0);
        put(v + 32, 0, 0, fb(1.0f), 0);
        put(v + 48, fb(4.0f * (float)i), fb((float)(i & 1) * 8.0f), fb(10.0f), 0);
    }
    put(model + 16 * 129, 0x14000000u, 0, 0, 0);
}

/* The unit: colour CNT, one node CNT, REF 9 GS, REF 8 skin 0, REF 1 arena,
 * CALL kernel, REF model; `clip` appends the clip pass. Returns its size. */
static uint32_t build_unit(uint8_t *u, int clip)
{
    uint32_t at = 0;
    put(u + at, 0x10000005u, 0, 0, 0); at += 16;
    put(u + at, 0, 0x11000000u, 0x01000101u, 0x6C0403F5u); at += 16;
    for (unsigned r = 0; r < 4; ++r, at += 16)       /* B: rows 0..2 zero, ambient (8388608 + 60) */
        put(u + at, r == 3 ? fb(8388668.0f) : 0, r == 3 ? fb(8388668.0f) : 0, r == 3 ? fb(8388668.0f) : 0,
            r == 3 ? fb(8388608.0f) : 0);
    put(u + at, 0x10000009u, 0, 0, 0); at += 16;
    put(u + at, 0, 0, 0x01000101u, 0x6C080000u); at += 16;
    const float m[8][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {2048, 2048, 0, 1}, {0}, {0}, {0}, {0}};
    for (unsigned r = 0; r < 8; ++r, at += 16) put(u + at, fb(m[r][0]), fb(m[r][1]), fb(m[r][2]), fb(m[r][3]));
    put(u + at, 0x30000009u, EM_OBJECT_UNIT_GS_STATE, 0, 0); at += 16;
    put(u + at, 0x30000008u, SKIN0, 0, 0); at += 16;
    put(u + at, 0x30000001u, EM_OBJECT_UNIT_ARENA, 0, 0); at += 16;
    put(u + at, 0x50000000u, EM_OBJECT_UNIT_KERNEL, 0, 0); at += 16;
    put(u + at, 0x30000082u, MODEL, 0, 0); at += 16;
    if (clip) {
        put(u + at, 0x30000008u, SKIN1, 0, 0); at += 16;
        put(u + at, 0x30000001u, EM_OBJECT_UNIT_ARENA, 0, 0); at += 16;
        put(u + at, 0x50000000u, EM_OBJECT_UNIT_CLIP_KERNEL, 0, 0); at += 16;
        put(u + at, 0x30000082u, MODEL, 0, 0); at += 16;
    }
    return at;
}

static EmObjectUnitPieces pieces;

static void expect_refusal(const uint8_t *u, uint32_t size, const char *reason)
{
    const char *why = NULL;
    const int rc = em_object_unit_parse(u, size, resolve, NULL, &pieces, &why);
    CHECK(rc == -1);
    CHECK(why && strstr(why, reason));
    if (rc != -1 || !why || !strstr(why, reason)) fprintf(stderr, "  wanted \"%s\", got \"%s\"\n", reason, why);
}

static void parse_refusals(void)
{
    uint8_t u[1024], w[1024];
    const uint32_t size = build_unit(u, 0);
    const char *why = NULL;
    CHECK(em_object_unit_parse(u, size, resolve, NULL, &pieces, &why) == 0);
    CHECK(pieces.bytes == size && pieces.unit.node_count == 1 && pieces.unit.block_count == 1 &&
          !pieces.unit.clip && !pieces.fog_off && pieces.model_address == MODEL);
    CHECK(pieces.skin_address[0] == SKIN0 && pieces.constants[16] == fb(255.0f));
    CHECK(em_object_unit_parse(NULL, size, resolve, NULL, &pieces, &why) == -1);
    struct { uint32_t at, word; uint32_t value; const char *reason; } m[] = {
        {0x10, 3, 0x6C0403F6u, "colour CNT"},          /* colour UNPACK address */
        {0x60, 0, 0x10000008u, "node CNT"},            /* 7 node qwords */
        {0x70, 3, 0x6C080008u, "node CNT"},            /* nodes out of order */
        {0x100, 1, EM_OBJECT_UNIT_GS_STATE + 0x90, "set 1 class 0"},
        {0x100, 0, 0x30000008u, "GS state REF 9"},
        {0x110, 1, SKIN0 + 0x10, "resolver"},          /* a REF target the resolver lacks */
        {0x120, 1, EM_OBJECT_UNIT_ARENA + 0x10, "arena REF"},
        {0x130, 1, EM_OBJECT_UNIT_FOG_OFF - 0x30, "CALL"},
        {0x140, 0, 0x30000081u, "0x82-qword"},
        {0x140, 0, 0x70000082u, "other than CNT"},
    };
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; ++i) {
        memcpy(w, u, size);
        memcpy(w + m[i].at + 4 * m[i].word, &m[i].value, 4);
        expect_refusal(w, size, m[i].reason);
    }
    expect_refusal(u, size - 16, "ends inside");
    memcpy(w, u, size);
    memset(w + size, 0, 16);
    expect_refusal(w, size + 16, "other than CNT");      /* a trailing tag that is no pass */
    /* The skin record's codes, then the GS state bytes. */
    skin0[12] ^= 1;
    expect_refusal(u, size, "skin record");
    skin0[12] ^= 1;
    CHECK(em_object_unit_gs_state_check(gs, &why) == 0);
    gs[0x40] ^= 2;                                     /* TEST_1 0x5000F */
    CHECK(em_object_unit_gs_state_check(gs, &why) == -1 && strstr(why, "class-0"));
    gs[0x40] ^= 2;
    gs[0x54] = 1;                                      /* ZBUF_1 ZMSK */
    CHECK(em_object_unit_gs_state_check(gs, &why) == -1);
    gs[0x54] = 0;
    CHECK(em_object_unit_gs_state_check(NULL, &why) == -1);
}

static void runs(void)
{
    uint8_t u[1024];
    EmObjectUnitResult r;
    memset(&r, 0, sizeof r);
    const char *why = NULL;
    /* Plain: 30 strip triangles, node 0 = the identity at (2048, 2048). */
    uint32_t size = build_unit(u, 0);
    CHECK(em_object_unit_parse(u, size, resolve, NULL, &pieces, &why) == 0);
    CHECK(em_object_unit_run(&pieces.unit, &r) == 0);
    CHECK(r.count == 30 && r.object_triangles == 30);
    if (r.count == 30) {
        const EmObjectUnitTriangle *t = &r.tri[0];
        CHECK(t->tex0 == TEX0 && t->pass == 0 && t->block == 0);
        CHECK(t->v[0].x == 2048u * 16u && t->v[0].y == 2048u * 16u && t->v[1].x == (2048u + 4u) * 16u);
        CHECK(t->v[0].z == 10u && t->v[0].f == 200u);
        CHECK(t->v[0].rgba[0] == 60u && t->v[0].rgba[3] == 0u);
        CHECK(t->v[2].q == fb(1.0f));
        CHECK(r.tri[29].v[2].x == (2048u + 4u * 31u) * 16u);
    }
    /* Clip: nothing leaves the guard band, so the clip pass adds nothing. */
    size = build_unit(u, 1);
    CHECK(em_object_unit_parse(u, size, resolve, NULL, &pieces, &why) == 0);
    CHECK(pieces.unit.clip == 1 && pieces.skin_address[1] == SKIN1);
    CHECK(em_object_unit_run(&pieces.unit, &r) == 0);
    CHECK(r.count == 30 && r.object_triangles == 30);
    /* The run's refusals. */
    EmGfxObjectUnit bad = pieces.unit;
    bad.program = 0x0023C990u;                         /* not an object-unit program */
    CHECK(em_object_unit_run(&bad, &r) == -1 && r.why && strstr(r.why, "object kernel"));
    bad.program = EM_GFX_OBJECT_FACE;                  /* a face without its weights */
    CHECK(em_object_unit_run(&bad, &r) == -1 && strstr(r.why, "missing"));
    bad = pieces.unit;
    bad.clip_constants = NULL;
    CHECK(em_object_unit_run(&bad, &r) == -1 && strstr(r.why, "missing"));
    bad = pieces.unit;
    bad.node_count = EM_OBJECT_UNIT_MAX_NODES + 1;
    CHECK(em_object_unit_run(&bad, &r) == -1);
    CHECK(em_object_unit_run(NULL, &r) == -1);
    CHECK(em_object_unit_run(&pieces.unit, NULL) == -1);
    model[16 * 129 + 3] = 0x17;                        /* block 0 ends in MSCNT */
    CHECK(em_object_unit_run(&pieces.unit, &r) == -1 && strstr(r.why, "VIF codes"));
    model[16 * 129 + 3] = 0x14;
    pieces.constants[13] ^= 1u << 15;                  /* template PRIM 0x03D */
    CHECK(em_object_unit_run(&pieces.unit, &r) == -1 && strstr(r.why, "template"));
    pieces.constants[13] ^= 1u << 15;
    pieces.nodes[0] = 0x7F800000u;                      /* an exponent-255 live operand */
    CHECK(em_object_unit_run(&pieces.unit, &r) == -1 && strstr(r.why, "faulted"));
    em_object_unit_result_free(&r);
    em_object_unit_result_free(NULL);
}

/* A synthetic face unit (001C7900 / 001CB2C0 / 001D3E40's layout): the
 * colour CNT, one node CNT, the weights CNT (all zero: the morph is the
 * base), the GS state, the arena, CALL 0x0023C480, skin record 0, one face
 * block of 32 11-qword vertices. */
enum { FACE = 0x01100040 };
static uint8_t face[16 * 0x163];

static const uint8_t *resolve_face(void *ctx, uint32_t address, uint32_t bytes)
{
    if (address == FACE && bytes <= sizeof face) return face;
    return resolve(ctx, address, bytes);
}

static void faces(void)
{
    memset(face, 0, sizeof face);
    put(face, 0, 0, 0x01000404u, 0x6C008000u);
    put(face + 16 * 257, 0, 0, 0x01000404u, 0x6C608100u);
    put(face + 16 * 354, 0x14000000u, 0, 0, 0);
    for (unsigned i = 0; i < 32; ++i) {
        const unsigned q = 11u * i;                    /* vertex i's first qword in the batch */
        uint8_t *v[4];
        for (unsigned k = 0; k < 4; ++k) {
            const unsigned at = q + k;                 /* 256 qwords, then the second UNPACK */
            v[k] = face + 16u * (at < 256u ? 1u + at : 2u + at);
        }
        put(v[0], (uint32_t)TEX0, (uint32_t)(TEX0 >> 32), 0, 0);
        put(v[1], fb(0.5f), fb(0.5f), fb(1.0f), 0);
        put(v[2], 0, 0, fb(1.0f), 0);
        put(v[3], fb(2.0f * (float)i), fb((float)(i & 1) * 4.0f), fb(10.0f), 0);
    }
    uint8_t u[1024];
    (void)build_unit(u, 0);                            /* keep its colour and node CNTs (0x00..0xFF) */
    put(u + 0x100, 0x10000003u, 0, 0, 0);
    put(u + 0x110, 0, 0, 0x01000404u, 0x6C0203F3u);
    put(u + 0x120, 0, 0, 0, 0);
    put(u + 0x130, 0, 0, 0, 0);
    uint32_t at = 0x140;
    put(u + at, 0x30000009u, EM_OBJECT_UNIT_GS_STATE, 0, 0); at += 16;
    put(u + at, 0x30000001u, EM_OBJECT_UNIT_ARENA, 0, 0); at += 16;
    put(u + at, 0x50000000u, EM_OBJECT_UNIT_FACE_KERNEL, 0, 0); at += 16;
    put(u + at, 0x30000008u, SKIN0, 0, 0); at += 16;
    put(u + at, 0x30000163u, FACE, 0, 0); at += 16;
    const char *why = NULL;
    CHECK(em_object_unit_parse(u, at, resolve_face, NULL, &pieces, &why) == 0);
    if (why) fprintf(stderr, "  face parse: %s\n", why);
    CHECK(pieces.unit.program == EM_GFX_OBJECT_FACE && pieces.unit.weights && pieces.unit.block_count == 1);
    EmObjectUnitResult r;
    memset(&r, 0, sizeof r);
    CHECK(em_object_unit_run(&pieces.unit, &r) == 0);
    CHECK(r.count == 30);
    if (r.count == 30) {
        CHECK(r.tri[0].v[1].x == (2048u + 2u) * 16u && r.tri[0].v[1].y == (2048u + 4u) * 16u);
        CHECK(r.tri[0].v[0].rgba[1] == 60u && r.tri[0].v[0].f == 200u && r.tri[0].tex0 == TEX0);
    }
    face[16 * 257 + 12] ^= 1;                          /* the second UNPACK's address */
    CHECK(em_object_unit_run(&pieces.unit, &r) == -1 && strstr(r.why, "face block"));
    face[16 * 257 + 12] ^= 1;
    put(u + at - 16, 0x30000162u, FACE, 0, 0);          /* not whole face blocks */
    CHECK(em_object_unit_parse(u, at, resolve_face, NULL, &pieces, &why) == -1 && strstr(why, "0x163"));
    em_object_unit_result_free(&r);
}

int main(void)
{
    build_memory();
    parse_refusals();
    runs();
    faces();
    if (failures) {
        fprintf(stderr, "object_unit_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("object_unit_test: PASS (parser refusals, plain, clip and face units, run refusals)\n");
    return 0;
}
