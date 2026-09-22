#include "game/em_snow.h"
#include "game/em_effect_color.h"

#include <stdio.h>
#include <string.h>

static float snow_add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

static float snow_mul(float a, float b)
{
    return em_effect_float32((double)a * b);
}

/* The captured original EE DIV.S results round to nearest for these inputs.
 * Truncating the seed quotient changes 49 of 108 AREA11 tile seed mantissas,
 * and therefore changes the VU random sequence. VU division has a different
 * verified rounding path; do not share this operation with that generator. */
static float snow_div(float a, float b)
{
    return (float)((double)a / b);
}

/* 00102B08 calls the original 001029E8 sine polynomial at pi/2-|angle|,
 * then obtains the other component with sqrt(1-s*s). Calling host sin/cos
 * for the matrix changes the original basis even with the same input angle.
 * Coefficients and lane accumulation order are from the original helper. */
static void snow_rotation(float angle, float *sine, float *cosine)
{
    static const float coefficient[4] = {
        0x1.5d3828p-19f, -0x1.9f643ep-13f,
        0x1.110e7cp-7f, -0x1.555548p-3f
    };
    float argument = snow_add(0x1.921fb6p+0f, -fabsf(angle));
    float square = snow_mul(argument, argument);
    float term[4];
    for (unsigned i = 0; i < 4; ++i)
        term[i] = snow_mul(coefficient[i], argument);
    for (unsigned lanes = 4; lanes > 0; --lanes)
        for (unsigned i = 0; i < lanes; ++i)
            term[i] = snow_mul(term[i], square);
    float value = argument;
    for (unsigned i = 4; i > 0; --i)
        value = snow_add(value, term[i-1]);
    *cosine = value;
    *sine = em_effect_float32(sqrt((double)snow_add(1.0f, -snow_mul(value, value))));
    if (angle < 0.0f) *sine = -*sine;
}

int em_snow_config_load(EmSnowConfig *config, const char *path)
{
    uint32_t header[5];
    EmSnowConfig loaded;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int valid = fread(header, sizeof header, 1, file) == 1 &&
                memcmp(header, "EMSN", 4) == 0 && header[1] == 1 &&
                header[2] == 9 && header[3] == 80 && header[4] == 6 &&
                fread(&loaded, sizeof loaded, 1, file) == 1 &&
                fgetc(file) == EOF;
    fclose(file);
    if (!valid) return 0;
    uint32_t count, flags, kind;
    memcpy(&count, &loaded.descriptor[8][0], sizeof count);
    memcpy(&flags, &loaded.descriptor[8][2], sizeof flags);
    memcpy(&kind, &loaded.descriptor[8][3], sizeof kind);
    if (count != 20 || flags != 30 || kind != 2) return 0;
    for (unsigned row = 0; row < 6; ++row)
        for (unsigned field = 0; field < 12; ++field)
            if (!isfinite(loaded.rows[row][field])) return 0;
    for (unsigned i = 0; i < 80; ++i)
        if (!isfinite(loaded.lookup[i])) return 0;
    *config = loaded;
    return 1;
}

void em_snow_tiles(EmWeather *weather, const EmSnowConfig *config,
                   float strength, const float eye[3],
                   EmSnowTile tiles[EM_SNOW_TILE_COUNT])
{
    float cell[3];
    for (unsigned axis = 0; axis < 3; ++axis) {
        int integral = (int)eye[axis];
        cell[axis] = snow_add((float)((integral + 100000) % 200),
                              snow_add(eye[axis], -(float)integral));
    }
    uint32_t seed = weather->seed;
    unsigned index = 0;
    float drift_step = snow_mul(0.004f, strength);
    for (unsigned row = 0; row < 6; ++row) {
        const float *data = config->rows[row];
        float wave = sinf(snow_mul(6.2831855f, weather->drift[row]));
        float angle = snow_add(data[0], snow_mul(0.5f, snow_mul(data[0], wave)));
        angle = snow_div(snow_mul(3.1415927f, angle), 180.0f);
        float sine, cosine;
        snow_rotation(angle, &sine, &cosine);
        for (unsigned z = 0; z < 6; ++z) {
            for (unsigned y = 0; y < 3; ++y) {
                EmSnowTile *tile = &tiles[index++];
                memcpy(tile->descriptor, config->descriptor, sizeof tile->descriptor);
                float color_scale = snow_mul(1.3f, strength);
                for (unsigned component = 0; component < 4; ++component) {
                    tile->descriptor[2][component] = tile->descriptor[3][component] =
                        snow_mul(data[8 + component], color_scale);
                    tile->descriptor[4][component] = tile->descriptor[5][component] = data[4 + component];
                }
                float relative[3] = {
                    snow_add(snow_mul(200.0f, snow_div((float)row, 6.0f)), -cell[0]),
                    snow_add(snow_mul(100.0f, snow_div((float)y, 3.0f)), -cell[1]),
                    snow_add(snow_mul(200.0f, snow_div((float)z, 6.0f)), -cell[2])
                };
                while (relative[0] < 0) relative[0] = snow_add(relative[0], 200.0f);
                while (relative[1] < 0) relative[1] = snow_add(relative[1], 100.0f);
                while (relative[2] < 0) relative[2] = snow_add(relative[2], 200.0f);
                relative[0] = snow_add(relative[0], -100.0f);
                relative[1] = snow_add(relative[1], -50.0f);
                relative[2] = snow_add(relative[2], -200.0f);
                memset(tile->matrix, 0, sizeof tile->matrix);
                tile->matrix[0] = tile->matrix[15] = 1.0f;
                tile->matrix[5] = tile->matrix[10] = cosine;
                tile->matrix[6] = sine;
                tile->matrix[9] = -sine;
                tile->matrix[12] = snow_add(relative[0], eye[0]);
                tile->matrix[13] = snow_add(snow_add(snow_mul(cosine, relative[1]),
                                                   snow_mul(-sine, relative[2])), eye[1]);
                tile->matrix[14] = snow_add(snow_add(snow_mul(sine, relative[1]),
                                                   snow_mul(cosine, relative[2])), eye[2]);
                float fraction = snow_div((float)(seed >> 16), 65535.0f);
                seed = seed * 37U + 11U;
                /* CFAE0 writes f12/f14/f15/f13 to VU59. The former C
                 * mislabeled these and passed phase in the wrong slot. */
                tile->params[0] = weather->phase[row];
                tile->params[1] = 1.0f;
                tile->params[2] = 0.000001f;
                tile->params[3] = snow_add(fraction, 0.0001f);
            }
        }
        weather->drift[row] = snow_add(weather->drift[row], drift_step);
        weather->phase[row] = snow_add(weather->phase[row], snow_mul(1.5f, snow_mul(data[2], strength)));
        if (weather->phase[row] > 2.0f)
            weather->phase[row] = snow_add(weather->phase[row], -1.0f);
    }
}
