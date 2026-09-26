/* em_object_unit.c - see em_object_unit.h and docs/OWNER_DRAW.md. */
#include "game/em_object_unit.h"

#include <stdlib.h>
#include <string.h>

#include "game/em_vu1_face_morph.h"
#include "game/em_vu1_object_clip.h"
#include "game/em_vu1_object_kernel.h"

/* ------------------------------------------------------------ helpers -- */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static int refuse(const char **why, const char *reason)
{
    if (why) *why = reason;
    return -1;
}

/* DMA tag ids (the tag's word 0 bits 28..30). */
enum { TAG_CNT = 1, TAG_REF = 3, TAG_CALL = 5 };

typedef struct {
    const uint8_t *unit;
    uint32_t size, at;
    EmObjectUnitResolve resolve;
    void *ctx;
} Walk;

typedef struct {
    uint32_t id, qwc, address;
    const uint8_t *payload;   /* CNT: the qwords after the tag; REF: the resolved target */
} Tag;

/* The next tag. CNT data follows the tag inline (payload); a REF's target
 * is fetched only by fetch() (the GS state and arena REFs are checked by
 * address and never read); CALL carries no data here (the kernel packet is
 * the program, which this module runs as its translation). */
static int next_tag(Walk *w, Tag *t, const char **why)
{
    if (w->at + 16u > w->size) return refuse(why, "unit ends inside a DMA tag");
    const uint8_t *p = w->unit + w->at;
    const uint32_t w0 = rd32(p);
    t->qwc = w0 & 0xFFFFu;
    t->id = (w0 >> 28) & 7u;
    t->address = rd32(p + 4);
    w->at += 16u;
    if (t->id == TAG_CNT) {
        if (t->qwc > (w->size - w->at) / 16u) return refuse(why, "CNT data past the unit's end");
        t->payload = w->unit + w->at;
        w->at += 16u * t->qwc;
        return 0;
    }
    if (t->id == TAG_REF) {
        t->payload = NULL;
        return 0;
    }
    if (t->id == TAG_CALL) {
        if (t->qwc) return refuse(why, "a CALL tag with data");
        t->payload = NULL;
        return 0;
    }
    return refuse(why, "a DMA tag other than CNT, REF or CALL");
}

static int fetch(Walk *w, Tag *t, const char **why)
{
    t->payload = t->qwc ? w->resolve(w->ctx, t->address, 16u * t->qwc) : NULL;
    if (!t->payload) return refuse(why, "a REF target the resolver does not hold");
    return 0;
}

static int peek_id(const Walk *w)
{
    if (w->at + 16u > w->size) return -1;
    return (int)((rd32(w->unit + w->at) >> 28) & 7u);
}

/* The VIF code words of a qword that only sets up an UNPACK: NOPs, then
 * `stcycl`, then the UNPACK V4-32 code `unpack`. */
static int codes_are(const uint8_t *q, uint32_t w0, uint32_t w1, uint32_t stcycl, uint32_t unpack)
{
    return rd32(q) == w0 && rd32(q + 4) == w1 && rd32(q + 8) == stcycl && rd32(q + 12) == unpack;
}

/* The REF 9 of 001D1F80(0, 1, 0): FLUSH, DIRECT 8, then one PACKED GIF
 * tag (NLOOP 7, EOP, NREG 1, A+D) and seven A+D writes. The backend
 * reproduces exactly this class-0 state (em_gfx.h "Object units"): TEX1_1
 * 0x60, TEST_1 0x5000D, ALPHA_1 0x80000000A8 (unused: ABE 0), CLAMP_1 0,
 * COLCLAMP 1, ZBUF_1 with ZMSK 0; PRIM is overwritten by the kernel's PRE
 * template. docs/LEVEL_MATERIALS.md and docs/OWNER_DRAW.md section 3. */
int em_object_unit_gs_state_check(const uint8_t *p, const char **why)
{
    if (!p) return refuse(why, "NULL GS state");
    if (!codes_are(p, 0, 0, 0x11000000u, 0x50000008u))
        return refuse(why, "the GS state REF is not FLUSH + DIRECT 8");
    const uint64_t tag = rd64(p + 16), regs = rd64(p + 24);
    if ((tag & 0x7FFFu) != 7u || !((tag >> 15) & 1u) || ((tag >> 46) & 1u) || ((tag >> 58) & 3u) != 0u ||
        (tag >> 60) != 1u || regs != 0xEu)
        return refuse(why, "the GS state REF's GIF tag is not seven PACKED A+D writes");
    unsigned seen = 0;
    for (unsigned i = 0; i < 7u; ++i) {
        const uint8_t *q = p + 32u + 16u * i;
        const uint64_t value = rd64(q), reg = rd64(q + 8) & 0xFFu;
        unsigned bit;
        switch (reg) {
        case 0x00u: bit = 1u; break;                                            /* PRIM */
        case 0x14u: bit = 2u; if (value != 0x60u) goto bad; break;             /* TEX1_1 */
        case 0x47u: bit = 4u; if (value != 0x5000Du) goto bad; break;          /* TEST_1 */
        case 0x4Eu: bit = 8u; if ((value >> 32) & 1u) goto bad; break;         /* ZBUF_1 ZMSK */
        case 0x42u: bit = 16u; if (value != 0x80000000A8ull) goto bad; break;  /* ALPHA_1 */
        case 0x08u: bit = 32u; if (value != 0u) goto bad; break;               /* CLAMP_1 */
        case 0x46u: bit = 64u; if (value != 1u) goto bad; break;               /* COLCLAMP */
        default: goto bad;
        }
        if (seen & bit) goto bad;
        seen |= bit;
    }
    if (seen != 0x7Fu) goto bad;
    return 0;
bad:
    return refuse(why, "the GS state REF is not the class-0 set the backend reproduces");
}

/* One pass: REF 8 skin record, REF 1 arena (VIF NOPs / FLUSH only), CALL
 * `kernel`, the optional REF 2 fog-off row, REF model. */
static int parse_pass(Walk *w, uint32_t kernel, uint32_t constants[28], uint32_t *skin_address,
                      int *fog_off, uint32_t *model, uint32_t *model_qwc, const uint8_t **blocks,
                      const char **why)
{
    Tag t;
    if (next_tag(w, &t, why)) return -1;
    if (t.id != TAG_REF || t.qwc != 8u) return refuse(why, "no skin record REF 8");
    if (fetch(w, &t, why)) return -1;
    if (!codes_are(t.payload, 0, 0, 0x01000404u, 0x6C0703F9u))
        return refuse(why, "the skin record is not STCYCL 4,4 + UNPACK 7 to dmem 0x3F9");
    for (unsigned k = 0; k < 28u; ++k) constants[k] = rd32(t.payload + 16u + 4u * k);
    *skin_address = t.address;
    if (next_tag(w, &t, why)) return -1;
    /* vif_append_ref_tag's REF 1 to *D_00275674: the arena's first qword
     * (NOP, NOP, NOP, FLUSH in every capture; built at boot by 001D0F20
     * like the GS state, so checked by address). */
    if (t.id != TAG_REF || t.qwc != 1u || t.address != EM_OBJECT_UNIT_ARENA)
        return refuse(why, "no arena REF 1 to *D_00275674");
    if (next_tag(w, &t, why)) return -1;
    if (t.id != TAG_CALL || t.address != kernel) return refuse(why, "no CALL of the expected program");
    if (next_tag(w, &t, why)) return -1;
    *fog_off = 0;
    if (t.id == TAG_REF && t.address == EM_OBJECT_UNIT_FOG_OFF) {
        /* 001D37D0 / 001D3AD0 while context +0x0C bit 0 is clear. */
        if (t.qwc != 2u || fetch(w, &t, why) || !codes_are(t.payload, 0, 0, 0x01000404u, 0x6C0103FDu))
            return refuse(why, "the fog-off REF is not UNPACK 1 to dmem 0x3FD");
        for (unsigned k = 0; k < 4u; ++k) constants[16u + k] = rd32(t.payload + 16u + 4u * k);
        *fog_off = 1;
        if (next_tag(w, &t, why)) return -1;
    }
    if (t.id != TAG_REF || !t.qwc || t.qwc % EM_OBJECT_UNIT_BLOCK_QWORDS)
        return refuse(why, "no model REF of whole 0x82-qword blocks");
    if (fetch(w, &t, why)) return -1;
    *model = t.address;
    *model_qwc = t.qwc;
    *blocks = t.payload;
    return 0;
}

/* The face unit's weights CNT (001CB2C0): STCYCL 4,4 + UNPACK 2 to 0x3F3. */
static int weights_cnt(const Walk *w)
{
    if (w->at + 32u > w->size) return 0;
    const uint8_t *p = w->unit + w->at;
    return ((rd32(p) >> 28) & 7u) == TAG_CNT && (rd32(p) & 0xFFFFu) == 3u &&
           codes_are(p + 16, 0, 0, 0x01000404u, 0x6C0203F3u);
}

/* A face unit's tail after the GS state REF (001D3E40): REF 1 arena, CALL
 * 0x0023C480, REF 8 skin record, the optional fog-off REF 2, REF of the
 * face resource's blocks. */
static int parse_face(Walk *w, EmObjectUnitPieces *out, uint32_t *qwc, const uint8_t **blocks, const char **why)
{
    Tag t;
    if (next_tag(w, &t, why)) return -1;
    if (t.id != TAG_REF || t.qwc != 1u || t.address != EM_OBJECT_UNIT_ARENA)
        return refuse(why, "no arena REF 1 to *D_00275674");
    if (next_tag(w, &t, why)) return -1;
    if (t.id != TAG_CALL || t.address != EM_OBJECT_UNIT_FACE_KERNEL)
        return refuse(why, "no CALL of the expected program");
    if (next_tag(w, &t, why)) return -1;
    if (t.id != TAG_REF || t.qwc != 8u) return refuse(why, "no skin record REF 8");
    if (fetch(w, &t, why)) return -1;
    if (!codes_are(t.payload, 0, 0, 0x01000404u, 0x6C0703F9u))
        return refuse(why, "the skin record is not STCYCL 4,4 + UNPACK 7 to dmem 0x3F9");
    for (unsigned k = 0; k < 28u; ++k) out->constants[k] = rd32(t.payload + 16u + 4u * k);
    out->skin_address[0] = t.address;
    if (next_tag(w, &t, why)) return -1;
    if (t.id == TAG_REF && t.address == EM_OBJECT_UNIT_FOG_OFF) {
        /* 001D3E40 while context +0x0C bit 0 is clear, as 001D37D0. */
        if (t.qwc != 2u || fetch(w, &t, why) || !codes_are(t.payload, 0, 0, 0x01000404u, 0x6C0103FDu))
            return refuse(why, "the fog-off REF is not UNPACK 1 to dmem 0x3FD");
        for (unsigned k = 0; k < 4u; ++k) out->constants[16u + k] = rd32(t.payload + 16u + 4u * k);
        out->fog_off = 1;
        if (next_tag(w, &t, why)) return -1;
    }
    if (t.id != TAG_REF || !t.qwc || t.qwc % EM_OBJECT_UNIT_FACE_BLOCK_QWORDS)
        return refuse(why, "no face REF of whole 0x163-qword blocks");
    if (fetch(w, &t, why)) return -1;
    out->model_address = t.address;
    *qwc = t.qwc;
    *blocks = t.payload;
    return 0;
}

int em_object_unit_parse_one(const uint8_t *unit, uint32_t size, EmObjectUnitResolve resolve, void *ctx,
                             EmObjectUnitPieces *out, const char **why)
{
    if (!unit || !resolve || !out) return refuse(why, "NULL argument");
    memset(out, 0, sizeof *out);
    Walk w = {unit, size, 0, resolve, ctx};
    Tag t;
    /* 001C7420 / 001C7900: the colour CNT (NOP, FLUSH, STCYCL 1,1, UNPACK 4
     * to 0x3F5). */
    if (next_tag(&w, &t, why)) return -1;
    if (t.id != TAG_CNT || t.qwc != 5u || !codes_are(t.payload, 0, 0x11000000u, 0x01000101u, 0x6C0403F5u))
        return refuse(why, "the unit does not start with 001C7420's colour CNT");
    for (unsigned k = 0; k < 16u; ++k) out->color[k] = rd32(t.payload + 16u + 4u * k);
    /* The node CNTs: STCYCL 1,1 + UNPACK n to dmem 0, 248, ... in order. */
    uint32_t qwords = 0;
    while (peek_id(&w) == TAG_CNT && !weights_cnt(&w)) {
        if (next_tag(&w, &t, why)) return -1;
        const uint32_t chunk = t.qwc ? t.qwc - 1u : 0u;
        if (!chunk || chunk % 8u || chunk > 0xF8u ||
            !codes_are(t.payload, 0, 0, 0x01000101u, 0x6C000000u | chunk << 16 | qwords))
            return refuse(why, "a node CNT is not STCYCL 1,1 + UNPACK of whole nodes in order");
        if ((qwords + chunk) / 8u > EM_OBJECT_UNIT_MAX_NODES)
            return refuse(why, "more nodes than fit below the kernel's batch buffers");
        for (uint32_t k = 0; k < 4u * chunk; ++k) out->nodes[4u * qwords + k] = rd32(t.payload + 16u + 4u * k);
        qwords += chunk;
    }
    if (!qwords) return refuse(why, "no node CNT");
    /* A face unit (001CB3C0): the weights CNT of 001CB2C0. */
    const int face = weights_cnt(&w);
    if (face) {
        if (next_tag(&w, &t, why)) return -1;
        for (unsigned k = 0; k < 8u; ++k) out->weights[k] = rd32(t.payload + 16u + 4u * k);
        if (qwords != 8u) return refuse(why, "a face unit with other than one node");
    }
    /* 001D1F80(0, 1, 0): the GS state REF 9. */
    if (next_tag(&w, &t, why)) return -1;
    if (t.id != TAG_REF || t.qwc != 9u) return refuse(why, "no GS state REF 9");
    /* The REF names set 1, class 0 of 001D0F20's boot-built GS state
     * packets; its bytes are the class-0 set in every AREA11 capture
     * (em_object_unit_gs_state_check, tools/test_object_unit_reference.py).
     * 001D0F20 is not translated, so the port's copy of those bytes is
     * not built: the address is what is checked here. */
    if (t.address != EM_OBJECT_UNIT_GS_STATE)
        return refuse(why, "the GS state REF is not set 1 class 0 (D_00815360), the class the backend reproduces");
    EmGfxObjectUnit *u = &out->unit;
    u->color = out->color;
    u->nodes = out->nodes;
    u->node_count = qwords / 8u;
    u->constants = out->constants;
    if (face) {
        uint32_t qwc = 0;
        const uint8_t *blocks = NULL;
        if (parse_face(&w, out, &qwc, &blocks, why)) return -1;
        out->bytes = w.at;
        u->program = EM_GFX_OBJECT_FACE;
        u->weights = out->weights;
        u->blocks = blocks;
        u->block_count = qwc / EM_OBJECT_UNIT_FACE_BLOCK_QWORDS;
        return 0;
    }
    /* 001CA940: the object pass, then (flags & 1) the clip pass, which
     * starts with skin record 1's REF 8 (a next unit starts with a CNT). */
    int fog0 = 0, fog1 = 0;
    uint32_t model_qwc = 0, model2 = 0, qwc2 = 0;
    const uint8_t *blocks = NULL, *blocks2 = NULL;
    if (parse_pass(&w, EM_OBJECT_UNIT_KERNEL, out->constants, &out->skin_address[0], &fog0, &out->model_address,
                   &model_qwc, &blocks, why))
        return -1;
    int clip = 0;
    if (w.at < w.size && peek_id(&w) != TAG_CNT) {
        if (parse_pass(&w, EM_OBJECT_UNIT_CLIP_KERNEL, out->clip_constants, &out->skin_address[1], &fog1,
                       &model2, &qwc2, &blocks2, why))
            return -1;
        if (model2 != out->model_address || qwc2 != model_qwc)
            return refuse(why, "the clip pass REFs another model than the object pass");
        clip = 1;
    }
    out->fog_off = (uint32_t)fog0 | (uint32_t)fog1 << 1;
    out->bytes = w.at;
    u->program = EM_GFX_OBJECT_KERNEL;
    u->clip_constants = clip ? out->clip_constants : NULL;
    u->blocks = blocks;
    u->block_count = model_qwc / EM_OBJECT_UNIT_BLOCK_QWORDS;
    u->clip = (uint32_t)clip;
    return 0;
}

int em_object_unit_parse(const uint8_t *unit, uint32_t size, EmObjectUnitResolve resolve, void *ctx,
                         EmObjectUnitPieces *out, const char **why)
{
    if (em_object_unit_parse_one(unit, size, resolve, ctx, out, why)) return -1;
    if (out->bytes != size) return refuse(why, "bytes after the unit's last pass");
    return 0;
}

/* ------------------------------------------------------------ running -- */

static int fail(EmObjectUnitResult *r, const char *why, uint32_t block)
{
    r->why = why;
    r->why_block = block;
    return -1;
}

static EmObjectUnitTriangle *push(EmObjectUnitResult *r)
{
    if (r->count == r->capacity) {
        const uint32_t cap = r->capacity ? 2u * r->capacity : 256u;
        EmObjectUnitTriangle *t = realloc(r->tri, (size_t)cap * sizeof *t);
        if (!t) return NULL;
        r->tri = t;
        r->capacity = cap;
    }
    return &r->tri[r->count++];
}

/* The strip triangles of the packet kicked at `kick`: PRE, PRIM 0x03C,
 * REGS TEX0 ST RGBAQ XYZF2 (the object kernel's and the face program's
 * template); vertex i >= 2 without ADC draws (i-2, i-1, i) with its TEX0. */
static int push_strips(EmObjectUnitResult *r, const EmVu1ObjQword *dmem, uint32_t kick, uint32_t k)
{
    EmVu1ObjGsVertex v[EM_VU1_OBJ_VERTICES];
    uint32_t prim = 0, regs = 0;
    const int n = em_vu1_object_kernel_decode(dmem, kick, v, &prim, &regs);
    if (n < 0 || prim != EM_OBJECT_UNIT_TEMPLATE_PRIM ||
        regs != (EM_VU1_OBJ_GS_TEX0 | EM_VU1_OBJ_GS_ST | EM_VU1_OBJ_GS_RGBAQ | EM_VU1_OBJ_GS_XYZF2))
        return fail(r, "the kicked packet is not the PRIM 0x03C TEX0/ST/RGBAQ/XYZF2 form", k);
    uint8_t last[EM_VU1_OBJ_VERTICES];
    const uint32_t tris = em_vu1_object_kernel_triangles(v, n, prim, last);
    if (tris == ~0u) return fail(r, "the kicked PRIM is not a triangle strip", k);
    for (uint32_t i = 0; i < tris; ++i) {
        EmObjectUnitTriangle *t = push(r);
        if (!t) return fail(r, "out of memory", k);
        const uint32_t end = last[i];
        t->tex0 = v[end].tex0;
        t->pass = 0;
        t->block = (uint16_t)k;
        for (unsigned c = 0; c < 3u; ++c) {
            const EmVu1ObjGsVertex *g = &v[end - 2u + c];
            EmObjectUnitVertex *o = &t->v[c];
            o->x = g->x;
            o->y = g->y;
            o->z = g->z;
            o->f = g->f;
            o->rgba[0] = g->r;
            o->rgba[1] = g->g;
            o->rgba[2] = g->b;
            o->rgba[3] = g->a;
            o->s = g->s;
            o->t = g->t;
            o->q = g->q;
        }
    }
    return 0;
}

/* A model block's VIF codes as the kernel units carry them: STCYCL 4,4 +
 * UNPACK V4-32 128 to TOPS, the 32 vertices, MSCAL 0 (block 0) / MSCNT. */
static int block_codes(const uint8_t *block, uint32_t k)
{
    return codes_are(block, 0, 0, 0x01000404u, 0x6C808000u) &&
           codes_are(block + 16u * 129u, k ? 0x17000000u : 0x14000000u, 0, 0, 0);
}

static int object_pass(const EmGfxObjectUnit *u, EmObjectUnitResult *r, EmVu1ObjQword *dmem)
{
    memset(dmem, 0, EM_VU1_OBJ_DMEM_QWORDS * sizeof *dmem);
    memcpy(dmem, u->nodes, 32u * sizeof(uint32_t) * u->node_count);
    memcpy(&dmem[1013], u->color, 16u * sizeof(uint32_t));
    memcpy(&dmem[1017], u->constants, 28u * sizeof(uint32_t));
    /* The template (dmem 1020) decides what the GS takes. */
    const uint64_t tmpl = (uint64_t)dmem[1020].w[0] | (uint64_t)dmem[1020].w[1] << 32;
    if (((tmpl >> 47) & 0x7FFu) != EM_OBJECT_UNIT_TEMPLATE_PRIM || !((tmpl >> 46) & 1u))
        return fail(r, "the kernel's GIF template is not PRE with PRIM 0x03C", 0);
    EmVu1ObjState s;
    memset(&s, 0, sizeof s);
    for (uint32_t k = 0; k < u->block_count; ++k) {
        const uint8_t *block = u->blocks + 16u * EM_OBJECT_UNIT_BLOCK_QWORDS * k;
        if (!block_codes(block, k)) return fail(r, "a model block's VIF codes are not the kernel form", k);
        const uint32_t top = em_vu1_object_kernel_top(k);
        memcpy(&dmem[top], block + 16u, 128u * sizeof *dmem);
        EmVu1ObjBatch b;
        const int rc = k ? em_vu1_object_kernel_mscnt(&s, dmem, top, &b)
                         : em_vu1_object_kernel_mscal(&s, dmem, top, &b);
        if (rc || b.fault) return fail(r, "the object kernel faulted (an exponent-255 live operand)", k);
        if (push_strips(r, dmem, b.kick, k) < 0) return -1;
    }
    return 0;
}

/* A face block (001D3E40's REF): STCYCL 4,4 + UNPACK 256 to TOPS + 0, 256
 * qwords, STCYCL 4,4 + UNPACK 96 to TOPS + 0x100, 96 qwords, MSCAL 0 (block
 * 0) / MSCNT: 32 vertices of 11 qwords (VU1_FACE_MORPH.md section 2). */
static int face_block_codes(const uint8_t *block, uint32_t k)
{
    return codes_are(block, 0, 0, 0x01000404u, 0x6C008000u) &&
           codes_are(block + 16u * 257u, 0, 0, 0x01000404u, 0x6C608100u) &&
           codes_are(block + 16u * 354u, k ? 0x17000000u : 0x14000000u, 0, 0, 0);
}

static int face_pass(const EmGfxObjectUnit *u, EmObjectUnitResult *r, EmVu1ObjQword *dmem)
{
    memset(dmem, 0, EM_VU1_OBJ_DMEM_QWORDS * sizeof *dmem);
    memcpy(dmem, u->nodes, 32u * sizeof(uint32_t) * u->node_count);
    memcpy(&dmem[EM_VU1_FACE_WEIGHTS], u->weights, 8u * sizeof(uint32_t));
    memcpy(&dmem[1013], u->color, 16u * sizeof(uint32_t));
    memcpy(&dmem[1017], u->constants, 28u * sizeof(uint32_t));
    EmVu1FaceState s;
    memset(&s, 0, sizeof s);
    for (uint32_t k = 0; k < u->block_count; ++k) {
        const uint8_t *block = u->blocks + 16u * EM_OBJECT_UNIT_FACE_BLOCK_QWORDS * k;
        if (!face_block_codes(block, k)) return fail(r, "a face block's VIF codes are not the face form", k);
        const uint32_t top = em_vu1_face_morph_top(k);
        memcpy(&dmem[top], block + 16u, 256u * sizeof *dmem);
        memcpy(&dmem[top + 256u], block + 16u * 258u, 96u * sizeof *dmem);
        /* dmem 1020 (the template) is re-read by every batch. */
        const uint64_t tmpl = (uint64_t)dmem[1020].w[0] | (uint64_t)dmem[1020].w[1] << 32;
        if (((tmpl >> 47) & 0x7FFu) != EM_OBJECT_UNIT_TEMPLATE_PRIM || !((tmpl >> 46) & 1u))
            return fail(r, "the kernel's GIF template is not PRE with PRIM 0x03C", k);
        EmVu1FaceBatch b;
        const int rc = k ? em_vu1_face_morph_mscnt(&s, dmem, top, &b) : em_vu1_face_morph_mscal(&s, dmem, top, &b);
        if (rc || b.fault) return fail(r, "the face program faulted (an exponent-255 live operand)", k);
        if (push_strips(r, dmem, b.kick, k) < 0) return -1;
    }
    return 0;
}

static uint32_t fbits(float f)
{
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

static int clip_pass(const EmGfxObjectUnit *u, EmObjectUnitResult *r)
{
    EmVu1Qword *dmem = malloc(EM_VU1_DMEM_QWORDS * sizeof *dmem);
    EmVu1Qword *qw = malloc(EM_VU1_OBJECT_CLIP_MAX_QWORDS * sizeof *qw);
    EmVu1ObjectClipResult *res = malloc(sizeof *res);
    EmVu1ObjectClipTriangle *tri = malloc(EM_VU1_OBJECT_CLIP_MAX_TRIANGLES * 32u * sizeof *tri);
    int rc = 0;
    if (!dmem || !qw || !res || !tri) { rc = fail(r, "out of memory", 0); goto done; }
    if (em_vu1_object_clip_image(dmem, u->nodes, u->node_count, u->color, u->clip_constants)) {
        rc = fail(r, "the clip image refused the unit", 0);
        goto done;
    }
    for (uint32_t k = 0; k < u->block_count; ++k) {
        const uint8_t *block = u->blocks + 16u * EM_OBJECT_UNIT_BLOCK_QWORDS * k;
        const int32_t top = em_vu1_object_clip_batch(dmem, k, block);
        if (top < 0) { rc = fail(r, "a model block's VIF codes are not the clip form", k); goto done; }
        memset(res, 0, sizeof *res);
        res->qw = qw;
        res->capacity = EM_VU1_OBJECT_CLIP_MAX_QWORDS;
        if (em_vu1_object_clip_run(dmem, (uint32_t)top, res) || res->fault) {
            rc = fail(r, "the clip program faulted (an FTOI outside int32 or a GIF packet without EOP)", k);
            goto done;
        }
        const int n = em_vu1_object_clip_triangles(res, tri, EM_VU1_OBJECT_CLIP_MAX_TRIANGLES * 32u);
        if (n < 0) { rc = fail(r, "the clip program kicked a packet outside its form", k); goto done; }
        for (int i = 0; i < n; ++i) {
            if (tri[i].prim != EM_OBJECT_UNIT_CLIP_PRIM) {
                rc = fail(r, "a clip packet's PRIM is not 0x03B", k);
                goto done;
            }
            EmObjectUnitTriangle *t = push(r);
            if (!t) { rc = fail(r, "out of memory", k); goto done; }
            t->tex0 = tri[i].tex0;
            t->pass = 1;
            t->block = (uint16_t)k;
            for (unsigned c = 0; c < 3u; ++c) {
                const EmVu1ObjectClipVertex *g = &tri[i].v[c];
                EmObjectUnitVertex *o = &t->v[c];
                o->x = g->x;
                o->y = g->y;
                o->z = g->z;
                o->f = g->f;
                memcpy(o->rgba, g->rgba, 4);
                o->s = fbits(g->s);
                o->t = fbits(g->t);
                o->q = fbits(g->q);
            }
        }
    }
done:
    free(dmem);
    free(qw);
    free(res);
    free(tri);
    return rc;
}

int em_object_unit_run(const EmGfxObjectUnit *u, EmObjectUnitResult *r)
{
    if (!r) return -1;
    r->count = r->object_triangles = 0;
    r->why = NULL;
    r->why_block = 0;
    if (!u) return fail(r, "NULL unit", 0);
    const int face = u->program == EM_GFX_OBJECT_FACE;
    if (u->program != EM_GFX_OBJECT_KERNEL && !face)
        return fail(r, "not the object kernel 0x0023C750 or the face program 0x0023C480", 0);
    if (!u->color || !u->nodes || !u->constants || !u->blocks || !u->block_count || !u->node_count ||
        (u->clip && !u->clip_constants) || (face && !u->weights))
        return fail(r, "a unit piece is missing", 0);
    if (u->node_count > EM_OBJECT_UNIT_MAX_NODES) return fail(r, "more nodes than the kernel's layout holds", 0);
    /* The face program's batch buffers start at dmem 0x20: one node only;
     * it has no clip pass (001D3E40 appends none). */
    if (face && (u->node_count != 1u || u->clip)) return fail(r, "a face unit with other than one node, or clip", 0);
    EmVu1ObjQword *dmem = malloc(EM_VU1_OBJ_DMEM_QWORDS * sizeof *dmem);
    if (!dmem) return fail(r, "out of memory", 0);
    int rc = face ? face_pass(u, r, dmem) : object_pass(u, r, dmem);
    free(dmem);
    r->object_triangles = r->count;
    if (!rc && u->clip) rc = clip_pass(u, r);
    return rc;
}

void em_object_unit_result_free(EmObjectUnitResult *r)
{
    if (!r) return;
    free(r->tri);
    memset(r, 0, sizeof *r);
}
