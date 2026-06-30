#ifndef APP_H
#define APP_H

#include "library.h"
#include "gfx.h"
#include "theme.h"
#include "lyrics.h"

#define COVER_TEX (7 * GRID)
#define REC_ART   72

typedef enum { SCREEN_LIBRARY = 0, SCREEN_RECORD, SCREEN_NOWPLAYING } Screen;
typedef enum { MODE_ALBUMS = 0, MODE_PLAYLISTS, MODE_TREES, MODE_COUNT } LibMode;

typedef enum { NP_VIEW_COVER = 0, NP_VIEW_LYRICS, NP_VIEW_VIZ } NpView;

typedef enum { LIBVIEW_LIST = 0, LIBVIEW_SHELF = 1 } LibView;

typedef struct {
    Library lib;
    Screen  screen;
    LibMode mode;

    int       lib_sel;
    int       lib_top;
    Texture  *preview_tex;
    int       preview_for;
    int       preview_mode;

    Record   *rec;
    int       rec_sel;
    int       rec_top;
    Texture  *rec_tex;
    Texture  *rec_thumb_tex;
    Texture  *np_tex;

    int       np_index;
    float     np_anim;
    int       scrub_dir;
    float     scrub_ms;
    float     scrub_seek_t;
    float     l_replay_t;

    int        tree_menu_open;
    int        tree_depth;
    FolderNode *tree_stack[MAX_TREE_DEPTH];
    int        tree_sel[MAX_TREE_DEPTH];

    int       shuffle;

    NpView    np_view;
    int       viz_style;
    float     lyrics_anim;
    float     viz_anim;
#if LYRICS_ENABLED

    Lyrics    lyrics;
#endif

    int       settings_open;
    float     settings_anim;
    int       settings_sel;

    int       controls_open;
    float     controls_anim;

    float        time;
    unsigned int btn_prev;
} App;

extern App g_app;

extern unsigned int g_pressed;
extern unsigned int g_held;
#define PRESSED(b) ((g_pressed & (b)) != 0)
#define HELD(b)    ((g_held & (b)) != 0)

void scr_library(void);
void scr_record(void);
void scr_nowplaying(void);

LibView libview_current(void);
void    libview_set(LibView v);

Record *lib_current_list(int *count);
void    update_preview(void);
Texture *app_load_cover(Record *r, int size);
void    open_record(int index);
void    open_record_ptr(Record *r);
void    go_library(void);
void    start_play(int index, int go_nowplaying, int animate);
int     next_track_index(int cur);
int     prev_track_index(int cur);
void    handle_auto_advance(void);
void    fmt_time(char *buf, int sz, int seconds);
unsigned int str_hash(const char *s);

#endif
