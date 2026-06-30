#include <math.h>

#include "controls.h"
#include "gfx.h"
#include "theme.h"
#include "text.h"
#include "widgets.h"
#include "lyrics.h"
#include "viz.h"

static void disc(float cx, float cy, float r, unsigned int col)
{
    int dy, ir = (int)(r + 0.5f);
    for (dy = -ir; dy <= ir; dy++) {
        float v  = r * r - (float)dy * (float)dy;
        float hw = (v > 0.0f) ? sqrtf(v) : 0.0f;
        if (hw > 0.0f) gfx_quad(cx - hw, cy + (float)dy, hw * 2.0f, 1.0f, col);
    }
}

static void rrect(float x, float y, float w, float h, float r, unsigned int col)
{
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    gfx_quad(x + r, y, w - 2 * r, h, col);
    gfx_quad(x, y + r, r, h - 2 * r, col);
    gfx_quad(x + w - r, y + r, r, h - 2 * r, col);
    disc(x + r,     y + r,     r, col);
    disc(x + w - r, y + r,     r, col);
    disc(x + r,     y + h - r, r, col);
    disc(x + w - r, y + h - r, r, col);
}

static void tri(float cx, float cy, float r, int dir, unsigned int col)
{
    int i, n = (int)(r * 2.0f + 0.5f);
    for (i = 0; i <= n; i++) {
        float t    = (float)i / (float)n;
        float half = t * r;
        switch (dir) {
        case 0: gfx_quad(cx - half, cy - r + i, half * 2.0f, 1.0f, col); break;
        case 1: gfx_quad(cx - half, cy + r - i, half * 2.0f, 1.0f, col); break;
        case 2: gfx_quad(cx - r + i, cy - half, 1.0f, half * 2.0f, col); break;
        default:gfx_quad(cx + r - i, cy - half, 1.0f, half * 2.0f, col); break;
        }
    }
}

static void cross(float cx, float cy, float r, unsigned int col)
{
    int k, ir = (int)r;
    for (k = -ir; k <= ir; k++) {
        gfx_quad(cx + k - 1.0f, cy + k - 1.0f, 2.0f, 2.0f, col);
        gfx_quad(cx + k - 1.0f, cy - k - 1.0f, 2.0f, 2.0f, col);
    }
}

static void cap_disc(float cx, float cy, float r)
{
    disc(cx, cy, r + 1.0f, TH.rule);
    disc(cx, cy, r,        TH.ink_dim);
}

static void cap_rrect(float x, float y, float w, float h, float r)
{
    rrect(x - 1, y - 1, w + 2, h + 2, r + 1, TH.rule);
    rrect(x, y, w, h, r, TH.ink_dim);
}

int psp_btn(PspButton b, int x, int y, int s)
{
    float cx = x + s * 0.5f;
    float cy = y + s * 0.5f;
    float r  = s * 0.5f;
    float sr = r * 0.55f;
    int   w;

    switch (b) {
    case BTN_CROSS:
        cap_disc(cx, cy, r);  cross(cx, cy, sr, TH.bg);                     return s;
    case BTN_CIRCLE:
        cap_disc(cx, cy, r);
        disc(cx, cy, sr,        TH.bg);
        disc(cx, cy, sr - 2.0f, TH.ink_dim);                               return s;
    case BTN_TRIANGLE:
        cap_disc(cx, cy, r);  tri(cx, cy + 1.0f, sr, 0, TH.bg);            return s;
    case BTN_SQUARE:
        cap_disc(cx, cy, r);
        gfx_rect_outline(cx - sr, cy - sr, sr * 2.0f, sr * 2.0f, 2.0f, TH.bg); return s;

    case BTN_DPAD_UP:
        cap_rrect(x, y, s, s, 4); tri(cx, cy, s * 0.26f, 0, TH.bg);        return s;
    case BTN_DPAD_DOWN:
        cap_rrect(x, y, s, s, 4); tri(cx, cy, s * 0.26f, 1, TH.bg);        return s;
    case BTN_DPAD_LEFT:
        cap_rrect(x, y, s, s, 4); tri(cx, cy, s * 0.26f, 2, TH.bg);        return s;
    case BTN_DPAD_RIGHT:
        cap_rrect(x, y, s, s, 4); tri(cx, cy, s * 0.26f, 3, TH.bg);        return s;

    case BTN_L:
    case BTN_R:
        w = s * 3 / 2;
        cap_rrect(x, y, w, s, 4);
        text_put_center(F_SM, x + w / 2, y + (s - font_ch(F_SM)) / 2, TH.bg,
                        b == BTN_L ? "L" : "R");
        return w;

    case BTN_SELECT:
    case BTN_START: {
        const char *lbl = (b == BTN_SELECT) ? "SELECT" : "START";
        w = text_w(F_SM, lbl) + 10;
        cap_rrect(x, y, w, s, r);
        text_put_center(F_SM, x + w / 2, y + (s - font_ch(F_SM)) / 2, TH.bg, lbl);
        return w;
    }
    }
    return s;
}

static float ease01(float a)
{
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    return a * a * (3.0f - 2.0f * a);
}

void controls_draw(float anim)
{
    static const struct { const char *action; PspButton b[2]; int n; } rows[] = {
        { "BROWSE",           { BTN_DPAD_UP,   BTN_DPAD_DOWN  }, 2 },
        { "PAGE / SKIM",      { BTN_DPAD_LEFT, BTN_DPAD_RIGHT }, 2 },
        { "OPEN / PLAY",      { BTN_CROSS                     }, 1 },
        { "BACK",             { BTN_CIRCLE                    }, 1 },
        { "ALBUM/PL/TREES",   { BTN_SQUARE                    }, 1 },
        { "SETTINGS",         { BTN_TRIANGLE                  }, 1 },
        { "REPLAY / NEXT",    { BTN_L,         BTN_R          }, 2 },
        { "PREV (DBL-TAP)",   { BTN_L                         }, 1 },
        { "SHUFFLE",          { BTN_L,         BTN_SQUARE     }, 2 },
#if LYRICS_ENABLED
        { "LYRICS",           { BTN_TRIANGLE                  }, 1 },
#endif
#if VIZ_ENABLED
        { "VISUALIZERS",      { BTN_SQUARE                    }, 1 },
#endif
        { "CONTROLS",         { BTN_SELECT                    }, 1 },
        { "PLAY / PAUSE",     { BTN_START                     }, 1 },
    };
    int n  = (int)(sizeof(rows) / sizeof(rows[0]));
    float e  = ease01(anim);
    int   px = SCR_W - (int)(CTRL_W * e);
    int   ix = px + 14;
    int   actx = ix + 70;
    int   y0 = 50, pitch = 17, s = 16;
    int   i, j;

    gfx_quad(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, (unsigned int)(120.0f * e)));

    gfx_quad((float)px, 0, CTRL_W, SCR_H, TH.panel);
    gfx_quad((float)px, 0, 3, SCR_H, TH.accent);

    text_put(F_LG, ix, 12, TH.ink, "CONTROLS");
    ui_rule(px + 10, 48, CTRL_W - 20);

    for (i = 0; i < n; i++) {
        int ry = y0 + i * pitch;
        int rx = ix;
        for (j = 0; j < rows[i].n; j++) {
            rx += psp_btn(rows[i].b[j], rx, ry, s);
            if (j < rows[i].n - 1) rx += 5;
        }
        text_put(F_SM, actx, ry, TH.ink, rows[i].action);
    }
}
