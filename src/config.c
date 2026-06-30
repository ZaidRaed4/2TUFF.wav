#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "theme.h"
#include "text.h"
#include "image.h"
#include "app.h"

static char g_cfg_path[256] = "ms0:/PSP/GAME/2TUFFwav/settings.cfg";

void config_init(const char *eboot)
{
    const char *slash;
    int dirlen;

    if (!eboot || !eboot[0]) return;
    slash = strrchr(eboot, '/');
    if (!slash) return;
    dirlen = (int)(slash - eboot);
    if (dirlen <= 0 || dirlen > (int)sizeof(g_cfg_path) - 16) return;
    memcpy(g_cfg_path, eboot, (size_t)dirlen);
    strcpy(g_cfg_path + dirlen, "/settings.cfg");
}

static int read_kv(const char *buf, const char *key, int *out)
{
    const char *p = strstr(buf, key);
    if (!p) return 0;
    *out = atoi(p + strlen(key));
    return 1;
}

void config_load(void)
{
    SceUID fd = sceIoOpen(g_cfg_path, PSP_O_RDONLY, 0777);
    char buf[128];
    int n, v;

    if (fd < 0) return;
    n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    if (read_kv(buf, "theme=", &v) && v >= 0 && v < THEME_COUNT)
        theme_set((ThemeId)v);
    if (read_kv(buf, "font=", &v) && (v == FACE_PLEX || v == FACE_PIXEL))
        text_set_face((FontFace)v);
    if (read_kv(buf, "cover=", &v))
        image_set_dither(v);
    if (read_kv(buf, "view=", &v) && (v == LIBVIEW_LIST || v == LIBVIEW_SHELF))
        libview_set((LibView)v);
}

void config_save(void)
{
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "theme=%d\nfont=%d\ncover=%d\nview=%d\n",
                       (int)theme_current(), (int)text_current_face(),
                       image_dither(), (int)libview_current());
    SceUID fd = sceIoOpen(g_cfg_path,
                          PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);

    if (fd < 0) return;
    if (len > 0) sceIoWrite(fd, buf, len);
    sceIoClose(fd);
}
