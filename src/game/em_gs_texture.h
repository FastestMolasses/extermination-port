/* em_gs_texture - the status pages' GS local memory and their texture
 * decode (docs/STATUS_PAGES.md section 5, "Textures").
 *
 * The data (assets/status_pages/status_pages.emsp, tools/export_status_pages.py,
 * from the user's own ELF and disc) holds the first level's GS memory after
 * a New Game, the set of blocks an upload wrote, and the blocks each page
 * module load (001FF830 through 001FF3F0 and state 7) and 0020CDC0's
 * 00200970(1) restore write. The port applies those steps in the order the
 * original runs them, so a page's TEX0 decodes from the GS memory the
 * original would hold then. Version 2 adds the resource-slot words a module
 * load relocates (001FF830 state 7: D_0028A490[slot] = its destination +
 * the slot's offset; module 0x1E's slot 0x38 is D_0028A570, the MAP page's
 * model bank); applying the module's step sets them.
 *
 * Decode: the TEX0 forms the status pages use, PSMT8 (256-entry CLUT) and
 * PSMT4 (16-entry CLUT), each with a PSMCT32 CLUT stored CSM1 at CBP with
 * CSA 0; the GS page / block / column layout of those formats; alpha 0..0x80
 * scaled by 2 (capped at 255); rows in screen order (the stored v-flip
 * undone), as tools/export_ui.py's decode_token_lm does for the other status
 * atlases. Any other form, or a texel or CLUT in a block no upload wrote,
 * is refused (fail-stop): the caller faults. */
#ifndef EM_GS_TEXTURE_H
#define EM_GS_TEXTURE_H

#include <stddef.h>
#include <stdint.h>

typedef struct EmGsTexture EmGsTexture;

/* The .data windows of the file (original address, size, bytes); the
 * status pages' views read them. */
typedef struct {
    uint32_t address, size;
    const uint8_t *bytes;
} EmGsDataWindow;

enum { EM_GS_TEXTURE_RESTORE = 0xFF }; /* the 00200970(1) step */

EmGsTexture *em_gs_texture_load(const char *path);
void em_gs_texture_free(EmGsTexture *gs);
/* Back to the world image (a New Game in AREA11, no page loaded). */
void em_gs_texture_reset(EmGsTexture *gs);
/* Applies one step (a module id, or EM_GS_TEXTURE_RESTORE). 1, or 0 when
 * the file holds no such step. */
int em_gs_texture_apply(EmGsTexture *gs, unsigned step);
/* Bumped by every applied step (a decode cache key). */
uint32_t em_gs_texture_generation(const EmGsTexture *gs);
/* The size of TEX0's texture (1 << TW, 1 << TH). 1, or 0 for a form the
 * decoder refuses. */
int em_gs_texture_size(uint64_t tex0, uint32_t *w, uint32_t *h);
/* Decodes TEX0 into rgba (w * h * 4 bytes, screen rows). 1, or 0 when
 * refused (see the header). */
int em_gs_texture_decode(const EmGsTexture *gs, uint64_t tex0, uint8_t *rgba, size_t size);
unsigned em_gs_texture_windows(const EmGsTexture *gs, const EmGsDataWindow **windows);
/* The value a relocated resource-slot word (e.g. D_0028A570) holds now: 0
 * until the step that loads its module is applied (and after a reset), then
 * the relocated address. 1, or 0 when the file relocates no such word. */
int em_gs_texture_word(const EmGsTexture *gs, uint32_t address, uint32_t *value);

#endif
