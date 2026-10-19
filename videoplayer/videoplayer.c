/*
 * videoplayer - Full-Screen Video & Audio Player for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host
 *
 * Features:
 *   1. Startup file browser opening directly in /mnt/media on the SD card.
 *   2. Contemporary multimedia decoding via FFmpeg (libavformat, libavcodec, libswscale,
 *      libswresample, libavutil) and synchronized audio playback via ALSA (libasound).
 *   3. Full-screen 320x240 native video playback with zero on-screen distractions.
 *   4. Play/Pause on the A button; while paused, displays a compact Device Help overlay.
 *   5. Exit on START or MENU.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <alsa/asoundlib.h>

#include "videoplayer_font.h"

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
#define SDLK_RETURN     XK_Return
#define SDLK_RCTRL      XK_Control_R
#define SDLK_UP         XK_Up
#define SDLK_DOWN       XK_Down
#define SDLK_LEFT       XK_Left
#define SDLK_RIGHT      XK_Right
#define SDLK_a          XK_a
#define SDLK_b          XK_b
#define SDLK_q          XK_q
#define SDLK_x          XK_x
#define SDLK_y          XK_y

#define SDL_QUIT        1
#define SDL_KEYDOWN     2
#define SDL_KEYUP       3

typedef struct {
    int bpp;
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
#define SDL_DISABLE     0
#define SDL_SWSURFACE   0

static Display *g_x11_dpy = NULL;
static Window g_x11_win = 0;
static GC g_x11_gc = 0;
static XImage *g_x11_img = NULL;
static Atom g_wm_delete_window = 0;
static char *g_scaled_buf = NULL;
static SDL_PixelFormat g_x11_fmt = { 32 };
static SDL_Surface g_x11_surface = { 0 };

static int SDL_Init(Uint32 flags) {
    (void)flags;
    g_x11_dpy = XOpenDisplay(NULL);
    return g_x11_dpy ? 0 : -1;
}

static const char *SDL_GetError(void) {
    return "Cannot open X11 DISPLAY";
}

static void SDL_ShowCursor(int toggle) {
    (void)toggle;
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

    XWMHints wm_hints;
    memset(&wm_hints, 0, sizeof(wm_hints));
    wm_hints.flags = InputHint | StateHint;
    wm_hints.input = True;
    wm_hints.initial_state = NormalState;
    XSetWMHints(g_x11_dpy, g_x11_win, &wm_hints);

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
#endif

/* =========================================================================
 * PowKiddy V90 Hardware Button Definitions (MiyooCFW 1.3.3)
 * ========================================================================= */
#define V90_BTN_UP      SDLK_UP
#define V90_BTN_DOWN    SDLK_DOWN
#define V90_BTN_LEFT    SDLK_LEFT
#define V90_BTN_RIGHT   SDLK_RIGHT
#define V90_BTN_A       SDLK_LALT       /* A button: Open file / Play & Pause */
#define V90_BTN_B       SDLK_LCTRL      /* B button: Up directory / Return to browser */
#define V90_BTN_X       SDLK_LSHIFT     /* X button: Return to file browser */
#define V90_BTN_Y       SDLK_SPACE      /* Y button: Play/Pause alias */
#define V90_BTN_L1      SDLK_TAB        /* L1 shoulder: Seek -5s */
#define V90_BTN_R1      SDLK_BACKSPACE  /* R1 shoulder: Seek +5s */
#define V90_BTN_SELECT  SDLK_ESCAPE     /* SELECT */
#define V90_BTN_START   SDLK_RETURN     /* START: Exit */
#define V90_BTN_MENU    SDLK_RCTRL      /* MENU: Exit */

/* =========================================================================
 * 24-bit TrueColor Framebuffer & Software UI Renderer
 * ========================================================================= */
static uint32_t g_fb[SCREEN_W * SCREEN_H];
static uint32_t g_video_fb[SCREEN_W * SCREEN_H];

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

static void fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > SCREEN_W) ? SCREEN_W : (x + w);
    int y1 = (y + h > SCREEN_H) ? SCREEN_H : (y + h);
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            blend_pixel(xx, yy, color, alpha);
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

static void draw_text_clipped(int font_id, int x, int y, int max_w, const char *s, uint32_t color) {
    if (text_width(font_id, s) <= max_w) {
        draw_text(font_id, x, y, s, color);
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s);
    size_t len = strlen(buf);
    while (len > 3) {
        buf[len - 3] = '.';
        buf[len - 2] = '.';
        buf[len - 1] = '\0';
        if (text_width(font_id, buf) <= max_w) break;
        len--;
    }
    draw_text(font_id, x, y, buf, color);
}

static void draw_text_centered(int font_id, int bx, int by, int bw, int bh, const char *s, uint32_t color) {
    const BardFont *f = &g_bard_fonts[font_id];
    int tw = text_width(font_id, s);
    int tx = bx + (bw - tw) / 2;
    int ty = by + (bh - f->line_height) / 2;
    draw_text(font_id, tx, ty, s, color);
}

static void draw_text_right_aligned(int font_id, int right_x, int y, const char *s, uint32_t color) {
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

static double get_wall_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

/* =========================================================================
 * Application States & /mnt/media File Browser
 * ========================================================================= */
typedef enum {
    STATE_BROWSER = 0,
    STATE_PLAYING = 1,
    STATE_PAUSED  = 2
} AppState;

#define MAX_FILES 512
typedef struct {
    char name[256];
    bool is_dir;
    off_t size;
} FileEntry;

static char      g_cwd[PATH_MAX];
static FileEntry g_files[MAX_FILES];
static int       g_file_count = 0;
static int       g_file_sel   = 0;
static int       g_file_scroll = 0;
#define BROWSER_ROWS 9

static AppState  g_state = STATE_BROWSER;

static void extract_dirname(const char *path, char *dir_out, size_t dir_sz) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        dir_out[0] = '\0';
    } else {
        size_t len = (size_t)(slash - path);
        if (len >= dir_sz) len = dir_sz - 1;
        memcpy(dir_out, path, len);
        dir_out[len] = '\0';
    }
}

static int cmp_file_entry(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;
    if (strcmp(fa->name, "..") == 0) return -1;
    if (strcmp(fb->name, "..") == 0) return 1;
    if (fa->is_dir != fb->is_dir) return fa->is_dir ? -1 : 1;
    return strcasecmp(fa->name, fb->name);
}

static bool has_video_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    static const char *exts[] = {
        ".mp4", ".mkv", ".avi", ".mov", ".webm", ".mpg", ".mpeg", ".flv", ".ogv", ".m4v", ".3gp", NULL
    };
    for (int i = 0; exts[i]; i++) {
        if (strcasecmp(dot, exts[i]) == 0) return true;
    }
    return false;
}

static void scan_directory(const char *dir_path) {
    char resolved[PATH_MAX];
    if (realpath(dir_path, resolved)) {
        snprintf(g_cwd, sizeof(g_cwd), "%s", resolved);
    } else {
        snprintf(g_cwd, sizeof(g_cwd), "%s", dir_path);
    }

    g_file_count = 0;
    g_file_sel = 0;
    g_file_scroll = 0;

    if (strcmp(g_cwd, "/") != 0) {
        snprintf(g_files[g_file_count].name, sizeof(g_files[0].name), "..");
        g_files[g_file_count].is_dir = true;
        g_files[g_file_count].size = 0;
        g_file_count++;
    }

    DIR *d = opendir(g_cwd);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_file_count < MAX_FILES) {
        if (de->d_name[0] == '.') continue;
        char full[PATH_MAX + 512];
        snprintf(full, sizeof(full), "%s/%s", g_cwd, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        bool is_dir = S_ISDIR(st.st_mode);
        if (!is_dir && !has_video_extension(de->d_name)) continue;

        snprintf(g_files[g_file_count].name, sizeof(g_files[0].name), "%s", de->d_name);
        g_files[g_file_count].is_dir = is_dir;
        g_files[g_file_count].size = st.st_size;
        g_file_count++;
    }
    closedir(d);

    qsort(g_files, (size_t)g_file_count, sizeof(FileEntry), cmp_file_entry);
}

/* =========================================================================
 * FFmpeg Video/Audio Decoder & ALSA Audio Output Engine
 * ========================================================================= */
static AVFormatContext *g_fmt_ctx   = NULL;
static AVCodecContext  *g_vcodec    = NULL;
static AVCodecContext  *g_acodec    = NULL;
static struct SwsContext *g_sws_ctx = NULL;
static struct SwrContext *g_swr_ctx = NULL;
static AVFrame         *g_vframe    = NULL;
static AVFrame         *g_aframe    = NULL;
static AVPacket        *g_pkt       = NULL;

static int              g_vstream_idx = -1;
static int              g_astream_idx = -1;
static double           g_vtime_base  = 0.0;
static double           g_atime_base  = 0.0;
static double           g_duration_sec = 0.0;
static double           g_current_pts  = 0.0;
static double           g_play_start_wall = 0.0;
static double           g_play_base_pts   = 0.0;
static char             g_video_title[128] = "";

static snd_pcm_t       *g_alsa_pcm = NULL;
static int              g_audio_rate = 44100;
static uint8_t         *g_audio_buf = NULL;
static int              g_audio_buf_cap = 0;

static void close_media_file(void) {
    if (g_alsa_pcm) {
        snd_pcm_drop(g_alsa_pcm);
        snd_pcm_close(g_alsa_pcm);
        g_alsa_pcm = NULL;
    }
    if (g_sws_ctx) {
        sws_freeContext(g_sws_ctx);
        g_sws_ctx = NULL;
    }
    if (g_swr_ctx) {
        swr_free(&g_swr_ctx);
        g_swr_ctx = NULL;
    }
    if (g_vframe) {
        av_frame_free(&g_vframe);
        g_vframe = NULL;
    }
    if (g_aframe) {
        av_frame_free(&g_aframe);
        g_aframe = NULL;
    }
    if (g_pkt) {
        av_packet_free(&g_pkt);
        g_pkt = NULL;
    }
    if (g_vcodec) {
        avcodec_free_context(&g_vcodec);
        g_vcodec = NULL;
    }
    if (g_acodec) {
        avcodec_free_context(&g_acodec);
        g_acodec = NULL;
    }
    if (g_fmt_ctx) {
        avformat_close_input(&g_fmt_ctx);
        g_fmt_ctx = NULL;
    }
    free(g_audio_buf);
    g_audio_buf = NULL;
    g_audio_buf_cap = 0;
    g_vstream_idx = -1;
    g_astream_idx = -1;
}

static void convert_vframe_to_screen(AVFrame *frame) {
    if (!g_sws_ctx || !frame) return;
    uint8_t *dst_data[4] = { (uint8_t *)g_video_fb, NULL, NULL, NULL };
    int dst_linesize[4]  = { SCREEN_W * 4, 0, 0, 0 };
    sws_scale(g_sws_ctx,
              (const uint8_t * const *)frame->data,
              frame->linesize,
              0,
              g_vcodec->height,
              dst_data,
              dst_linesize);
}

static void output_decoded_audio_frame(AVFrame *frame) {
    if (!g_swr_ctx || !g_alsa_pcm || !frame) return;
    int out_samples = swr_get_out_samples(g_swr_ctx, frame->nb_samples);
    if (out_samples <= 0) return;
    int bytes_needed = out_samples * 2 * (int)sizeof(int16_t);
    if (bytes_needed > g_audio_buf_cap) {
        uint8_t *nb = (uint8_t *)realloc(g_audio_buf, (size_t)bytes_needed);
        if (!nb) return;
        g_audio_buf = nb;
        g_audio_buf_cap = bytes_needed;
    }
    uint8_t *out_planes[1] = { g_audio_buf };
    int converted = swr_convert(g_swr_ctx, out_planes, out_samples,
                                (const uint8_t **)frame->data, frame->nb_samples);
    if (converted > 0) {
        snd_pcm_sframes_t written = snd_pcm_writei(g_alsa_pcm, g_audio_buf, (snd_pcm_uframes_t)converted);
        if (written < 0) {
            snd_pcm_recover(g_alsa_pcm, (int)written, 1);
        }
    }
}

static bool step_decode_next_video_frame(bool play_audio) {
    if (!g_fmt_ctx || !g_vcodec) return false;

    while (1) {
        int ret = avcodec_receive_frame(g_vcodec, g_vframe);
        if (ret == 0) {
            int64_t ts = g_vframe->best_effort_timestamp;
            if (ts != AV_NOPTS_VALUE) {
                g_current_pts = (double)ts * g_vtime_base;
            }
            convert_vframe_to_screen(g_vframe);
            av_frame_unref(g_vframe);
            return true;
        }

        if (av_read_frame(g_fmt_ctx, g_pkt) < 0) {
            return false; /* End of file */
        }

        if (g_pkt->stream_index == g_vstream_idx) {
            avcodec_send_packet(g_vcodec, g_pkt);
        } else if (g_pkt->stream_index == g_astream_idx && g_acodec) {
            if (avcodec_send_packet(g_acodec, g_pkt) == 0) {
                while (avcodec_receive_frame(g_acodec, g_aframe) == 0) {
                    if (play_audio) {
                        output_decoded_audio_frame(g_aframe);
                    }
                    av_frame_unref(g_aframe);
                }
            }
        }
        av_packet_unref(g_pkt);
    }
}

static bool open_media_file(const char *filepath) {
    close_media_file();

    if (avformat_open_input(&g_fmt_ctx, filepath, NULL, NULL) < 0) {
        return false;
    }
    if (avformat_find_stream_info(g_fmt_ctx, NULL) < 0) {
        close_media_file();
        return false;
    }

    const char *base = strrchr(filepath, '/');
    snprintf(g_video_title, sizeof(g_video_title), "%s", base ? (base + 1) : filepath);

    for (unsigned int i = 0; i < g_fmt_ctx->nb_streams; i++) {
        AVCodecParameters *par = g_fmt_ctx->streams[i]->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && g_vstream_idx < 0) {
            g_vstream_idx = (int)i;
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO && g_astream_idx < 0) {
            g_astream_idx = (int)i;
        }
    }

    if (g_vstream_idx < 0) {
        close_media_file();
        return false;
    }

    /* Open Video Decoder */
    AVStream *vst = g_fmt_ctx->streams[g_vstream_idx];
    const AVCodec *vdec = avcodec_find_decoder(vst->codecpar->codec_id);
    if (!vdec) {
        close_media_file();
        return false;
    }
    g_vcodec = avcodec_alloc_context3(vdec);
    avcodec_parameters_to_context(g_vcodec, vst->codecpar);
    if (avcodec_open2(g_vcodec, vdec, NULL) < 0) {
        close_media_file();
        return false;
    }
    g_vtime_base = av_q2d(vst->time_base);
    g_duration_sec = (g_fmt_ctx->duration != AV_NOPTS_VALUE)
                         ? ((double)g_fmt_ctx->duration / (double)AV_TIME_BASE)
                         : 0.0;

    g_sws_ctx = sws_getContext(
        g_vcodec->width, g_vcodec->height, g_vcodec->pix_fmt,
        SCREEN_W, SCREEN_H, AV_PIX_FMT_BGRA,
        SWS_FAST_BILINEAR, NULL, NULL, NULL
    );

    /* Open Audio Decoder + ALSA PCM if audio stream is present */
    if (g_astream_idx >= 0) {
        AVStream *ast = g_fmt_ctx->streams[g_astream_idx];
        const AVCodec *adec = avcodec_find_decoder(ast->codecpar->codec_id);
        if (adec) {
            g_acodec = avcodec_alloc_context3(adec);
            avcodec_parameters_to_context(g_acodec, ast->codecpar);
            if (avcodec_open2(g_acodec, adec, NULL) == 0) {
                g_atime_base = av_q2d(ast->time_base);
                g_audio_rate = (g_acodec->sample_rate > 0) ? g_acodec->sample_rate : 44100;

#if LIBSWRESAMPLE_VERSION_INT >= AV_VERSION_INT(4, 5, 100)
                AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
                swr_alloc_set_opts2(&g_swr_ctx,
                                    &out_layout, AV_SAMPLE_FMT_S16, g_audio_rate,
                                    &g_acodec->ch_layout, g_acodec->sample_fmt, g_acodec->sample_rate,
                                    0, NULL);
#else
                int64_t in_layout = g_acodec->channel_layout
                                        ? (int64_t)g_acodec->channel_layout
                                        : av_get_default_channel_layout(g_acodec->channels);
                g_swr_ctx = swr_alloc_set_opts(NULL,
                                               AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, g_audio_rate,
                                               in_layout, g_acodec->sample_fmt, g_acodec->sample_rate,
                                               0, NULL);
#endif
                if (g_swr_ctx && swr_init(g_swr_ctx) >= 0) {
                    if (snd_pcm_open(&g_alsa_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) == 0) {
                        snd_pcm_set_params(g_alsa_pcm,
                                           SND_PCM_FORMAT_S16_LE,
                                           SND_PCM_ACCESS_RW_INTERLEAVED,
                                           2,
                                           (unsigned int)g_audio_rate,
                                           1,
                                           100000);
                    } else {
                        g_alsa_pcm = NULL;
                    }
                }
            }
        }
    }

    g_vframe = av_frame_alloc();
    g_aframe = av_frame_alloc();
    g_pkt    = av_packet_alloc();
    g_current_pts = 0.0;

    memset(g_video_fb, 0, sizeof(g_video_fb));
    step_decode_next_video_frame(false);
    g_play_base_pts   = g_current_pts;
    g_play_start_wall = get_wall_time_sec();
    return true;
}

static void seek_media_relative(double delta_sec) {
    if (!g_fmt_ctx || g_vstream_idx < 0) return;
    double target = g_current_pts + delta_sec;
    if (target < 0.0) target = 0.0;
    if (g_duration_sec > 0.0 && target > g_duration_sec - 0.5) {
        target = g_duration_sec - 0.5;
        if (target < 0.0) target = 0.0;
    }
    int64_t ts = (int64_t)(target / g_vtime_base);
    int flags = (delta_sec < 0.0) ? AVSEEK_FLAG_BACKWARD : 0;
    if (av_seek_frame(g_fmt_ctx, g_vstream_idx, ts, flags) >= 0) {
        avcodec_flush_buffers(g_vcodec);
        if (g_acodec) avcodec_flush_buffers(g_acodec);
        if (g_alsa_pcm) snd_pcm_prepare(g_alsa_pcm);
        step_decode_next_video_frame(false);
        g_play_base_pts   = g_current_pts;
        g_play_start_wall = get_wall_time_sec();
    }
}

/* =========================================================================
 * UI Rendering: /mnt/media File Browser, Full-Screen Video, Pause Overlay
 * ========================================================================= */
static void render_browser_view(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(15, 21, 33));

    /* Top Header (y=0..23) */
    fill_rect(0, 0, SCREEN_W, 23, RGB(24, 34, 54));
    fill_rect(0, 23, SCREEN_W, 1, RGB(56, 78, 112));
    draw_pill(5, 4, 48, 15, RGB(37, 99, 235), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_10, 5, 4, 48, 15, "VIDEO", RGB(255, 255, 255));
    {
        int max_pw = SCREEN_W - 64;
        if (text_width(FONT_UI_12, g_cwd) <= max_pw) {
            draw_text(FONT_UI_12, 59, 4, g_cwd, RGB(226, 232, 240));
        } else {
            char pbuf[256];
            size_t clen = strlen(g_cwd);
            size_t off = 1;
            while (off < clen) {
                snprintf(pbuf, sizeof(pbuf), "...%s", g_cwd + off);
                if (text_width(FONT_UI_12, pbuf) <= max_pw) break;
                off++;
            }
            draw_text(FONT_UI_12, 59, 4, pbuf, RGB(226, 232, 240));
        }
    }

    /* File Rows (y=26..222) */
    int row_h = 21;
    for (int i = 0; i < BROWSER_ROWS; i++) {
        int idx = g_file_scroll + i;
        if (idx >= g_file_count) break;
        int ry = 27 + i * row_h;
        bool sel = (idx == g_file_sel);

        if (sel) {
            fill_rect(4, ry, SCREEN_W - 8, row_h - 1, RGB(37, 62, 102));
            draw_border(4, ry, SCREEN_W - 8, row_h - 1, RGB(250, 204, 21));
        } else if (i % 2 == 1) {
            fill_rect(4, ry, SCREEN_W - 8, row_h - 1, RGB(19, 27, 42));
        }

        if (g_files[idx].is_dir) {
            draw_pill(8, ry + 3, 32, 14, RGB(30, 58, 95), RGB(56, 189, 248));
            draw_text_centered(FONT_UI_10, 8, ry + 3, 32, 14, "DIR", RGB(125, 211, 252));
            draw_text_clipped(FONT_UI_12, 46, ry + 2, SCREEN_W - 56,
                              g_files[idx].name, sel ? RGB(255, 255, 255) : RGB(226, 232, 240));
        } else {
            draw_pill(8, ry + 3, 36, 14, RGB(14, 116, 144), RGB(56, 189, 248));
            draw_text_centered(FONT_UI_10, 8, ry + 3, 36, 14, "MP4", RGB(224, 242, 254));

            char sz_str[24];
            if (g_files[idx].size >= 1024 * 1024) {
                snprintf(sz_str, sizeof(sz_str), "%.1fM", (double)g_files[idx].size / (1024.0 * 1024.0));
            } else {
                snprintf(sz_str, sizeof(sz_str), "%.1fK", (double)g_files[idx].size / 1024.0);
            }
            int sz_w = text_width(FONT_UI_10, sz_str);
            draw_text_right_aligned(FONT_UI_10, SCREEN_W - 10, ry + 4, sz_str, RGB(148, 163, 184));
            draw_text_clipped(FONT_UI_12, 50, ry + 2, SCREEN_W - 66 - sz_w,
                              g_files[idx].name, sel ? RGB(255, 255, 255) : RGB(241, 245, 249));
        }
    }

    /* Bottom Bar (y=225..239) */
    fill_rect(0, 225, SCREEN_W, 15, RGB(10, 14, 23));
    fill_rect(0, 225, SCREEN_W, 1, RGB(30, 41, 59));
    draw_text_centered(FONT_UI_10, 0, 226, SCREEN_W, 14,
                       "UP/DN:Move  A:Play Video  B:Up Dir  START:Exit",
                       RGB(148, 163, 184));
}

static void format_time_mmss(double sec, char *out, size_t out_sz) {
    if (sec < 0.0) sec = 0.0;
    int total = (int)(sec + 0.5);
    int mins = total / 60;
    int secs = total % 60;
    snprintf(out, out_sz, "%02d:%02d", mins, secs);
}

static void render_paused_help_overlay(void) {
    /* First copy the full-screen 320x240 video frame */
    memcpy(g_fb, g_video_fb, sizeof(g_fb));

    /* Compact translucent device help panel in lower half of screen */
    int mx = 14, my = 104, mw = SCREEN_W - 28, mh = 124;
    fill_rect_alpha(mx, my, mw, mh, RGB(12, 18, 30), 228);
    draw_border(mx, my, mw, mh, RGB(96, 165, 250));
    draw_border(mx + 1, my + 1, mw - 2, mh - 2, RGB(37, 99, 235));

    /* Header row: PAUSED badge + video filename + timestamp */
    draw_pill(mx + 8, my + 6, 52, 15, RGB(217, 119, 6), RGB(251, 191, 36));
    draw_text_centered(FONT_UI_10, mx + 8, my + 6, 52, 15, "PAUSED", RGB(255, 255, 255));

    char cur_s[16], dur_s[16], t_str[40];
    format_time_mmss(g_current_pts, cur_s, sizeof(cur_s));
    format_time_mmss(g_duration_sec, dur_s, sizeof(dur_s));
    snprintf(t_str, sizeof(t_str), "%s / %s", cur_s, dur_s);
    int tw = text_width(FONT_UI_10, t_str);
    draw_text_right_aligned(FONT_UI_10, mx + mw - 8, my + 8, t_str, RGB(250, 204, 21));

    draw_text_clipped(FONT_UI_12, mx + 66, my + 6, mw - 78 - tw, g_video_title, RGB(248, 250, 252));

    /* Progress Bar */
    int bx = mx + 8, by = my + 25, bw = mw - 16, bh = 6;
    fill_rect(bx, by, bw, bh, RGB(15, 23, 42));
    draw_border(bx, by, bw, bh, RGB(71, 85, 105));
    int fill_w = (g_duration_sec > 0.0)
                     ? (int)(((bw - 2) * g_current_pts) / g_duration_sec)
                     : 0;
    if (fill_w < 2) fill_w = 2;
    if (fill_w > bw - 2) fill_w = bw - 2;
    fill_rect(bx + 1, by + 1, fill_w, bh - 2, RGB(56, 189, 248));

    /* Device Controls Help */
    int ky = my + 36;
    static const struct {
        const char *key;
        const char *desc;
    } kmaps[] = {
        { "A",            "Play / Resume Video"     },
        { "LEFT / RIGHT", "Seek -5s / +5s"          },
        { "B / X",        "Back to /mnt/media"      },
        { "START / MENU", "Exit Video Player"       }
    };

    for (int i = 0; i < 4; i++) {
        int ry = ky + i * 20;
        draw_pill(mx + 8, ry, 84, 15, RGB(30, 58, 95), RGB(56, 189, 248));
        draw_text_centered(FONT_UI_10, mx + 8, ry, 84, 15, kmaps[i].key, RGB(254, 240, 138));
        draw_text(FONT_UI_12, mx + 100, ry, kmaps[i].desc, RGB(226, 232, 240));
    }
}

static void render_ui(SDL_Surface *screen) {
    switch (g_state) {
        case STATE_BROWSER:
            render_browser_view();
            break;
        case STATE_PLAYING:
            /* Entire 320x240 screen shows the video with nothing else on screen */
            memcpy(g_fb, g_video_fb, sizeof(g_fb));
            break;
        case STATE_PAUSED:
            render_paused_help_overlay();
            break;
    }
    present_frame(screen);
}

/* =========================================================================
 * Main Entry Point & Event Loop
 * ========================================================================= */
int main(int argc, char *argv[]) {
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
#endif
    putenv((char *)"SDL_NOMOUSE=1");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL video: %s\n", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);

    SDL_Surface *screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, SCREEN_BPP, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "Failed to set video mode: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_WM_SetCaption("PowKiddy V90 Video Player", NULL);

    /* Determine starting directory (/mnt/media on PowKiddy V90 SD card) */
    struct stat st;
    if (argc > 1 && stat(argv[1], &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            scan_directory(argv[1]);
        } else {
            char dir_buf[PATH_MAX];
            extract_dirname(argv[1], dir_buf, sizeof(dir_buf));
            scan_directory(dir_buf[0] ? dir_buf : ".");
            if (open_media_file(argv[1])) {
                g_state = STATE_PLAYING;
            }
        }
    } else if (stat("/mnt/media", &st) == 0 && S_ISDIR(st.st_mode)) {
        scan_directory("/mnt/media");
    } else if (stat("./media", &st) == 0 && S_ISDIR(st.st_mode)) {
        scan_directory("./media");
    } else if (stat("/mnt", &st) == 0 && S_ISDIR(st.st_mode)) {
        scan_directory("/mnt");
    } else {
        scan_directory(".");
    }

    /* Automated demo screenshot generation mode if VIDEO_DEMO_DIR is set */
    const char *demo_dir = getenv("VIDEO_DEMO_DIR");
    if (demo_dir && demo_dir[0] != '\0') {
        char path[512];

        /* 1. /mnt/media File Browser screenshot */
        g_state = STATE_BROWSER;
        for (int i = 0; i < g_file_count; i++) {
            if (!g_files[i].is_dir) { g_file_sel = i; break; }
        }
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/video_browser.bmp", demo_dir);
        save_bmp(path);

        /* 2. Full-Screen Playing View & Paused Help Overlay screenshots */
        if (g_file_sel < g_file_count && !g_files[g_file_sel].is_dir) {
            char full_video[PATH_MAX + 512];
            snprintf(full_video, sizeof(full_video), "%s/%s", g_cwd, g_files[g_file_sel].name);
            if (open_media_file(full_video)) {
                /* Advance ~45 frames (1.5 seconds) into the video */
                for (int f = 0; f < 45; f++) {
                    step_decode_next_video_frame(false);
                }
                g_state = STATE_PLAYING;
                render_ui(screen);
                snprintf(path, sizeof(path), "%s/video_playing.bmp", demo_dir);
                save_bmp(path);

                g_state = STATE_PAUSED;
                render_ui(screen);
                snprintf(path, sizeof(path), "%s/video_paused.bmp", demo_dir);
                save_bmp(path);
            }
        }

        close_media_file();
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

                /* START or MENU (or Q/Esc on desktop) exits the player */
                if (k == V90_BTN_START || k == V90_BTN_MENU || k == SDLK_q) {
                    running = false;
                    break;
                }

                if (g_state == STATE_BROWSER) {
                    if (k == V90_BTN_UP) {
                        if (g_file_sel > 0) g_file_sel--;
                        if (g_file_sel < g_file_scroll) g_file_scroll = g_file_sel;
                    } else if (k == V90_BTN_DOWN) {
                        if (g_file_sel + 1 < g_file_count) g_file_sel++;
                        if (g_file_sel >= g_file_scroll + BROWSER_ROWS) {
                            g_file_scroll = g_file_sel - BROWSER_ROWS + 1;
                        }
                    } else if (k == V90_BTN_A || k == SDLK_a || k == SDLK_SPACE) {
                        if (g_file_sel >= 0 && g_file_sel < g_file_count) {
                            char target[PATH_MAX + 512];
                            snprintf(target, sizeof(target), "%s/%s", g_cwd, g_files[g_file_sel].name);
                            if (g_files[g_file_sel].is_dir) {
                                scan_directory(target);
                            } else if (open_media_file(target)) {
                                g_state = STATE_PLAYING;
                            }
                        }
                    } else if (k == V90_BTN_B || k == SDLK_b) {
                        if (strcmp(g_cwd, "/") != 0) {
                            char parent[PATH_MAX + 32];
                            snprintf(parent, sizeof(parent), "%s/..", g_cwd);
                            scan_directory(parent);
                        }
                    }
                } else if (g_state == STATE_PLAYING) {
                    /* A button: Pause video and show device help overlay */
                    if (k == V90_BTN_A || k == SDLK_a || k == V90_BTN_Y) {
                        g_state = STATE_PAUSED;
                    } else if (k == V90_BTN_LEFT || k == V90_BTN_L1) {
                        seek_media_relative(-5.0);
                    } else if (k == V90_BTN_RIGHT || k == V90_BTN_R1) {
                        seek_media_relative(5.0);
                    } else if (k == V90_BTN_B || k == SDLK_b || k == V90_BTN_X || k == SDLK_x) {
                        close_media_file();
                        g_state = STATE_BROWSER;
                    }
                } else if (g_state == STATE_PAUSED) {
                    /* A button: Resume playback */
                    if (k == V90_BTN_A || k == SDLK_a || k == V90_BTN_Y) {
                        g_play_base_pts   = g_current_pts;
                        g_play_start_wall = get_wall_time_sec();
                        g_state = STATE_PLAYING;
                    } else if (k == V90_BTN_LEFT || k == V90_BTN_L1) {
                        seek_media_relative(-5.0);
                    } else if (k == V90_BTN_RIGHT || k == V90_BTN_R1) {
                        seek_media_relative(5.0);
                    } else if (k == V90_BTN_B || k == SDLK_b || k == V90_BTN_X || k == SDLK_x) {
                        close_media_file();
                        g_state = STATE_BROWSER;
                    }
                }
            }
        }

        if (g_state == STATE_PLAYING) {
            double elapsed = get_wall_time_sec() - g_play_start_wall;
            double target_pts = g_play_base_pts + elapsed;
            if (g_current_pts <= target_pts) {
                if (!step_decode_next_video_frame(true)) {
                    /* Reached end of video: return to /mnt/media browser */
                    close_media_file();
                    g_state = STATE_BROWSER;
                }
            }
            render_ui(screen);
            SDL_Delay(4);
        } else {
            render_ui(screen);
            SDL_Delay(16);
        }
    }

    close_media_file();
    SDL_Quit();
    return 0;
}
