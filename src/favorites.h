#ifndef FAVORITES_H
#define FAVORITES_H

#include "library.h"

void favorites_init(const char *eboot_argv0);
void favorites_load(void);
void favorites_save(void);
void favorites_free(void);

int  favorites_contains(const char *path);
int  favorites_count(void);
int  favorites_toggle(const Track *t);

void favorites_fill_record(Record *r);

#endif
