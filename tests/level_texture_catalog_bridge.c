/* Exercise the production EMOT parser without selecting a live later area. */
#include "game/em_world_textures_live.c"

static Texture checked[EM_GFX_OBJECT_TEX_MAX];
static uint8_t *checked_storage;
static unsigned checked_count;

void level_texture_catalog_free(void)
{
    free(checked_storage);
    checked_storage = NULL;
    checked_count = 0;
    memset(checked, 0, sizeof checked);
}

int level_texture_catalog_load(const char *path)
{
    level_texture_catalog_free();
    S.failed = 0;
    int rc = catalog(path, &checked_storage, checked, &checked_count);
    if (rc) level_texture_catalog_free();
    return rc;
}

unsigned level_texture_catalog_count(void) { return checked_count; }
const Texture *level_texture_catalog_entry(unsigned index)
{ return index < checked_count ? &checked[index] : NULL; }
