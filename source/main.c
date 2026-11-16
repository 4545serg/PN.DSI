/* PROJECT NOISE DSi - main.c
 *
 * Top screen:    now playing, synced lyrics (.lrc), progress, status.
 * Bottom screen: folder browser (sd:/Music) and touch transport controls.
 */
#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "browser.h"
#include "lyrics.h"
#include "player.h"

#define ROOT_DIR      "sd:/Music"
#define STATE_DIR     "sd:/PROJECT_NOISE_DSI"
#define STATE_FILE    "sd:/PROJECT_NOISE_DSI/state.cfg"
#define LIST_ROWS     19             /* browser rows on the bottom screen */
#define SEEK_STEP_MS  5000
#define PREV_RESTART_MS 3000         /* L / prev restarts the song after 3 s */
#define SAVE_EVERY    300            /* frames (about 5 s) */

enum { REP_OFF, REP_ALL, REP_ONE };

static PrintConsole top_con, bot_con;

static char ui_dir[BR_PATH_MAX] = ROOT_DIR;     /* folder shown on the bottom screen */
static int sel = 0, scroll = 0;

static bool shuffle = false;
static int repeat_mode = REP_ALL;

static bool has_track = false;
static char play_dir[BR_PATH_MAX] = "";         /* folder of the current song */
static char play_name[BR_NAME_MAX] = "";        /* file name of the current song */
static char msg[40] = "";

static bool top_dirty = true, bot_dirty = true;
static uint32_t frame = 0;
static uint32_t drawn_sec = 0xFFFFFFFF;
static int drawn_ly = -99;
static int drawn_state = -1;
static unsigned seed_counter = 1;

/* ---------- state file ---------- */

static void save_state(void) {
    FILE *f = fopen(STATE_FILE, "w");
    if (!f) return;
    fprintf(f, "shuffle=%d\n", shuffle ? 1 : 0);
    fprintf(f, "repeat=%d\n", repeat_mode);
    fprintf(f, "ui_dir=%s\n", ui_dir);
    fprintf(f, "play_dir=%s\n", has_track ? play_dir : "");
    fprintf(f, "play_name=%s\n", has_track ? play_name : "");
    fprintf(f, "pos_ms=%lu\n", (unsigned long)(has_track ? player_pos_ms() : 0));
    fclose(f);
}

static void load_state(void) {
    FILE *f = fopen(STATE_FILE, "r");
    if (!f) return;

    char line[BR_PATH_MAX + 32];
    char rdir[BR_PATH_MAX] = "";
    char rname[BR_NAME_MAX] = "";
    uint32_t pos = 0;

    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;

        if (!strcmp(k, "shuffle")) shuffle = atoi(v) != 0;
        else if (!strcmp(k, "repeat")) repeat_mode = atoi(v) % 3;
        else if (!strcmp(k, "ui_dir")) snprintf(ui_dir, sizeof ui_dir, "%s", v);
        else if (!strcmp(k, "play_dir")) snprintf(rdir, sizeof rdir, "%s", v);
        else if (!strcmp(k, "play_name")) snprintf(rname, sizeof rname, "%s", v);
        else if (!strcmp(k, "pos_ms")) pos = (uint32_t)strtoul(v, NULL, 10);
    }
    fclose(f);

    if (rdir[0] && rname[0]) {
        /* Reopen the last song paused at the saved position (press B to play). */
        char full[BR_PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", rdir, rname);
        if (player_open(full, pos, false)) {
            snprintf(play_dir, sizeof play_dir, "%s", rdir);
            snprintf(play_name, sizeof play_name, "%s", rname);
            has_track = true;
            ly_load(full);
        }
    }
}

/* ---------- playback helpers ---------- */

static bool open_track(const char *dir, const char *name, uint32_t ms, bool autoplay) {
    char d[BR_PATH_MAX], n[BR_NAME_MAX], full[BR_PATH_MAX];
    snprintf(d, sizeof d, "%s", dir);
    snprintf(n, sizeof n, "%s", name);
    snprintf(full, sizeof full, "%s/%s", d, n);

    if (!player_open(full, ms, autoplay)) {
        has_track = false;
        snprintf(msg, sizeof msg, "Can't play this file");
        top_dirty = bot_dirty = true;
        return false;
    }
    snprintf(play_dir, sizeof play_dir, "%s", d);
    snprintf(play_name, sizeof play_name, "%s", n);
    has_track = true;
    ly_load(full);
    msg[0] = 0;
    top_dirty = bot_dirty = true;
    save_state();
    return true;
}

/*
 * Finds a neighbouring song in play_dir (step +1 / -1), or a random one.
 * wrap=false returns false at the end of the folder. Copies the name into out.
 * The browser is re-read for ui_dir afterwards.
 */
static bool neighbor(int step, bool random_pick, bool wrap, char *out, size_t outn) {
    if (!br_open(play_dir)) {
        br_open(ui_dir);
        return false;
    }
    static int idx[BR_MAX];
    int n = 0;
    for (int i = 0; i < br_count; i++)
        if (!br_items[i].dir) idx[n++] = i;

    bool found = false;
    if (n > 0) {
        int cur = -1;
        for (int k = 0; k < n; k++) {
            if (strcmp(br_items[idx[k]].name, play_name) == 0) {
                cur = k;
                break;
            }
        }

        int pick = -1;
        if (random_pick) {
            srand(seed_counter * 2654435761u ^ player_pos_ms());
            seed_counter++;
            pick = rand() % n;
            if (n > 1 && pick == cur) pick = (pick + 1) % n;
        } else if (cur < 0) {
            pick = (step > 0) ? 0 : n - 1;
        } else {
            pick = cur + step;
            if (pick < 0 || pick >= n) {
                pick = wrap ? (pick + n) % n : -1;
            }
        }

        if (pick >= 0) {
            snprintf(out, outn, "%s", br_items[idx[pick]].name);
            found = true;
        }
    }
    br_open(ui_dir);
    return found;
}

static void next_track(void) {
    if (!has_track) return;
    char name[BR_NAME_MAX];
    if (neighbor(1, shuffle, true, name, sizeof name))
        open_track(play_dir, name, 0, true);
}

static void prev_track(void) {
    if (!has_track) return;
    if (player_pos_ms() > PREV_RESTART_MS) {
        player_seek_ms(0);
        return;
    }
    char name[BR_NAME_MAX];
    if (neighbor(-1, shuffle, true, name, sizeof name))
        open_track(play_dir, name, 0, true);
}

static void on_track_end(void) {
    if (repeat_mode == REP_ONE) {
        open_track(play_dir, play_name, 0, true);
        return;
    }
    char name[BR_NAME_MAX];
    bool wrap = (repeat_mode == REP_ALL) || shuffle;
    if (neighbor(1, shuffle, wrap, name, sizeof name)) {
        open_track(play_dir, name, 0, true);
    } else {
        /* End of folder with repeat off: stop, ready to play again from the start. */
        open_track(play_dir, play_name, 0, false);
    }
}

static void toggle_pause(void) {
    if (!has_track) return;
    player_toggle();
    top_dirty = bot_dirty = true;
    save_state();
}

static void seek_rel(int32_t delta) {
    if (!has_track) return;
    int32_t pos = (int32_t)player_pos_ms() + delta;
    int32_t len = (int32_t)player_len_ms();
    if (pos < 0) pos = 0;
    if (pos >= len) {                 /* past the end = next track */
        next_track();
        return;
    }
    player_seek_ms((uint32_t)pos);
}

/* ---------- browser helpers ---------- */

static void move_sel(int d) {
    if (br_count == 0) return;
    sel += d;
    if (sel < 0) sel = 0;
    if (sel >= br_count) sel = br_count - 1;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + LIST_ROWS) scroll = sel - LIST_ROWS + 1;
}

/* A: open a folder, or play a song. */
static void activate(int i) {
    if (i < 0 || i >= br_count) return;
    BrEntry e = br_items[i];                     /* copy: the list may be re-read */

    if (e.dir) {
        char nd[BR_PATH_MAX];
        snprintf(nd, sizeof nd, "%s/%s", ui_dir, e.name);
        snprintf(ui_dir, sizeof ui_dir, "%s", nd);
        br_open(ui_dir);
        sel = 0;
        scroll = 0;
        bot_dirty = true;
        save_state();
    } else {
        open_track(ui_dir, e.name, 0, true);
    }
}

static void go_up(void) {
    size_t rootlen = strlen(ROOT_DIR);
    size_t curlen = strlen(ui_dir);
    if (curlen <= rootlen || strncmp(ui_dir, ROOT_DIR, rootlen) != 0) return;

    char *s = strrchr(ui_dir, '/');
    if (!s || (size_t)(s - ui_dir) < rootlen) return;
    *s = 0;
    br_open(ui_dir);
    sel = 0;
    scroll = 0;
    bot_dirty = true;
    save_state();
}

static void handle_touch(void) {
    touchPosition tp;
    touchRead(&tp);
    int col = tp.px / 8;
    int row = tp.py / 8;

    if (row >= 1 && row <= LIST_ROWS) {
        int i = scroll + row - 1;
        if (i < br_count) {
            if (i == sel) activate(i);
            else sel = i;
        }
    } else if (row == 22) {
        if (col < 8) prev_track();
        else if (col < 16) toggle_pause();
        else if (col < 24) next_track();
        else if (col < 28) shuffle = !shuffle;
        else repeat_mode = (repeat_mode + 1) % 3;
        save_state();
    }
}

static void handle_keys(uint32_t down) {
    if (down & KEY_UP) move_sel(-1);
    if (down & KEY_DOWN) move_sel(1);
    if (down & KEY_A) activate(sel);
    if (down & KEY_B) toggle_pause();
    if (down & KEY_X) { shuffle = !shuffle; save_state(); }
    if (down & KEY_Y) { repeat_mode = (repeat_mode + 1) % 3; save_state(); }
    if (down & KEY_L) prev_track();
    if (down & KEY_R) next_track();
    if (down & KEY_LEFT) seek_rel(-SEEK_STEP_MS);
    if (down & KEY_RIGHT) seek_rel(SEEK_STEP_MS);
    if (down & KEY_SELECT) go_up();
    if (down & KEY_TOUCH) handle_touch();
    if (down) top_dirty = bot_dirty = true;
}

/* ---------- drawing ---------- */

static void draw_top(void) {
    consoleSelect(&top_con);
    consoleClear();

    PlayerState st = player_state();
    uint32_t pos = player_pos_ms();
    uint32_t len = player_len_ms();

    const char *tag = "-- NO SONG --";
    if (st == PL_PLAYING) tag = "> NOW PLAYING";
    else if (st == PL_PAUSED) tag = "|| PAUSED";
    else if (st == PL_ENDED) tag = "[] END OF TRACK";

    char title[BR_NAME_MAX];
    snprintf(title, sizeof title, "%s", has_track ? play_name : "Press A on a song");
    char *dot = strrchr(title, '.');
    if (has_track && dot) *dot = 0;

    const char *artist = "";
    if (has_track) {
        const char *s = strrchr(play_dir, '/');
        artist = s ? s + 1 : play_dir;
    }

    iprintf("\x1b[36m PROJECT NOISE DSi\x1b[39m\n\n");
    iprintf(" %s\n", tag);
    iprintf(" \x1b[37m%.30s\x1b[39m\n", title);
    iprintf(" %.30s\n", artist);
    iprintf("\n");

    /* Synced lyrics: five lines, the current one highlighted. */
    int n = has_track ? ly_count() : 0;
    int cur = (n > 0) ? ly_index_at(pos / 10) : -1;
    if (n == 0) {
        iprintf("\n\n  No synced lyrics\n  (.lrc next to the mp3)\n\n");
    } else {
        int first = (cur < 0) ? 0 : cur - 2;
        for (int k = 0; k < 5; k++) {
            int i = first + k;
            if (i < 0 || i >= n) {
                iprintf("\n");
            } else if (i == cur) {
                iprintf("\x1b[33m> %.28s\x1b[39m\n", ly_text(i));
            } else {
                iprintf("  %.28s\n", ly_text(i));
            }
        }
    }
    iprintf("\n");

    int w = 28;
    int filled = (len > 0) ? (int)((uint64_t)pos * w / len) : 0;
    if (filled > w) filled = w;
    iprintf(" [%.*s%.*s]\n",
            filled, "############################",
            w - filled, "----------------------------");

    unsigned ps = (unsigned)(pos / 1000), ls = (unsigned)(len / 1000);
    iprintf(" %02u:%02u / %02u:%02u\n", ps / 60, ps % 60, ls / 60, ls % 60);
    iprintf(" SHUF %s   REP %s\n\n",
            shuffle ? "ON" : "off",
            repeat_mode == REP_OFF ? "off" : (repeat_mode == REP_ALL ? "all" : "one"));
    iprintf(" %.30s\n\n", msg);

    iprintf(" A play   B pause   Y repeat\n");
    iprintf(" L/R prev/next   <- -> seek 5s\n");
    iprintf(" X shuffle   SELECT folder up");
}

static void draw_bottom(void) {
    consoleSelect(&bot_con);
    consoleClear();

    const char *sub = ui_dir + strlen(ROOT_DIR);
    iprintf("\x1b[36m MUSIC%.24s\x1b[39m\n", sub);

    for (int r = 0; r < LIST_ROWS; r++) {
        int i = scroll + r;
        if (i >= br_count) {
            iprintf("\n");
            continue;
        }
        BrEntry *e = &br_items[i];
        bool is_cur = !e->dir && has_track && strcmp(e->name, play_name) == 0 &&
                      strcmp(ui_dir, play_dir) == 0;

        char label[BR_NAME_MAX + 4];
        if (e->dir) {
            snprintf(label, sizeof label, "[%s]", e->name);
        } else {
            snprintf(label, sizeof label, "%s", e->name);
            char *dot = strrchr(label, '.');
            if (dot) *dot = 0;
        }

        const char *color = is_cur ? "\x1b[32m" : (i == sel ? "\x1b[33m" : "\x1b[37m");
        const char *mark = (i == sel) ? ">" : " ";
        iprintf("%s%s%.28s\x1b[39m\n", color, mark, label);
    }

    PlayerState st = player_state();
    const char *pp = (st == PL_PLAYING) ? " ||  " : " >   ";
    const char *rep = (repeat_mode == REP_OFF) ? "RP-" : (repeat_mode == REP_ALL ? "RP+" : "RP1");

    iprintf("\n");
    iprintf("--------------------------------\n");
    iprintf("%-8s%-8s%-8s%-4s%-4s", " |<", pp, " >|", shuffle ? "SH+" : "SH-", rep);
    iprintf("\n");
    iprintf(" %d songs/folders here", br_count);
}

/* ---------- main ---------- */

int main(void) {
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&top_con, 0, BgType_Text4bpp, BgSize_T_256x256, 22, 0, true, true);
    consoleInit(&bot_con, 0, BgType_Text4bpp, BgSize_T_256x256, 22, 0, false, true);

    consoleSelect(&top_con);
    iprintf("\x1b[36m PROJECT NOISE DSi\x1b[39m\n\n Loading...\n");

    if (!fatInitDefault()) {
        consoleSelect(&top_con);
        consoleClear();
        iprintf("\n SD card not found.\n Insert the SD card\n and restart the app.\n");
        while (pmMainLoop()) swiWaitForVBlank();
    }

    soundEnable();
    mkdir(ROOT_DIR, 0777);
    mkdir(STATE_DIR, 0777);

    load_state();
    if (strncmp(ui_dir, ROOT_DIR, strlen(ROOT_DIR)) != 0) {   /* keep the browser under sd:/Music */
        snprintf(ui_dir, sizeof ui_dir, "%s", ROOT_DIR);
    }
    if (!br_open(ui_dir)) {
        snprintf(ui_dir, sizeof ui_dir, "%s", ROOT_DIR);
        br_open(ui_dir);
    }

    PlayerState last_state = PL_IDLE;
    while (pmMainLoop()) {
        swiWaitForVBlank();
        scanKeys();
        uint32_t down = keysDown();
        frame++;

        handle_keys(down);
        player_update();

        if (has_track && player_state() == PL_ENDED) on_track_end();
        if (frame % SAVE_EVERY == 0 && player_state() == PL_PLAYING) save_state();

        PlayerState st = player_state();
        if (st != last_state) {
            top_dirty = bot_dirty = true;
            last_state = st;
        }

        uint32_t sec = has_track ? player_pos_ms() / 1000 : 0;
        int ly = (has_track && ly_count() > 0) ? ly_index_at(player_pos_ms() / 10) : -1;
        if (top_dirty || sec != drawn_sec || ly != drawn_ly || (int)st != drawn_state) {
            draw_top();
            top_dirty = false;
            drawn_sec = sec;
            drawn_ly = ly;
            drawn_state = (int)st;
        }
        if (bot_dirty) {
            if (sel >= br_count) sel = br_count ? br_count - 1 : 0;
            draw_bottom();
            bot_dirty = false;
        }
    }
    return 0;
}
