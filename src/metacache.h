#ifndef METACACHE_H
#define METACACHE_H


typedef struct {
    char title[128];
    char artist[128];
    char album[128];
    int  track_no;
    int  year;
    int  duration_sec;
} TrackMeta;

void metacache_init(const char *eboot_argv0);
void metacache_load(void);
void metacache_save(void);
void metacache_free(void);

int  metacache_get(const char *path, TrackMeta *out);
void metacache_put(const char *path, const TrackMeta *in);

#endif
