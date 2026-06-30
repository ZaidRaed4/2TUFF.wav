#include <pspkernel.h>
#include <pspthreadman.h>
#include <pspaudio.h>
#include <pspmp3.h>
#include <pspiofilemgr.h>
#include <psputility.h>

#include <malloc.h>
#include <string.h>
#include <stdio.h>

#include "audio.h"

#define MP3_BUF_SIZE (16 * 1024)
#define PCM_BUF_SIZE (9216)

enum { REQ_NONE = 0, REQ_PLAY, REQ_STOP, REQ_PAUSE, REQ_RESUME };

static SceUID  g_lock;
static SceUID  g_thread = -1;
static volatile int g_thread_run = 1;

static volatile int g_req = REQ_NONE;
static char         g_req_path[512];
static volatile int g_req_total = 0;

static volatile int g_seek_req = 0;
static volatile int g_seek_ms  = 0;

static volatile int g_state = AUDIO_STOPPED;
static volatile int g_elapsed_ms = 0;
static volatile int g_total_sec = 0;
static volatile int g_finished = 0;

static volatile int g_level_q = 0;
static volatile int g_bass_q  = 0;

static volatile int g_band_q[AUDIO_BANDS];

enum {
    AERR_NONE = 0, AERR_OPEN, AERR_RESERVE, AERR_INIT,
    AERR_RESRC, AERR_ALLOC, AERR_THREAD, AERR_SRC
};
static volatile int g_err_where = AERR_NONE;
static volatile int g_err_code  = 0;

static void set_err(int where, int code) { g_err_code = code; g_err_where = where; }

static unsigned char *g_mp3buf = NULL;
static unsigned char *g_pcmbuf = NULL;

static void lock(void)   { sceKernelWaitSema(g_lock, 1, NULL); }
static void unlock(void) { sceKernelSignalSema(g_lock, 1); }

static int g_src_spc  = 0;
static int g_src_rate = 0;

static int src_ensure(int spc, int rate)
{
    int r, tries, drain_us, old_rate;
    if (g_src_spc == spc && g_src_rate == rate) return 0;

    if (g_src_spc > 0) {

        old_rate = g_src_rate > 0 ? g_src_rate : 44100;
        drain_us = (int)((long long)g_src_spc * 2 * 1000000 / old_rate) + 20000;
        sceKernelDelayThread(drain_us);
        sceAudioSRCChRelease();
    }
    g_src_spc = 0; g_src_rate = 0;

    for (tries = 0; tries < 10; tries++) {
        r = sceAudioSRCChReserve(spc, rate, 2);
        if (r >= 0) { g_src_spc = spc; g_src_rate = rate; return 0; }
        sceKernelDelayThread(8000);
    }
    return r;
}

static int is_frame_header(const unsigned char *h)
{
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    if (((h[1] >> 3) & 3) == 1) return 0;
    if (((h[1] >> 1) & 3) == 0) return 0;
    if (((h[2] >> 4) & 0xF) == 0xF) return 0;
    if (((h[2] >> 2) & 3) == 3) return 0;
    return 1;
}

static int find_frame_sync(SceUID fd, int from, int end)
{
    unsigned char win[2048];
    int n, i;
    if (from < 0) from = 0;
    sceIoLseek32(fd, from, PSP_SEEK_SET);
    n = sceIoRead(fd, win, sizeof(win));
    if (n < 4) return from;
    if (n > end - from) n = end - from;
    for (i = 0; i + 4 <= n; i++)
        if (is_frame_header(win + i)) return from + i;
    return from;
}

#define BAND_LP (AUDIO_BANDS - 1)

static const float BAND_A[BAND_LP] = {
    0.008f, 0.013f, 0.020f, 0.030f, 0.045f, 0.066f, 0.098f, 0.139f,
    0.192f, 0.269f, 0.366f, 0.481f, 0.604f, 0.723f, 0.819f
};

static const float BAND_GAIN[AUDIO_BANDS] = {
     5.0f,  6.0f,  7.5f,  9.0f, 11.0f, 13.0f, 16.0f, 19.0f,
    23.0f, 27.0f, 32.0f, 38.0f, 45.0f, 54.0f, 64.0f, 76.0f
};

static void publish_env(float lvl, float bass)
{
    float pl = g_level_q / 4096.0f, pb = g_bass_q / 4096.0f;
    pl += ((lvl  > pl) ? 0.50f : 0.12f) * (lvl  - pl);
    pb += ((bass > pb) ? 0.60f : 0.10f) * (bass - pb);
    g_level_q = (int)(pl * 4096.0f);
    g_bass_q  = (int)(pb * 4096.0f);
}

static void publish_bands(const float *band)
{
    int k;
    for (k = 0; k < AUDIO_BANDS; k++) {
        float p = g_band_q[k] / 4096.0f;
        p += ((band[k] > p) ? 0.55f : 0.14f) * (band[k] - p);
        g_band_q[k] = (int)(p * 4096.0f);
    }
}

static void analyze_pcm(const short *buf, int frames, float *lp, float *lpb)
{
    long sum = 0, bsum = 0;
    long bacc[AUDIO_BANDS];
    int i, k;
    float lpv = *lp;
    float lvl, bass, band[AUDIO_BANDS];
    if (frames <= 0) return;
    for (k = 0; k < AUDIO_BANDS; k++) bacc[k] = 0;
    for (i = 0; i < frames; i++) {
        int   m = ((int)buf[2 * i] + (int)buf[2 * i + 1]) >> 1;
        int   a = m < 0 ? -m : m;
        float fm = (float)m, d;
        float b;
        sum += a;
        lpv += 0.018f * (fm - lpv);
        b = lpv < 0 ? -lpv : lpv;
        bsum += (long)b;

        for (k = 0; k < BAND_LP; k++) {
            lpb[k] += BAND_A[k] * (fm - lpb[k]);
            d = (k == 0) ? lpb[0] : (lpb[k] - lpb[k - 1]);
            bacc[k] += (long)(d < 0 ? -d : d);
        }
        d = fm - lpb[BAND_LP - 1];
        bacc[BAND_LP] += (long)(d < 0 ? -d : d);
    }
    *lp = lpv;
    lvl  = (float)sum  / frames / 32768.0f * 3.2f;
    bass = (float)bsum / frames / 32768.0f * 6.5f;
    if (lvl  > 1.0f) lvl  = 1.0f;
    if (bass > 1.0f) bass = 1.0f;
    publish_env(lvl, bass);
    for (k = 0; k < AUDIO_BANDS; k++) {
        float v = (float)bacc[k] / frames / 32768.0f * BAND_GAIN[k];
        band[k] = v > 1.0f ? 1.0f : v;
    }
    publish_bands(band);
}

static void play_file(const char *path, int total)
{
    float lpf = 0.0f;
    float lpb[BAND_LP] = {0};
    SceUID fd;
    int handle, rate, eof = 0;
    int stream_start = 0, stream_end;
    int spf;
    long long played = 0;

    unsigned char id3[10];
    SceMp3InitArg arg;

    fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0) { set_err(AERR_OPEN, fd); g_state = AUDIO_STOPPED; return; }

    stream_end = sceIoLseek32(fd, 0, PSP_SEEK_END);
    sceIoLseek32(fd, 0, PSP_SEEK_SET);

    if (sceIoRead(fd, id3, 10) == 10 &&
        id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3') {
        stream_start = 10 + (((int)(id3[6] & 0x7f) << 21) |
                             ((id3[7] & 0x7f) << 14) |
                             ((id3[8] & 0x7f) << 7) |
                             (id3[9] & 0x7f));

        if (id3[5] & 0x10) stream_start += 10;
    }
    if (stream_start < 0 || stream_start >= stream_end) stream_start = 0;

    stream_start = find_frame_sync(fd, stream_start, stream_end);

    memset(&arg, 0, sizeof(arg));
    arg.mp3StreamStart = stream_start;
    arg.mp3StreamEnd   = stream_end;
    arg.mp3Buf     = g_mp3buf;
    arg.mp3BufSize = MP3_BUF_SIZE;
    arg.pcmBuf     = g_pcmbuf;
    arg.pcmBufSize = PCM_BUF_SIZE;

    handle = sceMp3ReserveMp3Handle(&arg);
    if (handle < 0) {
        set_err(AERR_RESERVE, handle);
        sceIoClose(fd); g_state = AUDIO_STOPPED; return;
    }

    {
        SceUChar8 *dst; SceInt32 towrite, srcpos; int rd;
        if (sceMp3GetInfoToAddStreamData(handle, &dst, &towrite, &srcpos) >= 0) {
            sceIoLseek32(fd, srcpos, PSP_SEEK_SET);
            rd = sceIoRead(fd, dst, towrite);
            if (rd < 0) rd = 0;
            if (rd == 0 || srcpos + rd >= stream_end) eof = 1;
            sceMp3NotifyAddStreamData(handle, rd);
        }
    }

    {
        int ir = sceMp3Init(handle);
        if (ir < 0) {
            set_err(AERR_INIT, ir);
            sceMp3ReleaseMp3Handle(handle);
            sceIoClose(fd);
            g_state = AUDIO_STOPPED;
            return;
        }
    }

    rate = sceMp3GetSamplingRate(handle);
    if (rate <= 0) rate = 44100;
    spf = sceMp3GetMaxOutputSample(handle);
    if (spf <= 0) spf = 1152;

    sceMp3SetLoopNum(handle, 0);

    g_total_sec   = (total > 0) ? total : 0;
    g_elapsed_ms  = 0;
    g_finished    = 0;
    set_err(AERR_NONE, 0);
    g_state       = AUDIO_PLAYING;
    lock(); g_seek_req = 0; unlock();

    for (;;) {
        int req, do_seek, seek_ms;
        short *buf = NULL;
        int dec, spc;

        lock();
        req = g_req;
        if (req == REQ_STOP) { g_req = REQ_NONE; unlock(); break; }
        if (req == REQ_PLAY) { unlock(); break; }
        if (req == REQ_PAUSE)  { g_req = REQ_NONE; g_state = AUDIO_PAUSED; }
        else if (req == REQ_RESUME) { g_req = REQ_NONE; g_state = AUDIO_PLAYING; }
        do_seek = g_seek_req; seek_ms = g_seek_ms; g_seek_req = 0;
        unlock();

        if (do_seek) {

            long long tframe = (long long)seek_ms * rate / 1000 / spf;
            int frames = sceMp3GetFrameNum(handle);
            if (tframe < 0) tframe = 0;
            if (frames > 0 && tframe > frames - 1) tframe = frames - 1;
            if (sceMp3ResetPlayPositionByFrame(handle, (SceUInt32)tframe) >= 0) {
                played = tframe * spf;
                g_elapsed_ms = (int)(played * 1000 / rate);
                eof = 0;
            }
        }

        if (g_state == AUDIO_PAUSED) { sceKernelDelayThread(12000); continue; }

        while (sceMp3CheckStreamDataNeeded(handle) > 0) {
            SceUChar8 *dst; SceInt32 towrite, srcpos; int rd;
            if (sceMp3GetInfoToAddStreamData(handle, &dst, &towrite, &srcpos) < 0)
                break;
            sceIoLseek32(fd, srcpos, PSP_SEEK_SET);
            rd = sceIoRead(fd, dst, towrite);
            if (rd <= 0) { eof = 1; sceMp3NotifyAddStreamData(handle, 0); break; }
            sceMp3NotifyAddStreamData(handle, rd);

            if (srcpos + rd >= stream_end) eof = 1;
        }

        dec = sceMp3Decode(handle, &buf);
        if (dec <= 0) {

            if (eof) { g_finished = 1; break; }
            sceKernelDelayThread(2000);
            continue;
        }

        spc = dec / 4;
        if (spc <= 0) continue;
        analyze_pcm(buf, spc, &lpf, lpb);
        {
            int sr = src_ensure(spc, rate);
            if (sr < 0) { set_err(AERR_SRC, sr); sceKernelDelayThread(2000); continue; }
        }
        sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, buf);

        played += spc;
        g_elapsed_ms = (int)(played * 1000 / rate);
    }

    sceMp3ReleaseMp3Handle(handle);
    sceIoClose(fd);
    g_state = AUDIO_STOPPED;
    g_level_q = 0; g_bass_q = 0;
    { int k; for (k = 0; k < AUDIO_BANDS; k++) g_band_q[k] = 0; }
}

static int audio_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    while (g_thread_run) {
        int req = REQ_NONE;
        char path[512];
        int total = 0;

        lock();
        if (g_req == REQ_PLAY) {
            req = REQ_PLAY;
            g_req = REQ_NONE;
            strncpy(path, g_req_path, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
            total = g_req_total;
        } else if (g_req == REQ_STOP) {
            g_req = REQ_NONE;
        }
        unlock();

        if (req == REQ_PLAY)
            play_file(path, total);
        else
            sceKernelDelayThread(15000);
    }
    return 0;
}

int audio_init(void)
{
    int r;

    sceUtilityLoadModule(PSP_MODULE_AV_MPEGBASE);
    sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    sceUtilityLoadModule(PSP_MODULE_AV_MP3);

    r = sceMp3InitResource();
    if (r < 0) {

        sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
        r = sceMp3InitResource();
        if (r < 0) { set_err(AERR_RESRC, r); return -1; }
    }

    g_mp3buf = (unsigned char *)memalign(64, MP3_BUF_SIZE);
    g_pcmbuf = (unsigned char *)memalign(64, PCM_BUF_SIZE);
    if (!g_mp3buf || !g_pcmbuf) { set_err(AERR_ALLOC, 0); return -1; }

    g_lock = sceKernelCreateSema("audio_lock", 0, 1, 1, NULL);
    if (g_lock < 0) { set_err(AERR_THREAD, g_lock); return -1; }

    g_thread_run = 1;
    g_thread = sceKernelCreateThread("audio_thread", audio_thread,
                                     0x12, 0x10000, 0, NULL);
    if (g_thread < 0) { set_err(AERR_THREAD, g_thread); return -1; }
    sceKernelStartThread(g_thread, 0, NULL);
    return 0;
}

void audio_shutdown(void)
{
    lock(); g_req = REQ_STOP; unlock();
    g_thread_run = 0;
    if (g_thread >= 0) {
        sceKernelWaitThreadEnd(g_thread, NULL);
        sceKernelDeleteThread(g_thread);
        g_thread = -1;
    }
    if (g_lock >= 0) { sceKernelDeleteSema(g_lock); g_lock = -1; }

    if (g_src_spc > 0) { sceAudioSRCChRelease(); g_src_spc = 0; g_src_rate = 0; }
    sceMp3TermResource();
    free(g_mp3buf); g_mp3buf = NULL;
    free(g_pcmbuf); g_pcmbuf = NULL;
}

void audio_play_file(const char *path, int total_sec)
{
    lock();
    g_req = REQ_PLAY;
    strncpy(g_req_path, path, sizeof(g_req_path) - 1);
    g_req_path[sizeof(g_req_path) - 1] = '\0';
    g_req_total = total_sec;
    g_finished = 0;
    unlock();
}

void audio_pause(void)
{
    lock();
    if (g_state == AUDIO_PLAYING) g_req = REQ_PAUSE;
    unlock();
}

void audio_resume(void)
{
    lock();
    if (g_state == AUDIO_PAUSED) g_req = REQ_RESUME;
    unlock();
}

void audio_toggle_pause(void)
{
    int st = g_state;
    if (st == AUDIO_PLAYING) audio_pause();
    else if (st == AUDIO_PAUSED) audio_resume();
}

void audio_stop(void)
{
    lock(); g_req = REQ_STOP; unlock();
}

void audio_seek(int target_ms)
{
    if (target_ms < 0) target_ms = 0;
    lock();
    if (g_state == AUDIO_PLAYING || g_state == AUDIO_PAUSED) {
        g_seek_ms  = target_ms;
        g_seek_req = 1;
    }
    unlock();
}

AudioState audio_state(void)   { return (AudioState)g_state; }
int audio_total_sec(void)      { return g_total_sec; }
int audio_elapsed_sec(void)    { return g_elapsed_ms / 1000; }
int audio_elapsed_ms(void)     { return g_elapsed_ms; }

float audio_progress(void)
{
    int tot = g_total_sec;
    if (tot <= 0) return 0.0f;
    {
        float p = (float)(g_elapsed_ms / 1000.0f) / (float)tot;
        if (p < 0) p = 0;
        if (p > 1) p = 1;
        return p;
    }
}

int  audio_finished(void)       { return g_finished; }
void audio_clear_finished(void) { g_finished = 0; }

float audio_level(void) { return g_level_q / 4096.0f; }
float audio_bass(void)  { return g_bass_q  / 4096.0f; }

int audio_bands(float *out, int n)
{
    int k;
    if (!out || n <= 0) return 0;
    if (n > AUDIO_BANDS) n = AUDIO_BANDS;
    for (k = 0; k < n; k++) out[k] = g_band_q[k] / 4096.0f;
    return n;
}

int audio_last_error(char *out, int n)
{
    int where = g_err_where;
    int code  = g_err_code;
    const char *label;

    if (!out || n <= 0) return 0;
    if (where == AERR_NONE) { out[0] = '\0'; return 0; }

    switch (where) {
        case AERR_OPEN:    label = "FILE OPEN FAILED";  break;
        case AERR_RESERVE: label = "MP3 HANDLE FAILED"; break;
        case AERR_INIT:    label = "MP3 NO FRAME SYNC"; break;
        case AERR_RESRC:   label = "CODEC INIT FAILED"; break;
        case AERR_ALLOC:   label = "OUT OF MEMORY";     break;
        case AERR_THREAD:  label = "AUDIO THREAD FAIL"; break;
        case AERR_SRC:     label = "AUDIO OUT FAILED";  break;
        default:           label = "AUDIO ERROR";       break;
    }
    if (code) snprintf(out, n, "%s (%08X)", label, (unsigned int)code);
    else      snprintf(out, n, "%s", label);
    return 1;
}
