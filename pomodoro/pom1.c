/*
 * pom1 - Pomodoro Work Timer for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host
 *
 * Screen resolution: 320x240, 16-bit RGB565 (scaled 2x on Linux X11 window)
 * Controls on PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host:
 *   A Button (Left Alt or 'A')      : Toggle Start / Stop (Pause) timer
 *   X Button (Left Shift or 'X')    : Switch mode (Work <-> Short Break <-> Long Break)
 *   B Button (Left Ctrl or 'B')     : Stop / Pause timer
 *   Y Button (Space, 'Y', or 'R')   : Reset current session timer
 *   UP / DOWN                       : Adjust current mode duration (+/- 1 minute when stopped)
 *   SELECT (Escape or 'Q')          : Save & Exit
 *   START  (Enter)                  : Hold SELECT+START to exit
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define SCREEN_BPP 16

/* =========================================================================
 * Platform Backend: Native X11 (Linux Host) vs SDL 1.2 (PowKiddy V90)
 * ========================================================================= */
#if defined(HOST_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/time.h>

#ifndef HOST_SCALE
#define HOST_SCALE 2
#endif

typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef uint32_t Uint32;

typedef int SDLKey;
#define SDLK_UNKNOWN    0
#define SDLK_LALT       XK_Alt_L
#define SDLK_LCTRL      XK_Control_L
#define SDLK_LSHIFT     XK_Shift_L
#define SDLK_SPACE      XK_space
#define SDLK_TAB        XK_Tab
#define SDLK_BACKSPACE  XK_BackSpace
#define SDLK_PAGEUP     XK_Page_Up
#define SDLK_PAGEDOWN   XK_Page_Down
#define SDLK_ESCAPE     XK_Escape
#define SDLK_RETURN     XK_Return
#define SDLK_RCTRL      XK_Control_R
#define SDLK_UP         XK_Up
#define SDLK_DOWN       XK_Down
#define SDLK_LEFT       XK_Left
#define SDLK_RIGHT      XK_Right
#define SDLK_a          XK_a
#define SDLK_b          XK_b
#define SDLK_x          XK_x
#define SDLK_y          XK_y
#define SDLK_r          XK_r
#define SDLK_q          XK_q

#define SDL_QUIT            1
#define SDL_KEYDOWN         2
#define SDL_KEYUP           3
#define SDL_JOYBUTTONDOWN   4

typedef struct {
    int dummy;
} SDL_PixelFormat;

typedef struct {
    Sint16 x, y;
    Uint16 w, h;
} SDL_Rect;

typedef struct {
    int w, h;
    SDL_PixelFormat *format;
    Uint32 *pixels;
} SDL_Surface;

typedef struct {
    int type;
    struct {
        struct {
            SDLKey sym;
        } keysym;
    } key;
    struct {
        Uint8 button;
    } jbutton;
} SDL_Event;

#define SDL_INIT_VIDEO    0x01
#define SDL_INIT_TIMER    0x02
#define SDL_INIT_JOYSTICK 0x04
#define SDL_INIT_AUDIO    0x08
#define SDL_DISABLE       0
#define SDL_SWSURFACE     0
#define AUDIO_S16SYS      0

typedef struct {
    int freq;
    int format;
    int channels;
    int samples;
    void (*callback)(void *userdata, Uint8 *stream, int len);
} SDL_AudioSpec;

static Display *g_x11_dpy = NULL;
static Window g_x11_win = 0;
static GC g_x11_gc = 0;
static XImage *g_x11_img = NULL;
static Atom g_wm_delete_window = 0;
static char *g_scaled_buf = NULL;
static SDL_PixelFormat g_dummy_fmt = {0};
static SDL_Surface g_x11_surface = {0};

static int SDL_Init(Uint32 flags) {
    (void)flags;
    g_x11_dpy = XOpenDisplay(NULL);
    if (!g_x11_dpy) return -1;
    return 0;
}

static const char *SDL_GetError(void) {
    return "Cannot open X11 DISPLAY (ensure DISPLAY is set)";
}

static int SDL_InitSubSystem(Uint32 flags) {
    (void)flags;
    return -1; /* Audio optional on X11 host */
}

static int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    (void)desired;
    (void)obtained;
    return -1;
}

static void SDL_PauseAudio(int pause_on) { (void)pause_on; }
static void SDL_CloseAudio(void) {}
static void SDL_ShowCursor(int toggle) { (void)toggle; }
static int SDL_NumJoysticks(void) { return 0; }
static void SDL_JoystickOpen(int idx) { (void)idx; }

static SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags) {
    (void)bpp;
    (void)flags;
    int scr = DefaultScreen(g_x11_dpy);
    int win_w = width * HOST_SCALE;
    int win_h = height * HOST_SCALE;

    g_x11_win = XCreateSimpleWindow(
        g_x11_dpy, RootWindow(g_x11_dpy, scr),
        100, 100, win_w, win_h, 1,
        BlackPixel(g_x11_dpy, scr), BlackPixel(g_x11_dpy, scr)
    );

    /* Lock window size to scaled V90 resolution */
    XSizeHints hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.max_width = win_w;
    hints.min_height = hints.max_height = win_h;
    XSetWMNormalHints(g_x11_dpy, g_x11_win, &hints);

    XSelectInput(g_x11_dpy, g_x11_win, KeyPressMask | KeyReleaseMask | ExposureMask | StructureNotifyMask);
    g_wm_delete_window = XInternAtom(g_x11_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_x11_dpy, g_x11_win, &g_wm_delete_window, 1);
    XMapWindow(g_x11_dpy, g_x11_win);
    g_x11_gc = DefaultGC(g_x11_dpy, scr);

    g_x11_surface.w = width;
    g_x11_surface.h = height;
    g_x11_surface.format = &g_dummy_fmt;
    g_x11_surface.pixels = (Uint32 *)calloc(width * height, sizeof(Uint32));

    g_scaled_buf = (char *)calloc(win_w * win_h, 4);
    g_x11_img = XCreateImage(
        g_x11_dpy, DefaultVisual(g_x11_dpy, scr),
        DefaultDepth(g_x11_dpy, scr), ZPixmap, 0,
        g_scaled_buf, win_w, win_h, 32, 0
    );
    return &g_x11_surface;
}

static void SDL_WM_SetCaption(const char *title, const char *icon) {
    (void)icon;
    if (g_x11_dpy && g_x11_win) {
        XStoreName(g_x11_dpy, g_x11_win, title);
    }
}

static Uint32 SDL_MapRGB(SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b) {
    (void)fmt;
    return ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
}

static void SDL_FillRect(SDL_Surface *dst, SDL_Rect *rect, Uint32 color) {
    int x0 = rect ? rect->x : 0;
    int y0 = rect ? rect->y : 0;
    int w  = rect ? rect->w : dst->w;
    int h  = rect ? rect->h : dst->h;

    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > dst->w) w = dst->w - x0;
    if (y0 + h > dst->h) h = dst->h - y0;
    if (w <= 0 || h <= 0) return;

    for (int y = y0; y < y0 + h; y++) {
        Uint32 *row = dst->pixels + y * dst->w + x0;
        for (int x = 0; x < w; x++) {
            row[x] = color;
        }
    }
}

static void SDL_Flip(SDL_Surface *screen) {
    int win_w = screen->w * HOST_SCALE;
    Uint32 *dst = (Uint32 *)g_scaled_buf;
    for (int y = 0; y < screen->h; y++) {
        for (int x = 0; x < screen->w; x++) {
            Uint32 c = screen->pixels[y * screen->w + x];
            for (int sy = 0; sy < HOST_SCALE; sy++) {
                Uint32 *row = dst + (y * HOST_SCALE + sy) * win_w + (x * HOST_SCALE);
                for (int sx = 0; sx < HOST_SCALE; sx++) {
                    row[sx] = c;
                }
            }
        }
    }
    XPutImage(g_x11_dpy, g_x11_win, g_x11_gc, g_x11_img, 0, 0, 0, 0,
              screen->w * HOST_SCALE, screen->h * HOST_SCALE);
    XFlush(g_x11_dpy);
}

static int SDL_PollEvent(SDL_Event *ev) {
    while (XPending(g_x11_dpy) > 0) {
        XEvent xev;
        XNextEvent(g_x11_dpy, &xev);
        if (xev.type == ClientMessage) {
            if ((Atom)xev.xclient.data.l[0] == g_wm_delete_window) {
                ev->type = SDL_QUIT;
                return 1;
            }
        } else if (xev.type == KeyPress || xev.type == KeyRelease) {
            /* Filter out X11 key auto-repeat so holding a button behaves like hardware */
            if (xev.type == KeyRelease && XEventsQueued(g_x11_dpy, QueuedAfterReading)) {
                XEvent nev;
                XPeekEvent(g_x11_dpy, &nev);
                if (nev.type == KeyPress && nev.xkey.time == xev.xkey.time &&
                    nev.xkey.keycode == xev.xkey.keycode) {
                    XNextEvent(g_x11_dpy, &nev);
                    continue;
                }
            }
            KeySym ks = XLookupKeysym(&xev.xkey, 0);
            if (ks >= XK_A && ks <= XK_Z) {
                ks = ks - XK_A + XK_a;
            }
            ev->type = (xev.type == KeyPress) ? SDL_KEYDOWN : SDL_KEYUP;
            ev->key.keysym.sym = (SDLKey)ks;
            return 1;
        }
    }
    return 0;
}

static Uint32 SDL_GetTicks(void) {
    static struct timespec start_ts = {0, 0};
    static bool initialized = false;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!initialized) {
        start_ts = now;
        initialized = true;
    }
    int64_t sec_diff  = (int64_t)now.tv_sec - (int64_t)start_ts.tv_sec;
    int64_t nsec_diff = (int64_t)now.tv_nsec - (int64_t)start_ts.tv_nsec;
    int64_t total_ms  = sec_diff * 1000 + nsec_diff / 1000000;
    if (total_ms < 0) total_ms = 0;
    return (Uint32)total_ms;
}

static void SDL_Delay(Uint32 ms) {
    usleep(ms * 1000);
}

static void SDL_Quit(void) {
    if (g_x11_img) {
        XDestroyImage(g_x11_img); /* Also frees g_scaled_buf */
        g_x11_img = NULL;
    }
    free(g_x11_surface.pixels);
    g_x11_surface.pixels = NULL;
    if (g_x11_dpy) {
        if (g_x11_win) XDestroyWindow(g_x11_dpy, g_x11_win);
        XCloseDisplay(g_x11_dpy);
        g_x11_dpy = NULL;
    }
}

#else
#include <SDL/SDL.h>
#endif /* HOST_X11 */

/* PowKiddy V90 / MiyooCFW 1.3.3 SDL Key Mappings */
#define V90_BTN_A       SDLK_LALT
#define V90_BTN_B       SDLK_LCTRL
#define V90_BTN_X       SDLK_LSHIFT
#define V90_BTN_Y       SDLK_SPACE
#define V90_BTN_L1      SDLK_TAB
#define V90_BTN_R1      SDLK_BACKSPACE
#define V90_BTN_L2      SDLK_PAGEUP
#define V90_BTN_R2      SDLK_PAGEDOWN
#define V90_BTN_SELECT  SDLK_ESCAPE
#define V90_BTN_START   SDLK_RETURN
#define V90_BTN_RESET   SDLK_RCTRL
#define V90_BTN_UP      SDLK_UP
#define V90_BTN_DOWN    SDLK_DOWN
#define V90_BTN_LEFT    SDLK_LEFT
#define V90_BTN_RIGHT   SDLK_RIGHT

/* Default Pomodoro durations (in minutes) */
#define DEFAULT_WORK_MINS       25
#define DEFAULT_SHORT_BRK_MINS   5
#define DEFAULT_LONG_BRK_MINS   15

typedef enum {
    MODE_WORK = 0,
    MODE_SHORT_BREAK = 1,
    MODE_LONG_BREAK = 2
} TimerMode;

typedef enum {
    STATUS_READY = 0,
    STATUS_RUNNING = 1,
    STATUS_PAUSED = 2,
    STATUS_DONE = 3
} TimerStatus;

typedef struct {
    uint64_t total_work_seconds;
    uint64_t today_work_seconds;
    uint32_t total_pomodoros;
    uint32_t today_pomodoros;
    char last_date[32]; /* YYYY-MM-DD */
    int work_mins;
    int short_break_mins;
    int long_break_mins;
} WorkStats;

/* Built-in 5x7 ASCII bitmap font (printable chars 32..126) */
static const uint8_t font5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* 32 ' ' */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* 33 '!' */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* 34 '"' */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* 35 '#' */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* 36 '$' */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* 37 '%' */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* 38 '&' */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* 39 ''' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* 40 '(' */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* 41 ')' */
    {0x14, 0x08, 0x3E, 0x08, 0x14}, /* 42 '*' */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* 43 '+' */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* 44 ',' */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* 45 '-' */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* 46 '.' */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* 47 '/' */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 48 '0' */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 49 '1' */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 50 '2' */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 51 '3' */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 52 '4' */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 53 '5' */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 54 '6' */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 55 '7' */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 56 '8' */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 57 '9' */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* 58 ':' */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* 59 ';' */
    {0x08, 0x14, 0x22, 0x41, 0x00}, /* 60 '<' */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* 61 '=' */
    {0x00, 0x41, 0x22, 0x14, 0x08}, /* 62 '>' */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* 63 '?' */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* 64 '@' */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* 65 'A' */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* 66 'B' */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* 67 'C' */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* 68 'D' */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* 69 'E' */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* 70 'F' */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* 71 'G' */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* 72 'H' */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* 73 'I' */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* 74 'J' */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* 75 'K' */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* 76 'L' */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* 77 'M' */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* 78 'N' */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* 79 'O' */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* 80 'P' */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* 81 'Q' */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* 82 'R' */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* 83 'S' */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* 84 'T' */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* 85 'U' */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* 86 'V' */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* 87 'W' */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* 88 'X' */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* 89 'Y' */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* 90 'Z' */
    {0x00, 0x7F, 0x41, 0x41, 0x00}, /* 91 '[' */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* 92 '\' */
    {0x00, 0x41, 0x41, 0x7F, 0x00}, /* 93 ']' */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* 94 '^' */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* 95 '_' */
    {0x00, 0x01, 0x02, 0x04, 0x00}, /* 96 '`' */
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* 97 'a' */
    {0x7F, 0x48, 0x44, 0x44, 0x38}, /* 98 'b' */
    {0x38, 0x44, 0x44, 0x44, 0x20}, /* 99 'c' */
    {0x38, 0x44, 0x44, 0x48, 0x7F}, /* 100 'd' */
    {0x38, 0x54, 0x54, 0x54, 0x18}, /* 101 'e' */
    {0x08, 0x7E, 0x09, 0x01, 0x02}, /* 102 'f' */
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, /* 103 'g' */
    {0x7F, 0x08, 0x04, 0x04, 0x78}, /* 104 'h' */
    {0x00, 0x44, 0x7D, 0x40, 0x00}, /* 105 'i' */
    {0x20, 0x40, 0x44, 0x3D, 0x00}, /* 106 'j' */
    {0x7F, 0x10, 0x28, 0x44, 0x00}, /* 107 'k' */
    {0x00, 0x41, 0x7F, 0x40, 0x00}, /* 108 'l' */
    {0x7C, 0x04, 0x18, 0x04, 0x78}, /* 109 'm' */
    {0x7C, 0x08, 0x04, 0x04, 0x78}, /* 110 'n' */
    {0x38, 0x44, 0x44, 0x44, 0x38}, /* 111 'o' */
    {0x7C, 0x14, 0x14, 0x14, 0x08}, /* 112 'p' */
    {0x08, 0x14, 0x14, 0x18, 0x7C}, /* 113 'q' */
    {0x7C, 0x08, 0x04, 0x04, 0x08}, /* 114 'r' */
    {0x48, 0x54, 0x54, 0x54, 0x20}, /* 115 's' */
    {0x04, 0x3F, 0x44, 0x40, 0x20}, /* 116 't' */
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, /* 117 'u' */
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* 118 'v' */
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* 119 'w' */
    {0x44, 0x28, 0x10, 0x28, 0x44}, /* 120 'x' */
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* 121 'y' */
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* 122 'z' */
    {0x00, 0x08, 0x36, 0x41, 0x00}, /* 123 '{' */
    {0x00, 0x00, 0x7F, 0x00, 0x00}, /* 124 '|' */
    {0x00, 0x41, 0x36, 0x08, 0x00}, /* 125 '}' */
    {0x08, 0x04, 0x08, 0x10, 0x08}  /* 126 '~' */
};

/* Global State */
static WorkStats g_stats;
static char g_save_path[512];
static char g_log_path[512];

/* Audio alert state */
static volatile int g_beep_samples_remaining = 0;
static uint32_t g_beep_phase = 0;

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *samples = (int16_t *)stream;
    int count = len / 2;
    for (int i = 0; i < count; i++) {
        if (g_beep_samples_remaining > 0) {
            /* Create a pleasant double-beep pattern */
            int16_t val = 0;
            int rem = g_beep_samples_remaining;
            bool tone_on = (rem > 14000) || (rem < 10000 && rem > 2000);
            if (tone_on) {
                /* 880 Hz square wave with soft amplitude */
                val = ((g_beep_phase / 25) % 2) ? 3000 : -3000;
                g_beep_phase++;
            }
            samples[i] = val;
            g_beep_samples_remaining--;
        } else {
            samples[i] = 0;
        }
    }
}

static void trigger_beep(void) {
    g_beep_samples_remaining = 22050; /* ~1 second at 22050 Hz */
}

static void get_current_date(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t && t->tm_year + 1900 >= 2020) {
        int yr = (t->tm_year + 1900) % 10000;
        int mo = (t->tm_mon + 1) % 100;
        int dy = t->tm_mday % 100;
        snprintf(buf, size, "%04d-%02d-%02d", yr, mo, dy);
    } else {
        snprintf(buf, size, "SESSION");
    }
}

static void resolve_save_paths(void) {
    /* On MiyooCFW 1.3.3, /mnt is the persistent FAT32 SD card partition */
    struct stat st;
    if (stat("/mnt", &st) == 0 && S_ISDIR(st.st_mode) && access("/mnt", W_OK) == 0) {
        snprintf(g_save_path, sizeof(g_save_path), "/mnt/.pom1_stats.cfg");
        snprintf(g_log_path, sizeof(g_log_path), "/mnt/pom1_work_log.txt");
    } else {
        const char *home = getenv("HOME");
        if (home && access(home, W_OK) == 0) {
            snprintf(g_save_path, sizeof(g_save_path), "%s/.pom1_stats.cfg", home);
            snprintf(g_log_path, sizeof(g_log_path), "%s/pom1_work_log.txt", home);
        } else {
            snprintf(g_save_path, sizeof(g_save_path), "./.pom1_stats.cfg");
            snprintf(g_log_path, sizeof(g_log_path), "./pom1_work_log.txt");
        }
    }
}

static void init_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.work_mins = DEFAULT_WORK_MINS;
    g_stats.short_break_mins = DEFAULT_SHORT_BRK_MINS;
    g_stats.long_break_mins = DEFAULT_LONG_BRK_MINS;
    get_current_date(g_stats.last_date, sizeof(g_stats.last_date));
}

static void load_stats(void) {
    init_stats();
    FILE *f = fopen(g_save_path, "r");
    if (!f) return;

    char line[256];
    char loaded_date[32] = {0};
    while (fgets(line, sizeof(line), f)) {
        unsigned long long val = 0;
        int ival = 0;
        if (sscanf(line, "total_work_seconds=%llu", &val) == 1) {
            g_stats.total_work_seconds = (uint64_t)val;
        } else if (sscanf(line, "today_work_seconds=%llu", &val) == 1) {
            g_stats.today_work_seconds = (uint64_t)val;
        } else if (sscanf(line, "total_pomodoros=%llu", &val) == 1) {
            g_stats.total_pomodoros = (uint32_t)val;
        } else if (sscanf(line, "today_pomodoros=%llu", &val) == 1) {
            g_stats.today_pomodoros = (uint32_t)val;
        } else if (sscanf(line, "last_date=%31s", loaded_date) == 1) {
            /* handled below */
        } else if (sscanf(line, "work_mins=%d", &ival) == 1 && ival >= 1 && ival <= 180) {
            g_stats.work_mins = ival;
        } else if (sscanf(line, "short_break_mins=%d", &ival) == 1 && ival >= 1 && ival <= 60) {
            g_stats.short_break_mins = ival;
        } else if (sscanf(line, "long_break_mins=%d", &ival) == 1 && ival >= 1 && ival <= 90) {
            g_stats.long_break_mins = ival;
        }
    }
    fclose(f);

    char cur_date[32];
    get_current_date(cur_date, sizeof(cur_date));
    if (strcmp(cur_date, "SESSION") != 0 && loaded_date[0] != '\0' && strcmp(cur_date, loaded_date) != 0) {
        /* New calendar day: reset today's counters while keeping lifetime totals */
        g_stats.today_work_seconds = 0;
        g_stats.today_pomodoros = 0;
    }
    snprintf(g_stats.last_date, sizeof(g_stats.last_date), "%s", cur_date);
}

static void save_stats(void) {
    FILE *f = fopen(g_save_path, "w");
    if (!f) return;

    fprintf(f, "total_work_seconds=%llu\n", (unsigned long long)g_stats.total_work_seconds);
    fprintf(f, "today_work_seconds=%llu\n", (unsigned long long)g_stats.today_work_seconds);
    fprintf(f, "total_pomodoros=%u\n", g_stats.total_pomodoros);
    fprintf(f, "today_pomodoros=%u\n", g_stats.today_pomodoros);
    fprintf(f, "last_date=%s\n", g_stats.last_date);
    fprintf(f, "work_mins=%d\n", g_stats.work_mins);
    fprintf(f, "short_break_mins=%d\n", g_stats.short_break_mins);
    fprintf(f, "long_break_mins=%d\n", g_stats.long_break_mins);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    /* Also write a human-readable summary log */
    FILE *logf = fopen(g_log_path, "w");
    if (logf) {
        double total_hrs = (double)g_stats.total_work_seconds / 3600.0;
        double today_hrs = (double)g_stats.today_work_seconds / 3600.0;
        uint64_t th = g_stats.total_work_seconds / 3600;
        uint64_t tm = (g_stats.total_work_seconds % 3600) / 60;
        uint64_t ts = g_stats.total_work_seconds % 60;
        fprintf(logf, "=== PowKiddy V90 Pomodoro Work Log ===\n");
        fprintf(logf, "Total Work Time : %.2f hours (%lluh %02llum %02llus)\n",
                total_hrs, (unsigned long long)th, (unsigned long long)tm, (unsigned long long)ts);
        fprintf(logf, "Today Work Time : %.2f hours\n", today_hrs);
        fprintf(logf, "Total Pomodoros : %u\n", g_stats.total_pomodoros);
        fprintf(logf, "Today Pomodoros : %u\n", g_stats.today_pomodoros);
        fflush(logf);
        fsync(fileno(logf));
        fclose(logf);
    }
}

/* Drawing Helpers */
static void draw_rect(SDL_Surface *surf, int x, int y, int w, int h, Uint32 color) {
    SDL_Rect r = { (Sint16)x, (Sint16)y, (Uint16)w, (Uint16)h };
    SDL_FillRect(surf, &r, color);
}

static void draw_border(SDL_Surface *surf, int x, int y, int w, int h, int thickness, Uint32 color) {
    draw_rect(surf, x, y, w, thickness, color);
    draw_rect(surf, x, y + h - thickness, w, thickness, color);
    draw_rect(surf, x, y, thickness, h, color);
    draw_rect(surf, x + w - thickness, y, thickness, h, color);
}

static void draw_char(SDL_Surface *surf, int x, int y, char c, int scale, Uint32 color) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font5x7[c - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                SDL_Rect r = {
                    (Sint16)(x + col * scale),
                    (Sint16)(y + row * scale),
                    (Uint16)scale,
                    (Uint16)scale
                };
                SDL_FillRect(surf, &r, color);
            }
        }
    }
}

static void draw_text(SDL_Surface *surf, int x, int y, const char *str, int scale, Uint32 color) {
    int cur_x = x;
    while (*str) {
        draw_char(surf, cur_x, y, *str, scale, color);
        cur_x += 6 * scale;
        str++;
    }
}

static void draw_text_centered(SDL_Surface *surf, int cx, int y, const char *str, int scale, Uint32 color) {
    int len = (int)strlen(str);
    if (len == 0) return;
    int width = len * 6 * scale - scale;
    draw_text(surf, cx - width / 2, y, str, scale, color);
}

static int get_mode_duration(TimerMode mode) {
    switch (mode) {
        case MODE_WORK:        return g_stats.work_mins * 60;
        case MODE_SHORT_BREAK: return g_stats.short_break_mins * 60;
        case MODE_LONG_BREAK:  return g_stats.long_break_mins * 60;
        default:               return 25 * 60;
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    putenv((char *)"SDL_NOMOUSE=1");
    resolve_save_paths();
    load_stats();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "Failed to initialize video: %s\n", SDL_GetError());
        return 1;
    }

    /* Optional audio initialization (don't fail if audio device is busy/unavailable) */
    bool audio_ok = false;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec desired, obtained;
        memset(&desired, 0, sizeof(desired));
        desired.freq = 22050;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = 512;
        desired.callback = audio_callback;
        if (SDL_OpenAudio(&desired, &obtained) == 0) {
            SDL_PauseAudio(0);
            audio_ok = true;
        }
    }

    SDL_ShowCursor(SDL_DISABLE);
    if (SDL_NumJoysticks() > 0) {
        SDL_JoystickOpen(0);
    }

    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, SCREEN_BPP, SDL_SWSURFACE);
    if (!screen) {
        screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 0, SDL_SWSURFACE);
        if (!screen) {
            fprintf(stderr, "Failed to set video mode: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
    }
    SDL_WM_SetCaption("V90 Pomodoro Timer", NULL);

    /* Colors */
    Uint32 col_bg          = SDL_MapRGB(screen->format, 18, 22, 31);
    Uint32 col_header      = SDL_MapRGB(screen->format, 28, 35, 49);
    Uint32 col_panel       = SDL_MapRGB(screen->format, 25, 31, 44);
    Uint32 col_border      = SDL_MapRGB(screen->format, 55, 68, 92);
    Uint32 col_white       = SDL_MapRGB(screen->format, 240, 244, 248);
    Uint32 col_muted       = SDL_MapRGB(screen->format, 145, 160, 180);
    Uint32 col_work        = SDL_MapRGB(screen->format, 235, 87, 87);   /* Pomodoro Red */
    Uint32 col_running     = SDL_MapRGB(screen->format, 46, 204, 113);  /* Emerald Green */
    Uint32 col_paused      = SDL_MapRGB(screen->format, 242, 153, 74);  /* Amber Orange */
    Uint32 col_break       = SDL_MapRGB(screen->format, 52, 152, 219);  /* Calm Blue */
    Uint32 col_gold        = SDL_MapRGB(screen->format, 241, 196, 15);  /* Gold */
    Uint32 col_bar_bg      = SDL_MapRGB(screen->format, 12, 15, 22);

    TimerMode mode = MODE_WORK;
    TimerStatus status = STATUS_READY;
    int total_phase_sec = get_mode_duration(mode);
    int remaining_sec = total_phase_sec;
    uint64_t current_session_work_sec = 0;

    Uint32 last_tick_ms = SDL_GetTicks();
    Uint32 accum_ms = 0;
    int unsaved_work_sec = 0;

    bool select_held = false;
    bool start_held = false;
    bool running = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                SDLKey k = ev.key.keysym.sym;

                if (k == V90_BTN_SELECT) select_held = true;
                if (k == V90_BTN_START)  start_held = true;

                /* A Button (SDLK_LALT on V90, or 'A' on keyboard): Toggle Start / Stop */
                if (k == V90_BTN_A || k == SDLK_a) {
                    if (status == STATUS_RUNNING) {
                        status = STATUS_PAUSED;
                        save_stats();
                        unsaved_work_sec = 0;
                    } else {
                        if (status == STATUS_DONE) {
                            /* Advance to next phase and start */
                            if (mode == MODE_WORK) {
                                mode = (g_stats.today_pomodoros % 4 == 0) ? MODE_LONG_BREAK : MODE_SHORT_BREAK;
                            } else {
                                mode = MODE_WORK;
                            }
                            total_phase_sec = get_mode_duration(mode);
                            remaining_sec = total_phase_sec;
                        }
                        status = STATUS_RUNNING;
                        last_tick_ms = SDL_GetTicks();
                    }
                }
                /* B Button (SDLK_LCTRL on V90, or 'B' on keyboard): Stop / Pause timer */
                else if (k == V90_BTN_B || k == SDLK_b) {
                    if (status == STATUS_RUNNING) {
                        status = STATUS_PAUSED;
                        save_stats();
                        unsaved_work_sec = 0;
                    } else if (status == STATUS_DONE) {
                        status = STATUS_READY;
                        total_phase_sec = get_mode_duration(mode);
                        remaining_sec = total_phase_sec;
                    }
                }
                /* X Button (SDLK_LSHIFT on V90, or 'X' on keyboard): Switch mode */
                else if (k == V90_BTN_X || k == SDLK_x) {
                    save_stats();
                    unsaved_work_sec = 0;
                    mode = (TimerMode)((mode + 1) % 3);
                    status = STATUS_READY;
                    total_phase_sec = get_mode_duration(mode);
                    remaining_sec = total_phase_sec;
                    accum_ms = 0;
                }
                /* Y Button (SDLK_SPACE on V90, or 'Y'/'R' on keyboard): Reset current timer */
                else if (k == V90_BTN_Y || k == SDLK_y || k == SDLK_r) {
                    status = STATUS_READY;
                    total_phase_sec = get_mode_duration(mode);
                    remaining_sec = total_phase_sec;
                    accum_ms = 0;
                    save_stats();
                    unsaved_work_sec = 0;
                }
                /* UP / DOWN: Adjust duration when not running */
                else if ((k == V90_BTN_UP || k == V90_BTN_DOWN) && status != STATUS_RUNNING) {
                    int delta = (k == V90_BTN_UP) ? 1 : -1;
                    if (mode == MODE_WORK) {
                        g_stats.work_mins += delta;
                        if (g_stats.work_mins < 1) g_stats.work_mins = 1;
                        if (g_stats.work_mins > 120) g_stats.work_mins = 120;
                    } else if (mode == MODE_SHORT_BREAK) {
                        g_stats.short_break_mins += delta;
                        if (g_stats.short_break_mins < 1) g_stats.short_break_mins = 1;
                        if (g_stats.short_break_mins > 60) g_stats.short_break_mins = 60;
                    } else {
                        g_stats.long_break_mins += delta;
                        if (g_stats.long_break_mins < 1) g_stats.long_break_mins = 1;
                        if (g_stats.long_break_mins > 90) g_stats.long_break_mins = 90;
                    }
                    status = STATUS_READY;
                    total_phase_sec = get_mode_duration(mode);
                    remaining_sec = total_phase_sec;
                    accum_ms = 0;
                    save_stats();
                }
                /* SELECT (or Q): Save and exit */
                else if (k == V90_BTN_SELECT || k == SDLK_q) {
                    running = false;
                }
            } else if (ev.type == SDL_KEYUP) {
                SDLKey k = ev.key.keysym.sym;
                if (k == V90_BTN_SELECT) select_held = false;
                if (k == V90_BTN_START)  start_held = false;
            } else if (ev.type == SDL_JOYBUTTONDOWN) {
                /* Fallback if SDL joystick events are emitted */
                if (ev.jbutton.button == 1 || ev.jbutton.button == 0) {
                    if (ev.jbutton.button == 1) { /* A: Toggle Start / Stop */
                        if (status == STATUS_RUNNING) {
                            status = STATUS_PAUSED;
                            save_stats();
                            unsaved_work_sec = 0;
                        } else {
                            status = STATUS_RUNNING;
                            last_tick_ms = SDL_GetTicks();
                        }
                    } else { /* B: Stop */
                        if (status == STATUS_RUNNING) {
                            status = STATUS_PAUSED;
                            save_stats();
                        }
                    }
                }
            }
        }

        if (select_held && start_held) {
            running = false;
        }

        /* Update Timer */
        Uint32 now_ms = SDL_GetTicks();
        if (status == STATUS_RUNNING) {
            Uint32 dt = (now_ms >= last_tick_ms) ? (now_ms - last_tick_ms) : 0;
            if (dt > 5000) dt = 1000; /* Guard against system suspend/clock jumps */
            last_tick_ms = now_ms;
            accum_ms += dt;

            while (accum_ms >= 1000) {
                accum_ms -= 1000;
                if (remaining_sec > 0) {
                    remaining_sec--;
                }
                if (mode == MODE_WORK) {
                    g_stats.total_work_seconds++;
                    g_stats.today_work_seconds++;
                    current_session_work_sec++;
                    unsaved_work_sec++;
                    /* Auto-save every 30 seconds of work to protect against sudden power off */
                    if (unsaved_work_sec >= 30) {
                        save_stats();
                        unsaved_work_sec = 0;
                    }
                }
                if (remaining_sec <= 0) {
                    remaining_sec = 0;
                    status = STATUS_DONE;
                    if (mode == MODE_WORK) {
                        g_stats.total_pomodoros++;
                        g_stats.today_pomodoros++;
                    }
                    save_stats();
                    unsaved_work_sec = 0;
                    trigger_beep();
                    break;
                }
            }
        } else {
            last_tick_ms = now_ms;
        }

        /* ================= RENDER UI (320x240) ================= */
        draw_rect(screen, 0, 0, SCREEN_W, SCREEN_H, col_bg);

        /* 1. Top Header Bar (y: 0..26) */
        const char *top_str = "READY TO WORK";
        Uint32 top_col = col_work;

        if (status == STATUS_RUNNING) {
            if (mode == MODE_WORK) {
                top_str = "WORKING: DO NOT DISTURB";
                top_col = col_work; /* Red */
            } else if (mode == MODE_SHORT_BREAK) {
                top_str = "SHORT BREAK: RELAX";
                top_col = col_break;
            } else {
                top_str = "LONG BREAK: RELAX";
                top_col = col_break;
            }
        } else if (status == STATUS_PAUSED) {
            top_str = "PAUSED: TIMER STOPPED";
            top_col = col_paused;
        } else if (status == STATUS_DONE) {
            top_str = (mode == MODE_WORK) ? "WORK DONE! TAKE A BREAK" : "BREAK OVER! READY TO WORK";
            top_col = col_gold;
        } else {
            if (mode == MODE_WORK) {
                top_str = "WORKING: DO NOT DISTURB";
                top_col = col_work; /* Red */
            } else if (mode == MODE_SHORT_BREAK) {
                top_str = "SHORT BREAK: READY";
                top_col = col_break;
            } else {
                top_str = "LONG BREAK: READY";
                top_col = col_break;
            }
        }

        draw_rect(screen, 0, 0, SCREEN_W, 26, col_header);
        draw_rect(screen, 0, 24, SCREEN_W, 2, top_col);
        draw_text_centered(screen, SCREEN_W / 2, 6, top_str, 2, top_col);

        /* 2. Status Banner (y: 32..56) */
        Uint32 status_col = col_muted;
        const char *status_str = "READY - PRESS A TO START";
        if (status == STATUS_RUNNING) {
            if (mode == MODE_WORK) {
                status_col = col_running;
                status_str = "STATUS: WORKING (PRESS A TO PAUSE)";
            } else {
                status_col = col_break;
                status_str = "STATUS: ON BREAK (PRESS A TO PAUSE)";
            }
        } else if (status == STATUS_PAUSED) {
            status_col = col_paused;
            status_str = "STATUS: PAUSED (PRESS A TO RESUME)";
        } else if (status == STATUS_DONE) {
            status_col = col_gold;
            status_str = (mode == MODE_WORK) ? "WORK COMPLETE! PRESS A FOR BREAK" :
                                               "BREAK OVER! PRESS A TO WORK";
        } else {
            status_col = (mode == MODE_WORK) ? col_work : col_break;
            status_str = (mode == MODE_WORK) ? "STATUS: READY TO WORK (PRESS A)" :
                                               "STATUS: READY FOR BREAK (PRESS A)";
        }

        draw_rect(screen, 10, 32, 300, 22, col_panel);
        draw_border(screen, 10, 32, 300, 22, 1, status_col);
        /* Status indicator dot */
        if (status != STATUS_RUNNING || ((now_ms / 500) % 2 == 0)) {
            draw_rect(screen, 18, 39, 8, 8, status_col);
        }
        draw_text_centered(screen, 165, 40, status_str, 1, col_white);

        /* 3. Main Countdown Timer Box (y: 60..136) */
        draw_rect(screen, 10, 60, 300, 76, col_panel);
        draw_border(screen, 10, 60, 300, 76, 1, col_border);

        int mins = remaining_sec / 60;
        int secs = remaining_sec % 60;
        char time_buf[16];
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", mins, secs);

        /* Scale 6 => 30x42 px per char; 5 chars => ~174px wide */
        Uint32 timer_col = (status == STATUS_PAUSED) ? col_paused :
                           (status == STATUS_DONE)   ? col_gold   : col_white;
        draw_text_centered(screen, 160, 68, time_buf, 6, timer_col);

        /* Progress Bar inside Timer Box (y: 120..128) */
        int bar_x = 24, bar_y = 120, bar_w = 272, bar_h = 8;
        draw_rect(screen, bar_x, bar_y, bar_w, bar_h, col_bar_bg);
        if (total_phase_sec > 0) {
            int elapsed = total_phase_sec - remaining_sec;
            if (elapsed < 0) elapsed = 0;
            if (elapsed > total_phase_sec) elapsed = total_phase_sec;
            int fill_w = (elapsed * bar_w) / total_phase_sec;
            if (fill_w > 0) {
                draw_rect(screen, bar_x, bar_y, fill_w, bar_h, status_col);
            }
        }
        draw_border(screen, bar_x, bar_y, bar_w, bar_h, 1, col_border);

        /* 4. Work Hours & Stats Panel (y: 142..208) */
        draw_rect(screen, 10, 142, 300, 66, col_panel);
        draw_border(screen, 10, 142, 300, 66, 1, col_border);

        double total_hours = (double)g_stats.total_work_seconds / 3600.0;
        uint64_t tot_h = g_stats.total_work_seconds / 3600;
        uint64_t tot_m = (g_stats.total_work_seconds % 3600) / 60;
        uint64_t tot_s = g_stats.total_work_seconds % 60;

        char line1[64];
        snprintf(line1, sizeof(line1), "WORKED: %.2f HRS", total_hours);
        draw_text(screen, 18, 149, line1, 2, col_gold);

        char line2[64];
        snprintf(line2, sizeof(line2), "TOTAL : %lluh %02llum %02llus  (%u POMS)",
                 (unsigned long long)tot_h, (unsigned long long)tot_m,
                 (unsigned long long)tot_s, g_stats.total_pomodoros);
        draw_text(screen, 18, 171, line2, 1, col_white);

        double today_hours = (double)g_stats.today_work_seconds / 3600.0;
        uint64_t sess_m = current_session_work_sec / 60;
        uint64_t sess_s = current_session_work_sec % 60;
        char line3[64];
        snprintf(line3, sizeof(line3), "TODAY : %.2f hrs (%u poms) | SESSION: %llum %02llus",
                 today_hours, g_stats.today_pomodoros,
                 (unsigned long long)sess_m, (unsigned long long)sess_s);
        draw_text(screen, 18, 184, line3, 1, col_muted);

        char line4[64];
        const char *mode_label = (mode == MODE_WORK) ? "Work" :
                                 (mode == MODE_SHORT_BREAK) ? "Short Break" : "Long Break";
        snprintf(line4, sizeof(line4), "MODE  : %s (%d min)  [UP/DN: Adjust]",
                 mode_label, total_phase_sec / 60);
        draw_text(screen, 18, 196, line4, 1, col_muted);

        /* 5. Bottom Controls Footer (y: 214..240) */
        draw_rect(screen, 0, 214, SCREEN_W, 26, col_header);
        draw_rect(screen, 0, 214, SCREEN_W, 1, col_border);

        /* Highlight A (Start/Stop) and X (Mode) button hints */
        Uint32 a_btn_col = (status == STATUS_RUNNING) ? col_paused : col_running;
        draw_rect(screen, 8, 220, 96, 14, a_btn_col);
        draw_text_centered(screen, 56, 224, "[A] START/STOP", 1, col_bg);

        draw_rect(screen, 110, 220, 62, 14, col_break);
        draw_text_centered(screen, 141, 224, "[X] MODE", 1, col_white);

        draw_text(screen, 180, 224, "[Y]RST  [SEL]EXIT", 1, col_white);

        SDL_Flip(screen);
        SDL_Delay(50); /* ~20 FPS to save battery on V90 */
    }

    save_stats();
    if (audio_ok) {
        SDL_CloseAudio();
    }
    SDL_Quit();
    return 0;
}
