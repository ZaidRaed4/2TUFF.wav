#ifndef VIZ_H
#define VIZ_H

#define VIZ_ENABLED 1

#define VIZ_STYLE_FIELD 0
#define VIZ_STYLE_BLOBS 1
#define VIZ_STYLE_ASCII 2
#define VIZ_STYLE_COUNT 3

void viz_render(int x, int y, int w, int h, float dt,
                float level, float bass, float alpha, int style);

#endif
