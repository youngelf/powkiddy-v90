/*
 * chordwheel - Interactive Circle of Fifths, Guitar Voicing Visualizer &
 *              Karplus-Strong Plucked-String Synthesizer for PowKiddy V90 & Linux Host
 *
 * Screen resolution: 320x240 16-bit RGB565 (scaled 2x to 640x480 on Linux X11 desktop)
 *
 * Controls (PowKiddy V90 / Linux Desktop):
 *   DPAD LEFT / RIGHT           : Rotate counter-clockwise (4ths) / clockwise (5ths)
 *   DPAD UP / DOWN              : Switch Ring: Outer (Major) <-> Inner (Minor) <-> Center (vii° Dim)
 *   A (LAlt / A / Space)        : Strum selected guitar chord (6-string Karplus-Strong synth)
 *   B (LCtrl / B)               : Toggle Triads (C, Am, G) <-> 7th Chords (Cmaj7, Am7, G7)
 *   L1 / R1 (Tab / BackSpace)   : Cycle Guitar Voicing (1: Open/CAGED, 2: E-Barre, 3: A-Barre)
 *   SELECT (Escape / Home / C)  : Toggle Key-Rotate Mode vs Chord-Explore (Lock Key) Mode
 *   X (LShift / X)              : Add current chord to 4-bar Progression (or Play/Stop Loop in Help)
 *   Y (Space / Y / H)           : Open / Close Help, Progression Looper & Presets Modal
 *   MENU (RCtrl / Q)            : Save state & Exit
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "chordwheel_font.h"

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
#define SDLK_BRACKETLEFT  XK_bracketleft
#define SDLK_BRACKETRIGHT XK_bracketright
#define SDLK_a          XK_a
#define SDLK_b          XK_b
#define SDLK_c          XK_c
#define SDLK_h          XK_h
#define SDLK_p          XK_p
#define SDLK_q          XK_q
#define SDLK_x          XK_x
#define SDLK_y          XK_y

#define SDL_QUIT        1
#define SDL_KEYDOWN     2
#define SDL_KEYUP       3

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
            pa = fn_new(NULL, "V90 ChordWheel", PA_STREAM_PLAYBACK, NULL, "Guitar Synth", &ss, NULL, NULL, &err);
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
#define V90_BTN_A       SDLK_LALT       /* A (East)  : Strum guitar chord */
#define V90_BTN_B       SDLK_LCTRL      /* B (South) : Toggle Triads / 7th chords */
#define V90_BTN_X       SDLK_LSHIFT     /* X (North) : Add chord to progression */
#define V90_BTN_Y       SDLK_SPACE      /* Y (West)  : Open/close Help & Presets modal */
#define V90_BTN_L1      SDLK_TAB        /* L1        : Prev guitar voicing */
#define V90_BTN_R1      SDLK_BACKSPACE  /* R1        : Next guitar voicing */
#define V90_BTN_SELECT  SDLK_ESCAPE     /* SELECT    : Lock/Unlock Key (Explore vs Rotate) */
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
    const CwFont *f = &g_cw_fonts[font_id];
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
    const CwFont *f = &g_cw_fonts[font_id];
    int cur_x = x;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 32 || c > 126) c = '?';
        const CwGlyph *g = &f->glyphs[c - 32];
        const uint8_t *bmp = &g_cw_font_bitmaps[g->offset];
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
    const CwFont *f = &g_cw_fonts[font_id];
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
 * Circle of Fifths & Guitar Music Theory Data
 * ========================================================================= */
typedef enum {
    QUAL_MAJ   = 0,
    QUAL_MIN   = 1,
    QUAL_DIM   = 2,
    QUAL_MAJ7  = 3,
    QUAL_MIN7  = 4,
    QUAL_DOM7  = 5,
    QUAL_M7B5  = 6
} ChordQuality;

typedef struct {
    const char *maj_name;       /* Outer ring label: C, G, D, A, E, B, F#, Db, Ab, Eb, Bb, F */
    const char *min_name;       /* Inner ring label: Am, Em, Bm, F#m, C#m, G#m, D#m, Bbm, Fm, Cm, Gm, Dm */
    const char *dim_root_name;  /* Leading-tone diminished root in this key: B, F#, C#, G#, D#, A#, F, C, G, D, A, E */
    const char *key_sig;        /* Key signature string: "0 #/b", "1 #", "2 #", ..., "1 b" */
    int         maj_pc;         /* Pitch class 0..11 of Major root (C=0, C#=1, D=2, ..., B=11) */
    int         min_pc;         /* Pitch class 0..11 of Relative Minor root */
    int         dim_pc;         /* Pitch class 0..11 of Leading-Tone Diminished root */
    bool        use_flats;      /* Spell notes with flats in flat keys */
} WheelSector;

static const WheelSector k_wheel[12] = {
    { "C",  "Am",  "B",  "0 #/b",  0,  9, 11, false }, /* 0  (12:00) */
    { "G",  "Em",  "F#", "1 #",    7,  4,  6, false }, /* 1  ( 1:00) */
    { "D",  "Bm",  "C#", "2 #",    2, 11,  1, false }, /* 2  ( 2:00) */
    { "A",  "F#m", "G#", "3 #",    9,  6,  8, false }, /* 3  ( 3:00) */
    { "E",  "C#m", "D#", "4 #",    4,  1,  3, false }, /* 4  ( 4:00) */
    { "B",  "G#m", "A#", "5 #",   11,  8, 10, false }, /* 5  ( 5:00) */
    { "F#", "D#m", "F",  "6 #",    6,  3,  5, false }, /* 6  ( 6:00) */
    { "Db", "Bbm", "C",  "5 b",    1, 10,  0, true  }, /* 7  ( 7:00) */
    { "Ab", "Fm",  "G",  "4 b",    8,  5,  7, true  }, /* 8  ( 8:00) */
    { "Eb", "Cm",  "D",  "3 b",    3,  0,  2, true  }, /* 9  ( 9:00) */
    { "Bb", "Gm",  "A",  "2 b",   10,  7,  9, true  }, /* 10 (10:00) */
    { "F",  "Dm",  "E",  "1 b",    5,  2,  4, true  }  /* 11 (11:00) */
};

static const char *k_note_sharp[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static const char *k_note_flat[12] = {
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
};

/* Standard guitar open string MIDI pitches: E2(40), A2(45), D3(50), G3(55), B3(59), E4(64) */
static const int k_string_midi[6] = { 40, 45, 50, 55, 59, 64 };
static const char *k_string_names[6] = { "E", "A", "D", "G", "B", "e" };

typedef struct {
    int8_t frets[6];        /* -1 = muted (X), 0 = open (O), 1..15 = fret number */
    const char *label;      /* "Open / CAGED", "E-Shape Barre", "A-Shape Barre" */
} GuitarVoicing;

/* Curated Open / CAGED Voicings for each pitch class (0=C .. 11=B) and quality */
static const int8_t k_open_maj[12][6] = {
    { -1,  3,  2,  0,  1,  0 }, /* C  */
    { -1,  4,  3,  1,  2,  1 }, /* Db (C-shape) */
    { -1, -1,  0,  2,  3,  2 }, /* D  */
    { -1,  6,  5,  3,  4,  3 }, /* Eb (C-shape) */
    {  0,  2,  2,  1,  0,  0 }, /* E  */
    {  1,  3,  3,  2,  1,  1 }, /* F  */
    {  2,  4,  4,  3,  2,  2 }, /* F# */
    {  3,  2,  0,  0,  0,  3 }, /* G  */
    {  4,  6,  6,  5,  4,  4 }, /* Ab */
    { -1,  0,  2,  2,  2,  0 }, /* A  */
    { -1,  1,  3,  3,  3,  1 }, /* Bb */
    { -1,  2,  4,  4,  4,  2 }  /* B  */
};

static const int8_t k_open_min[12][6] = {
    { -1,  3,  5,  5,  4,  3 }, /* Cm  */
    { -1,  4,  6,  6,  5,  4 }, /* C#m */
    { -1, -1,  0,  2,  3,  1 }, /* Dm  */
    { -1,  6,  8,  8,  7,  6 }, /* D#m */
    {  0,  2,  2,  0,  0,  0 }, /* Em  */
    {  1,  3,  3,  1,  1,  1 }, /* Fm  */
    {  2,  4,  4,  2,  2,  2 }, /* F#m */
    {  3,  5,  5,  3,  3,  3 }, /* Gm  */
    {  4,  6,  6,  4,  4,  4 }, /* G#m */
    { -1,  0,  2,  2,  1,  0 }, /* Am  */
    { -1,  1,  3,  3,  2,  1 }, /* Bbm */
    { -1,  2,  4,  4,  3,  2 }  /* Bm  */
};

static const int8_t k_open_maj7[12][6] = {
    { -1,  3,  2,  0,  0,  0 }, /* Cmaj7  */
    { -1,  4,  3,  1,  1,  1 }, /* Dbmaj7 */
    { -1, -1,  0,  2,  2,  2 }, /* Dmaj7  */
    { -1,  6,  5,  3,  3,  3 }, /* Ebmaj7 */
    {  0,  2,  1,  1,  0,  0 }, /* Emaj7  */
    { -1, -1,  3,  2,  1,  0 }, /* Fmaj7  */
    {  2, -1,  3,  3,  2, -1 }, /* F#maj7 */
    {  3,  2,  0,  0,  0,  2 }, /* Gmaj7  */
    {  4, -1,  5,  5,  4, -1 }, /* Abmaj7 */
    { -1,  0,  2,  1,  2,  0 }, /* Amaj7  */
    { -1,  1,  3,  2,  3,  1 }, /* Bbmaj7 */
    { -1,  2,  4,  3,  4,  2 }  /* Bmaj7  */
};

static const int8_t k_open_min7[12][6] = {
    { -1,  3,  5,  3,  4,  3 }, /* Cm7  */
    { -1,  4,  6,  4,  5,  4 }, /* C#m7 */
    { -1, -1,  0,  2,  1,  1 }, /* Dm7  */
    { -1,  6,  8,  6,  7,  6 }, /* D#m7 */
    {  0,  2,  0,  0,  0,  0 }, /* Em7  */
    {  1,  3,  1,  1,  1,  1 }, /* Fm7  */
    {  2,  4,  2,  2,  2,  2 }, /* F#m7 */
    {  3,  5,  3,  3,  3,  3 }, /* Gm7  */
    {  4,  6,  4,  4,  4,  4 }, /* G#m7 */
    { -1,  0,  2,  0,  1,  0 }, /* Am7  */
    { -1,  1,  3,  1,  2,  1 }, /* Bbm7 */
    { -1,  2,  0,  2,  0,  2 }  /* Bm7  */
};

static const int8_t k_open_dom7[12][6] = {
    { -1,  3,  2,  3,  1,  0 }, /* C7  */
    { -1,  4,  3,  4,  2, -1 }, /* Db7 */
    { -1, -1,  0,  2,  1,  2 }, /* D7  */
    { -1,  6,  5,  6,  4, -1 }, /* Eb7 */
    {  0,  2,  0,  1,  0,  0 }, /* E7  */
    {  1,  3,  1,  2,  1,  1 }, /* F7  */
    {  2,  4,  2,  3,  2,  2 }, /* F#7 */
    {  3,  2,  0,  0,  0,  1 }, /* G7  */
    {  4,  6,  4,  5,  4,  4 }, /* Ab7 */
    { -1,  0,  2,  0,  2,  0 }, /* A7  */
    { -1,  1,  3,  1,  3,  1 }, /* Bb7 */
    { -1,  2,  1,  2,  0,  2 }  /* B7  */
};

static void build_guitar_voicing(int root_pc, ChordQuality qual, int voicing_idx, GuitarVoicing *out) {
    root_pc = ((root_pc % 12) + 12) % 12;
    voicing_idx = ((voicing_idx % 3) + 3) % 3;

    /* Fret of root on 6th string (E=4 -> fret 0..11) and 5th string (A=9 -> fret 0..11) */
    int e_fret = (root_pc - 4 + 12) % 12;
    if (e_fret == 0) e_fret = 12;
    int a_fret = (root_pc - 9 + 12) % 12;
    if (a_fret == 0) a_fret = 12;

    if (voicing_idx == 0) {
        out->label = "1/3 Open/CAGED";
        if (qual == QUAL_MAJ) {
            memcpy(out->frets, k_open_maj[root_pc], 6);
            return;
        } else if (qual == QUAL_MIN) {
            memcpy(out->frets, k_open_min[root_pc], 6);
            return;
        } else if (qual == QUAL_MAJ7) {
            memcpy(out->frets, k_open_maj7[root_pc], 6);
            return;
        } else if (qual == QUAL_MIN7) {
            memcpy(out->frets, k_open_min7[root_pc], 6);
            return;
        } else if (qual == QUAL_DOM7) {
            memcpy(out->frets, k_open_dom7[root_pc], 6);
            return;
        } else {
            /* Diminished / m7b5 compact 4-string voicing */
            int d_fret = (root_pc - 2 + 12) % 12;
            out->frets[0] = -1;
            out->frets[1] = -1;
            out->frets[2] = (int8_t)d_fret;
            out->frets[3] = (int8_t)(d_fret + 1);
            out->frets[4] = (int8_t)d_fret;
            out->frets[5] = (qual == QUAL_M7B5) ? (int8_t)(d_fret + 1) : -1;
            return;
        }
    } else if (voicing_idx == 1) {
        /* 6th-String Root (E-Shape Barre) */
        out->label = "2/3 E-Barre (6str)";
        int f = e_fret;
        switch (qual) {
            case QUAL_MAJ:
                out->frets[0] = f; out->frets[1] = f + 2; out->frets[2] = f + 2;
                out->frets[3] = f + 1; out->frets[4] = f; out->frets[5] = f;
                break;
            case QUAL_MIN:
                out->frets[0] = f; out->frets[1] = f + 2; out->frets[2] = f + 2;
                out->frets[3] = f; out->frets[4] = f; out->frets[5] = f;
                break;
            case QUAL_MAJ7:
                out->frets[0] = f; out->frets[1] = -1; out->frets[2] = f + 1;
                out->frets[3] = f + 1; out->frets[4] = f; out->frets[5] = -1;
                break;
            case QUAL_MIN7:
                out->frets[0] = f; out->frets[1] = f + 2; out->frets[2] = f;
                out->frets[3] = f; out->frets[4] = f; out->frets[5] = f;
                break;
            case QUAL_DOM7:
                out->frets[0] = f; out->frets[1] = f + 2; out->frets[2] = f;
                out->frets[3] = f + 1; out->frets[4] = f; out->frets[5] = f;
                break;
            case QUAL_DIM:
            case QUAL_M7B5:
                out->frets[0] = e_fret; out->frets[1] = -1; out->frets[2] = e_fret;
                out->frets[3] = e_fret; out->frets[4] = e_fret - 1; out->frets[5] = -1;
                break;
        }
    } else {
        /* 5th-String Root (A-Shape Barre) */
        out->label = "3/3 A-Barre (5str)";
        int f = a_fret;
        switch (qual) {
            case QUAL_MAJ:
                out->frets[0] = -1; out->frets[1] = f; out->frets[2] = f + 2;
                out->frets[3] = f + 2; out->frets[4] = f + 2; out->frets[5] = f;
                break;
            case QUAL_MIN:
                out->frets[0] = -1; out->frets[1] = f; out->frets[2] = f + 2;
                out->frets[3] = f + 2; out->frets[4] = f + 1; out->frets[5] = f;
                break;
            case QUAL_MAJ7:
                out->frets[0] = -1; out->frets[1] = f; out->frets[2] = f + 2;
                out->frets[3] = f + 1; out->frets[4] = f + 2; out->frets[5] = f;
                break;
            case QUAL_MIN7:
                out->frets[0] = -1; out->frets[1] = f; out->frets[2] = f + 2;
                out->frets[3] = f; out->frets[4] = f + 1; out->frets[5] = f;
                break;
            case QUAL_DOM7:
                out->frets[0] = -1; out->frets[1] = f; out->frets[2] = f + 2;
                out->frets[3] = f; out->frets[4] = f + 2; out->frets[5] = f;
                break;
            case QUAL_DIM:
            case QUAL_M7B5:
                out->frets[0] = -1; out->frets[1] = a_fret; out->frets[2] = a_fret + 1;
                out->frets[3] = (qual == QUAL_M7B5) ? a_fret : (a_fret + 2);
                out->frets[4] = a_fret + 1; out->frets[5] = -1;
                break;
        }
    }
}

/* =========================================================================
 * 6-String Fixed-Point Karplus-Strong Plucked Guitar Synthesizer
 * (Background Synthesis Thread with Instant Preemption on Chord Change)
 * ========================================================================= */
#define KS_MAX_DELAY 600

typedef struct {
    int16_t buf[KS_MAX_DELAY];
    int     length;
    int     pos;
    int     start_delay_samples;
    int     active_samples_left;
    int16_t prev_val;
    bool    active;
} KsString;

static KsString        g_ks_strings[6];
static volatile bool   g_synth_lock         = false;
static volatile uint32_t g_synth_gen        = 0; /* Incremented on every chord change to kill active synth */
static volatile uint32_t g_active_audio_gen = 0; /* Generation currently loaded in g_ks_strings */

static pthread_t       g_synth_thread;
static pthread_mutex_t g_synth_mutex        = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_synth_cond         = PTHREAD_COND_INITIALIZER;
static volatile bool   g_synth_running      = false;
static volatile bool   g_synth_has_req      = false;
static GuitarVoicing   g_synth_req_voicing;

/* Precomputed 1024-entry sine LUT (Q14: -16384 .. +16384) for fast background excitation */
static int16_t         g_sin_lut[1024];
static bool            g_sin_lut_ready      = false;

static void init_sin_lut(void) {
    if (g_sin_lut_ready) return;
    for (int i = 0; i < 1024; i++) {
        g_sin_lut[i] = (int16_t)(sin((2.0 * M_PI * (double)i) / 1024.0) * 16384.0);
    }
    g_sin_lut_ready = true;
}

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

/* Background thread that synthesizes Karplus-Strong string buffers off the UI thread.
 * If g_synth_gen changes mid-synthesis, it immediately aborts the old chord and
 * starts synthesizing the newest requested chord.
 */
static void *synth_worker_thread_fn(void *arg) {
    (void)arg;
    KsString local_strings[6];

    while (1) {
        pthread_mutex_lock(&g_synth_mutex);
        while (!g_synth_has_req && g_synth_running) {
            pthread_cond_wait(&g_synth_cond, &g_synth_mutex);
        }
        if (!g_synth_running) {
            pthread_mutex_unlock(&g_synth_mutex);
            break;
        }
        GuitarVoicing v = g_synth_req_voicing;
        uint32_t my_gen = g_synth_gen;
        g_synth_has_req = false;
        pthread_mutex_unlock(&g_synth_mutex);

        uint32_t rng = 0x9E3779B9u ^ (my_gen * 0x85EBCA6Bu) ^ SDL_GetTicks();
        int active_order = 0;
        bool aborted = false;

        for (int s = 0; s < 6; s++) {
            /* If a newer chord was requested while synthesizing, kill this synthesis immediately */
            if (g_synth_gen != my_gen || !g_synth_running) {
                aborted = true;
                break;
            }

            KsString *ks = &local_strings[s];
            if (v.frets[s] < 0) {
                ks->active = false;
                ks->active_samples_left = 0;
                ks->length = 24;
                ks->pos = 0;
                ks->start_delay_samples = 0;
                ks->prev_val = 0;
                continue;
            }

            int midi_note = k_string_midi[s] + v.frets[s];
            double freq = 440.0 * pow(2.0, (double)(midi_note - 69) / 12.0);
            int period = (int)(SAMPLE_RATE / freq + 0.5);
            if (period < 24) period = 24;
            if (period >= KS_MAX_DELAY) period = KS_MAX_DELAY - 1;

            ks->length = period;
            ks->pos = 0;
            ks->prev_val = 0;
            /* 22 ms stagger between consecutive strings for a natural guitar downstroke */
            ks->start_delay_samples = active_order * ((SAMPLE_RATE * 22) / 1000);
            ks->active_samples_left = SAMPLE_RATE * 2; /* 2.0s ring-out */
            active_order++;

            /* Excite delay line with pick-filtered warm noise burst using fast LUT */
            int16_t raw[KS_MAX_DELAY];
            for (int i = 0; i < period; i++) {
                rng = rng * 1664525u + 1013904223u;
                int16_t r = (int16_t)((int32_t)(rng >> 16) - 32768);
                int idx1 = ((i * 1024) / period) & 1023;
                int idx2 = (idx1 * 2) & 1023;
                int32_t warm = ((int32_t)g_sin_lut[idx1] * 14000 + (int32_t)g_sin_lut[idx2] * 6000) >> 14;
                int32_t mixed = (r / 3) + warm;
                if (mixed > 28000) mixed = 28000;
                if (mixed < -28000) mixed = -28000;
                raw[i] = (int16_t)mixed;
            }
            /* Two-pass smoothing filter so higher strings sound warm & acoustic */
            for (int pass = 0; pass < 2; pass++) {
                int16_t prev = raw[period - 1];
                for (int i = 0; i < period; i++) {
                    int16_t cur = raw[i];
                    raw[i] = (int16_t)(((int32_t)cur + (int32_t)prev) >> 1);
                    prev = cur;
                }
            }
            memcpy(ks->buf, raw, (size_t)period * sizeof(int16_t));
            ks->active = true;
        }

        if (aborted || g_synth_gen != my_gen || !g_synth_running) {
            continue;
        }

        /* Publish newly synthesized chord to audio playback */
        g_synth_lock = true;
        memcpy(g_ks_strings, local_strings, sizeof(g_ks_strings));
        g_active_audio_gen = my_gen;
        g_synth_lock = false;
    }
    return NULL;
}

static void start_synth_worker(void) {
    init_sin_lut();
    pthread_mutex_lock(&g_synth_mutex);
    g_synth_running = true;
    g_synth_has_req = false;
    pthread_mutex_unlock(&g_synth_mutex);
    pthread_create(&g_synth_thread, NULL, synth_worker_thread_fn, NULL);
}

static void stop_synth_worker(void) {
    pthread_mutex_lock(&g_synth_mutex);
    if (!g_synth_running) {
        pthread_mutex_unlock(&g_synth_mutex);
        return;
    }
    g_synth_running = false;
    g_synth_gen++;
    pthread_cond_signal(&g_synth_cond);
    pthread_mutex_unlock(&g_synth_mutex);
    pthread_join(g_synth_thread, NULL);
}

/* Non-blocking chord strum trigger called from the UI thread:
 * Immediately kills any previous chord playback/synthesis and wakes the background synth thread.
 */
static void trigger_guitar_strum(const GuitarVoicing *v) {
    pthread_mutex_lock(&g_synth_mutex);
    g_synth_gen++;
    g_synth_req_voicing = *v;
    g_synth_has_req     = true;
    pthread_cond_signal(&g_synth_cond);
    pthread_mutex_unlock(&g_synth_mutex);
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int frames = len / (2 * AUDIO_CHANNELS);

    /* If a new chord is being synthesized (or lock is held), immediately silence the old chord */
    if (g_synth_lock || g_active_audio_gen != g_synth_gen) {
        memset(stream, 0, (size_t)len);
        return;
    }

    for (int f = 0; f < frames; f++) {
        int32_t mix = 0;
        for (int s = 0; s < 6; s++) {
            KsString *ks = &g_ks_strings[s];
            if (!ks->active || ks->active_samples_left <= 0) continue;

            if (ks->start_delay_samples > 0) {
                ks->start_delay_samples--;
                continue;
            }

            int p = ks->pos;
            int next_p = (p + 1 < ks->length) ? (p + 1) : 0;
            int32_t cur = ks->buf[p];
            int32_t nxt = ks->buf[next_p];

            /* Karplus-Strong 1-zero loop filter with Q15 damping (32590 / 32768 = 0.9945) */
            int32_t avg = (cur + nxt) >> 1;
            int32_t damped = (avg * 32590) >> 15;
            ks->buf[p] = (int16_t)damped;
            ks->pos = next_p;
            ks->active_samples_left--;
            if (ks->active_samples_left == 0) ks->active = false;

            mix += cur;
        }

        /* Soft clip 6-string sum into 16-bit signed audio */
        mix = (mix * 3) >> 2;
        if (mix > 32767)  mix = 32767;
        if (mix < -32767) mix = -32767;

        int16_t sample = (int16_t)mix;
        for (int c = 0; c < AUDIO_CHANNELS; c++) {
            out[f * AUDIO_CHANNELS + c] = sample;
        }
    }
}

/* =========================================================================
 * Application State & Chord Resolution
 * ========================================================================= */
typedef struct {
    int  sector;    /* 0..11 around the Circle of Fifths */
    int  ring;      /* 0 = Outer (Major), 1 = Inner (Minor), 2 = Center (vii° Dim) */
    bool use_7th;   /* Triad vs 7th when queued */
} ProgSlot;

static int      g_key_sector    = 0;     /* Active Key Signature sector (0=C, 1=G, 2=D, ...) */
static int      g_cur_sector    = 0;     /* Highlighted cursor sector (0..11) */
static int      g_cur_ring      = 0;     /* 0 = Outer Major, 1 = Inner Minor, 2 = Center vii° */
static bool     g_key_locked    = false; /* false = Key-Rotate Mode, true = Chord-Explore Mode */
static bool     g_seventh_mode  = false; /* false = Triads, true = 7th chords */
static int      g_voicing_idx   = 0;     /* 0 = Open/CAGED, 1 = E-Barre, 2 = A-Barre */
static bool     g_show_help     = false; /* Help & Progression Presets modal */

static ProgSlot g_prog[4];
static int      g_prog_write_idx = 0;
static bool     g_loop_playing   = false;
static int      g_loop_step      = 0;
static Uint32   g_last_loop_ms   = 0;
static int      g_preset_idx     = 0;

static char     g_config_path[512] = "";

typedef struct {
    const char *name;
    int         sector_offsets[4]; /* Offset relative to g_key_sector */
    int         rings[4];          /* 0 = Major, 1 = Minor, 2 = Dim */
} ProgPreset;

static const ProgPreset k_presets[5] = {
    { "I - V - vi - IV (Pop/Rock)",      {  0,  1,  0, 11 }, { 0, 0, 1, 0 } },
    { "I - IV - V - I (Blues/Folk)",     {  0, 11,  1,  0 }, { 0, 0, 0, 0 } },
    { "ii - V - I - vi (Jazz Turn)",     { 11,  1,  0,  0 }, { 1, 0, 0, 1 } },
    { "I - vi - IV - V (50s Classic)",   {  0,  0, 11,  1 }, { 0, 1, 0, 0 } },
    { "vi - IV - I - V (Minor Anthem)",  {  0, 11,  0,  1 }, { 1, 0, 0, 0 } }
};

static void apply_progression_preset(int preset_id) {
    preset_id = ((preset_id % 5) + 5) % 5;
    g_preset_idx = preset_id;
    for (int i = 0; i < 4; i++) {
        g_prog[i].sector  = (g_key_sector + k_presets[preset_id].sector_offsets[i]) % 12;
        g_prog[i].ring    = k_presets[preset_id].rings[i];
        g_prog[i].use_7th = g_seventh_mode;
    }
    g_prog_write_idx = 0;
}

typedef struct {
    int          root_pc;
    ChordQuality qual;
    char         chord_name[24];
    char         roman_num[16];
    char         func_desc[32];
    char         notes_str[32];
    int          diatonic_family; /* 0 = Non-diatonic, 1 = Tonic (I/vi), 2 = Subdom (IV/ii), 3 = Dom (V/iii/vii°) */
} ResolvedChord;

static void resolve_chord_at(int key_sec, int sec, int ring, bool seventh, ResolvedChord *out) {
    key_sec = ((key_sec % 12) + 12) % 12;
    sec     = ((sec % 12) + 12) % 12;
    int rel = (sec - key_sec + 12) % 12;
    bool flats = k_wheel[key_sec].use_flats;
    const char **note_tbl = flats ? k_note_flat : k_note_sharp;

    memset(out, 0, sizeof(*out));

    if (ring == 2) {
        /* Center Hub: Leading-Tone Diminished (vii dim in key_sec) */
        out->root_pc = k_wheel[key_sec].dim_pc;
        out->qual = seventh ? QUAL_M7B5 : QUAL_DIM;
        snprintf(out->chord_name, sizeof(out->chord_name), "%s%s",
                 k_wheel[key_sec].dim_root_name, seventh ? "m7b5" : "dim");
        snprintf(out->roman_num, sizeof(out->roman_num), "%s", seventh ? "viim7b5" : "viidim");
        snprintf(out->func_desc, sizeof(out->func_desc), "Leading-Tone Dim");
        out->diatonic_family = 3;
    } else if (ring == 0) {
        /* Outer Ring: Major / Dominant Chords */
        out->root_pc = k_wheel[sec].maj_pc;
        if (rel == 0) {
            out->qual = seventh ? QUAL_MAJ7 : QUAL_MAJ;
            snprintf(out->roman_num, sizeof(out->roman_num), "%s", seventh ? "Imaj7" : "I");
            snprintf(out->func_desc, sizeof(out->func_desc), "Tonic Major");
            out->diatonic_family = 1;
        } else if (rel == 11) {
            out->qual = seventh ? QUAL_MAJ7 : QUAL_MAJ;
            snprintf(out->roman_num, sizeof(out->roman_num), "%s", seventh ? "IVmaj7" : "IV");
            snprintf(out->func_desc, sizeof(out->func_desc), "Subdominant");
            out->diatonic_family = 2;
        } else if (rel == 1) {
            out->qual = seventh ? QUAL_DOM7 : QUAL_MAJ;
            snprintf(out->roman_num, sizeof(out->roman_num), "%s", seventh ? "V7" : "V");
            snprintf(out->func_desc, sizeof(out->func_desc), "Dominant");
            out->diatonic_family = 3;
        } else {
            /* Non-diatonic outer-ring chord (Secondary Dominant or Borrowed Flat Chord) */
            out->qual = seventh ? QUAL_DOM7 : QUAL_MAJ;
            out->diatonic_family = 0;
            static const char *k_maj_romans[12] = {
                "I", "V", "V/V", "V/vi", "V/ii", "V/iii", "bV", "bII", "bVI", "bIII", "bVII", "IV"
            };
            static const char *k_maj_funcs[12] = {
                "Tonic", "Dominant", "Sec Dom (II)", "Sec Dom (VI)", "Sec Dom (III)",
                "Sec Dom (VII)", "Tritone Sub", "Neapolitan bII", "Borrowed bVI",
                "Borrowed bIII", "Borrowed bVII", "Subdominant"
            };
            snprintf(out->roman_num, sizeof(out->roman_num), "%s%s",
                     k_maj_romans[rel], seventh ? "7" : "");
            snprintf(out->func_desc, sizeof(out->func_desc), "%s", k_maj_funcs[rel]);
        }
        const char *suffix = (out->qual == QUAL_MAJ7) ? "maj7" :
                             (out->qual == QUAL_DOM7) ? "7"    : "";
        snprintf(out->chord_name, sizeof(out->chord_name), "%s%s", k_wheel[sec].maj_name, suffix);
    } else {
        /* Inner Ring: Minor Chords */
        out->root_pc = k_wheel[sec].min_pc;
        out->qual = seventh ? QUAL_MIN7 : QUAL_MIN;
        if (rel == 0) {
            snprintf(out->roman_num, sizeof(out->roman_num), "%s", seventh ? "vi7" : "vi");
            snprintf(out->func_desc, sizeof(out->func_desc), "Relative Minor");
            out->diatonic_family = 1;
        } else if (rel == 11) {
            snprintf(out->roman_num, sizeof(out->roman_num), "%s", seventh ? "ii7" : "ii");
            snprintf(out->func_desc, sizeof(out->func_desc), "Supertonic Min");
            out->diatonic_family = 2;
        } else if (rel == 1) {
            snprintf(out->roman_num, sizeof(out->roman_num), "%s", seventh ? "iii7" : "iii");
            snprintf(out->func_desc, sizeof(out->func_desc), "Mediant Minor");
            out->diatonic_family = 3;
        } else {
            out->diatonic_family = 0;
            static const char *k_min_romans[12] = {
                "vi", "iii", "vii", "#iv", "#i", "#v", "biii", "bvii", "iv", "i", "v", "ii"
            };
            static const char *k_min_funcs[12] = {
                "Relative Min", "Mediant Min", "Min v/III", "Chromatic Min", "Chromatic Min",
                "Chromatic Min", "Chromatic Min", "Borrowed bvii", "Borrowed iv",
                "Parallel Min (i)", "Modal Min (v)", "Supertonic"
            };
            snprintf(out->roman_num, sizeof(out->roman_num), "%s%s",
                     k_min_romans[rel], seventh ? "7" : "");
            snprintf(out->func_desc, sizeof(out->func_desc), "%s", k_min_funcs[rel]);
        }
        snprintf(out->chord_name, sizeof(out->chord_name), "%s%s",
                 k_wheel[sec].min_name, seventh ? "7" : "");
    }

    /* Compute chord formula notes (Root, 3rd, 5th, [7th]) */
    int i3 = (out->qual == QUAL_MAJ || out->qual == QUAL_MAJ7 || out->qual == QUAL_DOM7) ? 4 : 3;
    int i5 = (out->qual == QUAL_DIM || out->qual == QUAL_M7B5) ? 6 : 7;
    int n1 = out->root_pc;
    int n2 = (out->root_pc + i3) % 12;
    int n3 = (out->root_pc + i5) % 12;

    if (out->qual == QUAL_MAJ7 || out->qual == QUAL_MIN7 || out->qual == QUAL_DOM7 || out->qual == QUAL_M7B5) {
        int i7 = (out->qual == QUAL_MAJ7) ? 11 : 10;
        int n4 = (out->root_pc + i7) % 12;
        snprintf(out->notes_str, sizeof(out->notes_str), "%s - %s - %s - %s",
                 note_tbl[n1], note_tbl[n2], note_tbl[n3], note_tbl[n4]);
    } else {
        snprintf(out->notes_str, sizeof(out->notes_str), "%s - %s - %s",
                 note_tbl[n1], note_tbl[n2], note_tbl[n3]);
    }
}

static void strum_current_selection(void) {
    ResolvedChord rc;
    resolve_chord_at(g_key_sector, g_cur_sector, g_cur_ring, g_seventh_mode, &rc);
    GuitarVoicing gv;
    build_guitar_voicing(rc.root_pc, rc.qual, g_voicing_idx, &gv);
    trigger_guitar_strum(&gv);
}

/* =========================================================================
 * Persistent Configuration (/mnt/.chordwheel.cfg or ~/.chordwheel.cfg)
 * ========================================================================= */
static void resolve_config_path(void) {
    const char *env_cfg = getenv("CHORDWHEEL_CFG");
    if (env_cfg && env_cfg[0] != '\0') {
        snprintf(g_config_path, sizeof(g_config_path), "%s", env_cfg);
        return;
    }
    struct stat st;
    if (stat("/mnt", &st) == 0 && S_ISDIR(st.st_mode) && access("/mnt", W_OK) == 0) {
        snprintf(g_config_path, sizeof(g_config_path), "/mnt/.chordwheel.cfg");
    } else {
        const char *home = getenv("HOME");
        if (home && access(home, W_OK) == 0) {
            snprintf(g_config_path, sizeof(g_config_path), "%s/.chordwheel.cfg", home);
        } else {
            snprintf(g_config_path, sizeof(g_config_path), "./.chordwheel.cfg");
        }
    }
}

static void save_config(void) {
    if (g_config_path[0] == '\0') return;
    FILE *f = fopen(g_config_path, "w");
    if (!f) return;
    fprintf(f, "key_sector=%d\n", g_key_sector);
    fprintf(f, "cur_sector=%d\n", g_cur_sector);
    fprintf(f, "cur_ring=%d\n", g_cur_ring);
    fprintf(f, "key_locked=%d\n", g_key_locked ? 1 : 0);
    fprintf(f, "seventh_mode=%d\n", g_seventh_mode ? 1 : 0);
    fprintf(f, "voicing_idx=%d\n", g_voicing_idx);
    fprintf(f, "preset_idx=%d\n", g_preset_idx);
    for (int i = 0; i < 4; i++) {
        fprintf(f, "prog%d=%d,%d,%d\n", i,
                g_prog[i].sector, g_prog[i].ring, g_prog[i].use_7th ? 1 : 0);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

static void load_config(void) {
    apply_progression_preset(0);
    if (g_config_path[0] == '\0') return;
    FILE *f = fopen(g_config_path, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int v = 0, s = 0, r = 0, u7 = 0, idx = 0;
        if (sscanf(line, "key_sector=%d", &v) == 1 && v >= 0 && v < 12) {
            g_key_sector = v;
        } else if (sscanf(line, "cur_sector=%d", &v) == 1 && v >= 0 && v < 12) {
            g_cur_sector = v;
        } else if (sscanf(line, "cur_ring=%d", &v) == 1 && v >= 0 && v <= 2) {
            g_cur_ring = v;
        } else if (sscanf(line, "key_locked=%d", &v) == 1) {
            g_key_locked = (v != 0);
        } else if (sscanf(line, "seventh_mode=%d", &v) == 1) {
            g_seventh_mode = (v != 0);
        } else if (sscanf(line, "voicing_idx=%d", &v) == 1 && v >= 0 && v < 3) {
            g_voicing_idx = v;
        } else if (sscanf(line, "preset_idx=%d", &v) == 1 && v >= 0 && v < 5) {
            g_preset_idx = v;
        } else if (sscanf(line, "prog%d=%d,%d,%d", &idx, &s, &r, &u7) == 4 &&
                   idx >= 0 && idx < 4 && s >= 0 && s < 12 && r >= 0 && r <= 2) {
            g_prog[idx].sector  = s;
            g_prog[idx].ring    = r;
            g_prog[idx].use_7th = (u7 != 0);
        }
    }
    fclose(f);
}

/* =========================================================================
 * Circle of Fifths Wheel & Guitar Fretboard Rendering (320x240)
 * ========================================================================= */
#define WHEEL_CX 86.0f
#define WHEEL_CY 112.0f
#define R_OUTER_MAX 84.0f
#define R_OUTER_MIN 56.5f
#define R_INNER_MAX 55.0f
#define R_INNER_MIN 31.5f
#define R_HUB_MAX   30.0f

/* Precomputed 2x2 subpixel lookup table for the Circle of Fifths wheel
 * so rendering on the FPU-less ARM926EJ-S uses pure integer table lookups:
 *   bits 0..3 : sector (0..11)
 *   bits 4..6 : zone:
 *     0 = background outside wheel
 *     1 = separator gap / ring gap
 *     2 = outer ring interior
 *     3 = outer ring radial/ring border (highlighted when selected, sep_col when on thin radial gap)
 *     4 = outer ring ring-only border (highlighted when selected, else outer_fill)
 *     5 = inner ring interior
 *     6 = inner ring radial/ring border (highlighted when selected, sep_col when on thin radial gap)
 *     7 = inner ring ring-only border (highlighted when selected, else inner_fill)
 *   bit 7     : hub flag (if zone==0: 1=hub interior, if zone==1: 1=hub border)
 */
static uint8_t g_wheel_lut[SCREEN_H][174][4];
static bool    g_wheel_lut_ready = false;

static void init_wheel_lut(void) {
    if (g_wheel_lut_ready) return;
    memset(g_wheel_lut, 0, sizeof(g_wheel_lut));

    int x0 = (int)(WHEEL_CX - R_OUTER_MAX - 2);
    int x1 = (int)(WHEEL_CX + R_OUTER_MAX + 2);
    int y0 = (int)(WHEEL_CY - R_OUTER_MAX - 2);
    int y1 = (int)(WHEEL_CY + R_OUTER_MAX + 2);

    for (int py = y0; py <= y1; py++) {
        if (py < 0 || py >= SCREEN_H) continue;
        for (int px = x0; px <= x1; px++) {
            if (px < 0 || px >= 174) continue;
            for (int sy = 0; sy < 2; sy++) {
                for (int sx = 0; sx < 2; sx++) {
                    int sub = sy * 2 + sx;
                    float fx = (float)px + (sx ? 0.75f : 0.25f) - WHEEL_CX;
                    float fy = (float)py + (sy ? 0.75f : 0.25f) - WHEEL_CY;
                    float r = sqrtf(fx * fx + fy * fy);

                    uint8_t code = 0; /* 0 = bg_col */
                    if (r <= R_OUTER_MAX) {
                        float deg = atan2f(fy, fx) * (180.0f / (float)M_PI) + 90.0f + 15.0f;
                        while (deg < 0.0f)   deg += 360.0f;
                        while (deg >= 360.0f) deg -= 360.0f;
                        int sec = ((int)(deg / 30.0f)) % 12;
                        float sec_pos = fmodf(deg, 30.0f);

                        if (r >= R_OUTER_MIN && r <= R_OUTER_MAX) {
                            bool on_radial_edge = (sec_pos < 1.3f || sec_pos > 28.7f);
                            bool on_ring_edge   = (r < R_OUTER_MIN + 1.4f || r > R_OUTER_MAX - 1.4f);
                            bool on_sep         = (sec_pos < 0.8f || sec_pos > 29.2f);
                            int z = 2;
                            if (on_sep) z = 3; /* sel ? sel_bdr : sep_col */
                            else if (on_radial_edge || on_ring_edge) z = 4; /* sel ? sel_bdr : outer_fill */
                            code = (uint8_t)((z << 4) | (sec & 0x0F));
                        } else if (r >= R_INNER_MIN && r <= R_INNER_MAX) {
                            bool on_radial_edge = (sec_pos < 1.8f || sec_pos > 28.2f);
                            bool on_ring_edge   = (r < R_INNER_MIN + 1.4f || r > R_INNER_MAX - 1.4f);
                            bool on_sep         = (sec_pos < 1.0f || sec_pos > 29.0f);
                            int z = 5;
                            if (on_sep) z = 6; /* sel ? sel_bdr : sep_col */
                            else if (on_radial_edge || on_ring_edge) z = 7; /* sel ? sel_bdr : inner_fill */
                            code = (uint8_t)((z << 4) | (sec & 0x0F));
                        } else if (r <= R_HUB_MAX) {
                            if (r > R_HUB_MAX - 1.5f) {
                                code = 0x80 | 0x10; /* Hub border */
                            } else {
                                code = 0x80;        /* Hub interior */
                            }
                        } else {
                            code = (1 << 4); /* sep_col ring gap */
                        }
                    }
                    g_wheel_lut[py][px][sub] = code;
                }
            }
        }
    }
    g_wheel_lut_ready = true;
}

static void render_circle_of_fifths_wheel(void) {
    init_wheel_lut();

    /* Precompute sector colors for both rings (0..11) */
    uint32_t outer_fill[12], inner_fill[12];
    for (int s = 0; s < 12; s++) {
        int rel = (s - g_key_sector + 12) % 12;
        bool sel_outer = (g_cur_ring == 0 && g_cur_sector == s);
        bool sel_inner = (g_cur_ring == 1 && g_cur_sector == s);

        /* Outer Ring (Major): I = Gold, IV = Sky Blue, V = Emerald */
        if (rel == 0) {
            outer_fill[s] = sel_outer ? RGB(245, 158, 11) : RGB(180, 112, 12);
        } else if (rel == 11) {
            outer_fill[s] = sel_outer ? RGB(37, 99, 235)  : RGB(29, 78, 178);
        } else if (rel == 1) {
            outer_fill[s] = sel_outer ? RGB(16, 185, 129) : RGB(14, 132, 92);
        } else {
            outer_fill[s] = sel_outer ? RGB(71, 85, 105)  : RGB(26, 35, 52);
        }

        /* Inner Ring (Minor): vi = Warm Amber/Purple, ii = Deep Cyan/Blue, iii = Teal */
        if (rel == 0) {
            inner_fill[s] = sel_inner ? RGB(217, 119, 6)  : RGB(138, 78, 14);
        } else if (rel == 11) {
            inner_fill[s] = sel_inner ? RGB(59, 130, 246) : RGB(30, 64, 142);
        } else if (rel == 1) {
            inner_fill[s] = sel_inner ? RGB(20, 184, 166) : RGB(15, 110, 96);
        } else {
            inner_fill[s] = sel_inner ? RGB(71, 85, 105)  : RGB(21, 29, 44);
        }
    }

    uint32_t hub_fill = (g_cur_ring == 2) ? RGB(190, 24, 93) : RGB(30, 41, 59);
    uint32_t bg_col   = RGB(13, 18, 28);
    uint32_t sep_col  = RGB(11, 15, 24);
    uint32_t sel_bdr  = RGB(254, 240, 138);
    uint32_t hub_bdr  = (g_cur_ring == 2) ? sel_bdr : RGB(96, 165, 250);

    /* Fast integer LUT scan over the wheel bounding box */
    int x0 = (int)(WHEEL_CX - R_OUTER_MAX - 2);
    int x1 = (int)(WHEEL_CX + R_OUTER_MAX + 2);
    int y0 = (int)(WHEEL_CY - R_OUTER_MAX - 2);
    int y1 = (int)(WHEEL_CY + R_OUTER_MAX + 2);

    for (int py = y0; py <= y1; py++) {
        if (py < 0 || py >= SCREEN_H) continue;
        for (int px = x0; px <= x1; px++) {
            if (px < 0 || px >= 174) continue;

            uint32_t r_acc = 0, g_acc = 0, b_acc = 0;
            const uint8_t *sub_lut = g_wheel_lut[py][px];
            for (int sub = 0; sub < 4; sub++) {
                uint8_t code = sub_lut[sub];
                uint32_t sc;
                if (code == 0) {
                    sc = bg_col;
                } else if (code & 0x80) {
                    sc = (code & 0x10) ? hub_bdr : hub_fill;
                } else {
                    int z   = (code >> 4) & 0x07;
                    int sec = code & 0x0F;
                    if (z == 1) {
                        sc = sep_col;
                    } else if (z <= 4) {
                        bool sel = (g_cur_ring == 0 && g_cur_sector == sec);
                        if (z == 2)      sc = outer_fill[sec];
                        else if (z == 3) sc = sel ? sel_bdr : sep_col;
                        else             sc = sel ? sel_bdr : outer_fill[sec];
                    } else {
                        bool sel = (g_cur_ring == 1 && g_cur_sector == sec);
                        if (z == 5)      sc = inner_fill[sec];
                        else if (z == 6) sc = sel ? sel_bdr : sep_col;
                        else             sc = sel ? sel_bdr : inner_fill[sec];
                    }
                }
                r_acc += (sc >> 16) & 0xFF;
                g_acc += (sc >> 8)  & 0xFF;
                b_acc += sc & 0xFF;
            }
            g_fb[py * SCREEN_W + px] = RGB(r_acc >> 2, g_acc >> 2, b_acc >> 2);
        }
    }

    /* Draw chord labels on each of the 12 Outer & Inner sectors */
    float r_outer_lbl = (R_OUTER_MIN + R_OUTER_MAX) * 0.5f;
    float r_inner_lbl = (R_INNER_MIN + R_INNER_MAX) * 0.5f;

    for (int s = 0; s < 12; s++) {
        float ang = ((float)s * 30.0f - 90.0f) * ((float)M_PI / 180.0f);
        float cos_a = cosf(ang);
        float sin_a = sinf(ang);
        int rel = (s - g_key_sector + 12) % 12;
        bool in_key = (rel == 0 || rel == 1 || rel == 11);

        int ox = (int)(WHEEL_CX + cos_a * r_outer_lbl + 0.5f);
        int oy = (int)(WHEEL_CY + sin_a * r_outer_lbl + 0.5f);
        bool sel_o = (g_cur_ring == 0 && g_cur_sector == s);
        uint32_t o_col = sel_o ? RGB(255, 255, 255) :
                         (in_key ? RGB(255, 251, 235) : RGB(148, 163, 184));
        draw_text_centered(FONT_UI_11, ox - 14, oy - 7, 28, 14, k_wheel[s].maj_name, o_col);

        int ix = (int)(WHEEL_CX + cos_a * r_inner_lbl + 0.5f);
        int iy = (int)(WHEEL_CY + sin_a * r_inner_lbl + 0.5f);
        bool sel_i = (g_cur_ring == 1 && g_cur_sector == s);
        uint32_t i_col = sel_i ? RGB(255, 255, 255) :
                         (in_key ? RGB(241, 245, 249) : RGB(120, 136, 158));
        draw_text_centered(FONT_UI_9, ix - 14, iy - 6, 28, 12, k_wheel[s].min_name, i_col);
    }

    /* Center Hub Text: Key Name + Key Signature + vii° Diminished Chord */
    char key_hub[20];
    snprintf(key_hub, sizeof(key_hub), "KEY %s", k_wheel[g_key_sector].maj_name);
    draw_text_centered(FONT_UI_11, (int)WHEEL_CX - 26, (int)WHEEL_CY - 19, 52, 12,
                       key_hub, RGB(250, 204, 21));
    draw_text_centered(FONT_UI_9, (int)WHEEL_CX - 26, (int)WHEEL_CY - 7, 52, 11,
                       k_wheel[g_key_sector].key_sig, RGB(186, 230, 253));

    char dim_lbl[20];
    snprintf(dim_lbl, sizeof(dim_lbl), "%s%s",
             k_wheel[g_key_sector].dim_root_name, g_seventh_mode ? "m7b5" : "dim");
    draw_text_centered(FONT_UI_9, (int)WHEEL_CX - 26, (int)WHEEL_CY + 5, 52, 11,
                       dim_lbl, (g_cur_ring == 2) ? RGB(254, 240, 138) : RGB(244, 114, 182));

    /* Top-left Header Badge & Bottom-left Mode Pill */
    draw_pill(4, 3, 98, 16, RGB(23, 33, 52), RGB(56, 189, 248));
    draw_text_centered(FONT_UI_9, 4, 3, 98, 16, "CIRCLE OF FIFTHS", RGB(186, 230, 253));

    uint32_t lock_fill = g_key_locked ? RGB(124, 45, 18) : RGB(20, 83, 45);
    uint32_t lock_bdr  = g_key_locked ? RGB(251, 146, 60) : RGB(74, 222, 128);
    draw_pill(105, 3, 66, 16, lock_fill, lock_bdr);
    draw_text_centered(FONT_UI_9, 105, 3, 66, 16,
                       g_key_locked ? "SEL:LOCKED" : "SEL:ROTATE",
                       RGB(255, 255, 255));

    /* Diatonic Color Legend beneath the wheel (y=200..215) */
    draw_pill(4, 200, 52, 14, RGB(146, 92, 12), RGB(250, 204, 21));
    draw_text_centered(FONT_UI_9, 4, 200, 52, 14, "I / vi", RGB(255, 255, 255));

    draw_pill(59, 200, 54, 14, RGB(29, 78, 178), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_9, 59, 200, 54, 14, "IV / ii", RGB(255, 255, 255));

    draw_pill(116, 200, 55, 14, RGB(14, 132, 92), RGB(52, 211, 153));
    draw_text_centered(FONT_UI_9, 116, 200, 55, 14, "V / iii", RGB(255, 255, 255));
}

static void render_guitar_inspector_pane(void) {
    int px = 175, pw = SCREEN_W - px - 3;

    ResolvedChord rc;
    resolve_chord_at(g_key_sector, g_cur_sector, g_cur_ring, g_seventh_mode, &rc);

    GuitarVoicing gv;
    build_guitar_voicing(rc.root_pc, rc.qual, g_voicing_idx, &gv);

    bool flats = k_wheel[g_key_sector].use_flats;
    const char **note_tbl = flats ? k_note_flat : k_note_sharp;

    /* 1. Selected Chord & Roman Function Card (y = 3..53) */
    fill_rect(px, 3, pw, 50, RGB(21, 29, 45));
    draw_border(px, 3, pw, 50, RGB(51, 68, 98));

    /* Chord Name + Roman Numeral Badge + 7th Mode Pill */
    draw_text(FONT_TITLE_16, px + 6, 6, rc.chord_name, RGB(250, 204, 21));

    int cname_w = text_width(FONT_TITLE_16, rc.chord_name);
    uint32_t fam_fill = (rc.diatonic_family == 1) ? RGB(146, 92, 12) :
                        (rc.diatonic_family == 2) ? RGB(29, 78, 178) :
                        (rc.diatonic_family == 3) ? RGB(14, 132, 92) : RGB(68, 64, 92);
    uint32_t fam_bdr  = (rc.diatonic_family == 1) ? RGB(250, 204, 21) :
                        (rc.diatonic_family == 2) ? RGB(96, 165, 250) :
                        (rc.diatonic_family == 3) ? RGB(52, 211, 153) : RGB(167, 139, 250);
    int rpill_w = text_width(FONT_UI_9, rc.roman_num) + 10;
    draw_pill(px + 10 + cname_w, 8, rpill_w, 15, fam_fill, fam_bdr);
    draw_text_centered(FONT_UI_9, px + 10 + cname_w, 8, rpill_w, 15, rc.roman_num, RGB(255, 255, 255));

    draw_pill(px + pw - 38, 6, 34, 14,
              g_seventh_mode ? RGB(109, 40, 217) : RGB(30, 58, 95),
              g_seventh_mode ? RGB(192, 132, 252) : RGB(96, 165, 250));
    draw_text_centered(FONT_UI_9, px + pw - 38, 6, 34, 14,
                       g_seventh_mode ? "B:7TH" : "B:TRI", RGB(255, 255, 255));

    draw_text(FONT_UI_9, px + 6, 25, rc.func_desc, RGB(148, 163, 184));
    draw_text(FONT_UI_11, px + 6, 37, rc.notes_str, RGB(226, 232, 240));

    /* 2. 6-String Guitar Fretboard Diagram Card (y = 56..171) */
    int fy = 56, fh = 115;
    fill_rect(px, fy, pw, fh, RGB(21, 29, 45));
    draw_border(px, fy, pw, fh, RGB(51, 68, 98));

    draw_text(FONT_UI_9, px + 6, fy + 4, gv.label, RGB(125, 211, 252));
    draw_text_right(FONT_UI_9, px + pw - 5, fy + 4, "L1/R1", RGB(250, 204, 21));

    /* Compute base fret (1 unless max fretted > 4) */
    int min_fret = 99, max_fret = 0;
    for (int s = 0; s < 6; s++) {
        if (gv.frets[s] > 0) {
            if (gv.frets[s] < min_fret) min_fret = gv.frets[s];
            if (gv.frets[s] > max_fret) max_fret = gv.frets[s];
        }
    }
    int base_fret = 1;
    if (max_fret > 4) {
        base_fret = (min_fret < 99 && min_fret > 1) ? min_fret : (max_fret - 3);
    }

    int grid_x = px + 26;
    int grid_y = fy + 29;
    int str_dx = 19;             /* 5 string intervals * 19 = 95 px */
    int fret_dy = 14;            /* 4 fret spaces * 14 = 56 px */
    int grid_w = str_dx * 5;
    int grid_h = fret_dy * 4;

    /* Fretboard wood background */
    fill_rect(grid_x, grid_y, grid_w + 1, grid_h + 1, RGB(34, 27, 24));

    /* Nut bar (thick ivory bar when base_fret == 1) or base fret number ("3fr") */
    if (base_fret == 1) {
        fill_rect(grid_x, grid_y - 2, grid_w + 1, 3, RGB(241, 245, 249));
    } else {
        char fr_str[12];
        snprintf(fr_str, sizeof(fr_str), "%dfr", base_fret);
        draw_text_right(FONT_UI_9, grid_x - 4, grid_y + 2, fr_str, RGB(250, 204, 21));
        fill_rect(grid_x, grid_y, grid_w + 1, 1, RGB(148, 163, 184));
    }

    /* Fret wires */
    for (int f = 1; f <= 4; f++) {
        fill_rect(grid_x, grid_y + f * fret_dy, grid_w + 1, 1, RGB(120, 130, 145));
    }

    /* 6 guitar strings (wound bass strings slightly brighter/thicker) */
    for (int s = 0; s < 6; s++) {
        int sx = grid_x + s * str_dx;
        fill_rect(sx, grid_y, 1, grid_h + 1, (s < 3) ? RGB(203, 213, 225) : RGB(148, 163, 184));

        /* Above-nut X / O markers */
        if (gv.frets[s] < 0) {
            draw_text_centered(FONT_UI_9, sx - 5, grid_y - 13, 11, 11, "x", RGB(248, 113, 113));
        } else if (gv.frets[s] == 0) {
            draw_text_centered(FONT_UI_9, sx - 5, grid_y - 13, 11, 11, "o", RGB(74, 222, 128));
        }

        /* Sounded note name beneath string (or string name if muted) */
        int sy_lbl = grid_y + grid_h + 4;
        if (gv.frets[s] >= 0) {
            int pc = (k_string_midi[s] + gv.frets[s]) % 12;
            bool is_root = (pc == rc.root_pc);
            draw_text_centered(FONT_UI_9, sx - 8, sy_lbl, 17, 11,
                               note_tbl[pc], is_root ? RGB(250, 204, 21) : RGB(203, 213, 225));
        } else {
            draw_text_centered(FONT_UI_9, sx - 6, sy_lbl, 13, 11,
                               k_string_names[s], RGB(100, 116, 139));
        }
    }

    /* Finger dots on fretted strings */
    for (int s = 0; s < 6; s++) {
        int f = gv.frets[s];
        if (f > 0) {
            int rel_f = f - base_fret + 1;
            if (rel_f >= 1 && rel_f <= 4) {
                float cx = (float)(grid_x + s * str_dx);
                float cy = (float)(grid_y + (rel_f - 1) * fret_dy + fret_dy / 2);
                int pc = (k_string_midi[s] + f) % 12;
                bool is_root = (pc == rc.root_pc);
                uint32_t dot_fill = is_root ? RGB(245, 158, 11) : RGB(14, 165, 233);
                uint32_t dot_bdr  = is_root ? RGB(254, 240, 138) : RGB(224, 242, 254);
                draw_filled_circle_aa(cx, cy, 5.2f, dot_fill, dot_bdr);
            }
        }
    }

    /* 3. Diatonic Chords in Active Key Card (y = 174..215) */
    int dy = 174, dh = 41;
    fill_rect(px, dy, pw, dh, RGB(21, 29, 45));
    draw_border(px, dy, pw, dh, RGB(51, 68, 98));

    char d_hdr[40];
    snprintf(d_hdr, sizeof(d_hdr), "DIATONIC (%s MAJ)", k_wheel[g_key_sector].maj_name);
    draw_text(FONT_UI_9, px + 5, dy + 3, d_hdr, RGB(148, 163, 184));
    draw_text_right(FONT_UI_9, px + pw - 5, dy + 3, "A:Strum", RGB(74, 222, 128));

    /* 7 Diatonic Degrees: I, ii, iii, IV, V, vi, vii° */
    static const int k_deg_off[7]  = {  0, 11,  1, 11,  1,  0,  0 };
    static const int k_deg_ring[7] = {  0,  1,  1,  0,  0,  1,  2 };
    int cell_w = (pw - 8) / 7;
    for (int d = 0; d < 7; d++) {
        int d_sec = (g_key_sector + k_deg_off[d]) % 12;
        int d_rng = k_deg_ring[d];
        bool is_sel = (d_rng == 2) ? (g_cur_ring == 2) :
                      (g_cur_ring == d_rng && g_cur_sector == d_sec);
        int cx = px + 4 + d * cell_w;
        int cy = dy + 16;
        if (is_sel) {
            fill_rect(cx, cy, cell_w - 1, 21, RGB(37, 99, 235));
            draw_border(cx, cy, cell_w - 1, 21, RGB(250, 204, 21));
        } else {
            fill_rect(cx, cy, cell_w - 1, 21, RGB(15, 22, 34));
        }
        const char *ch_lbl = (d_rng == 0) ? k_wheel[d_sec].maj_name :
                             (d_rng == 1) ? k_wheel[d_sec].min_name :
                                            k_wheel[g_key_sector].dim_root_name;
        draw_text_centered(FONT_UI_9, cx, cy + 4, cell_w - 1, 13, ch_lbl,
                           is_sel ? RGB(254, 240, 138) : RGB(226, 232, 240));
    }
}

static void render_bottom_progression_bar(void) {
    int by = 218, bh = 22;
    fill_rect(0, by, SCREEN_W, bh, RGB(17, 24, 39));
    fill_rect(0, by, SCREEN_W, 1, RGB(51, 65, 85));

    /* Loop Status Badge */
    draw_pill(4, by + 4, 48, 15,
              g_loop_playing ? RGB(16, 185, 129) : RGB(30, 41, 59),
              g_loop_playing ? RGB(110, 231, 183) : RGB(71, 85, 105));
    draw_text_centered(FONT_UI_9, 4, by + 4, 48, 15,
                       g_loop_playing ? "LOOP ON" : "X:PROG",
                       RGB(255, 255, 255));

    /* 4 Progression Chord Slots */
    int sx = 56;
    int slot_w = 50;
    for (int i = 0; i < 4; i++) {
        ResolvedChord prc;
        resolve_chord_at(g_key_sector, g_prog[i].sector, g_prog[i].ring, g_prog[i].use_7th, &prc);
        bool active_step = (g_loop_playing && g_loop_step == i);
        bool next_write  = (!g_loop_playing && g_prog_write_idx == i);

        uint32_t s_fill = active_step ? RGB(217, 119, 6) : RGB(26, 36, 56);
        uint32_t s_bdr  = active_step ? RGB(254, 240, 138) :
                          (next_write ? RGB(56, 189, 248) : RGB(64, 82, 115));
        draw_pill(sx, by + 4, slot_w, 15, s_fill, s_bdr);
        draw_text_centered(FONT_UI_9, sx, by + 4, slot_w, 15, prc.chord_name,
                           active_step ? RGB(255, 255, 255) : RGB(226, 232, 240));
        sx += slot_w + 3;
    }

    /* Discrete Y: Help Badge in bottom-right corner */
    draw_pill(SCREEN_W - 48, by + 4, 44, 15, RGB(30, 58, 95), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_9, SCREEN_W - 48, by + 4, 44, 15, "Y: Help", RGB(254, 240, 138));
}

static void render_help_modal(void) {
    int mx = 10, my = 10, mw = SCREEN_W - 20, mh = SCREEN_H - 20;
    fill_rect(mx, my, mw, mh, RGB(15, 23, 38));
    draw_border(mx, my, mw, mh, RGB(96, 165, 250));
    draw_border(mx + 1, my + 1, mw - 2, mh - 2, RGB(37, 99, 235));

    /* Header */
    fill_rect(mx + 2, my + 2, mw - 4, 21, RGB(28, 39, 60));
    fill_rect(mx + 2, my + 23, mw - 4, 1, RGB(64, 82, 115));
    draw_pill(mx + 6, my + 4, 44, 15, RGB(37, 99, 235), RGB(147, 197, 253));
    draw_text_centered(FONT_UI_9, mx + 6, my + 4, 44, 15, "HELP", RGB(255, 255, 255));
    draw_text(FONT_UI_11, mx + 56, my + 5, "CHORDWHEEL CONTROLS & LOOPER", RGB(248, 250, 252));

    /* Progression Preset & Auto-Looper Control Box */
    int py = my + 28;
    fill_rect(mx + 6, py, mw - 12, 42, RGB(23, 33, 52));
    draw_border(mx + 6, py, mw - 12, 42, RGB(56, 189, 248));

    char p_str[80];
    snprintf(p_str, sizeof(p_str), "PRESET (L/R): %s", k_presets[g_preset_idx].name);
    draw_text(FONT_UI_11, mx + 12, py + 5, p_str, RGB(250, 204, 21));
    draw_text(FONT_UI_9, mx + 12, py + 23,
              "Press [A] here to Start/Stop 4-Bar Auto-Strum Looper",
              RGB(125, 211, 252));

    /* Key Mappings List */
    int ky = py + 47;
    static const struct {
        const char *btn;
        const char *desc;
    } k_help_rows[7] = {
        { "LEFT / RIGHT", "Rotate Wheel (4ths / 5ths)" },
        { "UP / DOWN",    "Switch Maj / Min / Dim Ring" },
        { "A Button",     "Strum 6-String Guitar Voicing" },
        { "B Button",     "Toggle Triads <-> 7th Chords" },
        { "L1 / R1",      "Cycle Open / E-Barre / A-Barre" },
        { "SELECT",       "Lock Key (Explore) vs Rotate Key" },
        { "X / MENU",     "X: Add to Loop  |  MENU: Quit" }
    };

    for (int i = 0; i < 7; i++) {
        int ry = ky + i * 18;
        draw_pill(mx + 8, ry, 76, 15, RGB(30, 58, 95), RGB(71, 98, 145));
        draw_text_centered(FONT_UI_9, mx + 8, ry, 76, 15, k_help_rows[i].btn, RGB(254, 240, 138));
        draw_text(FONT_UI_9, mx + 90, ry + 3, k_help_rows[i].desc, RGB(226, 232, 240));
    }

    /* Footer */
    fill_rect(mx + 2, my + mh - 17, mw - 4, 15, RGB(12, 18, 30));
    draw_text_centered(FONT_UI_9, mx, my + mh - 16, mw, 14,
                       "Press Y to return to the Circle of Fifths",
                       RGB(186, 230, 253));
}

static void render_ui(SDL_Surface *screen) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(13, 18, 28));
    render_circle_of_fifths_wheel();
    render_guitar_inspector_pane();
    render_bottom_progression_bar();
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
    SDL_WM_SetCaption("PowKiddy V90 Circle of Fifths (chordwheel)", NULL);

    /* Automated Screenshot Mode (--screenshot-states <dir>) */
    if (argc >= 3 && strcmp(argv[1], "--screenshot-states") == 0) {
        const char *dir = argv[2];
        char path[512];

        /* 1. Key of C Major — Triads, Open C Major Voicing */
        g_key_sector = 0; g_cur_sector = 0; g_cur_ring = 0;
        g_key_locked = false; g_seventh_mode = false; g_voicing_idx = 0;
        g_show_help = false;
        apply_progression_preset(0);
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/chordwheel_main.bmp", dir);
        save_bmp(path);

        /* 2. Key of G Major (Locked) — Exploring ii7 Am7 (7th Chords + 5fr E-Barre Voicing) */
        g_key_sector = 1; g_cur_sector = 0; g_cur_ring = 1;
        g_key_locked = true; g_seventh_mode = true; g_voicing_idx = 1;
        g_show_help = false;
        apply_progression_preset(2);
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/chordwheel_7ths.bmp", dir);
        save_bmp(path);

        /* 3. Key of F Major — 4-Bar Auto-Loop Active on Bb (IV) with A-Shape Barre Voicing */
        g_key_sector = 11; g_cur_sector = 10; g_cur_ring = 0;
        g_key_locked = true; g_seventh_mode = false; g_voicing_idx = 2;
        g_show_help = false;
        apply_progression_preset(3);
        g_loop_playing = true; g_loop_step = 2;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/chordwheel_loop.bmp", dir);
        save_bmp(path);
        g_loop_playing = false;

        /* 4. Help & Progression Looper Modal */
        g_show_help = true;
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/chordwheel_help.bmp", dir);
        save_bmp(path);

        if (audio_ok) SDL_CloseAudio();
        SDL_Quit();
        return 0;
    }

    /* Start background Karplus-Strong synthesis thread & strum initial chord */
    init_wheel_lut();
    start_synth_worker();
    strum_current_selection();

    bool running = true;
    while (running) {
        Uint32 now_ms = SDL_GetTicks();

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

                /* Y (or H on desktop): Toggle Help & Progression Presets modal */
                if (k == V90_BTN_Y || k == SDLK_y || k == SDLK_h) {
                    g_show_help = !g_show_help;
                    continue;
                }

                if (g_show_help) {
                    if (k == V90_BTN_LEFT) {
                        apply_progression_preset(g_preset_idx - 1);
                    } else if (k == V90_BTN_RIGHT) {
                        apply_progression_preset(g_preset_idx + 1);
                    } else if (k == V90_BTN_A || k == SDLK_a) {
                        g_loop_playing = !g_loop_playing;
                        g_loop_step = 0;
                        g_last_loop_ms = now_ms;
                        if (g_loop_playing) {
                            g_cur_sector = g_prog[0].sector;
                            g_cur_ring   = g_prog[0].ring;
                            strum_current_selection();
                        }
                    } else if (k == V90_BTN_B || k == SDLK_b || k == V90_BTN_SELECT) {
                        g_show_help = false;
                    }
                    continue;
                }

                /* Main Wheel View Controls */
                if (k == V90_BTN_RIGHT) {
                    g_loop_playing = false;
                    if (!g_key_locked) {
                        g_key_sector = (g_key_sector + 1) % 12;
                        g_cur_sector = (g_cur_sector + 1) % 12;
                        for (int i = 0; i < 4; i++) {
                            g_prog[i].sector = (g_prog[i].sector + 1) % 12;
                        }
                    } else {
                        g_cur_sector = (g_cur_sector + 1) % 12;
                    }
                    strum_current_selection();
                } else if (k == V90_BTN_LEFT) {
                    g_loop_playing = false;
                    if (!g_key_locked) {
                        g_key_sector = (g_key_sector + 11) % 12;
                        g_cur_sector = (g_cur_sector + 11) % 12;
                        for (int i = 0; i < 4; i++) {
                            g_prog[i].sector = (g_prog[i].sector + 11) % 12;
                        }
                    } else {
                        g_cur_sector = (g_cur_sector + 11) % 12;
                    }
                    strum_current_selection();
                } else if (k == V90_BTN_DOWN) {
                    g_loop_playing = false;
                    if (g_cur_ring < 2) g_cur_ring++;
                    else g_cur_ring = 0;
                    strum_current_selection();
                } else if (k == V90_BTN_UP) {
                    g_loop_playing = false;
                    if (g_cur_ring > 0) g_cur_ring--;
                    else g_cur_ring = 2;
                    strum_current_selection();
                } else if (k == V90_BTN_A || k == SDLK_a) {
                    strum_current_selection();
                } else if (k == V90_BTN_B || k == SDLK_b) {
                    g_seventh_mode = !g_seventh_mode;
                    for (int i = 0; i < 4; i++) {
                        g_prog[i].use_7th = g_seventh_mode;
                    }
                    strum_current_selection();
                } else if (k == V90_BTN_R1
#if defined(HOST_X11)
                           || k == SDLK_BRACKETRIGHT
#endif
                ) {
                    g_voicing_idx = (g_voicing_idx + 1) % 3;
                    strum_current_selection();
                } else if (k == V90_BTN_L1
#if defined(HOST_X11)
                           || k == SDLK_BRACKETLEFT
#endif
                ) {
                    g_voicing_idx = (g_voicing_idx + 2) % 3;
                    strum_current_selection();
                } else if (k == V90_BTN_SELECT
#if defined(HOST_X11)
                           || k == SDLK_HOME || k == SDLK_c
#endif
                ) {
                    g_key_locked = !g_key_locked;
                    if (!g_key_locked) {
                        g_cur_sector = g_key_sector;
                    }
                } else if (k == V90_BTN_X || k == SDLK_x) {
                    g_prog[g_prog_write_idx].sector  = g_cur_sector;
                    g_prog[g_prog_write_idx].ring    = g_cur_ring;
                    g_prog[g_prog_write_idx].use_7th = g_seventh_mode;
                    g_prog_write_idx = (g_prog_write_idx + 1) % 4;
                    strum_current_selection();
                }
#if defined(HOST_X11)
                else if (k == SDLK_p) {
                    g_loop_playing = !g_loop_playing;
                    g_loop_step = 0;
                    g_last_loop_ms = now_ms;
                    if (g_loop_playing) {
                        g_cur_sector = g_prog[0].sector;
                        g_cur_ring   = g_prog[0].ring;
                        strum_current_selection();
                    }
                }
#endif
            }
        }

        /* Advance 4-Chord Progression Auto-Looper (1 chord every 1400 ms) */
        if (g_loop_playing && (now_ms - g_last_loop_ms >= 1400)) {
            g_last_loop_ms = now_ms;
            g_loop_step = (g_loop_step + 1) % 4;
            g_cur_sector   = g_prog[g_loop_step].sector;
            g_cur_ring     = g_prog[g_loop_step].ring;
            g_seventh_mode = g_prog[g_loop_step].use_7th;
            strum_current_selection();
        }

        render_ui(screen);
        SDL_Delay(16);
    }

    stop_synth_worker();
    save_config();
    if (audio_ok) {
        SDL_CloseAudio();
    }
    SDL_Quit();
    return 0;
}
