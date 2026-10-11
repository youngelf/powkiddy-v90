# Pomodoro Work Timer for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host

![Screenshot of pomodoro](/pomodoro/promo/pomodoro_grab.png)


A Pomodoro timer and work-hours tracker written in C (`pom1.c`) that builds from the exact same source file for:
1. **PowKiddy V90** running **MiyooCFW 1.3.3** (320×240 16-bit display, Allwinner F1C100S ARM926EJ-S, uClibc + SDL 1.2).
2. **Linux Host (`x86_64`)** using a native X11 window (scaled 2× to 640×480 by default) for instant local testing and debugging without needing to copy files to the SD card.

---

## Host Dependencies

**No additional packages need to be installed on this machine!**
Your machine already has `gcc` and `libx11-dev` (`libX11`) installed, which is all the Linux target (`pom1_linux`) requires.

*(If you ever build on a fresh Debian/Ubuntu machine, the only host dependencies are `sudo apt-get install build-essential libx11-dev`.)*

---

## Building & Running on Linux (for Testing / Debugging)

To compile the native x86_64 Linux binary (`pom1_linux` with debug symbols `-g` enabled):

```bash
make linux
```

To run it directly in an X11 window on your Linux desktop:

```bash
./pom1_linux
# or:
make run-linux
```

You can also customize the window scaling factor (default is `HOST_SCALE=2`, which renders the 320×240 screen in a crisp 640×480 window):

```bash
make linux HOST_SCALE=3
```

### Controls (PowKiddy V90 vs. Linux Keyboard)

Both the V90 hardware keys (`Left Alt`, `Left Ctrl`, etc.) and convenient letter keys (`A`, `B`, `X`, `Y`, `R`, `Q`) work out of the box:

| Action | PowKiddy V90 Button | Linux Host Keyboard |
| :--- | :--- | :--- |
| **Toggle Start / Stop (Pause)** | **A Button** | `A` or `Left Alt` |
| **Switch Mode** (Work / Short Break / Long Break) | **X Button** | `X` or `Left Shift` |
| **Reset Current Timer** | **Y Button** | `Y`, `R`, or `Space` |
| **Stop / Pause Timer** | **B Button** | `B` or `Left Ctrl` |
| **Adjust Duration** ($\pm 1$ min while stopped) | **D-Pad UP / DOWN** | `Up Arrow` / `Down Arrow` |
| **Save & Exit** | **SELECT** | `Esc`, `Q`, or close window |

---

## Persistent Work Hours Tracking

- Displays your current status (`READY TO WORK`, `WORKING (RUNNING)`, `PAUSED (STOPPED)`, `ON BREAK (RUNNING)`, or `WORK COMPLETE!`), a large `MM:SS` countdown with progress bar, and a statistics panel showing total hours worked (`WORKED: X.XX HRS`), exact `Xh YYm ZZs`, completed Pomodoros, today's hours, and current session time.
- Automatically saves every 30 seconds while working, plus on pause/stop/exit:
  - **On PowKiddy V90**: Saves to `/mnt/.pom1_stats.cfg` and `/mnt/pom1_work_log.txt` on the SD card.
  - **On Linux Host**: Saves to `~/.pom1_stats.cfg` and `~/pom1_work_log.txt` in your `$HOME` directory.

---

## Building & Installing on Your PowKiddy V90

To build the ARM binary (`pom1`) and create the SD card package folder (`sdcard_package/`):

```bash
make package
```

1. Insert your PowKiddy V90 SD card into your computer and mount the **`main`** partition (partition 4, FAT32, mounted as `/mnt` on the device).
2. Copy the contents of `sdcard_package/` to the root of the `main` partition:
   ```bash
   cp -r sdcard_package/* /path/to/sdcard/main/
   ```
   This places:
   - `apps/pom1/pom1` (ARM executable binary)
   - `apps/pom1/pom1.png` (32×32 icon)
   - `gmenu2x/sections/applications/pom1` (GMenu2X launcher shortcut)
3. Safely eject the SD card, insert it into your PowKiddy V90, and boot up.
4. Navigate to the **applications** section in GMenu2X and launch **Pomodoro**.
