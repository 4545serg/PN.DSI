/* PROJECT NOISE DSi - browser.c */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "browser.h"

BrEntry br_items[BR_MAX];
int br_count = 0;

static int cmp_entries(const void *a, const void *b) {
    const BrEntry *x = (const BrEntry *)a;
    const BrEntry *y = (const BrEntry *)b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static bool is_mp3(const char *name) {
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".mp3") == 0;
}

bool br_open(const char *dir) {
    br_count = 0;
    DIR *d = opendir(dir);
    if (!d) return false;

    struct dirent *e;
    while (br_count < BR_MAX && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;          /* skips . .. and hidden files */

        char full[BR_PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;
        bool isdir = S_ISDIR(st.st_mode);
        if (!isdir && !is_mp3(e->d_name)) continue;

        BrEntry *it = &br_items[br_count++];
        strncpy(it->name, e->d_name, BR_NAME_MAX - 1);
        it->name[BR_NAME_MAX - 1] = 0;
        it->dir = isdir;
    }
    closedir(d);

    qsort(br_items, (size_t)br_count, sizeof(BrEntry), cmp_entries);
    return true;
}
