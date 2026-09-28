/* em_settings.c — the port's switches (em_settings.h). */
#include "em_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const EmSettings em_settings_original = {
    .ps2_disc_drive_timing = 0,   /* the disc answers at host speed */
};

static EmSettings g_settings = {
    .ps2_disc_drive_timing = 0,
};

const EmSettings *em_settings(void) { return &g_settings; }

void em_settings_set(const EmSettings *s)
{
    if (s)
        g_settings = *s;
}

/* A 0/1 switch variable: unset leaves *out alone; "0" / "1" set it. */
static int env_switch(const char *name, uint8_t *out)
{
    const char *v = getenv(name);
    if (!v)
        return 0;
    if (!strcmp(v, "0") || !strcmp(v, "1")) {
        *out = (uint8_t)(v[0] - '0');
        return 0;
    }
    fprintf(stderr, "settings: %s must be 0 or 1 (got \"%s\")\n", name, v);
    return -1;
}

int em_settings_from_env(void)
{
    EmSettings s = em_settings_original;
    if (env_switch("EM_PS2_DISC_DRIVE_TIMING", &s.ps2_disc_drive_timing))
        return -1;
    g_settings = s;
    return 0;
}
