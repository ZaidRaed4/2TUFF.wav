#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "id3.h"

#define MAX_TAG_BYTES (8 * 1024 * 1024)

static void strcpy_safe(char *dst, int dstsz, const char *src)
{
    int i = 0;
    while (src[i] && i < dstsz - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void copy_fixed(char *dst, int dstsz, const char *src, int n)
{
    int o = 0, i;
    for (i = 0; i < n && o < dstsz - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == 0) break;
        dst[o++] = (c >= 32 && c < 128) ? (char)c : '?';
    }
    while (o > 0 && dst[o - 1] == ' ') o--;
    dst[o] = '\0';
}

static int mime_is_jpeg(const char *s)
{

    int i;
    for (i = 0; s[i]; i++) {
        if (tolower((unsigned char)s[i]) == 'j' &&
            tolower((unsigned char)s[i + 1]) == 'p')
            return 1;
    }
    return 0;
}

static int mime_is_png(const char *s)
{

    int i;
    for (i = 0; s[i]; i++) {
        if (tolower((unsigned char)s[i]) == 'p' &&
            tolower((unsigned char)s[i + 1]) == 'n' &&
            tolower((unsigned char)s[i + 2]) == 'g')
            return 1;
    }
    return 0;
}

static void decode_text(int enc, const unsigned char *s, long len,
                        char *out, int outsz)
{
    int o = 0;
    long i = 0;

    if (len < 0) len = 0;

    if (enc == 1 || enc == 2) {
        int be = (enc == 2);
        if (enc == 1 && len >= 2) {
            if (s[0] == 0xFF && s[1] == 0xFE) { be = 0; i = 2; }
            else if (s[0] == 0xFE && s[1] == 0xFF) { be = 1; i = 2; }
        }
        for (; i + 1 < len && o < outsz - 1; i += 2) {
            unsigned int u = be ? ((s[i] << 8) | s[i + 1])
                                : ((s[i + 1] << 8) | s[i]);
            if (u == 0) break;
            if (u >= 32 && u < 128) out[o++] = (char)u;
            else if (u >= 128 && (o == 0 || out[o - 1] != '?')) out[o++] = '?';
        }
    } else {
        for (; i < len && o < outsz - 1; i++) {
            unsigned char c = s[i];
            if (c == 0) break;
            if (c >= 32 && c < 128) out[o++] = (char)c;
            else if (c >= 128 && (o == 0 || out[o - 1] != '?')) out[o++] = '?';
        }
    }
    out[o] = '\0';
    while (o > 0 && out[o - 1] == ' ') out[--o] = '\0';
}

static void extract_pic(const unsigned char *d, long fsize, int major,
                        ID3Tag *t, long fileoff)
{
    long i = 1;
    int enc = d[0];
    int is_img = 0;

    if (major == 2) {

        char fmt[4] = {0};
        if (fsize >= 4) { memcpy(fmt, d + 1, 3); i = 4; }
        if ((fmt[0] == 'J' || fmt[0] == 'j') && (fmt[1] == 'P' || fmt[1] == 'p'))
            is_img = 1;
        else if ((fmt[0] == 'P' || fmt[0] == 'p') && (fmt[1] == 'N' || fmt[1] == 'n'))
            is_img = 1;
    } else {
        char mime[48];
        int m = 0;
        while (i < fsize && d[i] != 0 && m < (int)sizeof(mime) - 1)
            mime[m++] = (char)d[i++];
        mime[m] = '\0';
        i++;
        is_img = mime_is_jpeg(mime) || mime_is_png(mime);
    }

    if (i < fsize) i++;

    if (enc == 1 || enc == 2) {
        while (i + 1 < fsize && !(d[i] == 0 && d[i + 1] == 0)) i += 2;
        i += 2;
    } else {
        while (i < fsize && d[i] != 0) i++;
        i++;
    }

    if (is_img && i < fsize) {
        t->has_art = 1;
        t->art_offset = fileoff + i;
        t->art_len = fsize - i;
    }
}

static void parse_v2(const unsigned char *tag, long size, int major,
                     ID3Tag *t, long body_start)
{
    long p = body_start;
    int idlen  = (major == 2) ? 3 : 4;
    int headlen = (major == 2) ? 6 : 10;

    while (p + headlen <= size) {
        char id[5] = {0};
        long fsize;
        long datap;
        const unsigned char *d;

        if (tag[p] == 0) break;
        memcpy(id, tag + p, idlen);

        if (major == 2)
            fsize = ((long)tag[p + 3] << 16) | ((long)tag[p + 4] << 8) | tag[p + 5];
        else if (major == 4)
            fsize = ((long)(tag[p + 4] & 0x7f) << 21) | ((tag[p + 5] & 0x7f) << 14) |
                    ((tag[p + 6] & 0x7f) << 7) | (tag[p + 7] & 0x7f);
        else
            fsize = ((long)tag[p + 4] << 24) | ((long)tag[p + 5] << 16) |
                    ((long)tag[p + 6] << 8) | tag[p + 7];

        datap = p + headlen;
        if (fsize <= 0 || datap + fsize > size) break;
        d = tag + datap;

        if (id[0] == 'T') {
            char val[128];
            decode_text(d[0], d + 1, fsize - 1, val, sizeof(val));
            if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) strcpy_safe(t->title, sizeof(t->title), val);
            else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) strcpy_safe(t->artist, sizeof(t->artist), val);
            else if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) strcpy_safe(t->album, sizeof(t->album), val);
            else if (!strcmp(id, "TRCK") || !strcmp(id, "TRK")) t->track = atoi(val);
            else if (!strcmp(id, "TYER") || !strcmp(id, "TYE") || !strcmp(id, "TDRC")) t->year = atoi(val);
        } else if (!strcmp(id, "APIC") || !strcmp(id, "PIC")) {
            if (!t->has_art) extract_pic(d, fsize, major, t, datap);
        }

        p = datap + fsize;
    }
}

static int parse_album_v2(const unsigned char *tag, long avail, int major,
                          long body_start, char *out, int outsz)
{
    long p = body_start;
    int idlen   = (major == 2) ? 3 : 4;
    int headlen = (major == 2) ? 6 : 10;

    while (p + headlen <= avail) {
        char id[5] = {0};
        long fsize, datap;

        if (tag[p] == 0) break;
        memcpy(id, tag + p, idlen);

        if (major == 2)
            fsize = ((long)tag[p + 3] << 16) | ((long)tag[p + 4] << 8) | tag[p + 5];
        else if (major == 4)
            fsize = ((long)(tag[p + 4] & 0x7f) << 21) | ((tag[p + 5] & 0x7f) << 14) |
                    ((tag[p + 6] & 0x7f) << 7) | (tag[p + 7] & 0x7f);
        else
            fsize = ((long)tag[p + 4] << 24) | ((long)tag[p + 5] << 16) |
                    ((long)tag[p + 6] << 8) | tag[p + 7];

        if (fsize <= 0) break;
        datap = p + headlen;

        if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) {
            long got = (datap + fsize <= avail) ? fsize : (avail - datap);
            if (got > 1) decode_text(tag[datap], tag + datap + 1, got - 1, out, outsz);
            return out[0] != 0;
        }
        if (datap + fsize > avail) break;
        p = datap + fsize;
    }
    return 0;
}

int id3_read_album(const char *path, char *out, int outsz)
{
    enum { CAP = 16 * 1024 };
    FILE *f;
    unsigned char h[10];
    int found = 0;

    if (outsz > 0) out[0] = '\0';
    f = fopen(path, "rb");
    if (!f) return 0;

    if (fread(h, 1, 10, f) == 10 && h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
        int major = h[3];
        int flags = h[5];
        long size = ((long)(h[6] & 0x7f) << 21) | ((h[7] & 0x7f) << 14) |
                    ((h[8] & 0x7f) << 7) | (h[9] & 0x7f);
        if (size > 0) {
            long cap = (size < CAP) ? size : CAP;
            unsigned char *tag = (unsigned char *)malloc(cap);
            if (tag) {
                long rd = (long)fread(tag, 1, cap, f);
                long body = 0;
                if ((flags & 0x40) && rd >= 4) {
                    if (major == 4)
                        body = ((long)(tag[0] & 0x7f) << 21) | ((tag[1] & 0x7f) << 14) |
                               ((tag[2] & 0x7f) << 7) | (tag[3] & 0x7f);
                    else
                        body = 4 + (((long)tag[0] << 24) | ((long)tag[1] << 16) |
                                    ((long)tag[2] << 8) | tag[3]);
                    if (body < 0 || body >= rd) body = 0;
                }
                if (parse_album_v2(tag, rd, major, body, out, outsz)) found = 1;
                free(tag);
            }
        }
    }

    if (!found) {
        long filesize;
        fseek(f, 0, SEEK_END);
        filesize = ftell(f);
        if (filesize >= 128) {
            unsigned char v1[128];
            fseek(f, filesize - 128, SEEK_SET);
            if (fread(v1, 1, 128, f) == 128 &&
                v1[0] == 'T' && v1[1] == 'A' && v1[2] == 'G') {
                copy_fixed(out, outsz, (char *)v1 + 63, 30);
                if (out[0]) found = 1;
            }
        }
    }

    fclose(f);
    return found;
}

int id3_read(const char *path, ID3Tag *out)
{
    FILE *f;
    unsigned char h[10];
    long filesize;
    int got = 0;

    memset(out, 0, sizeof(*out));
    f = fopen(path, "rb");
    if (!f) return 0;

    if (fread(h, 1, 10, f) == 10 && h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
        int major = h[3];
        int flags = h[5];
        long size = ((long)(h[6] & 0x7f) << 21) | ((h[7] & 0x7f) << 14) |
                    ((h[8] & 0x7f) << 7) | (h[9] & 0x7f);
        if (size > 0 && size < MAX_TAG_BYTES) {
            unsigned char *tag = (unsigned char *)malloc(size);
            if (tag && fread(tag, 1, size, f) == (size_t)size) {
                long body = 0;
                if (flags & 0x40) {
                    if (major == 4)
                        body = ((long)(tag[0] & 0x7f) << 21) | ((tag[1] & 0x7f) << 14) |
                               ((tag[2] & 0x7f) << 7) | (tag[3] & 0x7f);
                    else
                        body = 4 + (((long)tag[0] << 24) | ((long)tag[1] << 16) |
                                    ((long)tag[2] << 8) | tag[3]);
                    if (body < 0 || body >= size) body = 0;
                }

                parse_v2(tag, size, major, out, body);
                got = 1;
            }
            free(tag);
        }
    }

    fseek(f, 0, SEEK_END);
    filesize = ftell(f);
    if (filesize >= 128) {
        unsigned char v1[128];
        fseek(f, filesize - 128, SEEK_SET);
        if (fread(v1, 1, 128, f) == 128 &&
            v1[0] == 'T' && v1[1] == 'A' && v1[2] == 'G') {
            if (!out->title[0])  copy_fixed(out->title, sizeof(out->title), (char *)v1 + 3, 30);
            if (!out->artist[0]) copy_fixed(out->artist, sizeof(out->artist), (char *)v1 + 33, 30);
            if (!out->album[0])  copy_fixed(out->album, sizeof(out->album), (char *)v1 + 63, 30);
            if (!out->year) {
                char y[5]; memcpy(y, v1 + 93, 4); y[4] = '\0';
                out->year = atoi(y);
            }
            if (!out->track && v1[125] == 0 && v1[126] != 0)
                out->track = v1[126];
            got = 1;
        }
    }

    fclose(f);

    if (out->has_art) out->art_offset += 10;
    return got;
}
