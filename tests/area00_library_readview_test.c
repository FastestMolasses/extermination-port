/* Exercise the unchanged production retained-library loader and provider. */
#include "game/em_area11_roger.c"
#include <assert.h>

int main(int argc, char **argv)
{
    assert(argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *disc = malloc((size_t)length);
    assert(disc && fread(disc, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    const unsigned ids[] = {0x4D, 0x4F, 0x58, 0x59, 0x72};
    const uint32_t *bank = em_area11_roger_library_word();
    assert(bank && R.regions == 16);
    unsigned added = (unsigned)atoi(argv[2]), checked = 0;
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; ++i) {
        uint32_t offset = rd32(disc + 4 + 4 * ids[i]) & ~3u;
        assert(offset + 16 < (uint32_t)length);
        uint32_t bytes = rd32(disc + offset + 12) + 0x50u * rd32(disc + offset + 8);
        assert(offset + bytes <= (uint32_t)length);
        uint32_t handle = 0, alias = 0, available = 0;
        assert(em_area11_roger_001C6120(*bank, ids[i], &handle) == 0);
        assert(em_area11_roger_001C6120(*bank, ids[i] | 0x8000, &alias) == 0);
        assert(handle == *bank + offset && alias == handle);
        const uint8_t *view = em_area11_roger_library_rest(handle, &available);
        if (!added && (ids[i] == 0x4F || ids[i] == 0x59)) {
            assert(!view && available == 0);
            continue;
        }
        assert(view && available >= bytes && memcmp(view, disc + offset, bytes) == 0);
        const uint8_t *last = em_area11_roger_library_rest(handle + bytes - 1, &available);
        assert(last && *last == disc[offset + bytes - 1]);
        checked++;
    }
    assert(checked == (added ? 5u : 3u));
    free(disc);
    free(R.file);
    puts("PASS production library model reads and aliases");
    return 0;
}
