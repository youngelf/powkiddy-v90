# MusicPlayer — PowKiddy V90 Directory Music Player (`MP3` / `OGG` / `FLAC`)

A fast, distraction-free directory music player for the **PowKiddy V90 / Q90** (`320×240` RGB565, Allwinner F1C100S `ARM926EJ-S`) and **Linux Desktop (`x86_64` X11)**.

## Features

1. **Native Decoding of `.ogg`, `.flac`, and `.mp3` on a Background Thread**:
   - **MP3 (`.mp3`)**: Decoded via **`libmpg123`** (`mpg123_open`, `mpg123_read`, `mpg123_seek`, `mpg123_id3`).
   - **Ogg Vorbis (`.ogg`)**: Decoded via **`libvorbisfile` + `libogg`** (`ov_fopen`, `ov_read`, `ov_time_seek`, `ov_comment`).
   - **FLAC (`.flac`)**: Decoded via **`libFLAC`** (`FLAC__stream_decoder_init_file`, `FLAC__stream_decoder_process_single`, `FLAC__stream_decoder_seek_absolute`).
2. **Opens to a `/mnt/media` Directory Browser with `[RESUME]` at the Top**:
   - Starts in `/mnt/media` (or `./sample_media` on desktop if `/mnt/media` does not exist).
   - When a saved session exists from a previous run, the top highlighted entry is **`[RESUME] <Directory> / <Track> (MM:SS)`** so pressing **`A`** immediately resumes playback where you left off.
   - Selecting any directory with **`A`** loads all `.mp3`, `.ogg`, and `.flac` files in that directory sorted in **filename order (`strcasecmp`)** and plays them sequentially.
3. **Interactive Playlist & Hold-to-Seek (`L1` / `R1`)**:
   - In the Player view, **`A`** toggles **Play / Pause**, **`UP` / `DOWN`** highlights songs in the playlist, and **`B`** selects and plays the highlighted song.
   - **`L1` / `R1`** seeks backward / forward (`-5s` / `+5s` on tap; **hold `L1`/`R1` for continuous Rewind / Fast-Forward**) in both the Player view and the Directory Browser view.
4. **Persistent File List & Play Position (`MENU` to Quit)**:
   - Pressing **`MENU`** saves the active directory, file list, current track index, and exact playback position (`position_ms`) to `/mnt/apps/musicplayer/musicplayer.cfg` (or `~/.musicplayer.cfg` on desktop) before exiting.

---

## Screenshots

| `/mnt/media` Directory Browser (with `[RESUME]`) | Now Playing (`FLAC`) & Playlist (`UP`/`DOWN` + `[B]`) | Ogg Vorbis (`OGG`) Playback | Controls & Guide (`Y`) |
| :---: | :---: | :---: | :---: |
| ![Directory Browser](promo/musicplayer_browser.png) | ![FLAC Playing & Playlist](promo/musicplayer_playing.png) | ![OGG Playback](promo/musicplayer_ogg.png) | ![Controls & Guide](promo/musicplayer_help.png) |

---

## Controls

| V90 Button | Linux Desktop (`X11`) | Action in Directory Browser | Action in Now Playing / Playlist View |
| :--- | :--- | :--- | :--- |
| **`A`** (`LALT`) | `Space` / `Return` / `A` | **Resume Saved Session** or **Play Highlighted Directory** | **Play / Pause** current song |
| **`B`** (`LCTRL`) | `B` / `BackSpace` | **Open / Enter** highlighted subdirectory | **Play Highlighted Song** in playlist |
| **`UP` / `DOWN`** | `Up` / `Down` Arrows | Move highlight cursor up/down folders | Move highlight cursor up/down **playlist** |
| **`LEFT` / `RIGHT`**| `Left` / `Right` Arrows | Parent folder (`..`) / Enter subfolder | Page Up / Page Down in playlist |
| **`L1` / `R1`** | `[` / `]` (or `Tab` / `E`) | **Seek Backward / Forward** (Hold: Rewind / Fast-Forward) | **Seek Backward / Forward** (Hold: Rewind / Fast-Forward) |
| **`SELECT` / `X`** | `Escape` / `S` / `C` / `X`| Switch to **Now Playing / Playlist View** | Switch to **`/mnt/media` Directory Browser** |
| **`Y`** (`SPACE`) | `Y` / `H` | Toggle **Help & Controls Guide** overlay | Toggle **Help & Controls Guide** overlay |
| **`MENU`** (`RCTRL`)| `Q` / `M` | **Save file list + play position & Quit** | **Save file list + play position & Quit** |

---

## Building & Verification

```bash
# Build both PowKiddy V90 (ARM) and Linux Desktop (x86_64 X11) binaries, run verification, and create sdcard_package/
make clean all package

# Run automated verification of libmpg123, libvorbisfile, and libFLAC decoding & seeking
make test
```

## Installing on the PowKiddy V90 MicroSD Card

Copy the contents of `sdcard_package/` onto the `main` (`fat32`) partition of your MiyooCFW MicroSD card:

```
/mnt/apps/musicplayer/musicplayer
/mnt/apps/musicplayer/icon.png
/mnt/apps/musicplayer/libs/
/mnt/gmenu2x/sections/applications/musicplayer.lnk
```
