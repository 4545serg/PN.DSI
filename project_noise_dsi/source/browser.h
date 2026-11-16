/* PROJECT NOISE DSi - browser.h
 * Lists one folder at a time: sub-folders first, then .mp3 files, A-Z. */
#ifndef BROWSER_H
#define BROWSER_H

#include <stdbool.h>

#define BR_MAX       256   /* entries per folder (DSi memory limit) */
#define BR_NAME_MAX  128
#define BR_PATH_MAX  320

typedef struct {
    char name[BR_NAME_MAX];
    bool dir;
} BrEntry;

extern BrEntry br_items[BR_MAX];
extern int br_count;

/* Reads a folder into br_items. Returns false if the folder can't be opened. */
bool br_open(const char *dir);

#endif
