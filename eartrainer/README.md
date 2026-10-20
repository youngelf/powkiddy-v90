# PowKiddy V90 Guitar Ear Trainer (`eartrainer`)

A low-CPU, guitar-focused interval and chord-quality ear trainer ([`eartrainer.c`](eartrainer.c)) designed for the **PowKiddy V90 / Q90** running **MiyooCFW 1.3.3** (`320×240` 16-bit RGB565 display, Allwinner F1C100S `ARM926EJ-S`), alongside a native **x86_64 Linux (`X11`)** target (`eartrainer_linux`) for desktop practice and debugging.

---

## Screenshots

| Interval Guessing (3 Tries) | Interval Answer & Fretboard Shape | Chord Quality Mode (`CHORDS`) | Lifetime Accuracy & Help (`Y`) |
| :---: | :---: | :---: | :---: |
| ![Interval Guessing](promo/eartrainer_intervals.png) | ![Interval Revealed](promo/eartrainer_revealed.png) | ![Chord Mode](promo/eartrainer_chords.png) | ![Help & Stats](promo/eartrainer_help.png) |

---

## 1. Design of the Application

### Key Features
1. **Mode 1 — Interval Trainer (`INTERVALS`)**:
   - Plays two notes (Root + Target Note) in the standard guitar pitch range (`E2`–`E5`) and challenges you to identify the interval across a **4×3 keypad grid** (`m2`, `M2`, `m3`, `M3`, `P4`, `TT`, `P5`, `m6`, `M6`, `m7`, `M7`, `P8`).
   - Supports **4 playback styles (`L1`)**: Ascending (`ASC`), Descending (`DESC`), Harmonic double-stop (`HARM`), and Mixed (`MIX`).
   - Supports **3 difficulty tiers (`R1`)**:
     - `EASY` (5 Core Guitar Intervals: `m3`, `M3`, `P4`, `P5`, `P8`)
     - `MEDIUM` (7 Diatonic Major Intervals: `M2`, `M3`, `P4`, `P5`, `M6`, `M7`, `P8`)
     - `ALL 12` (Full Chromatic `m2`–`P8`)
2. **Mode 2 — Chord Quality Trainer (`CHORDS`)**:
   - Press **`SELECT`** to switch to Chord Quality mode, using the same 4×3 grid layout for 12 guitar-essential chord types:
     - **Triads & Suspended**: `Major`, `Minor`, `dim`, `aug`, `sus2`, `sus4`
     - **7th Chords**: `Dom7` (`7`), `Maj7`, `m7`, `m7b5`, `dim7`, `7sus4`
   - Supports **3 playback styles (`L1`)**: `ARP+STRUM` (arpeggiates individual tones then strums the chord), `STRUM` (guitar down-strum with `35 ms` pick stagger), and `ARPEGGIO`.
3. **3-Guess Rule & Audition/Compare (`X`)**:
   - Each question gives you **3 tries** (`Tries:` indicator pips).
   - Wrong guesses are marked in **Crimson Red** and automatically replay the question audio so you can narrow down by ear.
   - Guessing right—or using all 3 tries—reveals the answer in **Emerald Green** along with the exact notes, formula, and song mnemonic.
   - Once revealed, move the cursor to any cell on the 4×3 grid and press **`X`** to **audition and compare** that interval or chord from the same root note on both the audio synth and the fretboard.
4. **6-String Guitar Fretboard Visualizer (`E A D G B e`, Frets `0`–`12`)**:
   - Displays a rosewood 12-fret guitar neck with pearloid inlays (`3`, `5`, `7`, `9`, double-dot `12`).
   - Shows the **Root Note** in amber-gold while you are guessing, and lights up the target interval position or moveable 4-string guitar chord voicing once revealed.
5. **6-Voice Karplus-Strong Plucked Guitar Synthesizer**:
   - Uses an integer waveguide delay-line filter (`< 2%` CPU on the ARM926EJ-S) excited with low-pass-filtered pick noise + fundamental body harmonics so intervals and chords sound like a warm plucked acoustic guitar.
6. **Persistent Accuracy Tracking**:
   - Saves your best streak and per-interval / per-chord accuracy to `/mnt/.eartrainer.cfg` on the PowKiddy V90 (`~/.eartrainer.cfg` on Linux desktop).

---

## 2. Controls

| Action | PowKiddy V90 Button | Linux Desktop Key (`eartrainer_linux`) |
| :--- | :--- | :--- |
| **Navigate 4×3 Grid** | **`D-Pad UP / DOWN / LEFT / RIGHT`** | `Arrow Keys` |
| **Submit Guess (3 Tries) / Next Question** | **`A`** (`LAlt`) | `A` or `Left Alt` |
| **Replay Question Audio** | **`B`** (`LCtrl`) | `B`, `R`, or `Left Ctrl` |
| **Play Root Note (Guessing) / Audition & Compare (Revealed)** | **`X`** (`LShift`) | `X` or `Left Shift` |
| **Open Help & Lifetime Accuracy Stats** | **`Y`** (`Space`) | `Y`, `H`, or `Space` |
| **Switch Mode (`INTERVALS` $\leftrightarrow$ `CHORDS`)** | **`SELECT`** (`Escape`) | `Esc` or `Home` |
| **Cycle Playback Style (`ASC`/`DESC`/`HARM`/`MIX` or `ARP+STRUM`)** | **`L1`** (`Tab`) | `Tab` or `[` |
| **Cycle Difficulty (`EASY` / `MEDIUM` / `ALL 12`)** | **`R1`** (`Backspace`) | `Backspace` or `]` |
| **Save Stats & Exit** | **`START`** (`Return`) or **`MENU`** (`RCtrl`) | `Enter`, `Right Ctrl`, or `Q` |

---

## 3. Building & Installing

### Required Linux Desktop Packages
```bash
sudo apt update
sudo apt install -y build-essential libx11-dev libpulse-dev
```

### Build Commands
From [`eartrainer/`](.) inside the repository:
```bash
# Build both the PowKiddy V90 ARM binary (./eartrainer) and Linux x86_64 binary (./eartrainer_linux)
make

# Launch the native Linux desktop version
make run-linux

# Build the SD card package directory layout in ./sdcard_package/
make package
```

### Installing on the PowKiddy V90 SD Card
Copy the contents of [`sdcard_package/`](sdcard_package/) onto the **`main`** (`FAT32`, mounted at `/mnt`) partition of your PowKiddy V90 MicroSD card:
```bash
cp -r sdcard_package/* /media/$USER/main/
sync
```
- `sdcard_package/apps/eartrainer/eartrainer` $\rightarrow$ `/mnt/apps/eartrainer/eartrainer`
- `sdcard_package/apps/eartrainer/icon.png` $\rightarrow$ `/mnt/apps/eartrainer/icon.png`
- `sdcard_package/gmenu2x/sections/applications/eartrainer` $\rightarrow$ `/mnt/gmenu2x/sections/applications/eartrainer`
