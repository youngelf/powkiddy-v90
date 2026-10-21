/*
 * eartrainer - Guitar Interval & Chord Quality Ear Trainer
 * Targets:
 *   1. PowKiddy V90 (MiyooCFW 1.3.3, 320x240 16-bit RGB565, SDL 1.2)
 *   2. Linux Desktop (x86_64 X11 640x480 window + PulseAudio/ALSA audio)
 *
 * Features:
 *   - Mode 1 (INTERVALS): Identifies 12 intervals (m2..P8) with Ascending,
 *     Descending, Harmonic, and Mixed playback styles.
 *   - Mode 2 (CHORDS): Identifies 12 guitar chord qualities (Major, Minor, dim,
 *     aug, sus2, sus4, Dom7, Maj7, m7, m7b5, dim7, 7sus4).
 *   - 3-Guess Rule: Gives the player 3 guesses per question before revealing
 *     the answer; eliminated wrong guesses are marked in crimson red.
 *   - 6-String Guitar Fretboard Visualizer (Frets 0..12): Shows the Root note
 *     position while guessing, and reveals the exact guitar fretboard shape
 *     (or any auditioned interval/chord via X) once answered.
 *   - 6-Voice Polyphonic Karplus-Strong Plucked Guitar String Synthesizer.
 *   - Persistent per-interval and per-chord accuracy stats (/mnt/.eartrainer.cfg).
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

#include "eartrainer_font.h"

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
#define SDLK_ESCAPE     XK_Escape
#define SDLK_HOME       XK_Home
#define SDLK_RETURN     XK_Return
#define SDLK_RCTRL      XK_Control_R
#define SDLK_UP         XK_Up
#define SDLK_DOWN       XK_Down
#define SDLK_LEFT       XK_Left
#define SDLK_RIGHT      XK_Right
#define SDLK_LEFTBRACKET  XK_bracketleft
#define SDLK_RIGHTBRACKET XK_bracketright
#define SDLK_a          XK_a
#define SDLK_b          XK_b
#define SDLK_h          XK_h
#define SDLK_q          XK_q
#define SDLK_r          XK_r
#define SDLK_x          XK_x
#define SDLK_y          XK_y

#define SDL_QUIT        1
#define SDL_KEYDOWN     2
#define SDL_KEYUP       3

typedef struct {
    int BitsPerPixel;
} SDL_PixelFormat;

typedef struct {
    int w, h;
    int pitch;
    SDL_PixelFormat *format;
    void *pixels;
} SDL_Surface;

typedef struct {
    int type;
    struct {
        struct {
            SDLKey sym;
        } keysym;
    } key;
} SDL_Event;

#define SDL_INIT_VIDEO  0x01
#define SDL_INIT_AUDIO  0x02
#define SDL_INIT_TIMER  0x04
#define SDL_DISABLE     0
#define SDL_SWSURFACE   0
#define AUDIO_S16SYS    0

typedef struct {
    int freq;
    int format;
    int channels;
    int samples;
    void (*callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
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
            pa = fn_new(NULL, "V90 EarTrainer", PA_STREAM_PLAYBACK, NULL, "Guitar Synth", &ss, NULL, NULL, &err);
        }
    }

    int chunk_frames = 512;
    int chunk_bytes = chunk_frames * AUDIO_CHANNELS * 2;
    int16_t buf[512 * AUDIO_CHANNELS];

    while (g_audio_running) {
        if (g_audio_paused || !g_audio_spec.callback) {
            memset(buf, 0, sizeof(buf));
        } else {
            g_audio_spec.callback(g_audio_spec.userdata, (Uint8 *)buf, chunk_bytes);
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

static int SDL_InitSubSystem(Uint32 flags) {
    (void)flags;
    return 0;
}

static const char *SDL_GetError(void) {
    return "Cannot open X11 DISPLAY";
}

static void SDL_ShowCursor(int toggle) {
    (void)toggle;
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
#endif

/* =========================================================================
 * PowKiddy V90 Button Definitions (MiyooCFW 1.3.3)
 * ========================================================================= */
#define V90_BTN_UP      SDLK_UP
#define V90_BTN_DOWN    SDLK_DOWN
#define V90_BTN_LEFT    SDLK_LEFT
#define V90_BTN_RIGHT   SDLK_RIGHT
#define V90_BTN_A       SDLK_LALT       /* East button (A): Submit guess / Next question */
#define V90_BTN_B       SDLK_LCTRL      /* South button (B): Replay question audio */
#define V90_BTN_X       SDLK_LSHIFT     /* North button (X): Audition highlighted grid item */
#define V90_BTN_Y       SDLK_SPACE      /* West button (Y): Help & Accuracy Stats modal */
#define V90_BTN_L1      SDLK_TAB        /* L1: Cycle playback style (ASC/DESC/HARM/MIX) */
#define V90_BTN_R1      SDLK_BACKSPACE  /* R1: Cycle difficulty (EASY / MEDIUM / ALL 12) */
#define V90_BTN_SELECT  SDLK_ESCAPE     /* SELECT: Toggle INTERVALS <-> CHORDS mode */
#define V90_BTN_START   SDLK_RETURN     /* START: Save & Exit */
#define V90_BTN_MENU    SDLK_RCTRL      /* MENU: Save & Exit */

/* =========================================================================
 * 24-bit TrueColor Software Backbuffer & Anti-Aliased Font Renderer
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
    fill_rect(x + 1, y + 1, w - 2, h - 2, fill_col);
    fill_rect(x + 1, y, w - 2, 1, border_col);
    fill_rect(x + 1, y + h - 1, w - 2, 1, border_col);
    fill_rect(x, y + 1, 1, h - 2, border_col);
    fill_rect(x + w - 1, y + 1, 1, h - 2, border_col);
}

static void draw_circle(int cx, int cy, int r, uint32_t fill_col, uint32_t border_col) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r) {
                int px = cx + dx;
                int py = cy + dy;
                if ((unsigned)px < SCREEN_W && (unsigned)py < SCREEN_H) {
                    g_fb[py * SCREEN_W + px] = (d2 >= (r - 1) * (r - 1)) ? border_col : fill_col;
                }
            }
        }
    }
}

static int char_advance(int font_id, char c) {
    const BardFont *f = &g_bard_fonts[font_id];
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
    const BardFont *f = &g_bard_fonts[font_id];
    int cur_x = x;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 32 || c > 126) c = '?';
        const BardGlyph *g = &f->glyphs[c - 32];
        const uint8_t *bmp = &g_bard_font_bitmaps[g->offset];
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
    const BardFont *f = &g_bard_fonts[font_id];
    int tw = text_width(font_id, s);
    int tx = bx + (bw - tw) / 2;
    int ty = by + (bh - f->line_height) / 2;
    draw_text(font_id, tx, ty, s, color);
}

static void draw_text_right(int font_id, int right_x, int y, const char *s, uint32_t color) {
    int tw = text_width(font_id, s);
    draw_text(font_id, right_x - tw, y, s, color);
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
 * Music Theory: Intervals, Guitar Chord Voicings & Fretboard Geometry
 * ========================================================================= */
#define NUM_ITEMS 12

/* Standard 6-string guitar open string MIDI notes: E2(40), A2(45), D3(50), G3(55), B3(59), E4(64) */
static const int k_open_string_midi[6] = { 40, 45, 50, 55, 59, 64 };
static const char *k_string_names[6]   = { "E", "A", "D", "G", "B", "e" };
static const char *k_note_names[12]    = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

typedef struct {
    const char *short_name;
    const char *sub_label;
    const char *full_name;
    const char *mnemonic;
    int semitones;
    int min_tier; /* 0 = Easy, 1 = Diatonic, 2 = All 12 */
} IntervalDef;

static const IntervalDef k_intervals[NUM_ITEMS] = {
    { "m2", "1 st",  "Minor 2nd",   "\"Jaws\" Theme",         1, 2 },
    { "M2", "2 st",  "Major 2nd",   "\"Happy Birthday\"",     2, 1 },
    { "m3", "3 st",  "Minor 3rd",   "\"Smoke on Water\"",     3, 0 },
    { "M3", "4 st",  "Major 3rd",   "\"When the Saints\"",    4, 0 },
    { "P4", "5 st",  "Perfect 4th", "\"Here Comes Bride\"",   5, 0 },
    { "TT", "b5/6",  "Tritone",     "\"The Simpsons\"",       6, 2 },
    { "P5", "7 st",  "Perfect 5th", "\"Star Wars\" (5th)",    7, 0 },
    { "m6", "8 st",  "Minor 6th",   "\"The Entertainer\"",    8, 2 },
    { "M6", "9 st",  "Major 6th",   "\"My Bonnie\"",          9, 1 },
    { "m7", "10 st", "Minor 7th",   "\"Star Trek\" Theme",   10, 2 },
    { "M7", "11 st", "Major 7th",   "\"Take On Me\"",        11, 1 },
    { "P8", "Oct",   "Octave",      "\"Over the Rainbow\"",  12, 0 }
};

typedef struct {
    const char *short_name;
    const char *formula;
    const char *full_name;
    int num_notes;
    int semitones[4];
    /* Moveable 4-note guitar shape from 5th-string or 6th-string root:
     * (string_offset_from_root_str, fret_offset_from_root_fret) */
    int str_off[4];
    int fret_off[4];
    int min_tier; /* 0 = Easy, 1 = Medium, 2 = All 12 */
} ChordDef;

/*
 * Note on guitar shapes: when root is on String 1 (A string, 50-5=45 MIDI),
 * offsets to D(+5), G(+10), B(+14), e(+19) allow authentic moveable guitar voicings!
 */
static const ChordDef k_chords[NUM_ITEMS] = {
    { "Major", "1-3-5",     "Major Triad",       4, {0, 4, 7, 12},  {0, 1, 2, 3}, {0, 2, 2, 2}, 0 },
    { "Minor", "1-b3-5",    "Minor Triad",       4, {0, 3, 7, 12},  {0, 1, 2, 3}, {0, 2, 2, 1}, 0 },
    { "dim",   "1-b3-b5",   "Diminished Triad",  3, {0, 3, 6, 0},   {0, 1, 2, 0}, {0, 1, 2, 0}, 1 },
    { "aug",   "1-3-#5",    "Augmented Triad",   3, {0, 4, 8, 0},   {0, 1, 2, 0}, {0, -1, 1, 0}, 1 },
    { "sus2",  "1-2-5",     "Suspended 2nd",     4, {0, 2, 7, 12},  {0, 1, 2, 3}, {0, 2, 2, 0}, 0 },
    { "sus4",  "1-4-5",     "Suspended 4th",     4, {0, 5, 7, 12},  {0, 1, 2, 3}, {0, 2, 2, 3}, 0 },
    { "Dom7",  "1-3-5-b7",  "Dominant 7th (7)",  4, {0, 4, 7, 10},  {0, 1, 2, 3}, {0, 2, 0, 2}, 1 },
    { "Maj7",  "1-3-5-7",   "Major 7th",         4, {0, 4, 7, 11},  {0, 1, 2, 3}, {0, 2, 1, 2}, 1 },
    { "m7",    "1-b3-5-b7", "Minor 7th",         4, {0, 3, 7, 10},  {0, 1, 2, 3}, {0, 2, 0, 1}, 1 },
    { "m7b5",  "1-b3-b5-b7","Half-Diminished",   4, {0, 3, 6, 10},  {0, 1, 2, 3}, {0, 1, 0, 1}, 2 },
    { "dim7",  "1-b3-b5-6", "Full Diminished 7", 4, {0, 3, 6, 9},   {0, 1, 2, 3}, {0, 1, -1, 1}, 2 },
    { "7sus4", "1-4-5-b7",  "Dominant 7sus4",    4, {0, 5, 7, 10},  {0, 1, 2, 3}, {0, 2, 0, 3}, 2 }
};

static void format_midi_note(int midi, char *buf, size_t sz) {
    int note_idx = ((midi % 12) + 12) % 12;
    int octave = (midi / 12) - 1;
    snprintf(buf, sz, "%s%d", k_note_names[note_idx], octave);
}

/* Given a root string (0..3), root fret (1..9), and interval semitones (1..12),
 * find the most natural playable guitar position (target_str, target_fret) on frets 0..12. */
static void find_guitar_interval_pos(int root_str, int root_fret, int semitones,
                                     int *out_str, int *out_fret) {
    int target_midi = k_open_string_midi[root_str] + root_fret + semitones;
    int best_str = root_str;
    int best_fret = root_fret + semitones;
    int best_cost = 9999;

    for (int s = root_str; s < 6; s++) {
        int f = target_midi - k_open_string_midi[s];
        if (f < 0 || f > 12) continue;
        int fret_dist = abs(f - root_fret);
        int str_dist = s - root_str;
        /* Prefer notes within a 3-fret box on adjacent strings (classic CAGED shapes) */
        int cost = fret_dist * 3 + str_dist * 2;
        if (s == root_str && semitones > 4) cost += 10;
        if (cost < best_cost) {
            best_cost = cost;
            best_str = s;
            best_fret = f;
        }
    }
    *out_str = best_str;
    *out_fret = best_fret;
}

/* =========================================================================
 * 6-Voice Polyphonic Karplus-Strong Plucked Guitar String Synthesizer
 * ========================================================================= */
#define MAX_VOICES     6
#define MAX_DELAY_LEN  1024
#define MAX_SCHEDULED  12

typedef struct {
    bool active;
    int16_t delay_line[MAX_DELAY_LEN];
    int delay_len;
    int pos;
    int samples_left;
    int damping_num; /* Out of 256 */
} GuitarVoice;

typedef struct {
    bool pending;
    int delay_samples;
    int midi_note;
    int duration_ms;
} ScheduledPluck;

static GuitarVoice    g_voices[MAX_VOICES];
static ScheduledPluck g_sched[MAX_SCHEDULED];
static int            g_next_voice = 0;
static uint32_t       g_prng_state = 0x13579BDFu;

static inline int16_t fast_rand_s16(void) {
    g_prng_state = g_prng_state * 1664525u + 1013904223u;
    return (int16_t)(g_prng_state >> 16);
}

static void pluck_voice_immediate(int midi_note, int duration_ms) {
    double freq = 440.0 * pow(2.0, (double)(midi_note - 69) / 12.0);
    int dlen = (int)((double)SAMPLE_RATE / freq + 0.5);
    if (dlen < 16) dlen = 16;
    if (dlen >= MAX_DELAY_LEN) dlen = MAX_DELAY_LEN - 1;

    GuitarVoice *v = &g_voices[g_next_voice];
    g_next_voice = (g_next_voice + 1) % MAX_VOICES;

    v->active = false;
    v->delay_len = dlen;
    v->pos = 0;
    v->samples_left = (SAMPLE_RATE * duration_ms) / 1000;
    v->damping_num = (midi_note < 50) ? 127 : 126; /* Slightly longer sustain on bass strings */

    /* Excite the waveguide with warm pick-position filtered noise + fundamental body */
    int32_t prev = 0;
    for (int i = 0; i < dlen; i++) {
        double phase = (2.0 * 3.14159265358979323846 * (double)i) / (double)dlen;
        int32_t body = (int32_t)(sin(phase) * 14000.0 + sin(phase * 2.0) * 6500.0);
        int32_t noise = (int32_t)fast_rand_s16() / 2;
        /* One-pole low-pass filter on the initial pluck noise for acoustic warmth */
        prev = (prev * 3 + noise) / 4;
        int32_t sample = body + prev;
        if (sample > 28000)  sample = 28000;
        if (sample < -28000) sample = -28000;
        v->delay_line[i] = (int16_t)sample;
    }
    v->active = true;
}

static void clear_audio_schedule(void) {
    for (int i = 0; i < MAX_SCHEDULED; i++) {
        g_sched[i].pending = false;
    }
    for (int i = 0; i < MAX_VOICES; i++) {
        g_voices[i].active = false;
    }
}

static void schedule_note_pluck(int midi_note, int delay_ms, int duration_ms) {
    for (int i = 0; i < MAX_SCHEDULED; i++) {
        if (!g_sched[i].pending) {
            g_sched[i].midi_note = midi_note;
            g_sched[i].delay_samples = (SAMPLE_RATE * delay_ms) / 1000;
            g_sched[i].duration_ms = duration_ms;
            g_sched[i].pending = true;
            return;
        }
    }
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int frames = len / (2 * AUDIO_CHANNELS);

    for (int i = 0; i < frames; i++) {
        /* Trigger any scheduled plucks whose delay countdown has reached 0 */
        for (int s = 0; s < MAX_SCHEDULED; s++) {
            if (g_sched[s].pending) {
                if (g_sched[s].delay_samples <= 0) {
                    g_sched[s].pending = false;
                    pluck_voice_immediate(g_sched[s].midi_note, g_sched[s].duration_ms);
                } else {
                    g_sched[s].delay_samples--;
                }
            }
        }

        /* Mix all active Karplus-Strong guitar voices using integer arithmetic */
        int32_t mix = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            GuitarVoice *gv = &g_voices[v];
            if (!gv->active) continue;
            if (gv->samples_left <= 0) {
                gv->active = false;
                continue;
            }
            gv->samples_left--;

            int p0 = gv->pos;
            int p1 = (p0 + 1 < gv->delay_len) ? (p0 + 1) : 0;
            int32_t s0 = gv->delay_line[p0];
            int32_t s1 = gv->delay_line[p1];

            mix += s0;

            /* Karplus-Strong two-point averaging filter with damping */
            int32_t avg = ((s0 + s1) * gv->damping_num) >> 8;
            gv->delay_line[p0] = (int16_t)avg;
            gv->pos = p1;
        }

        /* Soft clip & output stereo */
        mix = (mix * 3) / 4;
        if (mix > 32767)  mix = 32767;
        if (mix < -32767) mix = -32767;
        int16_t sample = (int16_t)mix;
        for (int c = 0; c < AUDIO_CHANNELS; c++) {
            out[i * AUDIO_CHANNELS + c] = sample;
        }
    }
}

static void maximize_hardware_volume(void) {
#if !defined(HOST_X11)
    int fd = open("/dev/mixer", O_RDWR);
    if (fd >= 0) {
        int vol = (90 << 8) | 90;
        ioctl(fd, SOUND_MIXER_WRITE_VOLUME, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_PCM, &vol);
        ioctl(fd, SOUND_MIXER_WRITE_SPEAKER, &vol);
        close(fd);
    }
#endif
}

/* =========================================================================
 * Application State & Persistent Statistics
 * ========================================================================= */
typedef enum {
    MODE_INTERVALS = 0,
    MODE_CHORDS    = 1
} TrainerMode;

typedef enum {
    INT_STYLE_ASC  = 0,
    INT_STYLE_DESC = 1,
    INT_STYLE_HARM = 2,
    INT_STYLE_MIX  = 3
} IntervalStyle;

typedef enum {
    CHD_STYLE_ARP_STRUM = 0,
    CHD_STYLE_STRUM     = 1,
    CHD_STYLE_ARP       = 2
} ChordStyle;

static const char *k_int_style_names[4] = { "ASC", "DESC", "HARM", "MIX" };
static const char *k_chd_style_names[3] = { "ARP+STRUM", "STRUM", "ARPEGGIO" };
static const char *k_tier_names[3]      = { "EASY", "MEDIUM", "ALL 12" };

static TrainerMode   g_mode          = MODE_INTERVALS;
static IntervalStyle g_int_style     = INT_STYLE_ASC;
static ChordStyle    g_chd_style     = CHD_STYLE_ARP_STRUM;
static int           g_tier          = 2; /* Default: ALL 12 */
static bool          g_show_help     = false;

/* Current question state */
static int           g_root_str      = 0; /* 0..2 (E, A, D strings) */
static int           g_root_fret     = 5; /* 1..7 */
static int           g_root_midi     = 45;
static int           g_correct_idx   = 6; /* 0..11 */
static bool          g_cur_is_desc   = false;
static int           g_guesses_left  = 3; /* 3 guesses before reveal */
static bool          g_wrong_guessed[NUM_ITEMS];
static bool          g_revealed      = false;
static bool          g_solved        = false;
static int           g_audition_idx  = 6; /* Which item's shape is shown when revealed */
static int           g_cursor        = 0; /* 0..11 in the 4x3 grid */

/* Session & Lifetime Stats */
static int           g_streak        = 0;
static int           g_best_streak   = 0;
static int           g_session_ok    = 0;
static int           g_session_tot   = 0;
static uint32_t      g_int_ok[NUM_ITEMS];
static uint32_t      g_int_tot[NUM_ITEMS];
static uint32_t      g_chd_ok[NUM_ITEMS];
static uint32_t      g_chd_tot[NUM_ITEMS];
static char          g_cfg_path[512];

static void resolve_cfg_path(void) {
    struct stat st;
    if (stat("/mnt", &st) == 0 && S_ISDIR(st.st_mode) && access("/mnt", W_OK) == 0) {
        snprintf(g_cfg_path, sizeof(g_cfg_path), "/mnt/.eartrainer.cfg");
    } else {
        const char *home = getenv("HOME");
        if (home && access(home, W_OK) == 0) {
            snprintf(g_cfg_path, sizeof(g_cfg_path), "%s/.eartrainer.cfg", home);
        } else {
            snprintf(g_cfg_path, sizeof(g_cfg_path), "./.eartrainer.cfg");
        }
    }
}

static void save_stats(void) {
    if (g_cfg_path[0] == '\0') return;
    FILE *f = fopen(g_cfg_path, "w");
    if (!f) return;
    fprintf(f, "best_streak=%d\n", g_best_streak);
    fprintf(f, "mode=%d\n", (int)g_mode);
    fprintf(f, "tier=%d\n", g_tier);
    for (int i = 0; i < NUM_ITEMS; i++) {
        fprintf(f, "int_%d=%u,%u\n", i, g_int_ok[i], g_int_tot[i]);
        fprintf(f, "chd_%d=%u,%u\n", i, g_chd_ok[i], g_chd_tot[i]);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

static void load_stats(void) {
    if (g_cfg_path[0] == '\0') return;
    FILE *f = fopen(g_cfg_path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int idx = 0;
        unsigned int ok = 0, tot = 0;
        int val = 0;
        if (sscanf(line, "best_streak=%d", &val) == 1 && val >= 0) {
            g_best_streak = val;
        } else if (sscanf(line, "tier=%d", &val) == 1 && val >= 0 && val <= 2) {
            g_tier = val;
        } else if (sscanf(line, "int_%d=%u,%u", &idx, &ok, &tot) == 3 && idx >= 0 && idx < NUM_ITEMS) {
            g_int_ok[idx] = ok;
            g_int_tot[idx] = tot;
        } else if (sscanf(line, "chd_%d=%u,%u", &idx, &ok, &tot) == 3 && idx >= 0 && idx < NUM_ITEMS) {
            g_chd_ok[idx] = ok;
            g_chd_tot[idx] = tot;
        }
    }
    fclose(f);
}

static bool is_item_enabled(int idx) {
    if (idx < 0 || idx >= NUM_ITEMS) return false;
    if (g_mode == MODE_INTERVALS) {
        return k_intervals[idx].min_tier <= g_tier;
    } else {
        return k_chords[idx].min_tier <= g_tier;
    }
}

/* Play a specific interval or chord (0..11) from the current root note */
static void play_item_audio(int item_idx) {
    if (item_idx < 0 || item_idx >= NUM_ITEMS) return;
    clear_audio_schedule();

    if (g_mode == MODE_INTERVALS) {
        int semitones = k_intervals[item_idx].semitones;
        int note1 = g_root_midi;
        int note2 = g_root_midi + semitones;

        if (g_int_style == INT_STYLE_HARM) {
            schedule_note_pluck(note1, 0, 1400);
            schedule_note_pluck(note2, 22, 1400);
        } else if (g_cur_is_desc) {
            schedule_note_pluck(note2, 0, 1100);
            schedule_note_pluck(note1, 580, 1300);
        } else {
            schedule_note_pluck(note1, 0, 1100);
            schedule_note_pluck(note2, 580, 1300);
        }
    } else {
        const ChordDef *cd = &k_chords[item_idx];
        if (g_chd_style == CHD_STYLE_STRUM) {
            for (int n = 0; n < cd->num_notes; n++) {
                schedule_note_pluck(g_root_midi + cd->semitones[n], n * 35, 1600);
            }
        } else if (g_chd_style == CHD_STYLE_ARP) {
            for (int n = 0; n < cd->num_notes; n++) {
                schedule_note_pluck(g_root_midi + cd->semitones[n], n * 230, 1400);
            }
        } else {
            /* ARP + STRUM: Arpeggiate individual notes, then strum chord together */
            for (int n = 0; n < cd->num_notes; n++) {
                schedule_note_pluck(g_root_midi + cd->semitones[n], n * 200, 1100);
            }
            int strum_start = cd->num_notes * 200 + 220;
            for (int n = 0; n < cd->num_notes; n++) {
                schedule_note_pluck(g_root_midi + cd->semitones[n], strum_start + n * 32, 1500);
            }
        }
    }
}

static void generate_new_question(bool play_sound) {
    /* Pick a guitar root string & fret:
     * For Intervals: String 0 (E2), String 1 (A2), or String 2 (D3), frets 1..7
     * For Chords: String 1 (A2), frets 2..7 so 4-string chord voicings sit cleanly on A-D-G-B */
    if (g_mode == MODE_INTERVALS) {
        g_root_str = rand() % 3;
        g_root_fret = 1 + (rand() % 7);
    } else {
        g_root_str = 1; /* 5th string (A string) root for clean 4-string chord shapes */
        g_root_fret = 2 + (rand() % 6);
    }
    g_root_midi = k_open_string_midi[g_root_str] + g_root_fret;

    /* Pick a random enabled interval/chord */
    int candidates[NUM_ITEMS];
    int count = 0;
    for (int i = 0; i < NUM_ITEMS; i++) {
        if (is_item_enabled(i)) {
            candidates[count++] = i;
        }
    }
    if (count == 0) {
        g_correct_idx = 0;
    } else {
        int pick = candidates[rand() % count];
        /* Avoid repeating the exact same answer twice in a row when possible */
        if (count > 1 && pick == g_correct_idx) {
            pick = candidates[rand() % count];
        }
        g_correct_idx = pick;
    }

    if (g_int_style == INT_STYLE_DESC) {
        g_cur_is_desc = true;
    } else if (g_int_style == INT_STYLE_MIX) {
        g_cur_is_desc = (rand() % 2) != 0;
    } else {
        g_cur_is_desc = false;
    }

    g_guesses_left = 3;
    memset(g_wrong_guessed, 0, sizeof(g_wrong_guessed));
    g_revealed = false;
    g_solved = false;
    g_audition_idx = g_correct_idx;

    if (!is_item_enabled(g_cursor)) {
        for (int i = 0; i < NUM_ITEMS; i++) {
            if (is_item_enabled(i)) { g_cursor = i; break; }
        }
    }

    if (play_sound) {
        play_item_audio(g_correct_idx);
    }
}

static void submit_guess(int idx) {
    if (g_revealed) {
        /* When answer is already revealed, pressing A advances to the next question */
        generate_new_question(true);
        return;
    }
    if (!is_item_enabled(idx) || g_wrong_guessed[idx]) {
        return;
    }

    if (idx == g_correct_idx) {
        /* Correct guess! */
        g_solved = true;
        g_revealed = true;
        g_audition_idx = g_correct_idx;
        g_streak++;
        if (g_streak > g_best_streak) g_best_streak = g_streak;
        g_session_ok++;
        g_session_tot++;
        if (g_mode == MODE_INTERVALS) {
            g_int_ok[g_correct_idx]++;
            g_int_tot[g_correct_idx]++;
        } else {
            g_chd_ok[g_correct_idx]++;
            g_chd_tot[g_correct_idx]++;
        }
        save_stats();
        play_item_audio(g_correct_idx);
    } else {
        /* Wrong guess */
        g_wrong_guessed[idx] = true;
        g_guesses_left--;
        if (g_guesses_left <= 0) {
            /* All 3 guesses used: reveal the answer */
            g_solved = false;
            g_revealed = true;
            g_audition_idx = g_correct_idx;
            g_streak = 0;
            g_session_tot++;
            if (g_mode == MODE_INTERVALS) {
                g_int_tot[g_correct_idx]++;
            } else {
                g_chd_tot[g_correct_idx]++;
            }
            save_stats();
            play_item_audio(g_correct_idx);
        } else {
            /* Replay the question so the player can listen again for their next guess */
            play_item_audio(g_correct_idx);
        }
    }
}

/* =========================================================================
 * UI Rendering (320x240): Top Bar, 6-String Guitar Fretboard, 4x3 Keypad
 * ========================================================================= */
static void render_fretboard_panel(void) {
    /* Middle Panel: y = 22..108 (height = 86px) */
    fill_rect(0, 22, SCREEN_W, 86, RGB(16, 22, 35));
    fill_rect(0, 107, SCREEN_W, 1, RGB(45, 58, 82));

    /* Left side: 6-String Guitar Fretboard (Frets 0..12), x = 16..184, y = 28..78 */
    int fb_x = 16;
    int fb_y = 28;
    int fret_w = 14; /* 12 frets * 14px = 168px (x = 16..184) */
    int str_sp = 10; /* 5 gaps * 10px = 50px (y = 28..78) */
    int fb_w = fret_w * 12;
    int fb_h = str_sp * 5;

    /* Rosewood fretboard background */
    fill_rect(fb_x, fb_y - 3, fb_w, fb_h + 6, RGB(46, 31, 24));
    draw_border(fb_x, fb_y - 3, fb_w, fb_h + 6, RGB(92, 66, 48));

    /* Bone nut at Fret 0 */
    fill_rect(fb_x - 3, fb_y - 3, 4, fb_h + 6, RGB(230, 222, 205));

    /* Fret wires & Pearloid Inlay Dots (3, 5, 7, 9, and double dot at 12) */
    for (int f = 1; f <= 12; f++) {
        int fx = fb_x + f * fret_w;
        fill_rect(fx, fb_y - 2, 1, fb_h + 4, RGB(148, 156, 168));
        int mid_x = fx - fret_w / 2;
        if (f == 3 || f == 5 || f == 7 || f == 9) {
            draw_circle(mid_x, fb_y + fb_h / 2, 2, RGB(203, 213, 225), RGB(148, 163, 184));
        } else if (f == 12) {
            draw_circle(mid_x, fb_y + str_sp * 1 + 5, 2, RGB(203, 213, 225), RGB(148, 163, 184));
            draw_circle(mid_x, fb_y + str_sp * 3 + 5, 2, RGB(203, 213, 225), RGB(148, 163, 184));
        }
    }

    /* Fret numbers below neck (3, 5, 7, 9, 12) */
    const int label_frets[5] = { 3, 5, 7, 9, 12 };
    for (int i = 0; i < 5; i++) {
        int f = label_frets[i];
        char fstr[8];
        snprintf(fstr, sizeof(fstr), "%d", f);
        int cx = fb_x + f * fret_w - fret_w / 2;
        draw_text_centered(FONT_UI_10, cx - 8, fb_y + fb_h + 4, 16, 11, fstr, RGB(148, 163, 184));
    }

    /* 6 Guitar Strings: top row = String 5 (high 'e'), bottom row = String 0 (low 'E') */
    for (int s = 0; s < 6; s++) {
        int row = 5 - s;
        int sy = fb_y + row * str_sp;
        draw_text(FONT_UI_10, 4, sy - 6, k_string_names[s], RGB(148, 163, 184));
        uint32_t scol = (s < 3) ? RGB(205, 175, 130) : RGB(210, 218, 228); /* Bronze wound vs steel plain */
        fill_rect(fb_x, sy, fb_w, 1, scol);
    }

    /* Draw Root Note marker (Amber Gold) */
    int root_row = 5 - g_root_str;
    int root_cx = (g_root_fret == 0) ? (fb_x - 1) : (fb_x + g_root_fret * fret_w - fret_w / 2);
    int root_cy = fb_y + root_row * str_sp;

    /* If revealed, also draw the target interval note or full chord voicing */
    int shown_idx = g_revealed ? g_audition_idx : g_correct_idx;
    if (g_revealed) {
        if (g_mode == MODE_INTERVALS) {
            int t_str = 0, t_fret = 0;
            find_guitar_interval_pos(g_root_str, g_root_fret, k_intervals[shown_idx].semitones, &t_str, &t_fret);
            int t_row = 5 - t_str;
            int t_cx = (t_fret == 0) ? (fb_x - 1) : (fb_x + t_fret * fret_w - fret_w / 2);
            int t_cy = fb_y + t_row * str_sp;
            uint32_t t_fill = (shown_idx == g_correct_idx) ? RGB(16, 185, 129) : RGB(59, 130, 246);
            draw_circle(t_cx, t_cy, 5, t_fill, RGB(255, 255, 255));
        } else {
            const ChordDef *cd = &k_chords[shown_idx];
            uint32_t c_fill = (shown_idx == g_correct_idx) ? RGB(16, 185, 129) : RGB(59, 130, 246);
            for (int n = 1; n < cd->num_notes; n++) {
                int ns = g_root_str + cd->str_off[n];
                int nf = g_root_fret + cd->fret_off[n];
                if (ns >= 0 && ns < 6 && nf >= 0 && nf <= 12) {
                    int nrow = 5 - ns;
                    int ncx = (nf == 0) ? (fb_x - 1) : (fb_x + nf * fret_w - fret_w / 2);
                    int ncy = fb_y + nrow * str_sp;
                    draw_circle(ncx, ncy, 4, c_fill, RGB(255, 255, 255));
                }
            }
        }
    }

    /* Draw Root Note on top so 'R' is always crisp */
    draw_circle(root_cx, root_cy, 5, RGB(245, 158, 11), RGB(254, 240, 138));

    /* Right Info Card: x = 190..316, y = 25..103 */
    int ix = 190, iy = 25, iw = 126, ih = 79;
    fill_rect(ix, iy, iw, ih, RGB(23, 32, 50));
    draw_border(ix, iy, iw, ih, g_revealed ? (g_solved ? RGB(16, 185, 129) : RGB(239, 68, 68)) : RGB(56, 78, 112));

    char root_name[16];
    format_midi_note(g_root_midi, root_name, sizeof(root_name));

    if (!g_revealed) {
        /* Guessing state: show Root Note, Question Prompt, and 3 Guess Pips */
        char rline[32];
        snprintf(rline, sizeof(rline), "Root: %s (%s%d)", root_name, k_string_names[g_root_str], g_root_fret);
        draw_text(FONT_UI_12, ix + 6, iy + 5, rline, RGB(250, 204, 21));

        draw_text(FONT_UI_10, ix + 6, iy + 24,
                  (g_mode == MODE_INTERVALS) ? (g_cur_is_desc ? "Guess Desc Int:" : "Guess Interval:")
                                             : "Guess Chord:",
                  RGB(148, 163, 184));
        draw_text(FONT_UI_10, ix + 6, iy + 39, "B: Replay   X: Root", RGB(226, 232, 240));

        /* 3-Guess Remaining Indicator Pips */
        draw_text(FONT_UI_10, ix + 6, iy + 58, "Tries:", RGB(148, 163, 184));
        for (int g = 0; g < 3; g++) {
            bool avail = (g < g_guesses_left);
            uint32_t pfill = avail ? RGB(16, 185, 129) : RGB(220, 38, 38);
            uint32_t pbdr  = avail ? RGB(110, 231, 183) : RGB(252, 165, 165);
            draw_pill(ix + 48 + g * 20, iy + 58, 16, 12, pfill, pbdr);
        }
    } else {
        /* Revealed state: show Answer (or Auditioned item), Notes, and Mnemonic/Formula */
        const char *hdr = (shown_idx == g_correct_idx)
                              ? (g_solved ? "CORRECT!" : "ANSWER:")
                              : "AUDITION (X):";
        uint32_t hcol = (shown_idx == g_correct_idx)
                            ? (g_solved ? RGB(52, 211, 153) : RGB(248, 113, 113))
                            : RGB(96, 165, 250);
        draw_text(FONT_UI_10, ix + 6, iy + 4, hdr, hcol);

        if (g_mode == MODE_INTERVALS) {
            const IntervalDef *id = &k_intervals[shown_idx];
            draw_text(FONT_UI_12, ix + 6, iy + 17, id->full_name, RGB(255, 255, 255));

            char t_name[16];
            format_midi_note(g_root_midi + id->semitones, t_name, sizeof(t_name));
            char nline[64];
            snprintf(nline, sizeof(nline), "%s -> %s (%s)", root_name, t_name, id->sub_label);
            draw_text(FONT_UI_10, ix + 6, iy + 35, nline, RGB(250, 204, 21));

            draw_text(FONT_UI_10, ix + 6, iy + 50, id->mnemonic, RGB(147, 197, 253));
        } else {
            const ChordDef *cd = &k_chords[shown_idx];
            char cname[32];
            snprintf(cname, sizeof(cname), "%s %s", k_note_names[g_root_midi % 12], cd->short_name);
            draw_text(FONT_UI_12, ix + 6, iy + 17, cname, RGB(255, 255, 255));
            draw_text(FONT_UI_10, ix + 6, iy + 35, cd->full_name, RGB(250, 204, 21));

            char fline[32];
            snprintf(fline, sizeof(fline), "Tones: %s", cd->formula);
            draw_text(FONT_UI_10, ix + 6, iy + 50, fline, RGB(147, 197, 253));
        }
        draw_text(FONT_UI_10, ix + 6, iy + 64, "A:Next  X:Compare", RGB(167, 243, 208));
    }
}

static void render_keypad_grid(void) {
    /* 4x3 Answer Grid: y = 110..220 (height = 110px) */
    int gx = 4, gy = 111;
    int cols = 4, rows = 3;
    int gap_x = 4, gap_y = 4;
    int bw = (SCREEN_W - 8 - (cols - 1) * gap_x) / cols; /* 75px */
    int bh = (108 - (rows - 1) * gap_y) / rows;          /* 33px */

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            int bx = gx + c * (bw + gap_x);
            int by = gy + r * (bh + gap_y);

            bool enabled = is_item_enabled(idx);
            bool focused = (idx == g_cursor);
            bool wrong   = g_wrong_guessed[idx];
            bool is_ans  = g_revealed && (idx == g_correct_idx);
            bool is_aud  = g_revealed && (idx == g_audition_idx) && (idx != g_correct_idx);

            uint32_t fill_col = RGB(30, 41, 59);
            uint32_t bdr_col  = RGB(56, 74, 102);
            uint32_t txt_col  = RGB(241, 245, 249);
            uint32_t sub_col  = RGB(148, 163, 184);

            if (!enabled) {
                fill_col = RGB(15, 20, 30);
                bdr_col  = RGB(28, 36, 50);
                txt_col  = RGB(60, 72, 90);
                sub_col  = RGB(48, 58, 74);
            } else if (is_ans) {
                fill_col = RGB(5, 122, 85);
                bdr_col  = RGB(52, 211, 153);
                txt_col  = RGB(255, 255, 255);
                sub_col  = RGB(209, 250, 229);
            } else if (wrong) {
                fill_col = RGB(127, 29, 29);
                bdr_col  = RGB(248, 113, 113);
                txt_col  = RGB(254, 202, 202);
                sub_col  = RGB(252, 165, 165);
            } else if (is_aud) {
                fill_col = RGB(29, 78, 216);
                bdr_col  = RGB(96, 165, 250);
                txt_col  = RGB(255, 255, 255);
                sub_col  = RGB(191, 219, 254);
            }

            fill_rect(bx, by, bw, bh, fill_col);
            draw_border(bx, by, bw, bh, bdr_col);

            if (focused) {
                draw_border(bx, by, bw, bh, RGB(250, 204, 21));
                draw_border(bx + 1, by + 1, bw - 2, bh - 2, RGB(250, 204, 21));
            }

            const char *main_lbl = (g_mode == MODE_INTERVALS) ? k_intervals[idx].short_name
                                                              : k_chords[idx].short_name;

            if (g_mode == MODE_INTERVALS) {
                /* Large 22px bold interval name ('m2', 'M2', 'm3', 'M3', ...), with
                 * just the semitone number tucked in the bottom-right corner */
                draw_text_centered(FONT_BTN_22, bx, by - 1, bw - 8, bh, main_lbl, txt_col);

                char num_lbl[8];
                snprintf(num_lbl, sizeof(num_lbl), "%d", k_intervals[idx].semitones);
                draw_text_right(FONT_UI_10, bx + bw - 4, by + bh - 12, num_lbl, sub_col);
            } else {
                /* Large 18px bold chord name centered in the button */
                draw_text_centered(FONT_BTN_18, bx, by, bw, bh, main_lbl, txt_col);
            }
        }
    }
}

static void render_help_stats_modal(void) {
    int mx = 8, my = 8, mw = SCREEN_W - 16, mh = SCREEN_H - 16;
    fill_rect(mx, my, mw, mh, RGB(15, 23, 42));
    draw_border(mx, my, mw, mh, RGB(96, 165, 250));
    draw_border(mx + 1, my + 1, mw - 2, mh - 2, RGB(37, 99, 235));

    /* Header */
    fill_rect(mx + 2, my + 2, mw - 4, 20, RGB(30, 41, 59));
    draw_pill(mx + 6, my + 4, 42, 15, RGB(37, 99, 235), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_10, mx + 6, my + 4, 42, 15, "STATS", RGB(255, 255, 255));

    char hdr_buf[64];
    snprintf(hdr_buf, sizeof(hdr_buf), "%s  (Best Streak: %d)",
             (g_mode == MODE_INTERVALS) ? "Intervals" : "Chords", g_best_streak);
    draw_text(FONT_UI_12, mx + 54, my + 4, hdr_buf, RGB(248, 250, 252));

    /* Per-item accuracy grid (4x3) */
    int gx = mx + 6, gy = my + 26;
    int bw = (mw - 12 - 9) / 4; /* 70px */
    int bh = 25;
    for (int i = 0; i < NUM_ITEMS; i++) {
        int r = i / 4, c = i % 4;
        int bx = gx + c * (bw + 3);
        int by = gy + r * (bh + 3);
        fill_rect(bx, by, bw, bh, RGB(24, 34, 54));
        draw_border(bx, by, bw, bh, RGB(51, 65, 85));

        const char *name = (g_mode == MODE_INTERVALS) ? k_intervals[i].short_name : k_chords[i].short_name;
        uint32_t ok  = (g_mode == MODE_INTERVALS) ? g_int_ok[i]  : g_chd_ok[i];
        uint32_t tot = (g_mode == MODE_INTERVALS) ? g_int_tot[i] : g_chd_tot[i];

        char stat_str[24];
        if (tot > 0) {
            snprintf(stat_str, sizeof(stat_str), "%u/%u %u%%", ok, tot, (ok * 100) / tot);
        } else {
            snprintf(stat_str, sizeof(stat_str), "--");
        }
        draw_text(FONT_UI_10, bx + 4, by + 2, name, RGB(250, 204, 21));
        draw_text(FONT_UI_10, bx + 4, by + 13, stat_str, (tot > 0) ? RGB(167, 243, 208) : RGB(100, 116, 139));
    }

    /* Controls Reference Table */
    int ky = my + 114;
    fill_rect(mx + 6, ky, mw - 12, 1, RGB(51, 65, 85));
    ky += 4;

    static const struct {
        const char *k;
        const char *v;
    } k_help_rows[5] = {
        { "A / B",       "A: Guess (3 Tries)/Next | B: Replay" },
        { "X",           "Audition & Compare on Fretboard"     },
        { "SELECT",      "Switch: Intervals <-> Chords"        },
        { "L1 / R1",     "L1: Play Style  |  R1: Difficulty"   },
        { "MENU / Y",    "MENU: Save & Quit | Y: Close Help"   }
    };

    for (int i = 0; i < 5; i++) {
        int ry = ky + i * 18;
        draw_pill(mx + 8, ry, 62, 14, RGB(30, 58, 95), RGB(56, 189, 248));
        draw_text_centered(FONT_UI_10, mx + 8, ry, 62, 14, k_help_rows[i].k, RGB(254, 240, 138));
        draw_text(FONT_UI_10, mx + 76, ry + 1, k_help_rows[i].v, RGB(226, 232, 240));
    }
}

static void render_ui(SDL_Surface *screen) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(11, 15, 25));

    /* 1. Top Status Bar (y = 0..21) */
    fill_rect(0, 0, SCREEN_W, 21, RGB(22, 31, 50));
    fill_rect(0, 21, SCREEN_W, 1, RGB(51, 65, 85));

    /* Mode Pill (INTERVALS vs CHORDS) */
    uint32_t m_fill = (g_mode == MODE_INTERVALS) ? RGB(37, 99, 235) : RGB(124, 58, 237);
    uint32_t m_bdr  = (g_mode == MODE_INTERVALS) ? RGB(96, 165, 250) : RGB(192, 132, 252);
    const char *m_txt = (g_mode == MODE_INTERVALS) ? "INTERVALS" : "CHORDS";
    draw_pill(4, 3, 62, 15, m_fill, m_bdr);
    draw_text_centered(FONT_UI_10, 4, 3, 62, 15, m_txt, RGB(255, 255, 255));

    /* Style Pill (L1) */
    const char *style_txt = (g_mode == MODE_INTERVALS) ? k_int_style_names[g_int_style]
                                                       : k_chd_style_names[g_chd_style];
    int sw = text_width(FONT_UI_10, style_txt) + 10;
    draw_pill(70, 3, sw, 15, RGB(30, 45, 72), RGB(71, 95, 132));
    draw_text_centered(FONT_UI_10, 70, 3, sw, 15, style_txt, RGB(186, 230, 253));

    /* Difficulty Tier Pill (R1) */
    const char *tier_txt = k_tier_names[g_tier];
    int tw = text_width(FONT_UI_10, tier_txt) + 10;
    draw_pill(74 + sw, 3, tw, 15, RGB(30, 45, 72), RGB(71, 95, 132));
    draw_text_centered(FONT_UI_10, 74 + sw, 3, tw, 15, tier_txt, RGB(253, 224, 71));

    /* Streak & Session Score (Right-Aligned) */
    char score_str[64];
    int pct = (g_session_tot > 0) ? ((g_session_ok * 100) / g_session_tot) : 100;
    snprintf(score_str, sizeof(score_str), "Strk:%d  %d/%d (%d%%)",
             g_streak, g_session_ok, g_session_tot, pct);
    draw_text_right(FONT_UI_10, SCREEN_W - 5, 5, score_str, RGB(226, 232, 240));

    /* 2. Fretboard & Prompt Panel (y = 22..108) */
    render_fretboard_panel();

    /* 3. 4x3 Answer Grid (y = 111..219) */
    render_keypad_grid();

    /* 4. Bottom Controls Footer (y = 223..239) */
    fill_rect(0, 223, SCREEN_W, 17, RGB(15, 22, 36));
    fill_rect(0, 223, SCREEN_W, 1, RGB(40, 52, 74));
    draw_text_centered(FONT_UI_10, 0, 224, SCREEN_W, 16,
                       g_revealed ? "A:Next  B:Replay  X:Compare  SELECT:Mode  Y:Stats"
                                  : "A:Guess  B:Replay  L1/R1:Opt  SELECT:Mode  Y:Help",
                       RGB(148, 163, 184));

    if (g_show_help) {
        render_help_stats_modal();
    }

    present_frame(screen);
}

/* =========================================================================
 * Main Entry Point & Event Loop
 * ========================================================================= */
int main(int argc, char *argv[]) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    putenv((char *)"SDL_NOMOUSE=1");
    resolve_cfg_path();
    load_stats();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "Failed to initialize SDL video: %s\n", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
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

    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, SCREEN_BPP, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "Failed to set video mode: %s\n", SDL_GetError());
        if (audio_ok) SDL_CloseAudio();
        SDL_Quit();
        return 1;
    }
    SDL_WM_SetCaption("PowKiddy V90 Guitar Ear Trainer", NULL);

    /* Automated promo screenshot mode (--screenshot-states <dir>) */
    if (argc >= 3 && strcmp(argv[1], "--screenshot-states") == 0) {
        const char *dir = argv[2];
        char path[512];

        /* 1. Interval Guessing State (1 wrong guess made, 2 tries left) */
        g_mode = MODE_INTERVALS;
        g_int_style = INT_STYLE_ASC;
        g_tier = 2;
        g_root_str = 0;  /* Low E string */
        g_root_fret = 5; /* A2 */
        g_root_midi = 45;
        g_correct_idx = 6; /* P5 */
        g_guesses_left = 2;
        memset(g_wrong_guessed, 0, sizeof(g_wrong_guessed));
        g_wrong_guessed[4] = true; /* P4 tried */
        g_cursor = 6;
        g_revealed = false;
        g_streak = 4;
        g_session_ok = 9;
        g_session_tot = 10;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/eartrainer_intervals.bmp", dir);
        save_bmp(path);

        /* 2. Interval Revealed State (showing Root + Target on fretboard) */
        g_revealed = true;
        g_solved = true;
        g_audition_idx = 6;
        g_streak = 5;
        g_session_ok = 10;
        g_session_tot = 11;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/eartrainer_revealed.bmp", dir);
        save_bmp(path);

        /* 3. Chord Quality Mode (Revealed Maj7 chord voicing on fretboard) */
        g_mode = MODE_CHORDS;
        g_chd_style = CHD_STYLE_ARP_STRUM;
        g_root_str = 1;  /* A string */
        g_root_fret = 3; /* C3 */
        g_root_midi = 48;
        g_correct_idx = 7; /* Maj7 */
        g_audition_idx = 7;
        g_cursor = 7;
        g_revealed = true;
        g_solved = true;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/eartrainer_chords.bmp", dir);
        save_bmp(path);

        /* 4. Help & Accuracy Stats Modal */
        g_mode = MODE_INTERVALS;
        g_best_streak = 12;
        for (int i = 0; i < NUM_ITEMS; i++) {
            g_int_tot[i] = 10;
            g_int_ok[i] = 8 + (i % 3);
        }
        g_show_help = true;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/eartrainer_help.bmp", dir);
        save_bmp(path);

        if (audio_ok) SDL_CloseAudio();
        SDL_Quit();
        return 0;
    }

    generate_new_question(true);

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

                if (k == V90_BTN_MENU || k == SDLK_q) {
                    running = false;
                    break;
                }

                if (g_show_help) {
                    if (k == V90_BTN_Y || k == SDLK_y || k == SDLK_h ||
                        k == V90_BTN_A || k == SDLK_a || k == V90_BTN_B || k == SDLK_b) {
                        g_show_help = false;
                    } else if (k == V90_BTN_SELECT || k == SDLK_HOME) {
                        g_mode = (g_mode == MODE_INTERVALS) ? MODE_CHORDS : MODE_INTERVALS;
                    }
                    continue;
                }

                /* D-Pad navigation on 4x3 grid */
                if (k == V90_BTN_LEFT) {
                    g_cursor = (g_cursor % 4 == 0) ? (g_cursor + 3) : (g_cursor - 1);
                } else if (k == V90_BTN_RIGHT) {
                    g_cursor = (g_cursor % 4 == 3) ? (g_cursor - 3) : (g_cursor + 1);
                } else if (k == V90_BTN_UP) {
                    g_cursor = (g_cursor < 4) ? (g_cursor + 8) : (g_cursor - 4);
                } else if (k == V90_BTN_DOWN) {
                    g_cursor = (g_cursor >= 8) ? (g_cursor - 8) : (g_cursor + 4);
                }
                /* A button: Submit guess (or advance to next question if revealed) */
                else if (k == V90_BTN_A || k == SDLK_a) {
                    submit_guess(g_cursor);
                }
                /* B button: Replay the current question's interval/chord */
                else if (k == V90_BTN_B || k == SDLK_b || k == SDLK_r) {
                    g_audition_idx = g_correct_idx;
                    play_item_audio(g_correct_idx);
                }
                /* X button: Audition highlighted grid item when revealed */
                else if (k == V90_BTN_X || k == SDLK_x) {
                    if (g_revealed) {
                        g_audition_idx = g_cursor;
                        play_item_audio(g_cursor);
                    } else {
                        /* Before reveal, X plays the Root reference note alone */
                        clear_audio_schedule();
                        schedule_note_pluck(g_root_midi, 0, 1200);
                    }
                }
                /* Y button: Open Help & Stats modal */
                else if (k == V90_BTN_Y || k == SDLK_y || k == SDLK_h) {
                    g_show_help = true;
                }
                /* SELECT (or Home on desktop): Switch between Intervals and Chords */
                else if (k == V90_BTN_SELECT || k == SDLK_HOME) {
                    g_mode = (g_mode == MODE_INTERVALS) ? MODE_CHORDS : MODE_INTERVALS;
                    generate_new_question(true);
                }
                /* L1 (or '[' on desktop): Cycle playback style */
                else if (k == V90_BTN_L1 || k == SDLK_LEFTBRACKET) {
                    if (g_mode == MODE_INTERVALS) {
                        g_int_style = (IntervalStyle)((g_int_style + 1) % 4);
                        if (g_int_style == INT_STYLE_DESC) g_cur_is_desc = true;
                        else if (g_int_style == INT_STYLE_ASC) g_cur_is_desc = false;
                    } else {
                        g_chd_style = (ChordStyle)((g_chd_style + 1) % 3);
                    }
                    play_item_audio(g_correct_idx);
                }
                /* R1 (or ']' on desktop): Cycle difficulty tier */
                else if (k == V90_BTN_R1 || k == SDLK_RIGHTBRACKET) {
                    g_tier = (g_tier + 1) % 3;
                    save_stats();
                    generate_new_question(true);
                }
            }
        }
        render_ui(screen);
        SDL_Delay(16);
    }

    save_stats();
    if (audio_ok) SDL_CloseAudio();
    SDL_Quit();
    return 0;
}
