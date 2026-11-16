/* PROJECT NOISE DSi - player.h */
#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PL_IDLE,      /* nothing loaded */
    PL_PLAYING,
    PL_PAUSED,    /* loaded, not playing (position kept) */
    PL_ENDED      /* reached the end of the track */
} PlayerState;

/* Opens an MP3 at start_ms. autoplay=false leaves it paused at that position. */
bool player_open(const char *path, uint32_t start_ms, bool autoplay);
void player_close(void);

/* Must be called once per frame: keeps the sound hardware fed. */
void player_update(void);

void player_toggle(void);                 /* play <-> pause (restarts if ended) */
void player_seek_ms(uint32_t ms);

PlayerState player_state(void);
uint32_t player_pos_ms(void);
uint32_t player_len_ms(void);

#endif
