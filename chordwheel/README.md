# PowKiddy V90 Interactive Circle of Fifths (`chordwheel`)

An interactive **Circle of Fifths, Guitar Voicing Visualizer, and 6-String Karplus-Strong Plucked Guitar Synthesizer** ([`chordwheel.c`](chordwheel.c)) built for the **PowKiddy V90 / Q90** running **MiyooCFW 1.3.3** (`320×240` 16-bit display, Allwinner F1C100S `ARM926EJ-S`), with a native **x86_64 Linux (`X11`)** target (`chordwheel_linux`) for desktop experimentation and debugging.

---

## Screenshots

| Key of C Major (`Triads`, Open `C`) | Key of G Major Locked (`7ths`, `Am7` 5fr E-Barre) | Key of F Major Auto-Loop (`Bb` A-Barre) | Help, Controls & Progression Looper (`Y`) |
| :---: | :---: | :---: | :---: |
| ![Main View](promo/chordwheel_main.png) | ![7ths & Barre View](promo/chordwheel_7ths.png) | ![Progression Loop View](promo/chordwheel_loop.png) | ![Help Modal](promo/chordwheel_help.png) |

---

## Features

1. **Interactive 12-Key Circle of Fifths Wheel**:
   - **Outer Ring**: All 12 Major keys (`C, G, D, A, E, B, F#, Db, Ab, Eb, Bb, F`).
   - **Inner Ring**: All 12 Relative Minor keys (`Am, Em, Bm, F#m, C#m, G#m, D#m, Bbm, Fm, Cm, Gm, Dm`).
   - **Center Hub**: Displays the active Key Center, Key Signature (`0 #/b` .. `6 #` / `5 b`), and the Leading-Tone Diminished chord (`vii°` / `viim7b5`).
   - **Harmonic Function Color-Coding**: Highlights the 6-sector diatonic pie window in the active key:
     - **Gold**: Tonic (`I` / `vi`)
     - **Blue**: Subdominant (`IV` / `ii`)
     - **Emerald**: Dominant (`V` / `iii`)
2. **Two Navigation Modes (`SELECT` Toggle)**:
   - **Key-Rotate Mode (`SEL:ROTATE`)**: Rotating `LEFT` / `RIGHT` moves the entire Key Signature window around the Circle of Fifths and automatically transposes the 4-bar chord progression at the bottom of the screen.
   - **Chord-Explore Mode (`SEL:LOCKED`)**: Locks the active Key Signature while letting you move the cursor freely across `I`, `ii`, `iii`, `IV`, `V`, `vi`, `vii°`, secondary dominants (`V/V`, `V/vi`, `V/ii`), and borrowed chords (`bVII`, `bVI`, `iv`).
3. **6-String Guitar Fretboard Chord Diagrams (`L1` / `R1`)**:
   - Displays a 6-string × 4-fret guitar chord box (`E A D G B E`) with nut / base-fret numbers (`5fr`), muted (`x`) and open (`o`) string indicators, root dots in Gold, and chord-tone dots in Cyan.
   - Cycle through **3 playable guitar voicings** for every chord using **`L1` / `R1`**:
     1. **`1/3 Open/CAGED`**
     2. **`2/3 E-Barre (6str)`** (6th-string root movable barre)
     3. **`3/3 A-Barre (5str)`** (5th-string root movable barre)
4. **Triads & 7th Chords (`B` Button)**:
   - Toggle instantly between **Triads** (`C, Dm, Em, F, G, Am, Bdim`) and **7th Chords** (`Cmaj7, Dm7, Em7, Fmaj7, G7, Am7, Bm7b5`).
5. **6-String Fixed-Point Karplus-Strong Guitar Synthesizer (`A` Button)**:
   - Pressing **`A`** strums the exact 6-string guitar voicing displayed on screen with a 22 ms pick-stagger downstroke using pure integer waveguide synthesis (< 1.5% CPU on the V90's ARM926EJ-S, zero external audio files).
6. **4-Chord Progression Bar & Auto-Strum Looper (`X` & `Y`)**:
   - Queue any chord into the 4-slot progression bar at the bottom with **`X`**, or open the **`Y: Help`** modal to pick from 5 classic guitar progression templates (`I-V-vi-IV`, `I-IV-V-I`, `ii-V-I-vi`, `I-vi-IV-V`, `vi-IV-I-V`) and toggle the automatic progression looper.
7. **Persistent State**:
   - Automatically saves your key, cursor, Triad/7th mode, voicing choice, and 4-chord progression to `/mnt/.chordwheel.cfg` on the PowKiddy V90 (`~/.chordwheel.cfg` on Linux desktop).

---

## Controls

| Action | PowKiddy V90 Button | Linux Desktop Key (`chordwheel_linux`) |
| :--- | :--- | :--- |
| **Rotate Wheel (4ths / 5ths)** | **`DPAD LEFT` / `RIGHT`** | `Left` / `Right Arrow` |
| **Switch Ring (Major / Minor / `vii°` Dim)** | **`DPAD UP` / `DOWN`** | `Up` / `Down Arrow` |
| **Strum Selected Guitar Voicing** | **`A`** (`LAlt`) | `A` or `Left Alt` |
| **Toggle Triads $\leftrightarrow$ 7th Chords** | **`B`** (`LCtrl`) | `B` or `Left Ctrl` |
| **Cycle Guitar Voicing (`Open` / `E-Barre` / `A-Barre`)** | **`L1` / `R1`** (`Tab` / `BS`) | `Tab` / `BackSpace` or `[` / `]` |
| **Lock / Unlock Key Center (`ROTATE` $\leftrightarrow$ `LOCKED`)** | **`SELECT`** (`Escape`) | `Esc`, `Home`, or `C` |
| **Add Selected Chord to 4-Bar Progression** | **`X`** (`LShift`) | `X` or `Left Shift` |
| **Toggle 4-Bar Progression Auto-Looper** | **`A`** inside **`Y: Help`** | `P` (or `A` inside `Y: Help`) |
| **Open / Close Help & Presets Modal** | **`Y`** (`Space`) | `Y`, `H`, or `Space` |
| **Save & Quit** | **`START`** (`Return`) or **`MENU`** (`RCtrl`) | `Enter`, `Right Ctrl`, or `Q` |

---

## Building & Running

From [`chordwheel/`](.) inside the repository:

```bash
# Build both PowKiddy V90 ARM binary (./chordwheel) and Linux x86_64 binary (./chordwheel_linux)
make

# Launch the native Linux desktop version (640x480 X11 window + PulseAudio guitar synth)
make run-linux

# Build the PowKiddy V90 SD card package in ./sdcard_package/
make package
```

### Installing on the PowKiddy V90 SD Card

Copy the contents of [`sdcard_package/`](sdcard_package/) onto the **`main`** (`FAT32`, mounted at `/mnt`) partition of your PowKiddy V90 MicroSD card:
- `sdcard_package/apps/chordwheel/chordwheel` $\rightarrow$ `/mnt/apps/chordwheel/chordwheel`
- `sdcard_package/apps/chordwheel/icon.png` $\rightarrow$ `/mnt/apps/chordwheel/icon.png`
- `sdcard_package/gmenu2x/sections/applications/chordwheel` $\rightarrow$ `/mnt/gmenu2x/sections/applications/chordwheel`
