# SightRead — Guitar Staff-to-Fretboard Sight-Reading Trainer (`sightread`)

A native C/SDL 1.2 guitar sight-reading trainer for the **PowKiddy V90 / Q90** (`320×240` RGB565, Allwinner F1C100S `ARM926EJ-S`) and **Linux Desktop (`x86_64` X11)**.

`sightread` displays two synchronized representations of a short musical piece:
1. **Top 70% of the Screen (`y: 24..164`) — Interactive Treble Clef Musical Staff (`8vb`)**:
   - Prominent 5-line treble staff (`8px` line spacing, `4px` per diatonic step) with key signature (`C Maj`, `G (1#)`, `D (2#)`, `A (3#)`, `E (4#)`, `F (1b)`, `Bb (2b)`), `4/4` time signature, ledger lines (`E2` through `E5`), quarter notes, and beamed eighth-note pairs.
   - Move **`LEFT` / `RIGHT`** across the staff to highlight any note, press **`B`** to play that single note on the guitar, or press **`A`** to play/pause the entire piece in time with the metronome.
2. **Bottom 30% of the Screen (`y: 166..215`) — Non-Interactive 12-Fret Guitar Neck Reference**:
   - Shows the fret positions of all notes used in the piece strictly within the selected **Neck Position** (`OPEN (0-4fr)`, `POS 2 (2-5fr)`, `POS 5 (5-8fr)`, `POS 7 (7-10fr)`, `POS 9 (9-12fr)`), and brightly highlights the currently selected/playing staff note with its left-hand finger number (`0=Open`, `1–4`).
3. **Programmatically Generated Progressive Exercises (`L1` / `R1`)**:
   - Deterministically generates `EX #01` through `EX #99` across 4 progressive difficulty levels (`LVL1` Quarter-Motion Foundation $\rightarrow$ `LVL2` Mixed Quarters & Beamed Eighths $\rightarrow$ `LVL3` Syncopated Eighths & Arpeggio Skips $\rightarrow$ `LVL4` Full-Position Etudes) in all 5 neck positions and 7 key signatures.
4. **100% Background-Thread Music Synthesis**:
   - All 6-voice Karplus-Strong guitar synthesis runs on a dedicated background worker thread (`synth_worker_thread_fn`) with atomic generation preemption (`g_synth_gen`).

---

## Screenshots

| Open Position (`EX #01 LVL1`, `C Maj`) | 5th Position (`EX #07 LVL2`, `G (1#)`, Playing) | 7th Position (`EX #16 LVL4`, `D (2#)`) | Controls & Guide (`Y`) |
| :---: | :---: | :---: | :---: |
| ![Open Position](promo/sightread_open.png) | ![5th Position](promo/sightread_pos5.png) | ![7th Position](promo/sightread_advanced.png) | ![Controls & Guide](promo/sightread_help.png) |

---

## Controls

| V90 Button | Linux Desktop (`X11`) | Action |
| :--- | :--- | :--- |
| **`A`** (`LALT`) | `Space` / `Return` / `A` / `Z` | **Play / Pause** the entire musical piece |
| **`B`** (`LCTRL`) | `B` / `X` / `BackSpace` | **Play Single Highlighted Note** on the staff & highlight it on the fretboard |
| **`LEFT` / `RIGHT`** | `Left` / `Right` Arrows | Move highlight cursor **previous / next note** on the Treble Staff |
| **`UP` / `DOWN`** | `Up` / `Down` Arrows | Increase / Decrease **Tempo (`40–180 BPM`)** |
| **`L1` / `R1`** | `Tab` / `Q` (`L1`) and `E` / `W` (`R1`) (or `,` / `.`, `[` / `]`) | **Previous / Next Exercise (`EX #01..#99`)** with progressive complexity |
| **`SELECT`** (`ESC`) | `S` / `C` / `Shift` | **Cycle Guitar Neck Position** (`OPEN (0-4fr)`, `POS 2`, `POS 5`, `POS 7`, `POS 9`) |
| **`X`** (`LSHIFT`) | `K` / `V` | **Cycle Key Signature** (`C Maj`, `G (1#)`, `D (2#)`, `A (3#)`, `E (4#)`, `F (1b)`, `Bb (2b)`) |
| **`Y`** (`SPACE`) | `Y` / `H` / `F1` / `/` | Toggle **Help & Sight-Reading Guide** overlay |
| **`MENU`** (`RCTRL`) | `Escape` | Save settings & **Quit** |

---

## Building & Verification

```bash
# Build both PowKiddy V90 (ARM) and Linux Desktop (x86_64 X11) binaries, run verification, and create sdcard_package/
make clean all package

# Run automated verification of all 5 neck positions x 7 keys x 20 exercises
make test
```

## Installing on the PowKiddy V90 MicroSD Card

Copy the contents of `sdcard_package/` onto the `main` (`fat32`) partition of your MiyooCFW MicroSD card:

```
/mnt/apps/sightread/sightread
/mnt/apps/sightread/icon.png
/mnt/gmenu2x/sections/applications/sightread.lnk
```
