/* EM_START_AT=<name> (em_start_at.h; docs/STARTUP.md "Developer switches"). */
#define _POSIX_C_SOURCE 200809L
#include "game/em_start_at.h"
#include "game/em_frame.h"
#include "game/em_random.h"
#include "em_input.h"
#include "game/em_scene_bindings.h"


#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define START_SAVES "build/start_saves"
#define SAVE_DIR "BASCUS-97112-DS00-00"
#define BLOCK 0x640u
#define MAX_FILES 16

/* The start points tools/build_start_saves.py builds, with the area byte
 * D_00810700 +0 of each one's slot (docs/SAVE_GAME.md section 7). */
static const struct {
    const char *name;
    uint8_t area;
} k_points[] = {
    {"area01", 0x01},
    {"area00", 0x00},
    {"area01-revisit", 0x01},
    {"area02", 0x02},
    {"a02_01", 0x02},
    {"a02_02", 0x02},
    {"a02_03", 0x02},
    {"a02_04", 0x02},
    {"a02_05", 0x02},
    {"a01_03", 0x01},
    {"a01_05", 0x01},
    {"a01_s7", 0x01},
    {"a01_07", 0x01},
    {"a00_01", 0x00},
    {"a00_04", 0x00},
    {"a01r_01", 0x01},
    /* the middle of the route to the first boss (lane C1) */
    {"area22", 0x16},
    {"a22_01", 0x16},
    {"a22_02", 0x16},
    {"a01u", 0x01},
    {"a01u_01", 0x01},
    {"a01u_02", 0x01},
    {"a01u_s0", 0x01},
    {"area06", 0x06},
    {"a06_01", 0x06},
    {"a06_02", 0x06},
    {"a06_03", 0x06},
    {"a06_04", 0x06},
    {"a06_05", 0x06},
    {"a06b", 0x06},
    {"a06b_01", 0x06},
    {"a01v", 0x01},
    {"a01v_01", 0x01},
    {"a01v_02", 0x01},
    {"a22b", 0x16},
    {"a22b_01", 0x16},
    {"a04_00", 0x04},
    {"a04_01", 0x04},
    {"a04_02", 0x04},
    {"a04_03", 0x04},
    {"a04_04", 0x04},
    {"a04_05", 0x04},
    {"a04b_00", 0x04},
    {"a04b_01", 0x04},
    {"a04b_02", 0x04},
    {"a04b_03", 0x04},
    {"a04b_04", 0x04},
    {"boss", 0x04},
    /* AREA15 and AREA03 (lane K1) */
    {"area15", 0x0F},
    {"a15_00", 0x0F},
    {"a15b_00", 0x0F},
    {"a15b_01", 0x0F},
    {"a15b_02", 0x0F},
    {"a15b_03", 0x0F},
    {"a15b_04", 0x0F},
    {"a03_00", 0x03},
};

static struct {
    int checked, on, report;
    const char *name;
    char root[512];
} s;

/* EM_START_AT_PAD=<file>: a recorded pad script (the level smoke's EMA1
 * format: "EMA1 1 <frames> <gap>", then "<frame> <buttons hex> <lx> <ly>"
 * rows in frame order) played from control as the whole pad. */
enum { PAD_MAX = 4096 };
typedef struct { int f; uint16_t buttons; uint8_t lx, ly; } PadRow;
enum { PATH_MAX_ROWS = 40000 };
static struct {
    int loaded, active, done;
    unsigned count, last, start;
    PadRow row[PAD_MAX];
    EmPadState now;
    /* EM_START_AT_PATH=<file>: the recording's player x, z per frame; the
     * stick then steers toward the recorded position a few frames ahead
     * (camera-relative, at the recorded stick's magnitude), so the route
     * survives a loaded camera that differs from the recording's. */
    unsigned path_count, hold;
    float last_pos[2];         /* the player's x, z at the previous step */
    int t;                     /* the script's clock (frames since control) */
    /* EM_START_AT_RAND=<hex>: the recording's rand() state word at the
     * beat's source state (*D_0024295C + 0x58), put in place at control
     * (comparison only, as EM_RAND_REPLAY: the creatures then draw the
     * recording's stream from the beat's start). */
    int rand_set;
    uint32_t rand_state;
    /* EM_START_AT_FADE=<level 0..255>: the transition level at the
     * recording's row 0 (D_0028A9A0 +4; 0 when no fade runs there). A
     * loaded game fades in with the player's input refused; the script's
     * row 0 waits for the same level, and the recording's rand() word is
     * put in place then, so the world's draws start with the script. */
    int fade_given, fade_wait;
    unsigned fade_level;
    /* EM_START_AT_RANDS=<file> ("f state" lines, f increasing): the
     * recording's state word at those script frames (comparison only). */
    unsigned rands, rand_next;
    struct { int f; uint32_t state; } rand_at[8192];
    /* EM_START_AT_RESTORE=<ram image>:<f>:<clock>[,...]: at that script frame the
     * hall bugs, the items' glow timers and the NPC's face take the image's
     * bytes and 0x70003B68 the recording's value (the actors' staggered
     * periods read it; comparison only; em_scene_restore_bugs). */
    const char *restore;
    int restore_f;
    unsigned restore_clock;
    /* EM_START_AT_SYNC=<byte address>:<value>:<f>: when that progress byte
     * first holds the value the script's clock is set to f (a scripted
     * stretch started off the recording's frame keeps the recording's rows
     * after it; comparison only). */
    unsigned sync_address, sync_value;
    int sync_f, sync_done;
    unsigned sync_counter;
    /* EM_START_AT_MARKS: the script frames to report the block at. */
    unsigned mark_count, mark_next;
    int mark[64];
    float path[PATH_MAX_ROWS][2];
    float path_yaw[PATH_MAX_ROWS];
    int path_has_yaw;
    unsigned turn;             /* frames spent turning for the coming press */
} pad;

static int refuse(const char *why, const char *detail)
{
    fprintf(stderr, "EM_START_AT=%s refused: %s%s%s\n", getenv("EM_START_AT"), why, detail ? ": " : "",
            detail ? detail : "");
    return -1;
}

/* ---- SHA-256 (FIPS 180-4), for the card files start.json lists ---- */
static const uint32_t k_sha[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t x, unsigned n) { return x >> n | x << (32 - n); }

static void sha_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (unsigned i = 16; i < 64; ++i) {
        const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (unsigned i = 0; i < 64; ++i) {
        const uint32_t t1 = k + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k_sha[i] + w[i];
        const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

static void sha256_hex(const uint8_t *data, size_t size, char out[65])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= size; i += 64) sha_block(h, data + i);
    uint8_t tail[128] = {0};
    const size_t rest = size - i;
    memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    const size_t n = rest < 56 ? 64 : 128;
    const uint64_t bits = (uint64_t)size * 8u;
    for (unsigned j = 0; j < 8; ++j) tail[n - 1 - j] = (uint8_t)(bits >> (8 * j));
    sha_block(h, tail);
    if (n == 128) sha_block(h, tail + 64);
    for (unsigned j = 0; j < 8; ++j) snprintf(out + 8 * j, 9, "%08x", h[j]);
}

/* ---- files ---- */
static uint8_t *read_file(const char *path, size_t *size)
{
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 0x800000) return NULL;
    FILE *in = fopen(path, "rb");
    if (!in) return NULL;
    uint8_t *b = malloc((size_t)st.st_size + 1);
    const size_t got = b ? fread(b, 1, (size_t)st.st_size, in) : 0;
    fclose(in);
    if (!b || got != (size_t)st.st_size) {
        free(b);
        return NULL;
    }
    b[got] = 0;
    *size = got;
    return b;
}

/* Every regular file below dir (relative names, at most MAX_FILES), names
 * beginning with '.' skipped as the card's own view skips them
 * (em_memcard's GetDir). -1 on a symlink, another file type or too many. */
static int list_files(const char *dir, const char *rel, char names[][128], int *n)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int rc = 0;
    while (rc == 0 && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[1024], name[128];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if ((size_t)snprintf(name, sizeof name, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name) >= sizeof name ||
            lstat(path, &st) != 0) {
            rc = -1;
        } else if (S_ISDIR(st.st_mode)) {
            rc = list_files(path, name, names, n);
        } else if (S_ISREG(st.st_mode) && *n < MAX_FILES) {
            snprintf(names[(*n)++], 128, "%s", name);
        } else {
            rc = -1;
        }
    }
    closedir(d);
    return rc;
}

/* ---- start.json (the builder's output; only the fields read here) ---- */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
    return p;
}

/* After `"key":` at the top level or inside the object starting at from. */
static const char *value_of(const char *from, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(from, pat);
    if (!p) return NULL;
    p = skip_ws(p + strlen(pat));
    return *p == ':' ? skip_ws(p + 1) : NULL;
}

static const char *string_at(const char *p, char *out, size_t size)
{
    if (!p || *p != '"') return NULL;
    size_t n = 0;
    for (++p; *p && *p != '"'; ++p) {
        if (*p == '\\' || n + 1 >= size) return NULL;
        out[n++] = *p;
    }
    if (*p != '"') return NULL;
    out[n] = 0;
    return p + 1;
}

static int floats_at(const char *p, double v[3])
{
    if (!p || *p != '[') return -1;
    ++p;
    for (int i = 0; i < 3; ++i) {
        char *end;
        v[i] = strtod(skip_ws(p), &end);
        if (end == p) return -1;
        p = skip_ws(end);
        if (*p != (i < 2 ? ',' : ']')) return -1;
        ++p;
    }
    return 0;
}

static float f32_at(const uint8_t *b, unsigned offset)
{
    float v;
    memcpy(&v, b + offset, 4);
    return v;
}

static int check_card(const char *card)
{
    char path[1024], text_name[64], folder[16];
    size_t size;
    snprintf(path, sizeof path, "%s/start.json", card);
    char *json = (char *)read_file(path, &size);
    if (!json) return refuse("the start-point card is not built (make start-saves)", path);
    int rc = -1;
    const char *files = value_of(json, "files");
    const char *pose = value_of(json, "load_pose");
    const char *cardv = value_of(json, "card");
    char head_hex[40];
    double position[3], rotation[3];
    if (!string_at(value_of(json, "name"), text_name, sizeof text_name) || strcmp(text_name, s.name) != 0) {
        refuse("start.json is not this start point's", path);
    } else if (!cardv || !string_at(value_of(cardv, "folder"), folder, sizeof folder) ||
               strcmp(folder, "slot1") != 0) {
        refuse("start.json's card is not slot1 (port 0)", path);
    } else if (!pose || floats_at(value_of(pose, "position"), position) ||
               floats_at(value_of(pose, "rotation"), rotation)) {
        refuse("the card is stale: start.json has no load pose (save2 round 4); rebuild with make start-saves",
               path);
    } else if (!string_at(value_of(json, "block_head"), head_hex, sizeof head_hex) || strlen(head_hex) != 32) {
        refuse("start.json has no block head", path);
    } else if (!files || *files != '{') {
        refuse("start.json lists no files", path);
    } else {
        rc = 0;
    }
    /* The files start.json lists, each with its SHA-256, and no others. */
    char listed[MAX_FILES][128], found[MAX_FILES][128];
    int nlisted = 0, nfound = 0;
    uint8_t *slot = NULL;
    if (rc == 0) {
        const char *p = skip_ws(files + 1);
        while (rc == 0 && *p != '}') {
            char name[128], hex[72];
            p = string_at(p, name, sizeof name);
            p = p ? skip_ws(p) : NULL;
            if (!p || *p != ':' || !(p = string_at(skip_ws(p + 1), hex, sizeof hex)) || strlen(hex) != 64 ||
                nlisted >= MAX_FILES) {
                rc = refuse("start.json's file list is not the builder's", path);
                break;
            }
            snprintf(listed[nlisted++], 128, "%s", name);
            char file[1024], got[65];
            snprintf(file, sizeof file, "%s/slot1/%s", card, name);
            uint8_t *bytes = read_file(file, &size);
            if (!bytes) {
                rc = refuse("a card file start.json lists is missing (or not a regular file)", file);
                break;
            }
            sha256_hex(bytes, size, got);
            if (strcmp(got, hex) != 0) {
                free(bytes);
                rc = refuse("the card is stale: a file's SHA-256 is not the one start.json records "
                            "(rebuild with make start-saves)", file);
                break;
            }
            if (strcmp(name, SAVE_DIR "/EX_DATA.00") == 0 && size == BLOCK) slot = bytes;
            else free(bytes);
            p = skip_ws(p);
            if (*p == ',') p = skip_ws(p + 1);
        }
    }
    if (rc == 0) {
        snprintf(path, sizeof path, "%s/slot1", card);
        if (list_files(path, "", found, &nfound) != 0 || nfound != nlisted) {
            rc = refuse("slot1 holds other files than start.json lists, or a symlink", path);
        } else {
            for (int i = 0; i < nfound && rc == 0; ++i) {
                int hit = 0;
                for (int j = 0; j < nlisted; ++j) hit |= strcmp(found[i], listed[j]) == 0;
                if (!hit) rc = refuse("slot1 holds a file start.json does not list", found[i]);
            }
        }
    }
    if (rc == 0) {
        struct stat st;
        snprintf(path, sizeof path, "%s/slot2", card);
        nfound = 0;
        if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode) || list_files(path, "", found, &nfound) != 0 ||
            nfound != 0)
            rc = refuse("slot2 is not an empty folder", path);
    }
    if (rc == 0 && !slot) rc = refuse("the slot " SAVE_DIR "/EX_DATA.00 is not one 0x640-byte block", card);
    if (rc == 0) {
        unsigned sum = 0, area = 0xFFu;
        for (unsigned i = 0; i < BLOCK; ++i) sum += slot[i];
        for (unsigned i = 0; i < sizeof k_points / sizeof k_points[0]; ++i)
            if (strcmp(k_points[i].name, s.name) == 0) area = k_points[i].area;
        int head_ok = 1;
        for (unsigned i = 0; i < 15; ++i) {
            unsigned v;
            head_ok &= sscanf(head_hex + 2 * i, "%2x", &v) == 1 && v == slot[i];
        }
        if ((sum & 0xFFu) != 0) rc = refuse("the slot's check byte +0x63F does not make the block sum to zero", card);
        else if (slot[0] != area) rc = refuse("the slot's area byte D_00810700 is not this start point's area", card);
        else if (!head_ok || slot[0xF] != 1)
            rc = refuse("the slot's head is not start.json's block head with +0xF = 1", card);
        else if (f32_at(slot, 0x10) != (float)position[0] || f32_at(slot, 0x14) != (float)position[1] ||
                 f32_at(slot, 0x18) != (float)position[2] || f32_at(slot, 0x20) != (float)rotation[0] ||
                 f32_at(slot, 0x24) != (float)rotation[1] || f32_at(slot, 0x28) != (float)rotation[2])
            rc = refuse("the card is stale: the slot's load pose D_00810710 / 720 is not start.json's "
                        "(rebuild with make start-saves)", card);
    }
    free(slot);
    free(json);
    return rc;
}

/* The fixtures that drive the pad or choose another route, input or card
 * (set at all, even empty: several read only whether they are set). Every
 * name ending in TEST but EM_START_AT_TEST and EM_INPUT_TEST (which only
 * prints the pad's changes): EM_STARTUP_TEST, the EM_*_TEST scripts of
 * em_game_selftest / em_opening_control_test, EM_NEW_GAME_STATE_TEST,
 * EM_AUDIO_TEST (main.c's audio modes); every EM_CAPTURE* (the capture
 * scripts, which inject input); and the names below (EM_RAND_REPLAY
 * re-seeds rand() at an area entry: the level smoke's comparison anchor). Every other EM_*
 * variable only observes (traces, dumps, counters, captures), changes the
 * host (EM_HEADLESS, EM_UNCAPPED, EM_MUTE, the disc-drive timing) or the
 * presentation (EM_HUD_*), or works only under a refused fixture
 * (EM_LEVEL_SMOKE_*_CAPTURE, EM_REPLAY_*, EM_AUDIO_FILE): docs/STARTUP.md
 * lists them, and tools/test_start_at.py checks every EM_* name the source
 * reads is refused or listed there. */
extern char **environ;
static int fixture_set(char *name, size_t size)
{
    static const char *const named[] = {
        "EM_SKIP_STARTUP", "EM_TEST_MEMCARD_ROOT", "EM_INPUT_RECORD", "EM_INPUT_PLAY", "EM_SCENE",
        "EM_STARTUP_MENU_WAIT", "EM_LEVEL_SMOKE_UNTIL", "EM_AIM_PAD_SCRIPT", "EM_A01_ARRIVAL_TICKS",
        "EM_AREA01_EXPLORE_SCRIPT", "EM_AREA01_EXPLORE_PHASE", "EM_CLIP_DEMO", "EM_CONTROL_LOW_GAIT",
        "EM_MOVE_LEGS", "EM_MOVE_EXPECT", "EM_RAND_REPLAY"};
    for (char **e = environ; e && *e; ++e) {
        const char *v = *e;
        const char *eq = strchr(v, '=');
        const size_t n = eq ? (size_t)(eq - v) : strlen(v);
        if (n < 4 || strncmp(v, "EM_", 3) != 0) continue;
        int hit = n >= 7 && strncmp(v + n - 4, "TEST", 4) == 0 &&
                  !(n == 16 && strncmp(v, "EM_START_AT_TEST", 16) == 0) &&
                  !(n == 13 && strncmp(v, "EM_INPUT_TEST", 13) == 0);
        if (n >= 10 && strncmp(v, "EM_CAPTURE", 10) == 0) hit = 1;
        for (unsigned i = 0; !hit && i < sizeof named / sizeof named[0]; ++i)
            hit = strlen(named[i]) == n && strncmp(v, named[i], n) == 0;
        if (hit) {
            snprintf(name, size, "%.*s", (int)(n < size ? n : size - 1), v);
            return 1;
        }
    }
    return 0;
}

int em_start_at_check(void)
{
    if (s.checked) return s.on ? 0 : (s.name ? -1 : 0);
    s.checked = 1;
    const char *v = getenv("EM_START_AT");
    if (!v || !v[0]) return 0;
    for (unsigned i = 0; i < sizeof k_points / sizeof k_points[0]; ++i)
        if (strcmp(k_points[i].name, v) == 0) s.name = k_points[i].name;
    if (!s.name) {
        s.name = "";
        return refuse("an unknown start point (area01, area00, area01-revisit, area02, a02_01 .. a02_05, "
                             "a01_03, a01_05, a01_s7, a01_07, a00_01, a00_04, a01r_01, area22, a22_01, "
                             "a22_02, a01u, a01u_01, a01u_02, a01u_s0, area06, a06_01 .. a06_05, a06b, "
                             "a06b_01, a01v, a01v_01, a01v_02, a22b, a22b_01)", NULL);
    }
    const char *ng = getenv("EM_NEW_GAME");
    if (ng && ng[0] && strcmp(ng, "0") != 0)
        return refuse("EM_NEW_GAME starts New Game, not a loaded game", NULL);
    char other[128];
    if (fixture_set(other, sizeof other))
        return refuse("a switch that drives the pad or chooses another route, input or card", other);
    const char *t = getenv("EM_START_AT_TEST");
    s.report = t && strcmp(t, "1") == 0;
    if (t && t[0] && !s.report) return refuse("EM_START_AT_TEST: expected 1", t);
    const char *dir = getenv("EM_START_AT_DIR");
    if (dir && dir[0] && !s.report) return refuse("EM_START_AT_DIR is test input (EM_START_AT_TEST=1 only)", dir);
    if ((size_t)snprintf(s.root, sizeof s.root, "%s/%s", dir && dir[0] ? dir : START_SAVES, s.name) >=
        sizeof s.root)
        return refuse("the card path is too long", NULL);
    if (check_card(s.root) != 0) return -1;
    const char *script = getenv("EM_START_AT_PAD");
    if (script && script[0]) {
        FILE *f = fopen(script, "r");
        unsigned version, gap;
        int last;
        if (!f) return refuse("EM_START_AT_PAD: no such file", script);
        int ok = fscanf(f, "EMA1 %u %d %u", &version, &last, &gap) == 3 && version == 1 && last >= 1 &&
                 last <= 1000000;
        int frame, fields = 0;
        unsigned buttons, lx, ly;
        while (ok && (fields = fscanf(f, "%d %x %u %u", &frame, &buttons, &lx, &ly)) == 4) {
            if (pad.count == PAD_MAX || frame < 0 || frame > last || buttons > 0xFFFF || lx > 255 || ly > 255 ||
                (pad.count && frame < pad.row[pad.count - 1].f)) ok = 0;
            else pad.row[pad.count++] = (PadRow){frame, (uint16_t)buttons, (uint8_t)lx, (uint8_t)ly};
        }
        ok = ok && fields == EOF && feof(f) && pad.count;
        fclose(f);
        if (!ok) return refuse("EM_START_AT_PAD: not an EMA1 pad script", script);
        pad.last = (unsigned)last;
        pad.loaded = 1;
        const char *marks = getenv("EM_START_AT_MARKS");
        for (const char *m = marks; m && *m;) {
            char *end;
            const long v = strtol(m, &end, 10);
            if (end == m || v < 0 || v > last || pad.mark_count == 64 ||
                (pad.mark_count && v <= pad.mark[pad.mark_count - 1]) || (*end && *end != ','))
                return refuse("EM_START_AT_MARKS: not increasing script frames", marks);
            pad.mark[pad.mark_count++] = (int)v;
            m = *end ? end + 1 : end;
        }
        const char *seed = getenv("EM_START_AT_RAND");
        if (seed && seed[0]) {
            char *end;
            const unsigned long v = strtoul(seed, &end, 16);
            if (*end || v > 0xFFFFFFFFul) return refuse("EM_START_AT_RAND: not a 32-bit hex word", seed);
            pad.rand_set = 1;
            pad.rand_state = (uint32_t)v;
        }
        const char *rands = getenv("EM_START_AT_RANDS");
        if (rands && rands[0]) {
            FILE *g = fopen(rands, "r");
            if (!g) return refuse("EM_START_AT_RANDS: no such file", rands);
            int fr;
            unsigned st;
            while (pad.rands < sizeof pad.rand_at / sizeof pad.rand_at[0] && fscanf(g, "%d %x", &fr, &st) == 2) {
                pad.rand_at[pad.rands].f = fr;
                pad.rand_at[pad.rands++].state = st;
            }
            fclose(g);
        }
        const char *sync = getenv("EM_START_AT_SYNC");
        if (sync && sync[0]) {
            if (sscanf(sync, "%x:%x:%d", &pad.sync_address, &pad.sync_value, &pad.sync_f) != 3)
                return refuse("EM_START_AT_SYNC: not <address>:<value>:<frame>", sync);
        } else {
            pad.sync_done = 1;
        }
        const char *restore = getenv("EM_START_AT_RESTORE");
        if (restore && restore[0]) {
            const char *colon = strchr(restore, ':');
            if (!colon || sscanf(colon + 1, "%d:%x", &pad.restore_f, &pad.restore_clock) != 2)
                return refuse("EM_START_AT_RESTORE: not <image>:<frame>:<0x70003B68 hex>", restore);
            pad.restore = restore;
        }
        const char *path = getenv("EM_START_AT_PATH");
        if (path && path[0]) {
            FILE *g = fopen(path, "r");
            if (!g) return refuse("EM_START_AT_PATH: no such file", path);
            float x, z;
            int fr;
            char line[96];
            while (pad.path_count < PATH_MAX_ROWS && fgets(line, sizeof line, g)) {
                float yaw = 0.0f;
                const int n = sscanf(line, "%d %f %f %f", &fr, &x, &z, &yaw);
                if (n < 3 || fr != (int)pad.path_count) break;
                pad.path[pad.path_count][0] = x;
                pad.path[pad.path_count][1] = z;
                pad.path_yaw[pad.path_count] = yaw;
                pad.path_has_yaw |= n == 4;
                pad.path_count++;
            }
            const int good = feof(g) && pad.path_count;
            fclose(g);
            if (!good) return refuse("EM_START_AT_PATH: not a frame-ordered x z path", path);
        }
    }
    {
        const char *seed = getenv("EM_START_AT_RAND");
        if (seed && seed[0] && !pad.loaded) return refuse("EM_START_AT_RAND works with EM_START_AT_PAD only", seed);
        const char *level = getenv("EM_START_AT_FADE");
        if (level && level[0]) {
            char *end = NULL;
            const unsigned long v = strtoul(level, &end, 0);
            if (*end || v > 255ul) return refuse("EM_START_AT_FADE: not a level 0..255", level);
            pad.fade_level = (unsigned)v;
            pad.fade_given = 1;
        }
        const char *marks = getenv("EM_START_AT_MARKS");
        if (marks && marks[0] && !pad.loaded)
            return refuse("EM_START_AT_MARKS works with EM_START_AT_PAD only", marks);
    }
    s.on = 1;
    printf("startup: EM_START_AT=%s: the boot title's LOAD GAME of slot 1 on the start-point card %s "
           "(read-only)\n", s.name, s.root);
    return 0;
}

const char *em_start_at_name(void) { return s.on ? s.name : NULL; }
const char *em_start_at_card_root(void) { return s.on ? s.root : NULL; }
int em_start_at_report(void) { return s.on && s.report; }

/* The pad script's filter: the whole pad is the script's command, with
 * the test buttons ORed in as em_input_pad does before the filter (the
 * cutscene skip fixture's START, em_opening_control_test.c). */
static void pad_filter(EmPadState *out, void *user)
{
    (void)user;
    *out = pad.now;
    out->buttons |= em_input_test_buttons();
}

void em_start_at_pad_control(unsigned counter)
{
    if (!s.on || !pad.loaded || pad.active || pad.done) return;
    pad.active = 1;
    pad.start = counter;
    pad.t = 0;
    pad.hold = 0;
    pad.now = (EmPadState){0};
    em_input_set_filter(pad_filter, NULL);
    printf("startup: EM_START_AT_PAD: %u rows over %u frames from control at engine frame %u\n", pad.count,
           pad.last, counter);
    /* Without EM_START_AT_FADE the script starts at control, as a level
     * smoke or a short test script expects. */
    pad.fade_wait = pad.fade_given;
    if (!pad.fade_wait && pad.rand_set) {
        em_random_seed(pad.rand_state);
        printf("startup: EM_START_AT_RAND: rand() state %08x at control (comparison only)\n", pad.rand_state);
    }
}

int em_start_at_pad_frame(unsigned counter, const float pos[3], const float fwd[3], const float *heading)
{
    if (!pad.active) return 0;
    /* The level smoke's rule: a recorded command is submitted two frames
     * after its row (the native pad reaches the next task tick). With a
     * path the script's clock waits (up to 300 frames at a time) while the
     * player is more than 2.5 units behind the recorded position, so the
     * presses stay where the recording made them. */
    if (pad.fade_wait) {
        const EmTransitionFade *fade = em_frame_transition();
        if (em_frame_fade_active() && fade && (fade->substate != 1 || (unsigned)fade->level > pad.fade_level))
            return 0;
        pad.fade_wait = 0;
        printf("startup: EM_START_AT_PAD: the script starts at engine frame %u (level %u)\n", counter,
               fade ? (unsigned)fade->level : 0u);
        if (pad.rand_set) {
            em_random_seed(pad.rand_state);
            printf("startup: EM_START_AT_RAND: rand() state %08x (comparison only)\n", pad.rand_state);
        }
    }
    /* With EM_START_AT_SYNC the script's clock is the recording's: the two
     * or three frames the frame machine's state changes step no hook (a
     * status page's open and close) pass on it too. */
    if (pad.sync_address && pad.sync_done && pad.sync_counter && counter - pad.sync_counter - 1u <= 2u)
        pad.t += (int)(counter - pad.sync_counter - 1u);
    pad.sync_counter = counter;
    if (!pad.sync_done) {
        const uint8_t *b = em_scene_progress_at(em_scene_state(), pad.sync_address, 1);
        if (b && *b == pad.sync_value) {
            printf("startup: EM_START_AT_SYNC: script frame %d is the recording's %d (comparison only)\n", pad.t,
                   pad.sync_f);
            pad.t = pad.sync_f;
            pad.sync_done = 1;
        }
    }
    const int t = pad.t;
    if (pad.rand_next < pad.rands && t >= pad.rand_at[pad.rand_next].f) {
        em_random_seed(pad.rand_at[pad.rand_next].state);
        printf("startup: EM_START_AT_RANDS: rand() state %08x at script frame %d (comparison only)\n",
               pad.rand_at[pad.rand_next].state, t);
        pad.rand_next++;
    }
    if (pad.restore && t >= pad.restore_f) {
        char path[512];
        snprintf(path, sizeof path, "%.*s", (int)(strchr(pad.restore, ':') - pad.restore), pad.restore);
        static uint8_t ram[0x2000000];
        FILE *g = fopen(path, "rb");
        const size_t got = g ? fread(ram, 1, sizeof ram, g) : 0;
        if (g) fclose(g);
        const int n = got == sizeof ram ? em_scene_restore_bugs(ram) : -1;
        em_scene_state()->spad3B68 = (int32_t)pad.restore_clock;
        if (got == sizeof ram) {
            /* The vitals the hall's bugs left: their bites before the anchor
             * are not the recording's (EM_START_AT_RESTORE's stretch). */
            memcpy(&em_scene_state()->d810858, ram + 0x810858, 4);
            memcpy(&em_scene_state()->d81085C, ram + 0x81085C, 4);
        }
        printf("startup: EM_START_AT_RESTORE: %d bugs at script frame %d (comparison only)\n", n, t);
        const char *next = strchr(pad.restore, ',');
        pad.restore = next ? next + 1 : NULL;
        if (next && (!(next = strchr(pad.restore, ':')) ||
                     sscanf(next + 1, "%d:%x", &pad.restore_f, &pad.restore_clock) != 2))
            pad.restore = NULL;
    }
    if (pad.path_count && pos && t >= 0 && (unsigned)t < pad.path_count && pad.hold < 300u) {
        /* A recorded position more than 20 units away is another room's
         * (a load the host drive ends earlier than the recording's: the
         * recorded rows still hold the old area's position): no wait. */
        /* A player that stands still with a neutral recorded stick is not
         * lagging (it is ahead: a recorded walk-in the native already
         * ended): no wait either. */
        const float hx = pad.path[t][0] - pos[0], hz = pad.path[t][1] - pos[2];
        const float d2 = hx * hx + hz * hz;
        const float mx = pos[0] - pad.last_pos[0], mz = pos[2] - pad.last_pos[1];
        const int moving = mx * mx + mz * mz > 1e-4f;
        pad.last_pos[0] = pos[0];
        pad.last_pos[1] = pos[2];
        const int stick = pad.now.lx * pad.now.lx + pad.now.ly * pad.now.ly > 0.01f;
        /* A player ahead of the recorded position along the recorded path
         * is not lagging either: only a player behind it is waited for (or
         * short of a recorded stop). */
        const unsigned ahead_at = (unsigned)t + 4u < pad.path_count ? (unsigned)t + 4u : pad.path_count - 1u;
        const float tx = pad.path[ahead_at][0] - pad.path[t][0], tz = pad.path[ahead_at][1] - pad.path[t][1];
        const int behind = tx * tx + tz * tz < 1e-4f || hx * tx + hz * tz > 0.0f;
        /* The rows' fourth column, the recorded heading: a press the next
         * step gives waits (up to 90 frames at a press) while the player
         * stands more than 0.6 from the recorded place (a walk there) or
         * faces more than 0.08 from the recorded heading (a light stick
         * toward it, a turn in place): a Use's cone, a strike's reach. */
        int press = 0;
        for (unsigned i = 0; i < pad.count; ++i)
            if (pad.row[i].f == t - 1 && pad.row[i].buttons && (!i || !pad.row[i - 1].buttons)) press = 1;
        if (!press) pad.turn = 0;
        /* (A press made standing: a running jump keeps its run.) */
        const unsigned before = t >= 4 ? (unsigned)t - 4u : 0u;
        const float bx = pad.path[t][0] - pad.path[before][0], bz = pad.path[t][1] - pad.path[before][1];
        if (press && bx * bx + bz * bz < 0.04f && pad.path_has_yaw && heading && fwd && d2 < 20.0f * 20.0f &&
            pad.turn < 90u) {
            float off = pad.path_yaw[t] - *heading;
            while (off > 3.14159265f) off -= 6.2831853f;
            while (off < -3.14159265f) off += 6.2831853f;
            const float n = sqrtf(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
            if (d2 > 0.6f * 0.6f && n > 0.0f) {
                const float fx = fwd[0] / n, fz = fwd[2] / n, d = sqrtf(d2);
                pad.turn++;
                pad.now = (EmPadState){0, 0.5f * (-hx * fz + hz * fx) / d, -0.5f * (hx * fx + hz * fz) / d, 0, 0};
                return 0;
            }
            if (fabsf(off) > 0.08f && n > 0.0f) {
                const float fx = fwd[0] / n, fz = fwd[2] / n;
                const float wx = sinf(pad.path_yaw[t]), wz = cosf(pad.path_yaw[t]);
                pad.turn++;
                pad.now = (EmPadState){0, 0.5f * (-wx * fz + wz * fx), -0.5f * (wx * fx + wz * fz), 0, 0};
                return 0;
            }
        }
        if (d2 > 2.5f * 2.5f && d2 < 20.0f * 20.0f && (moving || stick) && behind) pad.hold++;
        else {
            /* A wait of 10 frames or more is reported (where the native
             * player fell behind the recording). */
            if (pad.hold >= 10u)
                printf("startup: EM_START_AT_PATH: waited %u frames at script frame %d (engine frame %u)\n",
                       pad.hold, t, counter);
            pad.hold = 0;
            pad.t++;
        }
    } else {
        pad.hold = 0;
        pad.t++;
    }
    if (t > (int)pad.last + 2) {
        pad.active = 0;
        pad.done = 1;
        pad.now = (EmPadState){0};
        printf("startup: EM_START_AT_PAD: the script ended at engine frame %u\n", counter);
        return 1;
    }
    for (unsigned i = 0; i < pad.count && pad.row[i].f <= t - 2; ++i)
        pad.now = (EmPadState){pad.row[i].buttons, (pad.row[i].lx - 128) / 128.0f,
                               (pad.row[i].ly - 128) / 128.0f, 0, 0};
    const float m = sqrtf(pad.now.lx * pad.now.lx + pad.now.ly * pad.now.ly);
    if (pad.path_count && pos && fwd && m > 0.1f && t >= 0 && (unsigned)t < pad.path_count) {
        unsigned k = (unsigned)t + 8u;
        if (k >= pad.path_count) k = pad.path_count - 1u;
        /* A player ahead of that point with a recorded stop in front (up to
         * 48 rows on) takes the stop: a target behind a running player is a
         * reversal, a wide turn (door [6]: 6 units along the wall, a Use
         * short of its place). A path that turns back (a run-up) keeps
         * the near target. */
        for (unsigned step = 0, q = k; step < 48u && q + 1u < pad.path_count; ++step, ++q) {
            const float ax = pad.path[q + 1u][0] - pad.path[q][0], az = pad.path[q + 1u][1] - pad.path[q][1];
            if (ax * ax + az * az < 1e-6f) { k = q; break; }
            if ((pad.path[q][0] - pos[0]) * ax + (pad.path[q][1] - pos[2]) * az > 0.0f) break;
        }
        const float dx = pad.path[k][0] - pos[0], dz = pad.path[k][1] - pos[2];
        const float d = sqrtf(dx * dx + dz * dz);
        float fx = fwd[0], fz = fwd[2];
        const float n = sqrtf(fx * fx + fz * fz);
        if (d > 0.25f && d < 20.0f && n > 0.0f) {
            fx /= n;
            fz /= n;
            const float mag = m > 1.0f ? 1.0f : m;
            pad.now.lx = mag * (-dx * fz + dz * fx) / d;
            pad.now.ly = -mag * (dx * fx + dz * fz) / d;
        }
    }
    return 0;
}

int em_start_at_pad_progress(unsigned counter)
{
    (void)counter;
    return pad.active && pad.t > 0 && pad.t % 100 == 0 && !pad.hold;
}

int em_start_at_pad_clock(void) { return pad.t; }

int em_start_at_pad_mark(void)
{
    if (!pad.active || pad.mark_next >= pad.mark_count || pad.t < pad.mark[pad.mark_next]) return 0;
    ++pad.mark_next;
    return 1;
}
