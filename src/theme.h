#ifndef THEME_H
#define THEME_H

#define SCR_W 480
#define SCR_H 272

#define RGBA(r, g, b, a) (((unsigned int)(a) << 24) | ((unsigned int)(b) << 16) | \
                          ((unsigned int)(g) << 8) | (unsigned int)(r))
#define RGB(r, g, b) RGBA(r, g, b, 255)

typedef struct {
    const char  *name;
    unsigned int bg;
    unsigned int panel;
    unsigned int ink;
    unsigned int ink_dim;
    unsigned int ink_mute;
    unsigned int rule;
    unsigned int accent;
    unsigned int accent_ink;
    unsigned int sel_fill;
    unsigned int sel_ink;
    unsigned int meter_on;
    unsigned int meter_off;
    unsigned int grid;
    unsigned int cover_ink;
    unsigned int cover_bg;
    int          cover_invert;
} Theme;

typedef enum {
    THEME_PAPER = 0, THEME_TERMINAL = 1, THEME_CALLA = 2, THEME_LEATHER = 3,
    THEME_MUSTARD = 4, THEME_EARLGREY = 5, THEME_ZUSHI = 6
} ThemeId;
#define THEME_COUNT 7

#define ACTIVE_THEME THEME_PAPER

extern const Theme  THEME_TABLE[THEME_COUNT];
extern const Theme *g_theme;
void    theme_set(ThemeId id);
ThemeId theme_current(void);

#define TH (*g_theme)

#define GRID       18
#define GX(c)      ((c) * GRID)
#define GY(r)      ((r) * GRID)

#define PAD        GRID
#define HEADER_H   GRID
#define STATUS_Y   1
#define HEADER_RULE_Y GRID
#define CONTENT_TOP   GY(1)
#define FOOTER_H   20
#define FOOTER_TOP GY(14)

#endif
