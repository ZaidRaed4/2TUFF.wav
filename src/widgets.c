#include <string.h>

#include "widgets.h"
#include "gfx.h"
#include "theme.h"
#include "glyphs.h"

void ui_rule(int x, int y, int w)  { gfx_hline((float)x, (float)y, (float)w, 1, TH.rule); }
void ui_vrule(int x, int y, int h) { gfx_quad((float)x, (float)y, 1, (float)h, TH.rule); }

void ui_dotrule(int x, int y, int w)
{
    char buf[80];
    int cw = font_cw(F_SM);
    int n = (cw > 0) ? w / cw : 0;
    int i;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    for (i = 0; i < n; i++) buf[i] = GC_DOT;
    buf[n] = '\0';
    text_put(F_SM, x, y - font_ch(F_SM) / 2, TH.rule, buf);
}

void ui_statusbar(const char *left, const char *right)
{

    gfx_quad(0, 0, 4, HEADER_H - 1, TH.accent);
    if (left)  text_put(F_SM, PAD, STATUS_Y, TH.ink, left);
    if (right) text_put_right(F_SM, SCR_W - PAD, STATUS_Y, TH.ink_mute, right);
    ui_rule(0, HEADER_RULE_Y, SCR_W);
}

void ui_footer(const char *left, const char *right)
{
    ui_rule(0, FOOTER_TOP, SCR_W);
    if (left)  text_put(F_SM, PAD, FOOTER_TOP + 1, TH.ink_mute, left);
    if (right) text_put_right(F_SM, SCR_W - PAD, FOOTER_TOP + 1, TH.ink_mute, right);
}

void ui_meter(Font f, int x, int y, int cells, float frac,
              unsigned int on, unsigned int off)
{
    char buf[96];
    int cw = font_cw(f);
    int full, rem8, i, len, px, start;

    if (cells > (int)sizeof(buf) - 1) cells = (int)sizeof(buf) - 1;
    if (cells <= 0) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    {
        float exact = frac * cells;
        full = (int)exact;
        rem8 = (int)((exact - full) * 8.0f + 0.5f);
        if (rem8 >= 8) { full++; rem8 = 0; }
        if (full > cells) full = cells;
    }

    len = 0;
    for (i = 0; i < full; i++) buf[len++] = GC_FULL;
    buf[len] = '\0';
    text_put(f, x, y, on, buf);
    px = x + full * cw;

    start = full;
    if (full < cells && rem8 > 0) {
        char pb[2]; pb[0] = GC_EIGHTH(rem8); pb[1] = '\0';
        text_put(f, px, y, on, pb);
        px += cw;
        start = full + 1;
    }

    len = 0;
    for (i = start; i < cells; i++) buf[len++] = GC_LITE;
    buf[len] = '\0';
    text_put(f, px, y, off, buf);
}

void ui_bar_px(int x, int y, int w, int h, float frac,
               unsigned int on, unsigned int off)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    gfx_quad((float)x, (float)y, (float)w, (float)h, off);
    gfx_quad((float)x, (float)y, w * frac, (float)h, on);
}

void ui_grid(Font f, int x, int y, int cols, int rows, char glyph, unsigned int col)
{
    char buf[64];
    int ch = font_ch(f);
    int r, i;
    if (cols > (int)sizeof(buf) - 1) cols = (int)sizeof(buf) - 1;
    for (i = 0; i < cols; i++) buf[i] = glyph;
    buf[cols] = '\0';
    for (r = 0; r < rows; r++)
        text_put(f, x, y + r * ch, col, buf);
}

void ui_kv(int x, int y, int valx, const char *key, const char *val)
{
    text_put(F_SM, x, y, TH.ink_mute, key);
    text_put(F_SM, valx, y, TH.ink, val);
}

void ui_kv_right(int x, int y, int x_right, const char *key, const char *val)
{
    text_put(F_SM, x, y, TH.ink_mute, key);
    text_put_right(F_SM, x_right, y, TH.ink, val);
}

void ui_frame(int x, int y, int w, int h)
{
    gfx_rect_outline((float)x, (float)y, (float)w, (float)h, 1, TH.rule);
}

int ui_chip(Font f, int x, int y, const char *s, unsigned int fill, unsigned int ink)
{
    int tw = text_w(f, s);
    int padx = 5;
    int w = tw + padx * 2;
    gfx_quad((float)x, (float)y, (float)w, (float)font_ch(f), fill);
    text_put(f, x + padx, y, ink, s);
    return w;
}

void ui_folder_icon(int x, int y, int s, unsigned int col)
{
    int tabw = s * 9 / 20;
    int tabh = s * 4 / 20;
    int by, bh, i, j;
    if (tabh < 3) tabh = 3;
    by = y + tabh;
    bh = s - tabh;

    gfx_rect_outline((float)x, (float)y, (float)tabw, (float)(tabh + 2), 1.0f, col);
    gfx_rect_outline((float)x, (float)by, (float)s, (float)bh, 1.0f, col);

    if (s >= 26) {
        int x0 = x + 3, x1 = x + s - 3;
        int y0 = by + 3, y1 = by + bh - 3;
        for (j = y0; j < y1; j += 3) {
            int off = (((j - y0) / 3) & 1) ? 2 : 0;
            for (i = x0 + off; i < x1; i += 4)
                gfx_quad((float)i, (float)j, 1.5f, 1.5f, col);
        }
    }
}
