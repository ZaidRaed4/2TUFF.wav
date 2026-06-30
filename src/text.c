#include <pspgu.h>
#include <string.h>

#include "text.h"
#include "gfx.h"
#include "theme.h"
#include "font_plex.h"

typedef struct {
    Texture *tex;
    int cw, ch;
    int aw;
    const unsigned char *adv;
} GFont;

static GFont F[2][2];
static int   g_face = FACE_PLEX;

typedef struct { float u, v; unsigned int color; float x, y, z; } GVtx;

static Texture *build_atlas(const unsigned char *cov, int w, int h)
{
    Texture *t = tex_create(w, h);
    int y, x;
    if (!t) return NULL;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned int a = cov[y * w + x];

            t->data[y * t->tw + x] = (a << 24) | 0x00FFFFFFu;
        }
    tex_upload(t);
    return t;
}

static void set_font(int face, int sz, const unsigned char *cov, int w, int h,
                     int cw, int ch, const unsigned char *adv)
{
    F[face][sz].tex = build_atlas(cov, w, h);
    F[face][sz].cw  = cw;
    F[face][sz].ch  = ch;
    F[face][sz].aw  = w;
    F[face][sz].adv = adv;
}

void text_init(void)
{

    set_font(FACE_PLEX,  F_SM, plex_sm, PLEX_SM_W, PLEX_SM_H, PLEX_SM_CW, PLEX_SM_CH, NULL);
    set_font(FACE_PLEX,  F_LG, plex_lg, PLEX_LG_W, PLEX_LG_H, PLEX_LG_CW, PLEX_LG_CH, NULL);

    set_font(FACE_PIXEL, F_SM, pixel_sm, PIX_SM_W, PIX_SM_H, PIX_SM_CW, PIX_SM_CH, pixel_sm_adv);
    set_font(FACE_PIXEL, F_LG, pixel_lg, PIX_LG_W, PIX_LG_H, PIX_LG_CW, PIX_LG_CH, pixel_lg_adv);
}

void text_shutdown(void)
{
    int face, sz;
    for (face = 0; face < 2; face++)
        for (sz = 0; sz < 2; sz++) {
            tex_free(F[face][sz].tex); F[face][sz].tex = NULL;
        }
}

void text_set_face(FontFace face)
{
    if (face == FACE_PLEX || face == FACE_PIXEL) g_face = face;
}
FontFace text_current_face(void) { return (FontFace)g_face; }

static int glyph_adv(const GFont *fn, unsigned int c)
{
    if (c >= 128) c = '?';
    return fn->adv ? fn->adv[c] : fn->cw;
}

int font_cw(Font f) { return F[FACE_PLEX][f].cw; }
int font_ch(Font f) { return F[FACE_PLEX][f].ch; }

int text_w(Font f, const char *s)
{
    const GFont *fn = &F[g_face][f];
    const unsigned char *p = (const unsigned char *)s;
    int w = 0;
    if (!fn->adv) return (int)strlen(s) * fn->cw;
    while (*p) w += glyph_adv(fn, *p++);
    return w;
}

void text_put(Font f, int x, int y, unsigned int col, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    GFont *fn = &F[g_face][f];
    int n, i, k = 0;
    float pen = (float)x;
    GVtx *v;

    if (!fn->tex || !s) return;
    n = (int)strlen(s);
    if (n <= 0) return;

    v = (GVtx *)sceGuGetMemory(2 * n * sizeof(GVtx));
    for (i = 0; i < n; i++) {
        unsigned int c = p[i];
        int gx, gy;
        if (c >= 128) c = '?';
        gx = (c % PLEX_COLS) * fn->cw;
        gy = (c / PLEX_COLS) * fn->ch;
        v[k].u = (float)gx;          v[k].v = (float)gy;
        v[k].color = col;            v[k].x = pen;            v[k].y = (float)y;       v[k].z = 0;
        v[k + 1].u = (float)(gx + fn->cw); v[k + 1].v = (float)(gy + fn->ch);
        v[k + 1].color = col;        v[k + 1].x = pen + fn->cw; v[k + 1].y = (float)(y + fn->ch); v[k + 1].z = 0;
        k += 2;
        pen += (float)glyph_adv(fn, c);
    }

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, fn->tex->tw, fn->tex->th, fn->tex->tw, fn->tex->data);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuDrawArray(GU_SPRITES,
                   GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                   2 * n, 0, v);
    sceGuDisable(GU_TEXTURE_2D);
}

void text_put_scaled(Font f, int x, int y, unsigned int col, const char *s, float scale)
{
    const unsigned char *p = (const unsigned char *)s;
    GFont *fn = &F[g_face][f];
    int n, i, k = 0;
    float pen = (float)x;
    float dw = fn->cw * scale;
    float dh = fn->ch * scale;
    GVtx *v;

    if (!fn->tex || !s) return;
    n = (int)strlen(s);
    if (n <= 0) return;

    v = (GVtx *)sceGuGetMemory(2 * n * sizeof(GVtx));
    for (i = 0; i < n; i++) {
        unsigned int c = p[i];
        int gx, gy;
        if (c >= 128) c = '?';
        gx = (c % PLEX_COLS) * fn->cw;
        gy = (c / PLEX_COLS) * fn->ch;
        v[k].u = (float)gx;          v[k].v = (float)gy;
        v[k].color = col;            v[k].x = pen;            v[k].y = (float)y;        v[k].z = 0;
        v[k + 1].u = (float)(gx + fn->cw); v[k + 1].v = (float)(gy + fn->ch);
        v[k + 1].color = col;        v[k + 1].x = pen + dw;   v[k + 1].y = (float)y + dh; v[k + 1].z = 0;
        k += 2;
        pen += (float)glyph_adv(fn, c) * scale;
    }

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, fn->tex->tw, fn->tex->th, fn->tex->tw, fn->tex->data);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuDrawArray(GU_SPRITES,
                   GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                   2 * n, 0, v);
    sceGuDisable(GU_TEXTURE_2D);
}

void text_put_center(Font f, int cx, int y, unsigned int col, const char *s)
{
    text_put(f, cx - text_w(f, s) / 2, y, col, s);
}

void text_put_right(Font f, int x_right, int y, unsigned int col, const char *s)
{
    text_put(f, x_right - text_w(f, s), y, col, s);
}

void text_put_clip(Font f, int x, int y, unsigned int col, const char *s, int maxw)
{
    const GFont *fn = &F[g_face][f];
    int n = (int)strlen(s);
    char buf[192];
    int i, w, dotw, keep;

    if (maxw <= 0) return;
    if (text_w(f, s) <= maxw) { text_put(f, x, y, col, s); return; }

    dotw = 2 * glyph_adv(fn, '.');
    w = 0; keep = 0;
    for (i = 0; i < n && keep < (int)sizeof(buf) - 3; i++) {
        int a = glyph_adv(fn, (unsigned char)s[i]);
        if (w + a > maxw - dotw) break;
        w += a;
        buf[keep++] = s[i];
    }
    buf[keep] = '.';
    buf[keep + 1] = '.';
    buf[keep + 2] = '\0';
    text_put(f, x, y, col, buf);
}
