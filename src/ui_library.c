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
#include "config.h"
#include "controls.h"
#include "image.h"

#define LIB_ROW_H   GRID
#define LIB_VISIBLE 10
#define LIST_X      GX(1)
#define LIST_NAME   GX(3)
#define LIST_R      GX(16)
#define LIST_DIV    GX(17)
#define LIST_TOP    GY(4)

#define PV          (7 * GRID)
#define PV_X        GX(18)
#define PV_Y        GY(4)

#define SET_W       284
#define SET_DUR     0.22f
#define SET_ROWS    4

static float ease01(float a)
{
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    return a * a * (3.0f - 2.0f * a);
}

static LibView g_lib_view = LIBVIEW_LIST;
LibView libview_current(void)  { return g_lib_view; }
void    libview_set(LibView v) { g_lib_view = v; }

#define SH_COLS     4
#define SH_ROWS     2
#define SH_TOP      (GY(3) + 6)
#define SH_BOT      FOOTER_TOP
#define SH_BANDH    (SH_BOT - SH_TOP)
#define SH_ROWH     (SH_BANDH / SH_ROWS)
#define SH_COLW     (SCR_W / SH_COLS)
#define SH_NAMEH    16
#define SH_GAP      3
#define SH_PAD      4
#define SH_ART      (SH_ROWH - SH_NAMEH - SH_GAP - SH_PAD)
#define SH_SLIDE    0.16f
#define SH_DECODE_BUDGET 3
#define SH_NAME_SCALE 0.78f

#define CC_MAX 16
typedef struct { Record *rec; Texture *tex; unsigned int used; } CCEntry;
static CCEntry g_cc[CC_MAX];
static int g_cc_theme  = -1;
static int g_cc_dither = -1;
static int g_cc_budget = 0;

static void cc_flush(void)
{
    int i;
    for (i = 0; i < CC_MAX; i++) {
        if (g_cc[i].tex) tex_free(g_cc[i].tex);
        g_cc[i].tex = NULL;
        g_cc[i].rec = NULL;
    }
}

static void cc_sync(void)
{
    int th = (int)theme_current();
    int di = image_dither();
    if (th != g_cc_theme || di != g_cc_dither) {
        cc_flush();
        g_cc_theme  = th;
        g_cc_dither = di;
    }
    g_cc_budget = SH_DECODE_BUDGET;
}

static Texture *cc_get(Record *rec)
{
    int i, free_slot = -1, lru = 0;
    unsigned int oldest = 0xffffffffu;

    if (!rec) return NULL;
    for (i = 0; i < CC_MAX; i++)
        if (g_cc[i].tex && g_cc[i].rec == rec) {
            g_cc[i].used = gfx_frame();
            return g_cc[i].tex;
        }
    if (g_cc_budget <= 0) return NULL;

    for (i = 0; i < CC_MAX; i++) {
        if (!g_cc[i].tex) { free_slot = i; break; }
        if (g_cc[i].used < oldest) { oldest = g_cc[i].used; lru = i; }
    }
    if (free_slot < 0) { free_slot = lru; tex_free(g_cc[free_slot].tex); }

    g_cc_budget--;
    g_cc[free_slot].rec  = rec;
    g_cc[free_slot].tex  = app_load_cover(rec, SH_ART);
    g_cc[free_slot].used = gfx_frame();
    return g_cc[free_slot].tex;
}

static int         g_sh_top    = 0;
static float       g_sh_scroll = 0.0f;
static int         g_sh_snap   = 1;
static int         g_sh_tdepth = 0;
static FolderNode *g_sh_tpath[MAX_TREE_DEPTH];
static int         g_sh_tsel[MAX_TREE_DEPTH];

static const char *g_mq_name = NULL;
static float       g_mq_t     = 0.0f;
#define MQ_HOLD   0.9f
#define MQ_SPEED  30.0f
#define MQ_PAD    3

static unsigned int with_alpha(unsigned int c, int a)
{
    if (a < 0)   a = 0;
    if (a > 255) a = 255;
    return (c & 0x00ffffffu) | ((unsigned int)a << 24);
}

static int sh_name_cw(void)
{
    int cw = (int)(font_cw(F_SM) * SH_NAME_SCALE + 0.5f);
    return cw < 1 ? 1 : cw;
}

static void put_center_clip(int cx, int y, unsigned int col, const char *s, int maxw)
{
    int cw    = sh_name_cw();
    int maxch = maxw / cw;
    int len   = (int)strlen(s);
    const char *draw = s;
    char tmp[64];
    int tw;

    if (maxch < 1) return;
    if (len > maxch) {
        if (maxch > (int)sizeof(tmp) - 1) maxch = (int)sizeof(tmp) - 1;
        if (maxch >= 3) {
            memcpy(tmp, s, (size_t)(maxch - 2));
            tmp[maxch - 2] = '.';
            tmp[maxch - 1] = '.';
            tmp[maxch]     = '\0';
        } else {
            memcpy(tmp, s, (size_t)maxch);
            tmp[maxch] = '\0';
        }
        draw = tmp;
    }
    tw = (int)(text_w(F_SM, draw) * SH_NAME_SCALE + 0.5f);
    text_put_scaled(F_SM, cx - tw / 2, y, col, draw, SH_NAME_SCALE);
}

static int marquee_offset(int overflow)
{
    float travel, period, t;
    if (overflow <= 0) return 0;
    travel = (float)overflow / MQ_SPEED;
    period = 2.0f * (MQ_HOLD + travel);
    t = fmodf(g_mq_t, period);
    if (t < MQ_HOLD)            return 0;
    t -= MQ_HOLD;
    if (t < travel)            return (int)(t * MQ_SPEED);
    t -= travel;
    if (t < MQ_HOLD)           return overflow;
    t -= MQ_HOLD;
    return overflow - (int)(t * MQ_SPEED);
}

typedef struct { Record *rec; const char *name; int is_folder; } ShelfItem;

static FolderNode *sh_children(int *n)
{
    if (g_sh_tdepth == 0) {
        if (n) *n = g_app.lib.tree_count;
        return g_app.lib.trees;
    }
    {
        FolderNode *p = g_sh_tpath[g_sh_tdepth - 1];
        if (n) *n = p->child_count;
        return p->children;
    }
}

static int shelf_count(void)
{
    int n;
    if (g_app.mode == MODE_ALBUMS)    return g_app.lib.album_count;
    if (g_app.mode == MODE_PLAYLISTS) return g_app.lib.playlist_count;
    sh_children(&n);
    return n;
}

static void shelf_item(int idx, ShelfItem *it)
{
    it->rec = NULL; it->name = ""; it->is_folder = 0;
    if (g_app.mode == MODE_ALBUMS) {
        it->rec  = &g_app.lib.albums[idx];
        it->name = it->rec->name;
    } else if (g_app.mode == MODE_PLAYLISTS) {
        it->rec  = &g_app.lib.playlists[idx];
        it->name = it->rec->name;
    } else {
        int n;
        FolderNode *arr  = sh_children(&n);
        FolderNode *node = &arr[idx];
        it->name = node->name;
        if (node->record) it->rec = node->record;
        else              it->is_folder = 1;
    }
}

static int *shelf_sel_ptr(void)
{
    if (g_app.mode == MODE_TREES) return &g_sh_tsel[g_sh_tdepth];
    return &g_app.lib_sel;
}

static void shelf_tile(int cellx, int celltop, const ShelfItem *it, int sel, int alpha)
{
    int ax = cellx + (SH_COLW - SH_ART) / 2;
    int ay = celltop + SH_PAD;
    int cx = cellx + SH_COLW / 2;
    int ny = ay + SH_ART + SH_GAP;

    if (it->is_folder) {
        int gs = SH_ART * 5 / 8;
        gfx_rect_outline((float)(ax - 1), (float)(ay - 1), SH_ART + 2, SH_ART + 2,
                         1, with_alpha(TH.rule, alpha));
        ui_folder_icon(ax + (SH_ART - gs) / 2, ay + (SH_ART - gs) / 2, gs,
                       with_alpha(sel ? TH.ink : TH.ink_dim, alpha));
    } else {
        Texture *t = cc_get(it->rec);
        if (t) gfx_blit_nn(t, (float)ax, (float)ay, SH_ART, SH_ART,
                           with_alpha(RGB(255, 255, 255), alpha));
        else   gfx_quad((float)ax, (float)ay, SH_ART, SH_ART,
                        with_alpha(TH.panel, alpha));
        gfx_rect_outline((float)(ax - 1), (float)(ay - 1), SH_ART + 2, SH_ART + 2,
                         1, with_alpha(TH.rule, alpha));
    }

    if (sel) {
        int chh  = (int)(font_ch(F_SM) * SH_NAME_SCALE + 0.5f);
        int maxw = SH_COLW - 6;
        int tw   = (int)(text_w(F_SM, it->name) * SH_NAME_SCALE + 0.5f);

        if (tw <= maxw) {

            int pw = tw + 8;
            if (pw > SH_COLW - 2) pw = SH_COLW - 2;
            gfx_quad((float)(cx - pw / 2), (float)(ny - 1), pw, chh + 2,
                     with_alpha(TH.sel_fill, alpha));
            text_put_scaled(F_SM, cx - tw / 2, ny, with_alpha(TH.sel_ink, alpha),
                            it->name, SH_NAME_SCALE);
        } else {

            int cl     = cx - maxw / 2;
            int innerw = maxw - 2 * MQ_PAD;
            int off    = marquee_offset(tw - innerw);
            gfx_quad((float)cl, (float)(ny - 1), maxw, chh + 2,
                     with_alpha(TH.sel_fill, alpha));
            gfx_clip(cl + MQ_PAD, SH_TOP, innerw, SH_BANDH);
            text_put_scaled(F_SM, cl + MQ_PAD - off, ny, with_alpha(TH.sel_ink, alpha),
                            it->name, SH_NAME_SCALE);
            gfx_clip(0, SH_TOP, SCR_W, SH_BANDH);
        }
    } else {
        put_center_clip(cx, ny, with_alpha(TH.ink_dim, alpha), it->name, SH_COLW - 6);
    }
}

static void draw_shelf(int count, int sel)
{
    int sel_row = (count > 0 && sel >= 0) ? sel / SH_COLS : 0;
    int first_row, r;
    float target;

    if (sel_row < g_sh_top)            g_sh_top = sel_row;
    if (sel_row >= g_sh_top + SH_ROWS) g_sh_top = sel_row - SH_ROWS + 1;
    if (g_sh_top < 0)                  g_sh_top = 0;

    target = (float)g_sh_top * SH_ROWH;
    if (g_sh_snap) { g_sh_scroll = target; g_sh_snap = 0; }
    else {
        float step = gfx_dt() * (SH_ROWH / SH_SLIDE);
        if      (g_sh_scroll < target) { g_sh_scroll += step; if (g_sh_scroll > target) g_sh_scroll = target; }
        else if (g_sh_scroll > target) { g_sh_scroll -= step; if (g_sh_scroll < target) g_sh_scroll = target; }
    }

    if (count == 0) {
        text_put(F_SM, PAD, SH_TOP + 2, TH.ink_dim,
                 (g_app.mode == MODE_TREES)  ? "FOLDER IS EMPTY"
               : (g_app.mode == MODE_ALBUMS) ? "NO ALBUMS FOUND" : "NO PLAYLISTS FOUND");
        return;
    }

    {
        ShelfItem si;
        shelf_item(sel < count ? sel : 0, &si);
        if (!si.is_folder) cc_get(si.rec);
        if (si.name != g_mq_name) { g_mq_name = si.name; g_mq_t = 0.0f; }
        else                       g_mq_t += gfx_dt();
    }

    first_row = (int)(g_sh_scroll / SH_ROWH) - 1;
    if (first_row < 0) first_row = 0;

    gfx_clip(0, SH_TOP, SCR_W, SH_BANDH);
    for (r = first_row; ; r++) {
        int celltop = SH_TOP - (int)g_sh_scroll + r * SH_ROWH;
        int alpha = 255, c;

        if (celltop >= SH_BOT)        break;
        if (r * SH_COLS >= count)     break;
        if (celltop + SH_ROWH <= SH_TOP) continue;

        if (celltop < SH_TOP) {
            int a = (int)(255.0f * (float)(celltop + SH_ROWH - SH_TOP) / (float)SH_ROWH);
            if (a < alpha) alpha = a;
        }
        if (celltop + SH_ROWH > SH_BOT) {
            int a = (int)(255.0f * (float)(SH_BOT - celltop) / (float)SH_ROWH);
            if (a < alpha) alpha = a;
        }
        if (alpha < 0) alpha = 0;

        for (c = 0; c < SH_COLS; c++) {
            int idx = r * SH_COLS + c;
            ShelfItem it;
            if (idx >= count) break;
            shelf_item(idx, &it);
            shelf_tile(c * SH_COLW, celltop, &it, idx == sel, alpha);
        }
    }
    gfx_clip_reset();
}

static void shelf_input(int count)
{
    int *psel = shelf_sel_ptr();
    int sel = *psel;

    if (count > 0) {
        if (PRESSED(PSP_CTRL_RIGHT) && sel < count - 1) sel++;
        if (PRESSED(PSP_CTRL_LEFT)  && sel > 0)         sel--;
        if (PRESSED(PSP_CTRL_DOWN)) {
            if (sel + SH_COLS < count) sel += SH_COLS;
            else                       sel = count - 1;
        }
        if (PRESSED(PSP_CTRL_UP) && sel - SH_COLS >= 0) sel -= SH_COLS;
        if (sel < 0)            sel = 0;
        if (sel > count - 1)    sel = count - 1;
        *psel = sel;

        if (PRESSED(PSP_CTRL_CROSS)) {
            if (g_app.mode == MODE_TREES) {
                int n;
                FolderNode *arr  = sh_children(&n);
                FolderNode *node = &arr[sel];
                if (node->record) { open_record_ptr(node->record); return; }
                if (node->child_count > 0 && g_sh_tdepth < MAX_TREE_DEPTH - 1) {
                    g_sh_tpath[g_sh_tdepth] = node;
                    g_sh_tdepth++;
                    g_sh_tsel[g_sh_tdepth] = 0;
                    g_sh_top  = 0;
                    g_sh_snap = 1;
                }
            } else {
                open_record(sel);
                return;
            }
        }
    }

    if (g_app.mode == MODE_TREES && PRESSED(PSP_CTRL_CIRCLE) && g_sh_tdepth > 0) {
        g_sh_tdepth--;
        g_sh_top  = 0;
        g_sh_snap = 1;
    }
}

static void settings_toggle_theme(void)
{
    theme_set((ThemeId)((theme_current() + 1) % THEME_COUNT));
    if (g_app.preview_tex) { tex_free(g_app.preview_tex); g_app.preview_tex = NULL; }
    g_app.preview_for = -1;
    config_save();
}

static void settings_toggle_font(void)
{
    text_set_face(text_current_face() == FACE_PLEX ? FACE_PIXEL : FACE_PLEX);
    config_save();
}

static void settings_toggle_cover(void)
{
    image_set_dither(!image_dither());
    if (g_app.preview_tex) { tex_free(g_app.preview_tex); g_app.preview_tex = NULL; }
    g_app.preview_for = -1;
    config_save();
}

static void settings_toggle_view(void)
{
    libview_set(libview_current() == LIBVIEW_SHELF ? LIBVIEW_LIST : LIBVIEW_SHELF);
    g_sh_snap = 1;
    g_app.tree_menu_open = 0;
    g_app.preview_for = -1;
    if (libview_current() == LIBVIEW_LIST) cc_flush();
    config_save();
}

static void set_row(int idx, int px, int y, const char *key, const char *val)
{
    int sel = (g_app.settings_sel == idx);
    int ix  = px + 14;
    int xr  = px + SET_W - 14;
    unsigned int kc, vc;
    char buf[24];

    if (sel) {
        gfx_quad((float)(px + 8), (float)y, SET_W - 16, 22, TH.sel_fill);
        kc = vc = TH.sel_ink;
        snprintf(buf, sizeof(buf), "< %s >", val);
    } else {
        kc = TH.ink; vc = TH.ink_dim;
        snprintf(buf, sizeof(buf), "%s", val);
    }
    text_put(F_SM, ix, y + 4, kc, key);
    text_put_right(F_SM, xr, y + 4, vc, buf);
}

static void draw_settings(void)
{
    float e  = ease01(g_app.settings_anim);
    int   px = SCR_W - (int)(SET_W * e);
    int   ix = px + 14;
    int   xr = px + SET_W - 14;
    int   y;
    char  buf[32];

    gfx_quad(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, (unsigned int)(120.0f * e)));

    gfx_quad((float)px, 0, SET_W, SCR_H, TH.panel);
    gfx_quad((float)px, 0, 3, SCR_H, TH.accent);

    text_put(F_LG, ix, 12, TH.ink, "SETTINGS");
    ui_rule(px + 10, 48, SET_W - 20);

    set_row(0, px, 58, "THEME", TH.name);
    set_row(1, px, 84, "FONT",
            text_current_face() == FACE_PIXEL ? "PIXELIFY" : "PLEX MONO");
    set_row(2, px, 110, "COVER",
            image_dither() ? "DITHERED" : "ORIGINAL");
    set_row(3, px, 136, "VIEW",
            libview_current() == LIBVIEW_SHELF ? "SHELF" : "LIST");

    y = 172;
    text_put(F_SM, ix, y, TH.ink_mute, "LIBRARY");
    ui_dotrule(px + 10, y + 15, SET_W - 20);
    snprintf(buf, sizeof(buf), "%d", g_app.lib.album_count);
    ui_kv_right(ix, y + 23, xr, "ALBUMS", buf);
    snprintf(buf, sizeof(buf), "%d", g_app.lib.playlist_count);
    ui_kv_right(ix, y + 39, xr, "PLAYLISTS", buf);
    snprintf(buf, sizeof(buf), "%d", g_app.lib.tree_count);
    ui_kv_right(ix, y + 55, xr, "FOLDERS", buf);
}

#define TM_COL_W    150
#define TM_STEP     118
#define TM_ROW_H    16
#define TM_HDR_H    16
#define TM_VIS_ROWS 9
#define TM_MARGIN   6
#define TM_ANCHOR_X GX(5)

static void tree_open_menu(void)
{
    if (g_app.lib.tree_count <= 0) return;
    if (g_app.lib_sel < 0 || g_app.lib_sel >= g_app.lib.tree_count) return;
    if (g_app.lib.trees[g_app.lib_sel].child_count <= 0) return;
    g_app.tree_stack[0] = &g_app.lib.trees[g_app.lib_sel];
    g_app.tree_sel[0]   = 0;
    g_app.tree_depth    = 1;
    g_app.tree_menu_open = 1;
}

static void tree_menu_input(void)
{
    int d = g_app.tree_depth - 1;
    FolderNode *col = g_app.tree_stack[d];
    int n = col->child_count;
    int *sel = &g_app.tree_sel[d];

    if (PRESSED(PSP_CTRL_UP)   && *sel > 0)     (*sel)--;
    if (PRESSED(PSP_CTRL_DOWN) && *sel < n - 1) (*sel)++;

    if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_RIGHT)) {
        FolderNode *child = &col->children[*sel];
        if (child->record) {
            open_record_ptr(child->record);
            return;
        } else if (child->child_count > 0 && g_app.tree_depth < MAX_TREE_DEPTH) {
            g_app.tree_stack[g_app.tree_depth] = child;
            g_app.tree_sel[g_app.tree_depth]   = 0;
            g_app.tree_depth++;
        }
    }

    if (PRESSED(PSP_CTRL_CIRCLE) || PRESSED(PSP_CTRL_LEFT)) {
        if (g_app.tree_depth > 1) g_app.tree_depth--;
        else g_app.tree_menu_open = 0;
    }
}

static void draw_tree_page(void)
{
    int count = g_app.lib.tree_count;
    char buf[64];
    int i;

    if (count == 0) {
        text_put(F_SM, LIST_X, LIST_TOP, TH.ink_dim, "NO FOLDERS FOUND");
        text_put(F_SM, LIST_X, LIST_TOP + GRID, TH.ink_mute,
                 "NESTED FOLDERS APPEAR HERE");
        return;
    }

    for (i = 0; i < LIB_VISIBLE; i++) {
        int idx = g_app.lib_top + i;
        int ry  = LIST_TOP + i * LIB_ROW_H;
        int sel = (idx == g_app.lib_sel);
        FolderNode *node;
        unsigned int cidx, cname;
        if (idx >= count) break;
        node = &g_app.lib.trees[idx];

        if (sel) gfx_quad(LIST_X - 6, (float)ry, LIST_DIV - (LIST_X - 6),
                          LIB_ROW_H, TH.sel_fill);
        cidx  = sel ? TH.sel_ink : TH.ink_mute;
        cname = sel ? TH.sel_ink : TH.ink_dim;

        ui_folder_icon(LIST_X, ry + 2, 13, cidx);
        text_put_clip(F_SM, LIST_NAME, ry, cname, node->name, LIST_R - LIST_NAME - GRID);
        snprintf(buf, sizeof(buf), "%d", node->child_count);
        text_put_right(F_SM, LIST_R, ry, cidx, buf);
    }

    ui_frame(PV_X - 2, PV_Y - 2, PV + 4, PV + 4);
    {
        int gs = (PV * 5) / 8;
        ui_folder_icon(PV_X + (PV - gs) / 2, PV_Y + (PV - gs) / 2, gs, TH.ink);

        if (count > 0) {
            FolderNode *node = &g_app.lib.trees[g_app.lib_sel];
            text_put_clip(F_SM, PV_X, PV_Y + PV, TH.ink, node->name,
                          SCR_W - PV_X - PAD);
            snprintf(buf, sizeof(buf), "%d FOLDERS", node->child_count);
            text_put(F_SM, PV_X, PV_Y + PV + GRID, TH.ink_dim, buf);
            snprintf(buf, sizeof(buf), "%02d/%02d", g_app.lib_sel + 1, count);
            text_put_right(F_SM, PV_X + PV, PV_Y + PV + GRID, TH.ink_dim, buf);
        }
    }
}

static void draw_tree_menu(void)
{
    int depth = g_app.tree_depth;
    int base_x[MAX_TREE_DEPTH];
    int col_y[MAX_TREE_DEPTH];
    int shift, right_edge, d;
    int anchor_y = LIST_TOP + (g_app.lib_sel - g_app.lib_top) * LIB_ROW_H;

    if (depth < 1) return;

    gfx_quad(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 130));

    for (d = 0; d < depth; d++) base_x[d] = TM_ANCHOR_X + d * TM_STEP;
    right_edge = base_x[depth - 1] + TM_COL_W;
    shift = (right_edge > SCR_W - TM_MARGIN) ? right_edge - (SCR_W - TM_MARGIN) : 0;

    col_y[0] = anchor_y;
    for (d = 1; d < depth; d++) {
        int ptop = (g_app.tree_sel[d - 1] < TM_VIS_ROWS)
                     ? 0 : g_app.tree_sel[d - 1] - TM_VIS_ROWS + 1;
        col_y[d] = col_y[d - 1] + (g_app.tree_sel[d - 1] - ptop) * TM_ROW_H;
    }
    for (d = 0; d < depth; d++) {
        int rows = g_app.tree_stack[d]->child_count;
        int colh, maxy;
        if (rows > TM_VIS_ROWS) rows = TM_VIS_ROWS;
        colh = TM_HDR_H + rows * TM_ROW_H + 4;
        maxy = FOOTER_TOP - colh - 2;
        if (col_y[d] < GY(3) + 2) col_y[d] = GY(3) + 2;
        if (col_y[d] > maxy)      col_y[d] = maxy;
    }

    for (d = 0; d < depth; d++) {
        int x = base_x[d] - shift;
        int y = col_y[d];
        FolderNode *cnode = g_app.tree_stack[d];
        int n = cnode->child_count;
        int top = (g_app.tree_sel[d] < TM_VIS_ROWS)
                    ? 0 : g_app.tree_sel[d] - TM_VIS_ROWS + 1;
        int vis = n - top;
        int active = (d == depth - 1);
        int colh, i;
        if (vis > TM_VIS_ROWS) vis = TM_VIS_ROWS;
        colh = TM_HDR_H + vis * TM_ROW_H + 4;

        if (x + TM_COL_W < 0) continue;

        gfx_quad((float)(x - 1), (float)(y - 1), TM_COL_W + 2, colh + 2, TH.rule);
        gfx_quad((float)x, (float)y, TM_COL_W, colh, active ? TH.panel : TH.bg);
        gfx_quad((float)x, (float)y, 3, colh, active ? TH.accent : TH.rule);

        text_put_clip(F_SM, x + 8, y + 1, TH.ink_mute, cnode->name, TM_COL_W - 14);
        ui_rule(x + 6, y + TM_HDR_H - 1, TM_COL_W - 12);

        for (i = 0; i < vis; i++) {
            int idx = top + i;
            int ry  = y + TM_HDR_H + i * TM_ROW_H;
            FolderNode *ch = &cnode->children[idx];
            int rsel = (idx == g_app.tree_sel[d]);
            unsigned int ink;

            if (rsel) {
                gfx_quad((float)(x + 3), (float)ry, TM_COL_W - 6, TM_ROW_H,
                         active ? TH.sel_fill : TH.panel);
                ink = active ? TH.sel_ink : TH.ink;
            } else {
                ink = TH.ink_dim;
            }

            ui_folder_icon(x + 6, ry + 2, 11, ink);
            text_put_clip(F_SM, x + 22, ry, ink, ch->name, TM_COL_W - 22 - 14);
            if (ch->record) {
                char tb[8];
                snprintf(tb, sizeof(tb), "%d", ch->record->track_count);
                text_put_right(F_SM, x + TM_COL_W - 6, ry, ink, tb);
            } else {
                text_put_right(F_SM, x + TM_COL_W - 6, ry, ink, ">");
            }
        }
    }

    ui_rule(0, FOOTER_TOP, SCR_W);
    text_put(F_SM, PAD, FOOTER_TOP + 1, TH.ink_mute, "X/-> OPEN");
    text_put_right(F_SM, SCR_W - PAD, FOOTER_TOP + 1, TH.ink_mute, "O/<- BACK");
}

void scr_library(void)
{
    int count = 0;
    Record *list = lib_current_list(&count);
    const char *mode;
    const char *chip;
    char buf[64];
    int i;
    int shelf = (libview_current() == LIBVIEW_SHELF);
    float dt = gfx_dt();
    float tgt;

    if (shelf)                         count = shelf_count();
    else if (g_app.mode == MODE_TREES) count = g_app.lib.tree_count;

    if (g_app.settings_open) {

        if (PRESSED(PSP_CTRL_TRIANGLE) || PRESSED(PSP_CTRL_CIRCLE))
            g_app.settings_open = 0;
        if (PRESSED(PSP_CTRL_UP)   && g_app.settings_sel > 0) g_app.settings_sel--;
        if (PRESSED(PSP_CTRL_DOWN) && g_app.settings_sel < SET_ROWS - 1)
            g_app.settings_sel++;
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_LEFT) ||
            PRESSED(PSP_CTRL_RIGHT)) {
            if      (g_app.settings_sel == 0) settings_toggle_theme();
            else if (g_app.settings_sel == 1) settings_toggle_font();
            else if (g_app.settings_sel == 2) settings_toggle_cover();
            else                              settings_toggle_view();
        }
    } else if (g_app.controls_open) {

        if (PRESSED(PSP_CTRL_SELECT) || PRESSED(PSP_CTRL_CIRCLE) ||
            PRESSED(PSP_CTRL_TRIANGLE))
            g_app.controls_open = 0;
    } else if (g_app.tree_menu_open) {

        tree_menu_input();
        if (g_app.screen != SCREEN_LIBRARY) return;
    } else if (PRESSED(PSP_CTRL_TRIANGLE)) {
        g_app.settings_open = 1;
    } else if (PRESSED(PSP_CTRL_SELECT)) {
        g_app.controls_open = 1;
    } else {

        if (PRESSED(PSP_CTRL_SQUARE)) {
            g_app.mode = (LibMode)((g_app.mode + 1) % MODE_COUNT);
            g_app.lib_sel = 0;
            g_app.lib_top = 0;
            g_app.preview_for = -1;
            if (g_app.preview_tex) { tex_free(g_app.preview_tex); g_app.preview_tex = NULL; }
            g_sh_tdepth = 0; g_sh_tsel[0] = 0; g_sh_top = 0; g_sh_snap = 1;
            list = lib_current_list(&count);
            if (shelf)                         count = shelf_count();
            else if (g_app.mode == MODE_TREES) count = g_app.lib.tree_count;
        }
        if (shelf) {
            shelf_input(count);
            if (g_app.screen != SCREEN_LIBRARY) return;
        } else if (count > 0) {
            if (PRESSED(PSP_CTRL_DOWN)  && g_app.lib_sel < count - 1) g_app.lib_sel++;
            if (PRESSED(PSP_CTRL_UP)    && g_app.lib_sel > 0)         g_app.lib_sel--;
            if (PRESSED(PSP_CTRL_RIGHT)) g_app.lib_sel += LIB_VISIBLE;
            if (PRESSED(PSP_CTRL_LEFT))  g_app.lib_sel -= LIB_VISIBLE;
            if (g_app.lib_sel < 0) g_app.lib_sel = 0;
            if (g_app.lib_sel > count - 1) g_app.lib_sel = count - 1;
            if (PRESSED(PSP_CTRL_CROSS)) {
                if (g_app.mode == MODE_TREES) tree_open_menu();
                else { open_record(g_app.lib_sel); return; }
            }
        }

    }

    tgt = g_app.settings_open ? 1.0f : 0.0f;
    if (g_app.settings_anim < tgt) {
        g_app.settings_anim += dt / SET_DUR;
        if (g_app.settings_anim > tgt) g_app.settings_anim = tgt;
    } else if (g_app.settings_anim > tgt) {
        g_app.settings_anim -= dt / SET_DUR;
        if (g_app.settings_anim < tgt) g_app.settings_anim = tgt;
    }
    tgt = g_app.controls_open ? 1.0f : 0.0f;
    if (g_app.controls_anim < tgt) {
        g_app.controls_anim += dt / SET_DUR;
        if (g_app.controls_anim > tgt) g_app.controls_anim = tgt;
    } else if (g_app.controls_anim > tgt) {
        g_app.controls_anim -= dt / SET_DUR;
        if (g_app.controls_anim < tgt) g_app.controls_anim = tgt;
    }

    if (shelf) {

        if (count > 0) {
            int *ps = shelf_sel_ptr();
            if (*ps >= count) *ps = count - 1;
            if (*ps < 0)      *ps = 0;
        }
    } else {
        if (g_app.lib_sel < g_app.lib_top) g_app.lib_top = g_app.lib_sel;
        if (g_app.lib_sel >= g_app.lib_top + LIB_VISIBLE)
            g_app.lib_top = g_app.lib_sel - LIB_VISIBLE + 1;
        if (g_app.lib_top < 0) g_app.lib_top = 0;

        if (g_app.mode != MODE_TREES) update_preview();
    }

    mode = (g_app.mode == MODE_ALBUMS) ? "ALBUMS"
         : (g_app.mode == MODE_PLAYLISTS) ? "PLAYLISTS" : "TREES";

    chip = (shelf && g_app.mode == MODE_TREES && g_sh_tdepth > 0)
             ? g_sh_tpath[g_sh_tdepth - 1]->name : mode;

    ui_statusbar("2TUFF.WAV", "MS0:/MUSIC");
    text_put(F_LG, PAD, CONTENT_TOP, TH.ink, "LIBRARY");
    {
        int w = text_w(F_SM, chip) + 10;

        int chip_y = CONTENT_TOP + (font_ch(F_LG) - font_ch(F_SM)) / 2;
        ui_chip(F_SM, (PV_X + PV) - w, chip_y, chip, TH.accent, TH.accent_ink);
    }
    ui_rule(0, GY(3), SCR_W);

    if (shelf) {
        cc_sync();
        draw_shelf(count, *shelf_sel_ptr());
    } else if (g_app.mode == MODE_TREES) {
        ui_vrule(LIST_DIV, LIST_TOP, FOOTER_TOP - LIST_TOP);
        draw_tree_page();
    } else {
        ui_vrule(LIST_DIV, LIST_TOP, FOOTER_TOP - LIST_TOP);

        if (count == 0) {
            text_put(F_SM, LIST_X, LIST_TOP, TH.ink_dim,
                     (g_app.mode == MODE_ALBUMS) ? "NO ALBUMS FOUND"
                                                 : "NO PLAYLISTS FOUND");
            text_put(F_SM, LIST_X, LIST_TOP + GRID, TH.ink_mute, "PUT MUSIC IN MS0:/MUSIC");
        } else {
            for (i = 0; i < LIB_VISIBLE; i++) {
                int idx = g_app.lib_top + i;
                int ry = LIST_TOP + i * LIB_ROW_H;
                int sel = (idx == g_app.lib_sel);
                unsigned int cidx, cname;
                if (idx >= count) break;

                if (sel) gfx_quad(LIST_X - 6, (float)ry, LIST_DIV - (LIST_X - 6),
                                  LIB_ROW_H, TH.sel_fill);
                cidx  = sel ? TH.sel_ink : TH.ink_mute;
                cname = sel ? TH.sel_ink : TH.ink_dim;

                snprintf(buf, sizeof(buf), "%02d", idx + 1);
                text_put(F_SM, LIST_X, ry, cidx, buf);
                text_put_clip(F_SM, LIST_NAME, ry, cname, list[idx].name,
                              LIST_R - LIST_NAME - GRID);
                snprintf(buf, sizeof(buf), "%d", list[idx].track_count);
                text_put_right(F_SM, LIST_R, ry, cidx, buf);
            }
        }

        ui_frame(PV_X - 2, PV_Y - 2, PV + 4, PV + 4);
        if (g_app.preview_tex)
            gfx_blit_nn(g_app.preview_tex, PV_X, PV_Y, PV, PV, RGB(255, 255, 255));
        else
            ui_grid(F_SM, PV_X, PV_Y, PV / font_cw(F_SM),
                    PV / font_ch(F_SM), GC_MED, TH.rule);

        if (count > 0) {
            Record *r = &list[g_app.lib_sel];
            text_put_clip(F_SM, PV_X, PV_Y + PV, TH.ink, r->name,
                          SCR_W - PV_X - PAD);
            snprintf(buf, sizeof(buf), "TRK %02d", r->track_count);
            text_put(F_SM, PV_X, PV_Y + PV + GRID, TH.ink_dim, buf);
            snprintf(buf, sizeof(buf), "%02d/%02d", g_app.lib_sel + 1, count);
            text_put_right(F_SM, PV_X + PV, PV_Y + PV + GRID, TH.ink_dim, buf);
        }
    }

    if (!g_app.tree_menu_open) {
        ui_rule(0, FOOTER_TOP, SCR_W);
        {
            int bs = 13;
            int bw = psp_btn(BTN_SELECT, PAD, FOOTER_TOP + (FOOTER_H - bs) / 2, bs);
            text_put(F_SM, PAD + bw + 8, FOOTER_TOP + 1, TH.ink_mute, "CONTROLS");
        }
        snprintf(buf, sizeof(buf), "%02d %s", count,
                 (g_app.mode == MODE_TREES) ? "DIR" : "REC");
        text_put_right(F_SM, SCR_W - PAD, FOOTER_TOP + 1, TH.ink_mute, buf);
    }

    if (g_app.tree_menu_open)          draw_tree_menu();
    if (g_app.settings_anim > 0.001f)  draw_settings();
    if (g_app.controls_anim > 0.001f)  controls_draw(g_app.controls_anim);
}
