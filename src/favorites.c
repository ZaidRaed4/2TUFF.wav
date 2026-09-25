#include <pspiofilemgr.h>
#include <string.h>
#include <stdlib.h>

#include "favorites.h"

#define FV_MAGIC   0x56465432u  
#define FV_VERSION 1u
#define FV_PATH    512
#define FV_STR     128
#define FV_MAX     4000

typedef struct {
    char path[FV_PATH];
    char title[FV_STR];
    char artist[FV_STR];
    int  duration;
} FavEntry;

static FavEntry *g_fav   = NULL;
static int       g_count = 0;
static int       g_cap   = 0;
static int      *g_hash  = NULL;   /* pow2; holds index+1 (0 = empty) */
static int       g_hcap  = 0;
static int       g_dirty = 0;
static int       g_ready = 0;
static char      g_file[256] = "ms0:/PSP/GAME/2TUFFwav/favorites.dat";

static void scopy(char *dst, int sz, const char *src)
{
    int i = 0;
    if (sz <= 0) return;
    while (src[i] && i < sz - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static unsigned int fnv_str(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static void hash_add(int idx)
{
    unsigned int h;
    if (!g_hash || g_hcap == 0) return;
    h = fnv_str(g_fav[idx].path) & (unsigned int)(g_hcap - 1);
    while (g_hash[h]) h = (h + 1) & (unsigned int)(g_hcap - 1);
    g_hash[h] = idx + 1;
}

static void hash_rebuild(int newcap)
{
    int i;
    free(g_hash);
    g_hash = (int *)calloc((size_t)newcap, sizeof(int));
    g_hcap = g_hash ? newcap : 0;
    if (!g_hash) return;
    for (i = 0; i < g_count; i++) hash_add(i);
}

static int hash_find(const char *path)
{
    unsigned int h;
    if (!g_hash || g_hcap == 0) return -1;
    h = fnv_str(path) & (unsigned int)(g_hcap - 1);
    while (g_hash[h]) {
        int idx = g_hash[h] - 1;
        if (strcmp(g_fav[idx].path, path) == 0) return idx;
        h = (h + 1) & (unsigned int)(g_hcap - 1);
    }
    return -1;
}

static int ensure_cap(void)
{
    if (g_count >= g_cap) {
        int ncap = g_cap ? g_cap * 2 : 64;
        FavEntry *n = (FavEntry *)realloc(g_fav, (size_t)ncap * sizeof(FavEntry));
        if (!n) return 0;
        g_fav = n; g_cap = ncap;
    }
    if (g_hcap < (g_count + 1) * 2) {
        int hc = g_hcap ? g_hcap : 128;
        while (hc < (g_count + 1) * 2) hc <<= 1;
        hash_rebuild(hc);
    }
    return 1;
}

typedef struct { const unsigned char *b; long n, p; int ok; } Rd;

static unsigned int rd32(Rd *r)
{
    unsigned int v;
    if (!r->ok || r->p + 4 > r->n) { r->ok = 0; return 0; }
    v = (unsigned int)r->b[r->p] | ((unsigned int)r->b[r->p + 1] << 8) |
        ((unsigned int)r->b[r->p + 2] << 16) | ((unsigned int)r->b[r->p + 3] << 24);
    r->p += 4;
    return v;
}

static unsigned int rd16(Rd *r)
{
    unsigned int v;
    if (!r->ok || r->p + 2 > r->n) { r->ok = 0; return 0; }
    v = (unsigned int)r->b[r->p] | ((unsigned int)r->b[r->p + 1] << 8);
    r->p += 2;
    return v;
}

static void rdstr(Rd *r, char *out, int outsz)
{
    unsigned int len = rd16(r);
    unsigned int i;
    int cap;
    if (!r->ok || r->p + (long)len > r->n) { r->ok = 0; if (outsz) out[0] = '\0'; return; }
    for (i = 0; i < len && (int)i < outsz - 1; i++) out[i] = (char)r->b[r->p + i];
    cap = ((int)len < outsz - 1) ? (int)len : outsz - 1;
    out[cap] = '\0';
    r->p += len;
}

typedef struct { unsigned char *b; long n, cap; int ok; } Wr;

static void wneed(Wr *w, long extra)
{
    if (!w->ok) return;
    if (w->n + extra > w->cap) {
        long nc = w->cap ? w->cap * 2 : 4096;
        unsigned char *nb;
        while (nc < w->n + extra) nc *= 2;
        nb = (unsigned char *)realloc(w->b, (size_t)nc);
        if (!nb) { w->ok = 0; return; }
        w->b = nb; w->cap = nc;
    }
}

static void w32(Wr *w, unsigned int v)
{
    wneed(w, 4);
    if (!w->ok) return;
    w->b[w->n++] = (unsigned char)(v & 0xff);
    w->b[w->n++] = (unsigned char)((v >> 8) & 0xff);
    w->b[w->n++] = (unsigned char)((v >> 16) & 0xff);
    w->b[w->n++] = (unsigned char)((v >> 24) & 0xff);
}

static void w16(Wr *w, unsigned int v)
{
    wneed(w, 2);
    if (!w->ok) return;
    w->b[w->n++] = (unsigned char)(v & 0xff);
    w->b[w->n++] = (unsigned char)((v >> 8) & 0xff);
}

static void wstr(Wr *w, const char *s)
{
    int len = (int)strlen(s);
    if (len > 0xffff) len = 0xffff;
    w16(w, (unsigned int)len);
    wneed(w, len);
    if (!w->ok) return;
    memcpy(w->b + w->n, s, (size_t)len);
    w->n += len;
}

void favorites_init(const char *eboot)
{
    const char *slash;
    int dirlen;
    if (!eboot || !eboot[0]) return;
    slash = strrchr(eboot, '/');
    if (!slash) return;
    dirlen = (int)(slash - eboot);
    if (dirlen <= 0 || dirlen > (int)sizeof(g_file) - 20) return;
    memcpy(g_file, eboot, (size_t)dirlen);
    strcpy(g_file + dirlen, "/favorites.dat");
}

void favorites_load(void)
{
    SceIoStat st;
    SceUID fd;
    long fsize, got;
    unsigned char *buf;
    unsigned int magic, ver, cnt, i;
    Rd r;

    g_ready = 1;
    if (sceIoGetstat(g_file, &st) < 0) return;
    fsize = (long)st.st_size;
    if (fsize < 12) return;

    fd = sceIoOpen(g_file, PSP_O_RDONLY, 0777);
    if (fd < 0) return;
    buf = (unsigned char *)malloc((size_t)fsize);
    if (!buf) { sceIoClose(fd); return; }
    got = sceIoRead(fd, buf, fsize);
    sceIoClose(fd);
    if (got != fsize) { free(buf); return; }

    r.b = buf; r.n = fsize; r.p = 0; r.ok = 1;
    magic = rd32(&r); ver = rd32(&r); cnt = rd32(&r);
    if (!r.ok || magic != FV_MAGIC || ver != FV_VERSION || cnt > FV_MAX) {
        free(buf);
        return;
    }

    g_cap = (int)cnt < 64 ? 64 : (int)cnt;
    g_fav = (FavEntry *)malloc((size_t)g_cap * sizeof(FavEntry));
    if (!g_fav) { g_cap = 0; free(buf); return; }
    {
        int hc = 128;
        while (hc < g_cap * 2) hc <<= 1;
        g_hash = (int *)calloc((size_t)hc, sizeof(int));
        g_hcap = g_hash ? hc : 0;
    }

    for (i = 0; i < cnt && r.ok; i++) {
        char path[FV_PATH], ti[FV_STR], ar[FV_STR];
        int du;
        FavEntry *e;
        rdstr(&r, path, FV_PATH);
        rdstr(&r, ti, FV_STR);
        rdstr(&r, ar, FV_STR);
        du = (int)rd32(&r);
        if (!r.ok || !path[0]) continue;
        e = &g_fav[g_count];
        scopy(e->path, FV_PATH, path);
        scopy(e->title, FV_STR, ti);
        scopy(e->artist, FV_STR, ar);
        e->duration = du;
        hash_add(g_count);
        g_count++;
    }

    free(buf);
    g_dirty = 0;
}

void favorites_save(void)
{
    Wr w;
    SceUID fd;
    int i;

    if (!g_ready || !g_dirty) return;

    w.b = NULL; w.n = 0; w.cap = 0; w.ok = 1;
    w32(&w, FV_MAGIC); w32(&w, FV_VERSION); w32(&w, (unsigned int)g_count);
    for (i = 0; i < g_count; i++) {
        wstr(&w, g_fav[i].path);
        wstr(&w, g_fav[i].title);
        wstr(&w, g_fav[i].artist);
        w32(&w, (unsigned int)g_fav[i].duration);
    }
    if (!w.ok) { free(w.b); return; }

    fd = sceIoOpen(g_file, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, w.b, w.n);
        sceIoClose(fd);
        g_dirty = 0;
    }
    free(w.b);
}

void favorites_free(void)
{
    free(g_fav);  g_fav = NULL;  g_count = 0; g_cap = 0;
    free(g_hash); g_hash = NULL; g_hcap = 0;
    g_dirty = 0;  g_ready = 0;
}

int favorites_contains(const char *path)
{
    return hash_find(path) >= 0;
}

int favorites_count(void)
{
    return g_count;
}

int favorites_toggle(const Track *t)
{
    int idx;
    if (!g_ready || !t) return 0;

    idx = hash_find(t->path);
    if (idx >= 0) {
        int i;
        for (i = idx; i < g_count - 1; i++) g_fav[i] = g_fav[i + 1];
        g_count--;
        hash_rebuild(g_hcap ? g_hcap : 128);
        g_dirty = 1;
        return 0;
    }

    if (g_count >= FV_MAX) return 1;
    if (!ensure_cap()) return 0;
    idx = g_count++;
    scopy(g_fav[idx].path,   FV_PATH, t->path);
    scopy(g_fav[idx].title,  FV_STR,  t->title);
    scopy(g_fav[idx].artist, FV_STR,  t->artist);
    g_fav[idx].duration = t->duration_sec;
    hash_add(idx);
    g_dirty = 1;
    return 1;
}

void favorites_fill_record(Record *r)
{
    int i;
    memset(r, 0, sizeof(*r));
    scopy(r->name, NAME_LEN, "Favorites");
    scopy(r->artist, NAME_LEN, "PLAYLIST");
    r->is_playlist = 1;
    r->meta_loaded = 1;           /* titles/durations are already known */
    r->track_count = g_count;
    if (g_count <= 0) { r->tracks = NULL; return; }

    r->tracks = (Track *)calloc((size_t)g_count, sizeof(Track));
    if (!r->tracks) { r->track_count = 0; return; }
    for (i = 0; i < g_count; i++) {
        scopy(r->tracks[i].path,   MAX_PATH_LEN, g_fav[i].path);
        scopy(r->tracks[i].title,  NAME_LEN,     g_fav[i].title);
        scopy(r->tracks[i].artist, NAME_LEN,     g_fav[i].artist);
        r->tracks[i].track_no     = i + 1;
        r->tracks[i].duration_sec = g_fav[i].duration;
    }
}
