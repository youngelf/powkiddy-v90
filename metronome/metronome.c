/*
 * metronome - Precision Metronome for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host
 *
 * Screen resolution: 320x240, 16-bit RGB565 (scaled 2x on Linux X11 window)
 * Controls on PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host:
 *   A Button (Left Alt or 'A')      : Play / Pause metronome
 *   UP / DOWN                       : Increase / Decrease Tempo (BPM)
 *   LEFT / RIGHT                    : Cycle Time Signature (4/4, 3/4, 2/4, 5/4, 7/4)
 *   L1 / R1 (Tab / Backspace, [/])  : Decrease / Increase Click Drop-Rate by 5% (0%..100%)
 *   Y Button (Space or 'Y'/'H')     : Open / Close Help Screen
 *   MENU (Right Ctrl or 'Q')        : Save settings & Exit
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define SCREEN_BPP 16
#define SAMPLE_RATE 44100
#define AUDIO_CHANNELS 2

/* =========================================================================
 * Platform Backend: Native X11 + Dynamic Audio (Linux Host) vs SDL 1.2 (V90)
 * ========================================================================= */
#if defined(HOST_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <time.h>
#include <pthread.h>
#include <dlfcn.h>

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
#define SDLK_h          XK_h
#define SDLK_q          XK_q
#define SDLK_LEFTBRACKET  XK_bracketleft
#define SDLK_RIGHTBRACKET XK_bracketright

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

/* Host Audio Thread via dynamically loaded libpulse-simple or libasound */
static pthread_t g_audio_thread;
static volatile bool g_audio_running = false;
static volatile bool g_audio_paused = true;
static SDL_AudioSpec g_audio_spec;

typedef struct pa_simple pa_simple;
typedef struct {
    int format;
    uint32_t rate;
    uint8_t channels;
} pa_sample_spec;

#define PA_STREAM_PLAYBACK 1
#define PA_SAMPLE_S16LE    3

typedef pa_simple* (*pa_simple_new_fn)(const char*, const char*, int, const char*,
                                       const char*, const pa_sample_spec*,
                                       const void*, const void*, int*);
typedef int (*pa_simple_write_fn)(pa_simple*, const void*, size_t, int*);
typedef void (*pa_simple_free_fn)(pa_simple*);

static void *host_audio_worker(void *arg) {
    (void)arg;
    void *pa_lib = dlopen("libpulse-simple.so.0", RTLD_LAZY);
    pa_simple *pa = NULL;
    pa_simple_new_fn fn_new = NULL;
    pa_simple_write_fn fn_write = NULL;
    pa_simple_free_fn fn_free = NULL;

    if (pa_lib) {
        fn_new = (pa_simple_new_fn)dlsym(pa_lib, "pa_simple_new");
        fn_write = (pa_simple_write_fn)dlsym(pa_lib, "pa_simple_write");
        fn_free = (pa_simple_free_fn)dlsym(pa_lib, "pa_simple_free");
        if (fn_new && fn_write && fn_free) {
            pa_sample_spec ss = { PA_SAMPLE_S16LE, (uint32_t)g_audio_spec.freq, (uint8_t)g_audio_spec.channels };
            int err = 0;
            pa = fn_new(NULL, "V90 Metronome", PA_STREAM_PLAYBACK, NULL, "Metronome Click", &ss, NULL, NULL, &err);
        }
    }

    int chunk_frames = 512;
    int chunk_bytes = chunk_frames * AUDIO_CHANNELS * 2;
    int16_t buf[512 * AUDIO_CHANNELS];

    while (g_audio_running) {
        if (g_audio_paused || !g_audio_spec.callback) {
            memset(buf, 0, sizeof(buf));
        } else {
            g_audio_spec.callback(NULL, (Uint8 *)buf, chunk_bytes);
        }
        if (pa && fn_write) {
            int err = 0;
            fn_write(pa, buf, chunk_bytes, &err);
        } else {
            /* Fallback sleep if no audio server is reachable (e.g. headless Xvfb) */
            usleep((chunk_frames * 1000000ULL) / g_audio_spec.freq);
        }
    }

    if (pa && fn_free) fn_free(pa);
    if (pa_lib) dlclose(pa_lib);
    return NULL;
}

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
    return 0;
}

static int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    g_audio_spec = *desired;
    if (obtained) *obtained = *desired;
    g_audio_running = true;
    g_audio_paused = true;
    if (pthread_create(&g_audio_thread, NULL, host_audio_worker, NULL) != 0) {
        g_audio_running = false;
        return -1;
    }
    return 0;
}

static void SDL_PauseAudio(int pause_on) {
    g_audio_paused = (pause_on != 0);
}

static void SDL_CloseAudio(void) {
    if (g_audio_running) {
        g_audio_running = false;
        pthread_join(g_audio_thread, NULL);
    }
}

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
        120, 120, win_w, win_h, 1,
        BlackPixel(g_x11_dpy, scr), BlackPixel(g_x11_dpy, scr)
    );

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
        XDestroyImage(g_x11_img);
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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
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
#define V90_BTN_MENU    SDLK_RCTRL
#define V90_BTN_UP      SDLK_UP
#define V90_BTN_DOWN    SDLK_DOWN
#define V90_BTN_LEFT    SDLK_LEFT
#define V90_BTN_RIGHT   SDLK_RIGHT

#define MIN_BPM 30
#define MAX_BPM 260
#define DEFAULT_BPM 120

/* Time Signatures: 4/4, 3/4, 2/4, 5/4, 7/4 */
#define NUM_TIME_SIGS 5
static const int k_time_sig_beats[NUM_TIME_SIGS] = { 4, 3, 2, 5, 7 };
static const char *k_time_sig_names[NUM_TIME_SIGS] = { "4/4", "3/4", "2/4", "5/4", "7/4" };

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

/* Metronome State */
static volatile int g_bpm = DEFAULT_BPM;
static volatile int g_time_sig_idx = 0; /* Index into k_time_sig_beats */
static volatile int g_drop_pct = 0;     /* Click drop-rate (0% .. 100%, step 5%), starts at 0% */
static volatile bool g_playing = false;
static bool g_show_help = false;

#define LOCK_IN_MEASURES 4

/* Click synthesizer state */
static volatile int g_click_samples_left = 0;
static volatile bool g_click_is_accent = false;
static uint32_t g_click_sample_pos = 0;

static char g_config_path[512];

#define ACCENT_CLICK_MS 75
#define NORMAL_CLICK_MS 55

static void maximize_hardware_volume(void) {
#if !defined(HOST_X11)
    /* 1. Max out OSS /dev/mixer channels (Allwinner F1C100S suniv-codec) */
    int fd = open("/dev/mixer", O_RDWR);
    if (fd >= 0) {
        int vol = (100 << 8) | 100; /* 100% Left + 100% Right */
        ioctl(fd, SOUND_MIXER_WRITE_VOLUME, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_PCM, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_SPEAKER, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_ALTPCM, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_OGAIN, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_IGAIN, &vol);
        close(fd);
    }
    /* 2. Also max out ALSA mixer controls if amixer is present on MiyooCFW */
    int rc = system(
        "amixer sset 'Headphone' 100% unmute >/dev/null 2>&1; "
        "amixer sset 'Master' 100% unmute >/dev/null 2>&1; "
        "amixer sset 'PCM' 100% unmute >/dev/null 2>&1"
    );
    (void)rc;
#endif
}

static void trigger_click(bool accent) {
    g_click_is_accent = accent;
    g_click_sample_pos = 0;
    int duration_ms = accent ? ACCENT_CLICK_MS : NORMAL_CLICK_MS;
    g_click_samples_left = (SAMPLE_RATE * duration_ms) / 1000;
}

/* Play click unless dropped:
 * - First 4 measures (measure_count < 4) always play 100% of clicks to lock in the beat.
 * - Thereafter, drop clicks with probability g_drop_pct% (UI updates normally, sound omitted).
 */
static void maybe_trigger_click(bool accent, int measure_count, int drop_pct) {
    if (measure_count < LOCK_IN_MEASURES || drop_pct <= 0) {
        trigger_click(accent);
        return;
    }
    if (drop_pct >= 100) {
        return;
    }
    if ((rand() % 100) >= drop_pct) {
        trigger_click(accent);
    }
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *samples = (int16_t *)stream;
    int frames = len / (2 * AUDIO_CHANNELS);

    for (int i = 0; i < frames; i++) {
        int rem = g_click_samples_left;
        if (rem > 0) {
            bool accent = g_click_is_accent;
            uint32_t pos = g_click_sample_pos++;
            g_click_samples_left = rem - 1;

            int total_click_len = (SAMPLE_RATE * (accent ? ACCENT_CLICK_MS : NORMAL_CLICK_MS)) / 1000;
            double progress = (double)pos / (double)total_click_len;

            /* Sustained full-power envelope: hold at 100% full scale for the first
             * 70% (accent) or 65% (normal) of the click, then decay crisply */
            double sustain_frac = accent ? 0.70 : 0.65;
            double env = 1.0;
            if (progress > sustain_frac) {
                env = (1.0 - progress) / (1.0 - sustain_frac);
            }

            /* Frequencies tuned to the PowKiddy V90 speaker resonant peak (1-3 kHz):
             * Beat 1 Accent: 1760 Hz (A6) + 880 Hz body + 2640 Hz harmonic
             * Normal Beat  : 1000 Hz + 2000 Hz octave + 3000 Hz edge */
            double freq = accent ? 1760.0 : 1000.0;
            /* Add an extra pitch-drop transient in the first 5 ms for maximum punch */
            if (pos < (SAMPLE_RATE * 5) / 1000) {
                freq *= 1.35;
            }

            double phase = (2.0 * 3.14159265358979323846 * freq * (double)pos) / (double)SAMPLE_RATE;

            /* High-RMS saturated square-harmonic wave (drives rail-to-rail +/-32767) */
            double raw = sin(phase) + 0.65 * sin(phase * 2.0) + 0.45 * sin(phase * 3.0);
            if (accent) {
                raw += 0.50 * sin(phase * 0.5);
            }

            /* Hard overdrive saturation (3.5x gain before clamp) for maximum RMS loudness */
            double driven = raw * 3.5;
            if (driven > 1.0)  driven = 1.0;
            if (driven < -1.0) driven = -1.0;

            int32_t val = (int32_t)(driven * 32767.0 * env);
            if (val > 32767)  val = 32767;
            if (val < -32767) val = -32767;

            int16_t s = (int16_t)val;
            for (int c = 0; c < AUDIO_CHANNELS; c++) {
                samples[i * AUDIO_CHANNELS + c] = s;
            }
        } else {
            for (int c = 0; c < AUDIO_CHANNELS; c++) {
                samples[i * AUDIO_CHANNELS + c] = 0;
            }
        }
    }
}

static const char *get_tempo_marking(int bpm) {
    if (bpm < 45)  return "GRAVE";
    if (bpm < 60)  return "LARGO";
    if (bpm < 66)  return "LARGHETTO";
    if (bpm < 76)  return "ADAGIO";
    if (bpm < 108) return "ANDANTE";
    if (bpm < 120) return "MODERATO";
    if (bpm < 156) return "ALLEGRO";
    if (bpm < 176) return "VIVACE";
    if (bpm < 200) return "PRESTO";
    return "PRESTISSIMO";
}

static void resolve_config_path(void) {
    struct stat st;
    if (stat("/mnt", &st) == 0 && S_ISDIR(st.st_mode) && access("/mnt", W_OK) == 0) {
        snprintf(g_config_path, sizeof(g_config_path), "/mnt/.metronome.cfg");
    } else {
        const char *home = getenv("HOME");
        if (home && access(home, W_OK) == 0) {
            snprintf(g_config_path, sizeof(g_config_path), "%s/.metronome.cfg", home);
        } else {
            snprintf(g_config_path, sizeof(g_config_path), "./.metronome.cfg");
        }
    }
}

static void load_config(void) {
    FILE *f = fopen(g_config_path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int val = 0;
        if (sscanf(line, "bpm=%d", &val) == 1 && val >= MIN_BPM && val <= MAX_BPM) {
            g_bpm = val;
        } else if (sscanf(line, "time_sig_idx=%d", &val) == 1 && val >= 0 && val < NUM_TIME_SIGS) {
            g_time_sig_idx = val;
        }
    }
    fclose(f);
}

static void save_config(void) {
    FILE *f = fopen(g_config_path, "w");
    if (!f) return;
    fprintf(f, "bpm=%d\n", g_bpm);
    fprintf(f, "time_sig_idx=%d\n", g_time_sig_idx);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
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

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    putenv((char *)"SDL_NOMOUSE=1");
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    resolve_config_path();
    load_config();
    g_drop_pct = 0; /* Always start at 0% drop-rate so all clicks are played */

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "Failed to initialize video: %s\n", SDL_GetError());
        return 1;
    }

    maximize_hardware_volume();

    bool audio_ok = false;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec desired;
        memset(&desired, 0, sizeof(desired));
        desired.freq = SAMPLE_RATE;
        desired.format = AUDIO_S16SYS;
        desired.channels = AUDIO_CHANNELS;
        desired.samples = 512;
        desired.callback = audio_callback;
        if (SDL_OpenAudio(&desired, NULL) == 0) {
            maximize_hardware_volume();
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
    SDL_WM_SetCaption("V90 Metronome", NULL);

    /* Color Palette */
    Uint32 col_bg          = SDL_MapRGB(screen->format, 16, 20, 28);
    Uint32 col_header      = SDL_MapRGB(screen->format, 26, 33, 46);
    Uint32 col_panel       = SDL_MapRGB(screen->format, 23, 29, 41);
    Uint32 col_border      = SDL_MapRGB(screen->format, 55, 68, 92);
    Uint32 col_white       = SDL_MapRGB(screen->format, 240, 244, 248);
    Uint32 col_muted       = SDL_MapRGB(screen->format, 140, 155, 175);
    Uint32 col_accent      = SDL_MapRGB(screen->format, 235, 87, 87);   /* Red/Coral for Beat 1 Accent */
    Uint32 col_beat        = SDL_MapRGB(screen->format, 46, 204, 113);  /* Green for normal beats */
    Uint32 col_paused      = SDL_MapRGB(screen->format, 242, 153, 74);  /* Amber when paused */
    Uint32 col_blue        = SDL_MapRGB(screen->format, 52, 152, 219);  /* Time Sig Blue */
    Uint32 col_gold        = SDL_MapRGB(screen->format, 241, 196, 15);  /* Gold */
    Uint32 col_box_idle    = SDL_MapRGB(screen->format, 34, 42, 58);

    int current_beat = 0;  /* 0 .. beats_per_bar - 1 */
    int measure_count = 0; /* Measures elapsed since start (0..3 = 4-measure lock-in) */
    Uint32 last_tick_ms = SDL_GetTicks();
    double beat_accum_ms = 0.0;
    Uint32 beat_flash_ms = 0;

    bool up_held = false, down_held = false;
    Uint32 hold_start_ms = 0, last_repeat_ms = 0;

    bool running = true;

    while (running) {
        Uint32 now_ms = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                SDLKey k = ev.key.keysym.sym;

                /* MENU (Right Ctrl or 'Q' on Host): Save & Exit */
                if (k == V90_BTN_MENU
#if defined(HOST_X11)
                    || k == SDLK_q
#endif
                ) {
                    running = false;
                }
                /* Y Button (Space or 'Y'/'H' on Host): Toggle Help overlay */
                else if (k == V90_BTN_Y
#if defined(HOST_X11)
                         || k == SDLK_y || k == SDLK_h
#endif
                ) {
                    g_show_help = !g_show_help;
                } else if (g_show_help) {
                    if (k == V90_BTN_A || k == V90_BTN_B || k == SDLK_a || k == SDLK_b) {
                        g_show_help = false;
                    }
                }
                /* A Button (Left Alt or 'A'): Play / Pause */
                else if (k == V90_BTN_A || k == SDLK_a) {
                    g_playing = !g_playing;
                    if (g_playing) {
                        /* Immediately fire Beat 1 (accented) of Measure 1 when starting */
                        current_beat = 0;
                        measure_count = 0;
                        beat_accum_ms = 0.0;
                        last_tick_ms = now_ms;
                        beat_flash_ms = now_ms;
                        maybe_trigger_click(true, measure_count, g_drop_pct);
                    }
                }
                /* UP / DOWN: Tempo (BPM) control */
                else if (k == V90_BTN_UP) {
                    if (g_bpm < MAX_BPM) g_bpm++;
                    up_held = true;
                    hold_start_ms = now_ms;
                    last_repeat_ms = now_ms;
                    save_config();
                } else if (k == V90_BTN_DOWN) {
                    if (g_bpm > MIN_BPM) g_bpm--;
                    down_held = true;
                    hold_start_ms = now_ms;
                    last_repeat_ms = now_ms;
                    save_config();
                }
                /* L1 / R1: Reduce / Increase click drop-rate by 5% (0% .. 100%) */
                else if (k == V90_BTN_L1 || k == V90_BTN_R2
#if defined(HOST_X11)
                         || k == SDLK_LEFTBRACKET
#endif
                ) {
                    g_drop_pct -= 5;
                    if (g_drop_pct < 0) g_drop_pct = 0;
                } else if (k == V90_BTN_R1 || k == V90_BTN_L2
#if defined(HOST_X11)
                           || k == SDLK_RIGHTBRACKET
#endif
                ) {
                    g_drop_pct += 5;
                    if (g_drop_pct > 100) g_drop_pct = 100;
                }
                /* LEFT / RIGHT: Cycle between 4/4, 3/4, 2/4, 5/4, 7/4 */
                else if (k == V90_BTN_RIGHT) {
                    g_time_sig_idx = (g_time_sig_idx + 1) % NUM_TIME_SIGS;
                    current_beat = 0;
                    measure_count = 0;
                    beat_accum_ms = 0.0;
                    if (g_playing) {
                        last_tick_ms = now_ms;
                        beat_flash_ms = now_ms;
                        maybe_trigger_click(true, measure_count, g_drop_pct);
                    }
                    save_config();
                } else if (k == V90_BTN_LEFT) {
                    g_time_sig_idx = (g_time_sig_idx + NUM_TIME_SIGS - 1) % NUM_TIME_SIGS;
                    current_beat = 0;
                    measure_count = 0;
                    beat_accum_ms = 0.0;
                    if (g_playing) {
                        last_tick_ms = now_ms;
                        beat_flash_ms = now_ms;
                        maybe_trigger_click(true, measure_count, g_drop_pct);
                    }
                    save_config();
                }
            } else if (ev.type == SDL_KEYUP) {
                SDLKey k = ev.key.keysym.sym;
                if (k == V90_BTN_UP)     up_held = false;
                if (k == V90_BTN_DOWN)   down_held = false;
            } else if (ev.type == SDL_JOYBUTTONDOWN) {
                if (ev.jbutton.button == 1) { /* A button */
                    g_playing = !g_playing;
                    if (g_playing) {
                        current_beat = 0;
                        measure_count = 0;
                        beat_accum_ms = 0.0;
                        last_tick_ms = now_ms;
                        beat_flash_ms = now_ms;
                        maybe_trigger_click(true, measure_count, g_drop_pct);
                    }
                }
            }
        }

        /* Smooth key repeat when holding UP or DOWN to change BPM quickly */
        if ((up_held || down_held) && (now_ms >= hold_start_ms + 350)) {
            if (now_ms >= last_repeat_ms + 65) {
                if (up_held && g_bpm < MAX_BPM) g_bpm++;
                if (down_held && g_bpm > MIN_BPM) g_bpm--;
                last_repeat_ms = now_ms;
            }
        }

        /* Advance Metronome Beat Clock */
        now_ms = SDL_GetTicks();
        int beats_per_bar = k_time_sig_beats[g_time_sig_idx];
        double interval_ms = 60000.0 / (double)g_bpm;

        if (g_playing) {
            Uint32 dt = (now_ms >= last_tick_ms) ? (now_ms - last_tick_ms) : 0;
            if (dt > 2000) dt = 0;
            last_tick_ms = now_ms;
            beat_accum_ms += (double)dt;

            while (beat_accum_ms >= interval_ms) {
                beat_accum_ms -= interval_ms;
                current_beat = (current_beat + 1) % beats_per_bar;
                if (current_beat == 0) {
                    measure_count++;
                }
                beat_flash_ms = now_ms;
                maybe_trigger_click(current_beat == 0, measure_count, g_drop_pct);
            }
        } else {
            last_tick_ms = now_ms;
        }

        /* ================= RENDER UI (320x240) ================= */
        bool is_accent_beat = (current_beat == 0);
        bool flash_active = g_playing && ((now_ms - beat_flash_ms) < 90);

        draw_rect(screen, 0, 0, SCREEN_W, SCREEN_H, col_bg);

        /* 1. Top Header Bar (y: 0..26) */
        Uint32 top_col = g_playing ? (is_accent_beat ? col_accent : col_beat) : col_paused;
        const char *top_str = g_playing ? "METRONOME: PLAYING" : "METRONOME: PAUSED";
        draw_rect(screen, 0, 0, SCREEN_W, 26, col_header);
        draw_rect(screen, 0, 24, SCREEN_W, 2, top_col);
        draw_text_centered(screen, SCREEN_W / 2, 6, top_str, 2, top_col);

        /* 2. Tempo (BPM) & Time Signature Panels (y: 32..118) */
        /* Left Card: Tempo (BPM) */
        draw_rect(screen, 10, 32, 186, 86, col_panel);
        draw_border(screen, 10, 32, 186, 86, 1, flash_active ? top_col : col_border);
        draw_text_centered(screen, 103, 38, "TEMPO (BPM)", 1, col_muted);

        char bpm_str[16];
        snprintf(bpm_str, sizeof(bpm_str), "%d", g_bpm);
        draw_text_centered(screen, 103, 52, bpm_str, 6, col_white);
        draw_text_centered(screen, 103, 102, get_tempo_marking(g_bpm), 1, col_gold);

        /* Right Card: Time Signature */
        draw_rect(screen, 204, 32, 106, 86, col_panel);
        draw_border(screen, 204, 32, 106, 86, 1, col_blue);
        draw_text_centered(screen, 257, 38, "TIME SIG", 1, col_muted);
        draw_text_centered(screen, 257, 56, k_time_sig_names[g_time_sig_idx], 4, col_blue);
        draw_text_centered(screen, 257, 102, "< L / R >", 1, col_muted);

        /* 3. Beat Visualizer Panel (y: 124..208) */
        draw_rect(screen, 10, 124, 300, 84, col_panel);
        draw_border(screen, 10, 124, 300, 84, 1, col_border);

        char beat_hdr[64];
        if (g_playing) {
            snprintf(beat_hdr, sizeof(beat_hdr), "BEAT %d OF %d %s",
                     current_beat + 1, beats_per_bar,
                     (current_beat == 0) ? "(ACCENT!)" : "");
        } else {
            snprintf(beat_hdr, sizeof(beat_hdr), "PRESS [A] TO START  |  ACCENT ON BEAT 1");
        }
        draw_text_centered(screen, 160, 131, beat_hdr, 1,
                           (g_playing && current_beat == 0) ? col_accent : col_white);

        /* Draw Beat Boxes (1 .. beats_per_bar) */
        int area_w = 280;
        int gap = 6;
        int box_w = (area_w - gap * (beats_per_bar - 1)) / beats_per_bar;
        if (box_w > 48) box_w = 48;
        int total_boxes_w = box_w * beats_per_bar + gap * (beats_per_bar - 1);
        int start_x = 160 - total_boxes_w / 2;
        int box_y = 141;
        int box_h = 36;

        for (int b = 0; b < beats_per_bar; b++) {
            int bx = start_x + b * (box_w + gap);
            bool is_active = g_playing && (b == current_beat);
            Uint32 fill_col = col_box_idle;
            Uint32 bdr_col  = (b == 0) ? col_accent : col_border;

            if (is_active) {
                fill_col = (b == 0) ? col_accent : col_beat;
                bdr_col  = col_white;
            }

            draw_rect(screen, bx, box_y, box_w, box_h, fill_col);
            draw_border(screen, bx, box_y, box_w, box_h, (b == 0) ? 2 : 1, bdr_col);

            char num_str[16];
            snprintf(num_str, sizeof(num_str), "%d", b + 1);
            Uint32 num_col = is_active ? col_bg : ((b == 0) ? col_accent : col_white);
            draw_text_centered(screen, bx + box_w / 2, box_y + 6, num_str, 2, num_col);

            if (b == 0) {
                draw_text_centered(screen, bx + box_w / 2, box_y + 24, "ACC", 1,
                                   is_active ? col_bg : col_accent);
            }
        }

        /* Single drop% indicator below the beat markings */
        char drop_str[32];
        snprintf(drop_str, sizeof(drop_str), "DROP: %d%%", g_drop_pct);
        draw_text_centered(screen, 160, 182, drop_str, 1,
                           (g_drop_pct > 0) ? col_gold : col_muted);

        /* Pendulum / Phase Progress Bar inside Beat Panel (y: 195..201) */
        int pbar_x = 24, pbar_y = 195, pbar_w = 272, pbar_h = 5;
        draw_rect(screen, pbar_x, pbar_y, pbar_w, pbar_h, col_bg);
        if (g_playing && interval_ms > 0.0) {
            double phase = beat_accum_ms / interval_ms;
            if (phase < 0.0) phase = 0.0;
            if (phase > 1.0) phase = 1.0;
            /* Swing left-to-right on even beats, right-to-left on odd beats */
            double pos = (current_beat % 2 == 0) ? phase : (1.0 - phase);
            int knob_w = 18;
            int knob_x = pbar_x + (int)(pos * (pbar_w - knob_w));
            draw_rect(screen, knob_x, pbar_y, knob_w, pbar_h,
                      (current_beat == 0) ? col_accent : col_beat);
        }

        /* 4. Bottom Controls Footer (y: 214..240) */
        draw_rect(screen, 0, 214, SCREEN_W, 26, col_header);
        draw_rect(screen, 0, 214, SCREEN_W, 1, col_border);

        Uint32 a_col = g_playing ? col_paused : col_beat;
        draw_rect(screen, 4, 220, 82, 14, a_col);
        draw_text_centered(screen, 45, 224, "[A] PLAY/STOP", 1, col_bg);

        draw_text(screen, 91, 224, "[UP/DN]BPM [L/R]SIG [Y]HLP [MENU]EXIT", 1, col_white);

        if (g_show_help) {
            int mx = 14, my = 18, mw = SCREEN_W - 28, mh = SCREEN_H - 36;
            draw_rect(screen, mx, my, mw, mh, col_panel);
            draw_border(screen, mx, my, mw, mh, 2, col_blue);
            draw_rect(screen, mx + 2, my + 2, mw - 4, 22, col_header);
            draw_text_centered(screen, SCREEN_W / 2, my + 6, "METRONOME CONTROLS", 2, col_gold);

            static const struct {
                const char *btn;
                const char *desc;
            } k_help[6] = {
                { "[A] BUTTON",  "PLAY / PAUSE METRONOME" },
                { "[UP / DOWN]", "ADJUST TEMPO (30..260 BPM)" },
                { "[LEFT/RIGHT]","CYCLE TIME SIG (4/4..7/4)" },
                { "[L1 / R1]",   "CLICK DROP-RATE (0%..100%)" },
                { "[Y] BUTTON",  "OPEN / CLOSE HELP SCREEN" },
                { "[MENU]",      "SAVE SETTINGS & EXIT" }
            };
            for (int i = 0; i < 6; i++) {
                int ry = my + 32 + i * 24;
                draw_rect(screen, mx + 8, ry, 82, 18, col_box_idle);
                draw_border(screen, mx + 8, ry, 82, 18, 1, col_gold);
                draw_text_centered(screen, mx + 49, ry + 5, k_help[i].btn, 1, col_gold);
                draw_text(screen, mx + 98, ry + 5, k_help[i].desc, 1, col_white);
            }
            draw_rect(screen, mx + 2, my + mh - 18, mw - 4, 16, col_header);
            draw_text_centered(screen, SCREEN_W / 2, my + mh - 13, "PRESS [Y] TO CLOSE HELP", 1, col_blue);
        }

        SDL_Flip(screen);
        SDL_Delay(16); /* ~60 FPS for crisp visual beat synchronization */
    }

    save_config();
    if (audio_ok) {
        SDL_CloseAudio();
    }
    SDL_Quit();
    return 0;
}
