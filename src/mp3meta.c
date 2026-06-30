#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp3meta.h"

static const int br_v1l3[16] = {0, 32, 40, 48, 56, 64, 80, 96,
                                112, 128, 160, 192, 224, 256, 320, 0};
static const int br_v2l3[16] = {0, 8, 16, 24, 32, 40, 48, 56,
                                64, 80, 96, 112, 128, 144, 160, 0};

static const int sr_tab[4][3] = {
    {11025, 12000, 8000},
    {0, 0, 0},
    {22050, 24000, 16000},
    {44100, 48000, 32000}
};

int mp3_estimate_duration_sec(const char *path)
{
    FILE *f;
    long filesize, start = 0, region, got, i;
    unsigned char hdr[10];
    unsigned char *buf;
    int result = 0;

    f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    filesize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fread(hdr, 1, 10, f) == 10 &&
        hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
        /* ID3v2 tag size is a 28-bit sync-safe int (high bit of each byte is 0) */
        start = 10 + (((long)(hdr[6] & 0x7f) << 21) |
                      ((hdr[7] & 0x7f) << 14) |
                      ((hdr[8] & 0x7f) << 7) |
                      (hdr[9] & 0x7f));
    }
    if (start >= filesize) { fclose(f); return 0; }

    region = filesize - start;
    if (region > 64 * 1024) region = 64 * 1024;
    buf = (unsigned char *)malloc(region);
    if (!buf) { fclose(f); return 0; }

    fseek(f, start, SEEK_SET);
    got = (long)fread(buf, 1, region, f);
    fclose(f);

    for (i = 0; i + 4 <= got; i++) {
        int verbits, layer, bri, sri, chanmode, mpeg1, samplerate, bitrate, spf, sideinfo;
        long frames = 0, xo, vo;

        if (buf[i] != 0xFF || (buf[i + 1] & 0xE0) != 0xE0) continue;
        verbits  = (buf[i + 1] >> 3) & 3;
        layer    = (buf[i + 1] >> 1) & 3;
        bri      = (buf[i + 2] >> 4) & 0xF;
        sri      = (buf[i + 2] >> 2) & 3;
        chanmode = (buf[i + 3] >> 6) & 3;

        if (layer != 1 || verbits == 1 || sri == 3 || bri == 0 || bri == 15)
            continue;

        mpeg1      = (verbits == 3);
        samplerate = sr_tab[verbits][sri];
        bitrate    = mpeg1 ? br_v1l3[bri] : br_v2l3[bri];
        spf        = mpeg1 ? 1152 : 576;
        sideinfo   = mpeg1 ? (chanmode == 3 ? 17 : 32)
                           : (chanmode == 3 ? 9 : 17);

        xo = i + 4 + sideinfo;
        if (xo + 12 <= got &&
            (memcmp(buf + xo, "Xing", 4) == 0 || memcmp(buf + xo, "Info", 4) == 0)) {
            unsigned int flags = ((unsigned)buf[xo + 4] << 24) | (buf[xo + 5] << 16) |
                                 (buf[xo + 6] << 8) | buf[xo + 7];
            if (flags & 1)
                frames = ((long)buf[xo + 8] << 24) | (buf[xo + 9] << 16) |
                         (buf[xo + 10] << 8) | buf[xo + 11];
        } else {
            vo = i + 4 + 32;
            if (vo + 18 <= got && memcmp(buf + vo, "VBRI", 4) == 0)
                frames = ((long)buf[vo + 14] << 24) | (buf[vo + 15] << 16) |
                         (buf[vo + 16] << 8) | buf[vo + 17];
        }

        if (frames > 0 && samplerate > 0)
            result = (int)((double)frames * spf / samplerate + 0.5);
        else if (bitrate > 0)
            result = (int)((double)(filesize - start - i) * 8.0 /
                           (bitrate * 1000.0) + 0.5);
        break;
    }

    free(buf);
    return result;
}
