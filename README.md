# powkiddy-v90

Hand-written utilities, application replacements, and firmware tools for the **PowKiddy V90 / Q90** handheld running **MiyooCFW 1.3.3** (`320×240` 16-bit RGB565 display, Allwinner F1C100S `ARM926EJ-S`).

The PowKiddy V90 is a compact Linux device. While it is commonly used to play retro games, because it runs Linux (`uClibc-ng` + `SDL 1.2` + `ALSA`), you can write native C utilities controlled via the D-Pad, `A`/`B`/`X`/`Y`, `L1`/`R1`, `SELECT`, `START`, and `MENU` buttons.

Every application in this repository compiles both for the **PowKiddy V90 (`arm-miyoo-linux-uclibcgnueabi`)** and natively for **Linux Desktop (`x86_64` X11/SDL)** so you can test and debug on your workstation before copying the binaries to your MicroSD card.

---

## Included Applications & Tools

| Directory | Application | Replaces / Target on V90 | Desktop Binary |
| :--- | :--- | :--- | :--- |
| [`metronome/`](metronome/) | **Metronome** — Visual & audio beat timer with common time signatures | `/mnt/apps/metronome/metronome` | `metronome/metronome_linux` |
| [`pomodoro/`](pomodoro/) | **Pomodoro Timer** — Focus/break work timer with status display | `/mnt/apps/pomodoro/pomodoro` | `pomodoro/pomodoro_linux` |
| [`calc/`](calc/) | **Desk & Scientific Calculator (`BASIC` / `SCI`)** — High-contrast calculator with full `<math.h>` scientific functions | `/mnt/apps/calc/calc` | `calc/calc_linux` |
| [`bard/`](bard/) | **EPUB E-Book Reader** — Text-only `.epub` reader (`libzip` + `libxml2`) with chapter selector, light/dark themes, and whole-page scrolling | `/mnt/apps/bard/bard` | `bard/bard_linux` |
| [`videoplayer/`](videoplayer/) | **Native `320×240` Video Player** — Full-screen distraction-free video & ALSA audio player (`libavformat`, `libavcodec`, `libswscale`, `libswresample`) | `/mnt/apps/videoplayer/videoplayer` & `/mnt/gmenu2x/sections/applications/mplayer` | `videoplayer/videoplayer_linux` |
| [`gmu/`](gmu/) | **Gmu Music Player (Keymap Fix & Port)** — Fixes swapped `A`/`B` & `X`/`Y` buttons on V90 and adds modern GCC desktop build support | `/mnt/apps/gmu/` | `gmu/gmu.bin` |
| [`patch_bootlogo/`](patch_bootlogo/) | **Boot Splash Patcher** — Replaces the embedded PNG inside `/mnt/boot-logo` on a `powkiddy-q90-v90-cfw-v1.3.3.img` image | `/mnt/boot-logo` inside `.img` | `patch_bootlogo/patch_boot_logo` |

---

## 1. [Metronome](metronome/)

A simple metronome that displays common time signatures and plays a tick on the beat. See [`metronome/README.md`](metronome/README.md) for build and SD card installation instructions.

![Screenshot of metronome](metronome/promo/metronome_grab.png)

---

## 2. [Pomodoro](pomodoro/)

A work timer to track Pomodoro focus intervals and let folks know that you are working. See [`pomodoro/README.md`](pomodoro/README.md) for details.

![Screenshot of pomodoro](pomodoro/promo/pomodoro_grab.png)

---

## 3. [Desk & Scientific Calculator (`calc`)](calc/)

A complete redesign and replacement for the stock `/mnt/apps/calc/calc` (`bittboy-calc`) application:
- **Edge-to-Edge High-Contrast UI**: Removes the stock calculator's wasted gray/black borders and small 12pt text in favor of a full-width `320×72` anti-aliased display (`30px` bold digits) and color-coded 3D beveled keycaps.
- **Basic (`BASIC`) & Scientific (`SCI`) Modes**: Press **`SELECT`** on the PowKiddy V90 (or **`Home`** / **`Esc`** on Linux desktop) to toggle between the 5×5 Basic keypad and the 5×8 Scientific keypad (`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `log`, `ln`, `exp`, `10^x`, `x^y`, `x^2`, `sqrt`, `cbrt`, `1/x`, `n!`, `mod`, `pi`, `e`, and `DEG`/`RAD` switching via `L1`).
- **Clean Exit**: Press **`START`** or **`MENU`** to quit.

See [`calc/README.md`](calc/README.md) for full controls, build instructions, and SD card deployment steps.

| Stock Firmware Calculator (`bittboy-calc`) | Upgraded Basic Mode (`BASIC`) | Upgraded Scientific Mode (`SCI`) |
| :---: | :---: | :---: |
| ![Stock Calculator](calc/promo/calc_before.png) | ![Basic Calculator Mode](calc/promo/calc_desk.png) | ![Scientific Calculator Mode](calc/promo/calc_sci.png) |

---

## 4. [EPUB E-Book Reader (`bard`)](bard/)

A fast C EPUB reader (`libzip` + `libxml2`) that replaces the stock `/mnt/apps/bard/bard` TTS reader:
- **Starts in `/mnt/media`**: Opens a file browser directly in `/mnt/media` on the SD card (includes unabridged sample books in [`bard/media/`](bard/media/)).
- **Full-Screen Distraction-Free Reading**: Uses the entire `320×240` screen (`12` word-wrapped lines per page, zero horizontal scrolling, no automated scrolling) with a discrete **`Y: Help`** badge in the bottom-right corner.
- **Whole-Page Vertical Scrolling**: **`DPAD DOWN` / `UP`** scrolls one full page at a time.
- **Chapter Selector (`SELECT`)**: Lists all EPUB chapters (`toc.ncx` / EPUB 3 `<nav>`); navigate with **`DPAD UP` / `DOWN`** and press **`A`** to jump to a chapter.
- **Light & Dark Mode (`B`)**: Press **`B`** while reading to toggle between warm paper Light Mode and night-friendly Dark Mode.
- **Help & Progress Modal (`Y`)**: Displays the current chapter title, chapter & overall book page progress, and key mappings.

See [`bard/README.md`](bard/README.md) for full architecture, Linux package prerequisites, and PowKiddy V90 installation instructions.

| File Browser (`/mnt/media`) | Full-Screen Reading View (`Y: Help`) | Help & Progress Screen (`Y`) | Chapter List Overlay (`SELECT`) |
| :---: | :---: | :---: | :---: |
| ![File Browser](bard/promo/bard_files.png) | ![Reading View](bard/promo/bard_reading.png) | ![Help Screen](bard/promo/bard_help.png) | ![Chapter List](bard/promo/bard_chapters.png) |

---

## 5. [Full-Screen Video Player (`videoplayer`)](videoplayer/)

A native `320×240` video and audio player built on FFmpeg (`libavformat`, `libavcodec`, `libswscale`, `libswresample`, `libavutil`) and ALSA (`libasound`) to replace the stock `/mnt/apps/mplayer` script:
- **Startup `/mnt/media` File Browser**: Opens `/mnt/media` on startup so you can pick a video file (`.mp4`, `.mkv`, `.avi`, `.webm`, `.mov`, `.mpg`) with **`DPAD UP` / `DOWN`** and **`A`**.
- **Distraction-Free Full-Screen Playback**: Renders `320×240` video across the entire screen with zero borders or status bars, synchronized to the ALSA hardware audio clock.
- **Pause & Device Help Overlay (`A`)**: Pressing **`A`** toggles play/pause and displays a translucent Device Help overlay with playback progress and button hints when paused.
- **Clean Exit (`START` / `MENU`)**: Pressing **`START`** or **`MENU`** exits immediately.

See [`videoplayer/README.md`](videoplayer/README.md) for desktop setup, `ffmpeg` transcoding commands, and SD card deployment instructions.

| `/mnt/media` File Browser | Full-Screen Video Playback (`320×240`) | Paused View with Device Help (`A`) |
| :---: | :---: | :---: |
| ![File Browser](videoplayer/promo/video_browser.png) | ![Playing View](videoplayer/promo/video_playing.png) | ![Paused Overlay](videoplayer/promo/video_paused.png) |

---

## 6. [Gmu Music Player Keymap Fix & Port (`gmu`)](gmu/)

Fixes the inverted face-button mappings in the stock MiyooCFW 1.3.3 Gmu Music Player (`A` $\leftrightarrow$ `B` and `X` $\leftrightarrow$ `Y` were swapped due to a Dingoo A320 keymap in `gmuinput.miyoo.conf`), reconstructs the Miyoo hardware backend ([`gmu/src/hw_miyoo.c`](gmu/src/hw_miyoo.c)), and updates the codebase to compile cleanly on modern desktop Linux GCC. See [`gmu/README.md`](gmu/README.md) for details.

| Playlist Browser (`B: Change play mode`) | Corrected V90 Help Screen (`Select + Up`) | Track Info & Spectrum Analyzer (`Start`) |
| :---: | :---: | :---: |
| ![Gmu Playlist View](gmu/promo/gmu_playlist.png) | ![Gmu Help Screen](gmu/promo/gmu_help.png) | ![Gmu Track Info View](gmu/promo/gmu_trackinfo.png) |

---

## 7. [Boot Splash Patcher (`patch_bootlogo`)](patch_bootlogo/)

A zero-dependency C99 utility ([`patch_bootlogo/patch_boot_logo.c`](patch_bootlogo/patch_boot_logo.c)) that encodes a custom `.bmp` image into an RGBA PNG in memory and patches the `/mnt/boot-logo` binary inside a `powkiddy-q90-v90-cfw-v1.3.3.img` disk image. See [`patch_bootlogo/README.md`](patch_bootlogo/README.md) for usage instructions.

| Stock Boot Splash (`Powkiddy®`) | Patched Boot Splash (`Linux inside`) |
| :---: | :---: |
| ![Stock Boot Splash](patch_bootlogo/promo/boot_splash_before.png) | ![Patched Boot Splash](patch_bootlogo/promo/boot_splash_after.png) |
