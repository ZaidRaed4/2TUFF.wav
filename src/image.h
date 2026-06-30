#ifndef IMAGE_H
#define IMAGE_H

#include "gfx.h"

Texture *image_load_cover_file(const char *path, int size);
Texture *image_load_cover_mem(const unsigned char *buf, unsigned long len,
                              int size);

Texture *image_load_jpeg_file(const char *path, int size);
Texture *image_load_jpeg_mem(const unsigned char *buf, unsigned long len,
                             int size);
Texture *image_make_placeholder(int size, unsigned int seed);

void image_set_dither(int on);
int  image_dither(void);

#endif
