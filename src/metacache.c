#include <pspiofilemgr.h>
#include <string.h>
#include <stdlib.h>

#include "metacache.h"

#define MC_MAGIC   0x434D5432u 
#define MC_VERSION 1u
#define MC_PATH    512
#define MC_STR     128
#define MC_MAX     8000         

typedef struct {
    char path[MC_PATH];
    unsigned int size;
    unsigned int mtime;
    char title[MC_STR];
    char artist[MC_STR];
    char album[MC_STR];
    int  track_no;
    int  year;
    int  duration_sec;
} McEntry;

static McEntry *g_ent   = NULL;
static int      g_count = 0;
static int      g_cap   = 0;
static int     *g_hash  = NULL;  
static int      g_hcap  = 0;
static int      g_dirty = 0;
static int      g_ready = 0;
static char     g_file[256] = "ms0:/PSP/GAME/2TUFFwav/meta.cache";

static void scopy(char *dst, int sz, const char *src)
{
    int i = 0;
    if (sz <= 0) return;
    while (src[i] && i < sz - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static unsigned int fnv_bytes(const void *p, int n)
{
    const unsigned char *b = (const unsigned char *)p;
    unsigned int h = 2166136261u;
    int i;
    for (i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

static unsigned int fnv_str(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static int stat_key(const char *path, unsigned int *size, unsigned int *mtime)
{
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return 0;
    *size  = (unsigned int)st.st_size;
    *mtime = fnv_bytes(&st.sce_st_mtime, (int)sizeof(st.sce_st_mtime));
    return 1;
}


static void hash_add(int idx)
{
    unsigned int h;
    if (!g_hash || g_hcap == 0) return;
    h = fnv_str(g_ent[idx].path) & (unsigned int)(g_hcap - 1);
    while (g_hash[h]) h = (h + 1) & (unsigned int)(g_hcap - 1);
    g_hash[h] = idx + 1;
}

static int hash_find(const char *path)
{
    unsigned int h;
    if (!g_hash || g_hcap == 0) return -1;
    h = fnv_str(path) & (unsigned int)(g_hcap - 1);
    while (g_hash[h]) {
        int idx = g_hash[h] - 1;
        if (strcmp(g_ent[idx].path, path) == 0) return idx;
        h = (h + 1) & (unsigned int)(g_hcap - 1);
    }
    return -1;
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

static int ensure_cap(void)
{
    if (g_count < g_cap) {
        if (g_hcap < (g_count + 1) * 2) {
            int hc = g_hcap ? g_hcap : 512;
            while (hc < (g_count + 1) * 2) hc <<= 1;
            hash_rebuild(hc);
        }
        return 1;
    }
    {
        int ncap = g_cap ? g_cap * 2 : 256;
        McEntry *n = (McEntry *)realloc(g_ent, (size_t)ncap * sizeof(McEntry));
        if (!n) return 0;
        g_ent = n; g_cap = ncap;
    }
    {
        int hc = g_hcap ? g_hcap : 512;
        while (hc < g_cap * 2) hc <<= 1;
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
        long nc = w->cap ? w->cap * 2 : 8192;
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


void metacache_init(const char *eboot)
{
    const char *slash;
    int dirlen;
    if (!eboot || !eboot[0]) return;
    slash = strrchr(eboot, '/');
    if (!slash) return;
    dirlen = (int)(slash - eboot);
    if (dirlen <= 0 || dirlen > (int)sizeof(g_file) - 16) return;
    memcpy(g_file, eboot, (size_t)dirlen);
    strcpy(g_file + dirlen, "/meta.cache");
}

void metacache_load(void)
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
    if (!r.ok || magic != MC_MAGIC || ver != MC_VERSION || cnt > MC_MAX) {
        free(buf);
        return;
    }

    g_cap = (int)cnt < 256 ? 256 : (int)cnt;
    g_ent = (McEntry *)malloc((size_t)g_cap * sizeof(McEntry));
    if (!g_ent) { g_cap = 0; free(buf); return; }
    {
        int hc = 512;
        while (hc < g_cap * 2) hc <<= 1;
        g_hash = (int *)calloc((size_t)hc, sizeof(int));
        g_hcap = g_hash ? hc : 0;
    }

    for (i = 0; i < cnt && r.ok; i++) {
        char path[MC_PATH], ti[MC_STR], ar[MC_STR], al[MC_STR];
        unsigned int size, mtime;
        int tn, yr, du;
        McEntry *e;

        rdstr(&r, path, MC_PATH);
        size = rd32(&r); mtime = rd32(&r);
        rdstr(&r, ti, MC_STR); rdstr(&r, ar, MC_STR); rdstr(&r, al, MC_STR);
        tn = (int)rd32(&r); yr = (int)rd32(&r); du = (int)rd32(&r);
        if (!r.ok || !path[0]) continue;

        e = &g_ent[g_count];
        scopy(e->path, MC_PATH, path);
        e->size = size; e->mtime = mtime;
        scopy(e->title, MC_STR, ti);
        scopy(e->artist, MC_STR, ar);
        scopy(e->album, MC_STR, al);
        e->track_no = tn; e->year = yr; e->duration_sec = du;
        hash_add(g_count);
        g_count++;
    }

    free(buf);
    g_dirty = 0;
}

void metacache_save(void)
{
    Wr w;
    SceUID fd;
    int i;

    if (!g_ready || !g_dirty) return;

    w.b = NULL; w.n = 0; w.cap = 0; w.ok = 1;
    w32(&w, MC_MAGIC); w32(&w, MC_VERSION); w32(&w, (unsigned int)g_count);
    for (i = 0; i < g_count; i++) {
        wstr(&w, g_ent[i].path);
        w32(&w, g_ent[i].size);
        w32(&w, g_ent[i].mtime);
        wstr(&w, g_ent[i].title);
        wstr(&w, g_ent[i].artist);
        wstr(&w, g_ent[i].album);
        w32(&w, (unsigned int)g_ent[i].track_no);
        w32(&w, (unsigned int)g_ent[i].year);
        w32(&w, (unsigned int)g_ent[i].duration_sec);
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

void metacache_free(void)
{
    free(g_ent);  g_ent = NULL;  g_count = 0; g_cap = 0;
    free(g_hash); g_hash = NULL; g_hcap = 0;
    g_dirty = 0;  g_ready = 0;
}

int metacache_get(const char *path, TrackMeta *out)
{
    unsigned int size, mtime;
    int idx;
    if (!g_ready) return 0;
    if (!stat_key(path, &size, &mtime)) return 0;
    idx = hash_find(path);
    if (idx < 0) return 0;
    if (g_ent[idx].size != size || g_ent[idx].mtime != mtime) return 0;

    scopy(out->title,  (int)sizeof(out->title),  g_ent[idx].title);
    scopy(out->artist, (int)sizeof(out->artist), g_ent[idx].artist);
    scopy(out->album,  (int)sizeof(out->album),  g_ent[idx].album);
    out->track_no     = g_ent[idx].track_no;
    out->year         = g_ent[idx].year;
    out->duration_sec = g_ent[idx].duration_sec;
    return 1;
}

void metacache_put(const char *path, const TrackMeta *in)
{
    unsigned int size, mtime;
    int idx;
    if (!g_ready) return;
    if ((int)strlen(path) >= MC_PATH) return;
    if (!stat_key(path, &size, &mtime)) return;

    idx = hash_find(path);
    if (idx < 0) {
        if (g_count >= MC_MAX) return;
        if (!ensure_cap()) return;
        idx = g_count++;
        scopy(g_ent[idx].path, MC_PATH, path);
        hash_add(idx);
    }
    g_ent[idx].size  = size;
    g_ent[idx].mtime = mtime;
    scopy(g_ent[idx].title,  MC_STR, in->title);
    scopy(g_ent[idx].artist, MC_STR, in->artist);
    scopy(g_ent[idx].album,  MC_STR, in->album);
    g_ent[idx].track_no     = in->track_no;
    g_ent[idx].year         = in->year;
    g_ent[idx].duration_sec = in->duration_sec;
    g_dirty = 1;
}
