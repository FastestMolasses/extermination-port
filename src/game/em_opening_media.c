#include "game/em_opening_media.h"
#include "game/em_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum { MAX_FADE_VALUES = 64 };

static struct {
    float fade_values[MAX_FADE_VALUES];
    unsigned fade_count, fade_next, fade_mode;
    int prepared, armed;
} s;

static unsigned u16(const unsigned char *b)
{ return (unsigned)b[0] | (unsigned)b[1] << 8; }
static unsigned u32(const unsigned char *b)
{ return u16(b) | u16(b + 2) << 16; }

static int read_fades(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char h[12];
    if (!f) return -1;
    int okay = fread(h, 1, sizeof h, f) == sizeof h &&
               !memcmp(h, "EMFX", 4) && u32(h + 4) == 1;
    unsigned count = okay ? u32(h + 8) : 0;
    okay = count && count <= MAX_FADE_VALUES &&
           fread(s.fade_values, sizeof(float), count, f) == count &&
           fgetc(f) == EOF;
    fclose(f);
    if (!okay || s.fade_values[count - 1] != 0) return -1;
    for (unsigned i = 0; i < count; i++)
        if (!isfinite(s.fade_values[i])) return -1;
    s.fade_count = count;
    return 0;
}

int em_opening_media_prepare(const char *directory)
{
    if (s.prepared) return 0;
    char path[1024];
    if (!directory ||
        snprintf(path, sizeof path, "%s/opening.emfx", directory) >= (int)sizeof path ||
        read_fades(path)) {
        fprintf(stderr, "opening: missing or malformed fade track in %s\n",
                directory ? directory : "(null)");
        return -1;
    }
    s.fade_next = s.fade_mode = 0;
    s.prepared = 1;
    return 0;
}

void em_opening_media_restart(void)
{
    s.armed = s.prepared;
    s.fade_next = s.fade_mode = 0;
}

void em_opening_media_stop(void)
{
    s.armed = 0;
}

void em_opening_media_camera_tick(float time)
{
    if (!s.armed || s.fade_next >= s.fade_count) return;
    float value = s.fade_values[s.fade_next];
    if (value < 0) {
        if (value == -3) s.fade_mode = 0;
        else if (value == -4) s.fade_mode = 0x80;
        else if (value == -1) s.fade_mode = 1;
        else if (value == -2) s.fade_mode = 0x81;
        s.fade_next++;
    } else if (time >= value && s.fade_next + 1 < s.fade_count) {
        int speed = (int)s.fade_values[s.fade_next + 1];
        em_frame_fade_start_colour((s.fade_mode & 1) ? 1 : -1,
                                   speed, (s.fade_mode & 0x80) != 0);
        s.fade_mode ^= 1;
        s.fade_next += 2;
        if (s.fade_next < s.fade_count && s.fade_values[s.fade_next] == 0)
            s.fade_next = s.fade_count;
    }
}

void em_opening_media_shutdown(void)
{
    memset(&s, 0, sizeof s);
}
