# Guitar Speed-Builder Practice Routine & Lick Coach (`licktrainer`)

An interactive **6-string guitar tab player, live fretboard position coach, and automated BPM speed-builder** for the **PowKiddy V90** handheld (`MiyooCFW 1.3.3`, `320×240` 16-bit RGB565 display, Allwinner F1C100S `ARM926EJ-S`) and **Linux Desktop (`x86_64` X11)**.

| Pentatonic Licks (`#01` Gilbert 4-Note Seq) | Legato & Arpeggios (`#10` 5-Str Sweep) | Warmup & Spider (`#21` 1-2-3-4 Spider) | Controls & Practice Guide (`Y`) |
| :---: | :---: | :---: | :---: |
| ![Pentatonic Licks](promo/licktrainer_pentatonic.png) | ![Legato & Arpeggios](promo/licktrainer_legato.png) | ![Warmup & Spider](promo/licktrainer_spider.png) | ![Controls & Guide](promo/licktrainer_help.png) |

---

## Key Features

- **25 Built-In Guitar Exercises Across 5 Categories (`L1` / `R1` to switch category, `LEFT` / `RIGHT` to pick lick)**:
  1. **`PENTATONIC` (`#01`–`#05`)**: *Gilbert 4-Note Seq*, *Eric Johnson 5s*, *Pentatonic Threes (12/8)*, *Wide String Skip*, *Box 1-to-2 Slide*.
  2. **`LEGATO/ARP` (`#06`–`#10`)**: *Legato 3-NPS Roll*, *Minor Triad Arp*, *Min7 Skipped Arp*, *Dim7 Symmetry Arp*, *5-Str Sweep Shape*.
  3. **`ALT PICK` (`#11`–`#15`)**: *Gilbert 3-NPS Cell*, *Neoclassic Pedal*, *Diatonic 4-Note*, *Di Meola Syncopa*, *Cross-String Jump*.
  4. **`BLUES/ROCK` (`#16`–`#20`)**: *BB & Albert Turn*, *SRV Texas Shuffle*, *Dorian Mixo Run*, *Hendrix Chord Embl*, *Blues b5 Cascade*.
  5. **`SPIDER` (`#21`–`#25`)**: *1-2-3-4 Spider*, *1-3-2-4 Permute*, *Skipping Spider*, *Diagonal Spider*, *4-3-2-1 Pinky Work*.
- **6-String Guitar Tab with Picking & Left-Hand Fingerings**:
  - Displays a 12-step (triplet) or 16-step (16th-note) 6-string tab staff (`e B G D A E`) with a live highlighted step playhead.
  - **Picking & Articulation Row (`PK`)**: Shows `D` (Downstroke), `U` (Upstroke), `H` (Hammer-on), `P` (Pull-off), and `/` (Slide) above every note.
  - **Left-Hand Finger Guide (`FG`)**: Shows recommended left-hand finger (`1` Index, `2` Middle, `3` Ring, `4` Pinky) below every note.
- **Live 6-String Guitar Neck Position Window**:
  - Displays a zoomed 8-fret neck window centered on the exercise's active position, showing all notes in the lick as ghosted cyan rings and lighting up the currently played note in bright red/white with its left-hand finger (`1–4`), note name, and fret number.
- **Automated Speed-Builder BPM Ladder (`X`)**:
  - Cycle between `OFF (STATIC BPM)`, `+2 BPM / 2 BARS`, `+5 BPM / 4 BARS`, `+5 BPM / 8 BARS`, and `+10 BPM / 4 BARS` to automatically ramp up your picking speed as you repeat the loop.
- **Background-Thread Karplus-Strong Guitar Synth & Metronome (`B` to cycle mix)**:
  - Pre-synthesizes every step's plucked/legato guitar string waveform in a dedicated background worker thread (`synth_worker_thread_fn`) with atomic preemption so the UI never blocks when switching licks or transposing keys (`SELECT`).
  - Cycle audio mix between `TAB+CLK` (guitar synth + metronome click), `CLK ONLY` (metronome + visual cursor only), and `TAB ONLY`.

---

## Controls

| Action | PowKiddy V90 Button | Linux Desktop Key |
| :--- | :--- | :--- |
| **Play / Pause (with 4-beat count-in)** | **A** (`SDLK_LALT`) | `A`, `Return`, or `Left Alt` |
| **Cycle Audio Mix (`TAB+CLK` / `CLK ONLY` / `TAB ONLY`)** | **B** (`SDLK_LCTRL`) | `B` or `Left Ctrl` |
| **Cycle Speed-Builder Ladder (`OFF`, `+2/2b`, `+5/4b`, `+5/8b`, `+10/4b`)** | **X** (`SDLK_LSHIFT`) | `X` or `Left Shift` |
| **Toggle Help & Practice Guide Overlay** | **Y** (`SDLK_SPACE`) | `Y`, `H`, or `Space` |
| **Previous / Next Category (`1..5`)** | **L1** / **R1** (`SDLK_TAB` / `SDLK_BACKSPACE`) | `Tab` / `Backspace`, `[` / `]`, or `PageUp` / `PageDown` |
| **Previous / Next Lick in Category** | **D-Pad LEFT** / **RIGHT** | `Left Arrow` / `Right Arrow` |
| **Increase / Decrease Tempo (`BPM`)** | **D-Pad UP** / **DOWN** | `Up Arrow` / `Down Arrow` |
| **Transpose Key / Neck Fret Position (`E..Eb`)** | **SELECT** (`SDLK_ESCAPE`) | `C` or `Escape` |
| **Save Settings & Exit** | **START** (`SDLK_RETURN`) | `Q` |

---

## Building & Running

```bash
# Build both the PowKiddy V90 ARM binary (licktrainer) and Linux x86_64 binary (licktrainer_linux)
make

# Launch on the Linux desktop
make run-linux

# Build the GMenu2X SD card package in ./sdcard_package/
make package
```

### Installing on PowKiddy V90 (MiyooCFW 1.3.3)
Copy the contents of `sdcard_package/` onto the `main` (`FAT32` `/mnt`) partition of your PowKiddy V90 MicroSD card:
- `sdcard_package/apps/licktrainer/licktrainer` $\rightarrow$ `/mnt/apps/licktrainer/licktrainer`
- `sdcard_package/apps/licktrainer/icon.png` $\rightarrow$ `/mnt/apps/licktrainer/icon.png`
- `sdcard_package/gmenu2x/sections/applications/licktrainer.lnk` $\rightarrow$ `/mnt/gmenu2x/sections/applications/licktrainer.lnk`
