# PowKiddy V90 Metronome (MiyooCFW 1.3.3 & Linux Desktop)

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
| **Play / Pause** | **A** | `A`, `Space`, or `Left Alt` |
| **Increase / Decrease Tempo (±1 BPM)** | **UP** / **DOWN** | `Up Arrow` / `Down Arrow` |
| **Cycle Time Signature (`4/4`, `3/4`, `2/4`, `5/4`, `7/4`)** | **LEFT** / **RIGHT** | `Left Arrow` / `Right Arrow` |
| **Fast Tempo Step (±5 BPM)** | **R1** / **L1** | `Page Up` / `Page Down` or `]` / `[` |
| **Reset to 120 BPM** | **Y** | `R` or `Left Shift` |
| **Save & Quit** | **START** | `Enter`, `Q`, or `ESC` |

---

## Software Requirements on Linux (Ubuntu 24.04 / 26.06 x86_64)

### 1. Ubuntu System Packages (`apt`)

Install the standard build tools, X11 development headers (for the local `metronome_linux` desktop window), PulseAudio runtime/headers (for desktop audio clicks), and download utilities:

```bash
sudo apt update
sudo apt install -y build-essential libx11-dev libpulse-dev curl tar python3
```

- **`build-essential`**: Provides `gcc` and `make`.
- **`libx11-dev`**: Provides X11 headers and `libX11` for building `metronome_linux`.
- **`libpulse-dev`**: Provides `libpulse-simple.so.0` (dynamically loaded at runtime by `metronome_linux` for synthesized audio clicks on the desktop).
- **`curl`, `tar`, `python3`**: Used to fetch and extract the MiyooCFW cross-compilation SDK below.

### 2. PowKiddy V90 Cross-Compilation Toolchain (`arm-miyoo-linux-uclibcgnueabi`)

> **Why Ubuntu's `gcc-arm-linux-gnueabi` cannot be used:**  
> MiyooCFW 1.3.3 on the PowKiddy V90 (Allwinner F1C100S, `ARM926EJ-S` / `armv5te` soft-float) uses **`uClibc-ng`** (`/lib/ld-uClibc.so.0`) and **SDL 1.2** (`libSDL-1.2.so.0`). Ubuntu's `apt` cross-compiler (`gcc-arm-linux-gnueabi`) links against `glibc`, producing binaries that cannot run on MiyooCFW 1.3.3 and lacking the cross-compiled `libSDL` sysroot.

You can install the required `arm-miyoo-linux-uclibcgnueabi` SDK (Buildroot GCC 7.3.0 + uClibc + SDL 1.2 sysroot) using **either** of the two methods below:

#### Option A: Direct Extraction to `/opt/miyoo` (Recommended, No Docker Required)

Download and extract the prebuilt SDK layer from [`nfriedly/miyoo-toolchain:steward`](https://hub.docker.com/r/nfriedly/miyoo-toolchain) directly into `/opt/miyoo` (which the `Makefile` auto-detects):

```bash
sudo mkdir -p /opt/miyoo
TOKEN=$(curl -s "https://auth.docker.io/token?service=registry.docker.io&scope=repository:nfriedly/miyoo-toolchain:pull" \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['token'])")
curl -fL -H "Authorization: Bearer $TOKEN" \
  "https://registry-1.docker.io/v2/nfriedly/miyoo-toolchain/blobs/sha256:b03cca7cff1f30eb44d7d4929d65eecaa4515437b1d1b53804e2cfbdbcee51b1" \
  | sudo tar -xz -C /
```

*(If you prefer extracting without `sudo` into `/tmp/miyoo_sdk_extract`, replace `sudo tar -xz -C /` with `mkdir -p /tmp/miyoo_sdk_extract && tar -xz -C /tmp/miyoo_sdk_extract` — the `Makefile` automatically checks both `/opt/miyoo` and `/tmp/miyoo_sdk_extract/opt/miyoo`.)*

#### Option B: Using Docker (`docker.io`)

If you prefer running the cross-compiler inside a container:

```bash
sudo apt install -y docker.io
sudo docker run --rm -v "$PWD":/src -w /src nfriedly/miyoo-toolchain:steward make CHAINPREFIX=/opt/miyoo
```

---

## Building & Running on Linux (x86_64 Host Target)

The Linux target uses native **X11 (`libX11`)** for rendering (scaled 2× to 640×480 for desktop visibility) and dynamically loads `libpulse-simple.so.0` at runtime if available for audio clicks.

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
