# Message line layout and draw (message-glyph-draw lane)

Status: 2026-09-23. The translation and its oracle are done. Nothing in the
live game calls this module yet (see "Binding").

Files:
- `src/game/em_message_draw_original.{h,c}`: the native translation.
- `tools/test_message_draw_reference.py`: the original-instruction oracle.
- `tests/message_draw_test.c`: the native fail-stop contract (ASan/UBSan).
  The oracle compiles and runs it.

This module is the message service's `draw_line` worker
(`em_message_service.h`, `EmMessageWorkers.draw_line`). It replaces
`em_hud_subtitle` for script messages. Its callers are:
- panel `0x80000018` (route beats 00/03)
- refusal `0x8000001A` (02)
- director `0x97` / `0x99` (11/13)
- Roger `0x7F` chain (10)
- encounter lines (14)

## What it translates

| Original | Native | Notes |
|---|---|---|
| 001FD950 (draw prefix) | `em_message_draw_line` | Uses the bank word `D_0028A4E8` when bit 31 of +0x34 is set, else `D_0028A594`. The index has bit 31 masked off. Two `001FE530(D_00820ED0, v, 0)` + `001CC170` measures give w0 and w1. Then x = 0x100 − (max >> 1) (`sra`; `w0 < w1` picks w1), and it calls `001FE070(bank, index, x, 0xC2)`. The 0/1 result of 001FE070 is ignored, as it is in the original. The flag-mailbox half of 001FD950 stays in `em_message_service.c` (`present`). |
| 001FE070 (NEARMISS, from the .s) | `em_message_draw_fe070` | Details below. |
| 001FE530 | `em_message_draw_fe530` | With skip ≠ 0, the source moves past the skip-th 0x0A/0x0C within its strlen. It is unchanged when there are fewer. It then copies up to the next 0x0A/0x0C/NUL into dst (when dst ≠ 0) and terminates it. The return value points one past that separator, or at the NUL. |
| 001FE460 / 001FE480 / 001FE4B0 / 001FE4D0 | `em_message_bank_count/_string/_records/_record` | See the bank layout below. |
| 001CC170 | `em_message_draw_cc170` | strlen, then the sum of `001CBE10(c)` over the bytes c ≥ 0x20. The byte is `lbu`, so 0x80–0xFF are measured. |
| 001FC770 | `em_message_draw_fc770` | A NULL cfg becomes a copy of the `D_00264BF0` template, whose +0x14 style pointer is 0. It then calls 001FC7B0. |
| 001232E0 | `em_message_draw_strlen` | Plain strlen. The original's quadword/doubleword scan is executed by the oracle. |
| 00121A28 | `memset` | Clears the 0x80-byte `D_00820F90`. |

### 001FE070 walk

1. It returns 0 when the index is < 0 or ≥ 001FE460's count.
2. **No line records** (001FE4B0 = 0): it calls a single `001FC770(x, y, bank string, &D_00264CD0)`. The newlines
   go to 001FC7B0.
3. **With records** (16 bytes: tag, arg, trigger, byte +0xC): it clears `D_00820F90` and walks the text byte by
   byte while unconsumed records remain. It does not stop at the NUL.
   - **When the byte index equals the next record's trigger:**
     1. It flushes the current run through `001FC770` **before** it applies the record.
     2. It applies the record:
        - tag 4: `D_00275C50 = D_0026EC10[arg]` and `D_00275C55 = byte(+0xC) << 3`
        - tag 3: `D_00264CE4->+5 = byte(+4) << 3`
        - tag 2: `*D_00264CE4 = D_0026EC10[arg]`
        - any other tag: nothing
     3. It sets the run start to x + (width so far).
     4. It skips **unapplied** every following record with the same trigger.
   - **Byte 0x0A:** it flushes the run and returns to x. It resets the width and the run, and advances
     y by `(D_00264CD8 + D_00264CE0) >> 1`.
   - **Any other byte (the NUL included):** it adds its `001CC170` width to the line width and appends it to the
     run.
4. After the records are used up, the same byte step continues to strlen.
5. A final flush follows, and the function returns 1.
6. The NEARMISS C leaves out the width reset on 0x0A in the second walk. The .s has it. It cannot be observed
   (the width is only read in the record walk), but the native module follows the .s.

### Bank layout

These are the offsets from the bank address, as the four accessors read them.
- `tbl[0]` is the record area, `tbl[1]` the line count for 001FE4D0, and `tbl[2]` the record-area size.
- Line entry i is at `0x10 + 16 i`: +0 is the record offset (from `tbl[0]`), +0xC the record count << 4.
- The string section is at `H = tbl[0] + tbl[2]`: `H[0]` is the pool offset (from H) and `H[1]` the string
  count (001FE460).
- String entry i is at `H + 0x10 + 16 i`, and +0 holds its pool offset.

The oracle asserts that every string and record run lies inside the extent
`H + H[0] + H[2]`.

## Verified (last run, 2026-09-23)

`python3 tools/test_message_draw_reference.py` takes about 1 s. Its quick and
full modes differ only in the strlen grid (60 of 208 cases).
- Before anything runs, the executed routines' bytes in the capture are checked equal to the pinned ELF.
- The global bank (0x11729C0, 54 lines, no records) and the area bank (0x1432740, 161 lines, 20 of them with two
  tag-3 records) are the same bytes in every capture checked:
  - `opening`, `playable`, `elevator/refusal`, `panel`, `status-hub` and `roger-encounter`
  - route beats 00, 02, 10 and 14
- `D_00264CD0` (0, 6, 0x14, 0, 4, &D_00275C50), the `D_00264BF0` template and `D_0026EC10` are also the same in
  every capture.
- **001FD950 draw prefix:** the oracle runs it on every real line (215; 4,880 worker calls in order). It runs
  through a scratch entity with +0x6C = 1 and slot 0xFF, sequentially on one original and one native instance,
  so the persistent buffers and style carry over. That covers every in-scope line and all 20 record lines. For
  a record line the draws are:
  - an empty run at (x, 0xC2) in the previous style;
  - the text with flag byte 8, one run per 0x0A-separated line, with y + 0xC for each line;
  - an empty run at (x + the last line's width, y) with flag 0.

  For example, Roger's 0x7F has two lines, so it makes 4 draws. A line without records, such as 0x97 or
  0x80000018, makes one draw of the bank string.
- **Captured resumes:** 001FD950 runs on each capture's own `D_002821B0` block:
  - roger-encounter, area line 0x1 (the encounter line)
  - opening, 0x6D
  - elevator refusal, 0x8000001A
- **Synthetic banks** (this file's own ASCII): 16 lines × index −2..17 × three (x, y) pairs, which include
  negative values and values that wrap. They cover:
  - tags 0/2/3/4/7
  - a repeated trigger (the later records are skipped)
  - triggers past the NUL, and a trigger that is never reached
  - newlines inside record lines
  - control bytes and high bytes
  - empty lines, and a 131-byte two-line text
  - an index outside the bank (returns 0, no draw)
- **Accessors:** 001FE460/480/4B0 on every line of both banks. 001FE4D0 on indices −1, 0, 0x69, last, count and
  count + 3 × records 0/1/2/0xFFFFFFFF.
- **Other routines:**
  - 001232E0: alignment 0..15 × lengths 0..40
  - 001CC170 and 001FE530: 11 strings × skip 0/1/2/3/−1 × with and without dst
  - 001FC770: with the template and with `D_00264CD0`
- **Compared on every call:** each 001FC7B0 call (x, y, text bytes, config identity, the 5 config words, the 8
  style bytes behind +0x14 at call time), each 001CBE10 request, all return values, and the
  `D_00275C50..57` / `D_00820ED0` / `D_00820F90` bytes.
- **Writes:** every address the original stores to must be a byte the module models. Only the scratch entity's
  timer is exempt.
- **Mutation check:** 15 of 15 targeted mistranslations fail the oracle. They are:
  - y 0xC3
  - min instead of max
  - dropping the duplicate-trigger skip
  - flag shift 2
  - tag-4 flag read from +4
  - run start not x + width
  - 0x0C not a separator
  - next pointer past the NUL
  - advancing control bytes
  - line step without `D_00264CE0`
  - width kept on newline
  - applying a record before the flush
  - a skip off by one
  - unsigned 001FE4D0 index test
  - a no-record line drawn through the run buffer
- `tests/message_draw_test.c` (47 checks) covers:
  - init refusals
  - missing or failing workers, and the latched fault
  - a truncated bank, and an entry past the bank
  - colour indices outside `D_0026EC10`
  - a NULL `D_00264CE4` style
  - records with no record address (the original reads near address 0)
  - line-buffer and measure-buffer overruns
  - a missing template
  - unterminated strings

## Boundaries (workers, fail-stop)

- **001CBE10, the glyph advance.** It is a byte-matched switch over byte values: tall-font metrics, which are
  original data. The oracle backs the native worker with the original executed for all 256 values.
- **001FC7B0, the glyph-run draw** (NEARMISS, recursive; it calls 001CC1E0). It receives the NUL-terminated run,
  and the config whose +0x14 style is read at call time.
- Reads the original would make outside a supplied bank, table or buffer fault. They never read neighbouring
  memory. The buffers are `D_00820ED0` (0xC0 bytes, up to `D_00820F90`) and `D_00820F90` (0x80 bytes). The
  original would write into whatever follows them. The area-11 banks stay far inside these limits: the
  longest string is 80 bytes.

## Binding (for the coordinator chain)

1. **Worker slot.** `EmMessageWorkers.draw_line` has the same signature as `em_message_draw_line`. The service
   passes one shared `context`, so the adapter must call `em_message_draw_line(&draw, global, index)`. The call
   comes from 001FD950, i.e. `present` in `em_message_service.c`, every present tick. The module returns 0 on a
   fault; the service then latches `draw_line worker failed`.
2. **Style block.** `D_00275C50` is one block:
   - +0 colour
   - +4 glyph byte (`D_00275C54`)
   - +5 flag byte (`D_00275C55`)

   001FC9B0 (`em_message_reset`) writes it, and 001FE070's records write it too. Bind `data->text` and
   `line_config->style` to a single `EmMessageTextStyle`. `D_00264CE4` is statically `&D_00275C50` and has no
   other reference. The service currently keeps `text_color` / `text_glyph` / `text_flag` as separate fields.
   The adapter must make both views one storage (or copy them both ways around each call). Otherwise a record
   line's flag (8, then 0) and the reset defaults drift apart.
3. **Data, from a new exporter reading the user's own files:**
   - both bank images: the loaded message files at `*D_0028A4E8` and `*D_0028A594`, sized by the extent rule
     above
   - `D_0026EC10` (16 words)
   - `D_00264CD0` words 0–4
   - `D_00264BF0` words 0–4 (style pointer NULL)

   No values are embedded in the module.
4. **Workers:**
   - `glyph_advance` → a future 001CBE10 translation, or the tall-font metric table
   - `draw_text` → a 001FC7B0 port, or `em_hud`'s tall-font renderer, given (x, y, run, cfg, style)

   Until both are bound, keep the module unwired. A missing worker faults on the first message.
