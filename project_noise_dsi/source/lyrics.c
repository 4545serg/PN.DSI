/* PROJECT NOISE DSi - lyrics.c
 * LRC parser. Keeps synced lines only: "[mm:ss.xx]text". Lines with no
 * timestamp (such as [ar:Artist] tags) are skipped. Non-ASCII characters are
 * shown as '?' because the DS console font is ASCII only. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lyrics.h"

#define LY_MAX        512
#define LY_TEXT        64
#define LY_FILE_MAX 32768

typedef struct {
    uint32_t cs;               /* time in centiseconds */
    char text[LY_TEXT];
} LyLine;

static LyLine lines[LY_MAX];
static int nlines = 0;
static char filebuf[LY_FILE_MAX + 1];

/* Parses one "[mm:ss]" or "[mm:ss.xx]" stamp at *pp. Advances *pp past it on success. */
static bool parse_stamp(const char **pp, uint32_t *cs) {
    const char *p = *pp;
    if (*p != '[') return false;
    p++;
    if (!isdigit((unsigned char)*p)) return false;

    uint32_t mm = 0;
    while (isdigit((unsigned char)*p)) mm = mm * 10 + (uint32_t)(*p++ - '0');
    if (*p != ':') return false;
    p++;
    if (!isdigit((unsigned char)*p)) return false;

    uint32_t ss = 0;
    while (isdigit((unsigned char)*p)) ss = ss * 10 + (uint32_t)(*p++ - '0');

    uint32_t frac = 0;
    if (*p == '.' || *p == ':') {
        p++;
        int digits = 0;
        while (isdigit((unsigned char)*p)) {
            if (digits < 2) frac = frac * 10 + (uint32_t)(*p - '0');
            digits++;
            p++;
        }
        if (digits == 1) frac *= 10;   /* "[00:01.5]" means 50 cs */
    }
    if (*p != ']') return false;
    p++;

    *cs = (mm * 60 + ss) * 100 + frac;
    *pp = p;
    return true;
}

static void parse_line(char *s) {
    uint32_t stamps[8];
    int ns = 0;
    const char *q = s;

    while (*q == '[' && ns < 8) {
        uint32_t cs;
        if (!parse_stamp(&q, &cs)) break;
        stamps[ns++] = cs;
    }
    if (ns == 0) return;                       /* metadata or junk line */

    char text[LY_TEXT];
    int k = 0;
    for (; *q && k < LY_TEXT - 1; q++) {
        unsigned char c = (unsigned char)*q;
        if (c == '\r' || c < 32) continue;
        text[k++] = (c >= 127) ? '?' : (char)c;
    }
    text[k] = 0;

    for (int i = 0; i < ns && nlines < LY_MAX; i++) {
        lines[nlines].cs = stamps[i];
        memcpy(lines[nlines].text, text, LY_TEXT);
        nlines++;
    }
}

bool ly_load(const char *mp3_path) {
    nlines = 0;

    char path[320];
    snprintf(path, sizeof path, "%s", mp3_path);
    size_t len = strlen(path);
    if (len < 4) return false;
    strcpy(path + len - 4, ".lrc");

    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(filebuf, 1, LY_FILE_MAX, f);
    fclose(f);
    filebuf[got] = 0;

    char *p = filebuf;
    if (memcmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3;   /* UTF-8 BOM */

    while (*p && nlines < LY_MAX) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = 0;
        parse_line(p);
        if (!eol) break;
        p = eol + 1;
    }

    /* Sort by time (insertion sort: lists are small and mostly sorted already). */
    for (int i = 1; i < nlines; i++) {
        LyLine t = lines[i];
        int j = i - 1;
        while (j >= 0 && lines[j].cs > t.cs) {
            lines[j + 1] = lines[j];
            j--;
        }
        lines[j + 1] = t;
    }
    return nlines > 0;
}

int ly_count(void) { return nlines; }

int ly_index_at(uint32_t cs) {
    int idx = -1;
    for (int i = 0; i < nlines; i++) {
        if (lines[i].cs <= cs) idx = i;
        else break;
    }
    return idx;
}

const char *ly_text(int i) { return lines[i].text; }
