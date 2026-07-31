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

/* --- THE ENGINE'S WORLD PROJECTION, derived exactly (2026-06-11) -------
 *
 * The engine builds one projection P per frame from the zoom scalar s
 * (render-ctx +0x2468, default 480.0; decomp func_001D2960 — FINDINGS.md
 * "CAMERA SYSTEM" section 3). Stored GS rows:
 *
 *     (0.8s, 0, 0, 0) (0, 0.5s, 0, 0) (2048, 2048, bz, 1) (0, 0, az, 0)
 *
 * with the Z-row literals bz = 0x3F664CB3, az = 0x49CCCCCC = 1677721.5.
 * After the VU1 per-vertex divide by w_clip = z_view:
 *
 *     x_gs = 0.8s*x/z + 2048      y_gs = 0.5s*y/z + 2048
 *     z_gs = bz + az/z            (reversed hyperbolic, 24-bit GS Z)
 *
 * X/Y scale: the GS raster window is 512x448 displayed at 4:3, drawn in
 * interlaced FIELD space — x spans [1792, 2304] (512 px, half-width 256)
 * and y spans [1936, 2160] (224 field lines = 448 display lines,
 * half-height 112); both centers are the P's 2048 offsets (the UI
 * module's decoded GS offsets 0x700/0x790 pin the window). So in
 * normalized device coordinates of the 4:3 frame:
 *
 *     ndc_x = (0.8s/256)*(x/z) = (s/320)*(x/z)   tan(hfov/2) = 320/s
 *     ndc_y = (0.5s/112)*(y/z) = (s/224)*(y/z)   tan(vfov/2) = 224/s
 *
 * At s = 480: hfov 67.38 deg, vfov 50.03 deg. The tan ratio is 10/7,
 * not the square-pixel 4/3 — the 0.8/0.5 anisotropy bakes the 512x448
 * -> 4:3 pixel aspect and leaves a real ~7% horizontal angular squeeze
 * (a sphere renders ~93% as wide as tall on the original display).
 * That is the engine's image; it is REPRODUCED here, not corrected.
 * Scope camera: the engine sets s = 224/tan(half-vfov) (func_001D25F0
 * family, 224.0/x) — the same 224 half-height constant.
 *
 * Z row: bit-exact decode of the literals. az = 1677721.5 =
 * 0.1*(2^24 - 1) exactly, and bz = f32(1 - az/16711680) = 0x3F664CB3
 * exactly — i.e. z_gs(0.1) = 2^24 - 1 (the 24-bit max) and
 * z_gs(16711680) = 1.0. The far value 16711680 (0xFF0000) is also the
 * literal the engine passes to its parameterized sibling builder
 * func_001D2D20(m, zoom, w, h, near, far) for the level kernel's variant
 * P. That builder is BYTE-MATCHED, and it independently CONFIRMS (audit)
 * the HALF-extent derivation used below: decomp
 * Extermination/src/func_001D2D20.c writes
 *   m[0] = focal / (0.5f * width);   m[5] = focal / (0.5f * height);
 * so the divisors really are half the raster window (0.5*640 = 320 and
 * 0.5*448 = 224), matching EM_GS_HALF_W / EM_GS_HALF_H exactly. Its z
 * row is the OpenGL-family [-1,1] pair m[10] = (far+near)/(far-near),
 * m[14] = -2*far*near/(far-near), m[11] = 1, m[15] = 0 — the port keeps
 * the same clip planes but a [0,1] depth row (see below); depth is an
 * encoding choice and the ORDERING is identical.
 * So the engine's clip planes are NEAR = 0.1, FAR = 16711680 — the
 * far plane is effectively infinite (fog and the cull planes bound the
 * scene long before; the old port values 0.5/500-800 clipped both ends
 * visibly). Verified live: state01 ee.bin has z_gs 39638.7 at z_view
 * 42.33 = bz + az/z to float precision (the GS Z IS divided by w).
 *
 * The native equivalent below keeps the exact clip planes and the
 * hyperbolic depth family ([0,1], near -> 0: the GS's reversed
 * orientation is a depth-ENCODING choice, invisible in the image —
 * ordering is identical). Truth check: with state01's camera this
 * matrix reproduces the engine K = P*V screen positions to < 0.01 px
 * (player root -> (320.0, 441.1) on the 640x480 PCSX2 frame — the
 * pixel between the player's boots in the savestate screenshot). */
#define EM_GS_HALF_W   320.0f       /* 256 px  / 0.8: tan(hfov/2)*s */
#define EM_GS_HALF_H   224.0f       /* 112 fld / 0.5: tan(vfov/2)*s */
#define EM_GS_NEAR     0.1f         /* derived: z_gs(near) = 2^24-1 */
#define EM_GS_FAR      16711680.0f  /* 0xFF0000: z_gs(far) = 1.0     */

/* The engine projection for zoom s, in the port's native conventions
 * (right-handed -z forward, y-up NDC, depth [0,1] — pairs with
 * em_mat4_lookat_gs below, which already remaps the engine's y-down /
 * +z-forward view space). Aspect is BAKED: this matrix is only correct
 * rendered into a 4:3 viewport (the gfx backends letterbox the window
 * to 4:3, like the original display). */
static inline void em_mat4_perspective_gs(float *m, float zoom_s)
{
    memset(m, 0, 16 * sizeof(float));
    m[0]  = zoom_s / EM_GS_HALF_W;
    m[5]  = zoom_s / EM_GS_HALF_H;
    m[10] = EM_GS_FAR / (EM_GS_NEAR - EM_GS_FAR);
    m[11] = -1.0f;
    m[14] = (EM_GS_NEAR * EM_GS_FAR) / (EM_GS_NEAR - EM_GS_FAR);
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
