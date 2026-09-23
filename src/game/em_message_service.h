/* Single native message service over the original D_002821B0 block (WP-8).
 *
 * One machine, ticked once per frame at main-loop step F (001FCA10), that
 * owns the 0x9C-byte request block and the three presenter defaults that
 * 001FC9B0 resets. Requests arrive through the op0C translation
 * (001B7D60) or by writing the block the way the original callers do.
 *
 * Translated originals: 001FCA10 (phase/mode dispatch), 001FDB80 (mode-2
 * tick and teardown), 001FD790 (record fetch), 001FD950 (present + flag
 * mailbox), 001FD580 / 001FD6A0 (first / next voice lookup), 001FA5A0
 * (voice ring push helper), 001FAB80 (lane stop), 001FC9B0 (reset),
 * 001FD4C0 (area stream table request) and 001B7D60 (op0C).
 *
 * Everything else is a worker: glyph layout and drawing, the mode-3 and
 * mode-4 presenters, the audio lanes and the player face. A worker that is
 * needed and missing latches a fault; the service never simulates it.
 * No text, timing or cue data is embedded: tables are supplied by the
 * binder from the user's own ELF (D_00264DD0 / D_0026EC60). */
#ifndef EM_MESSAGE_SERVICE_H
#define EM_MESSAGE_SERVICE_H
#include <stddef.h>
#include <stdint.h>

/* D_002821B0, cleared as 0x9C bytes by 001FC9B0. Named fields are the ones
 * the translated functions read or write; the reserved ranges belong to the
 * mode-3 presenter 001FD0E0 (a worker) and are only carried and cleared. */
typedef struct EmMessageBlock {
    int32_t  mode;             /* +0x00 D_002821B0: 2 text, 3, 4, 16      */
    int32_t  phase;            /* +0x04 D_002821B4: 0 idle 1 busy 2 done  */
    uint32_t line;             /* +0x08 D_002821B8: bit31 global bank     */
    int32_t  delay;            /* +0x0C D_002821BC: mode-2 pre-delay      */
    uint8_t  presenter_10[0x10];
    uint32_t cursor;           /* +0x20 D_002821D0 = &D_00264D10 (FC9B0) */
    uint8_t  presenter_24[0x10];
    uint32_t current;          /* +0x34 line + record (FD790)             */
    uint8_t  presenter_38[0x18];
    uint8_t  wait_stream;      /* +0x50 record +5                         */
    uint8_t  slot;             /* +0x51 record +4, 0xFF none              */
    uint8_t  presenter_52[0x0A];
    int32_t  loaded;           /* +0x5C FDB80 sub-state 0 fetch 1 present */
    int32_t  record;           /* +0x60 record index                      */
    uint32_t talk_mask;        /* +0x64 flag-mailbox bits set by FD950    */
    int32_t  frames;           /* +0x68 incremented per present tick      */
    int32_t  remaining;        /* +0x6C record timer                      */
    int32_t  voice_line;       /* +0x70 FD580/FD6A0 voice bookkeeping     */
    uint16_t status;           /* +0x74 D_00282224 (op0C sub1 handshake)  */
    uint16_t status_76;        /* +0x76 cleared by FDB80(1)               */
    uint8_t  presenter_78[0x18];
    int32_t  aux_mode;         /* +0x90 D_00282240 mode-4 group           */
    int32_t  aux_arg;          /* +0x94 D_00282244                        */
    int32_t  aux_result;       /* +0x98 D_00282248 = 001FCF90(...)        */
} EmMessageBlock;

enum { EM_MESSAGE_BLOCK_SIZE = 0x9C, EM_MESSAGE_FLAG_SLOTS = 12,
       EM_MESSAGE_VOICE_RING = 16 };

/* D_00264DD0[n] 8-byte record (stride 8 in 001FD790/001FD580/001FD6A0):
 * +0 u16 duration, +2 s16 voice cue, +4 u8 flag slot, +5 u8 wait/terminal.
 * +6..+7 are not read by the message service. */
typedef struct {
    uint16_t duration;
    int16_t  voice;
    uint8_t  slot;
    uint8_t  wait_stream;
    uint8_t  unread[2];
} EmMessageRecord;

typedef struct {
    const EmMessageRecord *records; /* NULL = no table (original 0 pointer) */
    uint32_t count;                 /* reads at or past count fault         */
} EmMessageTable;

/* D_0026EC60 16-byte row {area, word1, line, cue}; the -1 terminator row is
 * not included (count gives the length). */
typedef struct {
    int32_t area, word1, line, cue;
} EmMessageStreamRow;

typedef struct {
    EmMessageTable global;          /* D_00264DD0[0]                     */
    const EmMessageTable *areas;    /* D_00264DD0[area + 1]              */
    uint32_t area_count;
    const EmMessageStreamRow *streams;
    uint32_t stream_count;
    int32_t  default_color;         /* D_0026EC10[0], stored by FC9B0     */
    uint32_t default_cursor;        /* address token &D_00264D10 (+0x20)  */
} EmMessageData;

/* State owned by other services and shared with this one. */
typedef struct {
    uint8_t  area;                  /* D_00810700                         */
    uint8_t  game_mode;             /* scratchpad 0x70003B8F              */
    int8_t   busy155, busy156;      /* D_00282155 / D_00282156 (lb)       */
    uint8_t *voice_mode;            /* D_008106F5, also the voice lane's  */
    uint8_t *stream_mode;           /* D_008106F4                         */
    uint8_t *flag_mailbox;          /* D_008106D4[12] script handshake    */
} EmMessageShared;

/* Every worker returns 1 on success; 0 latches a fault. */
typedef struct {
    void *context;
    /* 001FD950 -> 001FE070(bank table, index, centred x, 0xC2). The worker
     * owns 001FE480/001FE530/001CC170 layout; global = bit 31 of +0x34
     * (D_0028A4E8 bank) else the area bank D_0028A594. */
    int (*draw_line)(void *, int global, uint32_t index);
    int (*face_talk)(void *, int on);              /* 001D06E0(&D_008102B0) */
    int (*voice_push)(void *, int32_t cue);        /* 001FA5A0              */
    int (*stop_lane)(void *, int lane);            /* 001FAAC0              */
    int (*stream_stop)(void *, int32_t mask);      /* 001FD470              */
    int (*stream_play)(void *, int lane, int32_t cue); /* 001FA790          */
    int (*mode3_present)(void *, EmMessageBlock *); /* 001FD0E0(block, 2)   */
    int (*help_draw)(void *, int x, int y, int32_t group, uint32_t line); /* 001FCB90 */
    int (*record_setup)(void *, uint32_t line, int32_t arg, int32_t group,
                        int32_t *result);          /* 001FCF90              */
    int (*record_draw)(void *, uint32_t line, int x, int y); /* 001FCF60    */
} EmMessageWorkers;

typedef struct {
    EmMessageBlock block;
    int32_t text_color;             /* D_00275C50                         */
    uint8_t text_glyph;             /* D_00275C54                         */
    uint8_t text_flag;              /* D_00275C55                         */
    const EmMessageData *data;
    EmMessageWorkers workers;
    const char *fault;              /* latched; ticks refuse after a fault */
} EmMessageService;

/* D_00281CF0[16] ring with head D_00275B30 (owned by the voice lane). */
typedef struct {
    int32_t slots[EM_MESSAGE_VOICE_RING];
    int8_t  head;
} EmMessageVoiceRing;

/* Binds data and workers. The block starts as supplied by the caller
 * (zeroed, or a captured image); call em_message_reset for 001FC9B0.
 * Returns 1, or 0 when data is NULL or a nonzero count has no table
 * (areas, streams or global records); the service is then left unbound. */
int em_message_init(EmMessageService *, const EmMessageData *, const EmMessageWorkers *);
/* 001FC9B0: clear the block, restore text defaults. */
void em_message_reset(EmMessageService *);
/* 001FCA10, main-loop step F. 0 ok, -1 fault (see ->fault). */
int em_message_tick(EmMessageService *, const EmMessageShared *);
/* 001B7D60(op0C): record = the script record (sub at +0x08, words +0x14,
 * +0x18, +0x1C); handshake = the caller's state byte +4. Returns the
 * original 0 pending / 1 complete. Returns -1 without touching the block
 * when a fault is already latched, and latches one for a NULL handshake or
 * record (native contract; the original has no failure path). */
int em_message_op0c(EmMessageService *, uint8_t *handshake, const unsigned char *record);
/* 001FD4C0: area stream-table request. 1 started, 0 no row, -1 fault. */
int em_message_stream_request(EmMessageService *, const EmMessageShared *, int32_t line);
/* 001FA5A0 semantics for a binder that keeps the ring natively. Returns
 * the original 1, or -1 when head is outside 0..15. */
int em_message_voice_ring_push(EmMessageVoiceRing *, int32_t cue);

#endif
