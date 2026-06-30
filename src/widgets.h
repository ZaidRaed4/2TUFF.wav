#ifndef WIDGETS_H
#define WIDGETS_H

#include "text.h"

void ui_statusbar(const char *left, const char *right);
void ui_footer(const char *left, const char *right);

void ui_rule(int x, int y, int w);
void ui_vrule(int x, int y, int h);
void ui_dotrule(int x, int y, int w);

void ui_meter(Font f, int x, int y, int cells, float frac,
              unsigned int on, unsigned int off);
void ui_bar_px(int x, int y, int w, int h, float frac,
               unsigned int on, unsigned int off);

void ui_grid(Font f, int x, int y, int cols, int rows, char glyph, unsigned int col);

void ui_kv(int x, int y, int valx, const char *key, const char *val);
void ui_kv_right(int x, int y, int x_right, const char *key, const char *val);

void ui_frame(int x, int y, int w, int h);

int  ui_chip(Font f, int x, int y, const char *s, unsigned int fill, unsigned int ink);

void ui_folder_icon(int x, int y, int s, unsigned int col);

#endif
