# PowKiddy V90 Hardware Keymap & Standard App Control Reference

This reference documents the hardware-to-software button mappings on the **PowKiddy V90** running **MiyooCFW 1.3.3** (Allwinner F1C100S `ARM926EJ-S`, `miyoo_kbd` kernel driver, SDL 1.2) along with the **standard UI/UX button conventions** used across all native applications in this repository.

---

## 1. Standard Application Control Conventions

All apps in this suite follow these consistent button assignments:

| Convention # | Physical Button(s) | SDL 1.2 Keysym | Desktop Key | Standard App Role |
| :--- | :--- | :--- | :--- | :--- |
| **1a. Quit / Exit** | **`MENU / RESET`** *(small circular center button)* | `SDLK_RCTRL` (`305`) | Right `Ctrl` | **Quit / Exit** the application consistently across all apps (saving state if applicable). |
| **1b. Primary Switchers** | **D-Pad** (`UP`/`DOWN`/`LEFT`/`RIGHT`) & **`L1` / `R1`** | `SDLK_UP`, `SDLK_DOWN`, `SDLK_LEFT`, `SDLK_RIGHT`, `SDLK_TAB`, `SDLK_BACKSPACE` | Arrow Keys, `Tab`, `Backspace` | **Primary navigation, value adjustment, and parameter/category switching.** |
| **2. Help** | **`Y`** *(West / Left face button)* | `SDLK_SPACE` (`32`) | `Space` | **Toggle Help screen / overlay** consistently across all apps. |
| **3. Modal Shifts** | **`SELECT`** | `SDLK_ESCAPE` (`27`) | `Esc` | **Switch application modes** (if the app has multiple operating modes or views). |
| **4. Start / Stop Playback** | **`A`** *(East / Right face button)* | `SDLK_LALT` (`308`) | Left `Alt` | **Start / Stop audio or timer playback** (or primary confirm action). |

### Secondary / App-Specific Buttons
- **`B`** *(South / Bottom face button, `SDLK_LCTRL` / `306`, Desktop Left `Ctrl`)*: Secondary action (e.g., replay/reset, back/cancel in file browsers, or theme toggle).
- **`X`** *(North / Top face button, `SDLK_LSHIFT` / `304`, Desktop Left `Shift`)*: Tertiary action (e.g., audition/strum/tap-tempo).
- **`START`** *(`SDLK_RETURN` / `13`, Desktop `Enter`)*: Secondary confirm or control action as needed per app.
- **`L2` / `R2`** *(Inner shoulder triggers, `SDLK_PAGEUP` / `SDLK_PAGEDOWN`)*: Coarse paging or secondary cycling if needed.

---

## 2. Complete Hardware & Software Button Mapping Table

On the PowKiddy V90, the physical controls are wired directly to Allwinner F1C100S GPIO pins and polled by the `miyoo_kbd` kernel input driver (`/dev/input/event0`):

| Physical V90 Button | Location on Device | F1C100S GPIO Pin | Kernel `miyoo_kbd` Bitmask | Linux Keycode (`<linux/input.h>`) | SDL 1.2 Keysym (`SDLKey`) | Desktop (`X11`) Keysym & Key |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`MENU` / `RESET`** | Small circular center button above `SELECT`/`START` | `PE7` (`0x87`) | `0x0004` | `KEY_RIGHTCTRL` (`97`) | **`SDLK_RCTRL`** (`305`) | `XK_Control_R` (Right `Ctrl`) |
| **D-Pad Up** | Left directional pad (Up) | `PE2` (`0x82`) | `0x0008` | `KEY_UP` (`103`) | **`SDLK_UP`** (`273`) | `XK_Up` (`Up Arrow`) |
| **D-Pad Down** | Left directional pad (Down) | `PE3` (`0x83`) | `0x0800` | `KEY_DOWN` (`108`) | **`SDLK_DOWN`** (`274`) | `XK_Down` (`Down Arrow`) |
| **D-Pad Left** | Left directional pad (Left) | `PE4` (`0x84`) | `0x0080` | `KEY_LEFT` (`105`) | **`SDLK_LEFT`** (`276`) | `XK_Left` (`Left Arrow`) |
| **D-Pad Right** | Left directional pad (Right) | `PE5` (`0x85`) | `0x0040` | `KEY_RIGHT` (`106`) | **`SDLK_RIGHT`** (`275`) | `XK_Right` (`Right Arrow`) |
| **`A`** | Right / East face button | `PD9` (`0x69`) | `0x0010` | `KEY_LEFTALT` (`56`) | **`SDLK_LALT`** (`308`) | `XK_Alt_L` (Left `Alt`) |
| **`B`** | Bottom / South face button | `PE8` (`0x88`) | `0x0100` | `KEY_LEFTCTRL` (`29`) | **`SDLK_LCTRL`** (`306`) | `XK_Control_L` (Left `Ctrl`) |
| **`X`** | Top / North face button | `PD0` (`0x60`) | `0x0002` | `KEY_LEFTSHIFT` (`42`) | **`SDLK_LSHIFT`** (`304`) | `XK_Shift_L` (Left `Shift`) |
| **`Y`** | Left / West face button | `PE9` (`0x89`) | `0x0020` | `KEY_SPACE` (`57`) | **`SDLK_SPACE`** (`32`) | `XK_space` (`Space`) |
| **`L1`** | Outer Left shoulder button | `PC2` (`0x42`) | `0x1000` | `KEY_TAB` (`15`) | **`SDLK_TAB`** (`9`) | `XK_Tab` (`Tab`) |
| **`R1`** | Outer Right shoulder button | `PE1` (`0x81`) | `0x2000` | `KEY_BACKSPACE` (`14`) | **`SDLK_BACKSPACE`** (`8`) | `XK_BackSpace` (`Backspace`) |
| **`L2`** | Inner Left shoulder trigger | `PE6` (`0x86`) | `0x4000` | `KEY_PAGEUP` (`104`) | **`SDLK_PAGEUP`** (`280`) | `XK_Page_Up` (`Page Up`) |
| **`R2`** | Inner Right shoulder trigger | `PD1` (`0x61`) | `0x8000` | `KEY_PAGEDOWN` (`109`) | **`SDLK_PAGEDOWN`** (`281`) | `XK_Page_Down` (`Page Down`) |
| **`SELECT`** | Lower-left pill button | `PE0` (`0x80`) | `0x0400` | `KEY_ESC` (`1`) | **`SDLK_ESCAPE`** (`27`) | `XK_Escape` (`Esc`) |
| **`START`** | Lower-right pill button | `PC1` (`0x41`) | `0x0200` | `KEY_ENTER` (`28`) | **`SDLK_RETURN`** (`13`) | `XK_Return` (`Enter`) |

> **Note on MiyooCFW System Chords**: While holding `MENU` (`SDLK_RCTRL`), pressing D-Pad directions triggers MiyooCFW's background daemon shortcuts (`MENU + Up/Down` for backlight brightness, `MENU + Left/Right` for master volume). Triggering app exit on a standalone `MENU` press (or release without chord) works cleanly with MiyooCFW.

---

## 3. Standard C Header Definitions (`V90_BTN_*`)

Use the following macro definitions in C source files for consistent cross-target compilation (PowKiddy V90 SDL 1.2 + Linux X11 Desktop shim):

```c
/* =========================================================================
 * PowKiddy V90 Standard Button Definitions (MiyooCFW 1.3.3)
 * ========================================================================= */
#define V90_BTN_UP      SDLK_UP         /* D-Pad Up: Primary navigation / switcher */
#define V90_BTN_DOWN    SDLK_DOWN       /* D-Pad Down: Primary navigation / switcher */
#define V90_BTN_LEFT    SDLK_LEFT       /* D-Pad Left: Primary navigation / switcher */
#define V90_BTN_RIGHT   SDLK_RIGHT      /* D-Pad Right: Primary navigation / switcher */
#define V90_BTN_L1      SDLK_TAB        /* L1 Shoulder: Primary switcher (prev/cycle) */
#define V90_BTN_R1      SDLK_BACKSPACE  /* R1 Shoulder: Primary switcher (next/cycle) */
#define V90_BTN_L2      SDLK_PAGEUP     /* L2 Trigger: Secondary switcher / page up */
#define V90_BTN_R2      SDLK_PAGEDOWN   /* R2 Trigger: Secondary switcher / page down */

#define V90_BTN_A       SDLK_LALT       /* East (A): Start / Stop playback (or confirm) */
#define V90_BTN_B       SDLK_LCTRL      /* South (B): Secondary action / back / replay */
#define V90_BTN_X       SDLK_LSHIFT     /* North (X): Tertiary action */
#define V90_BTN_Y       SDLK_SPACE      /* West (Y): Toggle Help overlay (all apps) */

#define V90_BTN_SELECT  SDLK_ESCAPE     /* SELECT: Modal shift / mode toggle */
#define V90_BTN_START   SDLK_RETURN     /* START: Secondary control / confirm */
#define V90_BTN_MENU    SDLK_RCTRL      /* MENU / RESET (center circle): Quit app */
```
