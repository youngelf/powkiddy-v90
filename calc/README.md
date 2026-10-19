# PowKiddy V90 Desk & Scientific Calculator (`calc`)

A complete redesign and replacement for the stock `apps/calc/calc` (`bittboy-calc`) application shipped on the **PowKiddy V90 / Q90** running **MiyooCFW 1.3.3** (320×240 16-bit display, Allwinner F1C100S `ARM926EJ-S`), with a native **x86_64 Linux (`X11`)** target (`calc_linux`) for desktop use and testing.

---

## Before & After Comparison

| Stock Firmware Calculator (`bittboy-calc`) | Upgraded Basic Mode (`BASIC`) | Upgraded Scientific Mode (`SCI`) |
| :---: | :---: | :---: |
| ![Stock Calculator](promo/calc_before.png) | ![Basic Calculator Mode](promo/calc_desk.png) | ![Scientific Calculator Mode](promo/calc_sci.png) |

---

## What Was Wrong with the Stock Firmware Calculator (`apps/calc/calc`)

Disassembly of the stock `/mnt/apps/calc/calc` ELF binary (`bittboy-calc/main.c`) revealed several UI and usability flaws:
1. **Wasted Screen Space & Borders**: `draw_ui()` filled the 320×240 screen with dull medium gray (`RGB(64, 64, 64)`), leaving a **16px gray border** along the top, left, and right edges, inside which it drew a **5px fat black border** (`{x=16, y=16, w=288, h=48}`) around a glaring white box (`{x=21, y=21, w=278, h=38}`).
2. **Microscopic Text & Floating Buttons**: Loaded `LiberationMono-Regular.ttf` at just **12pt** for both the numeric readout and the tiny 32×32 black squares (`RGB(0, 0, 0)`), making numbers difficult to read on the 320×240 LCD.
3. **No Scientific Functions & `%.22g` Float Noise**: Only supported `+`, `-`, `*`, `/`, `sqrt`, and `%`, and formatted numbers with `"%.22g"`, causing floating-point representation noise (e.g., `0.1 + 0.2` displaying `0.3000000000000000444089`).
4. **Inconsistent Button Mapping**: Only `B` (`SDLK_LCTRL`) pressed buttons while `A` (`SDLK_LALT`) and `START` (`SDLK_RETURN`) did nothing, and `SELECT` (`SDLK_ESCAPE`) quit the app.

---

## Key Improvements

1. **Modern High-Contrast Color Scheme & Beveled Keycaps**:
   - Deep midnight-slate chassis (`#0F1420`) with a subtle gradient top display panel (`#141C2E` to `#1B263C`).
   - Color-coded 3D beveled keycaps with a glowing golden-amber 2px focus frame (`#FACC15`):
     - **Digits (`0`–`9`, `.`, `+/-`)**: Slate charcoal (`#2A364E`)
     - **Arithmetic Operators (`/`, `*`, `-`, `+`)**: Ocean blue (`#1D4ED8`, glowing `#3B82F6` when pending)
     - **Equals (`=`)**: Emerald green (`#059669`)
     - **Clear & Backspace (`C`, `DEL`)**: Crimson red (`#B91C1C` / `#91202E`)
     - **Memory & Percent (`MC`, `MR`, `M+`, `M-`, `%`)**: Deep slate (`#1E293E`) with amber-gold text (`#FBBF24`)
     - **Scientific Functions**: Deep indigo-violet (`#342760`) with soft lavender text (`#E0E7FF`)
2. **Edge-to-Edge Large Display (No Top Gray or Fat Black Border)**:
   - Removed the 16px outer gray border and 5px black border; the display spans the full **320×72 px** top edge (`x=0..319, y=0..71`).
   - Renders anti-aliased **30px bold** digits (automatically stepping down to 22px / 16px for long numbers) alongside a top status bar with **`BASIC` / `SCI` mode indicator pills**, a **Memory (`M`)** badge, and right-aligned **expression history** (`128 * 45 =`, `sin(45 deg)`).
3. **Full Scientific Math Library (`<math.h>` / `-lm`) & `SELECT` / `Home` Mode Switch**:
   - Press **`SELECT`** (`SDLK_ESCAPE` on V90, or **`Home`** / **`Esc`** on Linux desktop) at any time to switch between **Basic Calculator (`BASIC`, 5×5 grid)** and **Scientific Calculator (`SCI`, 5×8 grid)** while keeping your cursor on the corresponding numpad key.
   - **Trigonometric & Inverse Trigonometric** (in `DEG` or `RAD` mode): `sin`, `cos`, `tan`, `asin`, `acos`, `atan` (with exact zero/asymptote handling at $90^\circ, 180^\circ, 270^\circ$).
   - **Logarithmic & Exponential**: `log` ($\log_{10}$), `ln` ($\ln$), `exp` ($e^x$), `10^x` ($10^x$).
   - **Powers, Roots & Factorials**: `x^y` (`pow`), `x^2`, `sqrt` ($\sqrt{x}$), `cbrt` ($\sqrt[3]{x}$), `1/x`, `n!` (exact integer factorial up to $20!$ and $\Gamma(x+1)$ via `tgamma` for real values).
   - **Modulo, Percentage & Constants**: `mod` (`fmod`), `%`, `pi` ($\pi$), `e` ($e$).
4. **Updated Quit & Action Controls**:
   - **`START`** (`SDLK_RETURN`) quits the calculator.
   - **`MENU`** (`SDLK_RCTRL`) also quits the calculator.
   - **`SELECT`** (`SDLK_ESCAPE`) switches between Desk Calculator and Scientific Calculator modes.

---

## Controls

| Action | PowKiddy V90 Button | Linux Desktop Key (`calc_linux`) |
| :--- | :--- | :--- |
| **Navigate Keypad** | **D-Pad (`UP` / `DOWN` / `LEFT` / `RIGHT`)** | `Arrow Keys` |
| **Press Selected Button** | **`A`** (`LAlt`) or **`B`** (`LCtrl`) | `A`, `B`, `Space`, `Left Alt`, `Left Ctrl`, or direct keys (`0`-`9`, `+`, `-`, `*`, `/`, `=`) |
| **Switch Mode (`BASIC` $\leftrightarrow$ `SCI`)** | **`SELECT`** (`Escape`) | `Home` or `Esc` |
| **Clear All (`C` / `AC`)** | **`X`** (`LShift`) | `X`, `Left Shift`, or `C` |
| **Delete Last Digit (`DEL`)** | **`Y`** (`Space`) or **`R1`** (`Backspace`) | `Y` or `Backspace` |
| **Toggle `DEG` / `RAD`** | **`L1`** (`Tab`) | `Tab` |
| **Quit Calculator** | **`START`** (`Return`) or **`MENU`** (`RCtrl`) | `Enter`, `Right Ctrl`, or `Q` |

---

## Building

From [`calc/`](.) inside the top-level repository:

```bash
# Build both the PowKiddy V90 ARM binary (calc) and Linux desktop binary (calc_linux)
make

# Build and run the native Linux X11 desktop version
make run-linux

# Build the SD card directory layout in ./sdcard_package/
make package
```

### Installing on the PowKiddy V90 SD Card

Copy the contents of [`sdcard_package/`](sdcard_package/) to the `main` (`FAT32`, mounted at `/mnt`) partition of the PowKiddy V90 SD card:
- `sdcard_package/apps/calc/calc` $\rightarrow$ `/mnt/apps/calc/calc`
- `sdcard_package/apps/calc/icon.png` $\rightarrow$ `/mnt/apps/calc/icon.png`
- `sdcard_package/gmenu2x/sections/applications/calc` $\rightarrow$ `/mnt/gmenu2x/sections/applications/calc`
