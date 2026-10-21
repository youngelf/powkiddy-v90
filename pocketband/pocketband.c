/*
 * pocketband - Pocket Drum Machine, Bassline & Backing Track Jam Buddy
 *              for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Desktop (x86_64 X11)
 *
 * Screen resolution: 320x240 16-bit RGB565 (scaled 2x to 640x480 on Linux X11 desktop)
 *
 * Features:
 *   - Default Guitarist Jam View: Large Current/Next Chord readout, Bar counter,
 *     12-Fret Guitar Scale Map (Pentatonic / Blues / Major / Dorian), and Beat/Step LEDs.
 *   - 6 Built-in Rock, Blues & Jazz Styles in 4/4 (1 & 2 & 3 & 4 &) and 3/4 (1 & 2 & 3 &)
 *     with authentic drum grooves and basslines.
 *   - Step Sequencer View (SELECT): Edit Kick, Snare, Closed Hat, Open Hat, and Bassline
 *     (Root, 3rd, 5th, 6th, b7, Octave) on every beat and '&'.
 *   - 3-Way Backing Mix (X): Full Band (Drums+Bass+Chords) -> Drums+Bass -> Drums Only.
 *   - 100% Procedural Fixed-Point Audio Synth (Kick, Snare, Hi-Hats, Electric Bass, Chords).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "pocketband_font.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define SCREEN_BPP 16
#define SAMPLE_RATE 44100
#define AUDIO_CHANNELS 2

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
#define SDLK_UNKNOWN      0
#define SDLK_LALT         XK_Alt_L
#define SDLK_LCTRL        XK_Control_L
#define SDLK_LSHIFT       XK_Shift_L
#define SDLK_SPACE        XK_space
#define SDLK_TAB          XK_Tab
#define SDLK_BACKSPACE    XK_BackSpace
#define SDLK_ESCAPE       XK_Escape
#define SDLK_HOME         XK_Home
#define SDLK_RETURN       XK_Return
#define SDLK_RCTRL        XK_Control_R
#define SDLK_UP           XK_Up
#define SDLK_DOWN         XK_Down
#define SDLK_LEFT         XK_Left
#define SDLK_RIGHT        XK_Right
#define SDLK_BRACKETLEFT  XK_bracketleft
#define SDLK_BRACKETRIGHT XK_bracketright
#define SDLK_a            XK_a
#define SDLK_b            XK_b
#define SDLK_c            XK_c
#define SDLK_f            XK_f
#define SDLK_h            XK_h
#define SDLK_q            XK_q
#define SDLK_x            XK_x
#define SDLK_y            XK_y

#define SDL_QUIT          1
#define SDL_KEYDOWN       2
#define SDL_KEYUP         3

typedef struct { int bpp; } SDL_PixelFormat;
typedef struct {
    int w, h;
    int pitch;
    SDL_PixelFormat *format;
    void *pixels;
} SDL_Surface;

typedef struct {
    int type;
    struct {
        struct { SDLKey sym; } keysym;
    } key;
} SDL_Event;

#define SDL_INIT_VIDEO  0x01
#define SDL_INIT_TIMER  0x02
#define SDL_INIT_AUDIO  0x08
#define SDL_DISABLE     0
#define SDL_SWSURFACE   0
#define AUDIO_S16SYS    0

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
static SDL_PixelFormat g_x11_fmt = { 32 };
static SDL_Surface g_x11_surface = { 0 };

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
            pa = fn_new(NULL, "V90 PocketBand", PA_STREAM_PLAYBACK, NULL, "PocketBand Synth", &ss, NULL, NULL, &err);
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
            usleep((chunk_frames * 1000000ULL) / (unsigned)g_audio_spec.freq);
        }
    }

    if (pa && fn_free) fn_free(pa);
    if (pa_lib) dlclose(pa_lib);
    return NULL;
}

static int SDL_Init(Uint32 flags) {
    (void)flags;
    g_x11_dpy = XOpenDisplay(NULL);
    return g_x11_dpy ? 0 : -1;
}

static const char *SDL_GetError(void) {
    return "Cannot open X11 DISPLAY";
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
    XMapRaised(g_x11_dpy, g_x11_win);
    g_x11_gc = DefaultGC(g_x11_dpy, scr);

    g_x11_surface.w = width;
    g_x11_surface.h = height;
    g_x11_surface.pitch = width * 4;
    g_x11_surface.format = &g_x11_fmt;
    g_x11_surface.pixels = calloc((size_t)(width * height), sizeof(Uint32));

    g_scaled_buf = (char *)calloc((size_t)(win_w * win_h), 4);
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

static void SDL_Flip(SDL_Surface *screen) {
    int win_w = screen->w * HOST_SCALE;
    Uint32 *src = (Uint32 *)screen->pixels;
    Uint32 *dst = (Uint32 *)g_scaled_buf;
    for (int y = 0; y < screen->h; y++) {
        for (int x = 0; x < screen->w; x++) {
            Uint32 c = src[y * screen->w + x];
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

/* =========================================================================
 * PowKiddy V90 Button Mappings (MiyooCFW 1.3.3)
 * ========================================================================= */
#define V90_BTN_UP      SDLK_UP
#define V90_BTN_DOWN    SDLK_DOWN
#define V90_BTN_LEFT    SDLK_LEFT
#define V90_BTN_RIGHT   SDLK_RIGHT
#define V90_BTN_A       SDLK_LALT       /* A (East)  : Play/Stop (Jam View) or Cycle Step (Step Editor) */
#define V90_BTN_B       SDLK_LCTRL      /* B (South) : Queue Drum Fill (Jam) or Clear Step (Editor) */
#define V90_BTN_X       SDLK_LSHIFT     /* X (North) : Cycle Backing Mix (Full Band / Drum+Bass / Drums) */
#define V90_BTN_Y       SDLK_SPACE      /* Y (West)  : Open/Close Help & Settings modal */
#define V90_BTN_L1      SDLK_TAB        /* L1        : Tempo -5 BPM */
#define V90_BTN_R1      SDLK_BACKSPACE  /* R1        : Tempo +5 BPM */
#define V90_BTN_SELECT  SDLK_ESCAPE     /* SELECT    : Toggle Guitarist Jam View <-> Step Sequencer */
#define V90_BTN_START   SDLK_RETURN     /* START     : Save & Exit */
#define V90_BTN_MENU    SDLK_RCTRL      /* MENU      : Save & Exit */

/* =========================================================================
 * 24-Bit Software Framebuffer & Anti-Aliased Vector/Text Primitives
 * ========================================================================= */
static uint32_t g_fb[SCREEN_W * SCREEN_H];

#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

static inline void blend_pixel(int x, int y, uint32_t fg, uint8_t alpha) {
    if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H || alpha == 0) return;
    if (alpha == 255) {
        g_fb[y * SCREEN_W + x] = fg;
        return;
    }
    uint32_t bg = g_fb[y * SCREEN_W + x];
    uint32_t a = alpha;
    uint32_t ia = 255 - a;
    uint32_t r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * ia) / 255;
    uint32_t g = (((fg >> 8)  & 0xFF) * a + ((bg >> 8)  & 0xFF) * ia) / 255;
    uint32_t b = ((fg & 0xFF)         * a + (bg & 0xFF)         * ia) / 255;
    g_fb[y * SCREEN_W + x] = RGB(r, g, b);
}

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > SCREEN_W) ? SCREEN_W : (x + w);
    int y1 = (y + h > SCREEN_H) ? SCREEN_H : (y + h);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = &g_fb[yy * SCREEN_W];
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = color;
        }
    }
}

static void draw_border(int x, int y, int w, int h, uint32_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

static void draw_pill(int x, int y, int w, int h, uint32_t fill_col, uint32_t border_col) {
    if (w < 3 || h < 3) return;
    fill_rect(x + 1, y + 1, w - 2, h - 2, fill_col);
    fill_rect(x + 1, y, w - 2, 1, border_col);
    fill_rect(x + 1, y + h - 1, w - 2, 1, border_col);
    fill_rect(x, y + 1, 1, h - 2, border_col);
    fill_rect(x + w - 1, y + 1, 1, h - 2, border_col);
}

static void draw_filled_circle_aa(float cx, float cy, float radius, uint32_t fill_col, uint32_t border_col) {
    int x0 = (int)(cx - radius - 2.0f);
    int x1 = (int)(cx + radius + 2.0f);
    int y0 = (int)(cy - radius - 2.0f);
    int y1 = (int)(cy + radius + 2.0f);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float d = sqrtf(dx * dx + dy * dy);
            if (d <= radius - 1.0f) {
                blend_pixel(x, y, fill_col, 255);
            } else if (d <= radius) {
                float t = radius - d;
                blend_pixel(x, y, fill_col, (uint8_t)(t * 255.0f));
                blend_pixel(x, y, border_col, (uint8_t)((1.0f - t * 0.5f) * 220.0f));
            } else if (d <= radius + 0.9f) {
                float a = (radius + 0.9f - d) / 0.9f;
                blend_pixel(x, y, border_col, (uint8_t)(a * 200.0f));
            }
        }
    }
}

static int char_advance(int font_id, char c) {
    const PbFont *f = &g_pb_fonts[font_id];
    unsigned char uc = (unsigned char)c;
    if (uc < 32 || uc > 126) uc = '?';
    return f->glyphs[uc - 32].advance;
}

static int text_width(int font_id, const char *s) {
    int w = 0;
    while (*s) {
        w += char_advance(font_id, *s++);
    }
    return w;
}

static void draw_text(int font_id, int x, int y, const char *s, uint32_t color) {
    const PbFont *f = &g_pb_fonts[font_id];
    int cur_x = x;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 32 || c > 126) c = '?';
        const PbGlyph *g = &f->glyphs[c - 32];
        const uint8_t *bmp = &g_pb_font_bitmaps[g->offset];
        for (int row = 0; row < g->h; row++) {
            for (int col = 0; col < g->w; col++) {
                uint8_t a = bmp[row * g->w + col];
                if (a > 0) {
                    blend_pixel(cur_x + g->xoff + col, y + g->yoff + row, color, a);
                }
            }
        }
        cur_x += g->advance;
    }
}

static void draw_text_centered(int font_id, int bx, int by, int bw, int bh, const char *s, uint32_t color) {
    const PbFont *f = &g_pb_fonts[font_id];
    int tw = text_width(font_id, s);
    int tx = bx + (bw - tw) / 2;
    int ty = by + (bh - f->line_height) / 2;
    draw_text(font_id, tx, ty, s, color);
}

static void present_frame(SDL_Surface *screen) {
#if defined(HOST_X11)
    memcpy(screen->pixels, g_fb, sizeof(g_fb));
    SDL_Flip(screen);
#else
    if (screen->format->BitsPerPixel == 16) {
        uint16_t *dst = (uint16_t *)screen->pixels;
        int stride = screen->pitch / 2;
        for (int y = 0; y < SCREEN_H; y++) {
            uint16_t *drow = dst + y * stride;
            const uint32_t *srow = &g_fb[y * SCREEN_W];
            for (int x = 0; x < SCREEN_W; x++) {
                uint32_t c = srow[x];
                uint16_t r = (uint16_t)((c >> 16) & 0xFF);
                uint16_t g = (uint16_t)((c >> 8)  & 0xFF);
                uint16_t b = (uint16_t)(c & 0xFF);
                drow[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            }
        }
    } else if (screen->format->BitsPerPixel == 32) {
        memcpy(screen->pixels, g_fb, sizeof(g_fb));
    }
    SDL_Flip(screen);
#endif
}

static void save_bmp(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t row_bytes = SCREEN_W * 3;
    uint32_t img_bytes = row_bytes * SCREEN_H;
    uint32_t file_size = 54 + img_bytes;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (uint8_t)(file_size);
    hdr[3] = (uint8_t)(file_size >> 8);
    hdr[4] = (uint8_t)(file_size >> 16);
    hdr[5] = (uint8_t)(file_size >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (uint8_t)(SCREEN_W);
    hdr[19] = (uint8_t)(SCREEN_W >> 8);
    int32_t neg_h = -SCREEN_H;
    memcpy(&hdr[22], &neg_h, 4);
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t c = g_fb[y * SCREEN_W + x];
            uint8_t bgr[3] = {
                (uint8_t)(c & 0xFF),
                (uint8_t)((c >> 8) & 0xFF),
                (uint8_t)((c >> 16) & 0xFF)
            };
            fwrite(bgr, 1, 3, f);
        }
    }
    fclose(f);
}

/* =========================================================================
 * Built-In Rock, Blues & Jazz Styles (4/4 & 3/4) + Bassline & Drum Patterns
 * ========================================================================= */
/*
 * Step Subdivision:
 *   4/4 Time = 8 eighth-note steps per bar: 1, &, 2, &, 3, &, 4, & (steps 0..7)
 *   3/4 Time = 6 eighth-note steps per bar: 1, &, 2, &, 3, &       (steps 0..5)
 *
 * Drum Step Values (Tracks 0..3):
 *   0 = Off
 *   1 = Normal Hit
 *   2 = Accent Hit
 *
 * Bass Step Values (Track 4):
 *   0 = Off (Rest)
 *   1 = R   (Root, +0 semitones)
 *   2 = 3   (Chord 3rd: +3 minor / +4 major semitones)
 *   3 = 5   (Fifth, +7 semitones)
 *   4 = 6   (Major 6th, +9 semitones — classic Blues shuffle degree)
 *   5 = b7  (Flat 7th, +10 semitones — Blues/Rock/Jazz dominant walk)
 *   6 = 8   (Octave, +12 semitones)
 */
#define NUM_STYLES 6
#define MAX_STEPS  8
#define NUM_TRACKS 5

typedef enum {
    TRK_KICK   = 0,
    TRK_SNARE  = 1,
    TRK_CLHAT  = 2,
    TRK_OPHAT  = 3,
    TRK_BASS   = 4
} TrackId;

static const char *k_track_names[NUM_TRACKS] = {
    "KICK", "SNARE", "CL HAT", "OP HAT", "BASS"
};

static const char *k_bass_deg_labels[7] = {
    ".", "R", "3", "5", "6", "b7", "8"
};

typedef enum {
    CHQ_MAJ  = 0,
    CHQ_MIN  = 1,
    CHQ_DOM7 = 2,
    CHQ_MAJ7 = 3,
    CHQ_MIN7 = 4
} BarChordQuality;

typedef struct {
    int             root_offset; /* Semitones above active Key root (0..11) */
    BarChordQuality qual;
    const char     *roman;
} BarChord;

typedef struct {
    const char *name;
    const char *short_genre;
    int         beats_per_bar;   /* 4 for 4/4 (8 steps: 1 & 2 & 3 & 4 &), 3 for 3/4 (6 steps: 1 & 2 & 3 &) */
    int         steps_per_bar;   /* 8 for 4/4, 6 for 3/4 */
    int         default_bpm;
    int         swing_pct;       /* 50 = Straight 8ths, 62..66 = Swing/Shuffle on '&' steps */
    int         num_bars;        /* 4 or 12 bars in chord progression */
    BarChord    bars[12];
    const char *scale_name;
    uint8_t     scale_mask[12];  /* 1 = in soloing scale (semitones from key root), 2 = blue/color note */
    uint8_t     default_steps[NUM_TRACKS][MAX_STEPS];
} StylePreset;

static const StylePreset k_styles[NUM_STYLES] = {
    /* 0: Blues Shuffle 4/4 (12-Bar Blues, I7 - IV7 - V7, 65% Triplet Shuffle) */
    {
        "Blues Shuffle 4/4", "BLUES 4/4", 4, 8, 110, 65, 12,
        {
            { 0, CHQ_DOM7, "I7"  }, { 0, CHQ_DOM7, "I7"  }, { 0, CHQ_DOM7, "I7"  }, { 0, CHQ_DOM7, "I7"  },
            { 5, CHQ_DOM7, "IV7" }, { 5, CHQ_DOM7, "IV7" }, { 0, CHQ_DOM7, "I7"  }, { 0, CHQ_DOM7, "I7"  },
            { 7, CHQ_DOM7, "V7"  }, { 5, CHQ_DOM7, "IV7" }, { 0, CHQ_DOM7, "I7"  }, { 7, CHQ_DOM7, "V7"  }
        },
        "Minor Pentatonic + Blues (1 b3 4 b5 5 b7)",
        { 1, 0, 0, 1, 0, 1, 2, 1, 0, 0, 1, 0 },
        {
            /* steps:   1  &  2  &  3  &  4  &  */
            /* KICK */ { 2, 0, 0, 0, 1, 0, 0, 1 },
            /* SNARE*/ { 0, 0, 2, 0, 0, 0, 2, 0 },
            /* CLHAT*/ { 2, 1, 2, 1, 2, 1, 2, 1 },
            /* OPHAT*/ { 0, 0, 0, 0, 0, 0, 0, 0 },
            /* BASS */ { 1, 3, 4, 3, 1, 3, 4, 5 }  /* Classic Blues Shuffle: R-5-6-5 R-5-6-b7 */
        }
    },

    /* 1: Slow Blues 3/4 (12-Bar Waltz Blues in 3/4, 1 & 2 & 3 &) */
    {
        "Slow Blues 3/4", "BLUES 3/4", 3, 6, 86, 62, 12,
        {
            { 0, CHQ_DOM7, "I7"  }, { 0, CHQ_DOM7, "I7"  }, { 0, CHQ_DOM7, "I7"  }, { 0, CHQ_DOM7, "I7"  },
            { 5, CHQ_DOM7, "IV7" }, { 5, CHQ_DOM7, "IV7" }, { 0, CHQ_DOM7, "I7"  }, { 0, CHQ_DOM7, "I7"  },
            { 7, CHQ_DOM7, "V7"  }, { 5, CHQ_DOM7, "IV7" }, { 0, CHQ_DOM7, "I7"  }, { 7, CHQ_DOM7, "V7"  }
        },
        "Minor Pentatonic + Blues (1 b3 4 b5 5 b7)",
        { 1, 0, 0, 1, 0, 1, 2, 1, 0, 0, 1, 0 },
        {
            /* steps:   1  &  2  &  3  &       */
            /* KICK */ { 2, 0, 0, 1, 0, 0, 0, 0 },
            /* SNARE*/ { 0, 0, 0, 0, 2, 0, 0, 0 },
            /* CLHAT*/ { 2, 1, 1, 1, 1, 0, 0, 0 },
            /* OPHAT*/ { 0, 0, 0, 0, 0, 1, 0, 0 },
            /* BASS */ { 1, 2, 3, 4, 3, 2, 0, 0 }  /* 3/4 Blues Arpeggio: R - 3 - 5 - 6 - 5 - 3 */
        }
    },

    /* 2: Classic Rock 4/4 (Straight 8ths, I - bVII - IV - I) */
    {
        "Classic Rock 4/4", "ROCK 4/4", 4, 8, 120, 50, 4,
        {
            {  0, CHQ_MAJ, "I"    },
            { 10, CHQ_MAJ, "bVII" },
            {  5, CHQ_MAJ, "IV"   },
            {  0, CHQ_MAJ, "I"    }
        },
        "Mixolydian / Rock Pentatonic (1 2 3 4 5 6 b7)",
        { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 2, 0 },
        {
            /* steps:   1  &  2  &  3  &  4  &  */
            /* KICK */ { 2, 0, 0, 1, 2, 0, 0, 0 },
            /* SNARE*/ { 0, 0, 2, 0, 0, 0, 2, 0 },
            /* CLHAT*/ { 2, 1, 2, 1, 2, 1, 2, 0 },
            /* OPHAT*/ { 0, 0, 0, 0, 0, 0, 0, 1 },
            /* BASS */ { 1, 1, 1, 3, 1, 1, 6, 3 }  /* Driving 8th Rock Bass: R-R-R-5 R-R-8-5 */
        }
    },

    /* 3: Rock Ballad 3/4 (3/4 Time, i - bVI - bVII - V7) */
    {
        "Rock Ballad 3/4", "ROCK 3/4", 3, 6, 98, 50, 4,
        {
            {  0, CHQ_MIN,  "i"    },
            {  8, CHQ_MAJ,  "bVI"  },
            { 10, CHQ_MAJ,  "bVII" },
            {  7, CHQ_DOM7, "V7"   }
        },
        "Natural Minor / Aeolian (1 2 b3 4 5 b6 b7)",
        { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 },
        {
            /* steps:   1  &  2  &  3  &       */
            /* KICK */ { 2, 0, 0, 1, 0, 0, 0, 0 },
            /* SNARE*/ { 0, 0, 0, 0, 2, 0, 0, 0 },
            /* CLHAT*/ { 2, 1, 1, 1, 2, 0, 0, 0 },
            /* OPHAT*/ { 0, 0, 0, 0, 0, 1, 0, 0 },
            /* BASS */ { 1, 1, 3, 3, 6, 3, 0, 0 }  /* 3/4 Rock Bass: R - R - 5 - 5 - 8 - 5 */
        }
    },

    /* 4: Jazz Swing 4/4 (ii7 - V7 - Imaj7 - VI7, Walking Quarter-Note Bass) */
    {
        "Jazz Swing 4/4", "JAZZ 4/4", 4, 8, 126, 63, 4,
        {
            { 2, CHQ_MIN7, "ii7"   },
            { 7, CHQ_DOM7, "V7"    },
            { 0, CHQ_MAJ7, "Imaj7" },
            { 9, CHQ_DOM7, "VI7"   }
        },
        "Major / Jazz Bebop Modes (1 2 3 4 5 6 7)",
        { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 },
        {
            /* steps:   1  &  2  &  3  &  4  &  */
            /* KICK */ { 1, 0, 0, 0, 1, 0, 0, 0 },
            /* SNARE*/ { 0, 0, 0, 1, 0, 0, 0, 1 },
            /* CLHAT*/ { 0, 0, 2, 0, 0, 0, 2, 0 }, /* Hi-hat pedal chick on Beats 2 and 4 */
            /* OPHAT*/ { 2, 0, 1, 1, 2, 0, 1, 1 }, /* Classic Jazz Ride spang-a-lang: 1, 2 &, 3, 4 & */
            /* BASS */ { 1, 0, 2, 0, 3, 0, 5, 0 }  /* Quarter-note Walking Jazz Bass: R - 3 - 5 - b7 */
        }
    },

    /* 5: Jazz Waltz 3/4 (Imaj7 - vi7 - ii7 - V7 in 3/4 Swing) */
    {
        "Jazz Waltz 3/4", "JAZZ 3/4", 3, 6, 132, 60, 4,
        {
            { 0, CHQ_MAJ7, "Imaj7" },
            { 9, CHQ_MIN7, "vi7"   },
            { 2, CHQ_MIN7, "ii7"   },
            { 7, CHQ_DOM7, "V7"    }
        },
        "Major / Lydian Waltz (1 2 3 4 5 6 7)",
        { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 },
        {
            /* steps:   1  &  2  &  3  &       */
            /* KICK */ { 1, 0, 0, 0, 0, 0, 0, 0 },
            /* SNARE*/ { 0, 0, 0, 1, 0, 0, 0, 0 },
            /* CLHAT*/ { 0, 0, 2, 0, 0, 0, 0, 0 }, /* Hi-hat chick on Beat 2 */
            /* OPHAT*/ { 2, 0, 1, 1, 1, 0, 0, 0 }, /* Jazz Waltz Ride: 1, 2 &, 3 */
            /* BASS */ { 1, 0, 2, 0, 3, 4, 0, 0 }  /* 3/4 Walking Waltz Bass: R . 3 . 5 6 */
        }
    }
};

static const char *k_note_names[12] = {
    "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

/* Standard guitar string pitch classes from top row (high e) to bottom row (low E) */
static const char *k_gtr_str_lbl[6] = { "e", "B", "G", "D", "A", "E" };
static const int k_gtr_str_row_pc[6] = { 4, 11, 7, 2, 9, 4 }; /* Top row = high e, bottom row = low E */

/* =========================================================================
 * Application State & Editable Pattern Grid
 * ========================================================================= */
typedef enum {
    VIEW_JAM         = 0,
    VIEW_STEP_EDITOR = 1
} ViewMode;

typedef enum {
    MIX_FULL_BAND = 0, /* Drums + Bass + Chord Comping */
    MIX_DRUM_BASS = 1, /* Drums + Bassline */
    MIX_DRUMS_ONLY= 2  /* Drums Only */
} BackingMixMode;

static ViewMode       g_view         = VIEW_JAM;
static BackingMixMode g_mix_mode     = MIX_FULL_BAND;
static bool           g_show_help    = false;

static int            g_style_idx    = 0;   /* Default: Blues Shuffle 4/4 (in Key of A) */
static int            g_key_pc       = 9;   /* Default Key: A (9) — guitarists' favorite jam key */
static int            g_bpm          = 110;
static int            g_swing_pct    = 65;
static bool           g_auto_fill    = true;
static bool           g_fill_queued  = false;

static uint8_t        g_pattern[NUM_STYLES][NUM_TRACKS][MAX_STEPS];

/* Sequencer Transport State */
static volatile bool  g_playing      = true;
static volatile int   g_cur_bar      = 0;
static volatile int   g_cur_step     = 0;

/* Step Editor Cursor */
static int            g_edit_track   = 0;
static int            g_edit_step    = 0;

static char           g_config_path[512] = "";

static void format_chord_label(int key_pc, const BarChord *bc, char *buf, size_t sz) {
    int pc = (key_pc + bc->root_offset) % 12;
    const char *suf = "";
    switch (bc->qual) {
        case CHQ_MAJ:  suf = "";     break;
        case CHQ_MIN:  suf = "m";    break;
        case CHQ_DOM7: suf = "7";    break;
        case CHQ_MAJ7: suf = "maj7"; break;
        case CHQ_MIN7: suf = "m7";   break;
    }
    snprintf(buf, sz, "%s%s", k_note_names[pc], suf);
}

/* =========================================================================
 * Real-Time Procedural Fixed-Point Audio Engine (Drums + Bass + Chords)
 * ========================================================================= */
typedef struct {
    /* Sequencer sample clock */
    uint32_t step_sample_pos;
    uint32_t step_sample_len;

    /* 1. Kick Synthesizer */
    int      kick_rem;
    int      kick_total;
    uint32_t kick_phase;
    int      kick_amp;

    /* 2. Snare Synthesizer */
    int      snare_rem;
    int      snare_total;
    uint32_t snare_phase1;
    uint32_t snare_phase2;
    int      snare_amp;

    /* 3. Closed & Open Hi-Hat / Ride Synthesizer */
    int      hat_rem;
    int      hat_total;
    int      hat_amp;
    bool     hat_is_open;
    uint32_t hat_phases[6];
    int32_t  hat_prev_in;

    /* 4. Electric Bass Synthesizer */
    int      bass_rem;
    int      bass_total;
    uint32_t bass_phase;
    uint32_t bass_inc;
    int32_t  bass_lp_state;
    int      bass_amp;

    /* 5. Rhythm Chord Comping Synthesizer (3 voices) */
    int      chord_rem;
    int      chord_total;
    uint32_t chord_phases[3];
    uint32_t chord_incs[3];
    int      chord_amp;

    uint32_t lfsr;
} SynthState;

static SynthState g_synth;

static void maximize_hardware_volume(void) {
#if !defined(HOST_X11)
    int fd = open("/dev/mixer", O_RDWR);
    if (fd >= 0) {
        int vol = (95 << 8) | 95;
        ioctl(fd, SOUND_MIXER_WRITE_VOLUME, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_PCM, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_SPEAKER, &vol);
        close(fd);
    }
#endif
}

static uint32_t midi_to_phase_inc(int midi_note) {
    double freq = 440.0 * pow(2.0, (double)(midi_note - 69) / 12.0);
    return (uint32_t)((freq * 4294967296.0) / (double)SAMPLE_RATE);
}

static uint32_t compute_step_samples(int step_idx) {
    /* 1 beat (quarter note) = 60 / BPM seconds; 1 eighth-note step = 0.5 beat on average.
     * With swing_pct (50..68):
     *   Even steps (on the beat: 0, 2, 4, 6) have duration: 1.0 * beat * (swing_pct / 50.0) * 0.5
     *   Odd steps  (on the '&' : 1, 3, 5, 7) have duration: 1.0 * beat * ((100 - swing_pct) / 50.0) * 0.5
     */
    int bpm = g_bpm;
    if (bpm < 40) bpm = 40;
    if (bpm > 240) bpm = 240;
    double beat_samples = ((double)SAMPLE_RATE * 60.0) / (double)bpm;
    double ratio = (step_idx % 2 == 0) ? ((double)g_swing_pct / 50.0)
                                       : ((double)(100 - g_swing_pct) / 50.0);
    uint32_t len = (uint32_t)(beat_samples * 0.5 * ratio);
    return (len < 1000) ? 1000 : len;
}

static void trigger_step_voices(int style_id, int bar_idx, int step_idx) {
    const StylePreset *st = &k_styles[style_id];
    const BarChord *bc = &st->bars[bar_idx % st->num_bars];
    int chord_root_pc = (g_key_pc + bc->root_offset) % 12;

    bool is_fill_bar = ((bar_idx + 1) % 4 == 0 && g_auto_fill) || g_fill_queued;
    int last_two = st->steps_per_bar - 2;

    uint8_t kick_hit  = g_pattern[style_id][TRK_KICK][step_idx];
    uint8_t snare_hit = g_pattern[style_id][TRK_SNARE][step_idx];
    uint8_t clhat_hit = g_pattern[style_id][TRK_CLHAT][step_idx];
    uint8_t ophat_hit = g_pattern[style_id][TRK_OPHAT][step_idx];
    uint8_t bass_deg  = g_pattern[style_id][TRK_BASS][step_idx];

    /* Tasteful turnaround drum fill on the last beat ('4 &' or '3 &') of a fill bar */
    if (is_fill_bar && step_idx >= last_two) {
        snare_hit = (step_idx == st->steps_per_bar - 1) ? 2 : 1;
        if (step_idx == last_two) kick_hit = 1;
    }
    if (step_idx == st->steps_per_bar - 1 && g_fill_queued) {
        g_fill_queued = false;
    }

    /* 1. Trigger Kick */
    if (kick_hit > 0) {
        g_synth.kick_total = (SAMPLE_RATE * 130) / 1000;
        g_synth.kick_rem   = g_synth.kick_total;
        g_synth.kick_phase = 0;
        g_synth.kick_amp   = (kick_hit == 2) ? 15000 : 11500;
    }

    /* 2. Trigger Snare */
    if (snare_hit > 0) {
        g_synth.snare_total = (SAMPLE_RATE * 145) / 1000;
        g_synth.snare_rem   = g_synth.snare_total;
        g_synth.snare_phase1 = 0;
        g_synth.snare_phase2 = 0;
        g_synth.snare_amp   = (snare_hit == 2) ? 12500 : 9000;
    }

    /* 3. Trigger Open or Closed Hi-Hat (Closed Hat chokes Open Hat) */
    if (ophat_hit > 0) {
        g_synth.hat_total   = (SAMPLE_RATE * 230) / 1000;
        g_synth.hat_rem     = g_synth.hat_total;
        g_synth.hat_amp     = (ophat_hit == 2) ? 6200 : 4600;
        g_synth.hat_is_open = true;
    } else if (clhat_hit > 0) {
        g_synth.hat_total   = (SAMPLE_RATE * 48) / 1000;
        g_synth.hat_rem     = g_synth.hat_total;
        g_synth.hat_amp     = (clhat_hit == 2) ? 5800 : 4000;
        g_synth.hat_is_open = false;
    }

    /* 4. Trigger Electric Bass Note (if mix includes Bass) */
    if (g_mix_mode != MIX_DRUMS_ONLY && bass_deg > 0) {
        int semitone_off = 0;
        int third_semi = (bc->qual == CHQ_MIN || bc->qual == CHQ_MIN7) ? 3 : 4;
        switch (bass_deg) {
            case 1: semitone_off = 0;          break; /* R  */
            case 2: semitone_off = third_semi; break; /* 3  */
            case 3: semitone_off = 7;          break; /* 5  */
            case 4: semitone_off = 9;          break; /* 6  */
            case 5: semitone_off = 10;         break; /* b7 */
            case 6: semitone_off = 12;         break; /* 8  */
        }
        /* Keep bass root in E1 (MIDI 28) .. Eb2 (MIDI 39) range */
        int base_midi = 28 + ((chord_root_pc - 4 + 12) % 12);
        int midi_note = base_midi + semitone_off;

        g_synth.bass_total = (SAMPLE_RATE * 320) / 1000;
        g_synth.bass_rem   = g_synth.bass_total;
        g_synth.bass_inc   = midi_to_phase_inc(midi_note);
        g_synth.bass_amp   = (step_idx == 0) ? 10500 : 8800;
    }

    /* 5. Trigger Rhythm Chord Comping Stab (if mix is MIX_FULL_BAND) */
    if (g_mix_mode == MIX_FULL_BAND) {
        bool comp_hit = (step_idx == 0) ||
                        (st->beats_per_bar == 4 && step_idx == 3) ||
                        (st->beats_per_bar == 3 && step_idx == 2);
        if (comp_hit) {
            int root_midi = 48 + chord_root_pc; /* C3..B3 range */
            int third_semi = (bc->qual == CHQ_MIN || bc->qual == CHQ_MIN7) ? 3 : 4;
            int sev_semi   = (bc->qual == CHQ_MAJ7) ? 11 :
                             (bc->qual == CHQ_DOM7 || bc->qual == CHQ_MIN7) ? 10 : 7;

            g_synth.chord_incs[0] = midi_to_phase_inc(root_midi);
            g_synth.chord_incs[1] = midi_to_phase_inc(root_midi + third_semi);
            g_synth.chord_incs[2] = midi_to_phase_inc(root_midi + sev_semi);
            g_synth.chord_total   = (SAMPLE_RATE * ((step_idx == 0) ? 280 : 160)) / 1000;
            g_synth.chord_rem     = g_synth.chord_total;
            g_synth.chord_amp     = (step_idx == 0) ? 3800 : 3000;
        }
    }
}

/* Pre-computed 256-entry sine lookup table (-32767 .. +32767) for zero-FPU synthesis */
static int16_t g_sine_lut[256];

static void init_synth_tables(void) {
    memset(&g_synth, 0, sizeof(g_synth));
    g_synth.lfsr = 0x12345678u;
    for (int i = 0; i < 256; i++) {
        g_sine_lut[i] = (int16_t)(sin((2.0 * M_PI * (double)i) / 256.0) * 32767.0);
    }
    g_synth.step_sample_len = compute_step_samples(0);
    g_synth.step_sample_pos = 0;
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int frames = len / (2 * AUDIO_CHANNELS);

    if (!g_playing) {
        memset(stream, 0, (size_t)len);
        return;
    }

    /* 6 TR-808 metallic square-wave phase increments */
    static const uint32_t k_hat_incs[6] = {
        20000000u, 29600000u, 36000000u, 50800000u, 52600000u, 77900000u
    };

    for (int f = 0; f < frames; f++) {
        if (g_synth.step_sample_pos == 0) {
            trigger_step_voices(g_style_idx, g_cur_bar, g_cur_step);
        }
        g_synth.step_sample_pos++;
        if (g_synth.step_sample_pos >= g_synth.step_sample_len) {
            g_synth.step_sample_pos = 0;
            int spb = k_styles[g_style_idx].steps_per_bar;
            int nbars = k_styles[g_style_idx].num_bars;
            int next_step = g_cur_step + 1;
            if (next_step >= spb) {
                next_step = 0;
                g_cur_bar = (g_cur_bar + 1) % nbars;
            }
            g_cur_step = next_step;
            g_synth.step_sample_len = compute_step_samples(g_cur_step);
        }

        int32_t mix = 0;

        /* 1. Kick Drum: Pitch-swept sine (155 Hz -> 44 Hz) */
        if (g_synth.kick_rem > 0) {
            int elapsed = g_synth.kick_total - g_synth.kick_rem;
            int freq_hz = 44 + ((115 * g_synth.kick_rem) / g_synth.kick_total);
            if (elapsed < (SAMPLE_RATE * 18) / 1000) {
                freq_hz += 60;
            }
            g_synth.kick_phase += (uint32_t)((uint64_t)freq_hz * 97391u);
            int16_t s = g_sine_lut[g_synth.kick_phase >> 24];
            int32_t env = (g_synth.kick_rem * g_synth.kick_amp) / g_synth.kick_total;
            mix += (s * env) >> 15;
            g_synth.kick_rem--;
        }

        /* 2. Snare Drum: Dual body tone + HPF wire noise */
        if (g_synth.snare_rem > 0) {
            g_synth.snare_phase1 += (uint32_t)(185u * 97391u);
            g_synth.snare_phase2 += (uint32_t)(330u * 97391u);
            int32_t body = (g_sine_lut[g_synth.snare_phase1 >> 24] +
                            g_sine_lut[g_synth.snare_phase2 >> 24]) >> 1;

            g_synth.lfsr ^= g_synth.lfsr << 13;
            g_synth.lfsr ^= g_synth.lfsr >> 17;
            g_synth.lfsr ^= g_synth.lfsr << 5;
            int16_t noise = (int16_t)(g_synth.lfsr >> 16);

            int32_t env = (g_synth.snare_rem * g_synth.snare_amp) / g_synth.snare_total;
            int32_t body_env = (g_synth.snare_rem > (g_synth.snare_total >> 1)) ? env : (env >> 1);
            mix += ((noise * env) >> 15) + ((body * body_env) >> 16);
            g_synth.snare_rem--;
        }

        /* 3. Closed / Open Hi-Hat: 6-Oscillator Metallic Bank + High-Pass Filter */
        if (g_synth.hat_rem > 0) {
            int32_t sq_sum = 0;
            for (int o = 0; o < 6; o++) {
                g_synth.hat_phases[o] += k_hat_incs[o];
                sq_sum += (g_synth.hat_phases[o] & 0x80000000u) ? 4096 : -4096;
            }
            g_synth.lfsr ^= g_synth.lfsr << 13;
            g_synth.lfsr ^= g_synth.lfsr >> 17;
            g_synth.lfsr ^= g_synth.lfsr << 5;
            int32_t raw_in = sq_sum + ((int16_t)(g_synth.lfsr >> 16) >> 2);
            int32_t hpf = raw_in - g_synth.hat_prev_in;
            g_synth.hat_prev_in = raw_in;

            int32_t env = (g_synth.hat_rem * g_synth.hat_amp) / g_synth.hat_total;
            mix += (hpf * env) >> 15;
            g_synth.hat_rem--;
        }

        /* 4. Electric Bass: Plucked Low-Pass Filtered Saw + Fundamental Sub */
        if (g_synth.bass_rem > 0) {
            g_synth.bass_phase += g_synth.bass_inc;
            int16_t saw = (int16_t)((int32_t)(g_synth.bass_phase >> 16) - 32768);
            int16_t sub = g_sine_lut[g_synth.bass_phase >> 24];

            /* Dynamic filter brightness envelope (plucky attack, warm sustain) */
            int alpha = 32 + ((g_synth.bass_rem * 96) / g_synth.bass_total);
            g_synth.bass_lp_state += ((saw - g_synth.bass_lp_state) * alpha) >> 8;

            int32_t voice = (g_synth.bass_lp_state + sub) >> 1;
            int32_t env = (g_synth.bass_rem * g_synth.bass_amp) / g_synth.bass_total;
            mix += (voice * env) >> 15;
            g_synth.bass_rem--;
        }

        /* 5. Rhythm Chord Comping: Warm 3-Voice Organ/Guitar Stab */
        if (g_synth.chord_rem > 0) {
            int32_t csum = 0;
            for (int v = 0; v < 3; v++) {
                g_synth.chord_phases[v] += g_synth.chord_incs[v];
                uint8_t idx = (uint8_t)(g_synth.chord_phases[v] >> 24);
                csum += g_sine_lut[idx] + (g_sine_lut[(uint8_t)(idx << 1)] >> 1);
            }
            int32_t env = (g_synth.chord_rem * g_synth.chord_amp) / g_synth.chord_total;
            mix += ((csum / 3) * env) >> 15;
            g_synth.chord_rem--;
        }

        if (mix > 32767)  mix = 32767;
        if (mix < -32767) mix = -32767;
        int16_t sample = (int16_t)mix;
        for (int c = 0; c < AUDIO_CHANNELS; c++) {
            out[f * AUDIO_CHANNELS + c] = sample;
        }
    }
}

/* =========================================================================
 * Persistent Configuration (/mnt/.pocketband.cfg or ~/.pocketband.cfg)
 * ========================================================================= */
static void init_default_patterns(void) {
    for (int s = 0; s < NUM_STYLES; s++) {
        for (int t = 0; t < NUM_TRACKS; t++) {
            for (int st = 0; st < MAX_STEPS; st++) {
                g_pattern[s][t][st] = k_styles[s].default_steps[t][st];
            }
        }
    }
}

static void apply_style(int style_idx) {
    style_idx = ((style_idx % NUM_STYLES) + NUM_STYLES) % NUM_STYLES;
    g_style_idx = style_idx;
    g_bpm       = k_styles[style_idx].default_bpm;
    g_swing_pct = k_styles[style_idx].swing_pct;
    g_cur_bar   = 0;
    g_cur_step  = 0;
    if (g_edit_step >= k_styles[style_idx].steps_per_bar) {
        g_edit_step = 0;
    }
    g_synth.step_sample_pos = 0;
    g_synth.step_sample_len = compute_step_samples(0);
}

static void resolve_config_path(void) {
    const char *env_cfg = getenv("POCKETBAND_CFG");
    if (env_cfg && env_cfg[0] != '\0') {
        snprintf(g_config_path, sizeof(g_config_path), "%s", env_cfg);
        return;
    }
    struct stat st;
    if (stat("/mnt", &st) == 0 && S_ISDIR(st.st_mode) && access("/mnt", W_OK) == 0) {
        snprintf(g_config_path, sizeof(g_config_path), "/mnt/.pocketband.cfg");
    } else {
        const char *home = getenv("HOME");
        if (home && access(home, W_OK) == 0) {
            snprintf(g_config_path, sizeof(g_config_path), "%s/.pocketband.cfg", home);
        } else {
            snprintf(g_config_path, sizeof(g_config_path), "./.pocketband.cfg");
        }
    }
}

static void save_config(void) {
    if (g_config_path[0] == '\0') return;
    FILE *f = fopen(g_config_path, "w");
    if (!f) return;
    fprintf(f, "style_idx=%d\n", g_style_idx);
    fprintf(f, "key_pc=%d\n", g_key_pc);
    fprintf(f, "bpm=%d\n", g_bpm);
    fprintf(f, "swing_pct=%d\n", g_swing_pct);
    fprintf(f, "mix_mode=%d\n", (int)g_mix_mode);
    fprintf(f, "auto_fill=%d\n", g_auto_fill ? 1 : 0);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

static void load_config(void) {
    init_default_patterns();
    apply_style(0);
    if (g_config_path[0] == '\0') return;
    FILE *f = fopen(g_config_path, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int v = 0;
        if (sscanf(line, "style_idx=%d", &v) == 1 && v >= 0 && v < NUM_STYLES) {
            apply_style(v);
        } else if (sscanf(line, "key_pc=%d", &v) == 1 && v >= 0 && v < 12) {
            g_key_pc = v;
        } else if (sscanf(line, "bpm=%d", &v) == 1 && v >= 45 && v <= 220) {
            g_bpm = v;
        } else if (sscanf(line, "swing_pct=%d", &v) == 1 && v >= 50 && v <= 68) {
            g_swing_pct = v;
        } else if (sscanf(line, "mix_mode=%d", &v) == 1 && v >= 0 && v <= 2) {
            g_mix_mode = (BackingMixMode)v;
        } else if (sscanf(line, "auto_fill=%d", &v) == 1) {
            g_auto_fill = (v != 0);
        }
    }
    fclose(f);
}

/* =========================================================================
 * UI Rendering: Top Bar, Guitarist Jam View, Step Editor & Help Modal
 * ========================================================================= */
static void render_top_status_bar(void) {
    const StylePreset *st = &k_styles[g_style_idx];
    fill_rect(0, 0, SCREEN_W, 22, RGB(21, 30, 48));
    fill_rect(0, 21, SCREEN_W, 1, RGB(56, 78, 112));

    /* Mode Badge */
    draw_pill(4, 3, 54, 15,
              (g_view == VIEW_JAM) ? RGB(37, 99, 235) : RGB(124, 58, 237),
              (g_view == VIEW_JAM) ? RGB(147, 197, 253) : RGB(196, 181, 253));
    draw_text_centered(FONT_UI_9, 4, 3, 54, 15,
                       (g_view == VIEW_JAM) ? "JAM VIEW" : "STEP EDIT",
                       RGB(255, 255, 255));

    /* Genre / Style Pill */
    draw_pill(61, 3, 108, 15, RGB(30, 41, 59), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_9, 61, 3, 108, 15, st->name, RGB(254, 240, 138));

    /* Key Badge */
    char key_str[16];
    snprintf(key_str, sizeof(key_str), "KEY %s", k_note_names[g_key_pc]);
    draw_pill(172, 3, 46, 15, RGB(124, 45, 18), RGB(251, 146, 60));
    draw_text_centered(FONT_UI_9, 172, 3, 46, 15, key_str, RGB(255, 255, 255));

    /* Tempo BPM Badge */
    char bpm_str[20];
    snprintf(bpm_str, sizeof(bpm_str), "%d BPM", g_bpm);
    draw_pill(221, 3, 52, 15, RGB(23, 37, 58), RGB(71, 98, 145));
    draw_text_centered(FONT_UI_9, 221, 3, 52, 15, bpm_str, RGB(186, 230, 253));

    /* Play / Pause Badge */
    draw_pill(276, 3, 40, 15,
              g_playing ? RGB(16, 185, 129) : RGB(180, 83, 9),
              g_playing ? RGB(110, 231, 183) : RGB(251, 191, 36));
    draw_text_centered(FONT_UI_9, 276, 3, 40, 15,
                       g_playing ? "PLAY" : "STOP", RGB(255, 255, 255));
}

static void render_step_strip_and_footer(void) {
    const StylePreset *st = &k_styles[g_style_idx];
    int spb = st->steps_per_bar;

    /* Beat & '&' Step Strip (y = 191..216) */
    fill_rect(4, 191, SCREEN_W - 8, 25, RGB(19, 27, 43));
    draw_border(4, 191, SCREEN_W - 8, 25, RGB(45, 62, 92));

    char meter_lbl[16];
    snprintf(meter_lbl, sizeof(meter_lbl), "%d/4 STEP", st->beats_per_bar);
    draw_text(FONT_UI_9, 9, 198, meter_lbl, RGB(148, 163, 184));

    int strip_x = 64;
    int avail_w = SCREEN_W - strip_x - 54;
    int box_w = (avail_w - (spb - 1) * 3) / spb;

    for (int s = 0; s < spb; s++) {
        int bx = strip_x + s * (box_w + 3);
        bool active = g_playing && (g_cur_step == s);
        bool is_beat = (s % 2 == 0);

        uint32_t fill = active ? (is_beat ? RGB(245, 158, 11) : RGB(16, 185, 129))
                               : (is_beat ? RGB(30, 43, 68)  : RGB(22, 31, 49));
        uint32_t bdr  = active ? RGB(255, 255, 255)
                               : (is_beat ? RGB(96, 165, 250) : RGB(51, 68, 98));
        draw_pill(bx, 194, box_w, 19, fill, bdr);

        char slbl[16];
        if (is_beat) {
            snprintf(slbl, sizeof(slbl), "%d", (s / 2) + 1);
        } else {
            snprintf(slbl, sizeof(slbl), "&");
        }
        draw_text_centered(FONT_UI_10, bx, 194, box_w, 19, slbl,
                           active ? RGB(15, 23, 42) : (is_beat ? RGB(248, 250, 252) : RGB(148, 163, 184)));
    }

    /* Fill status pill on right of step strip */
    bool fill_now = (((g_cur_bar + 1) % 4 == 0 && g_auto_fill) || g_fill_queued);
    draw_pill(SCREEN_W - 46, 194, 38, 19,
              fill_now ? RGB(190, 24, 93) : RGB(28, 38, 58),
              fill_now ? RGB(244, 114, 182) : RGB(64, 82, 115));
    draw_text_centered(FONT_UI_9, SCREEN_W - 46, 194, 38, 19,
                       fill_now ? "FILL!" : "B:FILL", RGB(255, 255, 255));

    /* Bottom Bar (y = 219..239) */
    fill_rect(0, 219, SCREEN_W, 21, RGB(15, 22, 35));
    fill_rect(0, 219, SCREEN_W, 1, RGB(45, 62, 92));

    const char *mix_lbl = (g_mix_mode == MIX_FULL_BAND) ? "X:Band(D+B+Chd)" :
                          (g_mix_mode == MIX_DRUM_BASS) ? "X:Drums+Bass"    :
                                                          "X:Drums Only";
    draw_pill(4, 222, 96, 15, RGB(23, 52, 74), RGB(56, 189, 248));
    draw_text_centered(FONT_UI_9, 4, 222, 96, 15, mix_lbl, RGB(224, 242, 254));

    if (g_view == VIEW_JAM) {
        draw_text(FONT_UI_9, 105, 224, "L/R:Style U/D:Key SEL:Edit", RGB(203, 213, 225));
    } else {
        draw_text(FONT_UI_9, 105, 224, "A:Hit/Note B:Clr SEL:Jam", RGB(203, 213, 225));
    }

    /* Discrete Y: Help Badge in bottom-right corner */
    draw_pill(SCREEN_W - 48, 222, 44, 15, RGB(30, 58, 95), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_9, SCREEN_W - 48, 222, 44, 15, "Y: Help", RGB(254, 240, 138));
}

static void render_jam_view(void) {
    const StylePreset *st = &k_styles[g_style_idx];
    int bar = g_cur_bar % st->num_bars;
    int next_bar = (bar + 1) % st->num_bars;

    const BarChord *cur_bc  = &st->bars[bar];
    const BarChord *next_bc = &st->bars[next_bar];

    char cur_chord_str[24], next_chord_str[24];
    format_chord_label(g_key_pc, cur_bc, cur_chord_str, sizeof(cur_chord_str));
    format_chord_label(g_key_pc, next_bc, next_chord_str, sizeof(next_chord_str));

    /* 1. Left Card: NOW PLAYING CHORD & BAR COUNTER (x=4..160, y=25..95) */
    fill_rect(4, 25, 156, 70, RGB(21, 30, 48));
    draw_border(4, 25, 156, 70, RGB(56, 189, 248));

    char bar_hdr[32];
    snprintf(bar_hdr, sizeof(bar_hdr), "NOW: BAR %d OF %d", bar + 1, st->num_bars);
    draw_text(FONT_UI_9, 10, 29, bar_hdr, RGB(125, 211, 252));
    draw_pill(116, 28, 38, 14, RGB(146, 92, 12), RGB(250, 204, 21));
    draw_text_centered(FONT_UI_9, 116, 28, 38, 14, cur_bc->roman, RGB(255, 255, 255));

    draw_text_centered(FONT_CHORD_24, 6, 43, 152, 32, cur_chord_str, RGB(250, 204, 21));

    /* Show active bass note sequence on bottom of Now Playing card */
    char bass_seq[48] = "Bass: ";
    for (int s = 0; s < st->steps_per_bar; s++) {
        uint8_t d = g_pattern[g_style_idx][TRK_BASS][s];
        strncat(bass_seq, k_bass_deg_labels[d], sizeof(bass_seq) - strlen(bass_seq) - 1);
        if (s + 1 < st->steps_per_bar) {
            strncat(bass_seq, " ", sizeof(bass_seq) - strlen(bass_seq) - 1);
        }
    }
    draw_text_centered(FONT_UI_9, 6, 78, 152, 13, bass_seq, RGB(134, 239, 172));

    /* 2. Right Card: NEXT CHORD & PROGRESSION STRIP (x=164..315, y=25..95) */
    fill_rect(164, 25, 152, 70, RGB(21, 30, 48));
    draw_border(164, 25, 152, 70, RGB(51, 68, 98));

    char next_hdr[64];
    snprintf(next_hdr, sizeof(next_hdr), "NEXT (BAR %d): %s", next_bar + 1, next_chord_str);
    draw_text(FONT_UI_10, 170, 29, next_hdr, RGB(248, 250, 252));

    char swg_str[40];
    snprintf(swg_str, sizeof(swg_str), "Meter: %d/4  |  Swing: %d%%", st->beats_per_bar, g_swing_pct);
    draw_text(FONT_UI_9, 170, 44, swg_str, RGB(148, 163, 184));

    /* 4-Bar Window Preview inside Right Card */
    int win_start = (bar / 4) * 4;
    int cell_w = 34;
    for (int i = 0; i < 4; i++) {
        int bidx = (win_start + i) % st->num_bars;
        char cstr[16];
        format_chord_label(g_key_pc, &st->bars[bidx], cstr, sizeof(cstr));
        bool is_cur = (bidx == bar);
        int cx = 170 + i * (cell_w + 2);
        draw_pill(cx, 60, cell_w, 28,
                  is_cur ? RGB(37, 99, 235) : RGB(15, 22, 34),
                  is_cur ? RGB(250, 204, 21) : RGB(51, 68, 98));
        draw_text_centered(FONT_UI_9, cx, 62, cell_w, 12, cstr,
                           is_cur ? RGB(254, 240, 138) : RGB(226, 232, 240));
        draw_text_centered(FONT_UI_9, cx, 74, cell_w, 12, st->bars[bidx].roman,
                           is_cur ? RGB(186, 230, 253) : RGB(120, 136, 158));
    }

    /* 3. 12-Fret Guitar Soloing Scale Map (x=4..315, y=98..188) */
    fill_rect(4, 98, SCREEN_W - 8, 90, RGB(21, 30, 48));
    draw_border(4, 98, SCREEN_W - 8, 90, RGB(51, 68, 98));

    char sc_hdr[80];
    snprintf(sc_hdr, sizeof(sc_hdr), "SOLO SCALE: %s %s", k_note_names[g_key_pc], st->scale_name);
    draw_text(FONT_UI_9, 10, 101, sc_hdr, RGB(250, 204, 21));

    int neck_x = 24;
    int neck_y = 123;
    int fret_w = 23; /* 12 frets * 23 = 276 px */
    int str_h  = 10; /* 5 string spaces * 10 = 50 px */
    int neck_w = fret_w * 12;
    int neck_h = str_h * 5;

    /* Fretboard wood + nut + fret wires */
    fill_rect(neck_x, neck_y, neck_w, neck_h + 1, RGB(35, 28, 24));
    fill_rect(neck_x, neck_y - 1, 3, neck_h + 3, RGB(241, 245, 249));

    for (int f = 0; f <= 12; f++) {
        int fx = neck_x + f * fret_w;
        if (f > 0) {
            fill_rect(fx, neck_y, 1, neck_h + 1, RGB(110, 120, 135));
        }
        if (f == 0 || f == 3 || f == 5 || f == 7 || f == 9 || f == 12) {
            char fn[8];
            snprintf(fn, sizeof(fn), "%d", f);
            int lx = (f == 0) ? (neck_x - 4) : (fx - fret_w / 2);
            draw_text_centered(FONT_UI_9, lx - 8, neck_y - 11, 16, 10, fn, RGB(148, 163, 184));
        }
    }

    int cur_chord_pc = (g_key_pc + cur_bc->root_offset) % 12;

    /* 6 strings + scale/root dots */
    for (int r = 0; r < 6; r++) {
        int sy = neck_y + r * str_h;
        fill_rect(neck_x, sy, neck_w, 1, (r >= 3) ? RGB(190, 200, 215) : RGB(140, 150, 165));
        draw_text(FONT_UI_9, 9, sy - 5, k_gtr_str_lbl[r], RGB(148, 163, 184));

        int open_pc = k_gtr_str_row_pc[r];
        for (int f = 0; f <= 12; f++) {
            int pc = (open_pc + f) % 12;
            int rel = (pc - g_key_pc + 12) % 12;
            uint8_t mask = st->scale_mask[rel];
            if (mask > 0) {
                float cx = (f == 0) ? (float)(neck_x - 3) : (float)(neck_x + f * fret_w - fret_w / 2);
                float cy = (float)sy;
                bool is_key_root   = (pc == g_key_pc);
                bool is_chord_root = (pc == cur_chord_pc);
                uint32_t dfill = is_key_root   ? RGB(245, 158, 11) :
                                 is_chord_root ? RGB(16, 185, 129) :
                                 (mask == 2)   ? RGB(236, 72, 153) : RGB(14, 165, 233);
                uint32_t dbdr  = is_key_root   ? RGB(254, 240, 138) : RGB(224, 242, 254);
                draw_filled_circle_aa(cx, cy, is_key_root ? 4.0f : 3.2f, dfill, dbdr);
            }
        }
    }
}

static void render_step_editor_view(void) {
    const StylePreset *st = &k_styles[g_style_idx];
    int spb = st->steps_per_bar;

    fill_rect(4, 25, SCREEN_W - 8, 163, RGB(21, 30, 48));
    draw_border(4, 25, SCREEN_W - 8, 163, RGB(51, 68, 98));

    /* Column Header Row: Track + Step Beat Labels (1 & 2 & 3 & [4 &]) */
    draw_text(FONT_UI_9, 10, 29, "TRACK", RGB(148, 163, 184));

    int grid_x = 64;
    int avail_w = SCREEN_W - grid_x - 10;
    int cell_w = (avail_w - (spb - 1) * 4) / spb;

    for (int s = 0; s < spb; s++) {
        int cx = grid_x + s * (cell_w + 4);
        bool is_beat = (s % 2 == 0);
        char hdr[16];
        if (is_beat) snprintf(hdr, sizeof(hdr), "%d", (s / 2) + 1);
        else         snprintf(hdr, sizeof(hdr), "&");
        draw_text_centered(FONT_UI_10, cx, 28, cell_w, 13, hdr,
                           is_beat ? RGB(250, 204, 21) : RGB(148, 163, 184));
    }

    /* 5 Track Rows (KICK, SNARE, CL HAT, OP HAT, BASS) */
    int row_y0 = 44;
    int row_h  = 27;

    for (int t = 0; t < NUM_TRACKS; t++) {
        int ry = row_y0 + t * row_h;
        bool trk_sel = (g_edit_track == t);

        if (trk_sel) {
            fill_rect(6, ry - 1, 54, row_h - 3, RGB(30, 58, 95));
            draw_border(6, ry - 1, 54, row_h - 3, RGB(96, 165, 250));
        }
        draw_text(FONT_UI_9, 10, ry + 5, k_track_names[t],
                  trk_sel ? RGB(254, 240, 138) : RGB(226, 232, 240));

        for (int s = 0; s < spb; s++) {
            int cx = grid_x + s * (cell_w + 4);
            uint8_t val = g_pattern[g_style_idx][t][s];
            bool cursor = (g_edit_track == t && g_edit_step == s);
            bool play_col = g_playing && (g_cur_step == s);

            uint32_t fill = RGB(15, 22, 34);
            uint32_t bdr  = play_col ? RGB(147, 197, 253) : RGB(45, 62, 92);
            const char *txt = ".";
            uint32_t tcol = RGB(100, 116, 139);

            if (t == TRK_BASS) {
                if (val > 0 && val <= 6) {
                    fill = RGB(14, 116, 144);
                    bdr  = RGB(56, 189, 248);
                    txt  = k_bass_deg_labels[val];
                    tcol = RGB(255, 255, 255);
                }
            } else {
                if (val == 1) {
                    fill = RGB(16, 185, 129);
                    bdr  = RGB(110, 231, 183);
                    txt  = "ON";
                    tcol = RGB(15, 23, 42);
                } else if (val == 2) {
                    fill = RGB(245, 158, 11);
                    bdr  = RGB(254, 240, 138);
                    txt  = "ACC";
                    tcol = RGB(15, 23, 42);
                }
            }

            if (cursor) {
                bdr = RGB(250, 204, 21);
            }

            draw_pill(cx, ry, cell_w, row_h - 4, fill, bdr);
            if (cursor) {
                draw_border(cx, ry, cell_w, row_h - 4, RGB(250, 204, 21));
            }
            draw_text_centered(FONT_UI_9, cx, ry, cell_w, row_h - 4, txt, tcol);
        }
    }
}

static void render_help_modal(void) {
    int mx = 6, my = 10, mw = SCREEN_W - 12, mh = SCREEN_H - 20;
    fill_rect(mx, my, mw, mh, RGB(15, 23, 38));
    draw_border(mx, my, mw, mh, RGB(96, 165, 250));
    draw_border(mx + 1, my + 1, mw - 2, mh - 2, RGB(37, 99, 235));

    fill_rect(mx + 2, my + 2, mw - 4, 21, RGB(28, 39, 60));
    fill_rect(mx + 2, my + 23, mw - 4, 1, RGB(64, 82, 115));
    draw_pill(mx + 6, my + 4, 44, 15, RGB(37, 99, 235), RGB(147, 197, 253));
    draw_text_centered(FONT_UI_9, mx + 6, my + 4, 44, 15, "HELP", RGB(255, 255, 255));
    draw_text(FONT_UI_10, mx + 56, my + 6, "POCKETBAND CONTROLS & GROOVE", RGB(248, 250, 252));

    /* Groove & Feel Box */
    int gy = my + 27;
    fill_rect(mx + 6, gy, mw - 12, 36, RGB(23, 33, 52));
    draw_border(mx + 6, gy, mw - 12, 36, RGB(56, 189, 248));

    char g1[80];
    snprintf(g1, sizeof(g1), "Style: %s (%d/4)   |   Key: %s   |   %d BPM",
             k_styles[g_style_idx].name, k_styles[g_style_idx].beats_per_bar,
             k_note_names[g_key_pc], g_bpm);
    draw_text(FONT_UI_9, mx + 12, gy + 5, g1, RGB(250, 204, 21));

    char g2[80];
    snprintf(g2, sizeof(g2), "[L/R] Swing: %d%%   |   [A] 4-Bar Auto-Fill: %s",
             g_swing_pct, g_auto_fill ? "ON" : "OFF");
    draw_text(FONT_UI_9, mx + 12, gy + 20, g2, RGB(125, 211, 252));

    /* Controls Table */
    int ky = gy + 41;
    static const struct {
        const char *btn;
        const char *desc;
    } k_rows[7] = {
        { "SELECT",       "Switch Guitarist Jam View <-> Step Editor" },
        { "A Button",     "Jam: Play/Stop  |  Edit: Cycle Hit / Bass Note" },
        { "B Button",     "Jam: Queue Drum Fill  |  Edit: Clear Step" },
        { "X Button",     "Mix: Full Band -> Drums+Bass -> Drums Only" },
        { "LEFT / RIGHT", "Jam: Cycle Style (Rock/Blues/Jazz 4/4 & 3/4)" },
        { "UP / DOWN",    "Jam: Transpose Key (C .. B)" },
        { "L1/R1 / MENU", "L1/R1: Tempo -/+5 BPM  |  MENU: Save/Quit" }
    };

    for (int i = 0; i < 7; i++) {
        int ry = ky + i * 19;
        draw_pill(mx + 6, ry, 70, 15, RGB(30, 58, 95), RGB(71, 98, 145));
        draw_text_centered(FONT_UI_9, mx + 6, ry, 70, 15, k_rows[i].btn, RGB(254, 240, 138));
        draw_text(FONT_UI_9, mx + 82, ry + 3, k_rows[i].desc, RGB(226, 232, 240));
    }

    fill_rect(mx + 2, my + mh - 17, mw - 4, 15, RGB(12, 18, 30));
    draw_text_centered(FONT_UI_9, mx, my + mh - 16, mw, 14,
                       "Press Y to close Help and return to jamming",
                       RGB(186, 230, 253));
}

static void render_ui(SDL_Surface *screen) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(13, 18, 28));
    render_top_status_bar();
    if (g_view == VIEW_JAM) {
        render_jam_view();
    } else {
        render_step_editor_view();
    }
    render_step_strip_and_footer();
    if (g_show_help) {
        render_help_modal();
    }
    present_frame(screen);
}

/* =========================================================================
 * Main Entry Point & Event Loop
 * ========================================================================= */
int main(int argc, char *argv[]) {
    putenv((char *)"SDL_NOMOUSE=1");
    init_synth_tables();
    resolve_config_path();
    load_config();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "Failed to initialize SDL video: %s\n", SDL_GetError());
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
            SDL_PauseAudio(0);
            audio_ok = true;
        }
    }

    SDL_ShowCursor(SDL_DISABLE);
    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, SCREEN_BPP, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "Failed to set 320x240 video mode\n");
        SDL_Quit();
        return 1;
    }
    SDL_WM_SetCaption("PowKiddy V90 PocketBand (Drum & Bass Jam Looper)", NULL);

    /* Automated Screenshot Mode (--screenshot-states <dir>) */
    if (argc >= 3 && strcmp(argv[1], "--screenshot-states") == 0) {
        const char *dir = argv[2];
        char path[512];

        /* 1. Default Guitarist Jam View: Blues Shuffle 4/4 in Key of A */
        apply_style(0);
        g_key_pc = 9; /* Key of A */
        g_view = VIEW_JAM;
        g_mix_mode = MIX_FULL_BAND;
        g_playing = true;
        g_cur_bar = 0;
        g_cur_step = 2;
        g_show_help = false;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/pocketband_jam.bmp", dir);
        save_bmp(path);

        /* 2. Step Sequencer View (4/4 Classic Rock Drum & Bass Grid) */
        apply_style(2);
        g_key_pc = 4; /* Key of E */
        g_view = VIEW_STEP_EDITOR;
        g_edit_track = TRK_BASS;
        g_edit_step = 3;
        g_cur_step = 4;
        g_show_help = false;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/pocketband_steps.bmp", dir);
        save_bmp(path);

        /* 3. Jazz Waltz 3/4 Jam View in Key of C */
        apply_style(5);
        g_key_pc = 0; /* Key of C */
        g_view = VIEW_JAM;
        g_mix_mode = MIX_DRUM_BASS;
        g_cur_bar = 2;
        g_cur_step = 4;
        g_show_help = false;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/pocketband_waltz.bmp", dir);
        save_bmp(path);

        /* 4. Help & Groove Settings Modal */
        g_show_help = true;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/pocketband_help.bmp", dir);
        save_bmp(path);

        if (audio_ok) SDL_CloseAudio();
        SDL_Quit();
        return 0;
    }

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (ev.type == SDL_KEYDOWN) {
                SDLKey k = ev.key.keysym.sym;

                /* MENU (or Q on desktop) saves state & quits */
                if (k == V90_BTN_MENU || k == SDLK_q) {
                    running = false;
                    break;
                }

                /* Y (or H on desktop): Toggle Help modal */
                if (k == V90_BTN_Y || k == SDLK_y || k == SDLK_h) {
                    g_show_help = !g_show_help;
                    continue;
                }

                /* L1 / R1: Adjust Tempo (-5 / +5 BPM) */
                if (k == V90_BTN_L1
#if defined(HOST_X11)
                    || k == SDLK_BRACKETLEFT
#endif
                ) {
                    g_bpm -= 5;
                    if (g_bpm < 45) g_bpm = 45;
                    save_config();
                    continue;
                }
                if (k == V90_BTN_R1
#if defined(HOST_X11)
                    || k == SDLK_BRACKETRIGHT
#endif
                ) {
                    g_bpm += 5;
                    if (g_bpm > 220) g_bpm = 220;
                    save_config();
                    continue;
                }

                /* X: Cycle Backing Mix (Full Band -> Drums+Bass -> Drums Only) */
                if (k == V90_BTN_X || k == SDLK_x) {
                    g_mix_mode = (BackingMixMode)((g_mix_mode + 1) % 3);
                    save_config();
                    continue;
                }

                if (g_show_help) {
                    if (k == V90_BTN_LEFT) {
                        if (g_swing_pct > 50) g_swing_pct -= 2;
                        save_config();
                    } else if (k == V90_BTN_RIGHT) {
                        if (g_swing_pct < 68) g_swing_pct += 2;
                        save_config();
                    } else if (k == V90_BTN_A || k == SDLK_a) {
                        g_auto_fill = !g_auto_fill;
                        save_config();
                    } else if (k == V90_BTN_B || k == SDLK_b || k == V90_BTN_SELECT) {
                        g_show_help = false;
                    }
                    continue;
                }

                /* SELECT (or Home/C on desktop): Switch between Jam View and Step Editor */
                if (k == V90_BTN_SELECT
#if defined(HOST_X11)
                    || k == SDLK_HOME || k == SDLK_c
#endif
                ) {
                    g_view = (g_view == VIEW_JAM) ? VIEW_STEP_EDITOR : VIEW_JAM;
                    continue;
                }

                if (g_view == VIEW_JAM) {
                    if (k == V90_BTN_A || k == SDLK_a) {
                        g_playing = !g_playing;
                        if (g_playing) {
                            g_cur_bar = 0;
                            g_cur_step = 0;
                            g_synth.step_sample_pos = 0;
                        }
                    } else if (k == V90_BTN_B || k == SDLK_b
#if defined(HOST_X11)
                               || k == SDLK_f
#endif
                    ) {
                        g_fill_queued = !g_fill_queued;
                    } else if (k == V90_BTN_RIGHT) {
                        apply_style(g_style_idx + 1);
                        save_config();
                    } else if (k == V90_BTN_LEFT) {
                        apply_style(g_style_idx - 1);
                        save_config();
                    } else if (k == V90_BTN_UP) {
                        g_key_pc = (g_key_pc + 1) % 12;
                        save_config();
                    } else if (k == V90_BTN_DOWN) {
                        g_key_pc = (g_key_pc + 11) % 12;
                        save_config();
                    }
                } else {
                    /* VIEW_STEP_EDITOR Controls */
                    int spb = k_styles[g_style_idx].steps_per_bar;
                    if (k == V90_BTN_UP) {
                        g_edit_track = (g_edit_track + NUM_TRACKS - 1) % NUM_TRACKS;
                    } else if (k == V90_BTN_DOWN) {
                        g_edit_track = (g_edit_track + 1) % NUM_TRACKS;
                    } else if (k == V90_BTN_LEFT) {
                        g_edit_step = (g_edit_step + spb - 1) % spb;
                    } else if (k == V90_BTN_RIGHT) {
                        g_edit_step = (g_edit_step + 1) % spb;
                    } else if (k == V90_BTN_A || k == SDLK_a) {
                        uint8_t cur = g_pattern[g_style_idx][g_edit_track][g_edit_step];
                        int max_state = (g_edit_track == TRK_BASS) ? 6 : 2;
                        g_pattern[g_style_idx][g_edit_track][g_edit_step] = (uint8_t)((cur + 1) % (max_state + 1));
                    } else if (k == V90_BTN_B || k == SDLK_b) {
                        g_pattern[g_style_idx][g_edit_track][g_edit_step] = 0;
                    }
                }
            }
        }

        render_ui(screen);
        SDL_Delay(16);
    }

    save_config();
    if (audio_ok) {
        SDL_CloseAudio();
    }
    SDL_Quit();
    return 0;
}
