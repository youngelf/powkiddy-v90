/*
 * sightread - Guitar Staff-to-Fretboard Sight-Reading Trainer
 *             for PowKiddy V90 (MiyooCFW 1.3.3) & Linux x86_64 Desktop (X11)
 *
 * Screen resolution: 320x240, 16-bit RGB565 (scaled 2x on Linux X11 desktop)
 * Layout           : Top 70% = Prominent Interactive Treble Clef Staff (8vb)
 *                    Bottom 30% = Non-Interactive Guitar Neck Position Reference
 * Audio Engine     : Background-thread fixed-point Karplus-Strong guitar string
 *                    synthesizer + metronome beat pulse (44.1 kHz stereo)
 *
 * Controls (PowKiddy V90 / Linux Desktop):
 *   A (LALT / A / Return)             : Play / Pause the entire musical piece
 *   B (LCTRL / B)                     : Play single highlighted note on staff
 *   LEFT / RIGHT                      : Move highlight cursor across staff notes
 *   UP / DOWN                         : Increase / Decrease Tempo (BPM)
 *   L1 / R1 (TAB / BACKSPACE, [ / ])  : Previous / Next Exercise (progressive complexity)
 *   SELECT (ESCAPE / C)               : Cycle Neck Position (Open, 2nd, 5th, 7th, 9th)
 *   X (LSHIFT / X)                    : Cycle Key Signature (C, G, D, A, E, F, Bb)
 *   Y (SPACE / Y)                     : Toggle Help & Sight-Reading Guide overlay
 *   MENU (RCTRL / Q)                  : Save settings & Quit cleanly
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

#include "sightread_font.h"

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
            pa = fn_new(NULL, "V90 SightRead", PA_STREAM_PLAYBACK, NULL, "Guitar Sight Reader", &ss, NULL, NULL, &err);
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

static const char *SDL_GetError(void) { return "Cannot open X11 DISPLAY"; }
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
        0, 0, win_w, win_h, 0,
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
#define COL_PANEL_ALT   rgb565(32, 41, 58)
#define COL_BORDER      rgb565(55, 68, 92)
#define COL_STAFF_LINE  rgb565(150, 168, 195)
#define COL_BAR_LINE    rgb565(95, 114, 145)
#define COL_WHITE       rgb565(242, 246, 250)
#define COL_MUTED       rgb565(140, 155, 175)
#define COL_DIM         rgb565(82, 96, 120)
#define COL_ACCENT      rgb565(235, 87, 87)
#define COL_GOLD        rgb565(241, 196, 15)
#define COL_CYAN        rgb565(52, 152, 219)
#define COL_GREEN       rgb565(46, 204, 113)
#define COL_ORANGE      rgb565(242, 153, 74)
#define COL_PURPLE      rgb565(155, 89, 182)
#define COL_NECK_WOOD   rgb565(40, 29, 23)
#define COL_NECK_POS    rgb565(58, 46, 38)
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
            if (dx * dx + dy * dy <= r2) put_pixel(s, cx + dx, cy + dy, col);
        }
    }
}

static void draw_circle_outline(SDL_Surface *s, int cx, int cy, int r, uint16_t col) {
    int r_in = (r - 1) * (r - 1);
    int r_out = r * r + r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= r_in && d2 <= r_out) put_pixel(s, cx + dx, cy + dy, col);
        }
    }
}

/* Tilted musical oval notehead (rx=4, ry=3) */
static void draw_notehead(SDL_Surface *s, int cx, int cy, uint16_t col) {
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            /* Slightly tilted ellipse equation */
            int u = dx * 3 - dy;
            int v = dy * 4 + dx;
            if (u * u + v * v <= 155) {
                put_pixel(s, cx + dx, cy + dy, col);
            }
        }
    }
}

/* Thick slanted beam connecting two eighth-note stems */
static void draw_beam(SDL_Surface *s, int x0, int y0, int x1, int y1, int thickness, uint16_t col) {
    if (x1 <= x0) return;
    for (int x = x0; x <= x1; x++) {
        int y = y0 + ((y1 - y0) * (x - x0)) / (x1 - x0);
        for (int t = 0; t < thickness; t++) {
            put_pixel(s, x, y + t, col);
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
 * Guitar Positions, Key Signatures & Programmatic Sight-Reading Model
 * ========================================================================= */
/* Standard tuning open-string MIDI pitches: E2(40), A2(45), D3(50), G3(55), B3(59), E4(64) */
static const int  k_open_midi[6]  = { 40, 45, 50, 55, 59, 64 };
static const char k_str_labels[6] = { 'E', 'A', 'D', 'G', 'B', 'e' };

/* 5 Guitar Neck Positions (strictly constrains which frets are used in the piece) */
#define NUM_POSITIONS 5
typedef struct {
    const char *short_name;
    const char *full_name;
    int         min_fret;
    int         max_fret;
    int         base_fret; /* Fret where index finger (1) rests */
} NeckPosition;

static const NeckPosition k_positions[NUM_POSITIONS] = {
    { "OPEN (0-4fr)", "Open Position (Frets 0-4)", 0, 4, 1 },
    { "POS 2 (2-5fr)","2nd Position (Frets 2-5)",  2, 5, 2 },
    { "POS 5 (5-8fr)","5th Position (Frets 5-8)",  5, 8, 5 },
    { "POS 7 (7-10fr)","7th Position (Frets 7-10)",7, 10, 7 },
    { "POS 9 (9-12fr)","9th Position (Frets 9-12)",9, 12, 9 }
};

/*
 * 7 Key Signatures with their diatonic pitch classes and staff spelling:
 * For each natural letter C=0, D=1, E=2, F=3, G=4, A=5, B=6:
 *   deg_semitone[d] gives the pitch class (0..11) of that letter in this key!
 */
#define NUM_KEYS 7
typedef struct {
    const char *name;
    const char *badge;
    int         tonic_pc;
    int         sharps_or_flats; /* >0 = sharps, <0 = flats, 0 = C Maj / A Min */
    int         deg_pc[7];       /* Pitch class (0..11) for letter C, D, E, F, G, A, B */
    const char *deg_name[7];     /* Note name for letter C, D, E, F, G, A, B */
} KeySigDef;

static const KeySigDef k_keys[NUM_KEYS] = {
    { "C Major", "C Maj",    0,  0, { 0, 2, 4, 5, 7, 9, 11 }, { "C",  "D",  "E",  "F",  "G",  "A",  "B"  } },
    { "G Major", "G (1#)",   7,  1, { 0, 2, 4, 6, 7, 9, 11 }, { "C",  "D",  "E",  "F#", "G",  "A",  "B"  } },
    { "D Major", "D (2#)",   2,  2, { 1, 2, 4, 6, 7, 9, 11 }, { "C#", "D",  "E",  "F#", "G",  "A",  "B"  } },
    { "A Major", "A (3#)",   9,  3, { 1, 2, 4, 6, 8, 9, 11 }, { "C#", "D",  "E",  "F#", "G#", "A",  "B"  } },
    { "E Major", "E (4#)",   4,  4, { 1, 3, 4, 6, 8, 9, 11 }, { "C#", "D#", "E",  "F#", "G#", "A",  "B"  } },
    { "F Major", "F (1b)",   5, -1, { 0, 2, 4, 5, 7, 9, 10 }, { "C",  "D",  "E",  "F",  "G",  "A",  "Bb" } },
    { "Bb Major","Bb (2b)", 10, -2, { 0, 2, 3, 5, 7, 9, 10 }, { "C",  "D",  "Eb", "F",  "G",  "A",  "Bb" } }
};

/* Staff steps where key-signature sharps/flats are drawn on the Treble Clef:
 * Recall diatonic step 0 = Middle C (C3 guitar sounding = 1st ledger line below staff),
 * Line 1 (E3) = step 2, Line 2 (G3) = step 4, Line 3 (B3) = step 6, Line 4 (D4) = step 8, Line 5 (F4) = step 10.
 * Standard Treble Sharp order: F(10), C(7), G(11), D(8)
 * Standard Treble Flat order : B(6), E(9)
 */
static const int k_sharp_staff_steps[4] = { 10, 7, 11, 8 };
static const int k_flat_staff_steps[2]  = { 6, 9 };

#define MAX_PIECE_NOTES 16
#define MAX_SCALE_POOL  32

typedef struct {
    int8_t  str;           /* Guitar string 0 (6th E) .. 5 (1st e) */
    int8_t  fret;          /* Exact fret (strictly within active NeckPosition) */
    int8_t  finger;        /* 0=Open, 1=Index, 2=Middle, 3=Ring, 4=Pinky */
    int8_t  midi;          /* Sounding MIDI pitch (40=E2 .. 76=E5) */
    int8_t  diatonic_step; /* Staff step relative to C3=0 (-5=E2 .. +16=E5) */
    int8_t  dur_eighths;   /* 2 = Quarter Note, 1 = Eighth Note */
    int8_t  bar_idx;       /* Measure index (0..3) */
    int8_t  eighth_in_bar; /* Eighth-note offset within the measure */
    bool    beam_with_next;/* True if beamed to the next eighth note on the same beat */
    bool    beam_from_prev;/* True if beamed from the previous eighth note */
    char    label[16];     /* e.g. "C4", "F#3" */
} StaffNote;

typedef struct {
    int       exercise_num;  /* 1..99 */
    int       level;         /* 1..4 progressive complexity */
    int       num_bars;      /* 2 or 3 measures */
    int       beats_per_bar; /* 4 (or 3) */
    int       num_notes;     /* Total notes in piece (8..16) */
    StaffNote notes[MAX_PIECE_NOTES];
} SightPiece;

/* Build the ascending pool of diatonic notes strictly inside `pos` and `key` */
static int build_position_scale_pool(int pos_idx, int key_idx, StaffNote *pool) {
    const NeckPosition *pos = &k_positions[pos_idx];
    const KeySigDef *key = &k_keys[key_idx];
    int count = 0;
    int last_midi = -1;

    /* Scan from String 0 (6th E) to String 5 (1st e), frets min_fret..max_fret */
    for (int s = 0; s < 6; s++) {
        /* Avoid duplicate pitches on the next string by stopping before the next string's min_fret pitch */
        int next_str_min_midi = (s < 5) ? (k_open_midi[s + 1] + pos->min_fret) : 999;
        for (int f = pos->min_fret; f <= pos->max_fret; f++) {
            int midi = k_open_midi[s] + f;
            if (midi >= next_str_min_midi && s < 5) break;
            if (midi <= last_midi) continue;

            int pc = midi % 12;
            int found_deg = -1;
            for (int d = 0; d < 7; d++) {
                if (key->deg_pc[d] == pc) {
                    found_deg = d;
                    break;
                }
            }
            if (found_deg < 0) continue;

            /* Compute octave of the natural letter C..B so C3 (MIDI 48) -> step 0 */
            int c_base_midi = midi - key->deg_pc[found_deg];
            /* Handle C# (deg 0, pc 1) vs Cb */
            int octave_from_c3 = (c_base_midi - 48) / 12;
            if (c_base_midi < 48) octave_from_c3 = -((48 - c_base_midi + 11) / 12);
            int diatonic_step = octave_from_c3 * 7 + found_deg;

            int finger = 0;
            if (f > 0) {
                finger = f - pos->base_fret + 1;
                if (finger < 1) finger = 1;
                if (finger > 4) finger = 4;
            }

            StaffNote *n = &pool[count++];
            memset(n, 0, sizeof(*n));
            n->str = (int8_t)s;
            n->fret = (int8_t)f;
            n->finger = (int8_t)finger;
            n->midi = (int8_t)midi;
            n->diatonic_step = (int8_t)diatonic_step;
            int oct_num = (c_base_midi / 12) - 1;
            snprintf(n->label, sizeof(n->label), "%s%d", key->deg_name[found_deg], oct_num);

            last_midi = midi;
            if (count >= MAX_SCALE_POOL) return count;
        }
    }
    return count;
}

/*
 * Programmatically generate a musical sight-reading piece of 2 measures (8 beats,
 * 8 to 14 notes of mixed quarter and eighth notes) strictly within `pos_idx` and `key_idx`.
 * Complexity scales progressively with `exercise_num` (1, 2, 3, ...).
 */
static void generate_sight_piece(SightPiece *piece, int exercise_num, int pos_idx, int key_idx) {
    memset(piece, 0, sizeof(*piece));
    if (exercise_num < 1) exercise_num = 1;
    piece->exercise_num = exercise_num;
    piece->num_bars = 2;
    piece->beats_per_bar = 4;

    /* Progressive complexity tier based on exercise_num */
    if (exercise_num <= 3)      piece->level = 1; /* Level 1: Mostly Quarter Notes + 1 Eighth Pair */
    else if (exercise_num <= 8) piece->level = 2; /* Level 2: Balanced Quarter & Eighth Notes */
    else if (exercise_num <= 15)piece->level = 3; /* Level 3: Active Eighth Phrases & Diatonic Skips */
    else                        piece->level = 4; /* Level 4: Flowing Melodic Sight-Reading Etudes */

    StaffNote pool[MAX_SCALE_POOL];
    int pool_len = build_position_scale_pool(pos_idx, key_idx, pool);
    if (pool_len <= 0) return;

    const KeySigDef *key = &k_keys[key_idx];

    /* Find tonic note index near the middle of the position pool */
    int tonic_idx = pool_len / 2;
    int best_dist = 999;
    for (int i = 0; i < pool_len; i++) {
        if ((pool[i].midi % 12) == key->tonic_pc) {
            int d = abs(i - pool_len / 2);
            if (d < best_dist) {
                best_dist = d;
                tonic_idx = i;
            }
        }
    }

    /* Deterministic PRNG seeded by exercise_num, pos_idx, and key_idx */
    uint32_t rng = (uint32_t)(exercise_num * 747796405u + pos_idx * 2891336453u + key_idx * 1013904223u + 12345u);

    /* Decide rhythm across the 8 beats (2 bars x 4 beats):
     * Each beat is either 1 Quarter Note (dur=2) or 2 Beamed Eighth Notes (dur=1, 1).
     * Total notes capped at MAX_PIECE_NOTES (16) so 2 bars fit with generous spacing.
     */
    bool beat_is_eighth[8];
    int eighth_pair_target = 1;
    if (piece->level == 1)      eighth_pair_target = (exercise_num == 1) ? 1 : 2;
    else if (piece->level == 2) eighth_pair_target = 3;
    else if (piece->level == 3) eighth_pair_target = 4;
    else                        eighth_pair_target = 5;

    memset(beat_is_eighth, 0, sizeof(beat_is_eighth));
    /* Keep the final beat of Bar 2 (beat 7) as a Quarter Note cadence on the tonic */
    int pairs_placed = 0;
    for (int attempt = 0; attempt < 24 && pairs_placed < eighth_pair_target; attempt++) {
        rng = rng * 1664525u + 1013904223u;
        int b = (int)((rng >> 16) % 7); /* Beats 0..6 (leave beat 7 as quarter-note resolution) */
        if (!beat_is_eighth[b]) {
            beat_is_eighth[b] = true;
            pairs_placed++;
        }
    }

    int cur_pool_idx = tonic_idx;
    int dir = ((exercise_num & 1) == 0) ? 1 : -1;
    int n_idx = 0;

    for (int beat = 0; beat < 8 && n_idx < MAX_PIECE_NOTES; beat++) {
        int bar = beat / 4;
        int beat_in_bar = beat % 4;
        bool is_pair = beat_is_eighth[beat] && (n_idx + 1 < MAX_PIECE_NOTES);
        int sub_notes = is_pair ? 2 : 1;

        for (int sub = 0; sub < sub_notes; sub++) {
            if (beat == 0 && sub == 0) {
                /* Start on tonic or 3rd/5th of the key */
                cur_pool_idx = tonic_idx;
            } else if (beat == 7 && sub == 0) {
                /* Final beat resolves to the closest tonic in the pool */
                int nearest_t = tonic_idx;
                int min_d = 999;
                for (int i = 0; i < pool_len; i++) {
                    if ((pool[i].midi % 12) == key->tonic_pc) {
                        int d = abs(i - cur_pool_idx);
                        if (d < min_d) { min_d = d; nearest_t = i; }
                    }
                }
                cur_pool_idx = nearest_t;
            } else {
                rng = rng * 1664525u + 1013904223u;
                int r = (int)((rng >> 16) % 100);
                int step_delta = dir;
                if (piece->level == 1) {
                    /* Stepwise motion (+1 or -1 scale degree) */
                    if (r < 25) dir = -dir;
                    step_delta = dir;
                } else if (piece->level == 2) {
                    /* Stepwise + occasional diatonic 3rd skip */
                    if (r < 30) dir = -dir;
                    step_delta = (r > 80 && sub == 0) ? (dir * 2) : dir;
                } else {
                    /* Level 3 & 4: Stepwise, 3rd skips, and occasional 4th leap on beat */
                    if (r < 35) dir = -dir;
                    if (sub == 1) step_delta = dir; /* Eighth off-beat prefers stepwise */
                    else if (r > 85) step_delta = dir * 3;
                    else if (r > 65) step_delta = dir * 2;
                    else step_delta = dir;
                }

                cur_pool_idx += step_delta;
                if (cur_pool_idx >= pool_len - 1) {
                    cur_pool_idx = pool_len - 1;
                    dir = -1;
                } else if (cur_pool_idx <= 0) {
                    cur_pool_idx = 0;
                    dir = 1;
                }
            }

            StaffNote *dst = &piece->notes[n_idx];
            *dst = pool[cur_pool_idx];
            dst->dur_eighths   = is_pair ? 1 : 2;
            dst->bar_idx       = (int8_t)bar;
            dst->eighth_in_bar = (int8_t)(beat_in_bar * 2 + sub);
            dst->beam_with_next = (is_pair && sub == 0);
            dst->beam_from_prev = (is_pair && sub == 1);
            n_idx++;
        }
    }
    piece->num_notes = n_idx;
}

/* =========================================================================
 * Background-Thread Karplus-Strong Guitar Synthesizer + Beat Click
 * ========================================================================= */
#define NOTE_PCM_SAMPLES 26460 /* 0.60 sec per pre-synthesized note at 44.1 kHz */
#define CLICK_SAMPLES    2205  /* 50 ms subtle metronome pulse */

static int16_t           g_note_pcm[MAX_PIECE_NOTES][NOTE_PCM_SAMPLES];
static volatile uint32_t g_note_ready_gen[MAX_PIECE_NOTES];
static int16_t           g_click_pcm[CLICK_SAMPLES];

static pthread_t         g_synth_thread;
static pthread_mutex_t   g_synth_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t    g_synth_cond  = PTHREAD_COND_INITIALIZER;
static volatile uint32_t g_synth_gen   = 1;
static volatile bool     g_synth_quit  = false;

/* Shared application state */
#define MIN_BPM 40
#define MAX_BPM 180

static SightPiece    g_piece;
static volatile int  g_exercise_num = 1;
static volatile int  g_pos_idx      = 0; /* 0 = Open Position (Frets 0..4) */
static volatile int  g_key_idx      = 0; /* 0 = C Major */
static volatile int  g_bpm          = 84;
static volatile int  g_cur_note_idx = 0; /* Highlighted note on the staff */
static volatile bool g_playing      = false;
static volatile bool g_show_help    = false;

/* Active audio voice state */
static volatile int      g_active_voice_idx = -1;
static volatile uint32_t g_active_voice_pos = 0;
static volatile uint32_t g_active_voice_gen = 0;
static volatile bool     g_active_click     = false;
static volatile uint32_t g_active_click_pos = 0;

static void init_click_pcm(void) {
    for (int i = 0; i < CLICK_SAMPLES; i++) {
        double p = (double)i / (double)CLICK_SAMPLES;
        double env = (1.0 - p) * (1.0 - p);
        double ph = (2.0 * 3.14159265358979323846 * 1100.0 * i) / SAMPLE_RATE;
        g_click_pcm[i] = (int16_t)(sin(ph) * 9000.0 * env);
    }
}

/* Fixed-point Karplus-Strong string synthesis executed ONLY on background worker thread */
static bool synth_karplus_strong_note(int16_t *dst, int midi_note, uint32_t req_gen) {
    if (midi_note < 36) midi_note = 36;
    if (midi_note > 88) midi_note = 88;

    double freq = 440.0 * pow(2.0, (midi_note - 69) / 12.0);
    int delay_len = (int)(SAMPLE_RATE / freq + 0.5);
    if (delay_len < 16) delay_len = 16;
    if (delay_len > 1200) delay_len = 1200;

    int32_t delay_line[1205];
    uint32_t rng = (uint32_t)(midi_note * 2654435761U + 1234567U);
    int32_t prev = 0;
    for (int i = 0; i < delay_len; i++) {
        rng = rng * 1664525U + 1013904223U;
        int32_t noise = (int32_t)((int16_t)(rng >> 16));
        noise = (noise * 26000) >> 15;
        prev = (prev + noise) >> 1;
        delay_line[i] = prev;
    }

    int idx = 0;
    for (int s = 0; s < NOTE_PCM_SAMPLES; s++) {
        if ((s & 1023) == 0 && g_synth_gen != req_gen) {
            return false; /* Preempted by newer piece/selection */
        }
        int next_idx = idx + 1;
        if (next_idx >= delay_len) next_idx = 0;

        int32_t cur = delay_line[idx];
        int32_t nxt = delay_line[next_idx];
        delay_line[idx] = ((cur + nxt) * 254) >> 9;
        idx = next_idx;

        int32_t out = cur;
        if (s > (NOTE_PCM_SAMPLES * 85) / 100) {
            int rem = NOTE_PCM_SAMPLES - s;
            int tail = (NOTE_PCM_SAMPLES * 15) / 100;
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
        int num_notes = g_piece.num_notes;
        int midis[MAX_PIECE_NOTES];
        for (int i = 0; i < num_notes; i++) {
            midis[i] = g_piece.notes[i].midi;
        }
        pthread_mutex_unlock(&g_synth_mutex);

        bool completed = true;
        for (int i = 0; i < num_notes; i++) {
            if (g_synth_gen != req_gen || g_synth_quit) {
                completed = false;
                break;
            }
            if (!synth_karplus_strong_note(g_note_pcm[i], midis[i], req_gen)) {
                completed = false;
                break;
            }
            g_note_ready_gen[i] = req_gen;
        }

        if (completed && g_synth_gen == req_gen) {
            done_gen = req_gen;
        }
    }
    return NULL;
}

static void rebuild_and_resynth_piece(void) {
    pthread_mutex_lock(&g_synth_mutex);
    g_active_voice_idx = -1;
    generate_sight_piece(&g_piece, g_exercise_num, g_pos_idx, g_key_idx);
    if (g_cur_note_idx >= g_piece.num_notes) {
        g_cur_note_idx = 0;
    }
    g_synth_gen++;
    pthread_cond_signal(&g_synth_cond);
    pthread_mutex_unlock(&g_synth_mutex);
}

static void trigger_note_voice(int note_idx, bool with_click) {
    if (note_idx >= 0 && note_idx < g_piece.num_notes) {
        g_active_voice_pos = 0;
        g_active_voice_gen = g_synth_gen;
        g_active_voice_idx = note_idx;
    }
    if (with_click) {
        g_active_click_pos = 0;
        g_active_click = true;
    }
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *samples = (int16_t *)stream;
    int frames = len / (2 * AUDIO_CHANNELS);

    for (int i = 0; i < frames; i++) {
        int32_t mix = 0;

        int v = g_active_voice_idx;
        uint32_t vgen = g_active_voice_gen;
        if (v >= 0 && v < MAX_PIECE_NOTES && vgen == g_synth_gen && g_note_ready_gen[v] == vgen) {
            uint32_t pos = g_active_voice_pos;
            if (pos < NOTE_PCM_SAMPLES) {
                mix += g_note_pcm[v][pos];
                g_active_voice_pos = pos + 1;
            } else {
                g_active_voice_idx = -1;
            }
        }

        if (g_active_click) {
            uint32_t cpos = g_active_click_pos;
            if (cpos < CLICK_SAMPLES) {
                mix += g_click_pcm[cpos];
                g_active_click_pos = cpos + 1;
            } else {
                g_active_click = false;
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
        snprintf(g_config_path, sizeof(g_config_path), "/mnt/.sightread.cfg");
    } else {
        const char *home = getenv("HOME");
        if (home && access(home, W_OK) == 0) {
            snprintf(g_config_path, sizeof(g_config_path), "%s/.sightread.cfg", home);
        } else {
            snprintf(g_config_path, sizeof(g_config_path), "./.sightread.cfg");
        }
    }
}

static void load_config(void) {
    FILE *f = fopen(g_config_path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int val = 0;
        if (sscanf(line, "exercise_num=%d", &val) == 1 && val >= 1 && val <= 99) {
            g_exercise_num = val;
        } else if (sscanf(line, "pos_idx=%d", &val) == 1 && val >= 0 && val < NUM_POSITIONS) {
            g_pos_idx = val;
        } else if (sscanf(line, "key_idx=%d", &val) == 1 && val >= 0 && val < NUM_KEYS) {
            g_key_idx = val;
        } else if (sscanf(line, "bpm=%d", &val) == 1 && val >= MIN_BPM && val <= MAX_BPM) {
            g_bpm = val;
        }
    }
    fclose(f);
}

static void save_config(void) {
    FILE *f = fopen(g_config_path, "w");
    if (!f) return;
    fprintf(f, "exercise_num=%d\n", g_exercise_num);
    fprintf(f, "pos_idx=%d\n", g_pos_idx);
    fprintf(f, "key_idx=%d\n", g_key_idx);
    fprintf(f, "bpm=%d\n", g_bpm);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

/* =========================================================================
 * Automated Verifier for Programmatic Sight-Reading Generator
 * ========================================================================= */
static bool verify_sightread_generator(bool verbose) {
    bool all_ok = true;
    for (int pos = 0; pos < NUM_POSITIONS; pos++) {
        const NeckPosition *np = &k_positions[pos];
        for (int key = 0; key < NUM_KEYS; key++) {
            for (int ex = 1; ex <= 20; ex++) {
                SightPiece sp;
                generate_sight_piece(&sp, ex, pos, key);
                if (sp.num_notes < 8 || sp.num_notes > MAX_PIECE_NOTES) {
                    fprintf(stderr, "[FAIL] Ex #%d pos=%d key=%d: invalid num_notes=%d\n",
                            ex, pos, key, sp.num_notes);
                    all_ok = false;
                }
                int total_eighths = 0;
                for (int i = 0; i < sp.num_notes; i++) {
                    const StaffNote *n = &sp.notes[i];
                    if (n->fret < np->min_fret || n->fret > np->max_fret) {
                        fprintf(stderr,
                                "[FAIL] Ex #%d pos=%s note %d: fret %d outside position [%d..%d]\n",
                                ex, np->short_name, i, n->fret, np->min_fret, np->max_fret);
                        all_ok = false;
                    }
                    total_eighths += n->dur_eighths;
                }
                if (total_eighths != 16) {
                    fprintf(stderr, "[FAIL] Ex #%d pos=%d key=%d: total_eighths=%d (expected 16)\n",
                            ex, pos, key, total_eighths);
                    all_ok = false;
                }
            }
        }
        if (verbose) {
            printf("[OK] Position %-14s verified across all %d keys x 20 exercises\n",
                   np->short_name, NUM_KEYS);
        }
    }
    return all_ok;
}

/* =========================================================================
 * UI Rendering: Prominent Treble Staff (Top 70%) + Guitar Neck (Bottom 30%)
 * ========================================================================= */
/*
 * Staff vertical geometry inside the Prominent Staff Card (y: 24..164):
 *   5 Treble Staff Lines spaced by 8px (4px per diatonic step):
 *     Line 5 (F4 sounding / F5 written): y = 76  (step = 10)
 *     Line 4 (D4 sounding / D5 written): y = 84  (step = 8)
 *     Line 3 (B3 sounding / B4 written): y = 92  (step = 6)
 *     Line 2 (G3 sounding / G4 written): y = 100 (step = 4)
 *     Line 1 (E3 sounding / E4 written): y = 108 (step = 2)
 *   1st Ledger Line below staff (Middle C / C3 guitar): y = 116 (step = 0)
 *   General formula: y = 116 - diatonic_step * 4
 */
static inline int staff_y_for_step(int diatonic_step) {
    return 116 - diatonic_step * 4;
}

/* Anti-aliased 24x62 engraved Treble G-Clef bitmap aligned to 8px staff spacing
 * (Row 16 = Top Line F4 at y=76, Row 40 = G-Line at y=100, Row 48 = Bottom Line E3 at y=108) */
static const uint8_t k_treble_clef_alpha[62][24] = {
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  43,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 208, 255,  17,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 142, 255, 255, 227,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  35, 255, 255, 255, 255,  75,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 224, 255, 255, 255, 255, 255,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  43, 255, 255, 255,  54,  92, 255,  74,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 160, 255, 255,   0,   0,   0, 188, 245,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 255, 255,  55,   0,   0,   0,  44, 255,  18,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  37, 255, 251,   0,   0,   0,   0,   0, 255, 102,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 100, 255, 105,   0,   0,   0,   0,   0, 255, 162,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 170, 255,  15,   0,   0,   0,   0,  13, 255, 211,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 225, 255,   0,   0,   0,   0,   0,  81, 255, 231,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 255, 233,   0,   0,   0,   0,   0, 221, 255, 230,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 255, 168,   0,   0,   0,   0,  61, 255, 255, 217,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 255, 131,   0,   0,   0,   0, 255, 255, 255, 163,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 255, 131,   0,   0,   9, 137, 255, 255, 255, 100,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 230, 123,   0,  12, 194, 255, 255, 255, 255,  26,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 197, 128,   0, 126, 255, 255, 255, 255, 214,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 162, 125,  92, 255, 255, 255, 255, 255,  66,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  55, 255, 255, 255, 255, 255, 255, 207,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 214, 255, 255, 255, 255, 255, 255,  17,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,  10, 245, 255, 255, 255, 255, 255, 255,  85,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,  15, 255, 255, 255, 255, 255, 255, 255, 143,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   3,   9, 207, 255, 255, 255, 255, 255, 255, 207,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0, 108, 208, 255, 255, 255, 255, 255, 255, 145,  74,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0, 245, 255, 255, 255, 255, 255, 255, 255,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0, 196, 255, 255, 255, 255, 255, 244,   7, 214,  41,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0, 122, 255, 255, 255, 255, 255, 213,   0,   0, 182, 112,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,  26, 255, 255, 255, 255, 255, 171,   0,   0,   0, 119, 166,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0, 236, 255, 255, 255, 255, 157,   0,   0,   0,   0,  61, 208,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,  88, 255, 255, 255, 255, 187,   0,   0,   0,   0,   0,   0, 228,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0, 248, 255, 255, 255, 255,   0,   0,   0,   0,  43,  40,  27, 228, 112,  69,  57,  35,  24,  12,   0,   0,   0 },
    {   0,  78, 255, 255, 255, 251,  92,   0,   0,   0,   0, 171, 255, 255, 255, 255, 255, 255, 255, 255, 188,  43,   0,   0 },
    {   0, 171, 255, 255, 255, 131,   0,   0,   0,   0,  30, 238, 255, 255, 255, 255, 255, 255, 255, 255, 255, 163,   0,   0 },
    {   0, 251, 255, 255, 255,   0,   0,   0,   0,  32, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 132,   0 },
    {   0, 255, 255, 255, 153,   0,   0,   0,   0, 244, 255, 255, 255, 255, 148, 255,  89,  74, 199, 255, 255, 255, 255,  46 },
    {   1, 255, 255, 255,  61,   0,   0,   0,  78, 255, 255, 255,  98,   0,   0, 199,  66,   0,   0,  89, 255, 255, 255, 225 },
    {   0, 255, 255, 255,   3,   0,   0,   0, 202, 255, 255,  63,   0,   0,   0, 153, 143,   0,   0,   0, 148, 255, 255, 255 },
    {   0, 255, 255, 255,   0,   0,   0,   0, 228, 255, 199,   0,   0,   0,   0,  89, 202,   0,   0,   0,   0, 255, 255, 255 },
    {   0, 208, 255, 255,   7,   0,   0,   0, 196, 255, 151,   0,   0,   0,   0,  30, 227,   0,   0,   0,   0, 248, 255, 255 },
    {   0,  81, 255, 255,  71,   0,   0,   0,  85, 255, 219,   0,   0,   0,   0,   0, 219,  38,   0,   0,   0, 238, 255, 255 },
    {   0,   0, 255, 255, 143,   0,   0,   0,   0, 255, 255,   0,   0,   0,   0,   0, 205,  97,   0,   0,   0, 230, 255, 242 },
    {   0,   0,  54, 255, 255,   0,   0,   0,   0,  15, 255, 217,   0,   0,   0,   0, 145, 170,   0,   0,   0, 255, 255,  91 },
    {   0,   0,   0, 109, 255, 208,   0,   0,   0,   0,   0, 100,  85,   0,   0,   0,  74, 213,   0,   0, 142, 255, 219,   0 },
    {   0,   0,   0,   0, 119, 255, 193,   0,   0,   0,   0,   0,   0,   0,   0,   0,   6, 255,   0,  75, 255, 255,   0,   0 },
    {   0,   0,   0,   0,   0,  52, 255, 236,  47,   0,   0,   0,   0,   0,   0,   0,   0, 197, 168, 255, 233,   1,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0, 156, 255, 180,  57,   0,   0,   0,   0,   0,  54, 255, 255, 120,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0, 157, 247, 255, 255, 255, 255, 255, 255, 248, 140,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0, 162, 187, 219, 248, 248, 247, 241, 200, 168, 187,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 248,   4,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 231,  68,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0, 154, 255, 234,  88,   0,   0,   0,   0,   0,   0, 177, 126,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,  17, 255, 255, 255, 255, 255, 145,   0,   0,   0,   0,   0, 108, 182,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0, 196, 255, 255, 255, 255, 255, 255,  32,   0,   0,   0,   0,  46, 224,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255, 255, 157,   0,   0,   0,   0,  49, 238,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   6, 255, 255, 255, 255, 255, 255, 255, 168,   0,   0,   0,   0, 103, 207,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255, 255,  97,   0,   0,   0,   0, 234,  83,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0, 197, 255, 255, 255, 255, 255, 241,   0,   0,   0,   0, 143, 228,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,  15, 255, 255, 255, 255, 157,   0,   0,   0,   0, 174, 242,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,  44, 255, 255, 103,  26,  43,  81, 146, 255, 191,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0, 122, 213, 255, 225, 205, 153,  40,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 }
};

static void draw_treble_clef(SDL_Surface *s, int x, uint16_t col) {
    /* Top of sprite is at y=60 so Row 16 aligns with Top Staff Line (y=76)
     * and Row 40 aligns with the G-Line (y=100). */
    const int y0 = 60;
    for (int r = 0; r < 62; r++) {
        for (int c = 0; c < 24; c++) {
            uint8_t a = k_treble_clef_alpha[r][c];
            if (a > 0) {
                blend_pixel(s, x + c, y0 + r, col, a);
            }
        }
    }
}

static void render_help_overlay(SDL_Surface *screen) {
    fill_rect_alpha(screen, 0, 0, SCREEN_W, SCREEN_H, COL_BG, 230);
    fill_rect(screen, 12, 12, 296, 216, COL_PANEL);
    draw_border(screen, 12, 12, 296, 216, 2, COL_GOLD);

    fill_rect(screen, 14, 14, 292, 22, COL_HEADER);
    draw_text_centered(screen, FONT_UI_12, 160, 18, "SIGHT READER - CONTROLS & GUIDE", COL_GOLD);

    const struct {
        const char *btn;
        const char *desc;
        uint16_t    col;
    } rows[] = {
        { "[A] Button",   "Play / Pause entire musical piece",          COL_GREEN  },
        { "[B] Button",   "Play single highlighted note on staff",      COL_CYAN   },
        { "[LEFT/RIGHT]", "Highlight previous / next note on staff",    COL_WHITE  },
        { "[L1 / R1]",    "Prev / Next Exercise (progressive level)",   COL_GOLD   },
        { "[SELECT]",     "Cycle Neck Position (Open, 2nd, 5th, 7th)",  COL_ORANGE },
        { "[X] Button",   "Cycle Key Signature (C, G, D, A, E, F, Bb)", COL_PURPLE },
        { "[UP / DOWN]",  "Adjust Tempo (40 .. 180 BPM)",               COL_CYAN   },
        { "[Y] / [MENU]", "Close Help [Y]  |  Save & Quit [MENU]",      COL_ACCENT }
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
                       "8vb Guitar Treble Staff  +  Position Fretboard Map",
                       COL_GOLD);
}

static void render_ui(SDL_Surface *screen) {
    const NeckPosition *pos = &k_positions[g_pos_idx];
    const KeySigDef *key = &k_keys[g_key_idx];
    const StaffNote *active_note = &g_piece.notes[g_cur_note_idx];

    fill_rect(screen, 0, 0, SCREEN_W, SCREEN_H, COL_BG);

    /* ================= 1. TOP HEADER BAR (y: 0..22) ================= */
    fill_rect(screen, 0, 0, SCREEN_W, 22, COL_HEADER);
    fill_rect(screen, 0, 21, SCREEN_W, 1, COL_BORDER);

    /* Exercise # & Progressive Level Badge */
    uint16_t lvl_cols[5] = { COL_CYAN, COL_CYAN, COL_GREEN, COL_ORANGE, COL_PURPLE };
    fill_rect(screen, 4, 2, 74, 17, lvl_cols[g_piece.level]);
    char ex_str[24];
    snprintf(ex_str, sizeof(ex_str), "EX #%02d LVL%d", g_piece.exercise_num, g_piece.level);
    draw_text_centered(screen, FONT_UI_9, 41, 5, ex_str, COL_BG);

    /* Neck Position Badge */
    fill_rect(screen, 82, 2, 92, 17, COL_PANEL_ALT);
    draw_border(screen, 82, 2, 92, 17, 1, COL_CYAN);
    draw_text_centered(screen, FONT_UI_9, 128, 5, pos->short_name, COL_CYAN);

    /* Key Signature Badge */
    fill_rect(screen, 178, 2, 54, 17, COL_PANEL_ALT);
    draw_border(screen, 178, 2, 54, 17, 1, COL_GOLD);
    draw_text_centered(screen, FONT_UI_9, 205, 5, key->badge, COL_GOLD);

    /* BPM Readout */
    char bpm_str[16];
    snprintf(bpm_str, sizeof(bpm_str), "%dBPM", g_bpm);
    draw_text_centered(screen, FONT_UI_9, 257, 5, bpm_str, COL_WHITE);

    /* Y:Help Badge */
    fill_rect(screen, 283, 2, 33, 17, COL_PANEL_ALT);
    draw_border(screen, 283, 2, 33, 17, 1, COL_BORDER);
    draw_text_centered(screen, FONT_DOT_8, 299, 6, "Y:Help", COL_MUTED);

    /* ================= 2. PROMINENT TREBLE CLEF STAFF — 70% OF SCREEN (y: 24..164) ================= */
    fill_rect(screen, 4, 24, 312, 140, COL_PANEL);
    draw_border(screen, 4, 24, 312, 140, 1, COL_BORDER);

    /* Top Info Strip inside Staff Card (y: 26..42) */
    fill_rect(screen, 6, 26, 308, 16, COL_HEADER);
    char info_left[64];
    snprintf(info_left, sizeof(info_left),
             "NOTE %d/%d: %s  (%s)",
             g_cur_note_idx + 1, g_piece.num_notes,
             active_note->label,
             (active_note->dur_eighths == 1) ? "1/8 Eighth" : "1/4 Quarter");
    draw_text(screen, FONT_UI_9, 10, 29, info_left, COL_GOLD);

    char info_right[48];
    if (active_note->fret == 0) {
        snprintf(info_right, sizeof(info_right), "Str %d (%c) Open",
                 6 - active_note->str, k_str_labels[active_note->str]);
    } else {
        snprintf(info_right, sizeof(info_right), "Str %d (%c) Fret %d [Fg %d]",
                 6 - active_note->str, k_str_labels[active_note->str],
                 active_note->fret, active_note->finger);
    }
    draw_text(screen, FONT_UI_9, 198, 29, info_right, COL_CYAN);

    /* Draw the 5 Prominent Treble Staff Lines (y = 76, 84, 92, 100, 108) */
    int staff_x0 = 10;
    int staff_x1 = 310;
    for (int l = 0; l < 5; l++) {
        int ly = 76 + l * 8;
        fill_rect(screen, staff_x0, ly, staff_x1 - staff_x0, 1, COL_STAFF_LINE);
    }

    /* Left Staff Margin: Barline, Treble Clef (8vb), Key Signature, and 4/4 Time Sig */
    fill_rect(screen, staff_x0, 76, 2, 33, COL_STAFF_LINE);
    draw_treble_clef(screen, staff_x0 + 3, COL_GOLD);

    int ksig_x = staff_x0 + 28;
    if (key->sharps_or_flats > 0) {
        for (int s = 0; s < key->sharps_or_flats; s++) {
            int sy = staff_y_for_step(k_sharp_staff_steps[s]);
            draw_text(screen, FONT_UI_9, ksig_x + s * 6, sy - 5, "#", COL_GOLD);
        }
        ksig_x += key->sharps_or_flats * 6 + 2;
    } else if (key->sharps_or_flats < 0) {
        int nflats = -key->sharps_or_flats;
        for (int b = 0; b < nflats; b++) {
            int sy = staff_y_for_step(k_flat_staff_steps[b]);
            draw_text(screen, FONT_UI_9, ksig_x + b * 6, sy - 5, "b", COL_GOLD);
        }
        ksig_x += nflats * 6 + 2;
    } else {
        ksig_x += 4;
    }

    /* 4/4 Time Signature */
    draw_text_centered(screen, FONT_UI_10, ksig_x + 5, 78, "4", COL_WHITE);
    draw_text_centered(screen, FONT_UI_10, ksig_x + 5, 94, "4", COL_WHITE);

    /* Calculate Horizontal X Positions for each note across the 2 Measures (16 eighth-note slots) */
    int notes_x0 = ksig_x + 16;
    int notes_x1 = 304;
    int bar_w = (notes_x1 - notes_x0) / 2;

    /* Middle Measure Barline & Double End Barline */
    int mid_bar_x = notes_x0 + bar_w;
    fill_rect(screen, mid_bar_x, 76, 1, 33, COL_BAR_LINE);
    fill_rect(screen, staff_x1 - 3, 76, 1, 33, COL_STAFF_LINE);
    fill_rect(screen, staff_x1 - 1, 76, 2, 33, COL_STAFF_LINE);

    int note_cx[MAX_PIECE_NOTES];
    int note_cy[MAX_PIECE_NOTES];

    for (int i = 0; i < g_piece.num_notes; i++) {
        const StaffNote *n = &g_piece.notes[i];
        int bar_start_x = notes_x0 + n->bar_idx * bar_w + 10;
        int usable_bar_w = bar_w - 16;
        /* Position proportionally by eighth-note slot within the bar (0..7) */
        note_cx[i] = bar_start_x + (n->eighth_in_bar * usable_bar_w) / 8 + 4;
        note_cy[i] = staff_y_for_step(n->diatonic_step);
    }

    /* Draw Interactive Highlight Column for the Selected / Playing Note */
    int sel_x = note_cx[g_cur_note_idx];
    uint16_t sel_col = g_playing ? COL_ACCENT : COL_GOLD;
    fill_rect_alpha(screen, sel_x - 9, 45, 19, 115, sel_col, 45);
    draw_border(screen, sel_x - 10, 44, 21, 117, 1, sel_col);

    /* Draw Ledger Lines, Noteheads, Stems, and Eighth-Note Beams */
    for (int i = 0; i < g_piece.num_notes; i++) {
        const StaffNote *n = &g_piece.notes[i];
        int cx = note_cx[i];
        int cy = note_cy[i];
        bool is_sel = (i == g_cur_note_idx);

        /* 1. Draw Ledger Lines below bottom staff line (step <= 0: C3, A2, F2) */
        if (n->diatonic_step <= 0) {
            for (int st = 0; st >= n->diatonic_step; st -= 2) {
                int ly = staff_y_for_step(st);
                fill_rect(screen, cx - 6, ly, 13, 1, COL_STAFF_LINE);
            }
        }
        /* 2. Draw Ledger Lines above top staff line (step >= 12: A4, C5, E5) */
        if (n->diatonic_step >= 12) {
            for (int st = 12; st <= n->diatonic_step; st += 2) {
                int ly = staff_y_for_step(st);
                fill_rect(screen, cx - 6, ly, 13, 1, COL_STAFF_LINE);
            }
        }

        /* 3. Determine Stem Direction ( Beamed pairs share the first note's stem direction ) */
        int ref_step = n->diatonic_step;
        if (n->beam_with_next && i + 1 < g_piece.num_notes) {
            ref_step = (n->diatonic_step + g_piece.notes[i + 1].diatonic_step) / 2;
        } else if (n->beam_from_prev && i > 0) {
            ref_step = (g_piece.notes[i - 1].diatonic_step + n->diatonic_step) / 2;
        }
        bool stem_up = (ref_step < 6); /* Below middle line B3 -> stem up; on/above B3 -> stem down */

        uint16_t ncol = is_sel ? sel_col : ((n->dur_eighths == 1) ? COL_CYAN : COL_WHITE);
        draw_notehead(screen, cx, cy, ncol);

        int stem_x = stem_up ? (cx + 3) : (cx - 4);
        int stem_y_end = stem_up ? (cy - 22) : (cy + 22);
        int sy0 = (cy < stem_y_end) ? cy : stem_y_end;
        int sh  = abs(stem_y_end - cy);
        fill_rect(screen, stem_x, sy0, 2, sh, ncol);

        /* 4. If this is the first of a beamed eighth-note pair, draw the connecting beam! */
        if (n->beam_with_next && i + 1 < g_piece.num_notes) {
            int cx2 = note_cx[i + 1];
            int cy2 = note_cy[i + 1];
            int stem_x2 = stem_up ? (cx2 + 3) : (cx2 - 4);
            int stem_y2_end = stem_up ? (cy2 - 22) : (cy2 + 22);
            bool next_sel = (i + 1 == g_cur_note_idx);
            uint16_t bcol = (is_sel || next_sel) ? sel_col : COL_CYAN;
            draw_beam(screen, stem_x, stem_y_end, stem_x2 + 1, stem_y2_end, 3, bcol);
        }

        /* 5. Draw Note Label at bottom of Staff Card for selected note (and beat starts) */
        if (is_sel || (n->eighth_in_bar % 2 == 0 && !n->beam_with_next)) {
            draw_text_centered(screen, FONT_DOT_8, cx, 148, n->label,
                               is_sel ? sel_col : COL_MUTED);
        }
    }

    /* ================= 3. NON-INTERACTIVE GUITAR NECK POSITION REFERENCE (y: 166..215) ================= */
    fill_rect(screen, 4, 166, 312, 49, COL_PANEL);
    draw_border(screen, 4, 166, 312, 49, 1, COL_BORDER);

    /* Draw 12-Fret Guitar Neck (Fret 0 Open Nut + Frets 1..12) */
    int neck_x0 = 28;
    int fret_w  = 23; /* 12 frets * 23px = 276px (x: 28..304) */
    int neck_w  = fret_w * 12;
    int neck_y0 = 176;
    int neck_h  = 36;

    fill_rect(screen, neck_x0, neck_y0, neck_w, neck_h, COL_NECK_WOOD);

    /* Highlight the active Position Fret Range on the Fingerboard */
    int pos_f0 = (pos->min_fret == 0) ? 1 : pos->min_fret;
    int pos_f1 = pos->max_fret;
    fill_rect(screen,
              neck_x0 + (pos_f0 - 1) * fret_w,
              neck_y0,
              (pos_f1 - pos_f0 + 1) * fret_w,
              neck_h,
              COL_NECK_POS);

    /* String Labels on the left ('e B G D A E', staggered horizontally so 6px rows never touch) */
    for (int row = 0; row < 6; row++) {
        int str_idx = 5 - row;
        int sy = neck_y0 + 3 + row * 6;
        int lx = (row % 2 == 0) ? 7 : 14;
        char slbl[2] = { k_str_labels[str_idx], '\0' };
        draw_text(screen, FONT_DOT_8, lx, sy - 4, slbl,
                  (str_idx == active_note->str) ? COL_GOLD : COL_MUTED);
    }

    /* Fret Numbers (0..12), Fretwires, and Inlay Dots */
    draw_text_centered(screen, FONT_DOT_8, neck_x0 - 6, 167, "0",
                       (pos->min_fret == 0) ? COL_CYAN : COL_DIM);

    for (int f = 0; f <= 12; f++) {
        int fx = neck_x0 + f * fret_w;
        fill_rect(screen, fx, neck_y0, (f == 0) ? 3 : 1, neck_h,
                  (f == 0) ? COL_GOLD : COL_FRET_WIRE);

        if (f >= 1 && f <= 12) {
            int fcx = fx - fret_w / 2;
            bool in_pos = (f >= pos->min_fret && f <= pos->max_fret);
            char fn_str[8];
            snprintf(fn_str, sizeof(fn_str), "%d", f);
            draw_text_centered(screen, FONT_DOT_8, fcx, 167, fn_str,
                               in_pos ? COL_CYAN : COL_DIM);

            if (f == 3 || f == 5 || f == 7 || f == 9) {
                fill_circle(screen, fcx, neck_y0 + neck_h / 2, 2, COL_DIM);
            } else if (f == 12) {
                fill_circle(screen, fcx, neck_y0 + 9, 2, COL_DIM);
                fill_circle(screen, fcx, neck_y0 + neck_h - 9, 2, COL_DIM);
            }
        }
    }

    /* 6 Horizontal Guitar Strings */
    for (int row = 0; row < 6; row++) {
        int str_idx = 5 - row;
        int sy = neck_y0 + 3 + row * 6;
        uint16_t scol = (str_idx <= 2) ? rgb565(175, 160, 140) : rgb565(205, 212, 222);
        fill_rect(screen, neck_x0, sy, neck_w, (str_idx == 0) ? 2 : 1, scol);
    }

    /* 1. Draw all notes used in the piece on the guitar neck (subtle cyan rings) */
    for (int i = 0; i < g_piece.num_notes; i++) {
        const StaffNote *n = &g_piece.notes[i];
        int sy = neck_y0 + 3 + (5 - n->str) * 6;
        int fcx = (n->fret == 0) ? (neck_x0 - 6) : (neck_x0 + n->fret * fret_w - fret_w / 2);
        fill_circle(screen, fcx, sy, 3, COL_PANEL_ALT);
        draw_circle_outline(screen, fcx, sy, 3, COL_CYAN);
    }

    /* 2. Brightly highlight the currently selected staff note on the guitar neck */
    {
        int sy = neck_y0 + 3 + (5 - active_note->str) * 6;
        int fcx = (active_note->fret == 0)
                  ? (neck_x0 - 6)
                  : (neck_x0 + active_note->fret * fret_w - fret_w / 2);
        fill_circle(screen, fcx, sy, 5, sel_col);
        draw_circle_outline(screen, fcx, sy, 6, COL_WHITE);
        char fg_str[8];
        snprintf(fg_str, sizeof(fg_str), "%d", active_note->finger);
        draw_text_centered(screen, FONT_DOT_8, fcx, sy - 4, fg_str, COL_WHITE);
    }

    /* ================= 4. BOTTOM CONTROLS FOOTER (y: 217..240) ================= */
    fill_rect(screen, 0, 217, SCREEN_W, 23, COL_HEADER);
    fill_rect(screen, 0, 217, SCREEN_W, 1, COL_BORDER);

    uint16_t play_col = g_playing ? COL_ORANGE : COL_GREEN;
    fill_rect(screen, 4, 221, 54, 15, play_col);
    draw_text_centered(screen, FONT_DOT_8, 31, 225, g_playing ? "[A] PAUSE" : "[A] PLAY", COL_BG);

    draw_text(screen, FONT_DOT_8, 62, 225,
              "[B]1-Note [L/R]Note [L1/R1]Ex [SEL]Pos [X]Key",
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

    bool cli_auto_play = false;
    int  cli_note_idx  = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ex") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v >= 1 && v <= 99) g_exercise_num = v;
        } else if (strcmp(argv[i], "--pos") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v >= 0 && v < NUM_POSITIONS) g_pos_idx = v;
        } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v >= 0 && v < NUM_KEYS) g_key_idx = v;
        } else if (strcmp(argv[i], "--bpm") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v >= MIN_BPM && v <= MAX_BPM) g_bpm = v;
        } else if (strcmp(argv[i], "--note") == 0 && i + 1 < argc) {
            cli_note_idx = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--play") == 0) {
            cli_auto_play = true;
        } else if (strcmp(argv[i], "--help-overlay") == 0) {
            g_show_help = true;
        } else if (strcmp(argv[i], "--verify") == 0) {
            return verify_sightread_generator(true) ? 0 : 1;
        }
    }

    if (!verify_sightread_generator(false)) {
        fprintf(stderr, "Fatal: Sight-reading generator verification failed.\n");
        return 1;
    }

    init_click_pcm();
    generate_sight_piece(&g_piece, g_exercise_num, g_pos_idx, g_key_idx);
    if (cli_note_idx >= 0 && cli_note_idx < g_piece.num_notes) {
        g_cur_note_idx = cli_note_idx;
    }

    /* Start background Karplus-Strong synthesis worker thread */
    pthread_create(&g_synth_thread, NULL, synth_worker_thread_fn, NULL);
    rebuild_and_resynth_piece();
    if (cli_note_idx >= 0 && cli_note_idx < g_piece.num_notes) {
        g_cur_note_idx = cli_note_idx;
    }

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
    SDL_WM_SetCaption("V90 Sight Reader", NULL);

    double eighth_accum_ms = 0.0;
    int eighth_rem_in_note = g_piece.notes[g_cur_note_idx].dur_eighths;
    Uint32 last_tick_ms = SDL_GetTicks();

    if (cli_auto_play) {
        g_playing = true;
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

                /* Y Button: Toggle Help & Sight-Reading Guide Overlay */
                if (k == V90_BTN_Y
#if defined(HOST_X11)
                    || k == SDLK_y || k == SDLK_h
#endif
                ) {
                    g_show_help = !g_show_help;
                }
                /* MENU Button (or Q on host): Save & Quit cleanly */
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
                /* A Button: Play / Pause the entire piece */
                else if (k == V90_BTN_A
#if defined(HOST_X11)
                         || k == SDLK_a || k == SDLK_RETURN
#endif
                ) {
                    g_playing = !g_playing;
                    if (g_playing) {
                        eighth_accum_ms = 0.0;
                        last_tick_ms = now_ms;
                        eighth_rem_in_note = g_piece.notes[g_cur_note_idx].dur_eighths;
                        trigger_note_voice(g_cur_note_idx, true);
                    } else {
                        g_active_voice_idx = -1;
                    }
                }
                /* B Button: Play single currently highlighted note on the staff */
                else if (k == V90_BTN_B
#if defined(HOST_X11)
                         || k == SDLK_b
#endif
                ) {
                    g_playing = false;
                    trigger_note_voice(g_cur_note_idx, false);
                }
                /* LEFT / RIGHT: Move highlight cursor across notes on the Treble Staff */
                else if (k == V90_BTN_LEFT) {
                    g_playing = false;
                    g_cur_note_idx = (g_cur_note_idx + g_piece.num_notes - 1) % g_piece.num_notes;
                    trigger_note_voice(g_cur_note_idx, false);
                } else if (k == V90_BTN_RIGHT) {
                    g_playing = false;
                    g_cur_note_idx = (g_cur_note_idx + 1) % g_piece.num_notes;
                    trigger_note_voice(g_cur_note_idx, false);
                }
                /* L1 / R1: Previous / Next Programmatically Generated Exercise */
                else if (k == V90_BTN_L1 || k == V90_BTN_R2
#if defined(HOST_X11)
                         || k == SDLK_LEFTBRACKET
#endif
                ) {
                    g_playing = false;
                    g_exercise_num = (g_exercise_num > 1) ? (g_exercise_num - 1) : 99;
                    g_cur_note_idx = 0;
                    rebuild_and_resynth_piece();
                    save_config();
                } else if (k == V90_BTN_R1 || k == V90_BTN_L2
#if defined(HOST_X11)
                           || k == SDLK_RIGHTBRACKET
#endif
                ) {
                    g_playing = false;
                    g_exercise_num = (g_exercise_num < 99) ? (g_exercise_num + 1) : 1;
                    g_cur_note_idx = 0;
                    rebuild_and_resynth_piece();
                    save_config();
                }
                /* SELECT Button (or C on host): Cycle Neck Position */
                else if (k == V90_BTN_SELECT
#if defined(HOST_X11)
                         || k == SDLK_c
#endif
                ) {
                    g_playing = false;
                    g_pos_idx = (g_pos_idx + 1) % NUM_POSITIONS;
                    g_cur_note_idx = 0;
                    rebuild_and_resynth_piece();
                    save_config();
                }
                /* X Button: Cycle Key Signature */
                else if (k == V90_BTN_X
#if defined(HOST_X11)
                         || k == SDLK_x
#endif
                ) {
                    g_playing = false;
                    g_key_idx = (g_key_idx + 1) % NUM_KEYS;
                    g_cur_note_idx = 0;
                    rebuild_and_resynth_piece();
                    save_config();
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
            } else if (ev.type == SDL_KEYUP) {
                SDLKey k = ev.key.keysym.sym;
                if (k == V90_BTN_UP)   up_held = false;
                if (k == V90_BTN_DOWN) down_held = false;
            }
        }

        /* Smooth key repeat when holding UP or DOWN */
        if ((up_held || down_held) && (now_ms >= hold_start_ms + 350)) {
            if (now_ms >= last_repeat_ms + 60) {
                if (up_held && g_bpm < MAX_BPM) g_bpm++;
                if (down_held && g_bpm > MIN_BPM) g_bpm--;
                last_repeat_ms = now_ms;
            }
        }

        /* Advance Piece Playback Clock (1 eighth-note tick = 30000.0 / BPM ms) */
        now_ms = SDL_GetTicks();
        double eighth_ms = 30000.0 / (double)g_bpm;

        if (g_playing) {
            Uint32 dt = (now_ms >= last_tick_ms) ? (now_ms - last_tick_ms) : 0;
            if (dt > 2000) dt = 0;
            last_tick_ms = now_ms;
            eighth_accum_ms += (double)dt;

            while (eighth_accum_ms >= eighth_ms) {
                eighth_accum_ms -= eighth_ms;
                eighth_rem_in_note--;
                if (eighth_rem_in_note <= 0) {
                    g_cur_note_idx = (g_cur_note_idx + 1) % g_piece.num_notes;
                    const StaffNote *nn = &g_piece.notes[g_cur_note_idx];
                    eighth_rem_in_note = nn->dur_eighths;
                    bool on_beat = (nn->eighth_in_bar % 2) == 0;
                    trigger_note_voice(g_cur_note_idx, on_beat);
                } else {
                    /* Second half of a quarter note: trigger subtle beat pulse if on beat */
                    g_active_click_pos = 0;
                    g_active_click = true;
                }
            }
        } else {
            last_tick_ms = now_ms;
        }

        if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
        render_ui(screen);
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
