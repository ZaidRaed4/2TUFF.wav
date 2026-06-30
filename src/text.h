#ifndef TEXT_H
#define TEXT_H

typedef enum { F_SM = 0, F_LG = 1 } Font;
typedef enum { FACE_PLEX = 0, FACE_PIXEL = 1 } FontFace;

void text_init(void);
void text_shutdown(void);

void     text_set_face(FontFace face);
FontFace text_current_face(void);

int  font_cw(Font f);
int  font_ch(Font f);
int  text_w(Font f, const char *s);

void text_put(Font f, int x, int y, unsigned int col, const char *s);

void text_put_scaled(Font f, int x, int y, unsigned int col, const char *s, float scale);
void text_put_center(Font f, int cx, int y, unsigned int col, const char *s);
void text_put_right(Font f, int x_right, int y, unsigned int col, const char *s);

void text_put_clip(Font f, int x, int y, unsigned int col, const char *s, int maxw);

#endif
