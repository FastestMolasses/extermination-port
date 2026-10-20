/* em_memcard.h - the memory card: the platform boundary under the
 * memory-card screen 00225AC0 (docs/OPTIONS.md section 5).
 *
 * The original reaches the card through the SDK's libmc over SIF RPC to
 * the IOP's card server: 00114988 issues GetInfo(port, slot, &type,
 * &free, &format) (its RPC end callback 00114930 stores the three words),
 * 00114848 is Sync(mode, &cmd, &result). The port answers them from the
 * host, at host speed (the IOP and the card answer within the frame, as
 * the recordings show them: opt_07 and dmg_05 see the type word stored on
 * the frame GetInfo is issued and the result on the next Sync):
 *
 *   port 0 (slot 1 on the screen) is the host directory data/memcard/slot1,
 *   port 1 (slot 2) data/memcard/slot2 (relative to the working directory:
 *   the macOS app's directory, the iOS app's Documents). A directory holds
 *   the card's files as the game writes them, in the original's data
 *   layout (the card's root: the game's save directory and its files, byte
 *   for byte); the first level writes and reads none (docs/OPTIONS.md
 *   section 1: it has no save path, and its load row stops at the slot
 *   choice).
 *
 * GetInfo: type 2 (a formatted PS2 card) for a slot whose directory
 * exists; the result is 0 (the same card as the port's last GetInfo) once
 * the port has been asked since the boot, else -1 (a formatted card new to
 * the port). The boot's card check (the startup's 001AB9D0 state 3, the
 * native stand-in em_frontend's) creates both directories and asks both
 * ports, as the original's boot check 0022A460 does: the recordings' GetInfo
 * results in the options' load row are 0 on both ports. A free or format
 * pointer (no first-level caller passes one), a slot other than 0, a port
 * other than 0 / 1 or a missing directory faults: no recording shows what
 * the card server returns then. The store of a result goes to the caller
 * (the binder writes the words at the addresses the original passes).
 *
 * One storage for the card helpers' globals 001FE9A0 / 001FECB0 / 001FE920
 * / 001FE8D0 read and write: D_00275C58..D_00275C6F, D_00264E30..D_00264E3B
 * and D_00275840..D_00275847 (em_memcard_globals). */
#ifndef EM_MEMCARD_H
#define EM_MEMCARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_MEMCARD_DIR "data/memcard"

/* The file commands (docs/SAVE_GAME.md section 5). The save and load code
 * reaches the card's files through six more libmc calls, each issued the
 * same way as GetInfo (one pending command, D_00241D68 its number; its v0 is
 * that number while one is pending, else 0 issued) and completed by the
 * next Sync (the result word D_0027C680):
 *
 *   00114360 Open(port, slot, name, mode)    command 2; result a descriptor
 *   00114448 Mkdir(port, slot, name)         Open with mode 0x40, number 0xB
 *   00114480 Close(fd)                       command 3; result 0
 *   00114708 Write(fd, buf, size)            command 6; result the count
 *   00114630 Read(fd, buf, size)             command 5; result the count
 *   00114AC0 GetDir(port, slot, name, mode, max, table)  command 0xD;
 *                                            result the entries stored
 *
 * The card is the host directory data/memcard/slotN (port + 1): a card file
 * `D/F` is the host file data/memcard/slotN/D/F, byte for byte. The server
 * answers at host speed: the bytes Write is handed are read and written to
 * the host file when it is issued, and the bytes Read and GetDir store reach
 * EE memory then too (the original's IOP finishes within the frame on the
 * recordings; nothing reads them before the Sync that completes them).
 *
 * What the commands answer, and what they refuse (fault, as GetInfo does,
 * where no recording shows the server's answer): a name is the original's
 * relative card path (at most 0x3FF bytes, the SDK's copy; components of
 * letters, digits, '.', '_', '-', at most 31 bytes, never "." or "..";
 * GetDir's last component may hold '*' and '?', only below a directory).
 * Open modes 1 (read), 2 (write), 3 (both), each with 0x200 (create), and
 * Mkdir's 0x40; any other mode faults, as do an Open of a missing file
 * without 0x200, of a file in a missing directory or of a directory, a
 * Mkdir of a name that exists, a fourth open descriptor, a descriptor not
 * open, a Write on a read-only one or a Read on a write-only one, and a
 * GetDir with mode != 0 or of a directory that is missing. A descriptor is
 * the lowest free of 0..2 (the server's handle table; the recording that
 * checks it is docs/SAVE_GAME.md section 7's next step). Open does not
 * truncate; Write and Read move the descriptor's offset.
 *
 * GetDir: a name without a wildcard stores its one entry when it exists
 * (the result 1) and nothing when it does not (0); `D/pattern` lists "."
 * and ".." and then D's host entries in byte order of their names (host
 * names that begin with '.', such as Finder's .DS_Store, are not the
 * card's), each that matches, up to `max`. An entry is 0x40 bytes with its
 * name, NUL-padded, at +0x20; the port stores zero in the other 0x20 bytes
 * (the card server's dates, size and attributes, which no original that
 * reads the table reads: 00228C90 compares only the names). */

/* EE memory as the card server reaches it (the strings and buffers the
 * calls pass). Each returns 0, or -1 when the range is not the caller's. */
typedef struct {
    void *ctx;
    int (*read)(void *ctx, uint32_t address, void *dst, uint32_t size);
    int (*write)(void *ctx, uint32_t address, const void *src, uint32_t size);
} EmMemcardMemory;

/* The host directory that holds slot1 and slot2 (NULL: EM_MEMCARD_DIR,
 * relative to the working directory). Kept across em_memcard_reset. */
void em_memcard_set_root(const char *dir);
const char *em_memcard_root(void);

/* A word the SDK stores through a pointer its caller passed. */
typedef struct {
    uint32_t address;
    int32_t value;
} EmMemcardStore;

/* The boot's card check (em_frontend's stand-in for 001AB9D0 state 3):
 * creates data/memcard/slot1 and slot2 and asks both ports (GetInfo's
 * known state). 1 ok, 0 a directory could not be made. */
int em_memcard_boot_check(void);

/* The bytes of the card helpers' globals at an original address (see the
 * header), or NULL outside them. */
uint8_t *em_memcard_globals(uint32_t address, uint32_t size);

/* 00114988(port, slot, type, free, format): *v0 the original's return
 * (0 issued; the pending command when one is pending); stores[*n] the
 * words the RPC end callback stores (at most 3). 0, or -1 on a fault
 * (reported). */
int em_memcard_00114988(int32_t port, int32_t slot, uint32_t type, uint32_t free_words, uint32_t format,
                        int32_t *v0, EmMemcardStore stores[3], unsigned *n);
/* 00114848(mode, cmd, result): *v0 -1 (no command), 0 (busy; never at host
 * speed) or 1 (done: the command and result stored). 0, or -1 on a fault. */
int em_memcard_00114848(int32_t mode, uint32_t cmd, uint32_t result, int32_t *v0, EmMemcardStore stores[2],
                        unsigned *n);

/* The file commands (see above): *v0 the original's return (0 issued, or
 * the pending command's number). 0, or -1 on a fault (reported). */
int em_memcard_00114360(const EmMemcardMemory *m, int32_t port, int32_t slot, uint32_t name, int32_t mode,
                        int32_t *v0);
int em_memcard_00114448(const EmMemcardMemory *m, int32_t port, int32_t slot, uint32_t name, int32_t *v0);
int em_memcard_00114480(int32_t fd, int32_t *v0);
int em_memcard_00114708(const EmMemcardMemory *m, int32_t fd, uint32_t buf, int32_t size, int32_t *v0);
int em_memcard_00114630(const EmMemcardMemory *m, int32_t fd, uint32_t buf, int32_t size, int32_t *v0);
int em_memcard_00114AC0(const EmMemcardMemory *m, int32_t port, int32_t slot, uint32_t name, int32_t mode,
                        int32_t max, uint32_t table, int32_t *v0);

/* 1 when `target` is one of the eight libmc entries this module answers
 * (00114988, 00114848 and the six above), else 0. */
int em_memcard_is_sdk(uint32_t target);
/* One libmc call by its original address with the argument registers'
 * 64-bit images a[0..na) (a 32-bit argument as the EE holds it); the words
 * GetInfo and Sync store go through m->write. *v0 the original's v0
 * (sign-extended). 0, or -1 on a fault or a target that is not one of
 * them. */
int em_memcard_call(uint32_t target, const uint64_t *a, unsigned na, const EmMemcardMemory *m, uint64_t *v0);

/* Back to the boot state (no command pending, no port asked, no descriptor
 * open: the host files are closed, the globals as the boot leaves them). */
void em_memcard_reset(void);
/* The last fault text, or NULL. */
const char *em_memcard_fault(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_MEMCARD_H */
