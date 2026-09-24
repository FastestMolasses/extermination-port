/* The world-owner draw workers of 001CAA00 / 001CA990 and the AREA11 world
 * model bank. Docs: docs/OWNER_DRAW.md.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112):
 *   001CA7B0  cull/clip test of a bounding sphere: the position is taken
 *             through the view matrix D_00810610 (00102948 quadword copy
 *             with w = 1.0, then 001026A0), its view z and its dot products
 *             with the four cull planes at context +0x2410..+0x244F
 *             (00102738) are compared with -radius (culled: -1) and
 *             +radius (bits 1, 2, 4, 8, 0x10)
 *   001CA940  kernel choice: flags & 1 -> 001D3C30, else 001D38F0
 *   001D38F0 / 001D3C30  tail thunks to 001D38A0 / 001D3BA0 on channel 0
 *   001D38A0  REF 8 qw to the skin record D_00816440 + (context +0x9C) * 0x80,
 *             then 001D37D0
 *   001D3BA0  001D38A0, then REF 8 qw to D_00816540 + (context +0x9C) * 0x80,
 *             then 001D3AD0
 *   001D37D0 / 001D3AD0 (NEARMISS / C; the .s was followed)
 *             vif_append_ref_tag(chan, 0x0023C750 / 0x002354A0), then REF 2 qw
 *             to D_002514B0 only while 001D2910(0) is 0, then REF
 *             (model +0x04 low halfword) qw to model +0x40
 *   vif_append_ref_tag (001D2090)  REF 1 qw to *D_00275674, the CALL target
 *             stored at context +0x50 + 4 * chan, then a CALL tag (qwc 0) to it
 *   001D2910(0) = 001D2710(0): context word +0x0C bit 0
 *   001C6120  model table lookup over the exported bank (em_world_models_*)
 *
 * Verified by tools/test_owner_draw_reference.py, which executes the original
 * instructions (synthetic and captured AREA11 RAM) and compares every flag,
 * packet byte, cursor and context word; it also executes the whole original
 * 001CAA00 of every captured crate, drum, fan, truck, elevator and husk and
 * compares the unit with the captured display list, and runs the native
 * chain (em_owner_services 001CAA00 with these workers) against both.
 * tests/owner_draw_test.c pins the fail-stop contract.
 *
 * Arithmetic: every EE COP1 and VU0-macro instruction goes through
 * game/em_ee_float.h under its real form; no host float operation. The
 * position and radius cross as raw register bits.
 *
 * Packet memory follows em_owner_services_original.h: the channel cursor is
 * a host pointer into the display-list buffer (EmOwnerServicesChannel), so
 * the same channel array serves 001C7420 and 001CA940. The words written
 * INTO the tags are original addresses (the targets the DMAC would fetch);
 * the renderer resolves them (docs/OWNER_DRAW.md section 4). DMA tag bytes
 * +2 and +8..+0xF are never written, as in the original.
 *
 * A reached NULL view, an index outside the owned storage or a refused float
 * form latches a fault (fail-stop); every later call then returns -1.
 * stdint only; no dependency on any port subsystem beyond the owner-services
 * types it shares. */
#ifndef EM_OWNER_DRAW_ORIGINAL_H
#define EM_OWNER_DRAW_ORIGINAL_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_owner_services_original.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Original addresses the tags carry. */
#define EM_OWNER_DRAW_SKIN_RECORD_0 UINT32_C(0x00816440) /* 001D38A0 */
#define EM_OWNER_DRAW_SKIN_RECORD_1 UINT32_C(0x00816540) /* 001D3BA0 */
#define EM_OWNER_DRAW_KERNEL        UINT32_C(0x0023C750) /* 001D37D0 CALL */
#define EM_OWNER_DRAW_CLIP_KERNEL   UINT32_C(0x002354A0) /* 001D3AD0 CALL */
#define EM_OWNER_DRAW_D_002514B0    UINT32_C(0x002514B0) /* the optional REF 2 */

/* Fault codes (numerically the EM_SCENE_FAULT_* / EM_OWNER_FAULT_* codes). */
enum {
    EM_OWNER_DRAW_FAULT_NONE = 0,
    EM_OWNER_DRAW_FAULT_NULL_WORKER = 1, /* a reached data view is NULL */
    EM_OWNER_DRAW_FAULT_BAD_INDEX = 4,   /* channel, cursor window, model or bank index */
    EM_OWNER_DRAW_FAULT_UNMEASURED_FORM = 6 /* em_ee_float refused a form */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_OWNER_DRAW_FAULT_* */
} EmOwnerDrawFault;

/* ---- The AREA11 world model bank (*D_0028A59C) ---------------------------
 * Loaded from the exporter's assets/scene_snow/world_models.emwm (tools/
 * export_world_models.py): the original table and every model it indexes,
 * byte for byte as they lie in EE RAM from D_0028A59C, plus the address they
 * lie at (the handles 001C6120 returns and the REF targets are those
 * addresses). The bank keeps views into the caller's file buffer. */
#define EM_WORLD_MODELS_MAGIC 0x4D574D45u /* "EMWM" */
#define EM_WORLD_MODELS_VERSION 1u
#define EM_WORLD_MODELS_MAX 64u
#define EM_WORLD_MODELS_MAX_RECORDS 512u
#define EM_WORLD_MODELS_BLOCK_QWORDS 0x82u /* one 32-vertex block */

typedef struct {
    EmOwnerModel model;     /* the owner-services view (+0x08, +0x20, skeleton) */
    uint32_t id;            /* table index (001C6120 id) */
    uint32_t address;       /* original address = table + (word[1 + id] >> 2 << 2) */
    uint32_t w04;           /* +0x04: qword count of the block data at +0x40 */
    uint32_t blocks;        /* +0x00: 32-vertex blocks (w04 == blocks * 0x82) */
    uint32_t size;          /* bytes: +0x0C skeleton offset + 0x50 * bones */
    const uint8_t *bytes;   /* the model's original bytes (size) */
} EmWorldModel;

typedef struct {
    uint32_t table_address;      /* the original *D_0028A59C */
    uint32_t count;              /* table word 0 */
    const uint8_t *span;         /* original bytes from table_address */
    uint32_t span_size;
    uint32_t model_count;        /* == count */
    EmWorldModel models[EM_WORLD_MODELS_MAX];
    EmOwnerSkeletonRecord records[EM_WORLD_MODELS_MAX_RECORDS];
    uint32_t record_count;
} EmWorldModels;

/* Parse an EMWM file held in `data` (kept as a view). 0, or -1 on any
 * structural mismatch (every model's header, block codes and skeleton range
 * are checked; the bank is left zeroed). */
int em_world_models_parse(EmWorldModels *bank, const uint8_t *data, size_t size);
/* 001C6120(bank word, id): the original arithmetic (id & 0x7FFF; word
 * 1 + id; arithmetic >> 2 << 2) over the exported table. -1 when `bank_word`
 * is not the exported table's address, the id is not a table entry, or the
 * result is not the start of an exported model. */
int em_world_models_001C6120(const EmWorldModels *bank, uint32_t bank_word, uint32_t id,
                             uint32_t *handle);
/* The model at an original address (a 001C6120 handle), or NULL. */
const EmWorldModel *em_world_models_at(const EmWorldModels *bank, uint32_t address);
/* The model whose owner-services view is `model`, or NULL. */
const EmWorldModel *em_world_models_of(const EmWorldModels *bank, const EmOwnerModel *model);

/* ---- The draw workers ---------------------------------------------------- */

/* Canonical storage views. Each pointer is required only when reached. */
typedef struct {
    const uint32_t *d00810610;       /* 16 words: the view matrix (001CA7B0) */
    const uint32_t *ctx_2410;        /* 16 words: context +0x2410..+0x244F, four planes */
    const uint32_t *ctx_0C;          /* context word +0x0C: 001D2910(0) reads bit 0 */
    const uint32_t *ctx_9C;          /* context word +0x9C: the skin-record slot */
    const uint32_t *d00275674;       /* the arena base word (REF 1 target) */
    EmOwnerServicesChannel *channel; /* context +0x10 + 4 * chan (shared with 001C7420) */
    uint32_t channel_count;
    uint32_t *ctx_50;                /* context +0x50 + 4 * chan: CALL targets */
    uint32_t ctx_50_count;
    const EmWorldModels *models;     /* resolves EmOwnerModel views (001CA940) */
} EmOwnerDrawWorld;

typedef struct {
    EmOwnerDrawWorld world;
    EmOwnerDrawFault fault; /* latched; cleared only by the caller */
} EmOwnerDraw;

/* 001CA7B0(position, f12 = radius). `position` is the four words of the
 * quadword the original copies (lane w is replaced by 1.0, so it never
 * matters); `radius` is the raw f12 bits. *flags = -1 (culled) or 0..0x1F.
 * Returns 0, or -1 on a fault. */
int em_owner_draw_001CA7B0(EmOwnerDraw *s, const uint32_t position[4], uint32_t radius,
                           int32_t *flags);

/* 001CA940(flags, model) with the model given by its original address and
 * its +0x04 word. Returns 0, or -1 on a fault (checked before any write). */
int em_owner_draw_001CA940_at(EmOwnerDraw *s, int32_t flags, uint32_t model_address,
                              uint32_t model_w04);
/* 001CA940(flags, model) for an owner-services model view that belongs to
 * world.models (the 001CA6E0 binding points +0x44 into the bank). */
int em_owner_draw_001CA940(EmOwnerDraw *s, int32_t flags, const EmOwnerModel *model);

/* The pieces, for callers that reach them directly (chan is the register). */
int em_owner_draw_001D38A0(EmOwnerDraw *s, int32_t chan, uint32_t model_address, uint32_t model_w04);
int em_owner_draw_001D3BA0(EmOwnerDraw *s, int32_t chan, uint32_t model_address, uint32_t model_w04);

/* Bytes 001CA940 appends for `flags`: 0x40 (0x80 when flags & 1), plus
 * 0x10 per 001D37D0 / 001D3AD0 while the D_002514B0 REF is emitted. */
uint32_t em_owner_draw_001CA940_bytes(int32_t flags, int with_ref2);

#ifdef __cplusplus
}
#endif

#endif /* EM_OWNER_DRAW_ORIGINAL_H */
