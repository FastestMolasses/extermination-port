/* Cold-boot/title control flow recovered from SCUS-97112:
 * 001AB7E0/AB9D0/ABC60/ABE10/AC070/AC3B0/AC480. Portable C semantics;
 * PS2 byte matching remains the companion decomp's build responsibility.
 * Host services supply original assets, movie playback and card/save UI.
 * A missing service remains pending: there is no invented timeout or skip.
 */
#ifndef EM_STARTUP_H
#define EM_STARTUP_H

#include <stdint.h>

typedef enum {
    EM_STARTUP_BOOT_RESOURCES,   /* initial bank/pointer/card-state bring-up */
    EM_STARTUP_SCREEN_MODULE,    /* id = DATA.DAT module */
    EM_STARTUP_RESOURCE_BANK,    /* id = additional resident bank */
    EM_STARTUP_CARD_CHECK,       /* original 0022A460(0) gate */
    EM_STARTUP_TITLE_RESOURCES,  /* original FB370(D_0028A4A8) readiness */
    EM_STARTUP_MOVIE,            /* id = movie selector; 0 is E900.PSS */
    EM_STARTUP_MOVIE_SKIP,       /* signal stop; MOVIE still needs completion */
    EM_STARTUP_FADE_FULL,
    EM_STARTUP_FADE_IN,          /* value = speed, black/subtractive fade */
    EM_STARTUP_FADE_OUT,
    EM_STARTUP_AUDIO_STOP,
    EM_STARTUP_AUDIO_LEVEL,      /* 00119978: driver command0x28, core id0/1 */
    EM_STARTUP_EFFECT_LEVEL,     /* 00119828: distinct command0x16, core id0/1 */
    EM_STARTUP_CUE,              /* id = original cue; gain 0x1000 per channel */
    EM_STARTUP_NEW_GAME,         /* handoff id0=fresh (later movie0), id1=loaded */
    EM_STARTUP_LOAD_GAME,        /* completion 1=cancel, 2=loaded */
    EM_STARTUP_OPTIONS,          /* complete when original option screen closes */
    EM_STARTUP_ATTRACT           /* id = cycle 0..2; complete on original exit */
} EmStartupEventKind;

typedef struct {
    EmStartupEventKind kind;
    uint32_t serial;             /* nonzero: host must call complete(serial,...) */
    int id;
    int value;
} EmStartupEvent;

typedef void (*EmStartupNotify)(void *user, const EmStartupEvent *event);

typedef enum {
    EM_STARTUP_SCREEN_NONE,
    EM_STARTUP_SCREEN_CARD,
    EM_STARTUP_SCREEN_LOGO_A,
    EM_STARTUP_SCREEN_LOGO_B,
    EM_STARTUP_SCREEN_LOGO_C,
    EM_STARTUP_SCREEN_MOVIE,
    EM_STARTUP_SCREEN_TITLE,
    EM_STARTUP_SCREEN_EXTERNAL
} EmStartupScreen;

typedef struct {
    uint16_t held;               /* canonical EM_PAD_* from em_input.h */
    uint16_t pressed;
    int fade_state;              /* original 0=clear, 1=in, 2=black, 3=out */
    int movie_skip_ready;        /* original decoder field +8 >= 11; not seconds */
} EmStartupInput;

typedef struct {
    EmStartupScreen screen;
    unsigned cursor;            /* title 0..2 */
    unsigned timer;             /* original u16 countdown, wraps on old zero */
    int interactive;
    int handed_off;
    int failed;                 /* host completion failure; do not advance */
    uint32_t pending_serial;
    EmStartupEventKind pending_kind;
} EmStartupView;

/* Public storage for stack/static allocation; fields are private to .c.
 * Host may complete requests synchronously inside notify. Do not call tick
 * recursively or alter the state directly. Rendering uses em_startup_view. */
typedef struct {
    EmStartupNotify notify;
    void *user;
    uint32_t next_serial, pending_serial;
    EmStartupEventKind pending_kind;
    int pending_result;
    unsigned flow, major, sub, aux, cursor, cycle_mode, attract_cycle;
    uint16_t timer;
    EmStartupScreen screen;
    int failed, handed_off, movie_skip_sent;
} EmStartup;

void em_startup_init(EmStartup *startup, EmStartupNotify notify, void *user);
/* One ordinary engine iteration. Apply emitted fade changes immediately;
 * tick the host's fade AFTER this call, matching original task->fade order. */
void em_startup_tick(EmStartup *startup, const EmStartupInput *input);
/* The original movie call suspends ordinary task/fade ticks. Native media
 * pumps call this helper for held-input skip while em_startup_tick is paused.
 * It emits at most one skip request and never completes playback itself. */
void em_startup_movie_input(EmStartup *startup, uint16_t held,
                            int movie_skip_ready);
/* result>0 completes, 0 keeps waiting, result<0 records a service failure.
 * Returns 0 for a stale/wrong serial. Failed operations can be retried by
 * completing the same pending serial successfully; no request is reissued. */
int em_startup_complete(EmStartup *startup, uint32_t serial, int result);
EmStartupView em_startup_view(const EmStartup *startup,
                              const EmStartupInput *input);

#endif
