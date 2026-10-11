# PowKiddy V90 Metronome (MiyooCFW 1.3.3 & Linux Desktop)

![Screenshot of metronome](/metronome/promo/metronome_grab.png)

A lightweight, precision C metronome designed for the **PowKiddy V90** handheld running **MiyooCFW 1.3.3** (320×240 16-bit display, Allwinner F1C100S ARM926EJ-S), with a native **x86_64 Linux (X11)** target for fast desktop testing and debugging.

## Features
- **Tempo Control**: Adjustable from **20 BPM to 300 BPM** with classical tempo markings (*Grave*, *Largo*, *Adagio*, *Andante*, *Moderato*, *Allegro*, *Vivace*, *Presto*, *Prestissimo*).
- **5 Time Signatures**: Cycle between **`4/4`**, **`3/4`**, **`2/4`**, **`5/4`**, and **`7/4`** time.
- **Beat 1 Accent**:
  - **Audio**: Synthesized high-pitched accent click (**1320 Hz / E6**) on Beat 1 and standard click (**880 Hz / A5**) on subsequent beats.
  - **Visual**: Dynamic beat boxes that flash bright red (`ACC`) on Beat 1 and bright green on beats 2..N, paired with a smooth swinging pendulum indicator.
- **Persistent Settings**: Automatically saves your BPM and selected time signature (`/mnt/.metronome.cfg` on PowKiddy V90, `~/.metronome.cfg` on Linux).

---

## Controls

| Action | PowKiddy V90 Button | Linux Desktop Key |
| :--- | :--- | :--- |
| **Play / Pause** | **A** | `A`, `Space`, `Enter`, or `Left Alt` |
| **Increase / Decrease Tempo (±1 BPM)** | **UP** / **DOWN** | `Up Arrow` / `Down Arrow` |
| **Cycle Time Signature (`4/4`, `3/4`, `2/4`, `5/4`, `7/4`)** | **LEFT** / **RIGHT** | `Left Arrow` / `Right Arrow` |
| **Fast Tempo Step (±5 BPM)** | **R1** / **L1** | `Page Up` / `Page Down` or `]` / `[` |
| **Reset to 120 BPM** | **Y** | `R` or `Left Shift` |
| **Save & Quit** | **SELECT** or **START + SELECT** | `Q` or `ESC` |

---

## Building & Running on Linux (x86_64 Host Target)

The Linux target uses native **X11 (`libX11`)** for rendering (scaled 2× to 640×480 for desktop visibility) and dynamically loads `libpulse-simple.so.0` at runtime if available for audio clicks.

### Dependencies
On this machine, `libx11-dev` and `libpulse-simple.so.0` are **already installed**, so no extra packages are needed. On a fresh Debian/Ubuntu machine:
```bash
sudo apt-get update && sudo apt-get install -y build-essential libx11-dev
```

### Build & Run
```bash
# Build the x86_64 Linux binary (./metronome_linux)
make linux

# Or build and launch immediately
make run-linux
```

---

## Building & Installing on PowKiddy V90 (MiyooCFW 1.3.3)

### Build Cross-Compiled Binary & SD Card Package
```bash
# Build the ARM binary (./metronome)
make v90

# Create the GMenu2X SD card directory structure in ./sdcard_package/
make package
```

### Copy to SD Card
Copy the contents of `sdcard_package/` to the `main` (`FAT32` `/mnt`) partition of your PowKiddy V90 SD card:
- `sdcard_package/apps/metronome/metronome` -> `/mnt/apps/metronome/metronome`
- `sdcard_package/apps/metronome/metronome.png` -> `/mnt/apps/metronome/metronome.png`
- `sdcard_package/gmenu2x/sections/applications/metronome.lnk` -> `/mnt/gmenu2x/sections/applications/metronome.lnk`
