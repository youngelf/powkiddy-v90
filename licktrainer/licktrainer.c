/*
 * licktrainer - Guitar Speed-Builder Practice Routine & Lick Coach
 *               for PowKiddy V90 (MiyooCFW 1.3.3) & Linux x86_64 Desktop (X11)
 *
 * Screen resolution: 320x240, 16-bit RGB565 (scaled 2x on Linux X11 desktop)
 * Audio Engine     : Background-thread fixed-point Karplus-Strong guitar string
 *                    synthesizer + precision metronome click (44.1 kHz stereo)
 *
 * Controls (PowKiddy V90 / Linux Desktop):
 *   A (LALT / A)                      : Play / Pause exercise loop
 *   B (LCTRL / B)                     : Cycle Audio Mix (TAB+CLICK / CLICK / TAB)
 *   SELECT (ESCAPE / C)               : Cycle Speed-Builder BPM Ladder preset (Practice Mode)
 *   X (LSHIFT / X)                    : Transpose Musical Key / Neck Position
 *   Y (SPACE / Y)                     : Toggle Help & Practice Guide overlay
 *   L1 / R1 (TAB / BACKSPACE, [ / ])  : Previous / Next Exercise Category (1..5)
 *   LEFT / RIGHT                      : Previous / Next Exercise in category
 *   UP / DOWN                         : Increase / Decrease BPM (hold to repeat)
 *   MENU (RCTRL / Q)                  : Save settings & Exit cleanly
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "licktrainer_font.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define SCREEN_BPP 16
#define SAMPLE_RATE 44100
#define AUDIO_CHANNELS 2

/* =========================================================================
 * Platform Backend: Native X11 + PulseAudio (Linux Host) vs SDL 1.2 (V90)
 * ========================================================================= */
#if defined(HOST_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <dlfcn.h>

#ifndef HOST_SCALE
#define HOST_SCALE 2
#endif

typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef uint32_t Uint32;

typedef int SDLKey;
#define SDLK_UNKNOWN      0
#define SDLK_LALT         XK_Alt_L
#define SDLK_LCTRL        XK_Control_L
#define SDLK_LSHIFT       XK_Shift_L
#define SDLK_SPACE        XK_space
#define SDLK_TAB          XK_Tab
#define SDLK_BACKSPACE    XK_BackSpace
#define SDLK_PAGEUP       XK_Page_Up
#define SDLK_PAGEDOWN     XK_Page_Down
#define SDLK_ESCAPE       XK_Escape
#define SDLK_RETURN       XK_Return
#define SDLK_RCTRL        XK_Control_R
#define SDLK_UP           XK_Up
#define SDLK_DOWN         XK_Down
#define SDLK_LEFT         XK_Left
#define SDLK_RIGHT        XK_Right
#define SDLK_LEFTBRACKET  XK_bracketleft
#define SDLK_RIGHTBRACKET XK_bracketright
#define SDLK_a            XK_a
#define SDLK_b            XK_b
#define SDLK_c            XK_c
#define SDLK_h            XK_h
#define SDLK_q            XK_q
#define SDLK_r            XK_r
#define SDLK_x            XK_x
#define SDLK_y            XK_y

#define SDL_QUIT          1
#define SDL_KEYDOWN       2
#define SDL_KEYUP         3

typedef struct { int dummy; } SDL_PixelFormat;
typedef struct { Sint16 x, y; Uint16 w, h; } SDL_Rect;
typedef struct {
    int w, h;
    Uint16 pitch;
    SDL_PixelFormat *format;
    Uint16 *pixels;
} SDL_Surface;

typedef struct {
    int type;
    struct {
        struct { SDLKey sym; } keysym;
    } key;
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
            pa = fn_new(NULL, "V90 LickTrainer", PA_STREAM_PLAYBACK, NULL, "Guitar Lick Coach", &ss, NULL, NULL, &err);
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
    return "Cannot open X11 DISPLAY";
}

static int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
static void SDL_ShowCursor(int toggle) { (void)toggle; }
static int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
static void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }
static bool SDL_MUSTLOCK(SDL_Surface *s) { (void)s; return false; }

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

static void SDL_PauseAudio(int pause_on) { g_audio_paused = (pause_on != 0); }

static void SDL_CloseAudio(void) {
    if (g_audio_running) {
        g_audio_running = false;
        pthread_join(g_audio_thread, NULL);
    }
}

static SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags) {
    (void)bpp; (void)flags;
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
    g_x11_surface.pitch = (Uint16)(width * sizeof(Uint16));
    g_x11_surface.format = &g_dummy_fmt;
    g_x11_surface.pixels = (Uint16 *)calloc(width * height, sizeof(Uint16));

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
    if (g_x11_dpy && g_x11_win) XStoreName(g_x11_dpy, g_x11_win, title);
}

static void SDL_Flip(SDL_Surface *screen) {
    int win_w = screen->w * HOST_SCALE;
    Uint32 *dst = (Uint32 *)g_scaled_buf;
    for (int y = 0; y < screen->h; y++) {
        for (int x = 0; x < screen->w; x++) {
            Uint16 p = screen->pixels[y * screen->w + x];
            Uint32 r = ((p >> 11) & 0x1F) * 255 / 31;
            Uint32 g = ((p >> 5)  & 0x3F) * 255 / 63;
            Uint32 b = (p & 0x1F) * 255 / 31;
            Uint32 c = (r << 16) | (g << 8) | b;
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
            if (ks >= XK_A && ks <= XK_Z) ks = ks - XK_A + XK_a;
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
    return (Uint32)(total_ms < 0 ? 0 : total_ms);
}

static void SDL_Delay(Uint32 ms) { usleep(ms * 1000); }

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

/* =========================================================================
 * PowKiddy V90 / MiyooCFW 1.3.3 Hardware Key Mappings
 * ========================================================================= */
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
#define V90_BTN_MENU    SDLK_RCTRL
#define V90_BTN_UP      SDLK_UP
#define V90_BTN_DOWN    SDLK_DOWN
#define V90_BTN_LEFT    SDLK_LEFT
#define V90_BTN_RIGHT   SDLK_RIGHT

/* =========================================================================
 * RGB565 Drawing & Anti-Aliased Font Primitives
 * ========================================================================= */
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#define COL_BG          rgb565(16, 20, 28)
#define COL_HEADER      rgb565(26, 33, 46)
#define COL_PANEL       rgb565(23, 29, 41)
#define COL_PANEL_ALT   rgb565(30, 38, 54)
#define COL_BORDER      rgb565(55, 68, 92)
#define COL_WHITE       rgb565(240, 244, 248)
#define COL_MUTED       rgb565(140, 155, 175)
#define COL_DIM         rgb565(82, 96, 120)
#define COL_ACCENT      rgb565(235, 87, 87)
#define COL_GOLD        rgb565(241, 196, 15)
#define COL_CYAN        rgb565(52, 152, 219)
#define COL_GREEN       rgb565(46, 204, 113)
#define COL_ORANGE      rgb565(242, 153, 74)
#define COL_PURPLE      rgb565(155, 89, 182)
#define COL_NECK_WOOD   rgb565(44, 32, 26)
#define COL_FRET_WIRE   rgb565(130, 142, 160)

static inline void put_pixel(SDL_Surface *s, int x, int y, uint16_t col) {
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
        uint16_t *row = (uint16_t *)((uint8_t *)s->pixels + y * s->pitch);
        row[x] = col;
    }
}

static inline void blend_pixel(SDL_Surface *s, int x, int y, uint16_t col, uint8_t alpha) {
    if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H || alpha == 0) return;
    uint16_t *row = (uint16_t *)((uint8_t *)s->pixels + y * s->pitch);
    if (alpha == 255) {
        row[x] = col;
        return;
    }
    uint16_t bg = row[x];
    uint32_t sr = (col >> 11) & 0x1F, sg = (col >> 5) & 0x3F, sb = col & 0x1F;
    uint32_t dr = (bg  >> 11) & 0x1F, dg = (bg  >> 5) & 0x3F, db = bg  & 0x1F;
    uint32_t r = (sr * alpha + dr * (255 - alpha)) / 255;
    uint32_t g = (sg * alpha + dg * (255 - alpha)) / 255;
    uint32_t b = (sb * alpha + db * (255 - alpha)) / 255;
    row[x] = (uint16_t)((r << 11) | (g << 5) | b);
}

static void fill_rect(SDL_Surface *s, int x, int y, int w, int h, uint16_t col) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > SCREEN_W) ? SCREEN_W : (x + w);
    int y1 = (y + h > SCREEN_H) ? SCREEN_H : (y + h);
    for (int py = y0; py < y1; py++) {
        uint16_t *row = (uint16_t *)((uint8_t *)s->pixels + py * s->pitch);
        for (int px = x0; px < x1; px++) row[px] = col;
    }
}

static void fill_rect_alpha(SDL_Surface *s, int x, int y, int w, int h, uint16_t col, uint8_t alpha) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > SCREEN_W) ? SCREEN_W : (x + w);
    int y1 = (y + h > SCREEN_H) ? SCREEN_H : (y + h);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) blend_pixel(s, px, py, col, alpha);
    }
}

static void draw_border(SDL_Surface *s, int x, int y, int w, int h, int t, uint16_t col) {
    fill_rect(s, x, y, w, t, col);
    fill_rect(s, x, y + h - t, w, t, col);
    fill_rect(s, x, y, t, h, col);
    fill_rect(s, x + w - t, y, t, h, col);
}

static void fill_circle(SDL_Surface *s, int cx, int cy, int r, uint16_t col) {
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2) {
                put_pixel(s, cx + dx, cy + dy, col);
            }
        }
    }
}

static void draw_circle_outline(SDL_Surface *s, int cx, int cy, int r, uint16_t col) {
    int r_in = (r - 1) * (r - 1);
    int r_out = r * r + r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= r_in && d2 <= r_out) {
                put_pixel(s, cx + dx, cy + dy, col);
            }
        }
    }
}

static int text_width(int font_id, const char *str) {
    if (!str || font_id < 0 || font_id >= FB_FONT_COUNT) return 0;
    const FbFont *f = &g_fb_fonts[font_id];
    int w = 0;
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        if (c >= 32 && c <= 126) w += f->glyphs[c - 32].xadvance;
    }
    return w;
}

static void draw_text(SDL_Surface *s, int font_id, int x, int y, const char *str, uint16_t col) {
    if (!str || font_id < 0 || font_id >= FB_FONT_COUNT) return;
    const FbFont *f = &g_fb_fonts[font_id];
    int cur_x = x;
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        if (c < 32 || c > 126) c = '?';
        const FbGlyph *g = &f->glyphs[c - 32];
        const uint8_t *bmp = &g_fb_font_bitmap[g->data_offset];
        for (int gy = 0; gy < g->height; gy++) {
            int py = y + g->yoff + gy;
            for (int gx = 0; gx < g->width; gx++) {
                uint8_t a = bmp[gy * g->width + gx];
                if (a > 0) blend_pixel(s, cur_x + g->xoff + gx, py, col, a);
            }
        }
        cur_x += g->xadvance;
    }
}

static void draw_text_centered(SDL_Surface *s, int font_id, int cx, int y, const char *str, uint16_t col) {
    int w = text_width(font_id, str);
    draw_text(s, font_id, cx - w / 2, y, str, col);
}

/* =========================================================================
 * Guitar Exercise & Lick Data Model (5 Categories x 5 Licks = 25 Exercises)
 * ========================================================================= */
#define NUM_CATEGORIES 5
#define LICKS_PER_CAT  5
#define TOTAL_LICKS    (NUM_CATEGORIES * LICKS_PER_CAT)
#define MAX_STEPS      16

typedef struct {
    int8_t str;      /* 0=6th(E) .. 5=1st(e), -1=rest */
    int8_t fret_off; /* Offset relative to anchor fret */
    int8_t finger;   /* Left-hand finger: 1=Index, 2=Middle, 3=Ring, 4=Pinky */
    char   pick;     /* 'D'=Down, 'U'=Up, 'H'=Hammer-on, 'P'=Pull-off, '/'=Slide */
} LickStep;

typedef struct {
    int         cat_idx;
    const char *title;
    const char *subtitle;
    int         num_steps;       /* 16 for 16th-note 4/4, 12 for triplets */
    int         steps_per_beat;  /* 4 for 16th notes, 3 for triplets */
    int         default_bpm;
    LickStep    steps[MAX_STEPS];
} LickDef;

static const char *k_category_names[NUM_CATEGORIES] = {
    "PENTATONIC",
    "LEGATO/ARP",
    "ALT PICK",
    "BLUES/ROCK",
    "SPIDER"
};

/* 12 Transposable Keys with their primary guitar neck anchor fret */
#define NUM_KEYS 12
typedef struct {
    const char *name;
    int         anchor_fret;
} KeyAnchor;

static const KeyAnchor k_keys[NUM_KEYS] = {
    { "E",  0  },
    { "F",  1  },
    { "F#", 2  },
    { "G",  3  },
    { "G#", 4  },
    { "A",  5  }, /* Default: Key of A at 5th fret */
    { "Bb", 6  },
    { "B",  7  },
    { "C",  8  },
    { "C#", 9  },
    { "D",  10 },
    { "Eb", 11 }
};

static const char *k_note_names[12] = {
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"
};

/* Standard tuning open-string MIDI pitches: E2(40), A2(45), D3(50), G3(55), B3(59), E4(64) */
static const int k_open_midi[6] = { 40, 45, 50, 55, 59, 64 };
static const char k_str_labels[6] = { 'E', 'A', 'D', 'G', 'B', 'e' };

/*
 * 25 Curated Guitar Exercises ordered per design review:
 *   Set 1 (#01..#05): PENTATONIC LICKS
 *   Set 2 (#06..#10): LEGATO & ARPEGGIOS
 *   Set 3 (#11..#15): ALTERNATE PICKING
 *   Set 4 (#16..#20): BLUES & ROCK
 *   Set 5 (#21..#25): WARMUP & SPIDER
 */
static const LickDef k_licks[TOTAL_LICKS] = {
    /* ==================== 1. PENTATONIC LICKS (#01..#05) ==================== */
    {
        0, "GILBERT 4-NOTE SEQ", "Paul Gilbert Box 1 Pentatonic Burner", 16, 4, 100,
        {
            {4,0,1,'D'}, {4,3,4,'U'}, {5,0,1,'D'}, {4,3,4,'U'},
            {4,0,1,'D'}, {3,2,3,'U'}, {4,0,1,'D'}, {4,3,4,'U'},
            {5,0,1,'D'}, {5,3,4,'U'}, {5,0,1,'P'}, {4,3,4,'D'},
            {5,0,1,'U'}, {4,3,4,'D'}, {4,0,1,'P'}, {3,2,3,'D'}
        }
    },
    {
        0, "ERIC JOHNSON 5S", "Cascading Pentatonic Waterfall", 16, 4, 96,
        {
            {5,3,4,'D'}, {5,0,1,'P'}, {4,3,4,'D'}, {4,0,1,'U'},
            {3,2,3,'D'}, {4,3,4,'U'}, {4,0,1,'P'}, {3,2,3,'D'},
            {3,0,1,'U'}, {2,2,3,'D'}, {3,2,3,'U'}, {3,0,1,'P'},
            {2,2,3,'D'}, {2,0,1,'U'}, {1,2,3,'D'}, {1,0,1,'U'}
        }
    },
    {
        0, "PENTATONIC THREES", "Ascending 12/8 Triplet Box 1 Sequence", 12, 3, 95,
        {
            {0,0,1,'D'}, {0,3,4,'U'}, {1,0,1,'D'},
            {0,3,4,'U'}, {1,0,1,'D'}, {1,2,3,'U'},
            {1,0,1,'D'}, {1,2,3,'U'}, {2,0,1,'D'},
            {1,2,3,'U'}, {2,0,1,'D'}, {2,2,3,'U'}
        }
    },
    {
        0, "WIDE STRING SKIP", "String-Skipping Minor Pentatonic Etude", 16, 4, 90,
        {
            {0,0,1,'D'}, {0,3,4,'U'}, {2,0,1,'D'}, {2,2,3,'U'},
            {1,0,1,'D'}, {1,2,3,'U'}, {3,0,1,'D'}, {3,2,3,'U'},
            {2,0,1,'D'}, {2,2,3,'U'}, {4,0,1,'D'}, {4,3,4,'U'},
            {3,0,1,'D'}, {3,2,3,'U'}, {5,0,1,'D'}, {5,3,4,'U'}
        }
    },
    {
        0, "BOX 1 TO 2 SLIDE", "Diagonal Pentatonic Position Shift", 16, 4, 100,
        {
            {0,0,1,'D'}, {0,3,3,'H'}, {1,0,1,'D'}, {1,2,3,'H'},
            {1,4,3,'/'}, {2,2,1,'D'}, {2,4,3,'H'}, {3,2,1,'D'},
            {3,4,3,'H'}, {3,6,3,'/'}, {4,5,2,'D'}, {4,8,4,'H'},
            {5,5,1,'D'}, {5,8,4,'U'}, {5,5,1,'P'}, {4,8,4,'D'}
        }
    },

    /* ==================== 2. LEGATO & ARPEGGIOS (#06..#10) ==================== */
    {
        1, "LEGATO 3-NPS ROLL", "Hammer-On & Pull-Off Stamina Builder", 16, 4, 95,
        {
            {3,0,1,'D'}, {3,2,2,'H'}, {3,4,4,'H'}, {3,2,2,'P'},
            {4,0,1,'D'}, {4,2,2,'H'}, {4,3,3,'H'}, {4,2,2,'P'},
            {5,0,1,'D'}, {5,2,2,'H'}, {5,3,3,'H'}, {5,2,2,'P'},
            {5,0,1,'P'}, {4,3,3,'D'}, {4,2,2,'P'}, {4,0,1,'P'}
        }
    },
    {
        1, "MINOR TRIAD ARP", "3-String Minor Arpeggio Etude", 12, 3, 100,
        {
            {3,4,3,'D'}, {4,2,2,'D'}, {5,0,1,'D'},
            {5,3,4,'H'}, {5,0,1,'P'}, {4,2,2,'U'},
            {3,4,3,'U'}, {4,2,2,'D'}, {5,0,1,'D'},
            {5,3,4,'U'}, {5,0,1,'P'}, {4,2,2,'U'}
        }
    },
    {
        1, "MIN7 SKIPPED ARP", "String-Skipped Minor 7th Arpeggio", 16, 4, 92,
        {
            {1,2,1,'D'}, {1,5,4,'H'}, {3,2,1,'D'}, {3,4,3,'H'},
            {5,2,1,'D'}, {5,5,4,'H'}, {5,2,1,'P'}, {3,4,3,'D'},
            {3,2,1,'P'}, {1,5,4,'D'}, {1,2,1,'P'}, {3,2,1,'D'},
            {3,4,3,'H'}, {5,2,1,'D'}, {5,5,4,'U'}, {5,2,1,'P'}
        }
    },
    {
        1, "DIM7 SYMMETRY ARP", "Symmetrical Diminished 7th Sequence", 16, 4, 96,
        {
            {3,1,1,'D'}, {3,4,4,'H'}, {4,3,3,'D'}, {5,1,1,'D'},
            {5,4,4,'H'}, {5,1,1,'P'}, {4,3,3,'U'}, {3,4,4,'U'},
            {3,4,1,'/'}, {3,7,4,'H'}, {4,6,3,'D'}, {5,4,1,'D'},
            {5,7,4,'H'}, {5,4,1,'P'}, {4,6,3,'U'}, {3,7,4,'U'}
        }
    },
    {
        1, "5-STR SWEEP SHAPE", "A-Shape 5-String Minor Sweep Arpeggio", 16, 4, 88,
        {
            {1,2,4,'D'}, {2,0,2,'D'}, {3,0,2,'D'}, {4,1,3,'D'},
            {5,0,1,'D'}, {5,3,4,'H'}, {5,0,1,'P'}, {4,1,3,'U'},
            {3,0,2,'U'}, {2,0,2,'U'}, {1,2,4,'U'}, {2,0,2,'D'},
            {3,0,2,'D'}, {4,1,3,'D'}, {5,0,1,'D'}, {5,3,4,'U'}
        }
    },

    /* ==================== 3. ALTERNATE PICKING (#11..#15) ==================== */
    {
        2, "GILBERT 3-NPS CELL", "Inside/Outside 6-Note Picking Etude", 12, 3, 105,
        {
            {3,0,1,'D'}, {3,2,2,'U'}, {3,4,4,'D'},
            {4,0,1,'U'}, {3,4,4,'D'}, {3,2,2,'U'},
            {3,0,1,'D'}, {3,2,2,'U'}, {3,4,4,'D'},
            {4,0,1,'U'}, {4,2,2,'D'}, {4,3,3,'U'}
        }
    },
    {
        2, "NEOCLASSIC PEDAL", "Harmonic Minor High-e Pedal Point", 16, 4, 104,
        {
            {5,0,1,'D'}, {5,3,4,'U'}, {5,2,3,'D'}, {5,3,4,'U'},
            {4,3,4,'D'}, {5,3,4,'U'}, {5,0,1,'D'}, {5,3,4,'U'},
            {4,2,2,'D'}, {5,3,4,'U'}, {4,3,4,'D'}, {5,3,4,'U'},
            {5,0,1,'D'}, {5,3,4,'U'}, {5,2,3,'D'}, {5,0,1,'U'}
        }
    },
    {
        2, "DIATONIC 4-NOTE", "Strict Alternate Picking 3-NPS Climb", 16, 4, 100,
        {
            {0,0,1,'D'}, {0,2,2,'U'}, {0,3,3,'D'}, {1,0,1,'U'},
            {0,2,2,'D'}, {0,3,3,'U'}, {1,0,1,'D'}, {1,2,2,'U'},
            {0,3,3,'D'}, {1,0,1,'U'}, {1,2,2,'D'}, {1,3,3,'U'},
            {1,0,1,'D'}, {1,2,2,'U'}, {1,3,3,'D'}, {2,0,1,'U'}
        }
    },
    {
        2, "DI MEOLA SYNCOPA", "Syncopated Cross-String Picking Etude", 16, 4, 98,
        {
            {2,2,3,'D'}, {3,0,1,'U'}, {3,2,3,'D'}, {4,1,2,'U'},
            {3,2,3,'D'}, {3,0,1,'U'}, {2,2,3,'D'}, {3,0,1,'U'},
            {3,2,3,'D'}, {4,1,2,'U'}, {4,3,4,'D'}, {5,0,1,'U'},
            {4,3,4,'D'}, {4,1,2,'U'}, {3,2,3,'D'}, {3,0,1,'U'}
        }
    },
    {
        2, "CROSS-STRING JUMP", "Alternate Picking String-Crossing Drill", 16, 4, 94,
        {
            {2,0,1,'D'}, {3,2,3,'U'}, {2,2,3,'D'}, {4,0,1,'U'},
            {2,0,1,'D'}, {4,3,4,'U'}, {3,0,1,'D'}, {5,0,1,'U'},
            {3,2,3,'D'}, {5,3,4,'U'}, {4,0,1,'D'}, {5,0,1,'U'},
            {4,3,4,'D'}, {3,2,3,'U'}, {4,0,1,'D'}, {3,0,1,'U'}
        }
    },

    /* ==================== 4. BLUES & ROCK (#16..#20) ==================== */
    {
        3, "BB & ALBERT TURN", "Classic Major/Minor Blues Turnaround", 16, 4, 92,
        {
            {3,2,2,'D'}, {3,4,3,'/'}, {4,3,2,'U'}, {5,3,3,'D'},
            {5,5,4,'U'}, {5,3,3,'P'}, {4,5,4,'D'}, {4,3,2,'U'},
            {3,2,2,'D'}, {3,0,1,'P'}, {3,1,2,'H'}, {2,2,3,'D'},
            {3,0,1,'U'}, {4,0,1,'D'}, {4,3,4,'U'}, {5,0,1,'D'}
        }
    },
    {
        3, "SRV TEXAS SHUFFLE", "Driving 12/8 Blues Box Groove", 12, 3, 108,
        {
            {0,0,1,'D'}, {0,3,4,'U'}, {1,0,1,'D'},
            {1,1,2,'H'}, {1,2,3,'H'}, {2,0,1,'D'},
            {2,2,3,'U'}, {3,0,1,'D'}, {3,1,2,'H'},
            {4,0,1,'D'}, {4,3,4,'U'}, {2,2,3,'D'}
        }
    },
    {
        3, "DORIAN MIXO RUN", "Hybrid Minor/Major 3rd & 6th Lick", 16, 4, 98,
        {
            {1,0,1,'D'}, {1,2,3,'H'}, {2,0,1,'D'}, {2,2,3,'H'},
            {3,0,1,'D'}, {3,1,2,'H'}, {4,0,1,'D'}, {4,2,3,'H'},
            {5,0,1,'D'}, {5,2,3,'U'}, {5,3,4,'D'}, {5,0,1,'P'},
            {4,3,4,'D'}, {4,2,3,'U'}, {3,0,1,'D'}, {3,1,2,'H'}
        }
    },
    {
        3, "HENDRIX CHORD EMBL", "Double-Stop Style Melodic Fill", 16, 4, 88,
        {
            {2,0,1,'D'}, {2,2,3,'H'}, {3,0,1,'D'}, {2,2,3,'U'},
            {3,0,1,'D'}, {3,2,3,'H'}, {3,0,1,'P'}, {2,2,3,'D'},
            {4,0,1,'D'}, {4,3,4,'H'}, {4,0,1,'P'}, {3,2,3,'D'},
            {3,0,1,'U'}, {2,2,3,'D'}, {2,0,1,'P'}, {1,2,3,'D'}
        }
    },
    {
        3, "BLUES b5 CASCADE", "Descending Blues Scale with Blue Note", 16, 4, 96,
        {
            {5,3,4,'D'}, {5,0,1,'P'}, {4,3,4,'D'}, {4,0,1,'P'},
            {3,3,4,'D'}, {3,2,3,'P'}, {3,0,1,'P'}, {2,2,3,'D'},
            {3,2,3,'U'}, {3,0,1,'P'}, {2,2,3,'D'}, {2,0,1,'P'},
            {1,2,3,'D'}, {1,1,2,'P'}, {1,0,1,'P'}, {0,3,4,'D'}
        }
    },

    /* ==================== 5. WARMUP & SPIDER (#21..#25) ==================== */
    {
        4, "1-2-3-4 SPIDER", "Classic Chromatic 4-Finger Crawl", 16, 4, 90,
        {
            {0,0,1,'D'}, {0,1,2,'U'}, {0,2,3,'D'}, {0,3,4,'U'},
            {1,0,1,'D'}, {1,1,2,'U'}, {1,2,3,'D'}, {1,3,4,'U'},
            {2,0,1,'D'}, {2,1,2,'U'}, {2,2,3,'D'}, {2,3,4,'U'},
            {3,0,1,'D'}, {3,1,2,'U'}, {3,2,3,'D'}, {3,3,4,'U'}
        }
    },
    {
        4, "1-3-2-4 PERMUTE", "Alternating Finger Independence Drill", 16, 4, 88,
        {
            {2,0,1,'D'}, {2,2,3,'U'}, {2,1,2,'D'}, {2,3,4,'U'},
            {3,0,1,'D'}, {3,2,3,'U'}, {3,1,2,'D'}, {3,3,4,'U'},
            {4,0,1,'D'}, {4,2,3,'U'}, {4,1,2,'D'}, {4,3,4,'U'},
            {5,0,1,'D'}, {5,2,3,'U'}, {5,1,2,'D'}, {5,3,4,'U'}
        }
    },
    {
        4, "SKIPPING SPIDER", "String-Skipping Chromatic Spider (6-4-5-3)", 16, 4, 85,
        {
            {0,0,1,'D'}, {0,1,2,'U'}, {0,2,3,'D'}, {0,3,4,'U'},
            {2,0,1,'D'}, {2,1,2,'U'}, {2,2,3,'D'}, {2,3,4,'U'},
            {1,0,1,'D'}, {1,1,2,'U'}, {1,2,3,'D'}, {1,3,4,'U'},
            {3,0,1,'D'}, {3,1,2,'U'}, {3,2,3,'D'}, {3,3,4,'U'}
        }
    },
    {
        4, "DIAGONAL SPIDER", "Diagonal Chromatic Fretboard Shift", 16, 4, 90,
        {
            {0,0,1,'D'}, {1,1,2,'U'}, {2,2,3,'D'}, {3,3,4,'U'},
            {1,0,1,'D'}, {2,1,2,'U'}, {3,2,3,'D'}, {4,3,4,'U'},
            {2,0,1,'D'}, {3,1,2,'U'}, {4,2,3,'D'}, {5,3,4,'U'},
            {5,2,3,'D'}, {4,1,2,'U'}, {3,0,1,'D'}, {2,1,2,'U'}
        }
    },
    {
        4, "4-3-2-1 PINKY WORK", "Descending Pinky-First Finger Builder", 16, 4, 88,
        {
            {5,3,4,'D'}, {5,2,3,'U'}, {5,1,2,'D'}, {5,0,1,'U'},
            {4,3,4,'D'}, {4,2,3,'U'}, {4,1,2,'D'}, {4,0,1,'U'},
            {3,3,4,'D'}, {3,2,3,'U'}, {3,1,2,'D'}, {3,0,1,'U'},
            {2,3,4,'D'}, {2,2,3,'U'}, {2,1,2,'D'}, {2,0,1,'U'}
        }
    }
};

/* =========================================================================
 * Speed-Builder BPM Ladder Presets & Audio Mix Modes
 * ========================================================================= */
#define NUM_LADDERS 5
typedef struct {
    const char *label;
    int         bpm_step;
    int         bars_per_step;
    int         goal_offset;
} LadderPreset;

static const LadderPreset k_ladders[NUM_LADDERS] = {
    { "OFF (STATIC BPM)",   0, 0,  0 },
    { "+2 BPM / 2 BARS",    2, 2, 40 },
    { "+5 BPM / 4 BARS",    5, 4, 50 },
    { "+5 BPM / 8 BARS",    5, 8, 50 },
    { "+10 BPM / 4 BARS",  10, 4, 60 }
};

enum {
    MIX_TAB_AND_CLICK = 0,
    MIX_CLICK_ONLY    = 1,
    MIX_TAB_ONLY      = 2,
    MIX_COUNT         = 3
};

static const char *k_mix_names[MIX_COUNT] = {
    "TAB+CLK",
    "CLK ONLY",
    "TAB ONLY"
};

/* =========================================================================
 * Background Thread Karplus-Strong Guitar Synth + Metronome Audio Engine
 * ========================================================================= */
#define STEP_NOTE_SAMPLES 24255 /* 0.55 sec per pre-synthesized step at 44.1 kHz */
#define CLICK_SAMPLES     2866  /* 65 ms crisp metronome click */

/* Pre-synthesized step waveforms populated by background worker thread */
static int16_t           g_step_pcm[MAX_STEPS][STEP_NOTE_SAMPLES];
static volatile uint32_t g_step_ready_gen[MAX_STEPS];

static int16_t           g_click_accent_pcm[CLICK_SAMPLES];
static int16_t           g_click_normal_pcm[CLICK_SAMPLES];

static pthread_t         g_synth_thread;
static pthread_mutex_t   g_synth_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t    g_synth_cond  = PTHREAD_COND_INITIALIZER;
static volatile uint32_t g_synth_gen   = 1;
static volatile bool     g_synth_quit  = false;

/* Active playback state shared between UI and audio callback */
#define MIN_BPM 40
#define MAX_BPM 240

static volatile int  g_cat_idx    = 0; /* 0..4 (Starts on PENTATONIC LICKS) */
static volatile int  g_lick_in_cat = 0;/* 0..4 */
static volatile int  g_key_idx    = 5; /* 5 = Key of A (5th fret) */
static volatile int  g_bpm        = 100;
static volatile int  g_base_bpm   = 100;
static volatile int  g_ladder_idx = 2; /* Default: +5 BPM / 4 BARS */
static volatile int  g_mix_mode   = MIX_TAB_AND_CLICK;
static volatile bool g_playing    = false;
static volatile bool g_show_help  = false;

/* Live playback voice state */
static volatile int      g_active_step_voice = -1;
static volatile uint32_t g_active_step_pos   = 0;
static volatile uint32_t g_active_voice_gen  = 0;

static volatile int      g_active_click_type = 0; /* 0=none, 1=normal, 2=accent */
static volatile uint32_t g_active_click_pos  = 0;

static void init_click_waveforms(void) {
    for (int i = 0; i < CLICK_SAMPLES; i++) {
        double progress = (double)i / (double)CLICK_SAMPLES;
        double env = (progress < 0.65) ? 1.0 : ((1.0 - progress) / 0.35);
        double freq_acc = (i < 220) ? (1760.0 * 1.30) : 1760.0;
        double freq_nrm = (i < 220) ? (1000.0 * 1.30) : 1000.0;

        double ph_a = (2.0 * 3.14159265358979323846 * freq_acc * i) / SAMPLE_RATE;
        double ph_n = (2.0 * 3.14159265358979323846 * freq_nrm * i) / SAMPLE_RATE;

        double raw_a = (sin(ph_a) + 0.55 * sin(ph_a * 2.0)) * 2.4 * env;
        double raw_n = (sin(ph_n) + 0.50 * sin(ph_n * 2.0)) * 2.2 * env;
        if (raw_a > 1.0) raw_a = 1.0; else if (raw_a < -1.0) raw_a = -1.0;
        if (raw_n > 1.0) raw_n = 1.0; else if (raw_n < -1.0) raw_n = -1.0;

        g_click_accent_pcm[i] = (int16_t)(raw_a * 21000.0);
        g_click_normal_pcm[i] = (int16_t)(raw_n * 17000.0);
    }
}

/* Fixed-point Karplus-Strong string synthesis executed ONLY on background worker thread */
static bool synth_karplus_strong_step(int16_t *dst, int midi_note, char pick_type, uint32_t req_gen) {
    if (midi_note < 36) midi_note = 36;
    if (midi_note > 88) midi_note = 88;

    double freq = 440.0 * pow(2.0, (midi_note - 69) / 12.0);
    int delay_len = (int)(SAMPLE_RATE / freq + 0.5);
    if (delay_len < 16) delay_len = 16;
    if (delay_len > 1200) delay_len = 1200;

    int32_t delay_line[1205];
    uint32_t rng = (uint32_t)(midi_note * 2654435761U + (uint32_t)pick_type * 1013904223U);

    /* Legato notes (Hammer-on 'H', Pull-off 'P', Slide '/') have a warmer, rounder attack */
    bool is_legato = (pick_type == 'H' || pick_type == 'P' || pick_type == '/');
    int32_t amp = is_legato ? 21000 : 26000;

    int32_t prev = 0;
    for (int i = 0; i < delay_len; i++) {
        rng = rng * 1664525U + 1013904223U;
        int32_t noise = (int32_t)((int16_t)(rng >> 16));
        noise = (noise * amp) >> 15;
        if (is_legato) {
            /* Two-pole low-pass excitation for smooth hammer-on/pull-off timbre */
            prev = (prev * 3 + noise) >> 2;
            delay_line[i] = prev;
        } else {
            /* Crisp pick transient with warm body */
            prev = (prev + noise) >> 1;
            delay_line[i] = prev;
        }
    }

    int idx = 0;
    int32_t damping_num = is_legato ? 254 : 253; /* Out of 256 */

    for (int s = 0; s < STEP_NOTE_SAMPLES; s++) {
        if ((s & 1023) == 0 && g_synth_gen != req_gen) {
            return false; /* Preempted by newer selection! */
        }
        int next_idx = idx + 1;
        if (next_idx >= delay_len) next_idx = 0;

        int32_t cur = delay_line[idx];
        int32_t nxt = delay_line[next_idx];
        int32_t avg = ((cur + nxt) * damping_num) >> 9;
        delay_line[idx] = avg;
        idx = next_idx;

        /* Gentle tail fade over the last 15% of buffer */
        int32_t out = cur;
        if (s > (STEP_NOTE_SAMPLES * 85) / 100) {
            int rem = STEP_NOTE_SAMPLES - s;
            int tail = (STEP_NOTE_SAMPLES * 15) / 100;
            out = (out * rem) / tail;
        }
        if (out > 32767)  out = 32767;
        if (out < -32767) out = -32767;
        dst[s] = (int16_t)out;
    }
    return true;
}

static void *synth_worker_thread_fn(void *arg) {
    (void)arg;
    uint32_t done_gen = 0;

    while (!g_synth_quit) {
        pthread_mutex_lock(&g_synth_mutex);
        while (!g_synth_quit && g_synth_gen == done_gen) {
            pthread_cond_wait(&g_synth_cond, &g_synth_mutex);
        }
        if (g_synth_quit) {
            pthread_mutex_unlock(&g_synth_mutex);
            break;
        }
        uint32_t req_gen = g_synth_gen;
        int lick_idx = g_cat_idx * LICKS_PER_CAT + g_lick_in_cat;
        int key_idx  = g_key_idx;
        pthread_mutex_unlock(&g_synth_mutex);

        const LickDef *lick = &k_licks[lick_idx];
        int anchor_fret = k_keys[key_idx].anchor_fret;

        bool completed = true;
        for (int step = 0; step < lick->num_steps; step++) {
            if (g_synth_gen != req_gen || g_synth_quit) {
                completed = false;
                break;
            }
            const LickStep *st = &lick->steps[step];
            if (st->str >= 0 && st->str < 6) {
                int fret = anchor_fret + st->fret_off;
                int midi = k_open_midi[st->str] + fret;
                if (!synth_karplus_strong_step(g_step_pcm[step], midi, st->pick, req_gen)) {
                    completed = false;
                    break;
                }
            } else {
                memset(g_step_pcm[step], 0, sizeof(g_step_pcm[step]));
            }
            g_step_ready_gen[step] = req_gen;
        }

        if (completed && g_synth_gen == req_gen) {
            done_gen = req_gen;
        }
    }
    return NULL;
}

static void request_lick_resynth(void) {
    pthread_mutex_lock(&g_synth_mutex);
    g_active_step_voice = -1; /* Immediately silence old chord/lick on change */
    g_synth_gen++;
    pthread_cond_signal(&g_synth_cond);
    pthread_mutex_unlock(&g_synth_mutex);
}

static void trigger_step_audio(int step_idx, bool is_beat, bool is_accent_beat) {
    if (g_mix_mode != MIX_CLICK_ONLY && step_idx >= 0 && step_idx < MAX_STEPS) {
        g_active_step_pos   = 0;
        g_active_voice_gen  = g_synth_gen;
        g_active_step_voice = step_idx;
    }
    if (g_mix_mode != MIX_TAB_ONLY && is_beat) {
        g_active_click_pos  = 0;
        g_active_click_type = is_accent_beat ? 2 : 1;
    }
}

static void trigger_countin_click(bool is_accent_beat) {
    if (g_mix_mode != MIX_TAB_ONLY) {
        g_active_click_pos  = 0;
        g_active_click_type = is_accent_beat ? 2 : 1;
    }
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *samples = (int16_t *)stream;
    int frames = len / (2 * AUDIO_CHANNELS);

    for (int i = 0; i < frames; i++) {
        int32_t mix = 0;

        int v = g_active_step_voice;
        uint32_t vgen = g_active_voice_gen;
        if (v >= 0 && v < MAX_STEPS && vgen == g_synth_gen && g_step_ready_gen[v] == vgen) {
            uint32_t pos = g_active_step_pos;
            if (pos < STEP_NOTE_SAMPLES) {
                mix += g_step_pcm[v][pos];
                g_active_step_pos = pos + 1;
            } else {
                g_active_step_voice = -1;
            }
        }

        int ctype = g_active_click_type;
        if (ctype > 0) {
            uint32_t cpos = g_active_click_pos;
            if (cpos < CLICK_SAMPLES) {
                mix += (ctype == 2) ? g_click_accent_pcm[cpos] : g_click_normal_pcm[cpos];
                g_active_click_pos = cpos + 1;
            } else {
                g_active_click_type = 0;
            }
        }

        if (mix > 32767)  mix = 32767;
        if (mix < -32767) mix = -32767;
        int16_t out = (int16_t)mix;
        for (int c = 0; c < AUDIO_CHANNELS; c++) {
            samples[i * AUDIO_CHANNELS + c] = out;
        }
    }
}

static void maximize_hardware_volume(void) {
#if !defined(HOST_X11)
    int fd = open("/dev/mixer", O_RDWR);
    if (fd >= 0) {
        int vol = (100 << 8) | 100;
        ioctl(fd, SOUND_MIXER_WRITE_VOLUME, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_PCM, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_SPEAKER, &vol);
        close(fd);
    }
#endif
}

/* =========================================================================
 * Persistent Configuration
 * ========================================================================= */
static char g_config_path[512];

static void resolve_config_path(void) {
    struct stat st;
    if (stat("/mnt", &st) == 0 && S_ISDIR(st.st_mode) && access("/mnt", W_OK) == 0) {
        snprintf(g_config_path, sizeof(g_config_path), "/mnt/.licktrainer.cfg");
    } else {
        const char *home = getenv("HOME");
        if (home && access(home, W_OK) == 0) {
            snprintf(g_config_path, sizeof(g_config_path), "%s/.licktrainer.cfg", home);
        } else {
            snprintf(g_config_path, sizeof(g_config_path), "./.licktrainer.cfg");
        }
    }
}

static void load_config(void) {
    FILE *f = fopen(g_config_path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int val = 0;
        if (sscanf(line, "cat_idx=%d", &val) == 1 && val >= 0 && val < NUM_CATEGORIES) {
            g_cat_idx = val;
        } else if (sscanf(line, "lick_in_cat=%d", &val) == 1 && val >= 0 && val < LICKS_PER_CAT) {
            g_lick_in_cat = val;
        } else if (sscanf(line, "key_idx=%d", &val) == 1 && val >= 0 && val < NUM_KEYS) {
            g_key_idx = val;
        } else if (sscanf(line, "bpm=%d", &val) == 1 && val >= MIN_BPM && val <= MAX_BPM) {
            g_bpm = g_base_bpm = val;
        } else if (sscanf(line, "ladder_idx=%d", &val) == 1 && val >= 0 && val < NUM_LADDERS) {
            g_ladder_idx = val;
        } else if (sscanf(line, "mix_mode=%d", &val) == 1 && val >= 0 && val < MIX_COUNT) {
            g_mix_mode = val;
        }
    }
    fclose(f);
}

static void save_config(void) {
    FILE *f = fopen(g_config_path, "w");
    if (!f) return;
    fprintf(f, "cat_idx=%d\n", g_cat_idx);
    fprintf(f, "lick_in_cat=%d\n", g_lick_in_cat);
    fprintf(f, "key_idx=%d\n", g_key_idx);
    fprintf(f, "bpm=%d\n", g_base_bpm);
    fprintf(f, "ladder_idx=%d\n", g_ladder_idx);
    fprintf(f, "mix_mode=%d\n", g_mix_mode);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

/* =========================================================================
 * UI Rendering (320x240)
 * ========================================================================= */
static void render_help_overlay(SDL_Surface *screen) {
    fill_rect_alpha(screen, 0, 0, SCREEN_W, SCREEN_H, COL_BG, 225);
    fill_rect(screen, 12, 12, 296, 216, COL_PANEL);
    draw_border(screen, 12, 12, 296, 216, 2, COL_GOLD);

    fill_rect(screen, 14, 14, 292, 22, COL_HEADER);
    draw_text_centered(screen, FONT_UI_12, 160, 18, "LICK TRAINER - CONTROLS & GUIDE", COL_GOLD);

    const struct {
        const char *btn;
        const char *desc;
        uint16_t    col;
    } rows[] = {
        { "[A] Button",   "Play / Pause lick loop (4-beat count-in)",  COL_GREEN  },
        { "[L1 / R1]",    "Switch Category (Pentatonic, Legato...)",   COL_CYAN   },
        { "[LEFT/RIGHT]", "Previous / Next Lick (#01 .. #25)",         COL_WHITE  },
        { "[UP / DOWN]",  "Adjust Tempo (BPM) & base ladder speed",    COL_GOLD   },
        { "[SELECT]",     "Cycle Speed Ladder (+2/+5/+10 BPM step)",   COL_ORANGE },
        { "[B] Button",   "Cycle Audio Mix (Tab+Click / Click / Tab)", COL_CYAN   },
        { "[X] Button",   "Transpose Key / Neck Fret (E .. Eb)",       COL_PURPLE },
        { "[Y] / [MENU]", "Close Help [Y]  |  Save & Exit [MENU]",     COL_ACCENT }
    };

    for (int i = 0; i < 8; i++) {
        int ry = 42 + i * 20;
        fill_rect(screen, 18, ry, 76, 16, COL_PANEL_ALT);
        draw_border(screen, 18, ry, 76, 16, 1, rows[i].col);
        draw_text_centered(screen, FONT_DOT_8, 56, ry + 4, rows[i].btn, rows[i].col);
        draw_text(screen, FONT_DOT_8, 100, ry + 4, rows[i].desc, COL_WHITE);
    }

    fill_rect(screen, 18, 204, 284, 18, COL_HEADER);
    draw_text_centered(screen, FONT_DOT_8, 160, 209,
                       "PICK: D=Down U=Up H=Hammer P=Pull /=Slide | FINGER: 1-4",
                       COL_GOLD);
}

static void render_ui(
    SDL_Surface *screen,
    int cur_step,
    int cur_beat,
    int cur_bar_in_stage,
    bool in_countin
) {
    int lick_global_idx = g_cat_idx * LICKS_PER_CAT + g_lick_in_cat;
    const LickDef *lick = &k_licks[lick_global_idx];
    const KeyAnchor *key = &k_keys[g_key_idx];
    const LadderPreset *ladder = &k_ladders[g_ladder_idx];

    fill_rect(screen, 0, 0, SCREEN_W, SCREEN_H, COL_BG);

    /* ================= 1. TOP HEADER BAR (y: 0..24) ================= */
    fill_rect(screen, 0, 0, SCREEN_W, 24, COL_HEADER);
    fill_rect(screen, 0, 23, SCREEN_W, 1, COL_BORDER);

    /* Category Badge */
    uint16_t cat_cols[NUM_CATEGORIES] = { COL_CYAN, COL_ORANGE, COL_GOLD, COL_PURPLE, COL_GREEN };
    uint16_t cat_col = cat_cols[g_cat_idx];
    fill_rect(screen, 4, 3, 68, 17, cat_col);
    draw_text_centered(screen, FONT_UI_9, 38, 6, k_category_names[g_cat_idx], COL_BG);

    /* Exercise # and Title */
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "#%02d %s", lick_global_idx + 1, lick->title);
    draw_text(screen, FONT_UI_10, 77, 6, title_buf, COL_WHITE);

    /* Transposed Key & Fret Badge */
    char key_buf[24];
    snprintf(key_buf, sizeof(key_buf), "%s (%dfr)", key->name, key->anchor_fret);
    fill_rect(screen, 230, 3, 50, 17, COL_PANEL_ALT);
    draw_border(screen, 230, 3, 50, 17, 1, COL_GOLD);
    draw_text_centered(screen, FONT_UI_9, 255, 6, key_buf, COL_GOLD);

    /* Y:Help Badge */
    fill_rect(screen, 283, 3, 33, 17, COL_PANEL_ALT);
    draw_border(screen, 283, 3, 33, 17, 1, COL_BORDER);
    draw_text_centered(screen, FONT_DOT_8, 299, 7, "Y:Help", COL_MUTED);

    /* ================= 2. TEMPO, LADDER & BEAT STRIP (y: 26..62) ================= */
    /* Left Card: Tempo & Subdivision */
    fill_rect(screen, 4, 26, 92, 36, COL_PANEL);
    draw_border(screen, 4, 26, 92, 36, 1, COL_BORDER);
    draw_text(screen, FONT_DOT_8, 9, 29, "TEMPO", COL_MUTED);
    draw_text(screen, FONT_DOT_8, 52, 29, (lick->steps_per_beat == 3) ? "TRIPLET" : "16TH", COL_CYAN);

    char bpm_buf[24];
    snprintf(bpm_buf, sizeof(bpm_buf), "%d BPM", g_bpm);
    draw_text_centered(screen, FONT_UI_12, 50, 41, bpm_buf, COL_GOLD);

    /* Center Card: Speed-Builder Ladder */
    fill_rect(screen, 99, 26, 146, 36, COL_PANEL);
    draw_border(screen, 99, 26, 146, 36, 1, (g_ladder_idx > 0) ? COL_ORANGE : COL_BORDER);

    char lad_hdr[40];
    snprintf(lad_hdr, sizeof(lad_hdr), "LADDER: %s", ladder->label);
    draw_text_centered(screen, FONT_DOT_8, 172, 29, lad_hdr,
                       (g_ladder_idx > 0) ? COL_ORANGE : COL_MUTED);

    int goal_bpm = g_base_bpm + ladder->goal_offset;
    if (goal_bpm > MAX_BPM) goal_bpm = MAX_BPM;

    char lad_sub[48];
    if (in_countin && g_playing) {
        snprintf(lad_sub, sizeof(lad_sub), "COUNT-IN: BEAT %d OF 4", cur_beat + 1);
    } else if (g_ladder_idx > 0) {
        snprintf(lad_sub, sizeof(lad_sub), "BAR %d/%d  |  %d->%d BPM",
                 cur_bar_in_stage + 1, ladder->bars_per_step, g_base_bpm, goal_bpm);
    } else {
        snprintf(lad_sub, sizeof(lad_sub), "%s", lick->subtitle);
        if (text_width(FONT_DOT_8, lad_sub) > 138) {
            snprintf(lad_sub, sizeof(lad_sub), "STATIC TEMPO ([SEL] LADDER)");
        }
    }
    draw_text_centered(screen, FONT_DOT_8, 172, 41, lad_sub,
                       in_countin ? COL_ACCENT : COL_WHITE);

    /* Ladder Progress Bar */
    int pb_x = 105, pb_y = 53, pb_w = 134, pb_h = 5;
    fill_rect(screen, pb_x, pb_y, pb_w, pb_h, COL_BG);
    if (in_countin && g_playing) {
        int fill_w = ((cur_beat + 1) * pb_w) / 4;
        fill_rect(screen, pb_x, pb_y, fill_w, pb_h, COL_ACCENT);
    } else if (g_ladder_idx > 0 && ladder->bars_per_step > 0) {
        int total_steps_in_stage = ladder->bars_per_step * lick->num_steps;
        int done_steps = cur_bar_in_stage * lick->num_steps + (g_playing ? (cur_step + 1) : 0);
        if (done_steps > total_steps_in_stage) done_steps = total_steps_in_stage;
        int fill_w = (done_steps * pb_w) / total_steps_in_stage;
        fill_rect(screen, pb_x, pb_y, fill_w, pb_h, COL_GREEN);
    }

    /* Right Card: 4 Beat LEDs & Mix Badge */
    fill_rect(screen, 248, 26, 68, 36, COL_PANEL);
    draw_border(screen, 248, 26, 68, 36, 1, COL_BORDER);
    draw_text_centered(screen, FONT_DOT_8, 282, 29, k_mix_names[g_mix_mode], COL_GREEN);

    for (int b = 0; b < 4; b++) {
        int bx = 252 + b * 15;
        int by = 40;
        bool active_b = g_playing && (b == cur_beat);
        uint16_t b_fill = active_b ? ((b == 0) ? COL_ACCENT : COL_GREEN) : COL_PANEL_ALT;
        uint16_t b_bdr  = (b == 0) ? COL_ACCENT : COL_BORDER;
        fill_rect(screen, bx, by, 13, 17, b_fill);
        draw_border(screen, bx, by, 13, 17, 1, active_b ? COL_WHITE : b_bdr);
        char b_str[4] = { (char)('1' + b), '\0' };
        draw_text_centered(screen, FONT_UI_9, bx + 6, by + 3, b_str,
                           active_b ? COL_BG : ((b == 0) ? COL_ACCENT : COL_WHITE));
    }

    /* ================= 3. 6-STRING GUITAR TAB PANEL (y: 65..157) ================= */
    fill_rect(screen, 4, 65, 312, 92, COL_PANEL);
    draw_border(screen, 4, 65, 312, 92, 1, COL_BORDER);

    int nsteps = lick->num_steps;
    int col_w  = (nsteps == 12) ? 23 : 17;
    int grid_w = nsteps * col_w;
    int grid_x0 = 26 + (286 - grid_w) / 2;

    /* Left Labels: PK (Pick), Strings e B G D A E, FG (Finger) */
    draw_text(screen, FONT_DOT_8, 8, 68, "PK", COL_CYAN);
    for (int row = 0; row < 6; row++) {
        int str_idx = 5 - row; /* Top row = string 5 ('e'), bottom row = string 0 ('E') */
        int sy = 83 + row * 10;
        char slbl[2] = { k_str_labels[str_idx], '\0' };
        draw_text(screen, FONT_DOT_8, 11, sy - 4, slbl, (str_idx == 0 || str_idx == 5) ? COL_GOLD : COL_MUTED);
        /* Horizontal tab staff line */
        fill_rect(screen, grid_x0 - 2, sy, grid_w + 4, 1, COL_DIM);
    }
    draw_text(screen, FONT_DOT_8, 8, 144, "FG", COL_GOLD);

    /* Beat divider lines & Active Step Playhead Column */
    for (int s = 0; s < nsteps; s++) {
        int sx = grid_x0 + s * col_w;
        int scx = sx + col_w / 2;

        if (s % lick->steps_per_beat == 0) {
            fill_rect(screen, sx, 67, 1, 88, COL_BORDER);
        }

        bool is_cur = (!in_countin) && (s == cur_step);
        if (is_cur) {
            fill_rect_alpha(screen, sx + 1, 67, col_w - 1, 88,
                            g_playing ? COL_ACCENT : COL_GOLD, 55);
            draw_border(screen, sx, 66, col_w + 1, 90, 1,
                        g_playing ? COL_ACCENT : COL_GOLD);
        }

        const LickStep *st = &lick->steps[s];

        /* 1. Picking / Articulation symbol above tab ('D', 'U', 'H', 'P', '/') */
        char p_str[2] = { st->pick, '\0' };
        uint16_t p_col = COL_CYAN;
        if (st->pick == 'U') p_col = COL_GREEN;
        else if (st->pick == 'H' || st->pick == 'P' || st->pick == '/') p_col = COL_ORANGE;
        draw_text_centered(screen, FONT_DOT_8, scx, 68, p_str, is_cur ? COL_WHITE : p_col);

        /* 2. Fret number on the active guitar string */
        if (st->str >= 0 && st->str < 6) {
            int row = 5 - st->str;
            int sy = 83 + row * 10;
            int actual_fret = key->anchor_fret + st->fret_off;

            char f_str[16];
            snprintf(f_str, sizeof(f_str), "%d", actual_fret);
            int fw = text_width(FONT_UI_9, f_str) + 4;
            if (fw < 11) fw = 11;

            uint16_t pill_bg = is_cur ? (g_playing ? COL_ACCENT : COL_GOLD) : COL_PANEL;
            uint16_t pill_fg = is_cur ? COL_BG : COL_WHITE;
            fill_rect(screen, scx - fw / 2, sy - 5, fw, 10, pill_bg);
            draw_text_centered(screen, FONT_UI_9, scx, sy - 5, f_str, pill_fg);
        }

        /* 3. Left-hand finger number (1..4) below tab */
        if (st->finger >= 1 && st->finger <= 4) {
            char fg_str[4] = { (char)('0' + st->finger), '\0' };
            draw_text_centered(screen, FONT_DOT_8, scx, 144, fg_str,
                               is_cur ? COL_GOLD : COL_MUTED);
        }
    }

    /* ================= 4. LIVE 6-STRING FRETBOARD WINDOW (y: 160..215) ================= */
    fill_rect(screen, 4, 160, 312, 55, COL_PANEL);
    draw_border(screen, 4, 160, 312, 55, 1, COL_BORDER);

    /* Zoomed 8-fret window around the key's anchor fret */
    int min_off = 99, max_off = 0;
    for (int s = 0; s < nsteps; s++) {
        if (lick->steps[s].str >= 0) {
            if (lick->steps[s].fret_off < min_off) min_off = lick->steps[s].fret_off;
            if (lick->steps[s].fret_off > max_off) max_off = lick->steps[s].fret_off;
        }
    }
    int start_fret = key->anchor_fret + min_off - 1;
    if (start_fret < 0) start_fret = 0;
    int num_frets = 8;

    int neck_x0 = 24;
    int neck_w  = 216;
    int fret_w  = neck_w / num_frets; /* 27px per fret */
    int neck_y0 = 171;
    int neck_h  = 40;

    fill_rect(screen, neck_x0, neck_y0, fret_w * num_frets, neck_h, COL_NECK_WOOD);

    /* Fret numbers & Fretwires */
    for (int f = 0; f <= num_frets; f++) {
        int fx = neck_x0 + f * fret_w;
        int fret_num = start_fret + f;
        bool is_nut = (fret_num == 0 && f == 0);
        fill_rect(screen, fx, neck_y0, is_nut ? 3 : 1, neck_h, is_nut ? COL_GOLD : COL_FRET_WIRE);

        if (f < num_frets) {
            int fcx = fx + fret_w / 2;
            char fn_str[16];
            snprintf(fn_str, sizeof(fn_str), "%d", fret_num);
            draw_text_centered(screen, FONT_DOT_8, fcx, 162, fn_str,
                               (fret_num == key->anchor_fret) ? COL_GOLD : COL_MUTED);

            /* Standard fret inlay dots */
            if (fret_num == 3 || fret_num == 5 || fret_num == 7 ||
                fret_num == 9 || fret_num == 15 || fret_num == 17 || fret_num == 19) {
                fill_circle(screen, fcx, neck_y0 + neck_h / 2, 2, COL_DIM);
            } else if (fret_num == 12) {
                fill_circle(screen, fcx, neck_y0 + 10, 2, COL_DIM);
                fill_circle(screen, fcx, neck_y0 + neck_h - 10, 2, COL_DIM);
            }
        }
    }

    /* 6 Horizontal Guitar Strings on the Neck */
    for (int row = 0; row < 6; row++) {
        int str_idx = 5 - row;
        int sy = neck_y0 + 4 + row * 6;
        uint16_t scol = (str_idx <= 2) ? rgb565(175, 160, 140) : rgb565(205, 212, 222);
        fill_rect(screen, neck_x0, sy, fret_w * num_frets, (str_idx == 0) ? 2 : 1, scol);
    }

    /* Draw Ghosted Lick Shape Dots on Neck */
    for (int s = 0; s < nsteps; s++) {
        const LickStep *st = &lick->steps[s];
        if (st->str < 0) continue;
        int actual_fret = key->anchor_fret + st->fret_off;
        int rel_f = actual_fret - start_fret;
        if (rel_f >= 0 && rel_f < num_frets) {
            int fcx = neck_x0 + rel_f * fret_w + fret_w / 2;
            int sy  = neck_y0 + 4 + (5 - st->str) * 6;
            fill_circle(screen, fcx, sy, 3, COL_PANEL_ALT);
            draw_circle_outline(screen, fcx, sy, 3, COL_CYAN);
        }
    }

    /* Highlight Active Step Note on Neck + Right Info Card */
    const LickStep *active_st = &lick->steps[cur_step];
    if (active_st->str >= 0) {
        int actual_fret = key->anchor_fret + active_st->fret_off;
        int rel_f = actual_fret - start_fret;
        int midi  = k_open_midi[active_st->str] + actual_fret;
        const char *note_name = k_note_names[midi % 12];

        if (rel_f >= 0 && rel_f < num_frets) {
            int fcx = neck_x0 + rel_f * fret_w + fret_w / 2;
            int sy  = neck_y0 + 4 + (5 - active_st->str) * 6;
            fill_circle(screen, fcx, sy, 5, COL_ACCENT);
            draw_circle_outline(screen, fcx, sy, 6, COL_WHITE);
            char fg_ch[2] = { (char)('0' + active_st->finger), '\0' };
            draw_text_centered(screen, FONT_DOT_8, fcx, sy - 4, fg_ch, COL_WHITE);
        }

        /* Right Live Note Readout (x: 244..312) */
        fill_rect(screen, 244, 164, 68, 47, COL_PANEL_ALT);
        draw_border(screen, 244, 164, 68, 47, 1, COL_GOLD);
        draw_text_centered(screen, FONT_DOT_8, 278, 167, "ACTIVE NOTE", COL_MUTED);

        char note_info[24];
        snprintf(note_info, sizeof(note_info), "%s (Fret %d)", note_name, actual_fret);
        draw_text_centered(screen, FONT_UI_10, 278, 179, note_info, COL_GOLD);

        const char *finger_names[5] = { "", "1:Index", "2:Middle", "3:Ring", "4:Pinky" };
        draw_text_centered(screen, FONT_DOT_8, 278, 196,
                           finger_names[(active_st->finger >= 1 && active_st->finger <= 4) ? active_st->finger : 1],
                           COL_CYAN);
    }

    /* ================= 5. BOTTOM CONTROLS FOOTER (y: 217..240) ================= */
    fill_rect(screen, 0, 217, SCREEN_W, 23, COL_HEADER);
    fill_rect(screen, 0, 217, SCREEN_W, 1, COL_BORDER);

    uint16_t play_col = g_playing ? COL_ORANGE : COL_GREEN;
    fill_rect(screen, 4, 221, 56, 15, play_col);
    draw_text_centered(screen, FONT_DOT_8, 32, 225, g_playing ? "[A] PAUSE" : "[A] PLAY", COL_BG);

    draw_text(screen, FONT_DOT_8, 65, 225,
              "[L1/R1]Cat [L/R]Lick [SEL]Ladder [B]Mix [X]Key",
              COL_WHITE);

    if (g_show_help) {
        render_help_overlay(screen);
    }
}

/* =========================================================================
 * Main Entry Point & Event Loop
 * ========================================================================= */
int main(int argc, char *argv[]) {
    putenv((char *)"SDL_NOMOUSE=1");
    resolve_config_path();
    load_config();

    /* Default BPM to the initial lick's recommended tempo if no config loaded */
    bool cli_bpm_set = false;
    bool cli_auto_play = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cat") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v >= 0 && v < NUM_CATEGORIES) g_cat_idx = v;
        } else if (strcmp(argv[i], "--lick") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v >= 0 && v < LICKS_PER_CAT) g_lick_in_cat = v;
        } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v >= 0 && v < NUM_KEYS) g_key_idx = v;
        } else if (strcmp(argv[i], "--bpm") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v >= MIN_BPM && v <= MAX_BPM) {
                g_bpm = g_base_bpm = v;
                cli_bpm_set = true;
            }
        } else if (strcmp(argv[i], "--ladder") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v >= 0 && v < NUM_LADDERS) g_ladder_idx = v;
        } else if (strcmp(argv[i], "--play") == 0) {
            cli_auto_play = true;
        } else if (strcmp(argv[i], "--help-overlay") == 0) {
            g_show_help = true;
        }
    }

    if (!cli_bpm_set) {
        g_bpm = g_base_bpm = k_licks[g_cat_idx * LICKS_PER_CAT + g_lick_in_cat].default_bpm;
    }

    init_click_waveforms();

    /* Start background Karplus-Strong synthesis worker thread */
    pthread_create(&g_synth_thread, NULL, synth_worker_thread_fn, NULL);
    request_lick_resynth();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
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
    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, SCREEN_BPP, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_WM_SetCaption("V90 Lick Trainer", NULL);

    int cur_step = 0;
    int cur_beat = 0;
    int cur_bar_in_stage = 0;
    bool in_countin = false;
    double step_accum_ms = 0.0;
    Uint32 last_tick_ms = SDL_GetTicks();

    if (cli_auto_play) {
        g_playing = true;
        in_countin = false;
        cur_step = 5;
        cur_beat = 1;
        cur_bar_in_stage = 1;
    }

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

                /* Y Button: Toggle Help & Practice Guide Overlay */
                if (k == V90_BTN_Y
#if defined(HOST_X11)
                    || k == SDLK_y || k == SDLK_h
#endif
                ) {
                    g_show_help = !g_show_help;
                }
                /* MENU Button (or Q on host): Save & Exit cleanly */
                else if (k == V90_BTN_MENU
#if defined(HOST_X11)
                         || k == SDLK_q
#endif
                ) {
                    running = false;
                } else if (g_show_help) {
                    if (k == V90_BTN_B || k == V90_BTN_A || k == V90_BTN_SELECT) {
                        g_show_help = false;
                    }
                }
                /* A Button: Play / Pause exercise loop */
                else if (k == V90_BTN_A
#if defined(HOST_X11)
                         || k == SDLK_a
#endif
                ) {
                    g_playing = !g_playing;
                    if (g_playing) {
                        g_bpm = g_base_bpm;
                        cur_step = 0;
                        cur_beat = 0;
                        cur_bar_in_stage = 0;
                        in_countin = true; /* 4-beat count-in before Measure 1 */
                        step_accum_ms = 0.0;
                        last_tick_ms = now_ms;
                        trigger_countin_click(true);
                    } else {
                        g_active_step_voice = -1;
                    }
                }
                /* B Button: Cycle Audio Mix Mode (TAB+CLK / CLK ONLY / TAB ONLY) */
                else if (k == V90_BTN_B
#if defined(HOST_X11)
                         || k == SDLK_b
#endif
                ) {
                    g_mix_mode = (g_mix_mode + 1) % MIX_COUNT;
                    if (g_mix_mode == MIX_CLICK_ONLY) g_active_step_voice = -1;
                    save_config();
                }
                /* SELECT Button (or C on host): Cycle Speed-Builder Ladder Preset (Practice Mode) */
                else if (k == V90_BTN_SELECT
#if defined(HOST_X11)
                         || k == SDLK_c
#endif
                ) {
                    g_ladder_idx = (g_ladder_idx + 1) % NUM_LADDERS;
                    cur_bar_in_stage = 0;
                    g_bpm = g_base_bpm;
                    save_config();
                }
                /* X Button: Transpose Key / Neck Position */
                else if (k == V90_BTN_X
#if defined(HOST_X11)
                         || k == SDLK_x
#endif
                ) {
                    g_key_idx = (g_key_idx + 1) % NUM_KEYS;
                    request_lick_resynth();
                    save_config();
                }
                /* L1 / R1: Switch Exercise Category */
                else if (k == V90_BTN_L1 || k == V90_BTN_R2
#if defined(HOST_X11)
                         || k == SDLK_LEFTBRACKET
#endif
                ) {
                    g_cat_idx = (g_cat_idx + NUM_CATEGORIES - 1) % NUM_CATEGORIES;
                    cur_step = 0;
                    cur_bar_in_stage = 0;
                    g_bpm = g_base_bpm = k_licks[g_cat_idx * LICKS_PER_CAT + g_lick_in_cat].default_bpm;
                    request_lick_resynth();
                    save_config();
                } else if (k == V90_BTN_R1 || k == V90_BTN_L2
#if defined(HOST_X11)
                           || k == SDLK_RIGHTBRACKET
#endif
                ) {
                    g_cat_idx = (g_cat_idx + 1) % NUM_CATEGORIES;
                    cur_step = 0;
                    cur_bar_in_stage = 0;
                    g_bpm = g_base_bpm = k_licks[g_cat_idx * LICKS_PER_CAT + g_lick_in_cat].default_bpm;
                    request_lick_resynth();
                    save_config();
                }
                /* LEFT / RIGHT: Previous / Next Lick within Category */
                else if (k == V90_BTN_LEFT) {
                    g_lick_in_cat = (g_lick_in_cat + LICKS_PER_CAT - 1) % LICKS_PER_CAT;
                    cur_step = 0;
                    cur_bar_in_stage = 0;
                    g_bpm = g_base_bpm = k_licks[g_cat_idx * LICKS_PER_CAT + g_lick_in_cat].default_bpm;
                    request_lick_resynth();
                    save_config();
                } else if (k == V90_BTN_RIGHT) {
                    g_lick_in_cat = (g_lick_in_cat + 1) % LICKS_PER_CAT;
                    cur_step = 0;
                    cur_bar_in_stage = 0;
                    g_bpm = g_base_bpm = k_licks[g_cat_idx * LICKS_PER_CAT + g_lick_in_cat].default_bpm;
                    request_lick_resynth();
                    save_config();
                }
                /* UP / DOWN: Tempo (BPM) control */
                else if (k == V90_BTN_UP) {
                    if (g_base_bpm < MAX_BPM) {
                        g_base_bpm++;
                        g_bpm = g_base_bpm;
                    }
                    up_held = true;
                    hold_start_ms = now_ms;
                    last_repeat_ms = now_ms;
                    save_config();
                } else if (k == V90_BTN_DOWN) {
                    if (g_base_bpm > MIN_BPM) {
                        g_base_bpm--;
                        g_bpm = g_base_bpm;
                    }
                    down_held = true;
                    hold_start_ms = now_ms;
                    last_repeat_ms = now_ms;
                    save_config();
                }
            } else if (ev.type == SDL_KEYUP) {
                SDLKey k = ev.key.keysym.sym;
                if (k == V90_BTN_UP)   up_held = false;
                if (k == V90_BTN_DOWN) down_held = false;
            }
        }

        /* Smooth key repeat when holding UP or DOWN */
        if ((up_held || down_held) && (now_ms >= hold_start_ms + 350)) {
            if (now_ms >= last_repeat_ms + 60) {
                if (up_held && g_base_bpm < MAX_BPM) {
                    g_base_bpm++;
                    g_bpm = g_base_bpm;
                }
                if (down_held && g_base_bpm > MIN_BPM) {
                    g_base_bpm--;
                    g_bpm = g_base_bpm;
                }
                last_repeat_ms = now_ms;
            }
        }

        /* Advance Sequencer Clock */
        now_ms = SDL_GetTicks();
        const LickDef *lick = &k_licks[g_cat_idx * LICKS_PER_CAT + g_lick_in_cat];
        const LadderPreset *ladder = &k_ladders[g_ladder_idx];
        double beat_ms = 60000.0 / (double)g_bpm;
        double step_ms = beat_ms / (double)lick->steps_per_beat;

        if (g_playing) {
            Uint32 dt = (now_ms >= last_tick_ms) ? (now_ms - last_tick_ms) : 0;
            if (dt > 2000) dt = 0;
            last_tick_ms = now_ms;
            step_accum_ms += (double)dt;

            if (in_countin) {
                while (step_accum_ms >= beat_ms) {
                    step_accum_ms -= beat_ms;
                    cur_beat++;
                    if (cur_beat >= 4) {
                        /* Count-in finished: launch Step 0 of Measure 1! */
                        in_countin = false;
                        cur_beat = 0;
                        cur_step = 0;
                        cur_bar_in_stage = 0;
                        trigger_step_audio(0, true, true);
                        break;
                    } else {
                        trigger_countin_click(false);
                    }
                }
            } else {
                while (step_accum_ms >= step_ms) {
                    step_accum_ms -= step_ms;
                    cur_step++;
                    if (cur_step >= lick->num_steps) {
                        cur_step = 0;
                        cur_bar_in_stage++;
                        /* Check if Speed-Builder Ladder should step up BPM */
                        if (g_ladder_idx > 0 && ladder->bars_per_step > 0 &&
                            cur_bar_in_stage >= ladder->bars_per_step) {
                            cur_bar_in_stage = 0;
                            int goal_bpm = g_base_bpm + ladder->goal_offset;
                            if (goal_bpm > MAX_BPM) goal_bpm = MAX_BPM;
                            if (g_bpm < goal_bpm) {
                                g_bpm += ladder->bpm_step;
                                if (g_bpm > goal_bpm) g_bpm = goal_bpm;
                            }
                        }
                    }
                    cur_beat = cur_step / lick->steps_per_beat;
                    bool is_beat = (cur_step % lick->steps_per_beat) == 0;
                    bool is_accent = (cur_step == 0);
                    trigger_step_audio(cur_step, is_beat, is_accent);
                }
            }
        } else {
            last_tick_ms = now_ms;
        }

        if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
        render_ui(screen, cur_step, cur_beat, cur_bar_in_stage, in_countin);
        if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
        SDL_Flip(screen);
        SDL_Delay(16);
    }

    save_config();
    if (audio_ok) SDL_CloseAudio();

    pthread_mutex_lock(&g_synth_mutex);
    g_synth_quit = true;
    pthread_cond_signal(&g_synth_cond);
    pthread_mutex_unlock(&g_synth_mutex);
    pthread_join(g_synth_thread, NULL);

    SDL_Quit();
    return 0;
}
