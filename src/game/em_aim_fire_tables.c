#include "game/em_aim_fire_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fifth span, D_002754E8..D_002754F3 (.sdata), is the R2 aim camera's
 * eye offset (00198440 reads its three floats; em_camera_live serves them). */
#define SPANS 5u
static const uint32_t bases[] = {EM_AIM_FIRE_TABLE_BASE, 0x2533D0, 0x253720, 0x266930, 0x2754E8};
static const uint32_t sizes[] = {EM_AIM_FIRE_TABLE_SIZE, 0xC0, 0x20, 0x1B0, 0xC};
static uint8_t tables[EM_AIM_FIRE_TABLE_SIZE + 0xC0 + 0x20 + 0x1B0 + 0xC];
static int loaded;
static uint32_t word(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }

int em_aim_fire_tables_load(void)
{
    if (loaded) return 0;
    const char *path=getenv("EM_AIM_FIRE_TABLES");
    if (!path || !*path) path=EM_AIM_FIRE_TABLE_PATH;
    FILE *f=fopen(path,"rb");
    uint8_t header[8 + 8 * SPANS], pending[sizeof tables];
    if (!f) return -1;
    int valid=fread(header,1,sizeof header,f)==sizeof header &&
        memcmp(header,"EMAF",4)==0 && word(header+4)==4;
    for (unsigned i=0;i<SPANS && valid;++i)
        valid=word(header+8+i*8)==bases[i] && word(header+12+i*8)==sizes[i];
    valid=valid && fread(pending,1,sizeof pending,f)==sizeof pending && fgetc(f)==EOF && !ferror(f);
    fclose(f);
    if (!valid) return -1;
    /* Both six-row animation tables must name nine readable halfwords. */
    for (unsigned bank=0;bank<2;++bank)
        for (unsigned i=0;i<6;++i) {
            unsigned offset=(bank ? 0x248C50u : 0x248B70u)-EM_AIM_FIRE_TABLE_BASE+4*i;
            uint32_t row=word(pending+offset);
            if (row<EM_AIM_FIRE_TABLE_BASE || row-EM_AIM_FIRE_TABLE_BASE>EM_AIM_FIRE_TABLE_SIZE-18)
                return -1;
        }
    memcpy(tables,pending,sizeof tables);
    loaded=1;
    return 0;
}

const uint8_t *em_aim_fire_tables_bytes(uint32_t address, uint32_t size)
{
    if (!loaded) return NULL;
    unsigned offset=0;
    for (unsigned i=0;i<SPANS;offset+=sizes[i++])
        if (address>=bases[i] && size<=sizes[i] && address-bases[i]<=sizes[i]-size)
            return tables+offset+address-bases[i];
    return NULL;
}

int em_aim_fire_tables_contains(uint32_t address,uint32_t size)
{
    if (!size) return 0;
    for (unsigned i=0;i<SPANS;++i)
        if ((uint64_t)address+size>bases[i] && (uint64_t)address<(uint64_t)bases[i]+sizes[i]) return 1;
    return 0;
}
