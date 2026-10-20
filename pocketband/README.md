# PocketBand — Pocket Drum Machine, Bassline & Backing Track Jam Buddy

A lightweight drum machine, bassline sequencer, and chord-progression backing band (`320×240` RGB565, fixed-point 32-bit synthesis) designed for guitarists on the **PowKiddy V90 / Q90** (`MiyooCFW 1.3.3`) and **Linux Desktop (`x86_64` X11 + ALSA)**.

Plug the PowKiddy V90's 3.5mm audio output into your guitar amp's AUX input (or headphones) and jam immediately over **Blues, Rock, and Jazz** backing tracks in **`4/4`** and **`3/4`**.

---

## Screenshots

| Guitarist Jam View (`Blues Shuffle 4/4`) | Step & Bassline Editor (`Classic Rock 4/4`) | `3/4` Waltz Jam View (`Jazz Waltz 3/4`) | Help & Groove Modal (`Y`) |
| :---: | :---: | :---: | :---: |
| ![Jam View](promo/pocketband_jam.png) | ![Step Editor](promo/pocketband_steps.png) | ![Jazz Waltz View](promo/pocketband_waltz.png) | ![Help Modal](promo/pocketband_help.png) |

---

## Features

- **Starts Directly in Guitarist Jam View (`VIEW_JAM`) with Live Backing Playback**:
  - **Now Playing & Next Chord Cards**: Shows the active bar's chord (`A7`, `Dm7`, etc.) in large `24px` bold typography, its Roman numeral degree (`I7`, `IV7`, `ii7`, `V7`), the active bassline note degrees, and a 4-bar scrolling progression window.
  - **12-Fret Guitar Soloing Scale Map**: Highlights key roots (Gold), active bar chord roots (Green), characteristic blue/color notes (Pink), and scale tones (Cyan) across all 6 guitar strings (`E A D G B e`).
  - **Beat & `&` Step Strip**: Displays `8` steps (`1 & 2 & 3 & 4 &`) in **`4/4`** styles and `6` steps (`1 & 2 & 3 &`) in **`3/4`** styles so drum hits land cleanly on the beat or the `&` of the beat.
- **6 Built-In Styles Across Blues, Rock & Jazz (`4/4` and `3/4`)**:
  1. **`Blues Shuffle 4/4`** (`12`-bar `I7–IV7–V7` Blues, `65%` swing, walking box bass `R 5 6 5 R 5 6 b7`)
  2. **`Slow Blues 3/4`** (`12`-bar `3/4` Blues waltz, `60%` swing, `R . 3 . 5 6` walking bass)
  3. **`Classic Rock 4/4`** (`4`-bar `I–bVII–IV–I` Rock groove, straight `50%`, eighth-note root/fifth/octave bass `R R R 5 R R 8 5`)
  4. **`Rock Ballad 3/4`** (`4`-bar `i–bVI–bIII–bVII` Aeolian 3/4 ballad, `R . 5 . 8 5` bass)
  5. **`Jazz Swing 4/4`** (`4`-bar `ii7–V7–Imaj7–VI7` turnaround, `62%` swing, `R . 3 . 5 . b7 .` walking quarter-note bass)
  6. **`Jazz Waltz 3/4`** (`4`-bar `Imaj7–vi7–ii7–V7` Jazz waltz, `60%` swing, `R . 3 . 5 6` walking 3/4 bass)
- **3-Way Backing Mix (`X` Button)**:
  - Cycle at any time between **`Full Band (Drums + Bass + Chord Stabs)`** $\rightarrow$ **`Drums + Bass`** $\rightarrow$ **`Drums Only`**.
- **Interactive Drum & Bass Step Editor (`SELECT` Button)**:
  - Edit all 5 tracks (`KICK`, `SNARE`, `CL HAT`, `OP HAT`, and `BASS`) across `1 & 2 & 3 & [4 &]`.
  - On the `BASS` row, `A` cycles chord-relative scale degrees (`OFF` $\rightarrow$ `R` $\rightarrow$ `3` $\rightarrow$ `5` $\rightarrow$ `6` $\rightarrow$ `b7` $\rightarrow$ `8`) that automatically transpose across every bar and key.
- **Persistent State**:
  - Automatically saves your active style, key, tempo, swing %, backing mix mode, and custom drum/bass patterns to `/mnt/.pocketband.cfg` on the V90 (`~/.pocketband.cfg` on Linux) when exiting with `START`.

---

## Controls

| V90 Button | Linux Key | Guitarist Jam View (`VIEW_JAM`) | Step Editor View (`VIEW_STEP_EDITOR`) |
| :--- | :--- | :--- | :--- |
| **`A`** | `Space` / `Z` | **Play / Stop** backing track | Cycle drum hit (`OFF`/`ON`/`ACC`) or bass note (`R`,`3`,`5`,`6`,`b7`,`8`) |
| **`B`** | `X` / `BackSpace` | **Queue Drum Fill** at end of current bar | Clear selected step (`OFF`) |
| **`X`** | `C` | **Cycle Backing Mix**: `Full Band` $\rightarrow$ `Drums+Bass` $\rightarrow$ `Drums Only` | **Cycle Backing Mix**: `Full Band` $\rightarrow$ `Drums+Bass` $\rightarrow$ `Drums Only` |
| **`Y`** | `V` / `H` | Toggle **Help & Groove Settings** (`Swing %` & `Auto-Fill`) | Toggle **Help & Groove Settings** |
| **`SELECT`** | `Tab` | Switch to **Step & Bassline Editor** | Switch back to **Guitarist Jam View** |
| **`DPAD LEFT / RIGHT`** | `Left` / `Right` | Cycle **Style** (`Blues`, `Rock`, `Jazz` in `4/4` & `3/4`) | Move step cursor across `1 & 2 & 3 & [4 &]` |
| **`DPAD UP / DOWN`** | `Up` / `Down` | Transpose **Key** (`C`, `Db`, `D`, ..., `B`) | Select track (`KICK`, `SNARE`, `CL HAT`, `OP HAT`, `BASS`) |
| **`L1 / R1`** | `[` / `]` (or `-` / `=`) | Adjust **Tempo** (`-5` / `+5` BPM, `50–200 BPM`) | Adjust **Tempo** (`-5` / `+5` BPM) |
| **`START`** | `Enter` / `Q` / `Esc` | Save config & **Exit** | Save config & **Exit** |

---

## Building

```bash
# Build both PowKiddy V90 ARM binary (pocketband) and Linux x86_64 binary (pocketband_linux)
make

# Run natively on Linux desktop (640x480 2x window + ALSA audio)
make run-linux

# Populate sdcard_package/ for the PowKiddy V90 'main' SD card partition
make package
```

### Installing on the PowKiddy V90

Copy the contents of `sdcard_package/` onto the `main` partition of your PowKiddy V90 MicroSD card (mounted at `/mnt` on the device):
- `sdcard_package/apps/pocketband/pocketband` $\rightarrow$ `/mnt/apps/pocketband/pocketband`
- `sdcard_package/apps/pocketband/icon.png` $\rightarrow$ `/mnt/apps/pocketband/icon.png`
- `sdcard_package/gmenu2x/sections/applications/pocketband` $\rightarrow$ `/mnt/gmenu2x/sections/applications/pocketband`
