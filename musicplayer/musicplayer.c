/*
 * musicplayer.c - PowKiddy V90 Directory Music Player (MP3 / OGG / FLAC)
 *
 * Designed for the PowKiddy V90 (MiyooCFW 1.3.3, 320x240 16-bit RGB565,
 * Allwinner F1C100S ARM926EJ-S) and Linux Desktop (x86_64 X11).
 *
 * Key Features:
 *   1. Native decoding of .mp3 (libmpg123), .ogg (libvorbisfile), and .flac
 *      (libFLAC) on a dedicated background decoder thread.
 *   2. Opens to a Directory Browser in /mnt/media with a top highlighted
 *      [RESUME] entry when a previous session was saved.
 *   3. Selecting a directory plays all .mp3/.ogg/.flac files in filename order.
 *   4. In Now Playing + Playlist view:
 *        - A: Play / Pause current track
 *        - UP / DOWN: Highlight songs in the playlist
 *        - B: Select & play the highlighted song
 *        - L1 / R1: Seek backward / forward (Hold to Rewind / Fast-Forward,
 *          works in both Player view and Directory Browser view)
 *        - SELECT / X: Toggle between Directory Browser and Player view
 *        - Y: Toggle Help & Controls Guide overlay
 *        - MENU: Save playlist & play position and exit cleanly
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#include <mpg123.h>
#include <vorbis/vorbisfile.h>
#include <FLAC/stream_decoder.h>
#include <FLAC/metadata.h>

#define SCREEN_W        320
#define SCREEN_H        240
#define SCREEN_BPP      16
#define AUDIO_RATE      44100
#define AUDIO_CHANNELS  2
#define AUDIO_SAMPLES   1024

/* =========================================================================
 * Host X11 Compatibility Layer (for native desktop testing without SDL)
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
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int16_t  Sint16;

typedef struct {
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
} SDL_PixelFormat;

typedef struct {
    int w, h;
    int pitch;
    void *pixels;
    SDL_PixelFormat *format;
} SDL_Surface;

typedef int SDLKey;
enum {
    SDLK_UNKNOWN      = 0,
    SDLK_BACKSPACE    = XK_BackSpace,
    SDLK_TAB          = XK_Tab,
    SDLK_RETURN       = XK_Return,
    SDLK_ESCAPE       = XK_Escape,
    SDLK_SPACE        = XK_space,
    SDLK_LEFTBRACKET  = XK_bracketleft,
    SDLK_RIGHTBRACKET = XK_bracketright,
    SDLK_a            = XK_a,
    SDLK_b            = XK_b,
    SDLK_c            = XK_c,
    SDLK_e            = XK_e,
    SDLK_h            = XK_h,
    SDLK_m            = XK_m,
    SDLK_p            = XK_p,
    SDLK_q            = XK_q,
    SDLK_s            = XK_s,
    SDLK_x            = XK_x,
    SDLK_y            = XK_y,
    SDLK_UP           = XK_Up,
    SDLK_DOWN         = XK_Down,
    SDLK_LEFT         = XK_Left,
    SDLK_RIGHT        = XK_Right,
    SDLK_PAGEUP       = XK_Page_Up,
    SDLK_PAGEDOWN     = XK_Page_Down,
    SDLK_LSHIFT       = XK_Shift_L,
    SDLK_RCTRL        = XK_Control_R,
    SDLK_LCTRL        = XK_Control_L,
    SDLK_LALT         = XK_Alt_L
};

enum {
    SDL_QUIT = 1,
    SDL_KEYDOWN = 2,
    SDL_KEYUP = 3
};

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

typedef pa_simple* (*pa_simple_new_fn)(const char*, const char*, int, const char*, const char*, const pa_sample_spec*, const void*, const void*, int*);
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
            pa = fn_new(NULL, "V90 MusicPlayer", PA_STREAM_PLAYBACK, NULL, "MP3/OGG/FLAC Player", &ss, NULL, NULL, &err);
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
            fn_write(pa, buf, (size_t)chunk_bytes, &err);
        } else {
            usleep((useconds_t)((chunk_frames * 1000000ULL) / (unsigned)g_audio_spec.freq));
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
    hints.min_width  = hints.max_width  = win_w;
    hints.min_height = hints.max_height = win_h;
    XSetWMNormalHints(g_x11_dpy, g_x11_win, &hints);
    XStoreName(g_x11_dpy, g_x11_win, "PowKiddy V90 - Music Player (MP3 / OGG / FLAC)");

    g_wm_delete_window = XInternAtom(g_x11_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_x11_dpy, g_x11_win, &g_wm_delete_window, 1);
    XSelectInput(g_x11_dpy, g_x11_win, ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask);
    XMapWindow(g_x11_dpy, g_x11_win);

    g_x11_gc = DefaultGC(g_x11_dpy, scr);
    Visual *vis = DefaultVisual(g_x11_dpy, scr);
    int depth = DefaultDepth(g_x11_dpy, scr);

    g_scaled_buf = (char *)calloc((size_t)(win_w * win_h), 4);
    g_x11_img = XCreateImage(g_x11_dpy, vis, (unsigned int)depth, ZPixmap, 0, g_scaled_buf,
                             (unsigned int)win_w, (unsigned int)win_h, 32, 0);

    g_dummy_fmt.BitsPerPixel = 16;
    g_dummy_fmt.BytesPerPixel = 2;
    g_x11_surface.w = width;
    g_x11_surface.h = height;
    g_x11_surface.pitch = width * 2;
    g_x11_surface.pixels = calloc((size_t)(width * height), 2);
    g_x11_surface.format = &g_dummy_fmt;

    XFlush(g_x11_dpy);
    return &g_x11_surface;
}

static void SDL_Flip(SDL_Surface *s) {
    if (!g_x11_dpy || !g_x11_img) return;
    const uint16_t *src = (const uint16_t *)s->pixels;
    uint32_t *dst = (uint32_t *)g_scaled_buf;
    int win_w = s->w * HOST_SCALE;

    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            uint16_t c = src[y * s->w + x];
            uint32_t r = ((c >> 11) & 0x1F) * 255 / 31;
            uint32_t g = ((c >> 5)  & 0x3F) * 255 / 63;
            uint32_t b = (c         & 0x1F) * 255 / 31;
            uint32_t argb = (0xFFu << 24) | (r << 16) | (g << 8) | b;
            for (int dy = 0; dy < HOST_SCALE; dy++) {
                for (int dx = 0; dx < HOST_SCALE; dx++) {
                    dst[(y * HOST_SCALE + dy) * win_w + (x * HOST_SCALE + dx)] = argb;
                }
            }
        }
    }
    XPutImage(g_x11_dpy, g_x11_win, g_x11_gc, g_x11_img, 0, 0, 0, 0,
              (unsigned int)(s->w * HOST_SCALE), (unsigned int)(s->h * HOST_SCALE));
    XFlush(g_x11_dpy);
}

static int SDL_PollEvent(SDL_Event *ev) {
    if (!g_x11_dpy) return 0;
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
#define COL_ROW_SEL     rgb565(38, 52, 76)
#define COL_BORDER      rgb565(55, 68, 92)
#define COL_WHITE       rgb565(242, 246, 250)
#define COL_MUTED       rgb565(140, 155, 175)
#define COL_DIM         rgb565(82, 96, 120)
#define COL_ACCENT      rgb565(235, 87, 87)
#define COL_GOLD        rgb565(241, 196, 15)
#define COL_CYAN        rgb565(52, 152, 219)
#define COL_GREEN       rgb565(46, 204, 113)
#define COL_ORANGE      rgb565(242, 153, 74)
#define COL_PURPLE      rgb565(155, 89, 182)

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
        for (int px = x0; px < x1; px++) {
            row[px] = col;
        }
    }
}

static void fill_rect_alpha(SDL_Surface *s, int x, int y, int w, int h, uint16_t col, uint8_t alpha) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > SCREEN_W) ? SCREEN_W : (x + w);
    int y1 = (y + h > SCREEN_H) ? SCREEN_H : (y + h);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            blend_pixel(s, px, py, col, alpha);
        }
    }
}

static void draw_border(SDL_Surface *s, int x, int y, int w, int h, int t, uint16_t col) {
    fill_rect(s, x, y, w, t, col);
    fill_rect(s, x, y + h - t, w, t, col);
    fill_rect(s, x, y, t, h, col);
    fill_rect(s, x + w - t, y, t, h, col);
}

#include "musicplayer_font.h"

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

static void draw_text_clipped(SDL_Surface *s, int font_id, int x, int y, int max_w, const char *str, uint16_t col) {
    if (!str || max_w <= 0) return;
    if (text_width(font_id, str) <= max_w) {
        draw_text(s, font_id, x, y, str, col);
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", str);
    int len = (int)strlen(buf);
    while (len > 3) {
        buf[len - 3] = '.';
        buf[len - 2] = '.';
        buf[len - 1] = '\0';
        if (text_width(font_id, buf) <= max_w) break;
        len--;
    }
    draw_text(s, font_id, x, y, buf, col);
}

static void draw_path_tail(SDL_Surface *s, int font_id, int x, int y, int max_w, const char *path, uint16_t col) {
    if (!path || max_w <= 0) return;
    if (text_width(font_id, path) <= max_w) {
        draw_text(s, font_id, x, y, path, col);
        return;
    }
    size_t len = strlen(path);
    for (size_t off = 1; off < len; off++) {
        char buf[300];
        snprintf(buf, sizeof(buf), "..%s", path + off);
        if (text_width(font_id, buf) <= max_w) {
            draw_text(s, font_id, x, y, buf, col);
            return;
        }
    }
}

/* =========================================================================
 * Audio Formats, Ring Buffer & Background Decoder Thread
 * (libmpg123 + libvorbisfile + libFLAC)
 * ========================================================================= */
typedef enum {
    FMT_NONE = 0,
    FMT_MP3  = 1,
    FMT_OGG  = 2,
    FMT_FLAC = 3
} AudioFormat;

static AudioFormat detect_audio_format(const char *filename) {
    if (!filename) return FMT_NONE;
    const char *dot = strrchr(filename, '.');
    if (!dot) return FMT_NONE;
    if (strcasecmp(dot, ".mp3") == 0)  return FMT_MP3;
    if (strcasecmp(dot, ".ogg") == 0)  return FMT_OGG;
    if (strcasecmp(dot, ".flac") == 0) return FMT_FLAC;
    return FMT_NONE;
}

static const char *format_badge_str(AudioFormat fmt) {
    switch (fmt) {
        case FMT_MP3:  return "MP3";
        case FMT_OGG:  return "OGG";
        case FMT_FLAC: return "FLAC";
        default:       return "---";
    }
}

static uint16_t format_badge_col(AudioFormat fmt) {
    switch (fmt) {
        case FMT_MP3:  return COL_ORANGE;
        case FMT_OGG:  return COL_PURPLE;
        case FMT_FLAC: return COL_CYAN;
        default:       return COL_MUTED;
    }
}

/* Lock-free single-producer / single-consumer stereo PCM ring buffer (44.1 kHz S16LE) */
#define PCM_RING_FRAMES 65536
#define PCM_RING_MASK   (PCM_RING_FRAMES - 1)

static int16_t       g_pcm_ring[PCM_RING_FRAMES * 2];
static atomic_uint   g_ring_write_idx = 0;
static atomic_uint   g_ring_read_idx  = 0;

static inline uint32_t ring_available_to_read(void) {
    uint32_t w = atomic_load_explicit(&g_ring_write_idx, memory_order_acquire);
    uint32_t r = atomic_load_explicit(&g_ring_read_idx, memory_order_relaxed);
    return w - r;
}

static inline uint32_t ring_available_to_write(void) {
    uint32_t w = atomic_load_explicit(&g_ring_write_idx, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&g_ring_read_idx, memory_order_acquire);
    return PCM_RING_FRAMES - (w - r);
}

static inline void ring_clear(void) {
    uint32_t w = atomic_load_explicit(&g_ring_write_idx, memory_order_relaxed);
    atomic_store_explicit(&g_ring_read_idx, w, memory_order_release);
}

/* Decoder commands & telemetry shared between UI thread and Decoder thread */
static pthread_mutex_t g_dec_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t       g_dec_thread;

static volatile bool   g_dec_thread_running = false;
static volatile bool   g_dec_cmd_open       = false;
static char            g_dec_req_path[PATH_MAX];
static int64_t         g_dec_req_start_ms   = 0;
static volatile bool   g_dec_cmd_seek       = false;
static int64_t         g_dec_req_seek_delta = 0;

static atomic_bool     g_playing            = false;
static atomic_bool     g_track_loaded       = false;
static atomic_bool     g_track_eof          = false;
static atomic_int      g_dec_pos_ms         = 0;
static atomic_int      g_dec_dur_ms         = 0;
static atomic_int      g_dec_src_rate       = 44100;
static atomic_int      g_dec_src_channels   = 2;
static atomic_int      g_dec_fmt            = FMT_NONE;

static char            g_meta_title[128]    = "";
static char            g_meta_artist[128]   = "";
static pthread_mutex_t g_meta_mutex         = PTHREAD_MUTEX_INITIALIZER;

/* Audio output callback: pulls decoded 44.1 kHz S16LE stereo frames from ring buffer */
static uint64_t g_played_out_frames = 0;

static void audio_output_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int frames_needed = len / (AUDIO_CHANNELS * (int)sizeof(int16_t));

    if (!atomic_load(&g_playing) || !atomic_load(&g_track_loaded)) {
        memset(stream, 0, (size_t)len);
        return;
    }

    uint32_t avail = ring_available_to_read();
    uint32_t to_read = ((uint32_t)frames_needed < avail) ? (uint32_t)frames_needed : avail;
    uint32_t r = atomic_load_explicit(&g_ring_read_idx, memory_order_relaxed);

    for (uint32_t i = 0; i < to_read; i++) {
        uint32_t idx = (r + i) & PCM_RING_MASK;
        out[i * 2]     = g_pcm_ring[idx * 2];
        out[i * 2 + 1] = g_pcm_ring[idx * 2 + 1];
    }
    atomic_store_explicit(&g_ring_read_idx, r + to_read, memory_order_release);

    if (to_read < (uint32_t)frames_needed) {
        memset(&out[to_read * 2], 0, (size_t)((frames_needed - (int)to_read) * 2 * (int)sizeof(int16_t)));
    }

    if (to_read > 0) {
        g_played_out_frames += to_read;
        int pos = (int)((g_played_out_frames * 1000ULL) / AUDIO_RATE);
        int dur = atomic_load(&g_dec_dur_ms);
        if (dur > 0 && pos > dur) pos = dur;
        atomic_store(&g_dec_pos_ms, pos);
    }
}

/* Simple integer resampler + stereo converter pushing frames into the 44.1 kHz ring buffer */
static uint32_t g_resamp_frac = 0;

static void push_decoded_pcm_to_ring(const int16_t *src, int src_frames, int src_rate, int src_ch) {
    if (src_frames <= 0 || src_rate <= 0) return;

    uint32_t step_fp = (uint32_t)(((uint64_t)src_rate << 16) / AUDIO_RATE);
    int src_idx = 0;

    while (src_idx < src_frames && g_dec_thread_running && !g_dec_cmd_open && !g_dec_cmd_seek) {
        if (ring_available_to_write() < 4) {
            usleep(2000);
            continue;
        }
        int16_t l = 0, r = 0;
        if (src_ch >= 2) {
            l = src[src_idx * src_ch];
            r = src[src_idx * src_ch + 1];
        } else {
            l = r = src[src_idx];
        }

        uint32_t w = atomic_load_explicit(&g_ring_write_idx, memory_order_relaxed);
        uint32_t slot = w & PCM_RING_MASK;
        g_pcm_ring[slot * 2]     = l;
        g_pcm_ring[slot * 2 + 1] = r;
        atomic_store_explicit(&g_ring_write_idx, w + 1, memory_order_release);

        if (src_rate == AUDIO_RATE) {
            src_idx++;
        } else {
            g_resamp_frac += step_fp;
            src_idx += (int)(g_resamp_frac >> 16);
            g_resamp_frac &= 0xFFFFu;
        }
    }
}

/* FLAC Decoder Context & Callbacks */
typedef struct {
    int      sample_rate;
    int      channels;
    int      bits_per_sample;
    uint64_t total_samples;
    bool     eof_reached;
    int16_t  staging[4096 * 2];
} FlacCtx;

static FLAC__StreamDecoderWriteStatus flac_write_cb(
    const FLAC__StreamDecoder *decoder,
    const FLAC__Frame *frame,
    const FLAC__int32 * const buffer[],
    void *client_data)
{
    (void)decoder;
    FlacCtx *fc = (FlacCtx *)client_data;
    int blocksize = (int)frame->header.blocksize;
    int ch = (int)frame->header.channels;
    int bps = (int)frame->header.bits_per_sample;
    fc->sample_rate = (int)frame->header.sample_rate;
    fc->channels = ch;
    fc->bits_per_sample = bps;

    int offset = 0;
    while (offset < blocksize) {
        int chunk = blocksize - offset;
        if (chunk > 2048) chunk = 2048;
        for (int i = 0; i < chunk; i++) {
            FLAC__int32 sL = buffer[0][offset + i];
            FLAC__int32 sR = (ch >= 2 && buffer[1]) ? buffer[1][offset + i] : sL;
            int16_t outL, outR;
            if (bps > 16) {
                outL = (int16_t)(sL >> (bps - 16));
                outR = (int16_t)(sR >> (bps - 16));
            } else if (bps < 16) {
                outL = (int16_t)(sL << (16 - bps));
                outR = (int16_t)(sR << (16 - bps));
            } else {
                outL = (int16_t)sL;
                outR = (int16_t)sR;
            }
            fc->staging[i * 2]     = outL;
            fc->staging[i * 2 + 1] = outR;
        }
        push_decoded_pcm_to_ring(fc->staging, chunk, fc->sample_rate, 2);
        offset += chunk;
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flac_metadata_cb(
    const FLAC__StreamDecoder *decoder,
    const FLAC__StreamMetadata *metadata,
    void *client_data)
{
    (void)decoder;
    FlacCtx *fc = (FlacCtx *)client_data;
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        fc->sample_rate     = (int)metadata->data.stream_info.sample_rate;
        fc->channels        = (int)metadata->data.stream_info.channels;
        fc->bits_per_sample = (int)metadata->data.stream_info.bits_per_sample;
        fc->total_samples   = metadata->data.stream_info.total_samples;
        if (fc->sample_rate > 0 && fc->total_samples > 0) {
            int dur_ms = (int)((fc->total_samples * 1000ULL) / (unsigned)fc->sample_rate);
            atomic_store(&g_dec_dur_ms, dur_ms);
        }
        atomic_store(&g_dec_src_rate, fc->sample_rate);
        atomic_store(&g_dec_src_channels, fc->channels);
    } else if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
        pthread_mutex_lock(&g_meta_mutex);
        for (uint32_t i = 0; i < metadata->data.vorbis_comment.num_comments; i++) {
            const char *entry = (const char *)metadata->data.vorbis_comment.comments[i].entry;
            if (!entry) continue;
            if (strncasecmp(entry, "TITLE=", 6) == 0) {
                snprintf(g_meta_title, sizeof(g_meta_title), "%s", entry + 6);
            } else if (strncasecmp(entry, "ARTIST=", 7) == 0) {
                snprintf(g_meta_artist, sizeof(g_meta_artist), "%s", entry + 7);
            }
        }
        pthread_mutex_unlock(&g_meta_mutex);
    }
}

static void flac_error_cb(
    const FLAC__StreamDecoder *decoder,
    FLAC__StreamDecoderErrorStatus status,
    void *client_data)
{
    (void)decoder; (void)status; (void)client_data;
}

/* Dedicated Background Decoder Worker Thread */
static void *decoder_thread_fn(void *arg) {
    (void)arg;
    mpg123_handle *mh = NULL;
    OggVorbis_File vf;
    bool vf_open = false;
    FLAC__StreamDecoder *flac_dec = NULL;
    FlacCtx flac_ctx;
    AudioFormat active_fmt = FMT_NONE;
    bool local_eof = false;

    int16_t decode_buf[2048 * 2];

    while (g_dec_thread_running) {
        bool do_open = false;
        char open_path[PATH_MAX];
        int64_t start_ms = 0;

        bool do_seek = false;
        int64_t seek_delta_ms = 0;

        pthread_mutex_lock(&g_dec_mutex);
        if (g_dec_cmd_open) {
            do_open = true;
            snprintf(open_path, sizeof(open_path), "%s", g_dec_req_path);
            start_ms = g_dec_req_start_ms;
            g_dec_cmd_open = false;
            g_dec_cmd_seek = false;
        } else if (g_dec_cmd_seek) {
            do_seek = true;
            seek_delta_ms = g_dec_req_seek_delta;
            g_dec_cmd_seek = false;
        }
        pthread_mutex_unlock(&g_dec_mutex);

        /* 1. Handle Track Open Request */
        if (do_open) {
            ring_clear();
            g_resamp_frac = 0;
            local_eof = false;
            atomic_store(&g_track_eof, false);

            if (mh) {
                mpg123_close(mh);
                mpg123_delete(mh);
                mh = NULL;
            }
            if (vf_open) {
                ov_clear(&vf);
                vf_open = false;
            }
            if (flac_dec) {
                FLAC__stream_decoder_finish(flac_dec);
                FLAC__stream_decoder_delete(flac_dec);
                flac_dec = NULL;
            }

            pthread_mutex_lock(&g_meta_mutex);
            g_meta_title[0]  = '\0';
            g_meta_artist[0] = '\0';
            pthread_mutex_unlock(&g_meta_mutex);

            active_fmt = detect_audio_format(open_path);
            atomic_store(&g_dec_fmt, active_fmt);
            bool opened_ok = false;

            if (active_fmt == FMT_MP3) {
                int err = 0;
                mh = mpg123_new(NULL, &err);
                if (mh && mpg123_open(mh, open_path) == MPG123_OK) {
                    long rate = 44100;
                    int channels = 2, encoding = 0;
                    mpg123_getformat(mh, &rate, &channels, &encoding);
                    mpg123_format_none(mh);
                    mpg123_format(mh, rate, MPG123_STEREO, MPG123_ENC_SIGNED_16);
                    mpg123_scan(mh);

                    off_t len_samples = mpg123_length(mh);
                    int dur_ms = (len_samples > 0 && rate > 0)
                                 ? (int)(((uint64_t)len_samples * 1000ULL) / (uint64_t)rate)
                                 : 0;
                    atomic_store(&g_dec_src_rate, (int)rate);
                    atomic_store(&g_dec_src_channels, channels);
                    atomic_store(&g_dec_dur_ms, dur_ms);

                    mpg123_id3v1 *v1 = NULL;
                    mpg123_id3v2 *v2 = NULL;
                    if (mpg123_id3(mh, &v1, &v2) == MPG123_OK) {
                        pthread_mutex_lock(&g_meta_mutex);
                        if (v2 && v2->title && v2->title->p) {
                            snprintf(g_meta_title, sizeof(g_meta_title), "%s", v2->title->p);
                        } else if (v1 && v1->title[0]) {
                            snprintf(g_meta_title, sizeof(g_meta_title), "%.30s", v1->title);
                        }
                        if (v2 && v2->artist && v2->artist->p) {
                            snprintf(g_meta_artist, sizeof(g_meta_artist), "%s", v2->artist->p);
                        } else if (v1 && v1->artist[0]) {
                            snprintf(g_meta_artist, sizeof(g_meta_artist), "%.30s", v1->artist);
                        }
                        pthread_mutex_unlock(&g_meta_mutex);
                    }

                    if (start_ms > 0 && rate > 0) {
                        off_t target_sample = (off_t)((start_ms * rate) / 1000);
                        mpg123_seek(mh, target_sample, SEEK_SET);
                    }
                    opened_ok = true;
                }
            } else if (active_fmt == FMT_OGG) {
                if (ov_fopen(open_path, &vf) == 0) {
                    vf_open = true;
                    vorbis_info *vi = ov_info(&vf, -1);
                    int rate = vi ? (int)vi->rate : 44100;
                    int channels = vi ? vi->channels : 2;
                    double total_sec = ov_time_total(&vf, -1);
                    int dur_ms = (total_sec > 0.0) ? (int)(total_sec * 1000.0) : 0;

                    atomic_store(&g_dec_src_rate, rate);
                    atomic_store(&g_dec_src_channels, channels);
                    atomic_store(&g_dec_dur_ms, dur_ms);

                    vorbis_comment *vc = ov_comment(&vf, -1);
                    if (vc) {
                        pthread_mutex_lock(&g_meta_mutex);
                        for (int i = 0; i < vc->comments; i++) {
                            const char *c = vc->user_comments[i];
                            if (!c) continue;
                            if (strncasecmp(c, "TITLE=", 6) == 0) {
                                snprintf(g_meta_title, sizeof(g_meta_title), "%s", c + 6);
                            } else if (strncasecmp(c, "ARTIST=", 7) == 0) {
                                snprintf(g_meta_artist, sizeof(g_meta_artist), "%s", c + 7);
                            }
                        }
                        pthread_mutex_unlock(&g_meta_mutex);
                    }

                    if (start_ms > 0 && ov_seekable(&vf)) {
                        ov_time_seek(&vf, (double)start_ms / 1000.0);
                    }
                    opened_ok = true;
                }
            } else if (active_fmt == FMT_FLAC) {
                memset(&flac_ctx, 0, sizeof(flac_ctx));
                flac_ctx.sample_rate = 44100;
                flac_ctx.channels = 2;
                flac_ctx.bits_per_sample = 16;

                flac_dec = FLAC__stream_decoder_new();
                if (flac_dec) {
                    FLAC__stream_decoder_set_metadata_respond(flac_dec, FLAC__METADATA_TYPE_STREAMINFO);
                    FLAC__stream_decoder_set_metadata_respond(flac_dec, FLAC__METADATA_TYPE_VORBIS_COMMENT);
                    if (FLAC__stream_decoder_init_file(
                            flac_dec, open_path,
                            flac_write_cb, flac_metadata_cb, flac_error_cb,
                            &flac_ctx) == FLAC__STREAM_DECODER_INIT_STATUS_OK) {
                        FLAC__stream_decoder_process_until_end_of_metadata(flac_dec);
                        if (start_ms > 0 && flac_ctx.sample_rate > 0) {
                            uint64_t target_sample = ((uint64_t)start_ms * (uint64_t)flac_ctx.sample_rate) / 1000ULL;
                            if (flac_ctx.total_samples > 0 && target_sample >= flac_ctx.total_samples) {
                                target_sample = 0;
                                start_ms = 0;
                            }
                            FLAC__stream_decoder_seek_absolute(flac_dec, (FLAC__uint64)target_sample);
                        }
                        opened_ok = true;
                    }
                }
            }

            g_played_out_frames = ((uint64_t)start_ms * AUDIO_RATE) / 1000ULL;
            atomic_store(&g_dec_pos_ms, (int)start_ms);
            atomic_store(&g_track_loaded, opened_ok);
            continue;
        }

        /* 2. Handle Seek Request (L1 / R1 Tap or Hold) */
        if (do_seek && atomic_load(&g_track_loaded)) {
            int cur_ms = atomic_load(&g_dec_pos_ms);
            int dur_ms = atomic_load(&g_dec_dur_ms);
            int64_t target_ms = (int64_t)cur_ms + seek_delta_ms;
            if (target_ms < 0) target_ms = 0;
            if (dur_ms > 500 && target_ms > dur_ms - 500) target_ms = dur_ms - 500;

            ring_clear();
            g_resamp_frac = 0;
            local_eof = false;
            atomic_store(&g_track_eof, false);

            int rate = atomic_load(&g_dec_src_rate);
            if (rate <= 0) rate = 44100;

            if (active_fmt == FMT_MP3 && mh) {
                off_t target_sample = (off_t)((target_ms * rate) / 1000);
                mpg123_seek(mh, target_sample, SEEK_SET);
            } else if (active_fmt == FMT_OGG && vf_open) {
                if (ov_seekable(&vf)) {
                    ov_time_seek(&vf, (double)target_ms / 1000.0);
                }
            } else if (active_fmt == FMT_FLAC && flac_dec) {
                uint64_t target_sample = ((uint64_t)target_ms * (uint64_t)rate) / 1000ULL;
                FLAC__stream_decoder_seek_absolute(flac_dec, (FLAC__uint64)target_sample);
            }

            g_played_out_frames = ((uint64_t)target_ms * AUDIO_RATE) / 1000ULL;
            atomic_store(&g_dec_pos_ms, (int)target_ms);
            continue;
        }

        /* 3. Decode Next Chunk into Ring Buffer if Track is Loaded */
        if (!atomic_load(&g_track_loaded) || local_eof) {
            if (local_eof && ring_available_to_read() == 0) {
                atomic_store(&g_track_eof, true);
            }
            usleep(10000);
            continue;
        }

        if (ring_available_to_write() < 4096) {
            usleep(4000);
            continue;
        }

        int src_rate = atomic_load(&g_dec_src_rate);
        if (active_fmt == FMT_MP3 && mh) {
            size_t done = 0;
            int ret = mpg123_read(mh, (unsigned char *)decode_buf, sizeof(decode_buf), &done);
            if (done > 0) {
                int frames = (int)(done / (2 * sizeof(int16_t)));
                push_decoded_pcm_to_ring(decode_buf, frames, src_rate, 2);
            }
            if (ret == MPG123_DONE || (ret != MPG123_OK && ret != MPG123_NEW_FORMAT && done == 0)) {
                local_eof = true;
            }
        } else if (active_fmt == FMT_OGG && vf_open) {
            int bitstream = 0;
            long bytes = ov_read(&vf, (char *)decode_buf, (int)sizeof(decode_buf), 0, 2, 1, &bitstream);
            if (bytes > 0) {
                vorbis_info *vi = ov_info(&vf, -1);
                int ch = vi ? vi->channels : 2;
                int rate = vi ? (int)vi->rate : src_rate;
                int frames = (int)(bytes / (ch * (int)sizeof(int16_t)));
                push_decoded_pcm_to_ring(decode_buf, frames, rate, ch);
            } else if (bytes == 0) {
                local_eof = true;
            }
        } else if (active_fmt == FMT_FLAC && flac_dec) {
            FLAC__StreamDecoderState st = FLAC__stream_decoder_get_state(flac_dec);
            if (st == FLAC__STREAM_DECODER_END_OF_STREAM ||
                !FLAC__stream_decoder_process_single(flac_dec)) {
                local_eof = true;
            }
        } else {
            usleep(10000);
        }
    }

    if (mh) {
        mpg123_close(mh);
        mpg123_delete(mh);
    }
    if (vf_open) {
        ov_clear(&vf);
    }
    if (flac_dec) {
        FLAC__stream_decoder_finish(flac_dec);
        FLAC__stream_decoder_delete(flac_dec);
    }
    return NULL;
}

static void request_decoder_open(const char *path, int64_t start_ms, bool auto_play) {
    pthread_mutex_lock(&g_dec_mutex);
    snprintf(g_dec_req_path, sizeof(g_dec_req_path), "%s", path);
    g_dec_req_start_ms = start_ms;
    g_dec_cmd_open = true;
    g_dec_cmd_seek = false;
    ring_clear();
    atomic_store(&g_track_loaded, false);
    atomic_store(&g_track_eof, false);
    atomic_store(&g_dec_pos_ms, (int)start_ms);
    atomic_store(&g_playing, auto_play);
    pthread_mutex_unlock(&g_dec_mutex);
}

static void request_decoder_seek(int64_t delta_ms) {
    if (!atomic_load(&g_track_loaded)) return;
    pthread_mutex_lock(&g_dec_mutex);
    if (g_dec_cmd_seek) {
        g_dec_req_seek_delta += delta_ms;
    } else {
        g_dec_req_seek_delta = delta_ms;
        g_dec_cmd_seek = true;
    }
    pthread_mutex_unlock(&g_dec_mutex);
}

/* =========================================================================
 * Directory Browser & Playlist Management
 * ========================================================================= */
#define MAX_PLAYLIST_FILES 256
#define MAX_BROWSER_ITEMS  256
#define MAX_FILENAME_LEN   260

typedef enum {
    VIEW_BROWSER = 0,
    VIEW_PLAYER  = 1
} AppView;

typedef enum {
    BITEM_RESUME   = 0,
    BITEM_PLAY_DIR = 1,
    BITEM_PARENT   = 2,
    BITEM_SUBDIR   = 3,
    BITEM_AUDIO    = 4
} BrowserItemType;

typedef struct {
    BrowserItemType type;
    char            name[MAX_FILENAME_LEN];
    char            full_path[PATH_MAX];
    int             track_count;
    AudioFormat     fmt;
} BrowserItem;

/* Active Playlist State */
static char        g_playlist_dir[PATH_MAX] = "";
static int         g_playlist_count         = 0;
static char        g_playlist_files[MAX_PLAYLIST_FILES][MAX_FILENAME_LEN];
static int         g_playing_idx            = 0; /* Song currently playing */
static int         g_cursor_idx             = 0; /* Song highlighted with UP/DOWN */
static int         g_playlist_scroll        = 0;

/* Saved Session State (loaded from musicplayer.cfg on startup for [RESUME]) */
static bool        g_has_saved_session      = false;
static char        g_saved_dir[PATH_MAX]    = "";
static int         g_saved_playing_idx      = 0;
static int         g_saved_cursor_idx       = 0;
static int         g_saved_pos_ms           = 0;
static int         g_saved_count            = 0;
static char        g_saved_files[MAX_PLAYLIST_FILES][MAX_FILENAME_LEN];

/* Directory Browser State */
static char        g_browser_root[PATH_MAX] = "/mnt/media";
static char        g_browser_dir[PATH_MAX]  = "/mnt/media";
static BrowserItem g_browser_items[MAX_BROWSER_ITEMS];
static int         g_browser_count          = 0;
static int         g_browser_cursor         = 0;
static int         g_browser_scroll         = 0;

static AppView     g_view                   = VIEW_BROWSER;
static bool        g_show_help              = false;
static char        g_config_path[PATH_MAX]  = "";

static int cmp_filename_asc(const void *a, const void *b) {
    const char *sa = (const char *)a;
    const char *sb = (const char *)b;
    return strcasecmp(sa, sb);
}

static int cmp_browser_item(const void *a, const void *b) {
    const BrowserItem *ia = (const BrowserItem *)a;
    const BrowserItem *ib = (const BrowserItem *)b;
    if (ia->type != ib->type) return (int)ia->type - (int)ib->type;
    return strcasecmp(ia->name, ib->name);
}

/* Count supported audio files (.mp3, .ogg, .flac) inside a directory */
static int count_audio_files_in_dir(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (detect_audio_format(de->d_name) != FMT_NONE) {
            count++;
        }
    }
    closedir(d);
    return count;
}

/* Remove the [RESUME] notice from the Directory Browser once resume or any fresh playback starts */
static void dismiss_resume_notice(void) {
    g_has_saved_session = false;
    if (g_browser_count > 0 && g_browser_items[0].type == BITEM_RESUME) {
        if (g_browser_count > 1) {
            memmove(&g_browser_items[0], &g_browser_items[1],
                    (size_t)(g_browser_count - 1) * sizeof(BrowserItem));
        }
        g_browser_count--;
        if (g_browser_cursor > 0) g_browser_cursor--;
        if (g_browser_cursor >= g_browser_count) {
            g_browser_cursor = (g_browser_count > 0) ? (g_browser_count - 1) : 0;
        }
    }
}

/* Populate the Directory Browser items for `dir_path` */
static void scan_browser_directory(const char *dir_path) {
    char resolved[PATH_MAX];
    if (realpath(dir_path, resolved) != NULL) {
        snprintf(g_browser_dir, sizeof(g_browser_dir), "%s", resolved);
    } else {
        snprintf(g_browser_dir, sizeof(g_browser_dir), "%s", dir_path);
    }
    /* Strip trailing slash unless root "/" */
    size_t dlen = strlen(g_browser_dir);
    while (dlen > 1 && g_browser_dir[dlen - 1] == '/') {
        g_browser_dir[--dlen] = '\0';
    }

    g_browser_count = 0;

    /* 1. Top Highlighted Entry: [RESUME] if a valid saved session exists and hasn't been used/dismissed */
    if (g_has_saved_session && g_saved_count > 0 &&
        g_saved_playing_idx >= 0 && g_saved_playing_idx < g_saved_count) {
        BrowserItem *it = &g_browser_items[g_browser_count++];
        memset(it, 0, sizeof(*it));
        it->type = BITEM_RESUME;
        const char *slash = strrchr(g_saved_dir, '/');
        const char *folder_short = (slash && slash[1]) ? (slash + 1) : g_saved_dir;
        snprintf(it->name, sizeof(it->name), "%.96s / %.150s",
                 folder_short, g_saved_files[g_saved_playing_idx]);
        snprintf(it->full_path, sizeof(it->full_path), "%s", g_saved_dir);
        it->track_count = g_saved_pos_ms / 1000;
    }

    /* 2. Count direct audio files in `g_browser_dir` */
    int direct_audio_count = count_audio_files_in_dir(g_browser_dir);
    if (direct_audio_count > 0 && g_browser_count < MAX_BROWSER_ITEMS) {
        BrowserItem *it = &g_browser_items[g_browser_count++];
        memset(it, 0, sizeof(*it));
        it->type = BITEM_PLAY_DIR;
        snprintf(it->name, sizeof(it->name), "Play Entire Directory (%d tracks)", direct_audio_count);
        snprintf(it->full_path, sizeof(it->full_path), "%s", g_browser_dir);
        it->track_count = direct_audio_count;
    }

    /* 3. Parent directory '..' at ALL levels (including /mnt/media or initial toplevel) up to '/' */
    if (strcmp(g_browser_dir, "/") != 0 && g_browser_count < MAX_BROWSER_ITEMS) {
        BrowserItem *it = &g_browser_items[g_browser_count++];
        memset(it, 0, sizeof(*it));
        it->type = BITEM_PARENT;
        snprintf(it->name, sizeof(it->name), ".. (Parent Directory)");
        snprintf(it->full_path, sizeof(it->full_path), "%s", g_browser_dir);
        char *last_slash = strrchr(it->full_path, '/');
        if (last_slash && last_slash != it->full_path) {
            *last_slash = '\0';
        } else {
            snprintf(it->full_path, sizeof(it->full_path), "/");
        }
    }

    /* 4. Scan subdirectories and supported audio files */
    int sortable_start = g_browser_count;
    DIR *d = opendir(g_browser_dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL && g_browser_count < MAX_BROWSER_ITEMS) {
            if (de->d_name[0] == '.') continue;
            char full[PATH_MAX];
            if (strcmp(g_browser_dir, "/") == 0) {
                snprintf(full, sizeof(full), "/%.3500s", de->d_name);
            } else {
                snprintf(full, sizeof(full), "%.3500s/%.256s", g_browser_dir, de->d_name);
            }
            struct stat st;
            if (stat(full, &st) != 0) continue;

            if (S_ISDIR(st.st_mode)) {
                BrowserItem *it = &g_browser_items[g_browser_count++];
                memset(it, 0, sizeof(*it));
                it->type = BITEM_SUBDIR;
                snprintf(it->name, sizeof(it->name), "%s", de->d_name);
                snprintf(it->full_path, sizeof(it->full_path), "%s", full);
                it->track_count = count_audio_files_in_dir(full);
            } else if (S_ISREG(st.st_mode)) {
                AudioFormat fmt = detect_audio_format(de->d_name);
                if (fmt != FMT_NONE) {
                    BrowserItem *it = &g_browser_items[g_browser_count++];
                    memset(it, 0, sizeof(*it));
                    it->type = BITEM_AUDIO;
                    snprintf(it->name, sizeof(it->name), "%s", de->d_name);
                    snprintf(it->full_path, sizeof(it->full_path), "%s", full);
                    it->fmt = fmt;
                }
            }
        }
        closedir(d);
    }

    if (g_browser_count > sortable_start) {
        qsort(&g_browser_items[sortable_start],
              (size_t)(g_browser_count - sortable_start),
              sizeof(BrowserItem),
              cmp_browser_item);
    }

    g_browser_cursor = 0;
    g_browser_scroll = 0;
}

/* Start playing a specific track index in the active playlist */
static void start_playlist_track(int track_idx, int64_t start_ms, bool auto_play) {
    if (g_playlist_count <= 0) return;
    if (track_idx < 0) track_idx = 0;
    if (track_idx >= g_playlist_count) track_idx = 0;

    g_playing_idx = track_idx;
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%.3500s/%.256s",
             g_playlist_dir, g_playlist_files[g_playing_idx]);
    request_decoder_open(full_path, start_ms, auto_play);
}

/* Load all .mp3, .ogg, and .flac files in `dir_path` in filename order and play */
static bool load_directory_playlist(const char *dir_path, int start_track_idx, int64_t start_ms, bool auto_play) {
    DIR *d = opendir(dir_path);
    if (!d) return false;

    char temp_files[MAX_PLAYLIST_FILES][MAX_FILENAME_LEN];
    int count = 0;
    struct dirent *de;

    while ((de = readdir(d)) != NULL && count < MAX_PLAYLIST_FILES) {
        if (de->d_name[0] == '.') continue;
        if (detect_audio_format(de->d_name) != FMT_NONE) {
            snprintf(temp_files[count], MAX_FILENAME_LEN, "%s", de->d_name);
            count++;
        }
    }
    closedir(d);

    if (count == 0) return false;

    /* Sort tracks in filename order */
    qsort(temp_files, (size_t)count, MAX_FILENAME_LEN, cmp_filename_asc);

    snprintf(g_playlist_dir, sizeof(g_playlist_dir), "%s", dir_path);
    g_playlist_count = count;
    memcpy(g_playlist_files, temp_files, sizeof(temp_files));

    if (start_track_idx < 0 || start_track_idx >= g_playlist_count) {
        start_track_idx = 0;
    }
    g_playing_idx = start_track_idx;
    g_cursor_idx  = start_track_idx;
    g_playlist_scroll = 0;

    dismiss_resume_notice();
    start_playlist_track(g_playing_idx, start_ms, auto_play);
    return true;
}

/* Resume the saved session playlist & exact play position */
static bool resume_saved_session(bool auto_play) {
    if (!g_has_saved_session || g_saved_count <= 0) return false;
    snprintf(g_playlist_dir, sizeof(g_playlist_dir), "%s", g_saved_dir);
    g_playlist_count = g_saved_count;
    memcpy(g_playlist_files, g_saved_files, sizeof(g_saved_files));

    g_playing_idx = (g_saved_playing_idx >= 0 && g_saved_playing_idx < g_playlist_count)
                    ? g_saved_playing_idx : 0;
    g_cursor_idx  = (g_saved_cursor_idx >= 0 && g_saved_cursor_idx < g_playlist_count)
                    ? g_saved_cursor_idx : g_playing_idx;

    dismiss_resume_notice();
    start_playlist_track(g_playing_idx, g_saved_pos_ms, auto_play);
    return true;
}

/* =========================================================================
 * Persistent Playlist & Play Position Storage (MENU to Quit)
 * ========================================================================= */
static void resolve_config_path(void) {
    if (g_config_path[0] != '\0') return;
    if (access("/mnt/apps/musicplayer", W_OK) == 0) {
        snprintf(g_config_path, sizeof(g_config_path), "/mnt/apps/musicplayer/musicplayer.cfg");
    } else {
        const char *home = getenv("HOME");
        if (home && home[0]) {
            snprintf(g_config_path, sizeof(g_config_path), "%s/.musicplayer.cfg", home);
        } else {
            snprintf(g_config_path, sizeof(g_config_path), "./musicplayer.cfg");
        }
    }
}

static void save_state(void) {
    resolve_config_path();
    /* If no playlist was opened during this run, preserve any existing saved session */
    if (g_playlist_count <= 0) return;

    FILE *f = fopen(g_config_path, "w");
    if (!f) return;

    int pos_ms = atomic_load(&g_dec_pos_ms);
    fprintf(f, "active_dir=%s\n", g_playlist_dir);
    fprintf(f, "playing_idx=%d\n", g_playing_idx);
    fprintf(f, "cursor_idx=%d\n", g_cursor_idx);
    fprintf(f, "position_ms=%d\n", pos_ms);
    fprintf(f, "playlist_count=%d\n", g_playlist_count);
    for (int i = 0; i < g_playlist_count; i++) {
        fprintf(f, "file_%d=%s\n", i, g_playlist_files[i]);
    }
    fclose(f);
}

static void load_state(void) {
    resolve_config_path();
    FILE *f = fopen(g_config_path, "r");
    if (!f) return;

    g_has_saved_session = false;
    g_saved_count = 0;
    char line[PATH_MAX + 64];

    while (fgets(line, sizeof(line), f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';

        if (strncmp(line, "active_dir=", 11) == 0) {
            snprintf(g_saved_dir, sizeof(g_saved_dir), "%.*s", (int)(sizeof(g_saved_dir) - 1), line + 11);
        } else if (strncmp(line, "playing_idx=", 12) == 0) {
            g_saved_playing_idx = atoi(line + 12);
        } else if (strncmp(line, "cursor_idx=", 11) == 0) {
            g_saved_cursor_idx = atoi(line + 11);
        } else if (strncmp(line, "position_ms=", 12) == 0) {
            g_saved_pos_ms = atoi(line + 12);
        } else if (strncmp(line, "playlist_count=", 15) == 0) {
            int c = atoi(line + 15);
            if (c > MAX_PLAYLIST_FILES) c = MAX_PLAYLIST_FILES;
            if (c < 0) c = 0;
            g_saved_count = c;
        } else if (strncmp(line, "file_", 5) == 0) {
            const char *eq = strchr(line + 5, '=');
            if (eq) {
                int idx = atoi(line + 5);
                if (idx >= 0 && idx < MAX_PLAYLIST_FILES) {
                    snprintf(g_saved_files[idx], MAX_FILENAME_LEN, "%s", eq + 1);
                }
            }
        }
    }
    fclose(f);

    /* Verify the saved directory and track still exist on disk */
    if (g_saved_dir[0] != '\0' && g_saved_count > 0 &&
        g_saved_playing_idx >= 0 && g_saved_playing_idx < g_saved_count) {
        char check_path[PATH_MAX + MAX_FILENAME_LEN + 4];
        snprintf(check_path, sizeof(check_path), "%s/%s",
                 g_saved_dir, g_saved_files[g_saved_playing_idx]);
        if (access(check_path, R_OK) == 0) {
            g_has_saved_session = true;
        }
    }
}

/* =========================================================================
 * UI Rendering (320x240 RGB565: Directory Browser, Player View & Help Modal)
 * ========================================================================= */
static void format_mm_ss(int ms, char *out, size_t out_sz) {
    if (ms < 0) ms = 0;
    int total_sec = ms / 1000;
    int mins = total_sec / 60;
    int secs = total_sec % 60;
    snprintf(out, out_sz, "%02d:%02d", mins, secs);
}

static void render_help_overlay(SDL_Surface *screen) {
    fill_rect_alpha(screen, 0, 0, SCREEN_W, SCREEN_H, COL_BG, 232);
    fill_rect(screen, 12, 12, 296, 216, COL_PANEL);
    draw_border(screen, 12, 12, 296, 216, 2, COL_GOLD);

    fill_rect(screen, 14, 14, 292, 22, COL_HEADER);
    draw_text_centered(screen, FONT_UI_12, 160, 18, "MUSIC PLAYER - CONTROLS & GUIDE", COL_GOLD);

    const struct {
        const char *btn;
        const char *desc;
        uint16_t    col;
    } rows[] = {
        { "[A] Button",   "Play / Pause song (or Play Directory)",    COL_GREEN  },
        { "[B] Button",   "Play highlighted song in playlist",        COL_CYAN   },
        { "[UP / DOWN]",  "Highlight song in playlist / folder",      COL_WHITE  },
        { "[L1 / R1]",    "Seek -5s / +5s (Hold: Rewind / Fast-Fwd)", COL_GOLD   },
        { "[SELECT / X]", "Switch Directory Browser <-> Player View", COL_ORANGE },
        { "[LEFT/RIGHT]", "Parent folder (..) / Enter subfolder",     COL_PURPLE },
        { "[Y] Button",   "Toggle this Help & Controls Guide",        COL_CYAN   },
        { "[MENU]",       "Save file list + play position & Quit",    COL_ACCENT }
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
                       "Formats: OGG Vorbis, FLAC Lossless & MP3 Audio",
                       COL_GOLD);
}

static void render_browser_view(SDL_Surface *screen) {
    /* 1. Top Header Bar (y: 0..22) */
    fill_rect(screen, 0, 0, SCREEN_W, 22, COL_HEADER);
    fill_rect(screen, 0, 21, SCREEN_W, 1, COL_BORDER);

    fill_rect(screen, 4, 2, 82, 17, COL_CYAN);
    draw_text_centered(screen, FONT_UI_9, 45, 5, "MUSIC BROWSER", COL_BG);

    draw_path_tail(screen, FONT_UI_9, 92, 5, 186, g_browser_dir, COL_GOLD);

    fill_rect(screen, 283, 2, 33, 17, COL_PANEL_ALT);
    draw_border(screen, 283, 2, 33, 17, 1, COL_BORDER);
    draw_text_centered(screen, FONT_DOT_8, 299, 6, "Y:Help", COL_MUTED);

    /* 2. Directory List Card (y: 24..186 if track active, else y: 24..215) */
    bool track_active = atomic_load(&g_track_loaded) && (g_playlist_count > 0);
    int card_h = track_active ? 160 : 191;
    int visible_rows = track_active ? 7 : 9;

    fill_rect(screen, 4, 24, 312, card_h, COL_PANEL);
    draw_border(screen, 4, 24, 312, card_h, 1, COL_BORDER);

    fill_rect(screen, 6, 26, 308, 15, COL_HEADER);
    draw_text(screen, FONT_DOT_8, 10, 30,
              "SELECT DIRECTORY TO PLAY (SORTED BY FILENAME)", COL_MUTED);

    if (g_browser_cursor < g_browser_scroll) {
        g_browser_scroll = g_browser_cursor;
    } else if (g_browser_cursor >= g_browser_scroll + visible_rows) {
        g_browser_scroll = g_browser_cursor - visible_rows + 1;
    }

    for (int row = 0; row < visible_rows; row++) {
        int idx = g_browser_scroll + row;
        if (idx >= g_browser_count) break;
        const BrowserItem *it = &g_browser_items[idx];
        bool is_sel = (idx == g_browser_cursor);
        int ry = 43 + row * 19;

        if (is_sel) {
            fill_rect(screen, 7, ry, 306, 18, COL_ROW_SEL);
            draw_border(screen, 7, ry, 306, 18, 1,
                        (it->type == BITEM_RESUME) ? COL_GOLD : COL_CYAN);
        } else if (row % 2 == 1) {
            fill_rect(screen, 7, ry, 306, 18, COL_PANEL_ALT);
        }

        if (it->type == BITEM_RESUME) {
            fill_rect(screen, 10, ry + 2, 46, 14, COL_GOLD);
            draw_text_centered(screen, FONT_DOT_8, 33, ry + 5, "RESUME", COL_BG);
            draw_text_clipped(screen, FONT_UI_9, 60, ry + 3, 195, it->name, COL_GOLD);
            char pos_lbl[24];
            format_mm_ss(it->track_count * 1000, pos_lbl, sizeof(pos_lbl));
            draw_text(screen, FONT_DOT_8, 264, ry + 5, pos_lbl, COL_WHITE);
        } else if (it->type == BITEM_PLAY_DIR) {
            fill_rect(screen, 10, ry + 2, 46, 14, COL_GREEN);
            draw_text_centered(screen, FONT_DOT_8, 33, ry + 5, "PLAY ALL", COL_BG);
            draw_text_clipped(screen, FONT_UI_9, 60, ry + 3, 245, it->name, COL_GREEN);
        } else if (it->type == BITEM_PARENT) {
            fill_rect(screen, 10, ry + 2, 34, 14, COL_PANEL_ALT);
            draw_border(screen, 10, ry + 2, 34, 14, 1, COL_MUTED);
            draw_text_centered(screen, FONT_DOT_8, 27, ry + 5, "UP ..", COL_MUTED);
            draw_text_clipped(screen, FONT_UI_9, 48, ry + 3, 255, it->name, COL_MUTED);
        } else if (it->type == BITEM_SUBDIR) {
            fill_rect(screen, 10, ry + 2, 34, 14, COL_PANEL_ALT);
            draw_border(screen, 10, ry + 2, 34, 14, 1, COL_CYAN);
            draw_text_centered(screen, FONT_DOT_8, 27, ry + 5, "DIR", COL_CYAN);
            draw_text_clipped(screen, FONT_UI_9, 48, ry + 3, 200, it->name,
                              is_sel ? COL_WHITE : COL_CYAN);
            char cnt_str[24];
            snprintf(cnt_str, sizeof(cnt_str), "%d trk", it->track_count);
            draw_text(screen, FONT_DOT_8, 262, ry + 5, cnt_str, COL_MUTED);
        } else if (it->type == BITEM_AUDIO) {
            fill_rect(screen, 10, ry + 2, 34, 14, COL_PANEL_ALT);
            draw_border(screen, 10, ry + 2, 34, 14, 1, COL_MUTED);
            draw_text_centered(screen, FONT_DOT_8, 27, ry + 5, "SONG", COL_MUTED);
            draw_text_clipped(screen, FONT_UI_9, 48, ry + 3, 256, it->name,
                              is_sel ? COL_WHITE : COL_MUTED);
        }
    }

    /* 3. Mini Now-Playing + Seek Bar if a Track is Active (y: 186..215) */
    if (track_active) {
        fill_rect(screen, 4, 186, 312, 29, COL_PANEL_ALT);
        draw_border(screen, 4, 186, 312, 29, 1, COL_GOLD);

        bool is_playing = atomic_load(&g_playing);
        draw_text(screen, FONT_DOT_8, 8, 190,
                  is_playing ? "NOW PLAYING:" : "PAUSED:",
                  is_playing ? COL_GREEN : COL_ORANGE);
        draw_text_clipped(screen, FONT_DOT_8, 84, 190, 155,
                          g_playlist_files[g_playing_idx], COL_WHITE);

        int pos_ms = atomic_load(&g_dec_pos_ms);
        int dur_ms = atomic_load(&g_dec_dur_ms);
        char t_pos[16], t_dur[16], t_both[36];
        format_mm_ss(pos_ms, t_pos, sizeof(t_pos));
        format_mm_ss(dur_ms, t_dur, sizeof(t_dur));
        snprintf(t_both, sizeof(t_both), "%s/%s", t_pos, t_dur);
        draw_text(screen, FONT_DOT_8, 244, 190, t_both, COL_GOLD);

        /* Seek bar inside Mini Now-Playing card */
        fill_rect(screen, 8, 202, 304, 8, COL_BG);
        draw_border(screen, 8, 202, 304, 8, 1, COL_BORDER);
        if (dur_ms > 0) {
            int fill_w = (int)(((int64_t)pos_ms * 300) / dur_ms);
            if (fill_w < 0) fill_w = 0;
            if (fill_w > 300) fill_w = 300;
            fill_rect(screen, 10, 204, fill_w, 4, COL_GOLD);
        }
    }

    /* 4. Bottom Controls Footer (y: 217..240) */
    fill_rect(screen, 0, 217, SCREEN_W, 23, COL_HEADER);
    fill_rect(screen, 0, 217, SCREEN_W, 1, COL_BORDER);
    draw_text_centered(screen, FONT_DOT_8, 160, 225,
                       "[A]Play Dir [B]Open [L1/R1]Seek [SEL]Player [MENU]Quit",
                       COL_WHITE);
}

static void render_player_view(SDL_Surface *screen) {
    bool is_playing = atomic_load(&g_playing);
    AudioFormat fmt = (AudioFormat)atomic_load(&g_dec_fmt);
    int rate = atomic_load(&g_dec_src_rate);
    int pos_ms = atomic_load(&g_dec_pos_ms);
    int dur_ms = atomic_load(&g_dec_dur_ms);

    /* ================= 1. TOP HEADER BAR (y: 0..22) ================= */
    fill_rect(screen, 0, 0, SCREEN_W, 22, COL_HEADER);
    fill_rect(screen, 0, 21, SCREEN_W, 1, COL_BORDER);

    /* Play / Pause Status Badge */
    fill_rect(screen, 4, 2, 58, 17, is_playing ? COL_GREEN : COL_ORANGE);
    draw_text_centered(screen, FONT_UI_9, 33, 5, is_playing ? "PLAYING" : "PAUSED", COL_BG);

    /* Format Badge (MP3 / FLAC / OGG) */
    uint16_t fcol = format_badge_col(fmt);
    fill_rect(screen, 66, 2, 38, 17, COL_PANEL_ALT);
    draw_border(screen, 66, 2, 38, 17, 1, fcol);
    draw_text_centered(screen, FONT_UI_9, 85, 5, format_badge_str(fmt), fcol);

    /* Sample Rate & Track Counter */
    char hdr_info[64];
    snprintf(hdr_info, sizeof(hdr_info), "%.1fkHz   Track %02d / %02d",
             (double)rate / 1000.0,
             (g_playlist_count > 0) ? (g_playing_idx + 1) : 0,
             g_playlist_count);
    draw_text(screen, FONT_UI_9, 110, 5, hdr_info, COL_WHITE);

    /* Y:Help Badge */
    fill_rect(screen, 283, 2, 33, 17, COL_PANEL_ALT);
    draw_border(screen, 283, 2, 33, 17, 1, COL_BORDER);
    draw_text_centered(screen, FONT_DOT_8, 299, 6, "Y:Help", COL_MUTED);

    /* ================= 2. NOW PLAYING + SEEK BAR CARD (y: 24..102) ================= */
    fill_rect(screen, 4, 24, 312, 78, COL_PANEL);
    draw_border(screen, 4, 24, 312, 78, 1, COL_BORDER);

    const char *cur_filename = (g_playlist_count > 0)
                               ? g_playlist_files[g_playing_idx]
                               : "No Track Loaded";
    draw_text_clipped(screen, FONT_UI_12, 10, 30, 300, cur_filename, COL_GOLD);

    /* Metadata & Directory Subtitle */
    char sub_line[512];
    pthread_mutex_lock(&g_meta_mutex);
    if (g_meta_title[0] != '\0' && g_meta_artist[0] != '\0') {
        snprintf(sub_line, sizeof(sub_line), "%.120s  -  %.120s", g_meta_artist, g_meta_title);
    } else if (g_meta_title[0] != '\0') {
        snprintf(sub_line, sizeof(sub_line), "%.240s", g_meta_title);
    } else {
        snprintf(sub_line, sizeof(sub_line), "Dir: %.240s", g_playlist_dir);
    }
    pthread_mutex_unlock(&g_meta_mutex);
    draw_text_clipped(screen, FONT_UI_9, 10, 49, 300, sub_line, COL_CYAN);

    /* Seek Progress Bar (y: 68..80) + Time Readout (y: 84) */
    int bar_x = 10, bar_y = 67, bar_w = 300, bar_h = 12;
    fill_rect(screen, bar_x, bar_y, bar_w, bar_h, COL_BG);
    draw_border(screen, bar_x, bar_y, bar_w, bar_h, 1, COL_BORDER);

    int fill_w = 0;
    if (dur_ms > 0) {
        fill_w = (int)(((int64_t)pos_ms * (bar_w - 4)) / dur_ms);
        if (fill_w < 0) fill_w = 0;
        if (fill_w > bar_w - 4) fill_w = bar_w - 4;
    }
    if (fill_w > 0) {
        fill_rect(screen, bar_x + 2, bar_y + 2, fill_w, bar_h - 4,
                  is_playing ? COL_GREEN : COL_GOLD);
    }
    /* Playhead thumb marker */
    int thumb_x = bar_x + 2 + fill_w;
    if (thumb_x > bar_x + bar_w - 4) thumb_x = bar_x + bar_w - 4;
    fill_rect(screen, thumb_x - 1, bar_y + 1, 3, bar_h - 2, COL_WHITE);

    char pos_str[16], dur_str[16], time_label[48];
    format_mm_ss(pos_ms, pos_str, sizeof(pos_str));
    format_mm_ss(dur_ms, dur_str, sizeof(dur_str));
    snprintf(time_label, sizeof(time_label), "%s / %s", pos_str, dur_str);
    draw_text(screen, FONT_UI_9, 10, 83, time_label, COL_WHITE);
    draw_text(screen, FONT_DOT_8, 162, 85, "[L1/R1] Seek (Hold: Rew/FFW)", COL_MUTED);

    /* ================= 3. INTERACTIVE DIRECTORY PLAYLIST CARD (y: 104..215) ================= */
    fill_rect(screen, 4, 104, 312, 111, COL_PANEL);
    draw_border(screen, 4, 104, 312, 111, 1, COL_BORDER);

    fill_rect(screen, 6, 106, 308, 15, COL_HEADER);
    draw_text(screen, FONT_DOT_8, 10, 110, "DIR:", COL_GOLD);
    draw_path_tail(screen, FONT_DOT_8, 36, 110, 184, g_playlist_dir, COL_GOLD);
    draw_text(screen, FONT_DOT_8, 226, 110, "[UP/DN]+[B] Play", COL_CYAN);

    int visible_rows = 5;
    if (g_cursor_idx < g_playlist_scroll) {
        g_playlist_scroll = g_cursor_idx;
    } else if (g_cursor_idx >= g_playlist_scroll + visible_rows) {
        g_playlist_scroll = g_cursor_idx - visible_rows + 1;
    }

    for (int row = 0; row < visible_rows; row++) {
        int idx = g_playlist_scroll + row;
        if (idx >= g_playlist_count) break;
        bool is_cursor  = (idx == g_cursor_idx);
        bool is_current = (idx == g_playing_idx);
        int ry = 123 + row * 18;

        if (is_cursor) {
            fill_rect(screen, 7, ry, 306, 17, COL_ROW_SEL);
            draw_border(screen, 7, ry, 306, 17, 1, is_current ? COL_GREEN : COL_GOLD);
        } else if (row % 2 == 1) {
            fill_rect(screen, 7, ry, 306, 17, COL_PANEL_ALT);
        }

        /* Left status marker: '>' cursor and/or 'PLAY' indicator */
        if (is_current) {
            fill_rect(screen, 10, ry + 2, 28, 13, is_playing ? COL_GREEN : COL_ORANGE);
            draw_text_centered(screen, FONT_DOT_8, 24, ry + 4,
                               is_playing ? "PLAY" : "PAUS", COL_BG);
        } else if (is_cursor) {
            fill_rect(screen, 10, ry + 2, 28, 13, COL_GOLD);
            draw_text_centered(screen, FONT_DOT_8, 24, ry + 4, "[B]", COL_BG);
        } else {
            char num_str[24];
            snprintf(num_str, sizeof(num_str), "%02d.", idx + 1);
            draw_text(screen, FONT_DOT_8, 14, ry + 4, num_str, COL_MUTED);
        }

        uint16_t name_col = is_current ? COL_GREEN : (is_cursor ? COL_GOLD : COL_WHITE);
        draw_text_clipped(screen, FONT_UI_9, 43, ry + 2, 266,
                          g_playlist_files[idx], name_col);
    }

    /* ================= 4. BOTTOM CONTROLS FOOTER (y: 217..240) ================= */
    fill_rect(screen, 0, 217, SCREEN_W, 23, COL_HEADER);
    fill_rect(screen, 0, 217, SCREEN_W, 1, COL_BORDER);

    fill_rect(screen, 4, 221, 54, 15, is_playing ? COL_ORANGE : COL_GREEN);
    draw_text_centered(screen, FONT_DOT_8, 31, 225,
                       is_playing ? "[A] PAUSE" : "[A] PLAY", COL_BG);

    draw_text(screen, FONT_DOT_8, 62, 225,
              "[B]Play Sel [UP/DN]List [SEL]Dirs [MENU]Quit",
              COL_WHITE);
}

static void render_ui(SDL_Surface *screen) {
    fill_rect(screen, 0, 0, SCREEN_W, SCREEN_H, COL_BG);
    if (g_view == VIEW_BROWSER) {
        render_browser_view(screen);
    } else {
        render_player_view(screen);
    }
    if (g_show_help) {
        render_help_overlay(screen);
    }
}

/* =========================================================================
 * Automated Self-Test / Verification Mode (--verify <dir>)
 * ========================================================================= */
static int run_verification_suite(const char *test_dir) {
    printf("Verifying libmpg123, libvorbisfile, and libFLAC decoders in %s...\n", test_dir);
    mpg123_init();

    if (!load_directory_playlist(test_dir, 0, 0, false)) {
        fprintf(stderr, "FAIL: Could not load audio files from %s\n", test_dir);
        return 1;
    }

    /* Verify alphabetical filename ordering */
    for (int i = 1; i < g_playlist_count; i++) {
        if (strcasecmp(g_playlist_files[i - 1], g_playlist_files[i]) > 0) {
            fprintf(stderr, "FAIL: Playlist not in filename order: %s vs %s\n",
                    g_playlist_files[i - 1], g_playlist_files[i]);
            return 1;
        }
    }
    printf("[OK] Playlist sorted in filename order (%d tracks)\n", g_playlist_count);

    g_dec_thread_running = true;
    pthread_create(&g_dec_thread, NULL, decoder_thread_fn, NULL);

    for (int i = 0; i < g_playlist_count; i++) {
        start_playlist_track(i, 0, true);
        int waited_ms = 0;
        while (waited_ms < 2000 && ring_available_to_read() < 2048) {
            usleep(10000);
            waited_ms += 10;
        }
        if (!atomic_load(&g_track_loaded) || ring_available_to_read() == 0) {
            fprintf(stderr, "FAIL: Track %d (%s) failed to decode PCM frames\n",
                    i, g_playlist_files[i]);
            g_dec_thread_running = false;
            pthread_join(g_dec_thread, NULL);
            return 1;
        }
        /* Test seeking forward +1000ms */
        request_decoder_seek(1000);
        usleep(40000);
        printf("[OK] Verified %s (%s, %d Hz, dur=%d ms, seek_pos=%d ms)\n",
               g_playlist_files[i],
               format_badge_str((AudioFormat)atomic_load(&g_dec_fmt)),
               atomic_load(&g_dec_src_rate),
               atomic_load(&g_dec_dur_ms),
               atomic_load(&g_dec_pos_ms));
    }

    g_dec_thread_running = false;
    pthread_join(g_dec_thread, NULL);
    mpg123_exit();
    return 0;
}

/* =========================================================================
 * Main Entry Point & Event Loop
 * ========================================================================= */
int main(int argc, char *argv[]) {
    putenv((char *)"SDL_NOMOUSE=1");

    const char *cli_root_dir  = NULL;
    const char *cli_play_dir  = NULL;
    int         cli_view      = -1;
    int         cli_track_idx = 0;
    int         cli_cursor    = -1;
    int         cli_pos_ms    = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verify") == 0 && i + 1 < argc) {
            return run_verification_suite(argv[i + 1]);
        } else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            cli_root_dir = argv[++i];
        } else if (strcmp(argv[i], "--play-dir") == 0 && i + 1 < argc) {
            cli_play_dir = argv[++i];
        } else if (strcmp(argv[i], "--view") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            cli_view = (strcasecmp(v, "player") == 0) ? VIEW_PLAYER : VIEW_BROWSER;
        } else if (strcmp(argv[i], "--track") == 0 && i + 1 < argc) {
            cli_track_idx = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc) {
            cli_cursor = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--pos-ms") == 0 && i + 1 < argc) {
            cli_pos_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            snprintf(g_config_path, sizeof(g_config_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--help-overlay") == 0) {
            g_show_help = true;
        } else if (argv[i][0] != '-') {
            cli_root_dir = argv[i];
        }
    }

    mpg123_init();
    load_state();

    /* Determine starting directory (/mnt/media on V90, or ./sample_media on desktop) */
    if (cli_root_dir) {
        snprintf(g_browser_root, sizeof(g_browser_root), "%s", cli_root_dir);
    } else if (access("/mnt/media", R_OK) == 0) {
        snprintf(g_browser_root, sizeof(g_browser_root), "/mnt/media");
    } else if (access("./sample_media", R_OK) == 0) {
        snprintf(g_browser_root, sizeof(g_browser_root), "./sample_media");
    } else {
        snprintf(g_browser_root, sizeof(g_browser_root), ".");
    }

    scan_browser_directory(g_browser_root);

    /* Start background audio decoder thread */
    g_dec_thread_running = true;
    pthread_create(&g_dec_thread, NULL, decoder_thread_fn, NULL);

    if (cli_play_dir) {
        if (load_directory_playlist(cli_play_dir, cli_track_idx, cli_pos_ms, true)) {
            if (cli_cursor >= 0 && cli_cursor < g_playlist_count) {
                g_cursor_idx = cli_cursor;
            }
            if (cli_view < 0) g_view = VIEW_PLAYER;
        }
    }
    if (cli_view >= 0) {
        g_view = (AppView)cli_view;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        g_dec_thread_running = false;
        pthread_join(g_dec_thread, NULL);
        mpg123_exit();
        return 1;
    }

    SDL_ShowCursor(SDL_DISABLE);
    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, SCREEN_BPP, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        g_dec_thread_running = false;
        pthread_join(g_dec_thread, NULL);
        mpg123_exit();
        return 1;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec desired, obtained;
        memset(&desired, 0, sizeof(desired));
        desired.freq     = AUDIO_RATE;
        desired.format   = AUDIO_S16SYS;
        desired.channels = AUDIO_CHANNELS;
        desired.samples  = AUDIO_SAMPLES;
        desired.callback = audio_output_callback;
        if (SDL_OpenAudio(&desired, &obtained) == 0) {
            SDL_PauseAudio(0);
        }
    }

    bool l1_held = false, r1_held = false;
    Uint32 seek_hold_start_ms = 0, last_seek_repeat_ms = 0;
    bool up_held = false, down_held = false;
    Uint32 nav_hold_start_ms = 0, last_nav_repeat_ms = 0;
    bool running = true;

    while (running) {
        Uint32 now_ms = SDL_GetTicks();

        /* Auto-advance to the next song in filename order when a track finishes */
        if (atomic_load(&g_playing) && atomic_load(&g_track_eof) && g_playlist_count > 0) {
            bool follow_cursor = (g_cursor_idx == g_playing_idx);
            int next_idx = (g_playing_idx + 1) % g_playlist_count;
            start_playlist_track(next_idx, 0, true);
            if (follow_cursor) {
                g_cursor_idx = g_playing_idx;
            }
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                SDLKey k = ev.key.keysym.sym;

                /* Y Button: Toggle Help & Controls Guide overlay */
                if (k == V90_BTN_Y
#if defined(HOST_X11)
                    || k == SDLK_y || k == SDLK_h
#endif
                ) {
                    g_show_help = !g_show_help;
                }
                /* MENU Button: Save file list & play position and Quit */
                else if (k == V90_BTN_MENU
#if defined(HOST_X11)
                         || k == SDLK_q || k == SDLK_m
#endif
                ) {
                    running = false;
                } else if (g_show_help) {
                    if (k == V90_BTN_B || k == V90_BTN_A || k == V90_BTN_SELECT) {
                        g_show_help = false;
                    }
                }
                /* L1 / R1: Always Seek Backward / Forward (works in BOTH Player and Browser views!) */
                else if (k == V90_BTN_L1 || k == V90_BTN_R2
#if defined(HOST_X11)
                         || k == SDLK_LEFTBRACKET
#endif
                ) {
                    request_decoder_seek(-5000);
                    l1_held = true;
                    r1_held = false;
                    seek_hold_start_ms = now_ms;
                    last_seek_repeat_ms = now_ms;
                } else if (k == V90_BTN_R1 || k == V90_BTN_L2
#if defined(HOST_X11)
                           || k == SDLK_RIGHTBRACKET || k == SDLK_e
#endif
                ) {
                    request_decoder_seek(5000);
                    r1_held = true;
                    l1_held = false;
                    seek_hold_start_ms = now_ms;
                    last_seek_repeat_ms = now_ms;
                }
                /* SELECT or X: Toggle between Directory Browser and Now Playing / Playlist View */
                else if (k == V90_BTN_SELECT || k == V90_BTN_X
#if defined(HOST_X11)
                         || k == SDLK_s || k == SDLK_c || k == SDLK_x
#endif
                ) {
                    up_held = down_held = false;
                    if (g_view == VIEW_PLAYER) {
                        g_view = VIEW_BROWSER;
                    } else if (g_playlist_count > 0) {
                        g_view = VIEW_PLAYER;
                    }
                }
                /* View-Specific Controls */
                else if (g_view == VIEW_BROWSER) {
                    if (k == V90_BTN_UP) {
                        if (g_browser_count > 0) {
                            g_browser_cursor = (g_browser_cursor + g_browser_count - 1) % g_browser_count;
                        }
                        up_held = true;
                        down_held = false;
                        nav_hold_start_ms = now_ms;
                        last_nav_repeat_ms = now_ms;
                    } else if (k == V90_BTN_DOWN) {
                        if (g_browser_count > 0) {
                            g_browser_cursor = (g_browser_cursor + 1) % g_browser_count;
                        }
                        down_held = true;
                        up_held = false;
                        nav_hold_start_ms = now_ms;
                        last_nav_repeat_ms = now_ms;
                    } else if (k == V90_BTN_LEFT) {
                        /* Navigate to parent directory */
                        for (int i = 0; i < g_browser_count; i++) {
                            if (g_browser_items[i].type == BITEM_PARENT) {
                                char parent_path[PATH_MAX];
                                snprintf(parent_path, sizeof(parent_path), "%s", g_browser_items[i].full_path);
                                scan_browser_directory(parent_path);
                                break;
                            }
                        }
                    } else if (k == V90_BTN_RIGHT) {
                        if (g_browser_count > 0) {
                            const BrowserItem *it = &g_browser_items[g_browser_cursor];
                            if (it->type == BITEM_SUBDIR) {
                                char sub_path[PATH_MAX];
                                snprintf(sub_path, sizeof(sub_path), "%s", it->full_path);
                                scan_browser_directory(sub_path);
                            }
                        }
                    } else if (k == V90_BTN_A || k == V90_BTN_START
#if defined(HOST_X11)
                               || k == SDLK_a
#endif
                    ) {
                        /* A Button in Browser: Play the selected Directory, Resume entry, or File */
                        if (g_browser_count > 0) {
                            const BrowserItem *it = &g_browser_items[g_browser_cursor];
                            if (it->type == BITEM_RESUME) {
                                if (resume_saved_session(true)) {
                                    g_view = VIEW_PLAYER;
                                }
                            } else if (it->type == BITEM_PLAY_DIR || it->type == BITEM_SUBDIR) {
                                if (load_directory_playlist(it->full_path, 0, 0, true)) {
                                    g_view = VIEW_PLAYER;
                                } else if (it->type == BITEM_SUBDIR) {
                                    char sub_path[PATH_MAX];
                                    snprintf(sub_path, sizeof(sub_path), "%s", it->full_path);
                                    scan_browser_directory(sub_path);
                                }
                            } else if (it->type == BITEM_PARENT) {
                                char parent_path[PATH_MAX];
                                snprintf(parent_path, sizeof(parent_path), "%s", it->full_path);
                                scan_browser_directory(parent_path);
                            } else if (it->type == BITEM_AUDIO) {
                                if (load_directory_playlist(g_browser_dir, 0, 0, true)) {
                                    for (int i = 0; i < g_playlist_count; i++) {
                                        if (strcmp(g_playlist_files[i], it->name) == 0) {
                                            g_cursor_idx = i;
                                            start_playlist_track(i, 0, true);
                                            break;
                                        }
                                    }
                                    g_view = VIEW_PLAYER;
                                }
                            }
                        }
                    } else if (k == V90_BTN_B
#if defined(HOST_X11)
                               || k == SDLK_b
#endif
                    ) {
                        /* B Button in Browser: Enter subdirectory, or activate Resume / Play Dir / File */
                        if (g_browser_count > 0) {
                            const BrowserItem *it = &g_browser_items[g_browser_cursor];
                            if (it->type == BITEM_SUBDIR || it->type == BITEM_PARENT) {
                                char next_path[PATH_MAX];
                                snprintf(next_path, sizeof(next_path), "%s", it->full_path);
                                scan_browser_directory(next_path);
                            } else if (it->type == BITEM_RESUME) {
                                if (resume_saved_session(true)) {
                                    g_view = VIEW_PLAYER;
                                }
                            } else if (it->type == BITEM_PLAY_DIR) {
                                if (load_directory_playlist(it->full_path, 0, 0, true)) {
                                    g_view = VIEW_PLAYER;
                                }
                            } else if (it->type == BITEM_AUDIO) {
                                if (load_directory_playlist(g_browser_dir, 0, 0, true)) {
                                    for (int i = 0; i < g_playlist_count; i++) {
                                        if (strcmp(g_playlist_files[i], it->name) == 0) {
                                            g_cursor_idx = i;
                                            start_playlist_track(i, 0, true);
                                            break;
                                        }
                                    }
                                    g_view = VIEW_PLAYER;
                                }
                            }
                        }
                    }
                } else if (g_view == VIEW_PLAYER) {
                    /* A Button in Player: Play / Pause */
                    if (k == V90_BTN_A || k == V90_BTN_START
#if defined(HOST_X11)
                        || k == SDLK_a
#endif
                    ) {
                        bool cur = atomic_load(&g_playing);
                        atomic_store(&g_playing, !cur);
                    }
                    /* B Button in Player: Select & Play the UP/DOWN-highlighted song in the playlist */
                    else if (k == V90_BTN_B
#if defined(HOST_X11)
                             || k == SDLK_b
#endif
                    ) {
                        if (g_playlist_count > 0) {
                            start_playlist_track(g_cursor_idx, 0, true);
                        }
                    }
                    /* UP / DOWN in Player: Highlight songs in the playlist (hold to scroll quickly) */
                    else if (k == V90_BTN_UP) {
                        if (g_playlist_count > 0) {
                            g_cursor_idx = (g_cursor_idx + g_playlist_count - 1) % g_playlist_count;
                        }
                        up_held = true;
                        down_held = false;
                        nav_hold_start_ms = now_ms;
                        last_nav_repeat_ms = now_ms;
                    } else if (k == V90_BTN_DOWN) {
                        if (g_playlist_count > 0) {
                            g_cursor_idx = (g_cursor_idx + 1) % g_playlist_count;
                        }
                        down_held = true;
                        up_held = false;
                        nav_hold_start_ms = now_ms;
                        last_nav_repeat_ms = now_ms;
                    } else if (k == V90_BTN_LEFT) {
                        if (g_playlist_count > 0) {
                            g_cursor_idx -= 5;
                            if (g_cursor_idx < 0) g_cursor_idx = 0;
                        }
                    } else if (k == V90_BTN_RIGHT) {
                        if (g_playlist_count > 0) {
                            g_cursor_idx += 5;
                            if (g_cursor_idx >= g_playlist_count) g_cursor_idx = g_playlist_count - 1;
                        }
                    }
                }
            } else if (ev.type == SDL_KEYUP) {
                SDLKey k = ev.key.keysym.sym;
                if (k == V90_BTN_L1 || k == V90_BTN_R2
#if defined(HOST_X11)
                    || k == SDLK_LEFTBRACKET
#endif
                ) {
                    l1_held = false;
                }
                if (k == V90_BTN_R1 || k == V90_BTN_L2
#if defined(HOST_X11)
                    || k == SDLK_RIGHTBRACKET || k == SDLK_e
#endif
                ) {
                    r1_held = false;
                }
                if (k == V90_BTN_UP) {
                    up_held = false;
                }
                if (k == V90_BTN_DOWN) {
                    down_held = false;
                }
            }
        }

        /* Hold UP / DOWN to scroll quickly through the playlist or directory browser */
        if (!g_show_help && (up_held || down_held) && (now_ms - nav_hold_start_ms > 250)) {
            Uint32 repeat_ms = (now_ms - nav_hold_start_ms > 1100) ? 35 : 65;
            if (now_ms - last_nav_repeat_ms >= repeat_ms) {
                if (g_view == VIEW_BROWSER && g_browser_count > 0) {
                    if (up_held) {
                        g_browser_cursor = (g_browser_cursor + g_browser_count - 1) % g_browser_count;
                    } else {
                        g_browser_cursor = (g_browser_cursor + 1) % g_browser_count;
                    }
                } else if (g_view == VIEW_PLAYER && g_playlist_count > 0) {
                    if (up_held) {
                        g_cursor_idx = (g_cursor_idx + g_playlist_count - 1) % g_playlist_count;
                    } else {
                        g_cursor_idx = (g_cursor_idx + 1) % g_playlist_count;
                    }
                }
                last_nav_repeat_ms = now_ms;
            }
        }

        /* Hold-to-Rewind / Fast-Forward when holding L1 or R1 */
        if ((l1_held || r1_held) && (now_ms - seek_hold_start_ms > 280)) {
            if (now_ms - last_seek_repeat_ms >= 90) {
                int64_t step_ms = (now_ms - seek_hold_start_ms > 1500) ? 12000 : 4000;
                request_decoder_seek(l1_held ? -step_ms : step_ms);
                last_seek_repeat_ms = now_ms;
            }
        }

        if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
        render_ui(screen);
        if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
        SDL_Flip(screen);

        SDL_Delay(16);
    }

    /* Save active file list and exact playback position before exiting */
    save_state();

    SDL_CloseAudio();
    g_dec_thread_running = false;
    pthread_join(g_dec_thread, NULL);
    SDL_Quit();
    mpg123_exit();
    return 0;
}
