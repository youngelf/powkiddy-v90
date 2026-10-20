# pomodoro — Pomodoro Work Timer for PowKiddy V90 (MiyooCFW 1.3.3), Anbernic RG35XX SP (Knulli), & Linux Host

A Pomodoro timer and work-hours tracker written in C (`pomodoro.c`) that builds from the exact same source file for:
1. **PowKiddy V90** running **MiyooCFW 1.3.3** (320×240 16-bit display, Allwinner F1C100S ARM926EJ-S, uClibc + SDL 1.2).
2. **Anbernic RG35XX SP** running **Knulli (Scarab / Batocera)** (640×480 32-bit IPS display, Allwinner H700 Cortex-A53 `aarch64`, SDL2 / `/dev/fb0` + Linux `evdev`). Also available in its own dedicated directory at `~/xkdr/pom1_35xxsp`.
3. **Linux Host (`x86_64`)** using a native X11 window (scaled 2× to 640×480 by default) for instant local testing and debugging without needing to copy files to the SD card.

---

## Software Requirements on Linux (Ubuntu / gLinux x86_64)

### 1. System Packages (`apt`)

Install the standard build tools, X11 development headers (for `pomodoro_linux`), SquashFS tools (for `make opk`), and the 64-bit ARM cross-compiler (for the Anbernic RG35XX SP):

```bash
sudo apt update
sudo apt install -y build-essential libx11-dev squashfs-tools gcc-aarch64-linux-gnu libc6-dev-arm64-cross curl tar python3
```

- **`build-essential`**: Provides `gcc` and `make`.
- **`libx11-dev`**: Provides X11 headers and `libX11` for building `pomodoro_linux`.
- **`squashfs-tools`**: Provides `mksquashfs` for building `pomodoro.opk` (`make opk`).
- **`gcc-aarch64-linux-gnu` & `libc6-dev-arm64-cross`**: Provides `aarch64-linux-gnu-gcc` for building the Anbernic RG35XX SP binary (`make 35xxsp`).
- **`curl`, `tar`, `python3`**: Used to fetch and extract the MiyooCFW cross-compilation SDK below.

### 2. PowKiddy V90 Cross-Compilation Toolchain (`arm-miyoo-linux-uclibcgnueabi`)

> **Why Ubuntu's `gcc-arm-linux-gnueabi` cannot be used for the V90:**  
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

## Controls (PowKiddy V90, Anbernic RG35XX SP, & Linux Keyboard)

| Action | PowKiddy V90 | Anbernic RG35XX SP | Linux Host Keyboard |
| :--- | :--- | :--- | :--- |
| **Toggle Start / Stop (Pause)** | **A Button** | **A Button** | `A`, `Space`, or `Left Alt` |
| **Switch Mode** (Work / Short Break / Long Break) | **X Button** or **LEFT / RIGHT** | **X Button** or **D-Pad LEFT / RIGHT** | `X`, `Left Shift`, or `Left` / `Right Arrow` |
| **Reset Current Timer** | **Y Button** | **Y Button** | `Y` or `R` |
| **Stop / Pause Timer** | **B Button** | **B Button** | `B` or `Left Ctrl` |
| **Adjust Duration** ($\pm 1$ min while stopped) | **D-Pad UP / DOWN** or **R1 / L1** | **D-Pad UP / DOWN** or **R1 / L1** | `Up` / `Down Arrow` or `Backspace` / `Tab` |
| **Fast Adjust Duration** ($\pm 5$ min while stopped) | **R2 / L2** | **R2 / L2** | `Page Down` / `Page Up` |
| **Save & Exit** | **START** | **SELECT**, **MENU (`M`)**, or **START + SELECT** | `Enter`, `Esc`, `Q`, or close window |

---

## Building & Running on Linux (for Testing / Debugging)

To compile the native x86_64 Linux binary (`pomodoro_linux` with debug symbols `-g` enabled):

```bash
make linux
```

To run it directly in an X11 window on your Linux desktop:

```bash
./pomodoro_linux
# or:
make run-linux
```

---

## Building & Installing on Your Anbernic RG35XX SP (Knulli / Batocera)

A dedicated directory for the RG35XX SP is located at **`~/xkdr/pom1_35xxsp`** (or you can run `make package-35xxsp` in `~/xkdr/pom1`):

```bash
cd ~/xkdr/pom1_35xxsp
make
```

Copy the contents of `sdcard_package/roms/ports/` to the **`roms/ports/`** directory on your Knulli SD card (`SHARE` / `/userdata` partition):
- `Pomodoro.sh` -> `/userdata/roms/ports/Pomodoro.sh` (or `roms/ports/Pomodoro.sh` on the SD card)
- `pomodoro/` -> `/userdata/roms/ports/pomodoro/` (contains the `aarch64` `pomodoro` binary and `pomodoro.png`)

Then in Knulli (EmulationStation), press **START -> Game Settings -> Update Games Lists**, open **Ports**, and launch **Pomodoro**.

---

## Building & Installing on Your PowKiddy V90 (MiyooCFW 1.3.3)

### Option A: `.opk` Package (`make opk`)

To build a single self-contained OpenPackage (`pomodoro.opk`, SquashFS 4.0 `gzip`-compressed with `default.miyoo.desktop`, `pomodoro`, and `pomodoro.png`):

```bash
make opk
```

1. Copy `pomodoro.opk` to the `apps/` folder on your PowKiddy V90 **`main`** SD card partition (`/mnt/apps/pomodoro.opk`).
2. In **GMenu2X**, either launch `pomodoro.opk` from the Explorer or run the **OPK Scanner / Update OPK links** option to automatically add **Pomodoro** to the **applications** section.

### Option B: Direct SD Card Directory (`make package`)

To build the ARM binary (`pomodoro`) and create the unpacked SD card folder (`sdcard_package/`):

```bash
make package
```

Copy the contents of `sdcard_package/` to the root of the V90 `main` partition:
```bash
cp -r sdcard_package/* /path/to/sdcard/main/
```
