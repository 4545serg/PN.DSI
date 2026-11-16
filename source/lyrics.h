/* PROJECT NOISE DSi - lyrics.h
 * Synced lyrics (.lrc). For "Song.mp3" the player looks for "Song.lrc" in the
 * same folder. Multiple timestamps per line and [mm:ss.xx] stamps are supported. */
#ifndef LYRICS_H
#define LYRICS_H

#include <stdbool.h>
#include <stdint.h>

/* Loads the .lrc next to mp3_path. Returns true if at least one line was read. */
bool ly_load(const char *mp3_path);

/* Number of loaded lyric lines (0 = no synced lyrics for this song). */
int ly_count(void);

/* Index of the line that is current at time cs (centiseconds), or -1 before the first line. */
int ly_index_at(uint32_t cs);

/* Text of line i (0 <= i < ly_count()). */
const char *ly_text(int i);

#endif
