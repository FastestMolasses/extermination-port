/* Test-only view of the live service; no alternate message implementation. */
#include "../src/game/em_message_live.c"

static EmSceneState scene;
static unsigned installations;
static int events[128][5], event_count;
static int8_t busy[3];

EmSceneState *em_scene_state(void) { return &scene; }
void em_frame_set_message_service(const EmFrameMessageService *service)
{ if (service) ++installations; }
int em_hud_tall_glyph_cell(uint32_t index, EmHudGlyphCell *cell)
{ (void)index; (void)cell; return 1; }
void em_hud_glyph_strip(EmGfx *gfx, const EmMessageGlyphFlush *flush)
{ (void)gfx; (void)flush; }

static int event(int kind, int a, int b, int c, int d)
{
    if (event_count >= 128) return 0;
    int *e = events[event_count++];
    e[0] = kind; e[1] = a; e[2] = b; e[3] = c; e[4] = d;
    return 1;
}
static int face(void *ctx, int on)
{ (void)ctx; return event(2, on, 0, 0, 0); }
static int voice(void *ctx, int32_t cue)
{ (void)ctx; return event(3, cue, 0, 0, 0); }
static int lane(void *ctx, int n)
{ (void)ctx; return event(4, n, 0, 0, 0); }
static int stop(void *ctx, int32_t mask)
{ (void)ctx; return event(5, mask, 0, 0, 0); }
static int play(void *ctx, int n, int32_t cue)
{ (void)ctx; return event(6, n, cue, 0, 0); }
static int8_t active(void *ctx, int n)
{ (void)ctx; return busy[n]; }
static int help(void *ctx, int x, int y, int32_t group, uint32_t line)
{ (void)ctx; return event(8, x, y, group, (int)line); }

int area_test_install(const char *path)
{
    memset(&scene, 0, sizeof scene);
    scene.d810700 = 11;
    if (!em_message_live_install(path)) return 0;
    EmMessageLiveHost host = {NULL, face};
    EmMessageLiveStreams streams = {NULL, stop, play, voice, lane, active};
    EmMessageLivePresenters presenters = {0};
    presenters.help_draw = help;
    em_message_live_set_host(&host);
    em_message_live_set_streams(&streams);
    em_message_live_set_presenters(&presenters);
    return 1;
}

int area_test_switch(const char *path, uint32_t area)
{
    EmMessageService service = s.service;
    EmMessageDraw draw = s.draw;
    EmMessageGlyph glyph = s.glyph;
    EmMessageTextStyle style = s.style;
    EmMessageLiveHost host = s.host;
    EmMessageLiveStreams streams = s.streams;
    EmMessageLivePresenters presenters = s.presenters;
    unsigned installed = installations;
    scene.d810700 = (uint8_t)area;
    if (!em_message_live_select_area(path, area)) return 0;
    return !memcmp(&service, &s.service, sizeof service) &&
           !memcmp(&draw, &s.draw, sizeof draw) &&
           !memcmp(&glyph, &s.glyph, sizeof glyph) &&
           !memcmp(&style, &s.style, sizeof style) &&
           !memcmp(&host, &s.host, sizeof host) &&
           !memcmp(&streams, &s.streams, sizeof streams) &&
           !memcmp(&presenters, &s.presenters, sizeof presenters) &&
           installed == installations;
}

int area_test_prime(void)
{
    static const uint8_t probe[] = "test";
    if (em_message_live_reset() < 0 ||
        em_message_live_cc1e0(0, 8, 8, 0, 30, probe, sizeof probe, &s.style) < 0) return 0;
    /* Synthetic buffer sentinels make an accidental draw reset visible;
     * the glyph records above come from executing the actual glyph owner. */
    memset(s.draw.measure, 0xA5, sizeof s.draw.measure);
    memset(s.draw.line, 0x5A, sizeof s.draw.line);
    return s.glyph.strip_count != 0 && s.task.flush_count != 0;
}

const uint8_t *area_test_bank(int global, uint32_t *size)
{
    const EmMessageBank *b = global ? &s.draw_data.global : &s.draw_data.area;
    *size = b->size;
    return b->bytes;
}

void area_test_case(const uint8_t *block, const uint8_t *sh, int mode, int b1, int b2)
{
    memcpy(&s.service.block, block, sizeof s.service.block);
    memcpy(em_scene_req_at(&scene, 0x8106F4), sh, 2);
    memcpy(em_scene_req_at(&scene, 0x8106D4), sh + 2, 12);
    scene.spad3B8F = (uint8_t)mode;
    busy[1] = (int8_t)b1; busy[2] = (int8_t)b2;
    event_count = 0;
    s.task.flush_count = s.task.upload_count = 0;
}

void area_test_shared(uint8_t *out)
{
    memcpy(out, em_scene_req_at(&scene, 0x8106F4), 2);
    memcpy(out + 2, em_scene_req_at(&scene, 0x8106D4), 12);
}
int area_test_events(int *out)
{
    memcpy(out, events, (size_t)event_count * sizeof events[0]);
    return event_count;
}
