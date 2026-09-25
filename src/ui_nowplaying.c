#include <pspctrl.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "app.h"
#include "gfx.h"
#include "text.h"
#include "theme.h"
#include "widgets.h"
#include "glyphs.h"
#include "audio.h"
#include "viz.h"
#include "favorites.h"

#define ANIM_DUR  0.45f
#define VIEW_ANIM_DUR 0.30f

#define ART_CX 240
#define ART_CY 117
#define ART_SZ (7 * GRID)

#define NP_MAIN_Y   GY(2)
#define NP_MAIN_H   (GY(11) - GY(2))

#if LYRICS_ENABLED

#define ART_CX_LEFT  (PAD + ART_SZ / 2)
#define LYR_X        (ART_CX_LEFT + ART_SZ / 2 + GRID)
#define LYR_PITCH    GRID
#define LYR_SPAN     3
#define LYR_ROW_MAX  2
#endif

#define SKIM_RATE     12.0f
#define SEEK_THROTTLE 0.30f
#define L_DOUBLE_S    0.50f

static float smoothstep01(float x)
{
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    return x * x * (3.0f - 2.0f * x);
}

static void scrub_begin(int dir)
{
    if (g_app.scrub_dir == 0)
        g_app.scrub_ms = (float)(audio_elapsed_sec() * 1000);
    g_app.scrub_dir    = dir;
    g_app.scrub_seek_t = SEEK_THROTTLE;
}

static void scrub_end(void)
{
    if (g_app.scrub_dir == 0) return;
    g_app.scrub_dir = 0;
    audio_seek((int)g_app.scrub_ms);
}

static unsigned int fade(unsigned int col, float a)
{
    unsigned int al = (unsigned int)(((col >> 24) & 0xFFu) * a);
    return (col & 0x00FFFFFFu) | (al << 24);
}

#if LYRICS_ENABLED

static int lyr_wrap(const char *s, int maxw, char rows[LYR_ROW_MAX][128])
{
    int nr = 0;
    while (*s && nr < LYR_ROW_MAX) {
        while (*s == ' ') s++;
        if (!*s) break;

        if (nr == LYR_ROW_MAX - 1) {
            strncpy(rows[nr], s, 127); rows[nr][127] = '\0';
            nr++;
            break;
        }
        {
            char line[128];
            int  len = 0;
            line[0] = '\0';
            while (*s) {
                const char *we = s;
                char cand[160];
                int  wl;
                while (*we && *we != ' ') we++;
                wl = (int)(we - s);
                if (len) snprintf(cand, sizeof(cand), "%s %.*s", line, wl, s);
                else     snprintf(cand, sizeof(cand), "%.*s", wl, s);
                if (len > 0 && text_w(F_SM, cand) > maxw) break;
                strncpy(line, cand, 127); line[127] = '\0';
                len = (int)strlen(line);
                s = we;
                while (*s == ' ') s++;
            }
            strncpy(rows[nr], line, 127); rows[nr][127] = '\0';
            nr++;
        }
    }
    if (nr == 0) { rows[0][0] = '\0'; nr = 1; }
    return nr;
}

static void lyr_put(int y, unsigned int col, const char *s, int maxw)
{
    if (y >= GY(2) + 2 && y + LYR_PITCH <= GY(11))
        text_put_clip(F_SM, LYR_X, y, col, s, maxw);
}

static void draw_lyrics_panel(float la)
{
    int panelw = (SCR_W - PAD) - LYR_X;
    int ms, active, i, k, ar, activeTop, belowY, aboveY;
    char rows[LYR_ROW_MAX][128];

    if (g_app.lyrics.count == 0) {
        text_put(F_SM, LYR_X, ART_CY - font_ch(F_SM) / 2,
                 fade(TH.ink_mute, la), "NO LYRICS");
        return;
    }

    ms     = (g_app.scrub_dir != 0) ? (int)g_app.scrub_ms : audio_elapsed_ms();
    active = lyrics_index_at(&g_app.lyrics, ms);

    ar = (active >= 0)
         ? lyr_wrap(g_app.lyrics.lines[active].text, panelw, rows) : 0;
    activeTop = ART_CY - (ar * LYR_PITCH) / 2;
    for (k = 0; k < ar; k++)
        lyr_put(activeTop + k * LYR_PITCH, fade(TH.accent, la), rows[k], panelw);

    belowY = activeTop + ar * LYR_PITCH;
    for (i = 1; i <= LYR_SPAN; i++) {
        int li = active + i, nr;
        if (li < 0 || li >= g_app.lyrics.count || belowY >= GY(11)) break;
        nr = lyr_wrap(g_app.lyrics.lines[li].text, panelw, rows);
        for (k = 0; k < nr; k++)
            lyr_put(belowY + k * LYR_PITCH,
                    fade(i == 1 ? TH.ink_dim : TH.ink_mute, la), rows[k], panelw);
        belowY += nr * LYR_PITCH;
    }

    aboveY = activeTop;
    for (i = 1; i <= LYR_SPAN; i++) {
        int li = active - i, nr;
        if (li < 0 || aboveY <= GY(2) + 2) break;
        nr = lyr_wrap(g_app.lyrics.lines[li].text, panelw, rows);
        aboveY -= nr * LYR_PITCH;
        for (k = 0; k < nr; k++)
            lyr_put(aboveY + k * LYR_PITCH,
                    fade(i == 1 ? TH.ink_dim : TH.ink_mute, la), rows[k], panelw);
    }
}
#endif

static void corner(int x, int y, int dx, int dy, unsigned int col)
{
    gfx_quad((float)x, (float)y, (float)(dx * 12), 2, col);
    gfx_quad((float)x, (float)y, 2, (float)(dy * 12), col);
}

void scr_nowplaying(void)
{
    Record *r = g_app.play_rec;
    Track *t;
    float dt = gfx_dt();
    float a, cx, cy, sz;
    int y0, half;
    const char *status;
    unsigned int status_col;
    char buf[40], el[12], to[12], times[28];
    AudioState st;
    int total_ms, can_scrub, disp_ms;
    float disp_prog;

    if (!r || g_app.np_index < 0 || g_app.np_index >= r->track_count) {
        g_app.screen = SCREEN_LIBRARY;
        return;
    }
    t = &r->tracks[g_app.np_index];

    st       = audio_state();
    total_ms = audio_total_sec() * 1000;
    can_scrub = (total_ms > 0) &&
                (st == AUDIO_PLAYING || st == AUDIO_PAUSED);

    if (PRESSED(PSP_CTRL_CIRCLE)) { scrub_end(); g_app.screen = g_app.np_from; return; }

    if (PRESSED(PSP_CTRL_SELECT)) {
        app_toggle_favorite(&r->tracks[g_app.np_index]);
        if (!g_app.play_rec) { scrub_end(); g_app.screen = SCREEN_LIBRARY; return; }
        r = g_app.play_rec;
        if (g_app.np_index >= r->track_count) g_app.np_index = r->track_count - 1;
        if (g_app.np_index < 0) g_app.np_index = 0;
    }

    if (PRESSED(PSP_CTRL_CROSS)) {
        if (g_app.scrub_dir != 0) scrub_end();
        else                      audio_toggle_pause();
    }
    if (PRESSED(PSP_CTRL_START))  { scrub_end(); audio_toggle_pause(); }
#if LYRICS_ENABLED

    if (PRESSED(PSP_CTRL_TRIANGLE))
        g_app.np_view = (g_app.np_view == NP_VIEW_LYRICS) ? NP_VIEW_COVER
                                                          : NP_VIEW_LYRICS;
#endif
#if VIZ_ENABLED

    if (PRESSED(PSP_CTRL_SQUARE)) {
        if (g_app.np_view != NP_VIEW_VIZ) {
            g_app.np_view   = NP_VIEW_VIZ;
            g_app.viz_style = 0;
        } else if (g_app.viz_style + 1 < VIZ_STYLE_COUNT) {
            g_app.viz_style++;
        } else {
            g_app.np_view = NP_VIEW_COVER;
        }
    }
#endif

    if (PRESSED(PSP_CTRL_LTRIGGER)) {
        int can_prev = g_app.shuffle ? (r->track_count > 1) : (g_app.np_index > 0);
        if (can_prev && g_app.l_replay_t > 0.0f &&
            (g_app.time - g_app.l_replay_t) <= L_DOUBLE_S) {
            start_play(g_app.shuffle ? prev_track_index(g_app.np_index)
                                     : g_app.np_index - 1, 1, 0);
            g_app.l_replay_t = 0.0f;
        } else {
            start_play(g_app.np_index, 1, 0);
            g_app.l_replay_t = g_app.time;
        }
    }

    if (PRESSED(PSP_CTRL_RTRIGGER)) {
        if (g_app.shuffle && r->track_count > 1)
            start_play(next_track_index(g_app.np_index), 1, 0);
        else if (g_app.np_index < r->track_count - 1)
            start_play(g_app.np_index + 1, 1, 0);
    }

    if (can_scrub && PRESSED(PSP_CTRL_RIGHT)) scrub_begin(+1);
    if (can_scrub && PRESSED(PSP_CTRL_LEFT))  scrub_begin(-1);

    if (g_app.scrub_dir != 0) {
        g_app.scrub_ms += (float)g_app.scrub_dir * SKIM_RATE * 1000.0f * dt;
        if (g_app.scrub_ms <= 0.0f) {
            g_app.scrub_ms = 0.0f; scrub_end();
        } else if (g_app.scrub_ms >= (float)total_ms) {
            g_app.scrub_ms = (float)total_ms; scrub_end();
        } else {
            g_app.scrub_seek_t += dt;
            if (g_app.scrub_seek_t >= SEEK_THROTTLE) {
                audio_seek((int)g_app.scrub_ms);
                g_app.scrub_seek_t = 0.0f;
            }
        }
    }

    t = &r->tracks[g_app.np_index];

    if (g_app.np_anim < 1.0f) {
        g_app.np_anim += dt / ANIM_DUR;
        if (g_app.np_anim > 1.0f) g_app.np_anim = 1.0f;
    }

    {
        float step = dt / VIEW_ANIM_DUR;
        float lt = (g_app.np_view == NP_VIEW_LYRICS) ? 1.0f : 0.0f;
        float vt = (g_app.np_view == NP_VIEW_VIZ)    ? 1.0f : 0.0f;
        if (g_app.lyrics_anim < lt) {
            g_app.lyrics_anim += step;
            if (g_app.lyrics_anim > lt) g_app.lyrics_anim = lt;
        } else if (g_app.lyrics_anim > lt) {
            g_app.lyrics_anim -= step;
            if (g_app.lyrics_anim < lt) g_app.lyrics_anim = lt;
        }
        if (g_app.viz_anim < vt) {
            g_app.viz_anim += step;
            if (g_app.viz_anim > vt) g_app.viz_anim = vt;
        } else if (g_app.viz_anim > vt) {
            g_app.viz_anim -= step;
            if (g_app.viz_anim < vt) g_app.viz_anim = vt;
        }
    }

    if (g_app.scrub_dir > 0)      { status = "SKIM >>"; status_col = TH.accent; }
    else if (g_app.scrub_dir < 0) { status = "<< SKIM"; status_col = TH.accent; }
    else if (st == AUDIO_PAUSED)  { status = "PAUSED";  status_col = TH.accent; }
    else if (st == AUDIO_PLAYING) { status = "PLAYING"; status_col = TH.accent; }
    else                          { status = "STOPPED"; status_col = TH.ink_mute; }

    if (st == AUDIO_PLAYING && g_app.scrub_dir == 0) {
        float ph = 0.5f + 0.5f * sinf(g_app.time * 3.2f);
        unsigned int a = (unsigned int)((0.25f + 0.75f * ph) * 255.0f);
        status_col = (status_col & 0x00FFFFFFu) | (a << 24);
    }

    ui_statusbar("2TUFF.WAV", "NOW PLAYING");

    snprintf(buf, sizeof(buf), "TRK %02d/%02d", g_app.np_index + 1, r->track_count);
    text_put(F_SM, PAD, CONTENT_TOP, TH.ink_dim, buf);
    text_put_right(F_SM, SCR_W - PAD, CONTENT_TOP, status_col, status);
    ui_rule(0, GY(2), SCR_W);

    {
        char err[48];
        if (audio_last_error(err, sizeof(err)))
            text_put(F_SM, PAD, GY(2) + 6, TH.accent, err);
    }

    {
        float la = smoothstep01(g_app.lyrics_anim);
        float va = smoothstep01(g_app.viz_anim);
        float coverA = 1.0f - va;

#if VIZ_ENABLED

        if (va > 0.001f)
            viz_render(0, NP_MAIN_Y, SCR_W, NP_MAIN_H, dt,
                       audio_level(), audio_bass(), va, g_app.viz_style);
#endif

        if (coverA > 0.001f) {
            float restcx = ART_CX;
            unsigned int cc = fade(TH.accent, coverA);
#if LYRICS_ENABLED
            restcx = ART_CX + (ART_CX_LEFT - ART_CX) * la;
#endif

            half = ART_SZ / 2 + 6;
            corner((int)restcx - half, ART_CY - half,  1,  1, cc);
            corner((int)restcx + half, ART_CY - half, -1,  1, cc);
            corner((int)restcx - half, ART_CY + half,  1, -1, cc);
            corner((int)restcx + half, ART_CY + half, -1, -1, cc);

            a  = smoothstep01(g_app.np_anim);
            cx = (SCR_W - 64.0f) + (restcx - (SCR_W - 64.0f)) * a;
            cy = 58.0f + (ART_CY - 58.0f) * a;
            sz = 64.0f + (ART_SZ - 64.0f) * a;

            if (g_app.play_tex)
                gfx_blit_nn(g_app.play_tex, cx - sz * 0.5f, cy - sz * 0.5f, sz, sz,
                            fade(RGB(255, 255, 255), coverA));

#if LYRICS_ENABLED
            if (la > 0.001f) draw_lyrics_panel(la * coverA);
#endif
        }
    }

    y0 = GY(11);
    ui_rule(0, y0, SCR_W);

    text_put_clip(F_LG, PAD, y0, TH.ink, t->title, SCR_W - 2 * PAD - 22);
    {
        int ss = 15;
        ui_star(SCR_W - PAD - ss, y0 + (font_ch(F_LG) - ss) / 2, ss,
                favorites_contains(t->path) ? TH.accent : TH.ink_mute);
    }

    if (g_app.scrub_dir != 0) {
        disp_ms   = (int)g_app.scrub_ms;
        disp_prog = (total_ms > 0) ? (float)disp_ms / (float)total_ms : 0.0f;
    } else {
        disp_ms   = audio_elapsed_sec() * 1000;
        disp_prog = audio_progress();
    }

    fmt_time(el, sizeof(el), disp_ms / 1000);
    fmt_time(to, sizeof(to), audio_total_sec());
    snprintf(times, sizeof(times), "%s / %s", el, to);
    text_put_clip(F_SM, PAD, y0 + 36, TH.accent,
                  t->artist[0] ? t->artist : r->artist, SCR_W - 2 * PAD - 110);
    text_put_right(F_SM, SCR_W - PAD, y0 + 36, TH.ink_dim, times);

    ui_meter(F_SM, PAD, y0 + 54, (SCR_W - 2 * PAD) / font_cw(F_SM),
             disp_prog, TH.accent, TH.meter_off);
}
