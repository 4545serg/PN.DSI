/* PROJECT NOISE DSi - player.c
 *
 * MP3 is decoded on the ARM9 with minimp3 and played through the DS sound
 * hardware. Stereo is two mono hardware channels, hard-panned left and right.
 *
 * Audio is decoded in chunks of FRAMES_PER_CHUNK MP3 frames and double-buffered:
 * while one chunk plays, the other is decoded. update() must run every frame.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#include "player.h"

#define IN_BUF            16384
#define REFILL_BELOW       8192
#define FRAMES_PER_CHUNK      4
#define CHUNK_SAMPLES     (1152 * FRAMES_PER_CHUNK)   /* per channel */

static mp3dec_t dec;
static FILE *fp = NULL;
static uint8_t inbuf[IN_BUF];
static size_t in_len = 0, in_pos = 0;
static bool in_eof = false;
static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

static int16_t chunk_l[2][CHUNK_SAMPLES];
static int16_t chunk_r[2][CHUNK_SAMPLES];
static uint32_t slot_len[2];
static int cur_slot = 0;
static uint32_t cur_len = 0;
static int cur_l = -1, cur_r = -1;            /* hardware channel ids */

static uint32_t rate = 44100;
static uint32_t bitrate_kbps = 128;
static uint32_t data_start = 0, file_size = 0;
static uint64_t base_samples = 0;             /* samples before the last seek point */
static uint64_t started_since = 0;            /* samples started since the last seek */
static uint32_t paused_ms = 0;
static PlayerState state = PL_IDLE;

static bool chan_busy(int ch) {
    return ch >= 0 && (SCHANNEL_CR(ch) & SOUND_ENABLE);
}

static void kill_channels(void) {
    if (cur_l >= 0) soundKill(cur_l);
    if (cur_r >= 0) soundKill(cur_r);
    cur_l = cur_r = -1;
}

static void refill(void) {
    if (in_pos > 0) {
        memmove(inbuf, inbuf + in_pos, in_len - in_pos);
        in_len -= in_pos;
        in_pos = 0;
    }
    size_t n = fread(inbuf + in_len, 1, IN_BUF - in_len, fp);
    in_len += n;
    if (n == 0) in_eof = true;
}

/* Decodes the next MP3 frame into pcm[]. Returns samples per channel, 0 at end of file. */
static int next_frame(mp3dec_frame_info_t *info) {
    for (;;) {
        size_t rem = in_len - in_pos;
        if (rem < REFILL_BELOW && !in_eof) {
            refill();
            continue;
        }
        if (rem == 0) return 0;

        int s = mp3dec_decode_frame(&dec, inbuf + in_pos, (int)rem, pcm, info);
        if (info->frame_bytes > 0) {
            in_pos += (size_t)info->frame_bytes;
            if (s > 0) return s;
            continue;                     /* skipped data (tags etc.) */
        }
        in_pos++;                         /* not a frame here: resync one byte */
    }
}

/* Decodes up to FRAMES_PER_CHUNK frames into slot. Returns samples per channel. */
static uint32_t fill_chunk(int slot) {
    uint32_t n = 0;
    for (int f = 0; f < FRAMES_PER_CHUNK; f++) {
        mp3dec_frame_info_t info;
        int s = next_frame(&info);
        if (s <= 0) break;
        if (info.hz) rate = (uint32_t)info.hz;
        int ch = info.channels > 0 ? info.channels : 1;

        for (int i = 0; i < s; i++) {
            int16_t l = pcm[i * ch];
            int16_t r = (ch > 1) ? pcm[i * ch + 1] : l;
            chunk_l[slot][n + i] = l;
            chunk_r[slot][n + i] = r;
        }
        n += (uint32_t)s;
    }
    if (n) {
        DC_FlushRange(chunk_l[slot], n * 2);   /* make the samples visible to the sound hardware */
        DC_FlushRange(chunk_r[slot], n * 2);
    }
    return n;
}

static void start_slot(int slot) {
    uint32_t n = slot_len[slot];
    cur_l = soundPlaySample(chunk_l[slot], SoundFormat_16Bit, n * 2, (uint16_t)rate, 127, 0, false, 0);
    cur_r = soundPlaySample(chunk_r[slot], SoundFormat_16Bit, n * 2, (uint16_t)rate, 127, 127, false, 0);
    cur_slot = slot;
    cur_len = n;
    started_since += n;
}

/* Starts playback at ms (re-seeks the file, resets the decoder). */
static bool start_at(uint32_t ms) {
    kill_channels();

    uint64_t off = (uint64_t)data_start + (uint64_t)ms * bitrate_kbps * 125 / 1000;
    if (off >= file_size) {
        state = PL_ENDED;
        return false;
    }
    fseek(fp, (long)off, SEEK_SET);
    in_len = in_pos = 0;
    in_eof = false;
    mp3dec_init(&dec);

    base_samples = (uint64_t)ms * rate / 1000;
    started_since = 0;
    slot_len[0] = fill_chunk(0);
    slot_len[1] = fill_chunk(1);
    if (slot_len[0] == 0) {
        state = PL_ENDED;
        return false;
    }
    start_slot(0);
    state = PL_PLAYING;
    return true;
}

bool player_open(const char *path, uint32_t start_ms, bool autoplay) {
    player_close();

    fp = fopen(path, "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    file_size = (uint32_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Skip an ID3v2 tag (its size is "syncsafe": 7 bits per byte). */
    uint8_t hdr[10];
    data_start = 0;
    if (fread(hdr, 1, 10, fp) == 10 && memcmp(hdr, "ID3", 3) == 0) {
        uint32_t sz = ((uint32_t)(hdr[6] & 0x7F) << 21) | ((uint32_t)(hdr[7] & 0x7F) << 14) |
                      ((uint32_t)(hdr[8] & 0x7F) << 7) | (uint32_t)(hdr[9] & 0x7F);
        data_start = 10 + sz;
        if (hdr[5] & 0x10) data_start += 10;   /* footer present */
    }
    if (data_start >= file_size) {
        player_close();
        return false;
    }

    /* Probe the first frame for sample rate and bitrate. */
    fseek(fp, (long)data_start, SEEK_SET);
    in_len = in_pos = 0;
    in_eof = false;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    if (next_frame(&info) <= 0 || info.hz == 0) {
        player_close();
        return false;
    }
    rate = (uint32_t)info.hz;
    bitrate_kbps = info.bitrate_kbps > 0 ? (uint32_t)info.bitrate_kbps : 128;

    if (autoplay) {
        start_at(start_ms);
    } else {
        paused_ms = start_ms;
        state = PL_PAUSED;
    }
    return true;
}

void player_close(void) {
    kill_channels();
    if (fp) {
        fclose(fp);
        fp = NULL;
    }
    state = PL_IDLE;
    cur_len = 0;
}

void player_update(void) {
    if (state != PL_PLAYING) return;
    if (chan_busy(cur_l) || chan_busy(cur_r)) return;   /* current chunk still playing */

    int nxt = 1 - cur_slot;
    if (slot_len[nxt] == 0) {                            /* nothing decoded ahead: end of track */
        kill_channels();
        state = PL_ENDED;
        return;
    }
    int old = cur_slot;
    start_slot(nxt);
    slot_len[old] = fill_chunk(old);                    /* refill the buffer that just finished */
}

void player_toggle(void) {
    if (state == PL_PLAYING) {
        paused_ms = player_pos_ms();
        kill_channels();
        state = PL_PAUSED;
    } else if (state == PL_PAUSED || state == PL_ENDED) {
        start_at(paused_ms);
    }
}

void player_seek_ms(uint32_t ms) {
    if (state == PL_PLAYING) {
        start_at(ms);
    } else if (state == PL_PAUSED || state == PL_ENDED) {
        paused_ms = ms;
        state = PL_PAUSED;
    }
}

PlayerState player_state(void) { return state; }

uint32_t player_pos_ms(void) {
    if (state == PL_PAUSED) return paused_ms;
    if (state == PL_ENDED) return player_len_ms();
    if (state != PL_PLAYING || rate == 0) return 0;

    uint64_t done = base_samples + started_since;
    done = (done > cur_len) ? done - cur_len : 0;   /* the chunk being played hasn't finished */
    return (uint32_t)(done * 1000 / rate);
}

uint32_t player_len_ms(void) {
    if (!fp || bitrate_kbps == 0) return 0;
    /* bits / (kbit per second) = milliseconds. Exact for CBR, an estimate for VBR. */
    return (uint32_t)((uint64_t)(file_size - data_start) * 8 / bitrate_kbps);
}
