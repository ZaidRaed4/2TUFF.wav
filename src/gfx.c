#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspthreadman.h>

#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>

#include "gfx.h"
#include "theme.h"

#define BUF_W      512
#define LIST_WORDS (256 * 1024)

static unsigned int __attribute__((aligned(16))) g_list[LIST_WORDS];
static int g_fbp0, g_fbp1, g_zbp;
static unsigned int g_frame_ctr = 0;
static float g_dt = 1.0f / 60.0f;
static int64_t g_last_us = 0;

typedef struct { unsigned int color; float x, y, z; } CVtx;
typedef struct { float u, v; unsigned int color; float x, y, z; } TVtx;

static inline void *vrel(int off) { return (void *)(intptr_t)off; }

static int next_pow2(int x)
{
    int p = 1;
    while (p < x) p <<= 1;
    return p;
}

void gfx_init(void)
{
    g_fbp0 = 0;
    g_fbp1 = BUF_W * SCR_H * 4;
    g_zbp  = g_fbp1 + BUF_W * SCR_H * 4;

    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);

    sceGuDrawBuffer(GU_PSM_8888, vrel(g_fbp0), BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, vrel(g_fbp1), BUF_W);
    sceGuDepthBuffer(vrel(g_zbp), BUF_W);

    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);

    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);

    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_DITHER);

    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    g_last_us = sceKernelGetSystemTimeWide();
}

void gfx_shutdown(void)
{
    sceGuTerm();
}

void gfx_begin_frame(void)
{
    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(0xff000000);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void gfx_end_frame(void)
{
    int64_t now;

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();

    g_frame_ctr++;
    now = sceKernelGetSystemTimeWide();
    if (g_last_us) {
        g_dt = (float)(now - g_last_us) / 1000000.0f;
        if (g_dt > 0.1f) g_dt = 0.1f;
        if (g_dt < 0.0f) g_dt = 1.0f / 60.0f;
    }
    g_last_us = now;
}

float gfx_dt(void)           { return g_dt; }
unsigned int gfx_frame(void) { return g_frame_ctr; }

static void cquad(float x, float y, float w, float h,
                  unsigned int cTL, unsigned int cTR,
                  unsigned int cBL, unsigned int cBR)
{
    CVtx *v = (CVtx *)sceGuGetMemory(4 * sizeof(CVtx));
    v[0].color = cTL; v[0].x = x;     v[0].y = y;     v[0].z = 0;
    v[1].color = cBL; v[1].x = x;     v[1].y = y + h; v[1].z = 0;
    v[2].color = cTR; v[2].x = x + w; v[2].y = y;     v[2].z = 0;
    v[3].color = cBR; v[3].x = x + w; v[3].y = y + h; v[3].z = 0;

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_TRIANGLE_STRIP,
                   GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                   4, 0, v);
}

void gfx_quad(float x, float y, float w, float h, unsigned int c)
{
    cquad(x, y, w, h, c, c, c, c);
}

void gfx_quad_grad_v(float x, float y, float w, float h,
                     unsigned int top, unsigned int bot)
{
    cquad(x, y, w, h, top, top, bot, bot);
}

void gfx_hline(float x, float y, float w, float thick, unsigned int c)
{
    cquad(x, y, w, thick, c, c, c, c);
}

void gfx_rect_outline(float x, float y, float w, float h, float t,
                      unsigned int c)
{
    gfx_quad(x, y, w, t, c);
    gfx_quad(x, y + h - t, w, t, c);
    gfx_quad(x, y, t, h, c);
    gfx_quad(x + w - t, y, t, h, c);
}

void gfx_clip(int x, int y, int w, int h)
{
    sceGuScissor(x, y, w, h);
}

void gfx_clip_reset(void)
{
    sceGuScissor(0, 0, SCR_W, SCR_H);
}

void gfx_draw_background(float time)
{
    int i;
    (void)time;

    gfx_quad(0, 0, SCR_W, SCR_H, TH.bg);

    for (i = GRID; i < SCR_W; i += GRID)
        gfx_quad((float)i, 0, 1, SCR_H, TH.grid);
    for (i = GRID; i < SCR_H; i += GRID)
        gfx_quad(0, (float)i, SCR_W, 1, TH.grid);
}

Texture *tex_create(int w, int h)
{
    Texture *t = (Texture *)malloc(sizeof(Texture));
    if (!t) return NULL;
    t->w = w;
    t->h = h;
    t->tw = next_pow2(w);
    t->th = next_pow2(h);
    t->swizzled = 0;
    t->data = (uint32_t *)memalign(16, (size_t)t->tw * t->th * 4);
    if (!t->data) { free(t); return NULL; }
    memset(t->data, 0, (size_t)t->tw * t->th * 4);
    return t;
}

void tex_free(Texture *t)
{
    if (!t) return;
    free(t->data);
    free(t);
}

void tex_upload(Texture *t)
{
    if (t) sceKernelDcacheWritebackRange(t->data, (size_t)t->tw * t->th * 4);
}

static void tex_bind(Texture *t, int nearest)
{
    int flt = nearest ? GU_NEAREST : GU_LINEAR;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, t->swizzled);
    sceGuTexImage(0, t->tw, t->th, t->tw, t->data);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(flt, flt);
}

static void blit_xy(Texture *t, float x, float y, float w, float h,
                    unsigned int tint, int nearest)
{
    TVtx *v;
    if (!t) return;
    tex_bind(t, nearest);
    v = (TVtx *)sceGuGetMemory(4 * sizeof(TVtx));
    v[0].u = 0;       v[0].v = 0;       v[0].color = tint; v[0].x = x;     v[0].y = y;     v[0].z = 0;
    v[1].u = 0;       v[1].v = t->h;    v[1].color = tint; v[1].x = x;     v[1].y = y + h; v[1].z = 0;
    v[2].u = t->w;    v[2].v = 0;       v[2].color = tint; v[2].x = x + w; v[2].y = y;     v[2].z = 0;
    v[3].u = t->w;    v[3].v = t->h;    v[3].color = tint; v[3].x = x + w; v[3].y = y + h; v[3].z = 0;
    sceGuDrawArray(GU_TRIANGLE_STRIP,
                   GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                   4, 0, v);
    sceGuDisable(GU_TEXTURE_2D);
}

void gfx_blit(Texture *t, float x, float y, float w, float h, unsigned int tint)
{
    blit_xy(t, x, y, w, h, tint, 0);
}

void gfx_blit_nn(Texture *t, float x, float y, float w, float h, unsigned int tint)
{
    blit_xy(t, x, y, w, h, tint, 1);
}

void gfx_blit_rot(Texture *t, float cx, float cy, float dw, float dh,
                  float angle, unsigned int tint)
{
    float c, s, hw, hh, uw, vh;
    TVtx *v;
    if (!t) return;

    c = cosf(angle);
    s = sinf(angle);
    hw = dw * 0.5f;
    hh = dh * 0.5f;
    uw = (float)t->w;
    vh = (float)t->h;

    tex_bind(t, 1);
    v = (TVtx *)sceGuGetMemory(4 * sizeof(TVtx));

    v[0].u = 0;  v[0].v = 0;  v[0].color = tint; v[0].x = cx + (-hw) * c - (-hh) * s; v[0].y = cy + (-hw) * s + (-hh) * c; v[0].z = 0;
    v[1].u = 0;  v[1].v = vh; v[1].color = tint; v[1].x = cx + (-hw) * c - ( hh) * s; v[1].y = cy + (-hw) * s + ( hh) * c; v[1].z = 0;
    v[2].u = uw; v[2].v = 0;  v[2].color = tint; v[2].x = cx + ( hw) * c - (-hh) * s; v[2].y = cy + ( hw) * s + (-hh) * c; v[2].z = 0;
    v[3].u = uw; v[3].v = vh; v[3].color = tint; v[3].x = cx + ( hw) * c - ( hh) * s; v[3].y = cy + ( hw) * s + ( hh) * c; v[3].z = 0;
    sceGuDrawArray(GU_TRIANGLE_STRIP,
                   GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                   4, 0, v);
    sceGuDisable(GU_TEXTURE_2D);
}
