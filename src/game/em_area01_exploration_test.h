/* Opt-in AREA01 input exploration, included only by the level-smoke test
 * driver. Reads live position/camera and submits pad input; never installs
 * player, scene, collision, or captured state. The Python runner keeps
 * completion, observed coverage and original-capture parity distinct. */
#ifndef EM_AREA01_EXPLORATION_TEST_H
#define EM_AREA01_EXPLORATION_TEST_H

enum { A01_EXPLORE_MAX = 256 };
typedef struct {
    char kind, label[64];
    float x, z, tolerance, magnitude;
    unsigned frames, buttons, lx, ly;
} A01ExploreStep;
static struct {
    A01ExploreStep steps[A01_EXPLORE_MAX];
    unsigned count, index, frames, total;
    int active;
} a01_explore;

static int a01_explore_begin(void)
{
    memset(&a01_explore, 0, sizeof a01_explore);
    const char *path = getenv("EM_AREA01_EXPLORE_SCRIPT");
    const char *phase = getenv("EM_AREA01_EXPLORE_PHASE");
    if (!path || !phase || strcmp(phase, k_phases[t.current].name))
        return 0;
    a01_explore.active = 1;
    FILE *f = fopen(path, "r");
    if (!f) { fail("AREA01 exploration script missing"); return 1; }
    char line[256], extra;
    unsigned version;
    int valid = fgets(line, sizeof line, f) &&
                sscanf(line, "EMAX %u %c", &version, &extra) == 1 && version == 1;
    while (valid && fgets(line, sizeof line, f)) {
        if (a01_explore.count == A01_EXPLORE_MAX) { valid = 0; break; }
        A01ExploreStep s = {0};
        if (sscanf(line, "move %f %f %f %f %u %c", &s.x, &s.z, &s.tolerance,
                   &s.magnitude, &s.frames, &extra) == 5) {
            s.kind = 'm';
            valid = isfinite(s.x) && isfinite(s.z) && isfinite(s.tolerance) &&
                    isfinite(s.magnitude) && s.tolerance > 0 &&
                    s.magnitude > 0 && s.magnitude <= 1 && s.frames && s.frames <= 2000;
        } else if (sscanf(line, "hold %u %x %u %u %c", &s.frames, &s.buttons,
                          &s.lx, &s.ly, &extra) == 4) {
            s.kind = 'h';
            valid = s.frames && s.frames <= 2000 && s.buttons <= 65535 && s.lx <= 255 && s.ly <= 255;
        } else if (sscanf(line, "face %f %u %c", &s.x, &s.frames, &extra) == 2) {
            s.kind = 'f';
            valid = isfinite(s.x) && s.frames && s.frames <= 2000;
        } else if (sscanf(line, "mark %63s %c", s.label, &extra) == 1) {
            s.kind = 'k';
        } else {
            valid = 0;
        }
        if (valid) a01_explore.steps[a01_explore.count++] = s;
    }
    valid = valid && feof(f) && a01_explore.count;
    fclose(f);
    if (!valid) { fail("malformed AREA01 exploration script"); return 1; }
    if (em_scene_state()->d810700 != 1) {
        fail("AREA01 exploration did not start in AREA01"); return 1;
    }
    fprintf(stderr, "AREA01 explore: BEGIN phase=%s counter=%u steps=%u\n",
            phase, em_frame_counter(), a01_explore.count);
    return 1;
}

static int a01_explore_frame(void)
{
    ++a01_explore.total;
    if (a01_explore.total > 20000) {
        fail("AREA01 exploration exceeded total input budget"); return 0;
    }
    while (a01_explore.index < a01_explore.count) {
        const A01ExploreStep *s = &a01_explore.steps[a01_explore.index];
        int done = 0;
        if (s->kind == 'k') {
            fprintf(stderr, "AREA01 explore: MARK %s counter=%u pos=(%.5f,%.5f,%.5f)\n",
                    s->label, em_frame_counter(), g.pos[0], g.pos[1], g.pos[2]);
            done = 1;
        } else if (s->kind == 'h') {
            done = a01_explore.frames >= s->frames;
            if (!done) pad_apply((uint16_t)s->buttons, (s->lx - 128.0f) / 128.0f,
                                (s->ly - 128.0f) / 128.0f);
        } else if (s->kind == 'm') {
            done = nav_stick_toward(s->x, s->z, s->magnitude) <= s->tolerance;
        } else if (s->kind == 'f') {
            float diff = fmodf(s->x - g.yaw + 3.14159265f, 6.28318531f);
            if (diff < 0) diff += 6.28318531f;
            diff -= 3.14159265f;
            done = fabsf(diff) <= 0.12f;
            if (!done) nav_stick_toward(g.pos[0] + 100 * sinf(s->x),
                                       g.pos[2] + 100 * cosf(s->x), 0.6f);
        }
        if (done) {
            fprintf(stderr, "AREA01 explore: STEP index=%u kind=%c counter=%u pos=(%.5f,%.5f,%.5f)\n",
                    a01_explore.index, s->kind, em_frame_counter(), g.pos[0], g.pos[1], g.pos[2]);
            ++a01_explore.index;
            a01_explore.frames = 0;
            /* Adjacent holds are one continuous pad timeline. Inserting a
             * neutral frame here drops R1 before an R1+Circle trigger. */
            if (s->kind == 'h' && a01_explore.index < a01_explore.count &&
                a01_explore.steps[a01_explore.index].kind == 'h') continue;
            pad_apply(0, 0, 0);
            if (s->kind == 'k') continue;
            return 0;
        }
        if (++a01_explore.frames > s->frames) {
            fprintf(stderr, "AREA01 explore: BLOCKED step=%u kind=%c counter=%u pos=(%.5f,%.5f,%.5f) target=(%.5f,%.5f)\n",
                    a01_explore.index, s->kind, em_frame_counter(), g.pos[0], g.pos[1], g.pos[2], s->x, s->z);
            fail("AREA01 exploration input did not reach its target");
        }
        return 0;
    }
    pad_apply(0, 0, 0);
    fprintf(stderr, "AREA01 explore: COMPLETE counter=%u frames=%u (input completed; coverage check required)\n",
            em_frame_counter(), a01_explore.total);
    return 1;
}
#endif
