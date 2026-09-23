#include "game/em_status_hub_ui.h"
#include "game/em_hud.h"
#include "game/em_item_geometry.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    RECORD_SPRITE = 1,
    RECORD_RECTANGLE,
    RECORD_ARC,
    RECORD_MARKER,
    RECORD_TRAIL,
    RECORD_HEALTH,
    RECORD_BATTERY,
    RECORD_AMMO,
    RECORD_TEXT,
    RECORD_BLEND
};
enum { LAYOUTS = 10, HELP_LINES = 10, LAYOUT_MAX = 128, HELP_MAX = 16 };
enum { COMMAND_MAX = 1024, ARENA_MAX = 16384, TRAIL_TRIANGLES = 512, SPRITE_MAX = 64 };
enum { MARKER_MAX = 64 };
enum { TEXT_INFECTION = 1, TEXT_NULL_STYLE = 2 };
#define MARKER_WORDS (9 * 16 * 6)

typedef struct {
    uint32_t kind, mode, size;
    const uint8_t *body;
    const uint32_t *marker; /* aligned copy of a packed marker body */
} Record;

typedef struct {
    uint64_t tex0;
    uint32_t u, v, w, h;
} Sprite;

typedef struct {
    uint8_t kind, mode, proportional, null_style;
    int32_t xy[4];
    uint32_t rgba;
    uint64_t tex0;
    float arc[24];
    const uint32_t *marker;
    uint32_t text;
} Command;

struct EmStatusHubUI {
    uint8_t *records, *atlas;
    const uint8_t *pixels;
    uint32_t width, height, sprite_count, white;
    Sprite sprites[SPRITE_MAX];
    EmItemMath math;
    EmStatusHealthData health;
    EmStatusBatteryData battery;
    EmStatusAmmoData ammo;
    const char *percent;
    Record layouts[LAYOUTS][LAYOUT_MAX], help[HELP_LINES][HELP_MAX];
    uint32_t markers[MARKER_MAX][MARKER_WORDS];
    unsigned marker_count;
    uint32_t layout_count[LAYOUTS], help_count[HELP_LINES];
    Command commands[COMMAND_MAX];
    unsigned count, mode, arena_used, trail_count;
    char arena[ARENA_MAX];
    int32_t trail[TRAIL_TRIANGLES][7];
    int prepared, failed, uploaded;
};

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t integer(const uint8_t *p)
{
    return (int32_t)word(p);
}

static uint64_t wide(const uint8_t *p)
{
    return word(p) | (uint64_t)word(p + 4) << 32;
}

static float real(const uint8_t *p)
{
    uint32_t bits = word(p);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static uint8_t *read_file(const char *path, size_t limit, size_t *size)
{
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file)
        return NULL;
    uint8_t *data = NULL;
    long length = -1;
    if (fseek(file, 0, SEEK_END) == 0)
        length = ftell(file);
    if (length > 0 && (size_t)length <= limit && fseek(file, 0, SEEK_SET) == 0) {
        data = malloc((size_t)length);
        if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    *size = data ? (size_t)length : 0;
    return data;
}

typedef struct {
    const uint8_t *p, *end;
} Reader;

static int take(Reader *reader, size_t size, const uint8_t **out)
{
    if ((size_t)(reader->end - reader->p) < size)
        return 0;
    *out = reader->p;
    reader->p += size;
    return 1;
}

static int take_word(Reader *reader, uint32_t *value)
{
    const uint8_t *p;
    if (!take(reader, 4, &p))
        return 0;
    *value = word(p);
    return 1;
}

/* <u32 size with terminator><bytes><0>, as written by runtime_bundle. */
static const char *take_string(Reader *reader)
{
    uint32_t size;
    const uint8_t *p;
    if (!take_word(reader, &size) || !size || size > 1024 || !take(reader, size, &p) ||
        p[size - 1] || memchr(p, 0, size - 1))
        return NULL;
    return (const char *)p;
}

static const Sprite *find_sprite(const EmStatusHubUI *ui, uint64_t tex0)
{
    for (uint32_t i = 0; i < ui->sprite_count; ++i)
        if (i != ui->white && ui->sprites[i].tex0 == tex0)
            return &ui->sprites[i];
    return NULL;
}

/* The glyph cells passed by the original 001CBA50/001CC1E0 calls. */
static int text_style(int proportional, int32_t w, int32_t h, EmHudTextStyle *style)
{
    if (proportional) {
        *style = EM_HUD_TEXT_TALL;
        return w == 10 && h == 20;
    }
    if (w == 12 && h == 12)
        *style = EM_HUD_TEXT_NUM12;
    else if (w == 16 && h == 16)
        *style = EM_HUD_TEXT_NUM16;
    else if (w == 12 && h == 16)
        *style = EM_HUD_TEXT_NAME12_BLUE;
    else if (w == 10 && h == 10)
        *style = EM_HUD_TEXT_PROFILE10;
    else
        return 0;
    return 1;
}

static int take_record(Reader *reader, EmStatusHubUI *ui, Record *record)
{
    const uint8_t *header;
    if (!take(reader, 16, &header) || word(header + 12) || !take(reader, word(header + 8),
                                                                 &record->body))
        return 0;
    record->kind = word(header);
    record->mode = word(header + 4);
    record->size = word(header + 8);
    const uint8_t *p = record->body;
    if (record->mode > 3)
        return 0;
    switch (record->kind) {
    case RECORD_SPRITE:
        return record->size == 28 && integer(p + 8) > 0 && integer(p + 8) <= 1024 &&
               integer(p + 12) > 0 && integer(p + 12) <= 1024 && find_sprite(ui, wide(p + 20));
    case RECORD_RECTANGLE:
        return record->size == 20 && integer(p) <= integer(p + 8) && integer(p + 4) <= integer(p + 12);
    case RECORD_ARC:
        if (record->size != 96)
            return 0;
        for (unsigned i = 0; i < 24; ++i)
            if (!isfinite(real(p + i * 4)))
                return 0;
        return 1;
    case RECORD_MARKER: {
        if (record->size != MARKER_WORDS * 4 || ui->marker_count >= MARKER_MAX)
            return 0;
        uint32_t *words = ui->markers[ui->marker_count++];
        for (unsigned i = 0; i < MARKER_WORDS; ++i) {
            words[i] = word(p + i * 4);
            if (i % 6 < 4 && words[i] > 255)
                return 0;
        }
        record->marker = words;
        return 1;
    }
    case RECORD_TRAIL:
        return record->size == 8 && isfinite(real(p)) && isfinite(real(p + 4));
    case RECORD_HEALTH:
    case RECORD_AMMO:
        return record->size == 8;
    case RECORD_BATTERY:
        return record->size == 20 && find_sprite(ui, wide(p + 8));
    case RECORD_TEXT: {
        if (record->size < 37)
            return 0;
        int32_t proportional = integer(p), flags = integer(p + 20);
        EmHudTextStyle style;
        Reader text = {p + 32, p + record->size};
        const char *value = take_string(&text);
        return value && text.p == text.end && (proportional == 0 || proportional == 1) &&
               flags >= 0 && flags <= 3 && text_style(proportional, integer(p + 12), integer(p + 16),
                                                      &style) &&
               (!(flags & TEXT_NULL_STYLE) || proportional) &&
               (!(flags & TEXT_INFECTION) || (!proportional && !*value));
    }
    case RECORD_BLEND:
        return record->size == 0;
    default:
        return 0;
    }
}

static int load_atlas(EmStatusHubUI *ui, const char *path)
{
    size_t size;
    ui->atlas = read_file(path, 64u << 20, &size);
    if (!ui->atlas || size < 28 || memcmp(ui->atlas, "EMHA", 4) || word(ui->atlas + 4) != 1)
        return 0;
    ui->width = word(ui->atlas + 8);
    ui->height = word(ui->atlas + 12);
    ui->sprite_count = word(ui->atlas + 16);
    ui->white = word(ui->atlas + 20);
    uint32_t pixels = word(ui->atlas + 24);
    if (!ui->width || !ui->height || ui->width > 4096 || ui->height > 4096 ||
        !ui->sprite_count || ui->sprite_count > SPRITE_MAX || ui->white >= ui->sprite_count ||
        (uint64_t)ui->width * ui->height * 4 != pixels ||
        size != 28 + (uint64_t)ui->sprite_count * 24 + pixels)
        return 0;
    for (uint32_t i = 0; i < ui->sprite_count; ++i) {
        const uint8_t *p = ui->atlas + 28 + i * 24;
        Sprite *sprite = &ui->sprites[i];
        *sprite = (Sprite){wide(p), word(p + 8), word(p + 12), word(p + 16), word(p + 20)};
        if (!sprite->w || !sprite->h || sprite->w > ui->width || sprite->h > ui->height ||
            sprite->u > ui->width - sprite->w || sprite->v > ui->height - sprite->h)
            return 0;
        for (uint32_t other = 0; other < i; ++other)
            if (ui->sprites[other].tex0 == sprite->tex0)
                return 0;
    }
    const Sprite *white = &ui->sprites[ui->white];
    ui->pixels = ui->atlas + 28 + ui->sprite_count * 24;
    const uint8_t *texel = ui->pixels + ((size_t)white->v * ui->width + white->u) * 4;
    if (white->tex0 || white->w != 1 || white->h != 1 || word(texel) != 0xFFFFFFFFu)
        return 0;
    /* Original 00209860 reserve and secondary icons; the live inventory can
     * select each, so none may be discovered missing mid-frame. */
    static const uint64_t ammo_icons[] = {
        UINT64_C(0x2004518555422196), UINT64_C(0x20045385554221C2), UINT64_C(0x200451A5554221A2),
        UINT64_C(0x20045305554221A6), UINT64_C(0x20045325554221B2)};
    for (unsigned i = 0; i < sizeof ammo_icons / sizeof *ammo_icons; ++i)
        if (!find_sprite(ui, ammo_icons[i]))
            return 0;
    return 1;
}

static int load_records(EmStatusHubUI *ui, const char *path)
{
    size_t size;
    ui->records = read_file(path, 4u << 20, &size);
    if (!ui->records || size < 24 || memcmp(ui->records, "EMHS", 4) ||
        word(ui->records + 4) != 2 || word(ui->records + 8) != size - 24 ||
        word(ui->records + 12) != LAYOUTS || word(ui->records + 16) != HELP_LINES ||
        word(ui->records + 20))
        return 0;
    Reader reader = {ui->records + 24, ui->records + size};
    const uint8_t *p;
    if (!take(&reader, 4 * 96 + 16, &p))
        return 0;
    for (unsigned arc = 0; arc < 4; ++arc)
        for (unsigned i = 0; i < 24; ++i) {
            ui->health.arcs[arc][i] = real(p + arc * 96 + i * 4);
            if (!isfinite(ui->health.arcs[arc][i]))
                return 0;
        }
    /* Original 00265510 white and 00265528 red style records. */
    ui->health.white = ui->battery.white = ui->ammo.white = wide(p + 384);
    ui->health.red = wide(p + 392);
    uint32_t width;
    if (!take_word(&reader, &width) || width > 1024)
        return 0;
    ui->health.label_width = (int)width;
    const char *strings[7];
    for (unsigned i = 0; i < 7; ++i)
        if (!(strings[i] = take_string(&reader)))
            return 0;
    /* Resident 267298 label, 273558/560/568, 26729C, 2672A0 and 273570. */
    ui->health.label = strings[0];
    ui->health.warning_max = strings[1];
    ui->health.normal_max = strings[2];
    ui->health.separator = ui->battery.separator = strings[3];
    ui->battery.label = strings[4];
    ui->ammo.label = strings[5];
    ui->ammo.percent = ui->percent = strings[6];
    if (strlen(ui->percent) > 60)
        return 0;
    for (unsigned line = 0; line < HELP_LINES; ++line) {
        if (!take_word(&reader, &ui->help_count[line]) || !ui->help_count[line] ||
            ui->help_count[line] > HELP_MAX)
            return 0;
        for (unsigned i = 0; i < ui->help_count[line]; ++i) {
            Record *record = &ui->help[line][i];
            if (!take_record(&reader, ui, record) || record->kind != RECORD_TEXT ||
                integer(record->body + 20))
                return 0;
        }
    }
    unsigned seen = 0;
    for (unsigned layout = 0; layout < LAYOUTS; ++layout) {
        uint32_t index, count;
        if (!take_word(&reader, &index) || !take_word(&reader, &count) || index >= LAYOUTS ||
            seen & 1u << index || !count || count > LAYOUT_MAX)
            return 0;
        seen |= 1u << index;
        ui->layout_count[index] = count;
        unsigned kinds[RECORD_BLEND + 1] = {0}, dynamic_text = 0;
        for (unsigned i = 0; i < count; ++i) {
            Record *record = &ui->layouts[index][i];
            if (!take_record(&reader, ui, record) || (!i && record->kind != RECORD_BLEND))
                return 0;
            ++kinds[record->kind];
            dynamic_text += record->kind == RECORD_TEXT && integer(record->body + 20) & TEXT_INFECTION;
        }
        /* Each 209DF0 layout makes exactly one trail/health/battery/ammo call;
         * only the non-terminal infection branch formats D_002862C0. */
        if (kinds[RECORD_TRAIL] != 1 || kinds[RECORD_HEALTH] != 1 || kinds[RECORD_BATTERY] != 1 ||
            kinds[RECORD_AMMO] != 1 || dynamic_text != (index & 1 ? 0u : 1u))
            return 0;
    }
    return reader.p == reader.end;
}

EmStatusHubUI *em_status_hub_ui_load(const char *records_path, const char *atlas_path,
                                     const EmItemMath *math)
{
    if (!math || !math->sine || !math->cosine || !math->atan2 || !math->sqrt)
        return NULL;
    EmStatusHubUI *ui = calloc(1, sizeof *ui);
    if (!ui)
        return NULL;
    ui->math = *math;
    if (!load_atlas(ui, atlas_path) || !load_records(ui, records_path)) {
        em_status_hub_ui_free(ui);
        return NULL;
    }
    return ui;
}

void em_status_hub_ui_deactivate(EmStatusHubUI *ui)
{
    if (ui && ui->uploaded) {
        em_hud_decor_invalidate();
        ui->uploaded = 0;
    }
}

void em_status_hub_ui_free(EmStatusHubUI *ui)
{
    if (!ui)
        return;
    em_status_hub_ui_deactivate(ui);
    free(ui->records);
    free(ui->atlas);
    free(ui);
}

static Command *append(EmStatusHubUI *ui, unsigned kind)
{
    if (ui->count >= COMMAND_MAX)
        return NULL;
    Command *command = &ui->commands[ui->count++];
    memset(command, 0, sizeof *command);
    command->kind = (uint8_t)kind;
    command->mode = (uint8_t)ui->mode;
    return command;
}

static int intern(EmStatusHubUI *ui, const char *value, uint32_t *offset)
{
    size_t length = value ? strlen(value) + 1 : 0;
    if (!length || length > ARENA_MAX - ui->arena_used)
        return 0;
    memcpy(ui->arena + ui->arena_used, value, length);
    *offset = ui->arena_used;
    ui->arena_used += (unsigned)length;
    return 1;
}

static int worker_blend(void *context, unsigned mode)
{
    EmStatusHubUI *ui = context;
    if (mode > 3)
        return 0;
    ui->mode = mode;
    return append(ui, EM_STATUS_HUB_UI_BLEND) != NULL;
}

static int worker_rectangle(void *context, int x0, int y0, int x1, int y1, uint32_t rgba)
{
    Command *command = append(context, EM_STATUS_HUB_UI_RECTANGLE);
    if (!command)
        return 0;
    command->xy[0] = x0;
    command->xy[1] = y0;
    command->xy[2] = x1;
    command->xy[3] = y1;
    command->rgba = rgba;
    return 1;
}

static int text_command(EmStatusHubUI *ui, int proportional, int x, int y, int w, int h,
                        const char *value, uint64_t style, int null_style)
{
    EmHudTextStyle unused;
    if (!text_style(proportional, w, h, &unused))
        return 0;
    Command *command = append(ui, EM_STATUS_HUB_UI_TEXT);
    if (!command || !intern(ui, value, &command->text))
        return 0;
    command->proportional = (uint8_t)proportional;
    command->null_style = (uint8_t)null_style;
    command->xy[0] = x;
    command->xy[1] = y;
    command->xy[2] = w;
    command->xy[3] = h;
    command->tex0 = style;
    return 1;
}

static int worker_text(void *context, int proportional, int x, int y, int w, int h,
                       const char *value, uint64_t style)
{
    return text_command(context, proportional, x, y, w, h, value, style, 0);
}

static int worker_arc(void *context, const float descriptor[24])
{
    Command *command = append(context, EM_STATUS_HUB_UI_ARC);
    if (!command)
        return 0;
    memcpy(command->arc, descriptor, sizeof command->arc);
    return 1;
}

static int worker_sprite(void *context, int x, int y, int w, int h, uint32_t rgba, uint64_t tex0)
{
    EmStatusHubUI *ui = context;
    if (!find_sprite(ui, tex0))
        return 0;
    Command *command = append(ui, EM_STATUS_HUB_UI_SPRITE);
    if (!command)
        return 0;
    command->xy[0] = x;
    command->xy[1] = y;
    command->xy[2] = w;
    command->xy[3] = h;
    command->rgba = rgba;
    command->tex0 = tex0;
    return 1;
}

static int collect_triangle(void *context, const int32_t xy[3][2], unsigned intensity)
{
    EmStatusHubUI *ui = context;
    if (ui->trail_count >= TRAIL_TRIANGLES)
        return 0;
    int32_t *triangle = ui->trail[ui->trail_count++];
    for (unsigned vertex = 0; vertex < 3; ++vertex) {
        triangle[vertex * 2] = xy[vertex][0];
        triangle[vertex * 2 + 1] = xy[vertex][1];
    }
    triangle[6] = (int32_t)intensity;
    return 1;
}

static int fail(EmStatusHubUI *ui)
{
    ui->failed = 1;
    ui->prepared = 0;
    return 0;
}

static int static_record(EmStatusHubUI *ui, const Record *record)
{
    const uint8_t *p = record->body;
    Command *command;
    switch (record->kind) {
    case RECORD_BLEND:
        return worker_blend(ui, record->mode);
    case RECORD_SPRITE:
        return worker_sprite(ui, integer(p), integer(p + 4), integer(p + 8), integer(p + 12),
                             word(p + 16), wide(p + 20));
    case RECORD_RECTANGLE:
        return worker_rectangle(ui, integer(p), integer(p + 4), integer(p + 8), integer(p + 12),
                                word(p + 16));
    case RECORD_ARC: {
        float descriptor[24];
        for (unsigned i = 0; i < 24; ++i)
            descriptor[i] = real(p + i * 4);
        return worker_arc(ui, descriptor);
    }
    case RECORD_MARKER:
        if (!(command = append(ui, EM_STATUS_HUB_UI_MARKER)))
            return 0;
        command->marker = record->marker;
        return 1;
    case RECORD_TEXT: {
        Reader text = {p + 32, p + record->size};
        return text_command(ui, integer(p), integer(p + 4), integer(p + 8), integer(p + 12),
                            integer(p + 16), take_string(&text), wide(p + 24),
                            (integer(p + 20) & TEXT_NULL_STYLE) != 0);
    }
    default:
        return 0;
    }
}

int em_status_hub_ui_prepare(EmStatusHubUI *ui, const EmStatusHubDisplay *display,
                             const EmItemStick *stick, uint32_t *ui_clock, EmItemTrail *trail)
{
    if (!ui || ui->failed)
        return 0;
    ui->prepared = 0;
    ui->count = ui->arena_used = ui->trail_count = 0;
    ui->mode = 0;
    if (!display || !stick || !ui_clock || !trail || display->hover > 4 ||
        !isfinite(display->infection) || display->infection < 0 || display->infection > 100 ||
        !isfinite(stick->x) || !isfinite(stick->y) || !isfinite(stick->magnitude) ||
        !isfinite(stick->angle))
        return fail(ui);
    /* 209DF0: float_to_int(81085C) == 100 selects the terminal label branch. */
    int infection = (int)display->infection;
    unsigned layout = display->hover * 2u + (infection == 100);
    const EmStatusDrawWorkers workers = {ui,         worker_blend, worker_rectangle,
                                         worker_text, worker_arc,  worker_sprite};
    for (unsigned i = 0; i < ui->layout_count[layout]; ++i) {
        const Record *record = &ui->layouts[layout][i];
        const uint8_t *p = record->body;
        int ok;
        switch (record->kind) {
        case RECORD_TRAIL: {
            /* 20AC70 selects mode1 itself before its sixteen fans. */
            ui->mode = 1;
            Command *command = append(ui, EM_STATUS_HUB_UI_TRAIL);
            ok = command &&
                 em_item_trail_step(trail, stick, real(p), real(p + 4), &ui->math,
                                    collect_triangle, ui) &&
                 ui->trail_count == TRAIL_TRIANGLES;
            if (ok) {
                command->arc[0] = real(p);
                command->arc[1] = real(p + 4);
            }
            break;
        }
        case RECORD_HEALTH:
            ok = em_status_health_draw(ui_clock, display->health, display->warning, integer(p),
                                       integer(p + 4), &ui->health, &workers);
            break;
        case RECORD_BATTERY:
            ok = em_status_battery_draw(display->charge, display->capacity,
                                        display->battery_equipped, integer(p), integer(p + 4),
                                        wide(p + 8), integer(p + 16), &ui->battery, &workers);
            break;
        case RECORD_AMMO:
            ok = em_status_ammo_draw(&display->ammo, integer(p), integer(p + 4), &ui->ammo,
                                     &workers);
            break;
        case RECORD_TEXT:
            if (integer(p + 20) & TEXT_INFECTION) {
                /* 209DF0: 00123168(D_002862C0, 001C5FB0(n,3,1)), then
                 * 00122EF0 appends D_00273570. Here 0 <= n <= 99, so the
                 * formatter blanks leading zeros of a three-place field. */
                char value[72] = {' ', infection >= 10 ? (char)('0' + infection / 10) : ' ',
                                  (char)('0' + infection % 10), 0};
                strcat(value, ui->percent);
                ok = text_command(ui, 0, integer(p + 4), integer(p + 8), integer(p + 12),
                                  integer(p + 16), value, wide(p + 24), 0);
                break;
            }
            /* fall through */
        default:
            ok = static_record(ui, record);
            break;
        }
        if (!ok)
            return fail(ui);
    }
    ui->prepared = 1;
    return 1;
}

static void source_color(uint32_t rgba, int textured, float out[4])
{
    /* GS modulate uses 128 as unity; untextured RGB is the raw 0..255 value. */
    for (unsigned channel = 0; channel < 3; ++channel)
        out[channel] = (float)((rgba >> (8 * channel)) & 255) / (textured ? 128.0f : 255.0f);
    out[3] = (float)(rgba >> 24) / 128.0f;
}

static float canvas_x(float gs_x)
{
    return gs_x - 1792;
}

static float canvas_y(float gs_y)
{
    return (gs_y - 1936) * 2;
}

static int render_arc(EmGfx *gfx, const float descriptor[24], unsigned mode, float u, float v)
{
    EmItemVertex vertices[512];
    size_t count;
    if (!em_item_geometry_arc(descriptor, vertices, 512, &count))
        return 0;
    for (size_t i = 2; i < count; ++i) {
        float xy[3][2], color[3][4];
        for (unsigned corner = 0; corner < 3; ++corner) {
            const EmItemVertex *vertex = &vertices[i - 2 + corner];
            xy[corner][0] = canvas_x(vertex->x / 16.0f);
            xy[corner][1] = canvas_y(vertex->y / 16.0f);
            source_color(vertex->rgba, 0, color[corner]);
        }
        if (!em_gfx_overlay_triangle(gfx, xy, color, u, v, (EmGfxOverlayBlend)mode))
            return 0;
    }
    return 1;
}

/* Original 208750 line-strip endpoints keep their fixed-point values. Each
 * segment becomes a one-GS-pixel parallelogram: exact line coverage is the
 * GS/Metal rasterization boundary shared with tests/status_hub_visual.c. */
static int render_line(EmGfx *gfx, const uint32_t a[6], const uint32_t b[6], unsigned mode,
                       float u, float v)
{
    float x = canvas_x(a[4] / 16.0f), y = canvas_y(a[5] / 16.0f);
    float x1 = canvas_x(b[4] / 16.0f), y1 = canvas_y(b[5] / 16.0f);
    float dx = x1 - x, dy = (y1 - y) * 0.5f, nx, ny;
    if (dx == 0 && dy == 0)
        return 1;
    if (fabsf(dx) < fabsf(dy)) {
        nx = 1;
        ny = -dx / dy;
    } else {
        nx = -dy / dx;
        ny = 1;
    }
    const float corners[4][2] = {{x, y}, {x1, y1}, {x1 + nx, y1 + ny * 2}, {x + nx, y + ny * 2}};
    float colors[4][4];
    for (unsigned corner = 0; corner < 4; ++corner) {
        const uint32_t *source = corner == 0 || corner == 3 ? a : b;
        for (unsigned channel = 0; channel < 4; ++channel)
            colors[corner][channel] = (float)source[channel] / (channel == 3 ? 128.0f : 255.0f);
    }
    static const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned t = 0; t < 2; ++t) {
        float xy[3][2], color[3][4];
        for (unsigned i = 0; i < 3; ++i) {
            memcpy(xy[i], corners[indices[t][i]], sizeof xy[i]);
            memcpy(color[i], colors[indices[t][i]], sizeof color[i]);
        }
        if (!em_gfx_overlay_triangle(gfx, xy, color, u, v, (EmGfxOverlayBlend)mode))
            return 0;
    }
    return 1;
}

static int render_text(EmGfx *gfx, const EmStatusHubUICommand *command)
{
    EmHudTextStyle style;
    if (!text_style(command->proportional, command->xy[2], command->xy[3], &style))
        return 0;
    /* 001CC3B0 with a null style pointer fills with 0x80808080. */
    uint32_t rgb = command->null_style ? 0x808080u : (uint32_t)command->tex0 & 0xFFFFFFu;
    em_hud_text_color(gfx, canvas_x((float)command->xy[0]), canvas_y((float)command->xy[1]),
                      command->text, style, rgb);
    return 1;
}

static int render_command(EmStatusHubUI *ui, EmGfx *gfx, const EmStatusHubUICommand *command)
{
    const Sprite *white = &ui->sprites[ui->white];
    float u = white->u + 0.5f, v = white->v + 0.5f, color[4];
    switch (command->kind) {
    case EM_STATUS_HUB_UI_BLEND:
        return 1; /* Each queued primitive carries its original mode. */
    case EM_STATUS_HUB_UI_SPRITE: {
        const Sprite *sprite = find_sprite(ui, command->tex0);
        if (!sprite)
            return 0;
        source_color(command->rgba, 1, color);
        em_gfx_overlay_sprite_blend(gfx, canvas_x(command->xy[0] / 16.0f),
                                    canvas_y(command->xy[1] / 16.0f), (float)command->xy[2],
                                    (float)command->xy[3], (float)sprite->u, (float)sprite->v,
                                    (float)(sprite->u + sprite->w), (float)(sprite->v + sprite->h),
                                    color, (EmGfxOverlayBlend)command->mode);
        return 1;
    }
    case EM_STATUS_HUB_UI_RECTANGLE:
        source_color(command->rgba, 0, color);
        em_gfx_overlay_sprite_blend(gfx, canvas_x(command->xy[0] / 16.0f),
                                    canvas_y(command->xy[1] / 16.0f),
                                    (command->xy[2] - command->xy[0]) / 16.0f,
                                    (command->xy[3] - command->xy[1]) / 8.0f, u, v, u, v, color,
                                    (EmGfxOverlayBlend)command->mode);
        return 1;
    case EM_STATUS_HUB_UI_ARC:
        return render_arc(gfx, command->arc, command->mode, u, v);
    case EM_STATUS_HUB_UI_MARKER:
        for (unsigned strip = 0; strip < 9; ++strip)
            for (unsigned vertex = 1; vertex < 16; ++vertex) {
                const uint32_t *a = command->marker + (strip * 16 + vertex - 1) * 6;
                if (!render_line(gfx, a, a + 6, command->mode, u, v))
                    return 0;
            }
        return 1;
    case EM_STATUS_HUB_UI_TRAIL:
        /* 1D66A0 fan: centre RGB is intensity/255, outer vertices black. */
        for (unsigned i = 0; i < command->trail_count; ++i) {
            const int32_t *triangle = command->trail + i * 7;
            float xy[3][2], k = (float)triangle[6] / 255.0f;
            for (unsigned vertex = 0; vertex < 3; ++vertex) {
                xy[vertex][0] = canvas_x(triangle[vertex * 2] / 16.0f);
                xy[vertex][1] = canvas_y(triangle[vertex * 2 + 1] / 16.0f);
            }
            const float colors[3][4] = {{k, k, k, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
            if (!em_gfx_overlay_triangle(gfx, xy, colors, u, v, EM_GFX_UI_ADD))
                return 0;
        }
        return 1;
    case EM_STATUS_HUB_UI_TEXT:
        return render_text(gfx, command);
    }
    return 0;
}

static void view(const EmStatusHubUI *ui, const Command *command, EmStatusHubUICommand *out)
{
    memset(out, 0, sizeof *out);
    out->kind = (EmStatusHubUIKind)command->kind;
    out->mode = command->mode;
    memcpy(out->xy, command->xy, sizeof out->xy);
    out->rgba = command->rgba;
    out->tex0 = command->tex0;
    out->proportional = command->proportional;
    out->null_style = command->null_style;
    if (command->kind == EM_STATUS_HUB_UI_ARC)
        out->arc = command->arc;
    out->marker = command->marker;
    if (command->kind == EM_STATUS_HUB_UI_TRAIL) {
        out->trail_xy[0] = command->arc[0];
        out->trail_xy[1] = command->arc[1];
        out->trail = &ui->trail[0][0];
        out->trail_count = ui->trail_count;
    }
    if (command->kind == EM_STATUS_HUB_UI_TEXT)
        out->text = ui->arena + command->text;
}

/* Help calls live in the loaded records; view them without the arena. */
static void help_view(const Record *record, EmStatusHubUICommand *out)
{
    const uint8_t *p = record->body;
    Reader text = {p + 32, p + record->size};
    memset(out, 0, sizeof *out);
    out->kind = EM_STATUS_HUB_UI_TEXT;
    out->mode = record->mode;
    out->proportional = integer(p);
    for (unsigned i = 0; i < 4; ++i)
        out->xy[i] = integer(p + 4 + i * 4);
    out->null_style = (integer(p + 20) & TEXT_NULL_STYLE) != 0;
    out->tex0 = wide(p + 24);
    out->text = take_string(&text);
}

int em_status_hub_ui_tile(const EmStatusHubUI *ui, uint64_t tex0, float out[4])
{
    const Sprite *sprite = ui && out ? find_sprite(ui, tex0) : NULL;
    if (!sprite)
        return 0;
    out[0] = (float)sprite->u;
    out[1] = (float)sprite->v;
    out[2] = (float)sprite->w;
    out[3] = (float)sprite->h;
    return 1;
}

int em_status_hub_ui_bind(EmStatusHubUI *ui, EmGfx *gfx)
{
    if (!ui || ui->failed)
        return 0;
    if (!gfx)
        return fail(ui);
    if (!ui->uploaded) {
        if (!em_gfx_overlay_texture_set(gfx, EM_GFX_OVERLAY_TEX_UI, ui->pixels, ui->width,
                                        ui->height))
            return fail(ui);
        em_hud_decor_invalidate();
        ui->uploaded = 1;
    }
    return 1;
}

int em_status_hub_ui_render(EmStatusHubUI *ui, EmGfx *gfx, int help_line)
{
    if (!ui || ui->failed || !ui->prepared)
        return 0;
    if (!gfx || help_line < -1 || help_line >= HELP_LINES)
        return fail(ui);
    /* The font sheet is a required worker; missing glyphs are not skipped. */
    if (em_hud_text_width("0", EM_HUD_TEXT_NUM12) <= 0 ||
        em_hud_text_width("0", EM_HUD_TEXT_TALL) <= 0)
        return fail(ui);
    if (!em_status_hub_ui_bind(ui, gfx))
        return 0;
    em_gfx_overlay_canvas(gfx, EM_GFX_STATUS_W, EM_GFX_STATUS_H);
    int ok = 1;
    EmStatusHubUICommand command;
    for (unsigned i = 0; ok && i < ui->count; ++i) {
        view(ui, &ui->commands[i], &command);
        ok = render_command(ui, gfx, &command);
    }
    if (help_line >= 0)
        for (unsigned i = 0; ok && i < ui->help_count[help_line]; ++i) {
            help_view(&ui->help[help_line][i], &command);
            ok = render_text(gfx, &command);
        }
    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
    return ok ? 1 : fail(ui);
}

unsigned em_status_hub_ui_command_count(const EmStatusHubUI *ui)
{
    return ui && ui->prepared ? ui->count : 0;
}

int em_status_hub_ui_command(const EmStatusHubUI *ui, unsigned index, EmStatusHubUICommand *out)
{
    if (!ui || !ui->prepared || !out || index >= ui->count)
        return 0;
    view(ui, &ui->commands[index], out);
    return 1;
}

unsigned em_status_hub_ui_help_count(const EmStatusHubUI *ui, unsigned line)
{
    return ui && line < HELP_LINES ? ui->help_count[line] : 0;
}

int em_status_hub_ui_help(const EmStatusHubUI *ui, unsigned line, unsigned index,
                          EmStatusHubUICommand *out)
{
    if (!ui || !out || line >= HELP_LINES || index >= ui->help_count[line])
        return 0;
    help_view(&ui->help[line][index], out);
    return 1;
}
