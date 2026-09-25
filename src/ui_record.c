#include <pspctrl.h>
#include <stdio.h>
#include <math.h>

#include "app.h"
#include "gfx.h"
#include "text.h"
#include "theme.h"
#include "widgets.h"
#include "glyphs.h"
#include "favorites.h"

#define REC_ROW_H    GRID
#define REC_VISIBLE  8
#define REC_LIST_TOP GY(6)
#define REC_NAME     GX(3)
#define SHUF_SZ      16
#define REC_STAR_SZ  13
#define REC_STAR_X   (SCR_W - PAD - 62)

static void dab_line(float x0, float y0, float x1, float y1, float t, unsigned int c)
{
    float dx = x1 - x0, dy = y1 - y0;
    float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int   i, n = (int)((adx > ady ? adx : ady) + 0.5f);
    if (n < 1) n = 1;
    for (i = 0; i <= n; i++) {
        float u = (float)i / (float)n;
        gfx_quad(x0 + dx * u - t * 0.5f, y0 + dy * u - t * 0.5f, t, t, c);
    }
}

static void arrow_head(float tx, float ty, float ux, float uy,
                       float len, float t, unsigned int c)
{
    const float k = 0.70710678f;               /* cos/sin 45 */
    float bx = -ux, by = -uy;                 
    float b1x = bx * k - by * k, b1y = bx * k + by * k;
    float b2x = bx * k + by * k, b2y = -bx * k + by * k;
    dab_line(tx, ty, tx + b1x * len, ty + b1y * len, t, c);
    dab_line(tx, ty, tx + b2x * len, ty + b2y * len, t, c);
}


static void draw_shuffle(int x, int y, int s, unsigned int c)
{
    float t   = 1.7f;
    float ty  = y + s * 0.18f;
    float by  = y + s * 0.82f;
    float lx  = x + s * 0.02f;
    float xa  = x + s * 0.26f; 
    float xb  = x + s * 0.92f;
    float ah  = s * 0.30f;
    float dx  = xb - xa, dy = by - ty;
    float inv = 1.0f / sqrtf(dx * dx + dy * dy);
    float ux  = dx * inv, uy = dy * inv;

    dab_line(lx, ty, xa, ty, t, c);
    dab_line(xa, ty, xb, by, t, c);
    arrow_head(xb, by, ux, uy, ah, t, c);

    dab_line(lx, by, xa, by, t, c);
    dab_line(xa, by, xb, ty, t, c);
    arrow_head(xb, ty, ux, -uy, ah, t, c);
}

void scr_record(void)
{
    Record *r = g_app.rec;
    int mx = PAD + REC_ART + GRID;
    char buf[80];
    int i;

    if (!r) { go_library(); return; }

    if (PRESSED(PSP_CTRL_CIRCLE)) { go_library(); return; }

    if (PRESSED(PSP_CTRL_START) && playback_active()) { goto_nowplaying(); return; }

    if (PRESSED(PSP_CTRL_SELECT) && r->track_count > 0) {
        app_toggle_favorite(&r->tracks[g_app.rec_sel]);
        r = g_app.rec;                    
        if (!r) { go_library(); return; }  
    }

    if ((HELD(PSP_CTRL_LTRIGGER) && PRESSED(PSP_CTRL_SQUARE)) ||
        (HELD(PSP_CTRL_SQUARE)   && PRESSED(PSP_CTRL_LTRIGGER)))
        g_app.shuffle = !g_app.shuffle;
    if (r->track_count > 0) {
        if (PRESSED(PSP_CTRL_DOWN) && g_app.rec_sel < r->track_count - 1) g_app.rec_sel++;
        if (PRESSED(PSP_CTRL_UP)   && g_app.rec_sel > 0)                  g_app.rec_sel--;
        if (PRESSED(PSP_CTRL_RIGHT)) g_app.rec_sel += REC_VISIBLE;
        if (PRESSED(PSP_CTRL_LEFT))  g_app.rec_sel -= REC_VISIBLE;
        if (g_app.rec_sel < 0) g_app.rec_sel = 0;
        if (g_app.rec_sel > r->track_count - 1) g_app.rec_sel = r->track_count - 1;
        if (PRESSED(PSP_CTRL_CROSS)) {
            if (playback_active() && g_app.play_rec == g_app.rec &&
                g_app.np_index == g_app.rec_sel)
                goto_nowplaying();
            else
                play_record(g_app.rec, g_app.rec_sel, 1, 1);
            return;
        }
    }

    if (g_app.rec_sel < g_app.rec_top) g_app.rec_top = g_app.rec_sel;
    if (g_app.rec_sel >= g_app.rec_top + REC_VISIBLE)
        g_app.rec_top = g_app.rec_sel - REC_VISIBLE + 1;
    if (g_app.rec_top < 0) g_app.rec_top = 0;

    ui_statusbar("2TUFF.WAV", r->is_playlist ? "PLAYLIST" : "ALBUM");

    ui_frame(PAD - 1, CONTENT_TOP - 1, REC_ART + 2, REC_ART + 2);
    if (g_app.rec_thumb_tex)
        gfx_blit_nn(g_app.rec_thumb_tex, PAD, CONTENT_TOP, REC_ART, REC_ART, RGB(255, 255, 255));

    text_put_clip(F_LG, mx, CONTENT_TOP, TH.ink, r->name, SCR_W - mx - PAD);
    text_put_clip(F_SM, mx, GY(3), TH.accent,
                  r->artist[0] ? r->artist : (r->is_playlist ? "PLAYLIST" : "-"),
                  SCR_W - mx - PAD);
    if (r->year > 0)
        snprintf(buf, sizeof(buf), "YEAR %d    TRK %02d", r->year, r->track_count);
    else
        snprintf(buf, sizeof(buf), "TRK %02d", r->track_count);
    text_put(F_SM, mx, GY(4), TH.ink_dim, buf);

    draw_shuffle(SCR_W - PAD - SHUF_SZ, GY(4) + (font_ch(F_SM) - SHUF_SZ) / 2,
                 SHUF_SZ, g_app.shuffle ? TH.accent : TH.ink_mute);

    ui_rule(0, GY(5), SCR_W);

    for (i = 0; i < REC_VISIBLE; i++) {
        int idx = g_app.rec_top + i;
        int ry = REC_LIST_TOP + i * REC_ROW_H;
        int sel = (idx == g_app.rec_sel);
        Track *t;
        unsigned int cno, ctitle;
        char dur[12];
        if (idx >= r->track_count) break;
        t = &r->tracks[idx];

        if (sel) gfx_quad(PAD - 6, (float)ry, SCR_W - 2 * (PAD - 6), REC_ROW_H, TH.sel_fill);
        cno    = sel ? TH.sel_ink : TH.ink_mute;
        ctitle = sel ? TH.sel_ink : TH.ink_dim;

        snprintf(buf, sizeof(buf), "%02d", t->track_no);
        text_put(F_SM, PAD, ry, cno, buf);
        text_put_clip(F_SM, REC_NAME, ry, ctitle, t->title, REC_STAR_X - 4 - REC_NAME);
        if (favorites_contains(t->path))
            ui_star(REC_STAR_X, ry + (font_ch(F_SM) - REC_STAR_SZ) / 2, REC_STAR_SZ,
                    sel ? TH.sel_ink : TH.accent);
        fmt_time(dur, sizeof(dur), t->duration_sec);
        text_put_right(F_SM, SCR_W - PAD, ry, cno, dur);
    }

    if (playback_active()) {
        ui_rule(0, FOOTER_TOP, SCR_W);
        ui_nowplaying_bar(playback_title(), playback_paused());
    } else {
        snprintf(buf, sizeof(buf), "%02d/%02d", g_app.rec_sel + 1,
                 r->track_count > 0 ? r->track_count : 0);
        ui_footer(NULL, buf);
    }
}
