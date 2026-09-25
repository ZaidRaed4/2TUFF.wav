#ifndef LIBRARY_H
#define LIBRARY_H

#define MAX_PATH_LEN 512
#define NAME_LEN     128

typedef struct {
    char path[MAX_PATH_LEN];
    char title[NAME_LEN];
    char artist[NAME_LEN];
    int  track_no;
    int  duration_sec;
} Track;

typedef struct {
    char name[NAME_LEN];
    char artist[NAME_LEN];
    char path[MAX_PATH_LEN];
    char cover_path[MAX_PATH_LEN];
    int  year;
    int  is_playlist;
    int  meta_loaded;
    Track *tracks;
    int  track_count;
} Record;

#define MAX_TREE_DEPTH 16
typedef struct FolderNode {
    char  name[NAME_LEN];
    char  path[MAX_PATH_LEN];
    struct FolderNode *children;
    int   child_count;
    Record *record;
} FolderNode;

typedef struct {
    Record *albums;     int album_count;
    Record *playlists;  int playlist_count;
    FolderNode *trees;  int tree_count;
    int scanned;
    int has_fav;        /* 1 when playlists[0] is the synthesized Favorites list */
} Library;

void library_init(Library *lib);

int  library_scan(Library *lib, const char *const *roots, int nroots);
void library_free(Library *lib);

void record_load_metadata(Record *rec);

#endif
