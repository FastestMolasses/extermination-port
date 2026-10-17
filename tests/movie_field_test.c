/* The movie field clock (em_movie_field.h) against the fork's probes of
 * 00203350 (decomp build/fork_refs/probes_1016/movie_end and newgame,
 * docs/GS_EXACT.md 10.4): the gate on a field-0 vblank, two fields per
 * picture in order, the end stall and the held pictures, the play loop's
 * end on the last field-1 send, the two closing waits, field 1 at the
 * iteration's P, the sound's fields and the START skip after 11 decoded
 * pictures. The fork's opening waits for the disc (11 / 21 vblanks between
 * 002036E0's entry and the gate's call) are not host speed; every count
 * after the gate is compared as an offset from it. Runs in milliseconds. */
#include "game/em_movie_field.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                  \
            fputc('\n', stderr);                           \
            failures++;                                    \
        }                                                  \
    } while (0)

typedef struct {
    uint32_t steps;              /* port steps of the movie iteration, the arming one included */
    uint32_t gate;               /* vblank of the gate (0 = the first 001009C8 wait's) */
    uint32_t end;                /* the play loop's last vblank */
    int final_field;             /* the field the iteration's P reads */
    uint32_t sound_first, sound_last, sound_fields;
    uint32_t no_picture;
    uint32_t sends[2];
    int order_ok;                /* every picture: field 0 then field 1, in order, none dropped */
    uint32_t shown_last;         /* the last picture shown */
    uint32_t regular_last_f1;    /* the last field-1 send before the stall */
    uint32_t held_first_f0;      /* the first field-0 send after it */
    int tail_ok;                 /* the closing waits show the last picture, no sends */
    uint32_t skip_vblank;        /* the period the skip was honoured in (0: none) */
    uint32_t skip_decoded;
} Run;

/* f0: the GS field of the first 001009C8 wait's vblank (the arming step's).
 * skip_from: the pad's skip held from this vblank on (0: never). */
static Run run(EmMovieFieldConfig cfg, int f0, uint32_t skip_from)
{
    Run r;
    memset(&r, 0, sizeof r);
    r.order_ok = 1;
    r.tail_ok = 1;
    EmMovieField m;
    if (em_movie_field_begin(&m, &cfg) != 0) {
        CHECK(0, "begin refused");
        return r;
    }
    int playing = em_movie_field_main(&m, 0);
    uint32_t expect = 0;   /* the next picture whose field-0 half is due */
    int half = 0;          /* 0: its field-0 half next, 1: its field-1 half */
    uint32_t last_f1 = 0, gap_start = 0;
    r.steps = 1;
    for (uint32_t j = 1; playing && j < 1000000; ++j) {
        const int field = f0 ^ (int)(j & 1u);
        em_movie_field_vblank(&m, field);
        if (m.sent == 1) {
            if (half != 0 || (uint32_t)m.shown != expect || field != 0) r.order_ok = 0;
            half = 1;
            if (last_f1 && j - last_f1 > 1 && !r.held_first_f0) {
                r.regular_last_f1 = gap_start;
                r.held_first_f0 = j;
            }
        } else if (m.sent == 2) {
            if (half != 1 || field != 1) r.order_ok = 0;
            half = 0;
            expect++;
            last_f1 = gap_start = j;
        }
        if (m.sound_field) {
            if (!r.sound_first) r.sound_first = j;
            r.sound_last = j;
        }
        if (m.ended && j > m.end_vblank && (m.sent || (uint32_t)m.shown != expect - 1)) r.tail_ok = 0;
        em_movie_field_decode(&m);
        const int ready = em_movie_field_skip_ready(&m);
        const int skip = skip_from && j >= skip_from;
        if (skip && ready && !r.skip_vblank) {
            r.skip_vblank = j;
            r.skip_decoded = m.decoded;
        }
        playing = em_movie_field_main(&m, skip);
        r.steps++;
        r.final_field = field;
    }
    r.gate = m.gate_vblank;
    r.end = m.end_vblank;
    r.sound_fields = m.sound_fields;
    r.no_picture = m.no_picture;
    r.sends[0] = m.sends[0];
    r.sends[1] = m.sends[1];
    r.shown_last = (uint32_t)m.shown;
    if (expect != m.sends[1]) r.order_ok = 0;
    return r;
}

int main(void)
{
    /* E001.PSS (selector 1, the departure): 2,305 pictures; the fork's
     * canon run (its first wait's field 0) and d4 (field 1). */
    const EmMovieFieldConfig e001 = {2305, 2, 4731};
    for (int f0 = 0; f0 < 2; ++f0) {
        Run r = run(e001, f0, 0);
        /* host speed: the gate on the first field-0 vblank after the
         * second wait (vblank 1): vblank 2 or 3 */
        CHECK(r.gate == (f0 ? 3u : 2u), "E001 f0 %d gate %u", f0, r.gate);
        CHECK(r.order_ok, "E001 f0 %d picture order", f0);
        CHECK(r.sends[0] == 2305 && r.sends[1] == 2305, "E001 sends %u/%u", r.sends[0], r.sends[1]);
        /* fork canon: gate 458, first send 460, last regular field-1 send
         * 5065, held field-0 send 5190, play loop end 5193 (4735 after the
         * gate), return 5195; d4: gate 457, end 5192 */
        CHECK(r.regular_last_f1 == r.gate + 4607, "E001 last regular %u (gate %u)", r.regular_last_f1, r.gate);
        CHECK(r.held_first_f0 == r.gate + 4732, "E001 held send %u", r.held_first_f0);
        CHECK(r.held_first_f0 - r.regular_last_f1 == 125, "E001 stall gap %u", r.held_first_f0 - r.regular_last_f1);
        CHECK(r.no_picture == 124, "E001 no-picture vblanks %u (D_007A55F8)", r.no_picture);
        CHECK(r.end == r.gate + 4735, "E001 end %u", r.end);
        CHECK(r.steps == r.end + 3, "E001 steps %u: the end's vblank and two waits", r.steps);
        CHECK(r.final_field == 1, "E001 P reads field %d", r.final_field);
        CHECK(r.tail_ok && r.shown_last == 2304, "E001 tail shows picture %u", r.shown_last);
        CHECK(r.sound_first == r.gate + 1 && r.sound_last == r.end && r.sound_fields == r.end - r.gate,
              "E001 sound fields %u..%u (%u)", r.sound_first, r.sound_last, r.sound_fields);
        printf("movie field: E001 first wait on field %d: %u steps, gate %u, end %u, P field %d, "
               "%u pictures, stall %u\n", f0, r.steps, r.gate, r.end, r.final_field, r.sends[1], r.no_picture);
    }
    /* E900.PSS (selector 0: the intro and the New Game movie), the fork's
     * New Game run d0 played to its end: gate 289, first send 291, feed end
     * 9907, held sends 9909..9912, return 9914. */
    const EmMovieFieldConfig e900 = {4761, 2, 9618};
    {
        Run r = run(e900, 0, 0);
        CHECK(r.gate == 2, "E900 gate %u", r.gate);
        CHECK(r.order_ok && r.sends[1] == 4761, "E900 pictures %u", r.sends[1]);
        CHECK(r.held_first_f0 == r.gate + 9620 && r.held_first_f0 - r.regular_last_f1 == 101,
              "E900 held send %u gap %u", r.held_first_f0, r.held_first_f0 - r.regular_last_f1);
        CHECK(r.end == r.gate + 9623 && r.steps == r.end + 3 && r.final_field == 1, "E900 end %u", r.end);
        /* the fork's D_007A55F8 reached 99 in New Game runs d0 and d1 */
        CHECK(r.no_picture == 99, "E900 no-picture vblanks %u (D_007A55F8)", r.no_picture);
        CHECK(r.tail_ok && r.shown_last == 4760, "E900 tail shows picture %u", r.shown_last);
        printf("movie field: E900: %u steps, gate %u, end %u, P field %d, stall %u\n", r.steps, r.gate, r.end,
               r.final_field, r.no_picture);
    }
    /* The skip: START held from the first vblank (EM_NEW_GAME=1, the
     * replay). 002036E0 honours it once the decoder's count is 11, which
     * happens when picture 8's field-1 send frees a slot (picture 10
     * decoded); the decoder, already past its test, decodes picture 11,
     * then stops; the ring's pictures 10 and 11 are still shown. The fork
     * (decomp probes_1016/skip/d0_start_1016c, START held from the
     * driver's entry; tools/test_movie_skip_reference.py compares the
     * port's run with it) shows the same 12 pictures, the loop's end at
     * gate + 25 and the return at gate + 27 on field 1; its 00206B70 call
     * came at gate + 22 with the count at 11: its feed loop tests the pad
     * between reads that run at emulated disc and IOP speed. */
    for (int f0 = 0; f0 < 2; ++f0) {
        Run r = run(e900, f0, 1);
        CHECK(r.skip_decoded == 11 && r.skip_vblank == r.gate + 19, "skip at %u with %u decoded",
              r.skip_vblank, r.skip_decoded);
        CHECK(r.order_ok && r.sends[1] == 12 && r.shown_last == 11, "skip showed %u pictures", r.sends[1]);
        CHECK(r.end == r.gate + 25 && r.steps == r.end + 3 && r.final_field == 1, "skip end %u", r.end);
        CHECK(r.sound_fields == 25, "skip sound fields %u", r.sound_fields);
        printf("movie field: skip held (first wait field %d): %u steps, pictures 0..%u, P field %d\n", f0,
               r.steps, r.shown_last, r.final_field);
    }
    /* A skip pressed later (the fixtures hold START from picture 60's
     * display): honoured in that period, the ring's pictures shown. */
    {
        Run r = run(e001, 0, 2 + 2 + 2 * 60);
        CHECK(r.skip_vblank == 124 && r.order_ok && r.shown_last == 62, "late skip at %u showed to %u",
              r.skip_vblank, r.shown_last);
        CHECK(r.final_field == 1, "late skip P field %d", r.final_field);
    }
    /* The configuration the model cannot play. */
    {
        EmMovieField m;
        const EmMovieFieldConfig tiny = {3, 2, 10};
        CHECK(em_movie_field_begin(&m, &tiny) != 0, "a movie shorter than the ring and held pictures");
    }
    if (failures) {
        fprintf(stderr, "movie field: FAIL (%d)\n", failures);
        return 1;
    }
    printf("movie field: PASS\n");
    return 0;
}
