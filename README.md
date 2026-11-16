# PROJECT NOISE DSi

An MP3 player for the **Nintendo DSi** (and DSi-mode XL/LL), written from scratch in C with devkitARM + libnds + libfat.
It is a DSi port of the feature set of PROJECT NOISE (3DS): same folder browser, transport controls,
shuffle / repeat, resume and synced lyrics, adapted to the DSi's two 256x192 screens.

> **Status: first draft, not yet run on hardware.** The build environment used to write it had no DS toolchain,
> so the code has been checked with a host compile of the parser / browser and a compile of the player and UI
> against libnds stubs. Expect to report build errors or sound problems back. See *Testing* below.

## What it does

**Top screen** - *NOW PLAYING* / *PAUSED* status, the song name and folder, **synced lyrics** (five lines, the
current one highlighted in yellow), a progress bar, elapsed / total time, and the shuffle and repeat state.

**Bottom screen** - folder browser for `sd:/Music/` (folders first, then `.mp3` files, A-Z), the playing song
in green, and touch transport controls along the bottom row:

| Touch (bottom row) | Action |
|---|---|
| `\|<` | Previous song (restarts it if more than 3 s have played) |
| `\|\|` / `>` | Play / pause |
| `>\|` | Next song |
| `SH+` / `SH-` | Shuffle on / off |
| `RP-` / `RP+` / `RP1` | Repeat off / all / one |

### Controls

| Button | Action |
|---|---|
| **D-pad Up / Down** | Move through the list |
| **A** | Open the selected folder, or play the selected song |
| **B** | Play / pause |
| **L / R** | Previous / next song |
| **D-pad Left / Right** | Seek back / forward 5 s (past the end = next song) |
| **X** | Shuffle on / off |
| **Y** | Repeat: off -> all -> one |
| **SELECT** | Go up one folder |
| **Touch** | Tap a row to select it, tap it again to play. Tap the bottom bar for transport |

When a song ends, the next one plays (respecting shuffle and repeat). With repeat off, playback stops at the end
of the folder, ready to play again.

### Lyrics (synced)

Put an `.lrc` file with the same name next to the song:

```
Music/Artist/01 Song.mp3
Music/Artist/01 Song.lrc
```

Standard LRC is supported: `[mm:ss.xx]` stamps, several stamps per line (repeated choruses), and tag lines like
`[ar:...]` are skipped. Get `.lrc` files from LRCLIB or any LRC site on your PC and copy them to the SD card.

**Limits:** the DS font is ASCII-only, so accented letters show as `?`. Each lyric line is cut at about 28 characters
to fit the screen. The DSi build does **not** search for lyrics online (see *Differences* below).

### Resume

The last song, its folder and its position are saved to `sd:/PROJECT_NOISE_DSI/state.cfg` (every ~5 s while
playing, and on each change). On the next launch the song is shown paused at that point. Press **B** to continue.

## Putting music on the SD card

1. Put the SD card in your PC (FAT32).
2. Create `Music` at the top level: `Music/`. Sub-folders are fine: `Music/Artist/Album/01 Song.mp3`.
3. Copy `.mp3` files there (optionally with `.lrc` files beside them).
4. Put the card back in the DSi and launch the app.

**Tips:** MP3 only (CBR and VBR). Keep file names short (about 60 characters or fewer); long names may not
play. Up to 256 entries per folder.

## Running it on a DSi

This is a DSi-enhanced app that needs the **SD card** to be readable. Load it with a DSi-capable launcher such as
**TWiLight Menu++** or **DSiMenu++** (SD access on DSi needs the unlocked SD slot those launchers provide). Copy
`project_noise_dsi.nds` to the SD card and start it from the launcher.

## Differences from PROJECT NOISE (3DS)

Some features of the 3DS version can't run on a DSi, or would need a much bigger project. What is in this build
and what isn't:

| 3DS feature | DSi version |
|---|---|
| Folder browser, playback, shuffle, repeat, resume | **Yes** |
| Touch and button controls | **Yes** |
| MP3 playback | **Yes** (decoded on the ARM9 with minimp3) |
| Synced lyrics from `.lrc` files | **Yes** |
| Lyrics from the internet (LRCLIB, etc.) | **No**: the DSi has no HTTPS / TLS stack in libnds, and an HTTP-only fetch would need a proxy service |
| Cover flow, cover art, ambient glow | **No**: the DSi screens are 256x192 and the art pipeline (JPEG/PNG decode) is too heavy for the 67 MHz ARM9 |
| FLAC and WAV | **No** (MP3 only) |
| 5-band EQ, visualizers, OLED / themes, setup guide | **No** |
| Song search, favorites, Up Next queue | **No** (planned) |
| Lid-closed playback, sleep timer | **No** |
| Settings screens | **No**: shuffle and repeat are set with buttons |

The 3DS version has a graphical UI built on citro2d. The DSi version uses the DS text console for both screens,
so the look is simpler than the 3DS version.

## Building

### A. GitHub builds it for you (nothing to install)

1. Create a repository and upload this whole folder, including `.github/`.
2. Open **Actions** -> *Build PROJECT NOISE DSi* (it runs on every push, or press *Run workflow*).
3. Download the `PROJECT-NOISE-DSI` artifact: `project_noise_dsi.nds`.

### B. Locally with devkitPro

1. Install devkitPro and the DS packages: `dkp-pacman -S nds-dev`.
2. In a devkitPro shell, in this folder:
   ```
   make deps     # downloads minimp3.h into source/
   make          # -> project_noise_dsi.nds
   ```
   To pin the decoder for reproducible builds: `MINIMP3_REF=<commit> make deps`.

## Testing checklist

Please report which of these pass, and paste any build errors or console messages:

1. The app builds, and the `.nds` launches from TWiLight Menu++ / DSiMenu++.
2. The top screen shows the title, and "Loading..." is replaced by the browser on the bottom screen.
3. A song plays with clear sound (not crackly or too slow). Try a 128 kbps and a 320 kbps MP3.
4. Pause / resume, L / R, seek with Left / Right, shuffle, repeat, folder navigation and SELECT.
5. A song with an `.lrc` file: the current line highlights and advances in time.
6. Close the app (or reset), relaunch: the last song is shown paused at the saved place.
7. Long play: a 5+ minute song, and the song-to-song transition has no long silence.

**Known risks** (the places most likely to need work):

- **Decode speed.** minimp3 is written for floating point, and the DS ARM9 has no FPU, so 320 kbps files may stutter.
  If that happens, the fix is a fixed-point decoder (libmad or helix); the change stays inside `player.c`.
- **Sound buffer size.** `player.c` passes byte counts to `soundPlaySample()`. Check this against your libnds version.
- **Long file names and FAT32 quirks.** The browser reads whatever libfat returns.
- **Sleep / lid.** Playback is not paused automatically when the lid closes, so press **B** before closing the lid.

## Source layout

```
source/main.c      UI, input, touch, state save / resume, track flow
source/player.c    MP3 decode (minimp3) and double-buffered DS sound streaming
source/player.h
source/lyrics.c    LRC (synced lyrics) parser
source/lyrics.h
source/browser.c   Folder listing (folders first, mp3 only)
source/browser.h
source/mp3impl.c   minimp3 implementation (compiled once)
tools/get_deps.sh  Fetches minimp3.h
Makefile           devkitARM + libnds build
.github/workflows/build.yml   CI build (devkitPro Docker image)
```

## Credits

PROJECT NOISE DSi - MADE BY GRIMZZZZ. Decoder: [minimp3](https://github.com/lieff/minimp3) (CC0 / public domain).
