/* em_math.h — minimal column-major float[16] matrix helpers for the port.
 *
 * Conventions match the game engine's storage: a matrix is 4 consecutive
 * columns of 4 floats; m[12..14] is the translation. world = M * v with v a
 * column vector. Clip-space output targets Metal/D3D12/Vulkan-style depth in
 * [0, 1].
 */
#ifndef EM_MATH_H
#define EM_MATH_H

#include <math.h>
#include <string.h>

static inline void em_mat4_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* out = a * b (b applied first). out may not alias a or b. */
static inline void em_mat4_mul(float *out, const float *a, const float *b)
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0]
                           + a[1 * 4 + r] * b[c * 4 + 1]
                           + a[2 * 4 + r] * b[c * 4 + 2]
                           + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
}

/* Right-handed perspective, depth mapped to [0, 1] (Metal clip space). */
static inline void em_mat4_perspective(float *m, float fovy_rad, float aspect,
                                       float znear, float zfar)
{
    float f = 1.0f / tanf(fovy_rad * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = zfar / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (znear * zfar) / (znear - zfar);
}

/* Engine-faithful look-at builder (decomp func_00102CD0, the
 * sceVu0CameraMatrix-style builder the camera commit func_0018C0D0 feeds —
 * FINDINGS.md "CAMERA SYSTEM" section 3). Inputs are the engine's: a view
 * POSITION (the commit passes eye + 4*forward), a NORMALIZED forward, and
 * the engine's view-up — the global D_008105F0 = (0,-1,0).
 *
 * Handedness reconciliation (PS2 -> native): the engine's view space is
 * GS-shaped — +y DOWN (matching the GS raster, where screen y grows
 * downward) and +z INTO the screen (w_clip = z_view; a left-handed basis:
 * with s = fwd x up_gs and u = s x fwd, s x u = -fwd). The port's
 * em_mat4_perspective expects the opposite on both axes: y-up NDC
 * (Metal/D3D12/Vulkan) and right-handed -z forward. The remap negates all
 * three view-axis rows:
 *   - Y row:  GS y-down raster -> native y-up NDC;
 *   - Z row:  left-handed +z-forward -> right-handed -z-forward;
 *   - X row:  the two flips above alone would mirror the image; the X
 *     negation restores the screen chirality (level layout / texture
 *     text) that the port's renderer is validated against.
 * The result is numerically identical to em_mat4_lookat(pos, pos+fwd,
 * up=(0,1,0)); building it through the engine's convention keeps the
 * FINDINGS port contract explicit in code. */
static inline void em_mat4_lookat_gs(float *m, const float *pos,
                                     const float *fwd, const float *up_gs)
{
    /* Engine basis: s = fwd x up_gs (view +x), u = s x fwd (view +y,
     * world-down when up_gs = (0,-1,0)), view +z = fwd. */
    float sx = fwd[1] * up_gs[2] - fwd[2] * up_gs[1];
    float sy = fwd[2] * up_gs[0] - fwd[0] * up_gs[2];
    float sz = fwd[0] * up_gs[1] - fwd[1] * up_gs[0];
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;

    float ux = sy * fwd[2] - sz * fwd[1];
    float uy = sz * fwd[0] - sx * fwd[2];
    float uz = sx * fwd[1] - sy * fwd[0];

    /* Engine rows are (s, u, fwd) with translation -row.pos; the
     * reconciliation negates every view-axis row (translation included),
     * so the negated values are written directly. */
    m[0] = -sx;     m[4] = -sy;     m[8]  = -sz;
    m[1] = -ux;     m[5] = -uy;     m[9]  = -uz;
    m[2] = -fwd[0]; m[6] = -fwd[1]; m[10] = -fwd[2];
    m[3] = 0.0f;    m[7] = 0.0f;    m[11] = 0.0f;
    m[12] = sx * pos[0] + sy * pos[1] + sz * pos[2];
    m[13] = ux * pos[0] + uy * pos[1] + uz * pos[2];
    m[14] = fwd[0] * pos[0] + fwd[1] * pos[1] + fwd[2] * pos[2];
    m[15] = 1.0f;
}

/* Right-handed look-at view matrix. */
static inline void em_mat4_lookat(float *m, const float *eye,
                                  const float *center, const float *up)
{
    float fx = center[0] - eye[0], fy = center[1] - eye[1],
          fz = center[2] - eye[2];
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;

    /* s = f x up */
    float sx = fy * up[2] - fz * up[1];
    float sy = fz * up[0] - fx * up[2];
    float sz = fx * up[1] - fy * up[0];
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;

    /* u = s x f */
    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    m[0] = sx;  m[4] = sy;  m[8]  = sz;
    m[1] = ux;  m[5] = uy;  m[9]  = uz;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f;
    m[12] = -(sx * eye[0] + sy * eye[1] + sz * eye[2]);
    m[13] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
    m[14] =  (fx * eye[0] + fy * eye[1] + fz * eye[2]);
    m[15] = 1.0f;
}

#endif /* EM_MATH_H */
