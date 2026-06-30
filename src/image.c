#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <png.h>

#include "image.h"
#include "theme.h"

static int g_cover_dither = 1;
void image_set_dither(int on) { g_cover_dither = on ? 1 : 0; }
int  image_dither(void)       { return g_cover_dither; }

struct jerr_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};

static void jerr_exit(j_common_ptr cinfo)
{
    struct jerr_mgr *e = (struct jerr_mgr *)cinfo->err;
    longjmp(e->jb, 1);
}

static const unsigned char bayer8[8][8] = {
    {  0, 48, 12, 60,  3, 51, 15, 63 },
    { 32, 16, 44, 28, 35, 19, 47, 31 },
    {  8, 56,  4, 52, 11, 59,  7, 55 },
    { 40, 24, 36, 20, 43, 27, 39, 23 },
    {  2, 50, 14, 62,  1, 49, 13, 61 },
    { 34, 18, 46, 30, 33, 17, 45, 29 },
    { 10, 58,  6, 54,  9, 57,  5, 53 },
    { 42, 26, 38, 22, 41, 25, 37, 21 },
};

static void dither_tex(Texture *t)
{
    unsigned int ink = g_theme->cover_ink | 0xFF000000u;
    int invert = g_theme->cover_invert;
    int y, x;
    for (y = 0; y < t->h; y++) {
        for (x = 0; x < t->w; x++) {
            unsigned int px = t->data[y * t->tw + x];
            int r = px & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF;
            int L = (r * 77 + g * 150 + b * 29) >> 8;
            int thr = bayer8[y & 7][x & 7] * 4 + 2;
            int on = invert ? (L > thr) : ((255 - L) > thr);
            t->data[y * t->tw + x] = on ? ink : 0u;
        }
    }
}

static Texture *finish_decode(unsigned char *rgb, int dw, int dh, int size)
{
    Texture *t;
    int crop, ox, oy, y, x;

    crop = (dw < dh) ? dw : dh;
    if (crop <= 0) return NULL;
    ox = (dw - crop) / 2;
    oy = (dh - crop) / 2;

    t = tex_create(size, size);
    if (!t) return NULL;

    for (y = 0; y < size; y++) {
        int sy0 = oy + (int)((long long)y       * crop / size);
        int sy1 = oy + (int)((long long)(y + 1) * crop / size);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > dh)   sy1 = dh;
        for (x = 0; x < size; x++) {
            int sx0 = ox + (int)((long long)x       * crop / size);
            int sx1 = ox + (int)((long long)(x + 1) * crop / size);
            unsigned int sr = 0, sg = 0, sb = 0, cnt = 0;
            int yy, xx;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > dw)   sx1 = dw;
            for (yy = sy0; yy < sy1; yy++) {
                const unsigned char *row = rgb + (size_t)yy * dw * 3;
                for (xx = sx0; xx < sx1; xx++) {
                    const unsigned char *p = row + (size_t)xx * 3;
                    sr += p[0]; sg += p[1]; sb += p[2]; cnt++;
                }
            }
            if (cnt == 0) cnt = 1;
            t->data[y * t->tw + x] = RGBA(sr / cnt, sg / cnt, sb / cnt, 255);
        }
    }
    if (g_cover_dither) dither_tex(t);
    tex_upload(t);
    return t;
}

static Texture *decode_common(struct jpeg_decompress_struct *cinfo, int size)
{
    unsigned char *rgb = NULL;
    Texture *tex = NULL;
    int dw, dh, denom, row_stride;
    JSAMPROW rowptr[1];

    cinfo->out_color_space = JCS_RGB;

    jpeg_calc_output_dimensions(cinfo);
    denom = 1;
    while (denom < 8) {
        int ow = cinfo->image_width / (denom * 2);
        int oh = cinfo->image_height / (denom * 2);
        if (ow < size || oh < size) break;
        denom *= 2;
    }
    cinfo->scale_num = 1;
    cinfo->scale_denom = denom;

    jpeg_start_decompress(cinfo);
    dw = cinfo->output_width;
    dh = cinfo->output_height;
    if (cinfo->output_components != 3 || dw <= 0 || dh <= 0) {
        jpeg_abort_decompress(cinfo);
        return NULL;
    }

    row_stride = dw * 3;
    rgb = (unsigned char *)malloc((size_t)row_stride * dh);
    if (!rgb) { jpeg_abort_decompress(cinfo); return NULL; }

    while ((int)cinfo->output_scanline < dh) {
        rowptr[0] = rgb + (size_t)cinfo->output_scanline * row_stride;
        jpeg_read_scanlines(cinfo, rowptr, 1);
    }
    jpeg_finish_decompress(cinfo);

    tex = finish_decode(rgb, dw, dh, size);
    free(rgb);
    return tex;
}

Texture *image_load_jpeg_file(const char *path, int size)
{
    struct jpeg_decompress_struct cinfo;
    struct jerr_mgr jerr;
    Texture *tex = NULL;
    FILE *fp;

    if (!path || !path[0]) return NULL;
    fp = fopen(path, "rb");
    if (!fp) return NULL;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jerr_exit;
    if (setjmp(jerr.jb)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    tex = decode_common(&cinfo, size);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return tex;
}

Texture *image_load_jpeg_mem(const unsigned char *buf, unsigned long len,
                             int size)
{
    struct jpeg_decompress_struct cinfo;
    struct jerr_mgr jerr;
    Texture *tex = NULL;

    if (!buf || len == 0) return NULL;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jerr_exit;
    if (setjmp(jerr.jb)) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)buf, len);
    jpeg_read_header(&cinfo, TRUE);
    tex = decode_common(&cinfo, size);
    jpeg_destroy_decompress(&cinfo);
    return tex;
}

static Texture *png_decode(png_image *img, int size)
{
    unsigned char *rgb;
    Texture *t;

    img->format = PNG_FORMAT_RGB;
    rgb = (unsigned char *)malloc(PNG_IMAGE_SIZE(*img));
    if (!rgb) { png_image_free(img); return NULL; }

    if (!png_image_finish_read(img, NULL, rgb, 0, NULL)) {
        free(rgb); png_image_free(img); return NULL;
    }
    t = finish_decode(rgb, (int)img->width, (int)img->height, size);
    free(rgb);
    png_image_free(img);
    return t;
}

static Texture *image_load_png_mem(const unsigned char *buf, unsigned long len,
                                   int size)
{
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, buf, len)) return NULL;
    return png_decode(&img, size);
}

static Texture *image_load_png_file(const char *path, int size)
{
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, path)) return NULL;
    return png_decode(&img, size);
}

Texture *image_load_cover_mem(const unsigned char *buf, unsigned long len,
                              int size)
{
    if (!buf || len < 4) return NULL;
    if (buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G')
        return image_load_png_mem(buf, len, size);
    if (buf[0] == 0xFF && buf[1] == 0xD8)
        return image_load_jpeg_mem(buf, len, size);
    return NULL;
}

Texture *image_load_cover_file(const char *path, int size)
{
    unsigned char sig[4];
    FILE *fp;
    size_t n;

    if (!path || !path[0]) return NULL;
    fp = fopen(path, "rb");
    if (!fp) return NULL;
    n = fread(sig, 1, 4, fp);
    fclose(fp);

    if (n >= 4 && sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G')
        return image_load_png_file(path, size);
    return image_load_jpeg_file(path, size);
}

Texture *image_make_placeholder(int size, unsigned int seed)
{
    Texture *t = tex_create(size, size);
    float cx, cy, rmax, pitch;
    int y, x;

    if (!t) return NULL;
    cx = size * 0.5f;
    cy = size * 0.5f;
    rmax = size * 0.5f;
    pitch = 0.7f + (float)(seed & 7) * 0.12f;

    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            float dx = x + 0.5f - cx;
            float dy = y + 0.5f - cy;
            float d  = sqrtf(dx * dx + dy * dy);
            float dn = d / rmax;
            float ring = 0.5f + 0.5f * sinf(d * pitch);
            float base = 1.0f - dn * 0.78f;
            float v;
            int L;
            if (base < 0.0f) base = 0.0f;
            v = base * 0.70f + ring * 0.30f;
            if (d < size * 0.045f) v = 0.05f;
            L = (int)(34.0f + 196.0f * v);
            if (L < 18) L = 18;
            if (L > 232) L = 232;
            t->data[y * t->tw + x] = RGBA(L, L, L, 255);
        }
    }
    if (g_cover_dither) dither_tex(t);
    tex_upload(t);
    return t;
}
