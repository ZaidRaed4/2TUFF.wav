#include <math.h>
#include <string.h>

#include "viz.h"
#include "gfx.h"
#include "theme.h"
#include "text.h"
#include "audio.h"

#if VIZ_ENABLED

#define VZ_PITCH   12
#define VZ_DOT      6
#define VZ_MAXC    44
#define VZ_MAXR    24
#define VZ_LUT    128

#define TAU        6.2831853f

#define VZ_FLOOR    0.04f
#define VZ_AMBIENT  0.46f
#define VZ_CENTER   0.26f
#define VZ_RADWT    0.65f
#define VZ_BASSWT   0.70f
#define VZ_LIFT     0.12f
#define VZ_GAIN     1.7f
#define VZ_RINGS    2.4f
#define VZ_DRIFT    1.5f
#define VZ_SURGE    7.0f

#define BZ_PITCH    10
#define BZ_MAXC     50
#define BZ_MAXR     20
#define N_BLOBS      5
#define BZ_TLOW     0.62f
#define BZ_THIGH    1.06f
#define BZ_EPS      0.0009f
#define BZ_MINDOT   2.0f
#define BZ_BASSAMP  0.34f
#define BZ_LVLAMP   0.12f
#define BZ_DRIFT    1.35f
#define BZ_LVLSPEED 1.6f

typedef struct { float ax, ay, wx, wy, px, py, r; } Blob;
static const Blob BLOBS[N_BLOBS] = {
    { 0.42f, 0.34f, 0.55f, 0.73f, 0.0f, 1.7f, 0.24f },
    { 0.38f, 0.40f, 0.71f, 0.49f, 2.1f, 0.6f, 0.21f },
    { 0.46f, 0.30f, 0.43f, 0.67f, 4.0f, 3.2f, 0.26f },
    { 0.30f, 0.42f, 0.63f, 0.57f, 5.3f, 1.1f, 0.20f },
    { 0.40f, 0.36f, 0.51f, 0.77f, 1.2f, 4.5f, 0.23f },
};

#define VA_MAXC     96
#define VA_MAXR     24
#define VA_SCALE    0.7f
#define VA_TOPBAND  2.5f
#define VA_RISE     2.2f
#define VA_IDLE     0.10f
#define VA_IDLEAMP  0.07f
#define VA_ATTACK   0.55f
#define VA_RELEASE  0.10f

static const char VA_RAMP[] =
    " .'`^\",:;Il!i><~+_-?][}{1)(|/tfjrxnuvczXYUJCLQ0OZmwqpdbkho*#MW&8%@";

static float g_col_h[VA_MAXC];

static int   g_cols = 0, g_rows = 0;
static int   g_lw = -1, g_lh = -1;
static float g_dist01[VZ_MAXR][VZ_MAXC];
static unsigned char g_di[VZ_MAXR][VZ_MAXC];

static float g_t = 0.0f;
static float g_phase = 0.0f;
static float g_bt = 0.0f;
static float g_at = 0.0f;

static void build_layout(int cols, int rows)
{
    int c, r;
    float cx = (cols - 1) * 0.5f;
    float cy = (rows - 1) * 0.5f;
    float maxd = sqrtf(cx * cx + cy * cy);
    if (maxd < 1.0f) maxd = 1.0f;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            float dx = c - cx, dy = r - cy;
            float d  = sqrtf(dx * dx + dy * dy) / maxd;
            int   k  = (int)(d * (VZ_LUT - 1) + 0.5f);
            if (k < 0) k = 0; else if (k > VZ_LUT - 1) k = VZ_LUT - 1;
            g_dist01[r][c] = d;
            g_di[r][c]     = (unsigned char)k;
        }
    }
    g_cols = cols;
    g_rows = rows;
}

static unsigned int lerp_col(unsigned int a, unsigned int b, float t)
{
    int ar = a & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF;
    int br = b & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF;
    int rr = ar + (int)((br - ar) * t);
    int gg = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return 0xFF000000u | ((unsigned int)bl << 16) |
           ((unsigned int)gg << 8) | (unsigned int)rr;
}

static unsigned int with_alpha(unsigned int col, float f)
{
    unsigned int al = (unsigned int)(((col >> 24) & 0xFFu) * f);
    return (col & 0x00FFFFFFu) | (al << 24);
}

static void render_field(int x, int y, int w, int h,
                         float level, float bass, float alpha)
{
    int cols = w / VZ_PITCH, rows = h / VZ_PITCH;
    int c, r, x0, y0;
    float colw[VZ_MAXC], roww[VZ_MAXR], radlut[VZ_LUT];

    if (cols > VZ_MAXC) cols = VZ_MAXC;
    if (rows > VZ_MAXR) rows = VZ_MAXR;
    if (cols < 1 || rows < 1) return;

    if (cols != g_cols || rows != g_rows || w != g_lw || h != g_lh) {
        build_layout(cols, rows);
        g_lw = w; g_lh = h;
    }

    for (c = 0; c < cols; c++) colw[c] = sinf(c * 0.45f + g_t * 0.9f);
    for (r = 0; r < rows; r++) roww[r] = sinf(r * 0.55f - g_t * 0.7f);
    for (c = 0; c < VZ_LUT; c++)
        radlut[c] = sinf(((float)c / (VZ_LUT - 1)) * VZ_RINGS * TAU - g_phase);

    x0 = x + (w - cols * VZ_PITCH) / 2 + (VZ_PITCH - VZ_DOT) / 2;
    y0 = y + (h - rows * VZ_PITCH) / 2 + (VZ_PITCH - VZ_DOT) / 2;

    gfx_quad((float)x, (float)y, (float)w, (float)h,
             with_alpha(TH.cover_bg, alpha));

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            float plasma = (colw[c] + roww[r]) * 0.25f + 0.5f;
            float ripple = radlut[g_di[r][c]] * 0.5f + 0.5f;
            float glow   = 1.0f - g_dist01[r][c];
            float raw = VZ_AMBIENT * plasma
                      + VZ_CENTER  * glow * plasma
                      + VZ_RADWT   * level * ripple
                      + VZ_BASSWT  * bass  * glow * glow;
            float b = (raw - VZ_LIFT) * VZ_GAIN;
            if (b <= 0.0f) continue;
            if (b > 1.0f) b = 1.0f;
            b = b * b * (3.0f - 2.0f * b);   /* smoothstep */
            b = VZ_FLOOR + (1.0f - VZ_FLOOR) * b;
            gfx_quad((float)(x0 + c * VZ_PITCH), (float)(y0 + r * VZ_PITCH),
                     (float)VZ_DOT, (float)VZ_DOT,
                     with_alpha(lerp_col(TH.cover_bg, TH.cover_ink, b), alpha));
        }
    }
}

static void render_blobs(int x, int y, int w, int h,
                         float level, float bass, float alpha)
{
    int cols = w / BZ_PITCH, rows = h / BZ_PITCH;
    int c, r, i, x0, y0;
    float aspect = (float)w / (float)h;
    float colx[BZ_MAXC], rowy[BZ_MAXR];
    float bx[N_BLOBS], by[N_BLOBS], br2[N_BLOBS];
    float swell;
    unsigned int dot;

    if (cols > BZ_MAXC) cols = BZ_MAXC;
    if (rows > BZ_MAXR) rows = BZ_MAXR;
    if (cols < 1 || rows < 1) return;

    swell = 1.0f + BZ_BASSAMP * bass + BZ_LVLAMP * level;
    for (i = 0; i < N_BLOBS; i++) {
        float rr = BLOBS[i].r * swell;
        bx[i]  = (0.5f + BLOBS[i].ax * sinf(g_bt * BLOBS[i].wx + BLOBS[i].px))
                 * aspect;
        by[i]  =  0.5f + BLOBS[i].ay * sinf(g_bt * BLOBS[i].wy + BLOBS[i].py);
        br2[i] = rr * rr;
    }

    for (c = 0; c < cols; c++) colx[c] = ((c + 0.5f) / cols) * aspect;
    for (r = 0; r < rows; r++) rowy[r] =  (r + 0.5f) / rows;

    x0 = x + (w - cols * BZ_PITCH) / 2;
    y0 = y + (h - rows * BZ_PITCH) / 2;

    gfx_quad((float)x, (float)y, (float)w, (float)h,
             with_alpha(TH.cover_bg, alpha));
    dot = with_alpha(TH.cover_ink, alpha);

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            float field = 0.0f, cov, sz, off;
            for (i = 0; i < N_BLOBS; i++) {
                float dx = colx[c] - bx[i];
                float dy = rowy[r] - by[i];
                field += br2[i] / (dx * dx + dy * dy + BZ_EPS);
            }
            cov = (field - BZ_TLOW) / (BZ_THIGH - BZ_TLOW);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            cov = cov * cov * (3.0f - 2.0f * cov);   /* smoothstep */
            sz  = BZ_MINDOT + cov * (BZ_PITCH - BZ_MINDOT);
            off = (BZ_PITCH - sz) * 0.5f;
            gfx_quad((float)x0 + c * BZ_PITCH + off,
                     (float)y0 + r * BZ_PITCH + off, sz, sz, dot);
        }
    }
}

static float hash01(float a, float b)
{
    float s = sinf(a * 12.9898f + b * 78.233f) * 43758.5453f;
    return s - floorf(s);
}

static void render_ascii(int x, int y, int w, int h,
                         float level, float bass, float alpha)
{
    int cw = (int)(font_cw(F_SM) * VA_SCALE + 0.5f);
    int ch = (int)(font_ch(F_SM) * VA_SCALE + 0.5f);
    int cols, rows, c, r, x0, y0, scroll;
    int ramp_max = (int)sizeof(VA_RAMP) - 2;   /* -2: drop the NUL, convert size to last index */
    float bands[AUDIO_BANDS];
    int   nb;
    unsigned int ink;
    FontFace saved;
    char row[VA_MAXC + 1];

    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    cols = w / cw; rows = h / ch;
    if (cols > VA_MAXC) cols = VA_MAXC;
    if (rows > VA_MAXR) rows = VA_MAXR;
    if (cols < 1 || rows < 1) return;
    (void)bass;

    nb = audio_bands(bands, AUDIO_BANDS);
    if (nb < 1) { bands[0] = 0.0f; nb = 1; }

    for (c = 0; c < cols; c++) {
        float fb = (cols > 1) ? (float)c / (cols - 1) * (nb - 1) : 0.0f;
        int   bi = (int)fb;
        float ft = fb - bi;
        float bv = (bi + 1 < nb) ? bands[bi] * (1.0f - ft) + bands[bi + 1] * ft
                                 : bands[bi];
        float idle = VA_IDLE + VA_IDLEAMP * (0.5f + 0.5f * sinf(g_at * 0.9f
                                                               + c * 0.35f));
        float tgt = bv > idle ? bv : idle;
        float cur = g_col_h[c];
        cur += ((tgt > cur) ? VA_ATTACK : VA_RELEASE) * (tgt - cur);
        g_col_h[c] = cur;
    }

    scroll = (int)(g_at * VA_RISE);
    x0 = x + (w - cols * cw) / 2;
    y0 = y + (h - rows * ch) / 2;

    gfx_quad((float)x, (float)y, (float)w, (float)h,
             with_alpha(TH.cover_bg, alpha));

    saved = text_current_face();
    text_set_face(FACE_PLEX);
    ink = with_alpha(TH.cover_ink, alpha);

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            float surf = g_col_h[c] * rows;
            float from_bottom = (float)(rows - 1 - r);
            float grain = 0.6f * hash01((float)c, (float)(r + scroll))
                        + 0.4f * (0.5f + 0.5f * sinf(g_at * 2.3f
                                                     + c * 0.5f - r * 0.4f));
            int idx;
            if (from_bottom <= surf) {
                float depth = surf - from_bottom;
                float base  = 0.28f + 0.72f * (from_bottom / (rows - 1 > 0
                                                              ? rows - 1 : 1));

                float topfill = (surf - (rows - VA_TOPBAND)) / VA_TOPBAND;
                float inten;
                if (topfill < 0.0f) topfill = 0.0f;
                else if (topfill > 1.0f) topfill = 1.0f;
                base += 0.30f * (depth < 1.0f ? depth : 1.0f);

                inten = base * (0.42f + 0.58f * grain);
                if (depth < 1.0f) {
                    float dim = 0.3f + 0.7f * grain;
                    inten *= dim + (1.0f - dim) * topfill;
                }
                idx = (int)(inten * ramp_max + 0.5f);
                if (idx < 0) idx = 0; else if (idx > ramp_max) idx = ramp_max;
            } else {
                idx = (grain > 0.93f) ? 1 : 0;
            }
            row[c] = VA_RAMP[idx];
        }
        row[cols] = '\0';
        text_put_scaled(F_SM, x0, y0 + r * ch, ink, row, VA_SCALE);
    }

    text_set_face(saved);
}

void viz_render(int x, int y, int w, int h, float dt,
                float level, float bass, float alpha, int style)
{
    if (level < 0.0f) level = 0.0f; else if (level > 1.0f) level = 1.0f;
    if (bass  < 0.0f) bass  = 0.0f; else if (bass  > 1.0f) bass  = 1.0f;
    if (alpha < 0.0f) alpha = 0.0f; else if (alpha > 1.0f) alpha = 1.0f;
    if (alpha <= 0.0f) return;

    g_t     += dt;
    g_phase += (VZ_DRIFT + VZ_SURGE * bass) * dt;
    g_bt    += (BZ_DRIFT + BZ_LVLSPEED * level) * dt;
    g_at    += dt;
    if (g_t     > 100000.0f) g_t     -= 100000.0f;
    if (g_phase > 100000.0f) g_phase -= 100000.0f;
    if (g_bt    > 100000.0f) g_bt    -= 100000.0f;
    if (g_at    > 100000.0f) g_at    -= 100000.0f;

    if      (style == VIZ_STYLE_BLOBS) render_blobs(x, y, w, h, level, bass, alpha);
    else if (style == VIZ_STYLE_ASCII) render_ascii(x, y, w, h, level, bass, alpha);
    else                               render_field(x, y, w, h, level, bass, alpha);
}

#endif
