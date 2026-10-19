# Gmu Music Player for PowKiddy V90 (MiyooCFW 1.3.3 & Linux Desktop)

A port and keymap fix for the **Gmu Music Player** ([`denis-n-kuznetsov/gmu`](https://github.com/denis-n-kuznetsov/gmu)) targeting the **PowKiddy V90 / Q90** handheld running **MiyooCFW 1.3.3** (Allwinner F1C100S `ARM926EJ-S`, 320×240 16-bit display, SDL 1.2), along with a native **x86_64 Linux Desktop** build for testing and debugging.

![Gmu Playlist Browser on PowKiddy V90](/gmu/promo/gmu_playlist.png)

---

## What Was Fixed

### 1. Incorrect Face Button Mappings (`A` $\leftrightarrow$ `B` and `X` $\leftrightarrow$ `Y` Swapped)

In the stock MiyooCFW 1.3.3 image (`apps/gmu/gmuinput.miyoo.conf`), the SDL keycode table was copied directly from `gmuinput.dingux.conf` (Dingoo A320), which swaps the SDL keycodes for **A/B** and **X/Y** relative to the PowKiddy V90's `miyoo_kbd` kernel driver:

| Physical V90 Button | F1C100S GPIO Pin | Kernel `miyoo_kbd` Bitmask | Linux Keycode (`input.h`) | SDL 1.2 Keysym (`SDLKey`) | Stock `gmuinput.miyoo.conf` (Buggy) | Fixed [`gmu/gmuinput.miyoo.conf`](/gmu/gmuinput.miyoo.conf) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **D-Pad Up** | `PE2` (`0x82`) | `0x0008` | `KEY_UP` (`103`) | `273` (`SDLK_UP`) | `273,Up` | `273,Up` |
| **D-Pad Down** | `PE3` (`0x83`) | `0x0800` | `KEY_DOWN` (`108`) | `274` (`SDLK_DOWN`) | `274,Down` | `274,Down` |
| **D-Pad Left** | `PE4` (`0x84`) | `0x0080` | `KEY_LEFT` (`105`) | `276` (`SDLK_LEFT`) | `276,Left` | `276,Left` |
| **D-Pad Right** | `PE5` (`0x85`) | `0x0040` | `KEY_RIGHT` (`106`) | `275` (`SDLK_RIGHT`) | `275,Right` | `275,Right` |
| **A** (Right / East) | `PD9` (`0x69`) | `0x0010` | `KEY_LEFTALT` (`56`) | **`308`** (`SDLK_LALT`) | `306,A` ❌ | **`308,A`** ✅ |
| **B** (Bottom / South) | `PE8` (`0x88`) | `0x0100` | `KEY_LEFTCTRL` (`29`) | **`306`** (`SDLK_LCTRL`) | `308,B` ❌ | **`306,B`** ✅ |
| **X** (Top / North) | `PD0` (`0x60`) | `0x0002` | `KEY_LEFTSHIFT` (`42`) | **`304`** (`SDLK_LSHIFT`) | `32,X` ❌ | **`304,X`** ✅ |
| **Y** (Left / West) | `PE9` (`0x89`) | `0x0020` | `KEY_SPACE` (`57`) | **`32`** (`SDLK_SPACE`) | `304,Y` ❌ | **`32,Y`** ✅ |
| **L1** (Left Shoulder) | `PC2` (`0x42`) | `0x1000` | `KEY_TAB` (`15`) | `9` (`SDLK_TAB`) | `9,L` | `9,L` |
| **R1** (Right Shoulder) | `PE1` (`0x81`) | `0x2000` | `KEY_BACKSPACE` (`14`) | `8` (`SDLK_BACKSPACE`) | `8,R` | `8,R` |
| **SELECT** | `PE0` (`0x80`) | `0x0400` | `KEY_ESC` (`1`) | `27` (`SDLK_ESCAPE`) | `27,Select` | `27,Select` |
| **START** | `PC1` (`0x41`) | `0x0200` | `KEY_ENTER` (`28`) | `13` (`SDLK_RETURN`) | `13,Start` | `13,Start` |
| **MENU / RESET** | `PE7` (`0x87`) | `0x0004` | `KEY_RIGHTCTRL` (`97`) | `305` (`SDLK_RCTRL`) | `305,Lock` | `305,Lock` |

Because `306` (`SDLK_LCTRL`, physical **B**) and `308` (`SDLK_LALT`, physical **A**) were reversed—and `304` (`SDLK_LSHIFT`, physical **X**) and `32` (`SDLK_SPACE`, physical **Y**) were also reversed—every on-screen prompt and help entry for `A`, `B`, `X`, and `Y` was inverted on the PowKiddy V90.

### 2. Reconstructed Miyoo Hardware Backend & Modern GCC Support
- Reconstructed [`gmu/src/hw_miyoo.c`](/gmu/src/hw_miyoo.c) and [`gmu/src/hw_miyoo.h`](/gmu/src/hw_miyoo.h) from the disassembled MiyooCFW `gmu.bin` binary (OSS mixer control via `/dev/mixer`, backlight power control via `/sys/devices/platform/backlight/backlight/backlight/brightness` and `/mnt/.backlight.conf`).
- Updated [`gmu/src/core.c`](/gmu/src/core.c), [`gmu/src/decloader.c`](/gmu/src/decloader.c), [`gmu/src/feloader.c`](/gmu/src/feloader.c), [`gmu/src/decoders/vorbis.c`](/gmu/src/decoders/vorbis.c), [`gmu/configure`](/gmu/configure), and [`gmu/Makefile`](/gmu/Makefile) to build cleanly on modern GCC (`-std=c23` / `-fno-common`) and support standard `libvorbisfile` on desktop Linux.

---

## Screenshots

### Playlist Browser & Play Mode Cycling (`B: Change play mode`)
![Gmu Playlist View](/gmu/promo/gmu_playlist.png)

### Built-in Help Screen (`Select + Up`) Showing Corrected V90 Mappings
![Gmu Help Screen](/gmu/promo/gmu_help.png)

### Track Info & Spectrum Analyzer (`Start` to Toggle View)
![Gmu Track Info View](/gmu/promo/gmu_trackinfo.png)

### File Browser (`A: Add file/Ch.dir`, `Y: Add dir`, `B: Play file`)
![Gmu File Browser View](/gmu/promo/gmu_filebrowser.png)

### About Gmu (`Select + A` from Playlist / Track Info)
![Gmu About View](/gmu/promo/gmu_about.png)

---

## Controls (PowKiddy V90 & Linux Desktop Keyboard)

On the PowKiddy V90, **SELECT** (`Esc` on desktop) acts as the **Modifier (`Mod`)** key.

| Scope | Action | PowKiddy V90 Button | Linux Desktop Key |
| :--- | :--- | :--- | :--- |
| **Global** | **Switch View** (File Browser $\rightarrow$ Playlist $\rightarrow$ Track Info) | **START** | `Enter` |
| **Global** | **Pause / Resume Playback** | **X** | `Left Shift` |
| **Global** | **Stop Playback** | **SELECT + X** | `Esc + Left Shift` |
| **Global** | **Next / Previous Track** | **R1** / **L1** | `Backspace` / `Tab` |
| **Global** | **Seek Forward / Backward** | **SELECT + R1** / **SELECT + L1** | `Esc + Backspace` / `Esc + Tab` |
| **Global** | **Volume Up / Down** | **D-Pad RIGHT** / **LEFT** | `Right Arrow` / `Left Arrow` |
| **Global** | **Open Help Screen** | **SELECT + UP** | `Esc + Up Arrow` |
| **Global** | **About / Program Info** | **SELECT + A** | `Esc + Left Alt` |
| **Global** | **Lock Buttons + Screen Off** | **MENU (Reset)** | `Right Ctrl` |
| **Global** | **Unlock Buttons + Screen On** | **R1** (release) | `Backspace` |
| **Global** | **Exit Gmu** | **SELECT + START** | `Esc + Enter` |
| **File Browser** | **Add File to Playlist / Open Directory** | **A** | `Left Alt` |
| **File Browser** | **Play Single File Immediately** | **B** | `Left Ctrl` |
| **File Browser** | **Add Directory to Playlist** | **Y** | `Space` |
| **File Browser** | **Create New Playlist from Directory** | **SELECT + Y** | `Esc + Space` |
| **Playlist** | **Play Selected Item** | **A** | `Left Alt` |
| **Playlist** | **Change Play Mode** (Continue / Repeat / Random) | **B** | `Left Ctrl` |
| **Playlist** | **Remove Selected Item** | **Y** | `Space` |
| **Playlist** | **Clear Playlist** | **SELECT + Y** | `Esc + Space` |
| **Playlist** | **Save / Load Playlist** | **SELECT + B** | `Esc + Left Ctrl` |
| **Playlist** | **Enqueue Selected Item** | **SELECT + RIGHT** | `Esc + Right Arrow` |
| **Track Info** | **Toggle Cover / Spectrum Analyzer** | **A** | `Left Alt` |
| **Track Info** | **Toggle Lyrics / Text** | **B** | `Left Ctrl` |

---

## Building & Running on Linux (`x86_64` Desktop)

### 1. Install Required System Packages (`apt`)

```bash
sudo apt update
sudo apt install -y build-essential libsdl1.2-dev libsdl-image1.2-dev libsdl-gfx1.2-dev \
  libmpg123-dev libflac-dev libvorbis-dev libmikmod-dev libwavpack-dev
```

### 2. Configure and Compile

From the `gmu/` directory:

```bash
cd gmu
./configure
make
```

This builds `gmu/gmu.bin`, the SDL and HTTP frontends (`gmu/frontends/sdl.so`, `gmu/frontends/gmuhttp.so`), and the audio decoders (`gmu/decoders/mpg123.so`, `gmu/decoders/flac.so`, `gmu/decoders/vorbis.so`, `gmu/decoders/wavpack.so`, `gmu/decoders/mikmod.so`).

### 3. Run on Desktop Linux

Launch `gmu.bin` in a 320×240 window using [`gmu/gmu.miyoo-desktop.conf`](/gmu/gmu.miyoo-desktop.conf):

```bash
cd gmu
./gmu.bin -c gmu.miyoo-desktop.conf
```

---

## Installing on Your PowKiddy V90 (MiyooCFW 1.3.3)

### Option A: Fix Keymappings on Existing MiyooCFW 1.3.3 SD Card (No Recompile Needed)

Copy the corrected configuration files from `gmu/` into `/mnt/apps/gmu/` on the `main` (`FAT32`) partition of your PowKiddy V90 SD card:

```bash
cp gmu/gmuinput.miyoo.conf /path/to/sdcard/main/apps/gmu/gmuinput.miyoo.conf
cp gmu/miyoo.keymap        /path/to/sdcard/main/apps/gmu/miyoo.keymap
cp gmu/gmu.miyoo.conf      /path/to/sdcard/main/apps/gmu/gmu.miyoo.conf
```

### Option B: Cross-Compile for PowKiddy V90 (`arm-miyoo-linux-uclibcgnueabi`)

If you have the MiyooCFW cross-compilation toolchain installed in `/opt/miyoo` (see [`metronome/README.md`](/metronome/README.md) for extraction instructions):

```bash
cd gmu
cp miyoo.mk config.mk
PATH="/opt/miyoo/bin:$PATH" make clean all
```

Then copy `gmu.bin`, `frontends/`, `decoders/`, `gmuinput.miyoo.conf`, `miyoo.keymap`, and `gmu.miyoo.conf` to `/mnt/apps/gmu/` on the PowKiddy V90 SD card.
