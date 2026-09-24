/* em_player_record_pose.c - the player's pose on its own record (one owner).
 * See em_player_record_pose.h. */
#include "game/em_player_record_pose.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* ---- loading ------------------------------------------------------------ */

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file) return NULL;
    uint8_t *bytes = NULL;
    long length = -1;
    if (fseek(file, 0, SEEK_END) == 0) length = ftell(file);
    if (length > 0 && length <= 0x4000000 && fseek(file, 0, SEEK_SET) == 0) {
        bytes = malloc((size_t)length);
        if (bytes && fread(bytes, 1, (size_t)length, file) != (size_t)length) {
            free(bytes);
            bytes = NULL;
        }
    }
    fclose(file);
    if (bytes) *size = (uint32_t)length;
    return bytes;
}

int em_player_record_pose_load_bytes(EmPlayerRecordPose *pose, const uint8_t *bank, uint32_t bank_size,
                                     const uint8_t *row0, uint32_t row0_size)
{
    if (!pose || !bank || !row0) return -1;
    em_player_record_pose_free(pose);
    /* The bank: its count word, then one directory word per clip (001C6120
     * reads bank + 4 + 4 * clip). */
    if (bank_size < 4 || rd32(bank) != EM_PLAYER_POSE_ROWS ||
        bank_size < 4 + 4 * (uint32_t)EM_PLAYER_POSE_ROWS)
        return -1;
    /* The row column: 'EMCH', version 1, count, count signed halfwords. */
    if (row0_size != 12 + 2 * (uint32_t)EM_PLAYER_POSE_ROWS || memcmp(row0, "EMCH", 4) != 0 ||
        rd32(row0 + 4) != 1 || rd32(row0 + 8) != EM_PLAYER_POSE_ROWS)
        return -1;
    uint8_t *copy = malloc(bank_size);
    if (!copy) return -1;
    memcpy(copy, bank, bank_size);
    pose->bank = copy;
    pose->bank_size = bank_size;
    pose->bank_clips = rd32(bank);
    for (unsigned i = 0; i < EM_PLAYER_POSE_ROWS; ++i)
        pose->row0[i] = (int16_t)rd16(row0 + 12 + 2 * i);
    pose->loaded = 1;
    return 0;
}

int em_player_record_pose_load(EmPlayerRecordPose *pose, const char *bank_path, const char *row0_path)
{
    uint32_t bank_size = 0, row0_size = 0;
    uint8_t *bank = read_file(bank_path, &bank_size);
    uint8_t *row0 = read_file(row0_path, &row0_size);
    int result = bank && row0 ? em_player_record_pose_load_bytes(pose, bank, bank_size, row0, row0_size)
                              : -1;
    free(bank);
    free(row0);
    return result;
}

void em_player_record_pose_free(EmPlayerRecordPose *pose)
{
    if (!pose) return;
    free(pose->bank);
    uint8_t *tables = pose->tables;
    uint32_t base = pose->tables_base, size = pose->tables_size;
    memset(pose, 0, sizeof *pose);
    pose->tables = tables;
    pose->tables_base = base;
    pose->tables_size = size;
}

int em_player_record_pose_load_tables(EmPlayerRecordPose *pose, const char *path)
{
    if (!pose) return -1;
    uint32_t size = 0;
    uint8_t *bytes = read_file(path, &size);
    if (!bytes) return -1;
    const uint32_t base = UINT32_C(0x00248740), span = UINT32_C(0x00248ACC) - base;
    if (size != 16 + span || memcmp(bytes, "EMRG", 4) != 0 || rd32(bytes + 4) != 1 ||
        rd32(bytes + 8) != base || rd32(bytes + 12) != span) {
        free(bytes);
        return -1;
    }
    free(pose->tables);
    pose->tables = malloc(span);
    if (!pose->tables) { free(bytes); pose->tables_size = 0; return -1; }
    memcpy(pose->tables, bytes + 16, span);
    pose->tables_base = base;
    pose->tables_size = span;
    free(bytes);
    return 0;
}

/* ---- binding ------------------------------------------------------------ */

int em_player_record_pose_attach(EmPlayerRecordPose *pose, EmPlayerLiveActor *actor,
                                 uint8_t *d8106F3, EmPlayerStageScene *scene,
                                 EmPlayerStageGlobals *globals)
{
    if (!pose || !pose->loaded || !actor || !d8106F3 || !scene || !globals) return -1;
    pose->attached = 0;
    pose->actor = actor;
    memset(pose->nodes, 0, sizeof pose->nodes);

    EmPoseGlobals *g = &pose->globals;
    memset(g, 0, sizeof *g);
    g->d8106F3 = d8106F3;
    g->spad3400 = pose->spad3400;
    g->spad3440 = pose->spad3440;
    g->spad3600 = pose->spad3600;
    g->spad3760 = pose->spad3760;
    g->spad3A3C = &pose->spad3A3C;
    g->spad38B0 = pose->spad38B0;
    g->spad3A20 = &pose->spad3A20;
    g->column = &pose->column;

    memset(&pose->advance, 0, sizeof pose->advance);
    pose->advance.stage = scene;
    pose->advance.globals = globals;
    EmPlayerStageCallees *c = &pose->advance.callees;
    c->context = &pose->host;
    c->bone_init = em_pose_host_stage_bone_init;
    c->clip_init = em_pose_host_stage_clip_init;
    c->clip_resolve = em_pose_host_stage_clip_resolve;
    c->skeleton_frame = em_pose_host_stage_skeleton_frame;
    c->w001C8710 = em_pose_host_stage_8710;
    c->w001C87C0 = em_pose_host_stage_87C0;
    c->sample_bones = em_pose_host_stage_sample_bones;
    c->request = em_pose_host_stage_request;

    EmPoseHost *h = &pose->host;
    memset(h, 0, sizeof *h);
    h->region[0] = (EmPoseRegion){ EM_PLAYER_POSE_BANK_ADDRESS, pose->bank_size, pose->bank, 0 };
    h->region[1] = (EmPoseRegion){ EM_PLAYER_POSE_NODE_ADDRESS, sizeof pose->nodes, pose->nodes, 1 };
    h->region[2] = (EmPoseRegion){ EM_PLAYER_POSE_RECORD_ADDRESS, EM_PLAYER_ACTOR_SIZE, actor->bytes, 1 };
    h->region_count = 3;
    if (pose->tables)
        h->region[h->region_count++] = (EmPoseRegion){ pose->tables_base, pose->tables_size,
                                                        pose->tables, 0 };
    h->globals = g;
    h->callees.advance_context = &pose->advance;
    h->callees.advance = em_pose_host_player_advance;

    /* The structural words of the player record (every captured AREA11
     * image: +C = 0x15, +40 = 0xD689C0, +60..+6C = 1.0, +110 + 4i =
     * 0x7D5840 + 0xD0 * i, +164 = 0). */
    uint8_t *r = actor->bytes;
    r[0xC] = EM_PLAYER_POSE_NODES;
    wr32(r + 0x40, EM_PLAYER_POSE_BANK_ADDRESS);
    for (unsigned axis = 0; axis < 4; ++axis) wr32(r + 0x60 + 4 * axis, UINT32_C(0x3F800000));
    for (unsigned i = 0; i < EM_PLAYER_POSE_NODES; ++i)
        wr32(r + 0x110 + 4 * i, EM_PLAYER_POSE_NODE_ADDRESS + EM_POSE_NODE_BYTES * i);
    wr32(r + 0x110 + 4 * EM_PLAYER_POSE_NODES, 0);
    pose->attached = 1;
    return 0;
}

int em_player_record_pose_ready(const EmPlayerRecordPose *pose)
{
    return pose && pose->loaded && pose->attached && pose->actor;
}

EmPoseHost *em_player_record_pose_host(EmPlayerRecordPose *pose)
{
    return em_player_record_pose_ready(pose) ? &pose->host : NULL;
}

EmPlayerStageHost *em_player_record_pose_advance_host(EmPlayerRecordPose *pose)
{
    return em_player_record_pose_ready(pose) ? &pose->advance : NULL;
}

/* ---- the routines -------------------------------------------------------- */

#define READY(pose) do { if (!em_player_record_pose_ready(pose)) return -1; } while (0)
#define RECORD(pose) (pose)->actor->bytes, EM_PLAYER_ACTOR_SIZE

int em_player_record_pose_default(EmPlayerRecordPose *pose, int clip)
{
    READY(pose);
    return em_pose_host_001C63E0(&pose->host, RECORD(pose), clip);
}

int em_player_record_pose_request(EmPlayerRecordPose *pose, int clip, int flags, float blend, int *result)
{
    READY(pose);
    return em_pose_host_001749A0(&pose->host, RECORD(pose), clip, flags, blend, result);
}

int em_player_record_pose_arbiter(EmPlayerRecordPose *pose, int clip, float blend, float frame, int *result)
{
    READY(pose);
    return em_pose_host_001749F0(&pose->host, RECORD(pose), clip, blend, frame, result);
}

int em_player_record_pose_advance(EmPlayerRecordPose *pose, float step, uint32_t *flags)
{
    READY(pose);
    return em_player_stage_anim_advance(&pose->advance, pose->actor, step, flags);
}

int em_player_record_pose_frames(EmPlayerRecordPose *pose, int clip, int32_t *frames)
{
    READY(pose);
    return em_pose_host_001C61D0(&pose->host, EM_PLAYER_POSE_BANK_ADDRESS, clip, frames);
}

int em_player_record_pose_eval_skeleton(EmPlayerRecordPose *pose)
{
    READY(pose);
    return em_pose_host_001C6DA0(&pose->host, RECORD(pose));
}

int em_player_record_pose_skeleton(EmPlayerRecordPose *pose)
{
    READY(pose);
    return em_pose_host_001C68C0(&pose->host, RECORD(pose));
}

int em_player_record_pose_row0(const EmPlayerRecordPose *pose, int clip, int16_t *value)
{
    if (!pose || !pose->loaded || !value || clip < 0 || clip >= EM_PLAYER_POSE_ROWS) return -1;
    *value = pose->row0[clip];
    return 0;
}

/* 0015BCF0, after 0015BA50 and +BC = 1.0 (decomp src/func_0015BCF0.c). */
int em_player_record_pose_animate(EmPlayerRecordPose *pose)
{
    READY(pose);
    const uint8_t *r = pose->actor->bytes;
    uint8_t mode = r[0x2F3];
    if (mode == 0) {
        if (r[0x303] != 0) return 0;
        int16_t row;
        if (em_player_record_pose_row0(pose, (int16_t)rd16(r + 0x20C), &row) < 0) return -1;
        if (row != 0) return em_pose_host_001C6DA0(&pose->host, RECORD(pose));
        return em_pose_host_001C68C0(&pose->host, RECORD(pose));
    }
    if (mode == 3 || mode == 4) return em_pose_host_001C68C0(&pose->host, RECORD(pose));
    return em_pose_host_001C6960(&pose->host, RECORD(pose));
}

/* ---- views ------------------------------------------------------------------ */

unsigned em_player_record_pose_clip(const EmPlayerRecordPose *pose)
{
    return pose && pose->actor ? rd16(pose->actor->bytes + 0x2C) & 0x7FFFu : 0;
}

int em_player_record_pose_transition(const EmPlayerRecordPose *pose)
{
    return pose && pose->actor ? (rd16(pose->actor->bytes + 0x2C) & 0x8000u) != 0 : 0;
}

float em_player_record_pose_clock(const EmPlayerRecordPose *pose)
{
    float clock = 0;
    if (pose && pose->actor) memcpy(&clock, pose->actor->bytes + 0x3C, 4);
    return clock;
}

int16_t em_player_record_pose_requested(const EmPlayerRecordPose *pose)
{
    return pose && pose->actor ? (int16_t)rd16(pose->actor->bytes + 0x20C) : 0;
}

int em_player_record_pose_palette(const EmPlayerRecordPose *pose, float out[EM_PLAYER_POSE_PALETTE_BONES * 16])
{
    if (!em_player_record_pose_ready(pose) || !out) return -1;
    for (unsigned i = 0; i < EM_PLAYER_POSE_NODES; ++i) {
        const uint8_t *m = pose->nodes + EM_POSE_NODE_BYTES * i + 0x90;
        memcpy(out + 16 * i, m, 64);
        for (unsigned k = 0; k < 16; ++k)
            if (!isfinite(out[16 * i + k])) return -1;
    }
    /* The model's trailing slot is the identity in actor space, so in world
     * space it is the owner matrix the evaluation built: +D0 (build_trs of
     * +B0 / +C0 / +60, or the identity for 001C6960). */
    float *slot = out + 16 * EM_PLAYER_POSE_NODES;
    memcpy(slot, pose->actor->bytes + 0xD0, 64);
    for (unsigned k = 0; k < 16; ++k)
        if (!isfinite(slot[k])) return -1;
    return 0;
}

int em_player_record_pose_parents(EmPlayerRecordPose *pose, int clip, int32_t parents[EM_PLAYER_POSE_NODES])
{
    uint32_t header;
    READY(pose);
    if (!parents || em_pose_host_001C6120(&pose->host, EM_PLAYER_POSE_BANK_ADDRESS, clip, &header) < 0)
        return -1;
    uint32_t at = header - EM_PLAYER_POSE_BANK_ADDRESS;
    if (header < EM_PLAYER_POSE_BANK_ADDRESS || at > pose->bank_size ||
        pose->bank_size - at < 0x20 + 4 * (uint32_t)EM_PLAYER_POSE_NODES)
        return -1;
    for (unsigned i = 0; i < EM_PLAYER_POSE_NODES; ++i)
        parents[i] = (int16_t)rd16(pose->bank + at + 0x20 + 4 * i);
    return 0;
}
