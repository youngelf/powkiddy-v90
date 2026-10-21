/*
 * fretboard.c — Interactive Guitar Fretboard, CAGED Scale, Chord & Arpeggio Reference
 *               with 6-Voice Karplus-Strong Plucked Guitar Synthesizer
 *
 * Targets:
 *   1. PowKiddy V90 (MiyooCFW 1.3.3, ARM926EJ-S, 320x240 16-bit RGB565 SDL 1.2 + SDL Audio)
 *   2. Linux Desktop (x86_64, X11 640x480 2x scaled window + dynamic ALSA PCM audio)
 *
 * Controls (PowKiddy V90 / Linux Desktop):
 *   SELECT (Esc / C)          : Cycle Mode: SCALES & CAGED -> CHORDS & ARPEGGIOS -> FRET EXPLORER
 *   A (LAlt / A / Z)          : Audition Scale / Strum Chord Voicing / Pluck Explorer Note
 *   B (LCtrl / B)             : Scales: Toggle Chord-Tone Highlight | Chords: Toggle Arp Overlay | Explorer: Lock/Unlock Fret
 *   X (LShift / X)            : Cycle Dot Labels (Intervals -> Note Names -> Degrees)
 *   Y (Space / Y / H)         : Toggle Help & Guitar Tuning Modal ([LEFT/RIGHT] changes Tuning!)
 *   L1 / R1 (Tab/BS / [/])    : Scales: Cycle CAGED Box (ALL NECK, POS 1..5) | Chords: Cycle 5 CAGED Voicings | Explorer: -/+3 Frets
 *   DPAD UP / DOWN            : Scales/Chords: Transpose Root Key (C .. B) | Explorer: Move String Cursor (e .. E)
 *   DPAD LEFT / RIGHT         : Scales: Cycle 14 Scales | Chords: Cycle 12 Chord Qualities | Explorer: Move Fret Cursor (0 .. 15)
 *   MENU (RCtrl / Q)          : Save configuration & Exit
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "fretboard_font.h"

#define SCREEN_W      320
#define SCREEN_H      240
#define SAMPLE_RATE   22050
#define NUM_STRINGS   6
#define MAX_FRET      15
#define NUM_FRETS     16 /* Frets 0 .. 15 */

/* =========================================================================
 * Platform Abstraction Layer: PowKiddy V90 (SDL 1.2) vs Linux Desktop (X11 + ALSA)
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

typedef uint32_t Uint32;
typedef uint16_t Uint16;
typedef uint8_t  Uint8;

#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_TIMER 0x00000001u
#define SDL_SWSURFACE  0x00000000u
#define AUDIO_S16SYS   0x8010

typedef enum {
    SDLK_UNKNOWN      = 0,
    SDLK_UP           = 273,
    SDLK_DOWN         = 274,
    SDLK_RIGHT        = 275,
    SDLK_LEFT         = 276,
    SDLK_RETURN       = 13,
    SDLK_ESCAPE       = 27,
    SDLK_SPACE        = 32,
    SDLK_TAB          = 9,
    SDLK_BACKSPACE    = 8,
    SDLK_LCTRL        = 306,
    SDLK_LALT         = 308,
    SDLK_LSHIFT       = 304,
    SDLK_RCTRL        = 305,
    SDLK_LEFTBRACKET  = 91,
    SDLK_RIGHTBRACKET = 93,
    SDLK_MINUS        = 45,
    SDLK_EQUALS       = 61,
    SDLK_PAGEUP       = 280,
    SDLK_PAGEDOWN     = 281,
    SDLK_a            = 'a',
    SDLK_b            = 'b',
    SDLK_c            = 'c',
    SDLK_h            = 'h',
    SDLK_q            = 'q',
    SDLK_v            = 'v',
    SDLK_x            = 'x',
    SDLK_y            = 'y',
    SDLK_z            = 'z'
} SDLKey;

#define SDL_QUIT    12
#define SDL_KEYDOWN 2
#define SDL_KEYUP   3

typedef struct {
    SDLKey sym;
} SDL_keysym;

typedef struct {
    Uint8 type;
    SDL_keysym keysym;
} SDL_KeyboardEvent;

typedef union {
    Uint8 type;
    SDL_KeyboardEvent key;
} SDL_Event;

typedef struct {
    Uint8 BitsPerPixel;
} SDL_PixelFormat;

typedef struct {
    int w, h;
    int pitch;
    void *pixels;
    SDL_PixelFormat *format;
} SDL_Surface;

typedef struct {
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint16 samples;
    void (*callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
} SDL_AudioSpec;

static Display        *g_x11_dpy    = NULL;
static Window          g_x11_win    = 0;
static GC              g_x11_gc     = 0;
static XImage         *g_x11_img    = NULL;
static Atom            g_wm_delete  = 0;
static uint32_t        g_logical_fb[SCREEN_W * SCREEN_H];
static uint32_t       *g_scaled_fb  = NULL;
static SDL_PixelFormat g_x11_fmt    = { 32 };
static SDL_Surface     g_x11_surf   = { SCREEN_W, SCREEN_H, SCREEN_W * 4, g_logical_fb, &g_x11_fmt };

/* Dynamic ALSA audio thread for Linux desktop */
typedef struct _snd_pcm snd_pcm_t;
typedef long snd_pcm_sframes_t;
#define SND_PCM_STREAM_PLAYBACK 0
#define SND_PCM_FORMAT_S16_LE   2

static void        *g_alsa_dl     = NULL;
static snd_pcm_t   *g_alsa_pcm    = NULL;
static pthread_t    g_audio_thr;
static volatile int g_audio_run   = 0;
static volatile int g_audio_pause = 1;
static SDL_AudioSpec g_audio_spec;

typedef int (*fn_snd_pcm_open_t)(snd_pcm_t **, const char *, int, int);
typedef int (*fn_snd_pcm_set_params_t)(snd_pcm_t *, int, int, unsigned int, unsigned int, int, unsigned int);
typedef snd_pcm_sframes_t (*fn_snd_pcm_writei_t)(snd_pcm_t *, const void *, unsigned long);
typedef int (*fn_snd_pcm_recover_t)(snd_pcm_t *, int, int);
typedef int (*fn_snd_pcm_close_t)(snd_pcm_t *);

static fn_snd_pcm_open_t       p_snd_pcm_open       = NULL;
static fn_snd_pcm_set_params_t p_snd_pcm_set_params = NULL;
static fn_snd_pcm_writei_t     p_snd_pcm_writei     = NULL;
static fn_snd_pcm_recover_t    p_snd_pcm_recover    = NULL;
static fn_snd_pcm_close_t      p_snd_pcm_close      = NULL;

static void *alsa_audio_thread(void *arg) {
    (void)arg;
    int frames = g_audio_spec.samples > 0 ? g_audio_spec.samples : 512;
    int16_t *buf = (int16_t *)calloc((size_t)frames, sizeof(int16_t));
    if (!buf) return NULL;

    while (g_audio_run) {
        if (g_audio_pause || !g_audio_spec.callback) {
            memset(buf, 0, (size_t)frames * sizeof(int16_t));
        } else {
            g_audio_spec.callback(g_audio_spec.userdata, (Uint8 *)buf, frames * (int)sizeof(int16_t));
        }
        if (g_alsa_pcm && p_snd_pcm_writei) {
            snd_pcm_sframes_t w = p_snd_pcm_writei(g_alsa_pcm, buf, (unsigned long)frames);
            if (w < 0 && p_snd_pcm_recover) {
                p_snd_pcm_recover(g_alsa_pcm, (int)w, 1);
            }
        } else {
            usleep((useconds_t)((frames * 1000000LL) / g_audio_spec.freq));
        }
    }
    free(buf);
    return NULL;
}

static int SDL_Init(Uint32 flags) {
    (void)flags;
    g_x11_dpy = XOpenDisplay(NULL);
    if (!g_x11_dpy) {
        fprintf(stderr, "XOpenDisplay failed\n");
        return -1;
    }
    return 0;
}

static SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags) {
    (void)w; (void)h; (void)bpp; (void)flags;
    int screen = DefaultScreen(g_x11_dpy);
    int win_w  = SCREEN_W * HOST_SCALE;
    int win_h  = SCREEN_H * HOST_SCALE;

    g_x11_win = XCreateSimpleWindow(
        g_x11_dpy, RootWindow(g_x11_dpy, screen),
        40, 40, (unsigned int)win_w, (unsigned int)win_h, 1,
        BlackPixel(g_x11_dpy, screen), BlackPixel(g_x11_dpy, screen));

    XStoreName(g_x11_dpy, g_x11_win, "Fretboard - Guitar Scale, CAGED & Chord Reference (PowKiddy V90)");
    XSelectInput(g_x11_dpy, g_x11_win, ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask);

    g_wm_delete = XInternAtom(g_x11_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_x11_dpy, g_x11_win, &g_wm_delete, 1);

    g_x11_gc    = XCreateGC(g_x11_dpy, g_x11_win, 0, NULL);
    g_scaled_fb = (uint32_t *)calloc((size_t)(win_w * win_h), sizeof(uint32_t));
    g_x11_img   = XCreateImage(
        g_x11_dpy, DefaultVisual(g_x11_dpy, screen),
        (unsigned int)DefaultDepth(g_x11_dpy, screen),
        ZPixmap, 0, (char *)g_scaled_fb,
        (unsigned int)win_w, (unsigned int)win_h, 32, 0);

    XMapWindow(g_x11_dpy, g_x11_win);
    XFlush(g_x11_dpy);
    return &g_x11_surf;
}

static void SDL_ShowCursor(int toggle) { (void)toggle; }

static void SDL_Flip(SDL_Surface *s) {
    (void)s;
    if (!g_x11_dpy || !g_x11_img) return;
    int scale = HOST_SCALE;
    int win_w = SCREEN_W * scale;
    for (int y = 0; y < SCREEN_H; y++) {
        const uint32_t *src = &g_logical_fb[y * SCREEN_W];
        for (int sy = 0; sy < scale; sy++) {
            uint32_t *dst = &g_scaled_fb[(y * scale + sy) * win_w];
            for (int x = 0; x < SCREEN_W; x++) {
                uint32_t px = src[x];
                for (int sx = 0; sx < scale; sx++) {
                    *dst++ = px;
                }
            }
        }
    }
    XPutImage(g_x11_dpy, g_x11_win, g_x11_gc, g_x11_img,
              0, 0, 0, 0, (unsigned int)(SCREEN_W * scale), (unsigned int)(SCREEN_H * scale));
    XFlush(g_x11_dpy);
}

static SDLKey map_x11_keysym(KeySym ks) {
    switch (ks) {
        case XK_Up:           return SDLK_UP;
        case XK_Down:         return SDLK_DOWN;
        case XK_Left:         return SDLK_LEFT;
        case XK_Right:        return SDLK_RIGHT;
        case XK_Return:
        case XK_KP_Enter:     return SDLK_RETURN;
        case XK_Escape:       return SDLK_ESCAPE;
        case XK_space:        return SDLK_SPACE;
        case XK_Tab:          return SDLK_TAB;
        case XK_BackSpace:    return SDLK_BACKSPACE;
        case XK_Control_L:    return SDLK_LCTRL;
        case XK_Alt_L:        return SDLK_LALT;
        case XK_Shift_L:      return SDLK_LSHIFT;
        case XK_Control_R:    return SDLK_RCTRL;
        case XK_bracketleft:  return SDLK_LEFTBRACKET;
        case XK_bracketright: return SDLK_RIGHTBRACKET;
        case XK_minus:        return SDLK_MINUS;
        case XK_equal:        return SDLK_EQUALS;
        case XK_Page_Up:      return SDLK_PAGEUP;
        case XK_Page_Down:    return SDLK_PAGEDOWN;
        case XK_a: case XK_A: return SDLK_a;
        case XK_b: case XK_B: return SDLK_b;
        case XK_c: case XK_C: return SDLK_c;
        case XK_h: case XK_H: return SDLK_h;
        case XK_q: case XK_Q: return SDLK_q;
        case XK_v: case XK_V: return SDLK_v;
        case XK_x: case XK_X: return SDLK_x;
        case XK_y: case XK_Y: return SDLK_y;
        case XK_z: case XK_Z: return SDLK_z;
        default:              return SDLK_UNKNOWN;
    }
}

static int SDL_PollEvent(SDL_Event *ev) {
    if (!g_x11_dpy) return 0;
    while (XPending(g_x11_dpy) > 0) {
        XEvent xev;
        XNextEvent(g_x11_dpy, &xev);
        if (xev.type == ClientMessage && (Atom)xev.xclient.data.l[0] == g_wm_delete) {
            ev->type = SDL_QUIT;
            return 1;
        }
        if (xev.type == KeyPress || xev.type == KeyRelease) {
            if (xev.type == KeyRelease && XEventsQueued(g_x11_dpy, QueuedAfterReading)) {
                XEvent nev;
                XPeekEvent(g_x11_dpy, &nev);
                if (nev.type == KeyPress && nev.xkey.time == xev.xkey.time &&
                    nev.xkey.keycode == xev.xkey.keycode) {
                    XNextEvent(g_x11_dpy, &xev);
                    continue;
                }
            }
            KeySym ks = XLookupKeysym(&xev.xkey, 0);
            SDLKey mapped = map_x11_keysym(ks);
            if (mapped != SDLK_UNKNOWN) {
                ev->type = (xev.type == KeyPress) ? SDL_KEYDOWN : SDL_KEYUP;
                ev->key.keysym.sym = mapped;
                return 1;
            }
        }
    }
    return 0;
}

static int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
    g_audio_spec = *desired;
    if (obtained) *obtained = *desired;

    g_alsa_dl = dlopen("libasound.so.2", RTLD_LAZY);
    if (g_alsa_dl) {
        p_snd_pcm_open       = (fn_snd_pcm_open_t)dlsym(g_alsa_dl, "snd_pcm_open");
        p_snd_pcm_set_params = (fn_snd_pcm_set_params_t)dlsym(g_alsa_dl, "snd_pcm_set_params");
        p_snd_pcm_writei     = (fn_snd_pcm_writei_t)dlsym(g_alsa_dl, "snd_pcm_writei");
        p_snd_pcm_recover    = (fn_snd_pcm_recover_t)dlsym(g_alsa_dl, "snd_pcm_recover");
        p_snd_pcm_close      = (fn_snd_pcm_close_t)dlsym(g_alsa_dl, "snd_pcm_close");

        if (p_snd_pcm_open && p_snd_pcm_set_params) {
            if (p_snd_pcm_open(&g_alsa_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) >= 0) {
                p_snd_pcm_set_params(g_alsa_pcm, SND_PCM_FORMAT_S16_LE, 3,
                                     1, (unsigned int)desired->freq, 1, 50000);
            } else {
                g_alsa_pcm = NULL;
            }
        }
    }
    g_audio_run   = 1;
    g_audio_pause = 1;
    pthread_create(&g_audio_thr, NULL, alsa_audio_thread, NULL);
    return 0;
}

static void SDL_PauseAudio(int pause_on) {
    g_audio_pause = pause_on;
}

static void SDL_CloseAudio(void) {
    if (g_audio_run) {
        g_audio_run = 0;
        pthread_join(g_audio_thr, NULL);
    }
    if (g_alsa_pcm && p_snd_pcm_close) {
        p_snd_pcm_close(g_alsa_pcm);
        g_alsa_pcm = NULL;
    }
    if (g_alsa_dl) {
        dlclose(g_alsa_dl);
        g_alsa_dl = NULL;
    }
}

static void SDL_Delay(Uint32 ms) {
    usleep(ms * 1000);
}

static void SDL_Quit(void) {
    if (g_x11_img) {
        XDestroyImage(g_x11_img);
        g_x11_img   = NULL;
        g_scaled_fb = NULL;
    }
    if (g_x11_dpy) {
        if (g_x11_gc)  XFreeGC(g_x11_dpy, g_x11_gc);
        if (g_x11_win) XDestroyWindow(g_x11_dpy, g_x11_win);
        XCloseDisplay(g_x11_dpy);
        g_x11_dpy = NULL;
    }
}

#else /* PowKiddy V90 native SDL 1.2 */
#include <SDL/SDL.h>
#endif

/* =========================================================================
 * PowKiddy V90 Button Mappings (MiyooCFW 1.3.3)
 * ========================================================================= */
#define V90_BTN_UP      SDLK_UP
#define V90_BTN_DOWN    SDLK_DOWN
#define V90_BTN_LEFT    SDLK_LEFT
#define V90_BTN_RIGHT   SDLK_RIGHT
#define V90_BTN_A       SDLK_LALT       /* A (East)  : Audition Scale / Strum Chord / Pluck Note */
#define V90_BTN_B       SDLK_LCTRL      /* B (South) : Toggle Skeleton / Arp / Lock Note */
#define V90_BTN_X       SDLK_LSHIFT     /* X (North) : Cycle Dot Labels (Intervals / Notes / Degrees) */
#define V90_BTN_Y       SDLK_SPACE      /* Y (West)  : Toggle Help & Guitar Tuning modal */
#define V90_BTN_L1      SDLK_TAB        /* L1        : Prev CAGED Box / Chord Voicing */
#define V90_BTN_R1      SDLK_BACKSPACE  /* R1        : Next CAGED Box / Chord Voicing */
#define V90_BTN_SELECT  SDLK_ESCAPE     /* SELECT    : Cycle Mode (SCALES -> CHORDS -> EXPLORER) */
#define V90_BTN_START   SDLK_RETURN     /* START     : Save & Exit */
#define V90_BTN_MENU    SDLK_RCTRL      /* MENU      : Save & Exit */

/* =========================================================================
 * 24-bit Framebuffer & Anti-Aliased Graphics Primitives
 * ========================================================================= */
static uint32_t g_fb[SCREEN_W * SCREEN_H];

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

static inline void put_pixel(int x, int y, uint32_t col) {
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
        g_fb[y * SCREEN_W + x] = col;
    }
}

static inline uint32_t blend_rgb(uint32_t bg, uint32_t fg, uint8_t alpha) {
    if (alpha == 0)   return bg;
    if (alpha == 255) return fg;
    uint32_t a = alpha;
    uint32_t ia = 255 - a;
    uint32_t r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * ia) >> 8;
    uint32_t g = (((fg >> 8)  & 0xFF) * a + ((bg >> 8)  & 0xFF) * ia) >> 8;
    uint32_t b = ((fg         & 0xFF) * a + (bg         & 0xFF) * ia) >> 8;
    return (r << 16) | (g << 8) | b;
}

static inline void blend_pixel(int x, int y, uint32_t col, uint8_t alpha) {
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
        uint32_t bg = g_fb[y * SCREEN_W + x];
        g_fb[y * SCREEN_W + x] = blend_rgb(bg, col, alpha);
    }
}

static void fill_rect(int x, int y, int w, int h, uint32_t col) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > SCREEN_W ? SCREEN_W : (x + w);
    int y1 = (y + h) > SCREEN_H ? SCREEN_H : (y + h);
    for (int py = y0; py < y1; py++) {
        uint32_t *row = &g_fb[py * SCREEN_W + x0];
        for (int px = x0; px < x1; px++) {
            *row++ = col;
        }
    }
}

static void draw_border(int x, int y, int w, int h, uint32_t col) {
    fill_rect(x, y, w, 1, col);
    fill_rect(x, y + h - 1, w, 1, col);
    fill_rect(x, y, 1, h, col);
    fill_rect(x + w - 1, y, 1, h, col);
}

static void draw_pill(int x, int y, int w, int h, uint32_t fill_col, uint32_t border_col) {
    if (w < 4 || h < 4) {
        fill_rect(x, y, w, h, fill_col);
        return;
    }
    fill_rect(x + 1, y + 1, w - 2, h - 2, fill_col);
    fill_rect(x + 2, y, w - 4, 1, border_col);
    fill_rect(x + 2, y + h - 1, w - 4, 1, border_col);
    fill_rect(x, y + 2, 1, h - 4, border_col);
    fill_rect(x + w - 1, y + 2, 1, h - 4, border_col);
    put_pixel(x + 1, y + 1, border_col);
    put_pixel(x + w - 2, y + 1, border_col);
    put_pixel(x + 1, y + h - 2, border_col);
    put_pixel(x + w - 2, y + h - 2, border_col);
}

static void draw_filled_circle_aa(float cx, float cy, float r, uint32_t fill_col, uint32_t border_col) {
    int x0 = (int)floorf(cx - r - 1.5f);
    int x1 = (int)ceilf(cx + r + 1.5f);
    int y0 = (int)floorf(cy - r - 1.5f);
    int y1 = (int)ceilf(cy + r + 1.5f);

    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float dx = (float)px + 0.5f - cx;
            float dy = (float)py + 0.5f - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist <= r - 1.1f) {
                put_pixel(px, py, fill_col);
            } else if (dist <= r + 0.6f) {
                float edge_t = (dist - (r - 1.1f)) / 1.1f;
                if (edge_t < 0.0f) edge_t = 0.0f;
                if (edge_t > 1.0f) edge_t = 1.0f;
                uint32_t c = blend_rgb(fill_col, border_col, (uint8_t)(edge_t * 255.0f));
                float out_a = 1.0f - (dist - (r - 0.4f));
                if (out_a < 0.0f) out_a = 0.0f;
                if (out_a > 1.0f) out_a = 1.0f;
                blend_pixel(px, py, c, (uint8_t)(out_a * 255.0f));
            }
        }
    }
}

static int text_width(int font_id, const char *s) {
    if (!s) return 0;
    const FbFont *f = &g_fb_fonts[font_id];
    int w = 0;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126) c = '?';
        w += f->glyphs[c - 32].xadvance;
    }
    return w;
}

static void draw_text(int font_id, int x, int y, const char *s, uint32_t color) {
    if (!s) return;
    const FbFont *f = &g_fb_fonts[font_id];
    int cx = x;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126) c = '?';
        const FbGlyph *g = &f->glyphs[c - 32];
        const uint8_t *bmp = &g_fb_font_bitmap[g->data_offset];
        for (int gy = 0; gy < g->height; gy++) {
            int py = y + g->yoff + gy;
            for (int gx = 0; gx < g->width; gx++) {
                uint8_t a = bmp[gy * g->width + gx];
                if (a > 0) {
                    blend_pixel(cx + g->xoff + gx, py, color, a);
                }
            }
        }
        cx += g->xadvance;
    }
}

static void draw_text_centered(int font_id, int bx, int by, int bw, int bh, const char *s, uint32_t color) {
    const FbFont *f = &g_fb_fonts[font_id];
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
    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    if (screen->format->BitsPerPixel == 16) {
        uint16_t *dst = (uint16_t *)screen->pixels;
        int pitch16 = screen->pitch / 2;
        for (int y = 0; y < SCREEN_H; y++) {
            const uint32_t *src = &g_fb[y * SCREEN_W];
            uint16_t *row = &dst[y * pitch16];
            for (int x = 0; x < SCREEN_W; x++) {
                uint32_t c = src[x];
                uint16_t r5 = (uint16_t)((c >> 19) & 0x1F);
                uint16_t g6 = (uint16_t)((c >> 10) & 0x3F);
                uint16_t b5 = (uint16_t)((c >> 3)  & 0x1F);
                row[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
            }
        }
    } else if (screen->format->BitsPerPixel == 32) {
        memcpy(screen->pixels, g_fb, sizeof(g_fb));
    }
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
    SDL_Flip(screen);
#endif
}

static int save_bmp(const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    uint32_t row_stride = SCREEN_W * 3;
    uint32_t img_size   = row_stride * SCREEN_H;
    uint32_t file_size  = 54 + img_size;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (uint8_t)(file_size & 0xFF);
    hdr[3] = (uint8_t)((file_size >> 8) & 0xFF);
    hdr[4] = (uint8_t)((file_size >> 16) & 0xFF);
    hdr[5] = (uint8_t)((file_size >> 24) & 0xFF);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (uint8_t)(SCREEN_W & 0xFF);
    hdr[19] = (uint8_t)((SCREEN_W >> 8) & 0xFF);
    hdr[22] = (uint8_t)(SCREEN_H & 0xFF);
    hdr[23] = (uint8_t)((SCREEN_H >> 8) & 0xFF);
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, 54, fp);
    uint8_t row_buf[SCREEN_W * 3];
    for (int y = SCREEN_H - 1; y >= 0; y--) {
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t c = g_fb[y * SCREEN_W + x];
            row_buf[x * 3 + 0] = (uint8_t)(c & 0xFF);
            row_buf[x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
            row_buf[x * 3 + 2] = (uint8_t)((c >> 16) & 0xFF);
        }
        fwrite(row_buf, 1, row_stride, fp);
    }
    fclose(fp);
    return 0;
}

/* =========================================================================
 * Music Theory Database: Notes, Intervals, Tunings, Scales & Chords
 * ========================================================================= */
static const char *k_note_names[12] = {
    "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

static const char *k_interval_labels[12] = {
    "R", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7"
};

static const char *k_degree_labels[12] = {
    "1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7"
};

/* 6 Guitar Tunings: MIDI notes for strings 0 (low E) .. 5 (high e)
 * Display rows are 0 = high string (str_idx 5) down to 5 = low string (str_idx 0).
 */
typedef struct {
    const char *name;
    const char *short_name;
    int         midi[6];          /* String 0 (6th/lowest) to String 5 (1st/highest) */
    const char *row_lbl[6];       /* Top row (high string) to Bottom row (low string) */
} GuitarTuning;

static const GuitarTuning k_tunings[6] = {
    { "Standard (E A D G B e)",     "Std E",   { 40, 45, 50, 55, 59, 64 }, { "e", "B", "G", "D", "A", "E" } },
    { "Drop D (D A D G B e)",       "Drop D",  { 38, 45, 50, 55, 59, 64 }, { "e", "B", "G", "D", "A", "D" } },
    { "Eb Standard (Eb Ab Db Gb Bb eb)", "Eb Std", { 39, 44, 49, 54, 58, 63 }, { "eb","Bb","Gb","Db","Ab","Eb"} },
    { "D Standard (D G C F A d)",   "D Std",   { 38, 43, 48, 53, 57, 62 }, { "d", "A", "F", "C", "G", "D" } },
    { "DADGAD (D A D G A d)",       "DADGAD",  { 38, 45, 50, 55, 57, 62 }, { "d", "A", "G", "D", "A", "D" } },
    { "Open G (D G D G B d)",       "Open G",  { 38, 43, 50, 55, 59, 62 }, { "d", "B", "G", "D", "G", "D" } }
};
#define NUM_TUNINGS 6

/* 14 Guitar Scales & Modes
 * mask[0..11]:
 *   0 = not in scale
 *   1 = regular scale degree (2, 4, 6)
 *   2 = chord skeleton tone (3, b3, 5, 7, b7)
 *   3 = characteristic color / blue note (b5, Dorian 6, Lydian #4, Phrygian b2, etc.)
 *   4 = Root note (interval 0)
 */
typedef struct {
    const char *name;
    const char *short_name;
    const char *formula;
    const char *skeleton_chord;
    uint8_t     mask[12];
} ScaleDef;

static const ScaleDef k_scales[14] = {
    {
        "Minor Pentatonic", "Min Pent",
        "1 - b3 - 4 - 5 - b7", "m7 / 7",
        { 4, 0, 0, 2, 0, 1, 0, 2, 0, 0, 2, 0 }
    },
    {
        "Blues Scale", "Blues",
        "1 - b3 - 4 - b5 - 5 - b7", "7 / m7 (b5 Blue)",
        { 4, 0, 0, 2, 0, 1, 3, 2, 0, 0, 2, 0 }
    },
    {
        "Major Pentatonic", "Maj Pent",
        "1 - 2 - 3 - 5 - 6", "Maj / 6 / Maj7",
        { 4, 0, 1, 0, 2, 0, 0, 2, 0, 3, 0, 0 }
    },
    {
        "Natural Major (Ionian)", "Major",
        "1 - 2 - 3 - 4 - 5 - 6 - 7", "Maj7 (I - IV - V)",
        { 4, 0, 1, 0, 2, 1, 0, 2, 0, 1, 0, 2 }
    },
    {
        "Natural Minor (Aeolian)", "Nat Minor",
        "1 - 2 - b3 - 4 - 5 - b6 - b7", "m7 (i - iv - v)",
        { 4, 0, 1, 2, 0, 1, 0, 2, 3, 0, 2, 0 }
    },
    {
        "Dorian Mode", "Dorian",
        "1 - 2 - b3 - 4 - 5 - 6 - b7", "m7 / m6 (Nat 6)",
        { 4, 0, 1, 2, 0, 1, 0, 2, 0, 3, 2, 0 }
    },
    {
        "Mixolydian Mode", "Mixolydian",
        "1 - 2 - 3 - 4 - 5 - 6 - b7", "Dom 7 (Flat 7)",
        { 4, 0, 1, 0, 2, 1, 0, 2, 0, 1, 3, 0 }
    },
    {
        "Lydian Mode", "Lydian",
        "1 - 2 - 3 - #4 - 5 - 6 - 7", "Maj7#11 (#4)",
        { 4, 0, 1, 0, 2, 0, 3, 2, 0, 1, 0, 2 }
    },
    {
        "Phrygian Mode", "Phrygian",
        "1 - b2 - b3 - 4 - 5 - b6 - b7", "m7 / susb9 (b2)",
        { 4, 3, 0, 2, 0, 1, 0, 2, 1, 0, 2, 0 }
    },
    {
        "Harmonic Minor", "Harm Minor",
        "1 - 2 - b3 - 4 - 5 - b6 - 7", "m(maj7) (Lead 7)",
        { 4, 0, 1, 2, 0, 1, 0, 2, 1, 0, 0, 3 }
    },
    {
        "Melodic Minor (Jazz)", "Mel Minor",
        "1 - 2 - b3 - 4 - 5 - 6 - 7", "m(maj7) / m6",
        { 4, 0, 1, 2, 0, 1, 0, 2, 0, 3, 0, 3 }
    },
    {
        "Phrygian Dominant", "Phryg Dom",
        "1 - b2 - 3 - 4 - 5 - b6 - b7", "7b9 (Flamenco/Metal)",
        { 4, 3, 0, 0, 2, 1, 0, 2, 1, 0, 2, 0 }
    },
    {
        "Locrian Mode", "Locrian",
        "1 - b2 - b3 - 4 - b5 - b6 - b7", "m7b5 (Half-Dim)",
        { 4, 1, 0, 2, 0, 1, 3, 0, 1, 0, 2, 0 }
    },
    {
        "Whole-Half Diminished", "Diminished",
        "1 - 2 - b3 - 4 - b5 - b6 - 6 - 7", "dim7 (Symmetrical)",
        { 4, 0, 1, 2, 0, 1, 3, 0, 1, 2, 0, 3 }
    }
};
#define NUM_SCALES 14

/* 12 Guitar Chord & Arpeggio Qualities */
typedef struct {
    const char *name;
    const char *suffix;
    const char *formula;
    uint8_t     mask[12];      /* 4=Root, 2=3rd/5th, 3=7th/Color extension */
    int         pref_bass_str; /* Lowest string typically played in root-position shapes */
} ChordQualityDef;

static const ChordQualityDef k_chords[12] = {
    { "Major Triad",      "",     "1 - 3 - 5",        { 4,0,0,0,2,0,0,2,0,0,0,0 }, 0 },
    { "Minor Triad",      "m",    "1 - b3 - 5",       { 4,0,0,2,0,0,0,2,0,0,0,0 }, 0 },
    { "Dominant 7th",     "7",    "1 - 3 - 5 - b7",   { 4,0,0,0,2,0,0,2,0,0,3,0 }, 0 },
    { "Major 7th",        "maj7", "1 - 3 - 5 - 7",    { 4,0,0,0,2,0,0,2,0,0,0,3 }, 0 },
    { "Minor 7th",        "m7",   "1 - b3 - 5 - b7",  { 4,0,0,2,0,0,0,2,0,0,3,0 }, 0 },
    { "Half-Diminished",  "m7b5", "1 - b3 - b5 - b7", { 4,0,0,2,0,0,3,0,0,0,2,0 }, 1 },
    { "Diminished 7th",   "dim7", "1 - b3 - b5 - 6",  { 4,0,0,2,0,0,3,0,0,2,0,0 }, 1 },
    { "Suspended 2nd",    "sus2", "1 - 2 - 5",        { 4,0,3,0,0,0,0,2,0,0,0,0 }, 1 },
    { "Suspended 4th",    "sus4", "1 - 4 - 5",        { 4,0,0,0,0,3,0,2,0,0,0,0 }, 0 },
    { "Major Add 9",      "add9", "1 - 2 - 3 - 5",    { 4,0,3,0,2,0,0,2,0,0,0,0 }, 0 },
    { "Major 6th",        "6",    "1 - 3 - 5 - 6",    { 4,0,0,0,2,0,0,2,0,3,0,0 }, 0 },
    { "Minor 6th",        "m6",   "1 - b3 - 5 - 6",   { 4,0,0,2,0,0,0,2,0,3,0,0 }, 0 }
};
#define NUM_CHORDS 12

static const char *k_caged_pos_names[6] = {
    "ALL NECK",
    "POS 1 (E)",
    "POS 2 (D)",
    "POS 3 (C)",
    "POS 4 (A)",
    "POS 5 (G)"
};

/* CAGED box offsets relative to 6th-string Root fret:
 *   POS 1 (E-Shape): [root_fret - 1 .. root_fret + 3]
 *   POS 2 (D-Shape): [root_fret + 2 .. root_fret + 6]
 *   POS 3 (C-Shape): [root_fret + 4 .. root_fret + 8]
 *   POS 4 (A-Shape): [root_fret + 7 .. root_fret + 11]
 *   POS 5 (G-Shape): [root_fret + 9 .. root_fret + 13]
 */
static const int k_caged_rel_start[5] = { 0, 2, 4, 7, 9 };

/* =========================================================================
 * Application State
 * ========================================================================= */
typedef enum {
    MODE_SCALES   = 0,
    MODE_CHORDS   = 1,
    MODE_EXPLORER = 2
} AppMode;

typedef enum {
    LABEL_INTERVALS = 0,
    LABEL_NOTES     = 1,
    LABEL_DEGREES   = 2
} DotLabelMode;

static AppMode      g_mode           = MODE_SCALES;
static DotLabelMode g_label_mode     = LABEL_INTERVALS;
static bool         g_show_help      = false;

static int          g_root_pc        = 9;    /* Default: Key of A (9) */
static int          g_scale_idx      = 1;    /* Default: Blues Scale (1) */
static int          g_caged_pos      = 1;    /* Default: POS 1 (E-Shape) in Scales */
static bool         g_chord_skeleton = true; /* Highlight chord tones inside scale */

static int          g_chord_idx      = 3;    /* Default in Chords mode: maj7 */
static int          g_voicing_pos    = 0;    /* 0..4 (5 CAGED voicings up the neck) */
static bool         g_show_arp_bg    = true; /* Show full-neck arpeggio behind chord voicing */

static int          g_tuning_idx     = 0;    /* Default: Standard E A D G B e */

/* Explorer Mode State (Reverse Chord & Note Finder) */
static int          g_exp_row        = 2;    /* 0 = high e .. 5 = low E */
static int          g_exp_fret       = 5;    /* 0 .. 15 */
static int          g_exp_locked[6]  = { 0, 2, 2, 0, 0, 0 }; /* Fret per string 0..5, or -1 if unfretted */

/* ActiveKarplus-Strong Pluck Highlight on Fretboard */
static volatile int g_active_play_str  = -1; /* 0..5 (0=low E, 5=high e) */
static volatile int g_active_play_fret = -1; /* 0..15 */
static volatile int g_active_play_ttl  = 0;  /* Audio frames remaining for glow */

/* =========================================================================
 * 6-Voice Karplus-Strong Plucked Guitar String Synthesizer & Sequencer
 * ========================================================================= */
#define KS_MAX_DELAY 400

typedef struct {
    bool    active;
    int     delay_len;
    int     ptr;
    int     samples_left;
    int     string_idx;
    int     fret;
    int16_t line[KS_MAX_DELAY];
} KSVoice;

typedef struct {
    int string_idx; /* 0 (low E) .. 5 (high e) */
    int fret;       /* 0 .. 15 */
    int midi_note;
    int delay_samples;
} PluckEvent;

#define MAX_PLUCK_QUEUE 48
static KSVoice    g_voices[NUM_STRINGS];
static PluckEvent g_pluck_queue[MAX_PLUCK_QUEUE];
static int        g_pluck_count = 0;
static int        g_pluck_head  = 0;
static uint32_t   g_rng_state   = 0x9E3779B9u;

static inline int16_t ks_rand_s16(void) {
    g_rng_state ^= g_rng_state << 13;
    g_rng_state ^= g_rng_state >> 17;
    g_rng_state ^= g_rng_state << 5;
    return (int16_t)((g_rng_state & 0xFFFFu) - 32768);
}

static float midi_to_freq(int midi) {
    return 440.0f * powf(2.0f, (float)(midi - 69) / 12.0f);
}

static void trigger_ks_voice(int str_idx, int fret, int midi_note) {
    if (str_idx < 0 || str_idx >= NUM_STRINGS) return;
    float freq = midi_to_freq(midi_note);
    int dlen = (int)roundf((float)SAMPLE_RATE / freq);
    if (dlen < 12) dlen = 12;
    if (dlen >= KS_MAX_DELAY) dlen = KS_MAX_DELAY - 1;

    KSVoice *v = &g_voices[str_idx];
    v->delay_len    = dlen;
    v->ptr          = 0;
    v->samples_left = SAMPLE_RATE * 2; /* 2.0 seconds decay */
    v->string_idx   = str_idx;
    v->fret         = fret;

    /* Warm low-passed initial pluck excitation */
    int32_t prev = 0;
    for (int i = 0; i < dlen; i++) {
        int32_t r = ks_rand_s16() / 3;
        prev = (prev * 5 + r * 3) >> 3;
        v->line[i] = (int16_t)prev;
    }
    v->active = true;

    g_active_play_str  = str_idx;
    g_active_play_fret = fret;
    g_active_play_ttl  = SAMPLE_RATE / 4;
}

static void clear_pluck_queue(void) {
    SDL_PauseAudio(1);
    g_pluck_count     = 0;
    g_pluck_head      = 0;
    g_active_play_ttl = 0;
    SDL_PauseAudio(0);
}

static void enqueue_pluck(int str_idx, int fret, int delay_ms) {
    if (g_pluck_count >= MAX_PLUCK_QUEUE) return;
    int midi = k_tunings[g_tuning_idx].midi[str_idx] + fret;
    PluckEvent *ev = &g_pluck_queue[g_pluck_count++];
    ev->string_idx    = str_idx;
    ev->fret          = fret;
    ev->midi_note     = midi;
    ev->delay_samples = (delay_ms * SAMPLE_RATE) / 1000;
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int nsamples = len / (int)sizeof(int16_t);

    for (int i = 0; i < nsamples; i++) {
        while (g_pluck_head < g_pluck_count) {
            if (g_pluck_queue[g_pluck_head].delay_samples <= 0) {
                PluckEvent ev = g_pluck_queue[g_pluck_head++];
                trigger_ks_voice(ev.string_idx, ev.fret, ev.midi_note);
            } else {
                g_pluck_queue[g_pluck_head].delay_samples--;
                break;
            }
        }

        if (g_active_play_ttl > 0) {
            g_active_play_ttl--;
            if (g_active_play_ttl == 0) {
                g_active_play_str  = -1;
                g_active_play_fret = -1;
            }
        }

        int32_t mix = 0;
        for (int s = 0; s < NUM_STRINGS; s++) {
            KSVoice *v = &g_voices[s];
            if (!v->active) continue;
            int cur_idx  = v->ptr;
            int next_idx = (cur_idx + 1) % v->delay_len;
            int32_t s0   = v->line[cur_idx];
            int32_t s1   = v->line[next_idx];
            int32_t avg  = ((s0 + s1) * 254) >> 9; /* Gentle warm acoustic damping */
            v->line[cur_idx] = (int16_t)avg;
            v->ptr = next_idx;
            mix += s0;
            if (--v->samples_left <= 0) {
                v->active = false;
            }
        }
        if (mix > 32767)  mix = 32767;
        if (mix < -32768) mix = -32768;
        out[i] = (int16_t)mix;
    }
}

/* =========================================================================
 * CAGED Box & Playable Chord Voicing Calculator (Supports All 6 Tunings!)
 * ========================================================================= */
static void compute_caged_box_range(int root_pc, int caged_pos, int *out_low, int *out_high) {
    if (caged_pos <= 0) {
        *out_low  = 0;
        *out_high = MAX_FRET;
        return;
    }
    int open_low_pc = k_tunings[g_tuning_idx].midi[0] % 12;
    int root_fret_6 = (root_pc - open_low_pc + 12) % 12;
    int start = root_fret_6 + k_caged_rel_start[caged_pos - 1];
    while (start > 11) {
        start -= 12;
    }
    if (start < 0) start = 0;
    int low  = (start > 0) ? (start - 1) : 0;
    int high = low + 4;
    if (high > MAX_FRET) high = MAX_FRET;
    *out_low  = low;
    *out_high = high;
}

/* Computes a realistic, playable 6-string guitar chord voicing for:
 *   - Any Root Key (0..11)
 *   - Any Chord Quality (0..11)
 *   - Any of the 5 CAGED positions up the neck (voicing_idx 0..4)
 *   - Any of the 6 Guitar Tunings!
 */
typedef struct {
    int  frets[6];      /* -1 = muted ('X'), 0..15 = fretted/open */
    int  fingers[6];    /* 0 = open/muted, 1..4 = finger number */
    int  barre_fret;    /* >0 if index finger barres multiple strings */
    int  barre_low_str; /* lowest string of barre (0..5) */
    int  base_fret;     /* starting fret of the voicing box */
    int  max_fret;      /* highest fret of the voicing box */
} ComputedVoicing;

static void compute_chord_voicing(int root_pc, int chord_idx, int voicing_idx, ComputedVoicing *out) {
    const ChordQualityDef *cd = &k_chords[chord_idx];
    const GuitarTuning    *tn = &k_tunings[g_tuning_idx];

    /* Determine target fret window for this CAGED position (0..4) */
    int open_s0_pc  = tn->midi[0] % 12;
    int open_s1_pc  = tn->midi[1] % 12;
    int root_f6     = (root_pc - open_s0_pc + 12) % 12;
    int root_f5     = (root_pc - open_s1_pc + 12) % 12;

    /* 5 canonical CAGED anchors ordered by ascending fret position */
    int anchors[5];
    int bass_str[5];
    anchors[0] = root_f6;                         bass_str[0] = 0; /* E-shape lower */
    anchors[1] = (root_f6 + 2) % 12;              bass_str[1] = 2; /* D/C-shape */
    anchors[2] = root_f5;                         bass_str[2] = 1; /* A-shape */
    anchors[3] = (root_f5 + 3) % 12;              bass_str[3] = 0; /* G-shape */
    anchors[4] = root_f6 + ((root_f6 <= 3) ? 12 : 7); bass_str[4] = 0; /* Upper octave / high box */

    /* Sort the 5 anchors by fret so L1/R1 walks smoothly up the neck from nut to fret 15 */
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (anchors[j] < anchors[i]) {
                int tmp_a = anchors[i]; anchors[i] = anchors[j]; anchors[j] = tmp_a;
                int tmp_b = bass_str[i]; bass_str[i] = bass_str[j]; bass_str[j] = tmp_b;
            }
        }
    }
    /* Ensure distinctness across the 5 positions */
    for (int i = 1; i < 5; i++) {
        if (anchors[i] <= anchors[i - 1]) {
            anchors[i] = anchors[i - 1] + 2;
        }
        if (anchors[i] > 12) anchors[i] = 12;
    }

    int win_low  = anchors[voicing_idx % 5];
    int min_str  = bass_str[voicing_idx % 5];
    if (cd->pref_bass_str > min_str && win_low > 0) {
        min_str = cd->pref_bass_str;
    }
    int win_high = win_low + 3;
    if (win_low == 0) win_high = 3;
    if (win_high > MAX_FRET) win_high = MAX_FRET;

    for (int s = 0; s < 6; s++) {
        out->frets[s]   = -1;
        out->fingers[s] = 0;
    }

    /* Place the bass root on the lowest active string if possible */
    bool bass_placed = false;
    for (int s = min_str; s <= 2; s++) {
        for (int f = win_low; f <= win_high + 1 && f <= MAX_FRET; f++) {
            int pc = (tn->midi[s] + f) % 12;
            if (pc == root_pc) {
                out->frets[s] = f;
                min_str = s;
                bass_placed = true;
                break;
            }
        }
        if (bass_placed) break;
    }

    /* Fill remaining strings from min_str..5 with the closest chord tone in [win_low .. win_high] */
    for (int s = min_str; s < 6; s++) {
        if (out->frets[s] >= 0) continue;
        int best_f = -1;
        int best_score = -999;
        for (int f = win_low; f <= win_high + 1 && f <= MAX_FRET; f++) {
            int pc  = (tn->midi[s] + f) % 12;
            int rel = (pc - root_pc + 12) % 12;
            uint8_t m = cd->mask[rel];
            if (m > 0) {
                /* Prefer chord extensions (3rd/7th) on upper strings D, G, B, e */
                int score = (int)m * 10 - abs(f - (win_low + 1)) * 3;
                if (s >= 2 && (m == 2 || m == 3)) score += 12;
                if (score > best_score) {
                    best_score = score;
                    best_f     = f;
                }
            }
        }
        out->frets[s] = best_f;
    }

    /* Compute min/max fretted (>0) and finger numbers 1..4 */
    int min_f = 99, max_f = 0, min_f_count = 0, min_f_low_str = 6;
    for (int s = 0; s < 6; s++) {
        int f = out->frets[s];
        if (f > 0) {
            if (f < min_f) {
                min_f = f;
                min_f_count = 1;
                min_f_low_str = s;
            } else if (f == min_f) {
                min_f_count++;
                if (s < min_f_low_str) min_f_low_str = s;
            }
            if (f > max_f) max_f = f;
        }
    }
    if (min_f == 99) { min_f = 0; max_f = 3; }

    out->base_fret     = (min_f > 0) ? min_f : 1;
    out->max_fret      = (max_f > out->base_fret) ? max_f : (out->base_fret + 3);
    out->barre_fret    = (min_f > 0 && min_f_count >= 2) ? min_f : 0;
    out->barre_low_str = min_f_low_str;

    int next_finger = (out->barre_fret > 0) ? 2 : 1;
    for (int f = min_f; f <= max_f && f > 0; f++) {
        for (int s = 0; s < 6; s++) {
            if (out->frets[s] == f) {
                if (f == out->barre_fret) {
                    out->fingers[s] = 1;
                } else {
                    out->fingers[s] = (next_finger <= 4) ? next_finger : 4;
                    if (next_finger < 4) next_finger++;
                }
            }
        }
    }
}

/* =========================================================================
 * Reverse Chord Identifier (Mode 3: FRET EXPLORER)
 * ========================================================================= */
static void identify_locked_chord(char *out_name, size_t name_sz, char *out_notes, size_t notes_sz) {
    const GuitarTuning *tn = &k_tunings[g_tuning_idx];
    uint16_t pc_mask = 0;
    int bass_pc   = -1;
    int bass_midi = 999;
    int note_cnt  = 0;

    out_notes[0] = '\0';
    for (int s = 0; s < 6; s++) {
        int f = g_exp_locked[s];
        if (f >= 0) {
            int midi = tn->midi[s] + f;
            int pc   = midi % 12;
            pc_mask |= (uint16_t)(1u << pc);
            if (midi < bass_midi) {
                bass_midi = midi;
                bass_pc   = pc;
            }
            if (note_cnt > 0) {
                strncat(out_notes, " - ", notes_sz - strlen(out_notes) - 1);
            }
            strncat(out_notes, k_note_names[pc], notes_sz - strlen(out_notes) - 1);
            note_cnt++;
        }
    }

    if (note_cnt == 0) {
        snprintf(out_name, name_sz, "No Locked Notes (Press B to Lock)");
        snprintf(out_notes, notes_sz, "Move cursor & press A to pluck");
        return;
    }
    if (note_cnt == 1) {
        snprintf(out_name, name_sz, "%s (Single Note)", k_note_names[bass_pc]);
        return;
    }

    /* Candidate chord templates to match against pc_mask */
    static const struct {
        const char *suffix;
        uint16_t    req_mask;
        uint16_t    allow_mask;
        int         bonus;
    } k_templates[] = {
        { "",      (1<<0)|(1<<4)|(1<<7),                (1<<0)|(1<<4)|(1<<7),                30 },
        { "m",     (1<<0)|(1<<3)|(1<<7),                (1<<0)|(1<<3)|(1<<7),                30 },
        { "5",     (1<<0)|(1<<7),                       (1<<0)|(1<<7),                       24 },
        { "7",     (1<<0)|(1<<4)|(1<<10),               (1<<0)|(1<<4)|(1<<7)|(1<<10),        34 },
        { "maj7",  (1<<0)|(1<<4)|(1<<11),               (1<<0)|(1<<4)|(1<<7)|(1<<11),        34 },
        { "m7",    (1<<0)|(1<<3)|(1<<10),               (1<<0)|(1<<3)|(1<<7)|(1<<10),        34 },
        { "m(maj7)",(1<<0)|(1<<3)|(1<<11),              (1<<0)|(1<<3)|(1<<7)|(1<<11),        32 },
        { "sus2",  (1<<0)|(1<<2)|(1<<7),                (1<<0)|(1<<2)|(1<<7),                28 },
        { "sus4",  (1<<0)|(1<<5)|(1<<7),                (1<<0)|(1<<5)|(1<<7),                28 },
        { "7sus4", (1<<0)|(1<<5)|(1<<10),               (1<<0)|(1<<5)|(1<<7)|(1<<10),        32 },
        { "add9",  (1<<0)|(1<<2)|(1<<4),                (1<<0)|(1<<2)|(1<<4)|(1<<7),         33 },
        { "m(add9)",(1<<0)|(1<<2)|(1<<3),               (1<<0)|(1<<2)|(1<<3)|(1<<7),         35 },
        { "6",     (1<<0)|(1<<4)|(1<<9),                (1<<0)|(1<<4)|(1<<7)|(1<<9),         31 },
        { "m6",    (1<<0)|(1<<3)|(1<<9),                (1<<0)|(1<<3)|(1<<7)|(1<<9),         31 },
        { "m7b5",  (1<<0)|(1<<3)|(1<<6)|(1<<10),        (1<<0)|(1<<3)|(1<<6)|(1<<10),        36 },
        { "dim7",  (1<<0)|(1<<3)|(1<<6)|(1<<9),         (1<<0)|(1<<3)|(1<<6)|(1<<9),         36 },
        { "dim",   (1<<0)|(1<<3)|(1<<6),                (1<<0)|(1<<3)|(1<<6),                28 },
        { "aug",   (1<<0)|(1<<4)|(1<<8),                (1<<0)|(1<<4)|(1<<8),                28 },
        { "9",     (1<<0)|(1<<2)|(1<<4)|(1<<10),        (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<10), 36 },
        { "m9",    (1<<0)|(1<<2)|(1<<3)|(1<<10),        (1<<0)|(1<<2)|(1<<3)|(1<<7)|(1<<10), 36 },
        { "maj9",  (1<<0)|(1<<2)|(1<<4)|(1<<11),        (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<11), 36 }
    };

    int best_score = -999;
    int best_root  = bass_pc;
    const char *best_suf = "";

    for (int r = 0; r < 12; r++) {
        if (!(pc_mask & (1u << r))) continue;
        /* Rotate pc_mask so candidate root r is bit 0 */
        uint16_t rel_mask = 0;
        for (int p = 0; p < 12; p++) {
            if (pc_mask & (1u << p)) {
                rel_mask |= (uint16_t)(1u << ((p - r + 12) % 12));
            }
        }
        for (size_t t = 0; t < sizeof(k_templates) / sizeof(k_templates[0]); t++) {
            if ((rel_mask & k_templates[t].req_mask) == k_templates[t].req_mask &&
                (rel_mask & ~k_templates[t].allow_mask) == 0) {
                int score = k_templates[t].bonus + ((r == bass_pc) ? 10 : 0);
                if (score > best_score) {
                    best_score = score;
                    best_root  = r;
                    best_suf   = k_templates[t].suffix;
                }
            }
        }
    }

    if (best_score < 0) {
        /* Dyad or custom cluster fallback */
        int rel_iv = 0;
        for (int p = 1; p < 12; p++) {
            if (pc_mask & (1u << ((bass_pc + p) % 12))) {
                rel_iv = p;
                break;
            }
        }
        snprintf(out_name, name_sz, "%s + %s Cluster", k_note_names[bass_pc], k_interval_labels[rel_iv]);
        return;
    }

    if (best_root != bass_pc) {
        snprintf(out_name, name_sz, "%s%s/%s",
                 k_note_names[best_root], best_suf, k_note_names[bass_pc]);
    } else {
        snprintf(out_name, name_sz, "%s%s", k_note_names[best_root], best_suf);
    }
}

/* =========================================================================
 * Audio Audition Helpers (A Button in Each Mode)
 * ========================================================================= */
static void audition_current_mode(void) {
    clear_pluck_queue();
    SDL_PauseAudio(1);

    if (g_mode == MODE_SCALES) {
        /* Play the active scale / CAGED box ascending across the 6 strings */
        const ScaleDef *sc = &k_scales[g_scale_idx];
        const GuitarTuning *tn = &k_tunings[g_tuning_idx];
        int low_f, high_f;
        compute_caged_box_range(g_root_pc, g_caged_pos, &low_f, &high_f);
        if (g_caged_pos == 0) {
            /* When ALL NECK is selected, audition the primary 4-fret box around root */
            compute_caged_box_range(g_root_pc, 1, &low_f, &high_f);
        }

        int step_idx = 0;
        int last_midi = -1;
        for (int s = 0; s < NUM_STRINGS; s++) {
            for (int f = low_f; f <= high_f; f++) {
                int midi = tn->midi[s] + f;
                int pc   = midi % 12;
                int rel  = (pc - g_root_pc + 12) % 12;
                if (sc->mask[rel] > 0 && midi > last_midi) {
                    enqueue_pluck(s, f, (step_idx == 0) ? 0 : 145);
                    last_midi = midi;
                    step_idx++;
                }
            }
        }
    } else if (g_mode == MODE_CHORDS) {
        /* Strum the active 6-string CAGED chord voicing */
        ComputedVoicing cv;
        compute_chord_voicing(g_root_pc, g_chord_idx, g_voicing_pos, &cv);
        int strum_idx = 0;
        for (int s = 0; s < NUM_STRINGS; s++) {
            if (cv.frets[s] >= 0) {
                enqueue_pluck(s, cv.frets[s], (strum_idx == 0) ? 0 : 32);
                strum_idx++;
            }
        }
    } else {
        /* MODE_EXPLORER: Pluck cursor note, or strum locked chord if cursor is on a locked note */
        int cur_str = 5 - g_exp_row;
        if (g_exp_locked[cur_str] == g_exp_fret) {
            int strum_idx = 0;
            for (int s = 0; s < NUM_STRINGS; s++) {
                if (g_exp_locked[s] >= 0) {
                    enqueue_pluck(s, g_exp_locked[s], (strum_idx == 0) ? 0 : 32);
                    strum_idx++;
                }
            }
        } else {
            enqueue_pluck(cur_str, g_exp_fret, 0);
        }
    }

    SDL_PauseAudio(0);
}

/* =========================================================================
 * Persistent Configuration (/mnt/.fretboard.cfg or ~/.fretboard.cfg)
 * ========================================================================= */
static char g_cfg_path[512];

static void resolve_config_path(void) {
#if defined(HOST_X11)
    const char *home = getenv("HOME");
    if (home && home[0]) {
        snprintf(g_cfg_path, sizeof(g_cfg_path), "%s/.fretboard.cfg", home);
    } else {
        snprintf(g_cfg_path, sizeof(g_cfg_path), ".fretboard.cfg");
    }
#else
    if (access("/mnt", W_OK) == 0) {
        snprintf(g_cfg_path, sizeof(g_cfg_path), "/mnt/.fretboard.cfg");
    } else {
        snprintf(g_cfg_path, sizeof(g_cfg_path), ".fretboard.cfg");
    }
#endif
}

static void load_config(void) {
    FILE *fp = fopen(g_cfg_path, "r");
    if (!fp) return;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        int val = 0;
        if (sscanf(line, "mode=%d", &val) == 1 && val >= 0 && val <= 2) g_mode = (AppMode)val;
        else if (sscanf(line, "label_mode=%d", &val) == 1 && val >= 0 && val <= 2) g_label_mode = (DotLabelMode)val;
        else if (sscanf(line, "root_pc=%d", &val) == 1 && val >= 0 && val < 12) g_root_pc = val;
        else if (sscanf(line, "scale_idx=%d", &val) == 1 && val >= 0 && val < NUM_SCALES) g_scale_idx = val;
        else if (sscanf(line, "caged_pos=%d", &val) == 1 && val >= 0 && val <= 5) g_caged_pos = val;
        else if (sscanf(line, "chord_skeleton=%d", &val) == 1) g_chord_skeleton = (val != 0);
        else if (sscanf(line, "chord_idx=%d", &val) == 1 && val >= 0 && val < NUM_CHORDS) g_chord_idx = val;
        else if (sscanf(line, "voicing_pos=%d", &val) == 1 && val >= 0 && val < 5) g_voicing_pos = val;
        else if (sscanf(line, "show_arp_bg=%d", &val) == 1) g_show_arp_bg = (val != 0);
        else if (sscanf(line, "tuning_idx=%d", &val) == 1 && val >= 0 && val < NUM_TUNINGS) g_tuning_idx = val;
    }
    fclose(fp);
}

static void save_config(void) {
    FILE *fp = fopen(g_cfg_path, "w");
    if (!fp) return;
    fprintf(fp, "mode=%d\n", (int)g_mode);
    fprintf(fp, "label_mode=%d\n", (int)g_label_mode);
    fprintf(fp, "root_pc=%d\n", g_root_pc);
    fprintf(fp, "scale_idx=%d\n", g_scale_idx);
    fprintf(fp, "caged_pos=%d\n", g_caged_pos);
    fprintf(fp, "chord_skeleton=%d\n", g_chord_skeleton ? 1 : 0);
    fprintf(fp, "chord_idx=%d\n", g_chord_idx);
    fprintf(fp, "voicing_pos=%d\n", g_voicing_pos);
    fprintf(fp, "show_arp_bg=%d\n", g_show_arp_bg ? 1 : 0);
    fprintf(fp, "tuning_idx=%d\n", g_tuning_idx);
    fclose(fp);
}

/* =========================================================================
 * UI Rendering (320x240)
 * ========================================================================= */
static void render_top_status_bar(void) {
    fill_rect(0, 0, SCREEN_W, 22, RGB(19, 27, 43));
    fill_rect(0, 21, SCREEN_W, 1, RGB(45, 62, 92));

    /* 1. Mode Badge (x=4..60) */
    const char *mode_txt = (g_mode == MODE_SCALES) ? "SCALES" :
                           (g_mode == MODE_CHORDS) ? "CHORDS" : "EXPLORER";
    uint32_t mode_bg  = (g_mode == MODE_SCALES) ? RGB(37, 99, 235)  :
                        (g_mode == MODE_CHORDS) ? RGB(124, 58, 237) : RGB(13, 148, 136);
    uint32_t mode_bdr = (g_mode == MODE_SCALES) ? RGB(147, 197, 253) :
                        (g_mode == MODE_CHORDS) ? RGB(216, 180, 254) : RGB(94, 234, 212);
    draw_pill(4, 3, 56, 16, mode_bg, mode_bdr);
    draw_text_centered(FONT_UI_9, 4, 3, 56, 16, mode_txt, RGB(255, 255, 255));

    /* 2. Key Badge (x=63..107) */
    char key_lbl[16];
    snprintf(key_lbl, sizeof(key_lbl), "KEY %s", k_note_names[g_root_pc]);
    draw_pill(63, 3, 44, 16, RGB(146, 64, 14), RGB(251, 146, 60));
    draw_text_centered(FONT_UI_9, 63, 3, 44, 16, key_lbl, RGB(255, 247, 237));

    /* 3. Center Title Pill (x=110..212) */
    char center_lbl[32];
    if (g_mode == MODE_SCALES) {
        snprintf(center_lbl, sizeof(center_lbl), "%s", k_scales[g_scale_idx].short_name);
    } else if (g_mode == MODE_CHORDS) {
        snprintf(center_lbl, sizeof(center_lbl), "%s%s (%s)",
                 k_note_names[g_root_pc], k_chords[g_chord_idx].suffix, k_chords[g_chord_idx].name);
        if (text_width(FONT_UI_9, center_lbl) > 96) {
            snprintf(center_lbl, sizeof(center_lbl), "%s%s",
                     k_note_names[g_root_pc], k_chords[g_chord_idx].suffix);
        }
    } else {
        snprintf(center_lbl, sizeof(center_lbl), "Reverse Finder");
    }
    draw_pill(110, 3, 102, 16, RGB(28, 39, 62), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_9, 110, 3, 102, 16, center_lbl, RGB(254, 240, 138));

    /* 4. Position / Box Badge (x=215..272) */
    char pos_lbl[24];
    if (g_mode == MODE_SCALES) {
        snprintf(pos_lbl, sizeof(pos_lbl), "%s", k_caged_pos_names[g_caged_pos]);
    } else if (g_mode == MODE_CHORDS) {
        snprintf(pos_lbl, sizeof(pos_lbl), "VOICING %d", g_voicing_pos + 1);
    } else {
        snprintf(pos_lbl, sizeof(pos_lbl), "STR %s:F%d",
                 k_tunings[g_tuning_idx].row_lbl[g_exp_row], g_exp_fret);
    }
    draw_pill(215, 3, 57, 16, RGB(24, 34, 54), RGB(71, 98, 145));
    draw_text_centered(FONT_UI_9, 215, 3, 57, 16, pos_lbl, RGB(224, 242, 254));

    /* 5. Guitar Tuning Badge (x=275..315) */
    draw_pill(275, 3, 41, 16, RGB(16, 80, 62), RGB(52, 211, 153));
    draw_text_centered(FONT_UI_9, 275, 3, 41, 16, k_tunings[g_tuning_idx].short_name, RGB(236, 253, 245));
}

static void render_theory_info_card(const ComputedVoicing *cv) {
    fill_rect(4, 25, SCREEN_W - 8, 41, RGB(21, 30, 48));
    draw_border(4, 25, SCREEN_W - 8, 41, RGB(51, 68, 98));

    if (g_mode == MODE_SCALES) {
        const ScaleDef *sc = &k_scales[g_scale_idx];
        char hdr1[80];
        snprintf(hdr1, sizeof(hdr1), "%s %s   (%s)",
                 k_note_names[g_root_pc], sc->name, sc->formula);
        draw_text(FONT_UI_10, 10, 29, hdr1, RGB(250, 204, 21));

        /* Build scale notes list */
        char notes_buf[64] = "Notes: ";
        for (int i = 0; i < 12; i++) {
            if (sc->mask[i] > 0) {
                int pc = (g_root_pc + i) % 12;
                strncat(notes_buf, k_note_names[pc], sizeof(notes_buf) - strlen(notes_buf) - 1);
                strncat(notes_buf, " ", sizeof(notes_buf) - strlen(notes_buf) - 1);
            }
        }
        draw_text(FONT_UI_9, 10, 46, notes_buf, RGB(186, 230, 253));

        int low_f, high_f;
        compute_caged_box_range(g_root_pc, g_caged_pos, &low_f, &high_f);
        char box_buf[48];
        if (g_caged_pos == 0) {
            snprintf(box_buf, sizeof(box_buf), "Chords: %s%s", k_note_names[g_root_pc], sc->skeleton_chord);
        } else {
            snprintf(box_buf, sizeof(box_buf), "Box Frets %d-%d | %s%s",
                     low_f, high_f, k_note_names[g_root_pc], sc->skeleton_chord);
        }
        int bw = text_width(FONT_UI_9, box_buf);
        draw_text(FONT_UI_9, SCREEN_W - 10 - bw, 46, box_buf, RGB(134, 239, 172));

    } else if (g_mode == MODE_CHORDS) {
        const ChordQualityDef *cd = &k_chords[g_chord_idx];
        char ch_name[32];
        snprintf(ch_name, sizeof(ch_name), "%s%s", k_note_names[g_root_pc], cd->suffix);

        draw_pill(10, 29, 60, 33, RGB(30, 43, 68), RGB(250, 204, 21));
        draw_text_centered(FONT_UI_12, 10, 29, 60, 33, ch_name, RGB(250, 204, 21));

        char line1[96];
        snprintf(line1, sizeof(line1), "%s (%s) | Pos %d/5 (Frets %d-%d)",
                 cd->name, cd->formula, g_voicing_pos + 1, cv->base_fret, cv->max_fret);
        draw_text(FONT_UI_9, 76, 30, line1, RGB(248, 250, 252));

        /* Show chord tones and string fret tab (e.g. X-3-5-4-5-3) */
        char notes_str[48] = "Tones: ";
        for (int i = 0; i < 12; i++) {
            if (cd->mask[i] > 0) {
                int pc = (g_root_pc + i) % 12;
                strncat(notes_str, k_note_names[pc], sizeof(notes_str) - strlen(notes_str) - 1);
                strncat(notes_str, " ", sizeof(notes_str) - strlen(notes_str) - 1);
            }
        }
        char tab_str[128];
        char f0[16], f1[16], f2[16], f3[16], f4[16], f5[16];
        snprintf(f0, sizeof(f0), cv->frets[0] < 0 ? "X" : "%d", cv->frets[0]);
        snprintf(f1, sizeof(f1), cv->frets[1] < 0 ? "X" : "%d", cv->frets[1]);
        snprintf(f2, sizeof(f2), cv->frets[2] < 0 ? "X" : "%d", cv->frets[2]);
        snprintf(f3, sizeof(f3), cv->frets[3] < 0 ? "X" : "%d", cv->frets[3]);
        snprintf(f4, sizeof(f4), cv->frets[4] < 0 ? "X" : "%d", cv->frets[4]);
        snprintf(f5, sizeof(f5), cv->frets[5] < 0 ? "X" : "%d", cv->frets[5]);
        snprintf(tab_str, sizeof(tab_str), "%s  |  Frets: %s-%s-%s-%s-%s-%s",
                 notes_str, f0, f1, f2, f3, f4, f5);
        draw_text(FONT_UI_9, 76, 47, tab_str, RGB(134, 239, 172));

    } else {
        /* MODE_EXPLORER: Reverse Chord Finder + Cursor Pitch/Frequency Readout */
        char ch_id[64], ch_notes[64];
        identify_locked_chord(ch_id, sizeof(ch_id), ch_notes, sizeof(ch_notes));

        char id_line[160];
        snprintf(id_line, sizeof(id_line), "CHORD FINDER: %s   (%s)", ch_id, ch_notes);
        draw_text(FONT_UI_10, 10, 29, id_line, RGB(250, 204, 21));

        int cur_str  = 5 - g_exp_row;
        int cur_midi = k_tunings[g_tuning_idx].midi[cur_str] + g_exp_fret;
        int cur_pc   = cur_midi % 12;
        int cur_oct  = (cur_midi / 12) - 1;
        float cur_hz = midi_to_freq(cur_midi);
        int rel_iv   = (cur_pc - g_root_pc + 12) % 12;

        char cur_line[96];
        snprintf(cur_line, sizeof(cur_line),
                 "Cursor: Str %d (%s) Fret %d  ->  %s%d  (%.1f Hz, Interval %s vs %s)",
                 g_exp_row + 1, k_tunings[g_tuning_idx].row_lbl[g_exp_row], g_exp_fret,
                 k_note_names[cur_pc], cur_oct, cur_hz,
                 k_interval_labels[rel_iv], k_note_names[g_root_pc]);
        draw_text(FONT_UI_9, 10, 47, cur_line, RGB(186, 230, 253));
    }
}

static void format_dot_label(int pc, int rel_iv, int finger_num, char *out, size_t out_sz) {
    if (g_label_mode == LABEL_NOTES) {
        snprintf(out, out_sz, "%s", k_note_names[pc]);
    } else if (g_label_mode == LABEL_DEGREES) {
        if (g_mode == MODE_CHORDS && finger_num > 0 && finger_num <= 4) {
            snprintf(out, out_sz, "F%d", finger_num & 7);
        } else {
            snprintf(out, out_sz, "%s", k_degree_labels[rel_iv]);
        }
    } else {
        snprintf(out, out_sz, "%s", k_interval_labels[rel_iv]);
    }
}

static void render_hero_fretboard(const ComputedVoicing *cv) {
    /* Hero 15-Fret Guitar Neck Card: y = 69..192 (height = 123px) */
    fill_rect(4, 69, SCREEN_W - 8, 123, RGB(21, 30, 48));
    draw_border(4, 69, SCREEN_W - 8, 123, RGB(51, 68, 98));

    const GuitarTuning *tn = &k_tunings[g_tuning_idx];

    /* Neck geometry:
     *   Open string column (Fret 0): center x = 25
     *   Nut bar: x = 33..35 (3px bone nut)
     *   Frets 1..15: 15 frets * 18px = 270px (x = 35..305)
     *   6 strings: y = 92 + r * 16 (y = 92, 108, 124, 140, 156, 172) -> 80px span
     */
    int nut_x  = 34;
    int fret_w = 18;
    int neck_w = fret_w * MAX_FRET; /* 270 px */
    int str_y0 = 92;
    int str_dy = 16;
    int neck_h = str_dy * 5;        /* 80 px */

    /* Fretboard rosewood/ebony background */
    fill_rect(nut_x, str_y0 - 5, neck_w, neck_h + 10, RGB(34, 26, 22));

    /* Highlight active CAGED Box or Chord Voicing span on the fretboard wood */
    int box_low = 0, box_high = MAX_FRET;
    if (g_mode == MODE_SCALES) {
        compute_caged_box_range(g_root_pc, g_caged_pos, &box_low, &box_high);
        if (g_caged_pos > 0) {
            int bx0 = (box_low <= 1) ? nut_x : (nut_x + (box_low - 1) * fret_w);
            int bx1 = nut_x + box_high * fret_w;
            fill_rect(bx0, str_y0 - 5, bx1 - bx0, neck_h + 10, RGB(54, 43, 30));
            draw_border(bx0, str_y0 - 6, bx1 - bx0, neck_h + 12, RGB(245, 158, 11));
        }
    } else if (g_mode == MODE_CHORDS) {
        box_low  = cv->base_fret;
        box_high = cv->max_fret;
        int bx0  = (box_low <= 1) ? nut_x : (nut_x + (box_low - 1) * fret_w);
        int bx1  = nut_x + box_high * fret_w;
        fill_rect(bx0, str_y0 - 5, bx1 - bx0, neck_h + 10, RGB(46, 39, 58));
        draw_border(bx0, str_y0 - 6, bx1 - bx0, neck_h + 12, RGB(167, 139, 250));
    }

    /* Fret numbers 0..15 along top of neck + pearloid fret inlays */
    for (int f = 0; f <= MAX_FRET; f++) {
        int f_cx = (f == 0) ? (nut_x - 9) : (nut_x + f * fret_w - fret_w / 2);
        if (f > 0) {
            int wire_x = nut_x + f * fret_w;
            fill_rect(wire_x, str_y0 - 5, 1, neck_h + 10, RGB(115, 125, 142));
        }
        bool is_marker = (f == 0 || f == 3 || f == 5 || f == 7 || f == 9 || f == 12 || f == 15);
        char fn[8];
        snprintf(fn, sizeof(fn), "%d", f);
        draw_text_centered(FONT_UI_9, f_cx - 8, 72, 16, 11, fn,
                           is_marker ? RGB(250, 204, 21) : RGB(110, 124, 145));

        /* Pearloid inlay dots inside fretboard */
        if (f == 3 || f == 5 || f == 7 || f == 9 || f == 15) {
            draw_filled_circle_aa((float)f_cx, (float)(str_y0 + neck_h / 2), 2.5f,
                                  RGB(75, 68, 62), RGB(95, 88, 82));
        } else if (f == 12) {
            draw_filled_circle_aa((float)f_cx, (float)(str_y0 + str_dy * 1 + 8), 2.5f,
                                  RGB(75, 68, 62), RGB(95, 88, 82));
            draw_filled_circle_aa((float)f_cx, (float)(str_y0 + str_dy * 3 + 8), 2.5f,
                                  RGB(75, 68, 62), RGB(95, 88, 82));
        }
    }

    /* Bone Nut (x = 33..35) */
    fill_rect(nut_x - 1, str_y0 - 5, 3, neck_h + 10, RGB(241, 245, 249));

    /* 6 Horizontal Guitar Strings (Row 0 = high string, Row 5 = low string) */
    for (int r = 0; r < NUM_STRINGS; r++) {
        int sy = str_y0 + r * str_dy;
        uint32_t scol = (r >= 3) ? RGB(195, 205, 220) : RGB(145, 158, 175);
        fill_rect(nut_x, sy, neck_w, (r >= 4) ? 2 : 1, scol);
        draw_text(FONT_UI_9, 8, sy - 6, tn->row_lbl[r], RGB(186, 230, 253));
    }

    /* Draw Barre Indicator Bar in Chords Mode if voicing has a barre */
    if (g_mode == MODE_CHORDS && cv->barre_fret > 0) {
        int bf_cx = nut_x + cv->barre_fret * fret_w - fret_w / 2;
        int top_r = 0;
        int bot_r = 5 - cv->barre_low_str;
        int by0   = str_y0 + top_r * str_dy - 4;
        int by1   = str_y0 + bot_r * str_dy + 4;
        draw_pill(bf_cx - 4, by0, 8, by1 - by0, RGB(30, 64, 175), RGB(147, 197, 253));
    }

    /* Render Scale / Chord / Explorer Note Dots across 6 strings x 16 frets */
    for (int r = 0; r < NUM_STRINGS; r++) {
        int str_idx = 5 - r; /* 0 = low E .. 5 = high e */
        int sy      = str_y0 + r * str_dy;

        /* In Chords mode, show 'X' on nut if string is muted */
        if (g_mode == MODE_CHORDS && cv->frets[str_idx] < 0) {
            draw_text_centered(FONT_UI_9, nut_x - 16, sy - 6, 14, 12, "X", RGB(248, 113, 113));
        }

        for (int f = 0; f <= MAX_FRET; f++) {
            int f_cx    = (f == 0) ? (nut_x - 9) : (nut_x + f * fret_w - fret_w / 2);
            int midi    = tn->midi[str_idx] + f;
            int pc      = midi % 12;
            int rel_iv  = (pc - g_root_pc + 12) % 12;
            bool ringing = (g_active_play_ttl > 0 && g_active_play_str == str_idx && g_active_play_fret == f);

            if (g_mode == MODE_SCALES) {
                uint8_t m = k_scales[g_scale_idx].mask[rel_iv];
                if (m == 0) continue;

                bool in_box = (g_caged_pos == 0) || (f >= box_low && f <= box_high);
                if (!in_box) {
                    /* Ghost context dot outside active CAGED box */
                    uint32_t gfill = (m == 4) ? RGB(92, 64, 18) : RGB(32, 46, 68);
                    uint32_t gbdr  = (m == 4) ? RGB(161, 98, 7)  : RGB(58, 78, 110);
                    draw_filled_circle_aa((float)f_cx, (float)sy, 4.0f, gfill, gbdr);
                    continue;
                }

                uint32_t dfill, dbdr, tcol;
                if (ringing) {
                    dfill = RGB(255, 255, 255);
                    dbdr  = RGB(250, 204, 21);
                    tcol  = RGB(15, 23, 42);
                } else if (m == 4) {
                    /* Root Note: Gold */
                    dfill = RGB(245, 158, 11);
                    dbdr  = RGB(254, 240, 138);
                    tcol  = RGB(15, 23, 42);
                } else if (m == 3) {
                    /* Characteristic / Blue Note: Pink/Magenta */
                    dfill = RGB(219, 39, 119);
                    dbdr  = RGB(251, 207, 232);
                    tcol  = RGB(255, 255, 255);
                } else if (m == 2 && g_chord_skeleton) {
                    /* Chord Tone (3, 5, 7): Emerald Green */
                    dfill = RGB(5, 150, 105);
                    dbdr  = RGB(167, 243, 208);
                    tcol  = RGB(255, 255, 255);
                } else {
                    /* Scale Tone: Sky Cyan */
                    dfill = RGB(2, 132, 199);
                    dbdr  = RGB(186, 230, 253);
                    tcol  = RGB(255, 255, 255);
                }

                draw_filled_circle_aa((float)f_cx, (float)sy, 6.4f, dfill, dbdr);
                char dlbl[8];
                format_dot_label(pc, rel_iv, 0, dlbl, sizeof(dlbl));
                draw_text_centered(FONT_DOT_8, f_cx - 7, sy - 6, 14, 12, dlbl, tcol);

            } else if (g_mode == MODE_CHORDS) {
                uint8_t m = k_chords[g_chord_idx].mask[rel_iv];
                bool is_voicing_note = (cv->frets[str_idx] == f);

                if (!is_voicing_note) {
                    if (!g_show_arp_bg || m == 0) continue;
                    /* Secondary full-neck arpeggio dot */
                    uint32_t afill = (m == 4) ? RGB(110, 74, 16) : RGB(28, 64, 68);
                    uint32_t abdr  = (m == 4) ? RGB(217, 119, 6) : RGB(45, 122, 118);
                    draw_filled_circle_aa((float)f_cx, (float)sy, 4.2f, afill, abdr);
                    continue;
                }

                uint32_t dfill, dbdr, tcol;
                if (ringing) {
                    dfill = RGB(255, 255, 255);
                    dbdr  = RGB(250, 204, 21);
                    tcol  = RGB(15, 23, 42);
                } else if (m == 4) {
                    dfill = RGB(245, 158, 11);
                    dbdr  = RGB(254, 240, 138);
                    tcol  = RGB(15, 23, 42);
                } else if (m == 3) {
                    dfill = RGB(219, 39, 119);
                    dbdr  = RGB(251, 207, 232);
                    tcol  = RGB(255, 255, 255);
                } else {
                    dfill = RGB(5, 150, 105);
                    dbdr  = RGB(167, 243, 208);
                    tcol  = RGB(255, 255, 255);
                }

                draw_filled_circle_aa((float)f_cx, (float)sy, 6.6f, dfill, dbdr);
                char dlbl[8];
                format_dot_label(pc, rel_iv, cv->fingers[str_idx], dlbl, sizeof(dlbl));
                draw_text_centered(FONT_DOT_8, f_cx - 7, sy - 6, 14, 12, dlbl, tcol);

            } else {
                /* MODE_EXPLORER */
                bool is_locked = (g_exp_locked[str_idx] == f);
                bool is_cursor = (g_exp_row == r && g_exp_fret == f);

                if (!is_locked && !is_cursor) {
                    /* Subtle root reference markers in background */
                    if (pc == g_root_pc) {
                        draw_filled_circle_aa((float)f_cx, (float)sy, 2.8f,
                                              RGB(68, 52, 22), RGB(146, 92, 18));
                    }
                    continue;
                }

                uint32_t dfill = is_locked ? ((pc == g_root_pc) ? RGB(245, 158, 11) : RGB(5, 150, 105))
                                           : RGB(37, 99, 235);
                uint32_t dbdr  = is_cursor ? RGB(255, 255, 255) : RGB(167, 243, 208);
                uint32_t tcol  = (is_locked && pc == g_root_pc) ? RGB(15, 23, 42) : RGB(255, 255, 255);

                if (ringing) {
                    dfill = RGB(255, 255, 255);
                    tcol  = RGB(15, 23, 42);
                }

                if (is_cursor) {
                    draw_border(f_cx - 8, sy - 8, 17, 17, RGB(250, 204, 21));
                }
                draw_filled_circle_aa((float)f_cx, (float)sy, 6.6f, dfill, dbdr);
                char dlbl[8];
                format_dot_label(pc, rel_iv, 0, dlbl, sizeof(dlbl));
                draw_text_centered(FONT_DOT_8, f_cx - 7, sy - 6, 14, 12, dlbl, tcol);
            }
        }
    }
}

static void render_legend_and_footer(void) {
    /* Legend & CAGED Box Strip: y = 194..216 (22px) */
    fill_rect(4, 194, SCREEN_W - 8, 22, RGB(21, 30, 48));
    draw_border(4, 194, SCREEN_W - 8, 22, RGB(51, 68, 98));

    /* Color-coded dot legend on left */
    draw_filled_circle_aa(14.0f, 205.0f, 4.5f, RGB(245, 158, 11), RGB(254, 240, 138));
    draw_text(FONT_UI_9, 22, 199, "Root", RGB(254, 240, 138));

    draw_filled_circle_aa(56.0f, 205.0f, 4.5f, RGB(5, 150, 105), RGB(167, 243, 208));
    draw_text(FONT_UI_9, 64, 199, "Chord(3/5/7)", RGB(167, 243, 208));

    draw_filled_circle_aa(138.0f, 205.0f, 4.5f, RGB(219, 39, 119), RGB(251, 207, 232));
    draw_text(FONT_UI_9, 146, 199, "Color/Blue", RGB(251, 207, 232));

    draw_filled_circle_aa(208.0f, 205.0f, 4.5f, RGB(2, 132, 199), RGB(186, 230, 253));
    draw_text(FONT_UI_9, 216, 199, "Scale", RGB(186, 230, 253));

    /* Right pill in legend bar: L1/R1 Position hint */
    draw_pill(252, 197, 60, 16, RGB(30, 43, 68), RGB(96, 165, 250));
    const char *lr_txt = (g_mode == MODE_SCALES) ? "L1/R1:Box" :
                         (g_mode == MODE_CHORDS) ? "L1/R1:Pos" : "B:Lock Note";
    draw_text_centered(FONT_UI_9, 252, 197, 60, 16, lr_txt, RGB(224, 242, 254));

    /* Bottom Control Bar: y = 219..239 (21px) */
    fill_rect(0, 219, SCREEN_W, 21, RGB(15, 22, 35));
    fill_rect(0, 219, SCREEN_W, 1, RGB(45, 62, 92));

    const char *lbl_txt = (g_label_mode == LABEL_INTERVALS) ? "X:Intv(R,b3)" :
                          (g_label_mode == LABEL_NOTES)     ? "X:Notes(A,C)" :
                                                              "X:Deg/Fing";
    draw_pill(4, 222, 74, 15, RGB(23, 52, 74), RGB(56, 189, 248));
    draw_text_centered(FONT_UI_9, 4, 222, 74, 15, lbl_txt, RGB(224, 242, 254));

    if (g_mode == MODE_SCALES) {
        draw_text(FONT_UI_9, 83, 224, "A:Play B:Skel L/R:Scale U/D:Key", RGB(203, 213, 225));
    } else if (g_mode == MODE_CHORDS) {
        draw_text(FONT_UI_9, 83, 224, "A:Strum B:Arp L/R:Chord U/D:Key", RGB(203, 213, 225));
    } else {
        draw_text(FONT_UI_9, 83, 224, "A:Pluck B:Lock DPad:Move SEL:Mode", RGB(203, 213, 225));
    }

    /* Discrete Y: Help Badge in bottom-right corner */
    draw_pill(SCREEN_W - 48, 222, 44, 15, RGB(30, 58, 95), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_9, SCREEN_W - 48, 222, 44, 15, "Y: Help", RGB(254, 240, 138));
}

static void render_help_modal(void) {
    int mx = 6, my = 8, mw = SCREEN_W - 12, mh = SCREEN_H - 16;
    fill_rect(mx, my, mw, mh, RGB(15, 23, 38));
    draw_border(mx, my, mw, mh, RGB(96, 165, 250));
    draw_border(mx + 1, my + 1, mw - 2, mh - 2, RGB(37, 99, 235));

    fill_rect(mx + 2, my + 2, mw - 4, 21, RGB(28, 39, 60));
    fill_rect(mx + 2, my + 23, mw - 4, 1, RGB(64, 82, 115));
    draw_pill(mx + 6, my + 4, 44, 15, RGB(37, 99, 235), RGB(147, 197, 253));
    draw_text_centered(FONT_UI_9, mx + 6, my + 4, 44, 15, "HELP", RGB(255, 255, 255));
    draw_text(FONT_UI_10, mx + 56, my + 6, "FRETBOARD CONTROLS & GUITAR TUNING", RGB(248, 250, 252));

    /* Guitar Tuning Selector Box (Interactive inside Help modal via LEFT/RIGHT) */
    int gy = my + 27;
    fill_rect(mx + 6, gy, mw - 12, 36, RGB(23, 33, 52));
    draw_border(mx + 6, gy, mw - 12, 36, RGB(52, 211, 153));

    char t1[80];
    snprintf(t1, sizeof(t1), "[LEFT / RIGHT] Guitar Tuning (%d/%d): %s",
             g_tuning_idx + 1, NUM_TUNINGS, k_tunings[g_tuning_idx].name);
    draw_text(FONT_UI_9, mx + 12, gy + 5, t1, RGB(250, 204, 21));

    draw_text(FONT_UI_9, mx + 12, gy + 20,
              "Tunings: Std E | Drop D | Eb Std | D Std | DADGAD | Open G",
              RGB(167, 243, 208));

    /* Controls Reference Table */
    int ky = gy + 41;
    static const struct {
        const char *btn;
        const char *desc;
    } k_rows[7] = {
        { "SELECT",       "Mode: Scales & CAGED -> Chords -> Explorer" },
        { "A Button",     "Audition Scale / Strum Chord / Pluck Note" },
        { "B Button",     "Scales: Chord Tones | Chords: Arp | Exp: Lock" },
        { "X Button",     "Cycle Labels: Intervals -> Notes -> Degrees" },
        { "L1 / R1",      "Cycle 5 CAGED Boxes or 5 Chord Voicings" },
        { "DPAD U/D/L/R", "U/D: Root Key (C..B) | L/R: 14 Scales/12 Chords" },
        { "MENU",         "Save settings & Exit to PowKiddy V90 menu" }
    };

    for (int i = 0; i < 7; i++) {
        int ry = ky + i * 19;
        draw_pill(mx + 6, ry, 70, 15, RGB(30, 58, 95), RGB(71, 98, 145));
        draw_text_centered(FONT_UI_9, mx + 6, ry, 70, 15, k_rows[i].btn, RGB(254, 240, 138));
        draw_text(FONT_UI_9, mx + 82, ry + 3, k_rows[i].desc, RGB(226, 232, 240));
    }

    fill_rect(mx + 2, my + mh - 17, mw - 4, 15, RGB(12, 18, 30));
    draw_text_centered(FONT_UI_9, mx, my + mh - 16, mw, 14,
                       "Press Y to close Help and return to Fretboard",
                       RGB(186, 230, 253));
}

static void render_ui(SDL_Surface *screen) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(13, 18, 28));

    ComputedVoicing cv;
    compute_chord_voicing(g_root_pc, g_chord_idx, g_voicing_pos, &cv);

    render_top_status_bar();
    render_theory_info_card(&cv);
    render_hero_fretboard(&cv);
    render_legend_and_footer();

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
    resolve_config_path();
    load_config();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        return 1;
    }

    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 16, SDL_SWSURFACE);
    if (!screen) {
        SDL_Quit();
        return 1;
    }
    SDL_ShowCursor(0);

    /* Automated Screenshot State Generator (--screenshot-states <dir>) */
    if (argc >= 3 && strcmp(argv[1], "--screenshot-states") == 0) {
        const char *dir = argv[2];
        char path[512];

        /* 1. Scales & CAGED View: Key of A, Blues Scale, POS 1 (E-Shape) at Frets 4-8 */
        g_mode           = MODE_SCALES;
        g_root_pc        = 9; /* A */
        g_scale_idx      = 1; /* Blues Scale */
        g_caged_pos      = 1; /* POS 1 (E-Shape) */
        g_chord_skeleton = true;
        g_label_mode     = LABEL_INTERVALS;
        g_tuning_idx     = 0;
        g_show_help      = false;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/fretboard_scales.bmp", dir);
        save_bmp(path);

        /* 2. Chords & Arpeggios View: Key of C, Cmaj7, Voicing 2 (A-Shape Barre) + Full-Neck Arp */
        g_mode        = MODE_CHORDS;
        g_root_pc     = 0; /* C */
        g_chord_idx   = 3; /* maj7 */
        g_voicing_pos = 1; /* Voicing 2 (A-Shape Barre at Fret 3) */
        g_show_arp_bg = true;
        g_label_mode  = LABEL_NOTES;
        g_show_help   = false;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/fretboard_chords.bmp", dir);
        save_bmp(path);

        /* 3. Fret Explorer / Reverse Chord Finder: Em(add9) (0-2-4-0-0-0) */
        g_mode          = MODE_EXPLORER;
        g_root_pc       = 4; /* E */
        g_exp_locked[0] = 0; /* Low E (0) */
        g_exp_locked[1] = 2; /* A string fret 2 (B) */
        g_exp_locked[2] = 4; /* D string fret 4 (F#) */
        g_exp_locked[3] = 0; /* G string open (G) */
        g_exp_locked[4] = 0; /* B string open (B) */
        g_exp_locked[5] = 0; /* high e open (E) */
        g_exp_row       = 3; /* D string */
        g_exp_fret      = 4; /* Fret 4 (F#) */
        g_label_mode    = LABEL_NOTES;
        g_show_help     = false;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/fretboard_explorer.bmp", dir);
        save_bmp(path);

        /* 4. Help & Guitar Tuning Modal (Y Button) */
        g_show_help = true;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/fretboard_help.bmp", dir);
        save_bmp(path);

        SDL_Quit();
        return 0;
    }

    SDL_AudioSpec desired, obtained;
    memset(&desired, 0, sizeof(desired));
    desired.freq     = SAMPLE_RATE;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples  = 512;
    desired.callback = audio_callback;

    bool audio_ok = (SDL_OpenAudio(&desired, &obtained) == 0);
    if (audio_ok) {
        SDL_PauseAudio(0);
    }

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                SDLKey k = ev.key.keysym.sym;

                /* MENU (or Q on desktop): Save & Exit */
                if (k == V90_BTN_MENU || k == SDLK_q) {
                    running = false;
                    break;
                }

                /* Y Button (V90_BTN_Y = SDLK_SPACE on V90, or Y / H on desktop): Toggle Help & Tuning Modal */
                if (k == V90_BTN_Y || k == SDLK_y || k == SDLK_h) {
                    g_show_help = !g_show_help;
                    continue;
                }

                /* Inside Help Modal: LEFT/RIGHT changes Guitar Tuning; A/B/SELECT/Y closes */
                if (g_show_help) {
                    if (k == V90_BTN_LEFT) {
                        g_tuning_idx = (g_tuning_idx + NUM_TUNINGS - 1) % NUM_TUNINGS;
                    } else if (k == V90_BTN_RIGHT) {
                        g_tuning_idx = (g_tuning_idx + 1) % NUM_TUNINGS;
                    } else if (k == V90_BTN_A || k == V90_BTN_B || k == V90_BTN_SELECT ||
                               k == SDLK_a || k == SDLK_b || k == SDLK_z) {
                        g_show_help = false;
                    }
                    continue;
                }

                /* SELECT (V90_BTN_SELECT = SDLK_ESCAPE on V90, or C on desktop):
                 * Cycle Mode (SCALES -> CHORDS -> EXPLORER)
                 */
                if (k == V90_BTN_SELECT || k == SDLK_c) {
                    g_mode = (AppMode)(((int)g_mode + 1) % 3);
                    audition_current_mode();
                    continue;
                }

                /* X Button (V90_BTN_X = SDLK_LSHIFT on V90, or X on desktop):
                 * Cycle Dot Labels (Intervals -> Notes -> Degrees/Fingers)
                 */
                if (k == V90_BTN_X || k == SDLK_x) {
                    g_label_mode = (DotLabelMode)(((int)g_label_mode + 1) % 3);
                    continue;
                }

                /* A Button (V90_BTN_A = SDLK_LALT on V90, or A / Z on desktop):
                 * Audition Scale / Strum Chord Voicing / Pluck Explorer Note
                 */
                if (k == V90_BTN_A || k == SDLK_a || k == SDLK_z) {
                    audition_current_mode();
                    continue;
                }

                /* B Button (V90_BTN_B = SDLK_LCTRL on V90, or B on desktop):
                 * Scales: Toggle Chord-Tone Skeleton | Chords: Toggle Arp Backdrop | Explorer: Lock/Unlock Fret
                 */
                if (k == V90_BTN_B || k == SDLK_b) {
                    if (g_mode == MODE_SCALES) {
                        g_chord_skeleton = !g_chord_skeleton;
                    } else if (g_mode == MODE_CHORDS) {
                        g_show_arp_bg = !g_show_arp_bg;
                    } else {
                        int str_idx = 5 - g_exp_row;
                        if (g_exp_locked[str_idx] == g_exp_fret) {
                            g_exp_locked[str_idx] = -1;
                        } else {
                            g_exp_locked[str_idx] = g_exp_fret;
                            clear_pluck_queue();
                            SDL_PauseAudio(1);
                            enqueue_pluck(str_idx, g_exp_fret, 0);
                            SDL_PauseAudio(0);
                        }
                    }
                    continue;
                }

                /* Shoulder L1 / R1 (V90_BTN_L1 = SDLK_TAB, V90_BTN_R1 = SDLK_BACKSPACE on V90;
                 * or [ / ] / - / = / PageUp / PageDown on desktop):
                 * Cycle CAGED Box (ALL NECK, POS 1..5) / Chord Voicing (VOICING 1..5) / Explorer Fret Jump
                 */
                if (k == V90_BTN_L1 || k == SDLK_PAGEUP || k == SDLK_LEFTBRACKET || k == SDLK_MINUS) {
                    if (g_mode == MODE_SCALES) {
                        g_caged_pos = (g_caged_pos + 6 - 1) % 6;
                    } else if (g_mode == MODE_CHORDS) {
                        g_voicing_pos = (g_voicing_pos + 5 - 1) % 5;
                        audition_current_mode();
                    } else {
                        g_exp_fret -= 3;
                        if (g_exp_fret < 0) g_exp_fret = 0;
                    }
                    continue;
                }
                if (k == V90_BTN_R1 || k == SDLK_PAGEDOWN || k == SDLK_RIGHTBRACKET || k == SDLK_EQUALS) {
                    if (g_mode == MODE_SCALES) {
                        g_caged_pos = (g_caged_pos + 1) % 6;
                    } else if (g_mode == MODE_CHORDS) {
                        g_voicing_pos = (g_voicing_pos + 1) % 5;
                        audition_current_mode();
                    } else {
                        g_exp_fret += 3;
                        if (g_exp_fret > MAX_FRET) g_exp_fret = MAX_FRET;
                    }
                    continue;
                }

                /* DPAD Navigation */
                if (g_mode == MODE_SCALES) {
                    if (k == V90_BTN_UP) {
                        g_root_pc = (g_root_pc + 1) % 12;
                    } else if (k == V90_BTN_DOWN) {
                        g_root_pc = (g_root_pc + 11) % 12;
                    } else if (k == V90_BTN_LEFT) {
                        g_scale_idx = (g_scale_idx + NUM_SCALES - 1) % NUM_SCALES;
                    } else if (k == V90_BTN_RIGHT) {
                        g_scale_idx = (g_scale_idx + 1) % NUM_SCALES;
                    }
                } else if (g_mode == MODE_CHORDS) {
                    if (k == V90_BTN_UP) {
                        g_root_pc = (g_root_pc + 1) % 12;
                        audition_current_mode();
                    } else if (k == V90_BTN_DOWN) {
                        g_root_pc = (g_root_pc + 11) % 12;
                        audition_current_mode();
                    } else if (k == V90_BTN_LEFT) {
                        g_chord_idx = (g_chord_idx + NUM_CHORDS - 1) % NUM_CHORDS;
                        audition_current_mode();
                    } else if (k == V90_BTN_RIGHT) {
                        g_chord_idx = (g_chord_idx + 1) % NUM_CHORDS;
                        audition_current_mode();
                    }
                } else {
                    if (k == V90_BTN_UP) {
                        g_exp_row = (g_exp_row + NUM_STRINGS - 1) % NUM_STRINGS;
                    } else if (k == V90_BTN_DOWN) {
                        g_exp_row = (g_exp_row + 1) % NUM_STRINGS;
                    } else if (k == V90_BTN_LEFT) {
                        g_exp_fret = (g_exp_fret + NUM_FRETS - 1) % NUM_FRETS;
                    } else if (k == V90_BTN_RIGHT) {
                        g_exp_fret = (g_exp_fret + 1) % NUM_FRETS;
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
