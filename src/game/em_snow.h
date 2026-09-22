#ifndef EM_SNOW_H
#define EM_SNOW_H

#include "game/em_weather.h"

enum { EM_SNOW_TILE_COUNT = 108 };

typedef struct EmSnowConfig {
    float descriptor[9][4];
    float lookup[80];
    float rows[6][12];
} EmSnowConfig;

typedef struct EmSnowTile {
    float descriptor[9][4];
    float params[4];
    float matrix[16];
} EmSnowTile;

/* Loads locally exported original tables; no guessed fallback geometry. */
int em_snow_config_load(EmSnowConfig *config, const char *path);

/* 001E67C0's AREA11 tile emission. Advances the renderer-owned phase fields
 * once per row. Each tile carries the actual VU50..5D submission parameters. */
void em_snow_tiles(EmWeather *weather, const EmSnowConfig *config,
                   float strength, const float eye[3],
                   EmSnowTile tiles[EM_SNOW_TILE_COUNT]);

#endif
