#include "theme.h"

const Theme THEME_TABLE[THEME_COUNT] = {

    {
        .name        = "WHIMSY TULIP",
        .bg          = RGB(247, 248, 242),
        .panel       = RGB(235, 238, 226),
        .ink         = RGB(28, 32, 24),
        .ink_dim     = RGB(92, 100, 80),
        .ink_mute    = RGB(144, 152, 126),
        .rule        = RGB(50, 58, 42),
        .accent      = RGB(116, 137, 50),
        .accent_ink  = RGB(247, 248, 242),
        .sel_fill    = RGB(28, 32, 24),
        .sel_ink     = RGB(247, 248, 242),
        .meter_on    = RGB(28, 32, 24),
        .meter_off   = RGB(212, 217, 200),
        .grid        = RGBA(60, 80, 30, 20),
        .cover_ink   = RGB(28, 32, 24),
        .cover_bg    = RGB(240, 242, 230),
        .cover_invert = 0,
    },

    {
        .name        = "VILLAINY BLACK",
        .bg          = RGB(13, 14, 16),
        .panel       = RGB(22, 24, 27),
        .ink         = RGB(226, 224, 214),
        .ink_dim     = RGB(150, 152, 146),
        .ink_mute    = RGB(96, 100, 98),
        .rule        = RGB(62, 66, 68),
        .accent      = RGB(204, 162, 92),
        .accent_ink  = RGB(13, 14, 16),
        .sel_fill    = RGB(204, 162, 92),
        .sel_ink     = RGB(13, 14, 16),
        .meter_on    = RGB(226, 224, 214),
        .meter_off   = RGB(48, 51, 54),
        .grid        = RGBA(120, 128, 130, 22),
        .cover_ink   = RGB(214, 210, 196),
        .cover_bg    = RGB(18, 20, 22),
        .cover_invert = 1,
    },

    {
        .name        = "CALLA LILY",
        .bg          = RGB(228, 138, 166),
        .panel       = RGB(216, 120, 150),
        .ink         = RGB(48, 22, 34),
        .ink_dim     = RGB(112, 64, 84),
        .ink_mute    = RGB(158, 108, 128),
        .rule        = RGB(74, 34, 54),
        .accent      = RGB(124, 18, 74),
        .accent_ink  = RGB(248, 234, 240),
        .sel_fill    = RGB(48, 22, 34),
        .sel_ink     = RGB(248, 234, 240),
        .meter_on    = RGB(48, 22, 34),
        .meter_off   = RGB(212, 164, 182),
        .grid        = RGBA(82, 20, 52, 26),
        .cover_ink   = RGB(48, 22, 34),
        .cover_bg    = RGB(232, 148, 174),
        .cover_invert = 0,
    },

    {
        .name        = "WORN LEATHER",
        .bg          = RGB(82, 57, 40),
        .panel       = RGB(98, 70, 50),
        .ink         = RGB(240, 230, 214),
        .ink_dim     = RGB(198, 178, 152),
        .ink_mute    = RGB(150, 128, 104),
        .rule        = RGB(120, 92, 66),
        .accent      = RGB(212, 180, 140),
        .accent_ink  = RGB(50, 34, 22),
        .sel_fill    = RGB(212, 180, 140),
        .sel_ink     = RGB(50, 34, 22),
        .meter_on    = RGB(240, 230, 214),
        .meter_off   = RGB(110, 82, 60),
        .grid        = RGBA(180, 150, 112, 22),
        .cover_ink   = RGB(236, 224, 206),
        .cover_bg    = RGB(64, 44, 30),
        .cover_invert = 1,
    },
};

const Theme *g_theme = &THEME_TABLE[ACTIVE_THEME];

void theme_set(ThemeId id)
{
    if (id >= 0 && id < THEME_COUNT)
        g_theme = &THEME_TABLE[id];
}

ThemeId theme_current(void)
{
    return (ThemeId)(g_theme - THEME_TABLE);
}
