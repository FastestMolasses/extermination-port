/* Message line layout and draw: the draw half of 001FD950 and its callees.
 * Docs: docs/MESSAGE_DRAW.md.
 *
 * Hand translation of the original routines the message service's
 * `draw_line` worker stands for (em_message_service.h, EmMessageWorkers):
 *
 *   001FD950 (draw prefix only) bank select, two-segment measure, centring
 *   001FE070  (NEARMISS; translated from the .s) record-driven line walker
 *   001FE530  segment copy / skip on 0x0A and 0x0C
 *   001FE480  bank string address          001FE460  bank string count
 *   001FE4B0  line record count            001FE4D0  line record address
 *   001CC170  string width (sum of 001CBE10 advances of bytes >= 0x20)
 *   001FC770  default-config substitution, then 001FC7B0
 *   001232E0  strlen
 *
 * Verified by tools/test_message_draw_reference.py, which executes the
 * original instructions over captured EE RAM and compares every draw call
 * (position, text bytes, config words and the style bytes at that moment),
 * every glyph-advance request, the style bytes left behind and every
 * return value.
 *
 * Workers (fail-stop): 001CBE10 glyph advance and 001FC7B0 glyph-run draw.
 * A reached NULL worker, a worker that returns 0, or any read the original
 * would make outside the supplied bank / table / buffer latches a fault;
 * a latched fault makes every later call fail. No text, metric or colour
 * data is embedded: banks, D_0026EC10, D_00264CD0 and D_00264BF0 come from
 * the binder (the user's own ELF / RAM).
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_MESSAGE_DRAW_ORIGINAL_H
#define EM_MESSAGE_DRAW_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Buffer sizes. D_00820ED0 runs up to D_00820F90 (0xC0 bytes); D_00820F90
 * is cleared as 0x80 bytes by 001FE070. A store past either end, or a line
 * buffer handed to 001FC7B0 without a terminator inside it, faults (the
 * original would run into whatever follows). */
enum { EM_MESSAGE_DRAW_MEASURE_SIZE = 0xC0, EM_MESSAGE_DRAW_LINE_SIZE = 0x80 };

/* D_00275C50 (+0 colour word), D_00275C54 (+4 glyph byte), D_00275C55 (+5
 * flag byte): the text style the message service resets (001FC9B0) and
 * 001FE070's line records rewrite. Same byte layout as the original. */
typedef struct EmMessageTextStyle {
    int32_t color;
    uint8_t glyph;
    uint8_t flag;
    uint8_t pad[2];
} EmMessageTextStyle;

/* A 0x18-byte glyph-run config as 001FC7B0 receives it: D_00264CD0 for
 * message lines, the D_00264BF0 template when a caller of 001FC770 passes
 * none. word[2] (+0x08) and word[4] (+0x10) give 001FE070's line advance
 * (word[2] + word[4]) >> 1; +0x14 is the style pointer (D_00264CE4, which
 * the ELF initialises to &D_00275C50; the template's is 0 = NULL). */
typedef struct EmMessageDrawConfig {
    int32_t word[5];
    EmMessageTextStyle *style;
} EmMessageDrawConfig;

/* One message bank image starting at the address held in D_0028A4E8
 * (global) or D_0028A594 (area). All offsets the translation computes are
 * relative to `bytes` (the original's absolute address minus the bank
 * address, modulo 2^32). */
typedef struct EmMessageBank {
    const uint8_t *bytes;
    uint32_t size;
} EmMessageBank;

typedef struct EmMessageDrawData {
    EmMessageBank global;              /* *D_0028A4E8 (+0x34 bit 31 set)    */
    EmMessageBank area;                /* *D_0028A594                        */
    const int32_t *colors;             /* D_0026EC10 (record tags 2 and 4)   */
    uint32_t color_count;              /* 16 words in the ELF                */
    EmMessageDrawConfig *line_config;  /* D_00264CD0 (+0x14 = D_00264CE4)    */
    const EmMessageDrawConfig *fallback; /* D_00264BF0; NULL = not supplied  */
    EmMessageTextStyle *text;          /* D_00275C50 / D_00275C55 (tag 4)    */
} EmMessageDrawData;

/* Every worker returns 1 on success; 0 latches a fault. */
typedef struct EmMessageDrawWorkers {
    void *context;
    /* 001CBE10(c): pixel advance of byte c (called for c >= 0x20 only). */
    int (*glyph_advance)(void *context, uint8_t c, int32_t *advance);
    /* 001FC7B0(x, y, text, cfg): draw a run. `text` is NUL-terminated; the
     * style bytes are read through cfg->style at the time of the call. */
    int (*draw_text)(void *context, int32_t x, int32_t y, const uint8_t *text,
                     const EmMessageDrawConfig *cfg);
} EmMessageDrawWorkers;

typedef struct EmMessageDraw {
    const EmMessageDrawData *data;
    EmMessageDrawWorkers workers;
    uint8_t measure[EM_MESSAGE_DRAW_MEASURE_SIZE]; /* D_00820ED0 */
    uint8_t line[EM_MESSAGE_DRAW_LINE_SIZE];       /* D_00820F90 */
    const char *fault;                             /* latched     */
} EmMessageDraw;

/* Binds data and workers and clears both buffers. Returns 1, or 0 when
 * `draw` or `data` is NULL, or `line_config` / `text` is NULL, or a colour
 * count has no table (the module is then left unbound). */
int em_message_draw_init(EmMessageDraw *draw, const EmMessageDrawData *data,
                         const EmMessageDrawWorkers *workers);

/* The service's draw_line worker (void * = EmMessageDraw *): the 001FD950
 * draw prefix for line `index` (bit 31 of +0x34 masked off) of the global
 * (`global` != 0) or area bank: measure the first two 0x0A/0x0C segments,
 * x = 0x100 - max(w0, w1) / 2 (arithmetic shift), 001FE070(bank, index,
 * x, 0xC2). 001FE070's own 0/1 result is ignored, as in 001FD950.
 * Returns 1, or 0 on a (latched) fault. */
int em_message_draw_line(void *draw, int global, uint32_t index);

/* 001FE070(bank, index, x, y): the original's 0 (index outside the bank)
 * or 1, or -1 on a fault. */
int em_message_draw_fe070(EmMessageDraw *draw, const EmMessageBank *bank,
                          int32_t index, int32_t x, int32_t y);

/* 001FE530(dst, src, skip). `src` points at `avail` readable bytes; `dst`
 * may be NULL (nothing is stored), else it has `dst_size` bytes. *next is
 * the returned pointer as an offset from src. 0 ok, -1 fault. */
int em_message_draw_fe530(EmMessageDraw *draw, uint8_t *dst, uint32_t dst_size,
                          const uint8_t *src, uint32_t avail, int32_t skip,
                          uint32_t *next);

/* 001CC170(s): 0 ok (*width set), -1 fault. */
int em_message_draw_cc170(EmMessageDraw *draw, const uint8_t *s, uint32_t avail,
                          int32_t *width);

/* 001FC770(x, y, text, cfg): cfg NULL selects a copy of the D_00264BF0
 * template (fault when it was not supplied). 0 ok, -1 fault. */
int em_message_draw_fc770(EmMessageDraw *draw, int32_t x, int32_t y,
                          const uint8_t *text, const EmMessageDrawConfig *cfg);

/* 001232E0 over `avail` bytes: 1 and *length, or 0 when no NUL is inside. */
int em_message_draw_strlen(const uint8_t *s, uint32_t avail, uint32_t *length);

/* Bank accessors. 1 ok, 0 when a read falls outside the bank (fault). */
int em_message_bank_count(const EmMessageBank *bank, int32_t *count);            /* 001FE460 */
int em_message_bank_string(const EmMessageBank *bank, int32_t index, uint32_t *offset); /* 001FE480 */
int em_message_bank_records(const EmMessageBank *bank, int32_t index, uint32_t *count); /* 001FE4B0 */
/* 001FE4D0: *found = 0 for the original's 0 return, else 1 and *offset. */
int em_message_bank_record(const EmMessageBank *bank, int32_t index, uint32_t record,
                           int *found, uint32_t *offset);

#ifdef __cplusplus
}
#endif

#endif
