#ifndef GFX_H
#define GFX_H

#include <stdint.h>

typedef struct {
    int w, h;
    int tw, th;
    uint32_t *data;
    int swizzled;
} Texture;

void  gfx_init(void);
void  gfx_shutdown(void);

void  gfx_begin_frame(void);
void  gfx_end_frame(void);
float gfx_dt(void);
unsigned int gfx_frame(void);

void gfx_quad(float x, float y, float w, float h, unsigned int color);
void gfx_quad_grad_v(float x, float y, float w, float h,
                     unsigned int top, unsigned int bot);
void gfx_rect_outline(float x, float y, float w, float h, float t,
                      unsigned int color);
void gfx_hline(float x, float y, float w, float thick, unsigned int color);

void gfx_clip(int x, int y, int w, int h);
void gfx_clip_reset(void);

void gfx_draw_background(float time);

Texture *tex_create(int w, int h);
void     tex_free(Texture *t);
void     tex_upload(Texture *t);

void gfx_blit(Texture *t, float x, float y, float w, float h,
              unsigned int tint);

void gfx_blit_nn(Texture *t, float x, float y, float w, float h,
                 unsigned int tint);

void gfx_blit_rot(Texture *t, float cx, float cy, float dw, float dh,
                  float angle, unsigned int tint);

#endif
