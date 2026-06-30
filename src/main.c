#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>
#include <string.h>

#include "app.h"
#include "gfx.h"
#include "text.h"
#include "theme.h"
#include "audio.h"
#include "library.h"
#include "config.h"

PSP_MODULE_INFO("2TUFFwav", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

static const char *const MUSIC_ROOTS[] = { "ms0:/MUSIC", "ef0:/MUSIC" };
#define NUM_MUSIC_ROOTS ((int)(sizeof(MUSIC_ROOTS) / sizeof(MUSIC_ROOTS[0])))

static volatile int g_running = 1;

static int exit_cb(int a1, int a2, void *p)
{
    (void)a1; (void)a2; (void)p;
    g_running = 0;
    return 0;
}

static int cb_thread(SceSize args, void *argp)
{
    int id;
    (void)args; (void)argp;
    id = sceKernelCreateCallback("Exit Callback", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int th = sceKernelCreateThread("update_thread", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}

int main(int argc, char *argv[])
{
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    gfx_init();
    text_init();
    if (audio_init() < 0) {

    }
    config_init(argc > 0 ? argv[0] : 0);
    config_load();

    memset(&g_app, 0, sizeof(g_app));
    g_app.screen = SCREEN_LIBRARY;
    g_app.mode = MODE_ALBUMS;
    g_app.preview_for = -1;
    library_scan(&g_app.lib, MUSIC_ROOTS, NUM_MUSIC_ROOTS);

    while (g_running) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);
        g_held = pad.Buttons;
        g_pressed = g_held & ~g_app.btn_prev;   /* edge-detect: buttons pressed just this frame */
        g_app.btn_prev = g_held;

        g_app.time += gfx_dt();
        handle_auto_advance();

        gfx_begin_frame();
        gfx_draw_background(g_app.time);
        switch (g_app.screen) {
        case SCREEN_LIBRARY:    scr_library();    break;
        case SCREEN_RECORD:     scr_record();     break;
        case SCREEN_NOWPLAYING: scr_nowplaying(); break;
        }
        gfx_end_frame();
    }

    audio_shutdown();
#if LYRICS_ENABLED
    lyrics_free(&g_app.lyrics);
#endif
    if (g_app.preview_tex)   tex_free(g_app.preview_tex);
    if (g_app.rec_tex)       tex_free(g_app.rec_tex);
    if (g_app.rec_thumb_tex) tex_free(g_app.rec_thumb_tex);
    if (g_app.np_tex)        tex_free(g_app.np_tex);
    library_free(&g_app.lib);
    text_shutdown();
    gfx_shutdown();

    sceKernelExitGame();
    return 0;
}
