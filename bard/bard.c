/*
 * bard - Lightweight EPUB & Text Reader for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host
 *
 * Replaces the stock MiyooCFW 1.3.3 /mnt/apps/bard/bard reader with:
 *   1. Text-only EPUB 2 / EPUB 3 rendering (images/styles/scripts ignored) via libzip + libxml2.
 *   2. Chapter List (Table of Contents) modal toggled by SELECT, navigated with DPAD UP/DOWN,
 *      and selected with the A button.
 *   3. Strict word-wrapped layout (320px width, zero horizontal scrolling).
 *   4. Static whole-page vertical scrolling on DPAD UP / DPAD DOWN (no automated scrolling).
 *   5. Startup file browser opening directly in /mnt/media on the SD card.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>

#include <zip.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>

#include "bard_font.h"

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
#define SDLK_HOME       XK_Home
#define SDLK_PAGEUP     XK_Page_Up
#define SDLK_PAGEDOWN   XK_Page_Down
#define SDLK_RETURN     XK_Return
#define SDLK_RCTRL      XK_Control_R
#define SDLK_UP         XK_Up
#define SDLK_DOWN       XK_Down
#define SDLK_LEFT       XK_Left
#define SDLK_RIGHT      XK_Right
#define SDLK_a          XK_a
#define SDLK_b          XK_b
#define SDLK_c          XK_c
#define SDLK_h          XK_h
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
 * PowKiddy V90 Button Definitions (MiyooCFW 1.3.3)
 * ========================================================================= */
#define V90_BTN_UP      SDLK_UP
#define V90_BTN_DOWN    SDLK_DOWN
#define V90_BTN_LEFT    SDLK_LEFT
#define V90_BTN_RIGHT   SDLK_RIGHT
#define V90_BTN_A       SDLK_LALT       /* East button (A): Select chapter / Open file */
#define V90_BTN_B       SDLK_LCTRL      /* South button (B): Back / Close modal */
#define V90_BTN_X       SDLK_LSHIFT     /* North button (X): Return to file browser */
#define V90_BTN_Y       SDLK_SPACE      /* West button (Y): Toggle Paper / Dark theme */
#define V90_BTN_L1      SDLK_TAB        /* Left shoulder: Prev page */
#define V90_BTN_R1      SDLK_BACKSPACE  /* Right shoulder: Next page */
#define V90_BTN_SELECT  SDLK_ESCAPE     /* SELECT: Open/Close Chapter List */
#define V90_BTN_START   SDLK_RETURN     /* START: Quit */
#define V90_BTN_MENU    SDLK_RCTRL      /* MENU: Quit */

/* =========================================================================
 * 24-bit TrueColor Backbuffer & Anti-Aliased Software Renderer
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
 * Data Structures: File Browser, EPUB Book, Chapters, and Wrapped Lines
 * ========================================================================= */
typedef enum {
    STATE_FILE_BROWSER = 0,
    STATE_READING      = 1,
    STATE_CHAPTER_LIST = 2,
    STATE_HELP         = 3
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

#define MAX_MANIFEST 512
typedef struct {
    char id[128];
    char href[256];
    char media_type[64];
    bool is_nav;
} ManifestItem;

#define MAX_CHAPTERS 256
typedef struct {
    char title[128];
    char href[256];
    char frag[64];
    char end_frag[64];
    int  start_spine;
    int  end_spine;
    bool use_start_frag;
    int  page_count;
} EpubChapter;

typedef struct {
    char *text;
    bool is_heading;
    bool is_blank;
} WrappedLine;

#define LINES_PER_PAGE 12
#define TEXT_MARGIN_X  12
#define TEXT_MAX_WIDTH (SCREEN_W - 2 * TEXT_MARGIN_X)

static zip_t        *g_epub_zip = NULL;
static bool          g_is_plain_txt = false;
static char          g_plain_txt_path[PATH_MAX];
static char          g_book_title[128] = "";
static char          g_book_author[128] = "";
static char          g_spine_hrefs[MAX_MANIFEST][256];
static int           g_spine_count = 0;
static EpubChapter   g_chapters[MAX_CHAPTERS];
static int           g_chapter_count = 0;
static int           g_cur_chapter = 0;
static int           g_book_total_pages = 1;
static int           g_toc_sel = 0;
static int           g_toc_scroll = 0;
#define TOC_ROWS 8

static WrappedLine  *g_lines = NULL;
static int           g_line_count = 0;
static int           g_line_cap = 0;
static int           g_cur_page = 0;
static int           g_total_pages = 1;
static bool          g_dark_theme = false;
static AppState      g_state = STATE_FILE_BROWSER;

/* =========================================================================
 * UTF-8 Normalization & Word-Wrapping Paginator (Zero Horizontal Scroll)
 * ========================================================================= */
static void free_wrapped_lines(void) {
    if (g_lines) {
        for (int i = 0; i < g_line_count; i++) {
            free(g_lines[i].text);
        }
        free(g_lines);
        g_lines = NULL;
    }
    g_line_count = 0;
    g_line_cap = 0;
    g_cur_page = 0;
    g_total_pages = 1;
}

static void append_wrapped_line(const char *s, size_t len, bool is_heading, bool is_blank) {
    if (is_blank && g_line_count == 0) return;
    if (is_blank && g_line_count > 0 && g_lines[g_line_count - 1].is_blank) return;
    /* Avoid blank line at the very top of a new page */
    if (is_blank && (g_line_count % LINES_PER_PAGE) == 0) return;

    if (g_line_count >= g_line_cap) {
        int new_cap = (g_line_cap == 0) ? 256 : (g_line_cap * 2);
        WrappedLine *nl = (WrappedLine *)realloc(g_lines, (size_t)new_cap * sizeof(WrappedLine));
        if (!nl) return;
        g_lines = nl;
        g_line_cap = new_cap;
    }
    char *copy = (char *)malloc(len + 1);
    if (!copy) return;
    memcpy(copy, s, len);
    copy[len] = '\0';
    g_lines[g_line_count].text = copy;
    g_lines[g_line_count].is_heading = is_heading;
    g_lines[g_line_count].is_blank = is_blank;
    g_line_count++;
}

/* Convert common UTF-8 typographic sequences into clean ASCII equivalents */
static void normalize_utf8_text(const char *in, char *out, size_t out_sz) {
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in;
    bool last_space = true;

    while (*p && o + 4 < out_sz) {
        if (*p < 0x80) {
            char c = (char)*p++;
            if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
                if (!last_space) {
                    out[o++] = ' ';
                    last_space = true;
                }
            } else if (c >= 32 && c <= 126) {
                out[o++] = c;
                last_space = false;
            }
        } else if (p[0] == 0xC2 && p[1] == 0xA0) {
            /* Non-breaking space */
            if (!last_space) {
                out[o++] = ' ';
                last_space = true;
            }
            p += 2;
        } else if (p[0] == 0xC2 || p[0] == 0xC3) {
            /* Latin-1 supplement characters (e.g. accented vowels) */
            unsigned int cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            char mapped = '?';
            if (cp >= 0xC0 && cp <= 0xC5) mapped = 'A';
            else if (cp == 0xC7) mapped = 'C';
            else if (cp >= 0xC8 && cp <= 0xCB) mapped = 'E';
            else if (cp >= 0xCC && cp <= 0xCF) mapped = 'I';
            else if (cp == 0xD1) mapped = 'N';
            else if (cp >= 0xD2 && cp <= 0xD6) mapped = 'O';
            else if (cp >= 0xD9 && cp <= 0xDC) mapped = 'U';
            else if (cp >= 0xE0 && cp <= 0xE5) mapped = 'a';
            else if (cp == 0xE7) mapped = 'c';
            else if (cp >= 0xE8 && cp <= 0xEB) mapped = 'e';
            else if (cp >= 0xEC && cp <= 0xEF) mapped = 'i';
            else if (cp == 0xF1) mapped = 'n';
            else if (cp >= 0xF2 && cp <= 0xF6) mapped = 'o';
            else if (cp >= 0xF9 && cp <= 0xFC) mapped = 'u';
            else if (cp == 0xA9) mapped = 'c';
            else if (cp == 0xAB || cp == 0xBB) mapped = '"';
            if (mapped != '?') {
                out[o++] = mapped;
                last_space = false;
            }
            p += 2;
        } else if (p[0] == 0xE2 && p[1] == 0x80) {
            unsigned char b2 = p[2];
            if (b2 == 0x93 || b2 == 0x94) {
                /* en-dash / em-dash */
                out[o++] = '-';
                out[o++] = '-';
                last_space = false;
            } else if (b2 == 0x98 || b2 == 0x99) {
                /* curly single quotes */
                out[o++] = '\'';
                last_space = false;
            } else if (b2 == 0x9C || b2 == 0x9D) {
                /* curly double quotes */
                out[o++] = '"';
                last_space = false;
            } else if (b2 == 0xA6) {
                /* ellipsis */
                out[o++] = '.';
                out[o++] = '.';
                out[o++] = '.';
                last_space = false;
            } else if (b2 == 0xA2) {
                out[o++] = '*';
                last_space = false;
            }
            p += 3;
        } else if ((p[0] & 0xE0) == 0xC0 && p[1]) {
            p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
            p += 3;
        } else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            p += 4;
        } else {
            p++;
        }
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = '\0';
}

/* Word-wrap a single paragraph strictly within TEXT_MAX_WIDTH (296 px) */
static void wrap_paragraph_ex(const char *raw_text, bool is_heading, bool add_blank_after) {
    if (!raw_text || !*raw_text) return;
    size_t raw_len = strlen(raw_text);
    size_t norm_sz = raw_len * 2 + 16;
    char *norm = (char *)malloc(norm_sz);
    if (!norm) return;
    normalize_utf8_text(raw_text, norm, norm_sz);
    if (norm[0] == '\0') {
        free(norm);
        return;
    }

    int font_id = is_heading ? FONT_BOOK_BOLD16 : FONT_BOOK_15;
    int space_w = char_advance(font_id, ' ');

    char line_buf[256];
    size_t line_len = 0;
    int line_w = 0;

    const char *p = norm;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char *w_start = p;
        int word_w = 0;
        while (*p && *p != ' ') {
            word_w += char_advance(font_id, *p);
            p++;
        }
        size_t word_len = (size_t)(p - w_start);

        /* If a single word exceeds TEXT_MAX_WIDTH, break it across lines */
        if (word_w > TEXT_MAX_WIDTH) {
            if (line_len > 0) {
                append_wrapped_line(line_buf, line_len, is_heading, false);
                line_len = 0;
                line_w = 0;
            }
            size_t wi = 0;
            while (wi < word_len) {
                line_len = 0;
                line_w = 0;
                while (wi < word_len) {
                    int cw = char_advance(font_id, w_start[wi]);
                    if (line_w + cw > TEXT_MAX_WIDTH && line_len > 0) break;
                    if (line_len + 1 < sizeof(line_buf)) {
                        line_buf[line_len++] = w_start[wi];
                    }
                    line_w += cw;
                    wi++;
                }
                append_wrapped_line(line_buf, line_len, is_heading, false);
            }
            line_len = 0;
            line_w = 0;
            continue;
        }

        int needed_w = (line_len == 0) ? word_w : (line_w + space_w + word_w);
        if (needed_w <= TEXT_MAX_WIDTH && (line_len + 1 + word_len < sizeof(line_buf))) {
            if (line_len > 0) {
                line_buf[line_len++] = ' ';
                line_w += space_w;
            }
            memcpy(line_buf + line_len, w_start, word_len);
            line_len += word_len;
            line_w += word_w;
        } else {
            append_wrapped_line(line_buf, line_len, is_heading, false);
            memcpy(line_buf, w_start, word_len);
            line_len = word_len;
            line_w = word_w;
        }
    }

    if (line_len > 0) {
        append_wrapped_line(line_buf, line_len, is_heading, false);
    }
    if (add_blank_after) {
        /* Add a visual paragraph separator line */
        append_wrapped_line("", 0, false, true);
    }

    free(norm);
}

static void wrap_paragraph(const char *raw_text, bool is_heading) {
    wrap_paragraph_ex(raw_text, is_heading, true);
}

/* =========================================================================
 * EPUB Archive & XML/XHTML Parser (libzip + libxml2)
 * ========================================================================= */
static char *read_zip_entry(zip_t *za, const char *entry_path, size_t *out_size) {
    if (!za || !entry_path || !*entry_path) return NULL;
    struct zip_stat st;
    zip_stat_init(&st);
    if (zip_stat(za, entry_path, 0, &st) != 0) {
        /* Try case-insensitive fallback scan */
        zip_int64_t n = zip_get_num_entries(za, 0);
        zip_int64_t found_idx = -1;
        for (zip_int64_t i = 0; i < n; i++) {
            const char *name = zip_get_name(za, (zip_uint64_t)i, 0);
            if (name && strcasecmp(name, entry_path) == 0) {
                found_idx = i;
                break;
            }
        }
        if (found_idx < 0 || zip_stat_index(za, (zip_uint64_t)found_idx, 0, &st) != 0) {
            return NULL;
        }
    }
    zip_file_t *zf = zip_fopen_index(za, st.index, 0);
    if (!zf) return NULL;
    size_t sz = (size_t)st.size;
    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        zip_fclose(zf);
        return NULL;
    }
    size_t total = 0;
    while (total < sz) {
        zip_int64_t r = zip_fread(zf, buf + total, sz - total);
        if (r <= 0) break;
        total += (size_t)r;
    }
    buf[total] = '\0';
    zip_fclose(zf);
    if (out_size) *out_size = total;
    return buf;
}

static void extract_fragment(const char *href, char *frag_out, size_t frag_sz) {
    frag_out[0] = '\0';
    if (!href) return;
    const char *hash = strchr(href, '#');
    if (hash && hash[1] != '\0') {
        snprintf(frag_out, frag_sz, "%s", hash + 1);
    }
}

static void resolve_relative_path(const char *base_dir, const char *href, char *out, size_t out_sz) {
    if (!href) {
        out[0] = '\0';
        return;
    }
    /* Strip URL fragment (#...) and decode %20 */
    char clean_href[256];
    size_t ci = 0;
    for (size_t i = 0; href[i] && href[i] != '#' && ci + 1 < sizeof(clean_href); i++) {
        if (href[i] == '%' && href[i + 1] == '2' && href[i + 2] == '0') {
            clean_href[ci++] = ' ';
            i += 2;
        } else {
            clean_href[ci++] = href[i];
        }
    }
    clean_href[ci] = '\0';

    char combined[512];
    if (base_dir && base_dir[0] != '\0' && clean_href[0] != '/') {
        snprintf(combined, sizeof(combined), "%s/%s", base_dir, clean_href);
    } else {
        const char *s = (clean_href[0] == '/') ? (clean_href + 1) : clean_href;
        snprintf(combined, sizeof(combined), "%s", s);
    }

    /* Normalize '.' and '..' path components */
    char *parts[64];
    int nparts = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(combined, "/", &saveptr);
    while (tok) {
        if (strcmp(tok, ".") == 0) {
            /* skip */
        } else if (strcmp(tok, "..") == 0) {
            if (nparts > 0) nparts--;
        } else if (nparts < 64) {
            parts[nparts++] = tok;
        }
        tok = strtok_r(NULL, "/", &saveptr);
    }

    out[0] = '\0';
    size_t pos = 0;
    for (int i = 0; i < nparts; i++) {
        int written = snprintf(out + pos, out_sz - pos, "%s%s", (i > 0) ? "/" : "", parts[i]);
        if (written < 0 || (size_t)written >= out_sz - pos) break;
        pos += (size_t)written;
    }
}

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

/* Recursively walk XHTML DOM to extract text paragraphs while ignoring images/scripts/styles */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    const char *start_frag;
    const char *end_frag;
    bool active;
    bool stopped;
} StrBuf;

static void sb_append(StrBuf *sb, const char *s) {
    if (!sb->active || sb->stopped || !s || !*s) return;
    size_t slen = strlen(s);
    if (sb->len + slen + 2 >= sb->cap) {
        size_t ncap = (sb->cap == 0) ? 1024 : (sb->cap * 2 + slen);
        char *nb = (char *)realloc(sb->buf, ncap);
        if (!nb) return;
        sb->buf = nb;
        sb->cap = ncap;
    }
    memcpy(sb->buf + sb->len, s, slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
}

static void sb_flush_ex(StrBuf *sb, bool is_heading, bool add_blank_after) {
    if (sb->len > 0 && sb->buf) {
        wrap_paragraph_ex(sb->buf, is_heading, add_blank_after);
        sb->len = 0;
        sb->buf[0] = '\0';
    }
}

static void sb_flush(StrBuf *sb, bool is_heading) {
    sb_flush_ex(sb, is_heading, true);
}

static bool is_ignored_tag(const char *name) {
    return (strcasecmp(name, "head") == 0 ||
            strcasecmp(name, "style") == 0 ||
            strcasecmp(name, "script") == 0 ||
            strcasecmp(name, "img") == 0 ||
            strcasecmp(name, "svg") == 0 ||
            strcasecmp(name, "image") == 0 ||
            strcasecmp(name, "audio") == 0 ||
            strcasecmp(name, "video") == 0);
}

static bool is_heading_tag(const char *name) {
    return (strcasecmp(name, "h1") == 0 ||
            strcasecmp(name, "h2") == 0 ||
            strcasecmp(name, "h3") == 0 ||
            strcasecmp(name, "h4") == 0);
}

static bool is_block_tag(const char *name) {
    return (is_heading_tag(name) ||
            strcasecmp(name, "h5") == 0 ||
            strcasecmp(name, "h6") == 0 ||
            strcasecmp(name, "p") == 0 ||
            strcasecmp(name, "div") == 0 ||
            strcasecmp(name, "li") == 0 ||
            strcasecmp(name, "blockquote") == 0 ||
            strcasecmp(name, "pre") == 0 ||
            strcasecmp(name, "tr") == 0 ||
            strcasecmp(name, "section") == 0 ||
            strcasecmp(name, "article") == 0 ||
            strcasecmp(name, "header") == 0);
}

static bool node_matches_frag(xmlNode *node, const char *frag) {
    if (!node || !frag || !*frag || node->type != XML_ELEMENT_NODE) return false;
    xmlChar *id = xmlGetProp(node, (const xmlChar *)"id");
    if (id) {
        bool match = (strcmp((const char *)id, frag) == 0);
        xmlFree(id);
        if (match) return true;
    }
    xmlChar *nm = xmlGetProp(node, (const xmlChar *)"name");
    if (nm) {
        bool match = (strcmp((const char *)nm, frag) == 0);
        xmlFree(nm);
        if (match) return true;
    }
    return false;
}

static bool dom_has_frag(xmlNode *node, const char *frag) {
    if (!frag || !*frag) return false;
    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (node_matches_frag(cur, frag)) return true;
        if (cur->children && dom_has_frag(cur->children, frag)) return true;
    }
    return false;
}

static void walk_xhtml_nodes(xmlNode *node, StrBuf *sb, bool in_heading) {
    for (xmlNode *cur = node; cur && !sb->stopped; cur = cur->next) {
        if (cur->type == XML_TEXT_NODE || cur->type == XML_CDATA_SECTION_NODE) {
            if (cur->content) {
                sb_append(sb, (const char *)cur->content);
            }
        } else if (cur->type == XML_ENTITY_REF_NODE && cur->name) {
            const char *ename = (const char *)cur->name;
            if (strcasecmp(ename, "nbsp") == 0) sb_append(sb, " ");
            else if (strcasecmp(ename, "mdash") == 0 || strcasecmp(ename, "ndash") == 0) sb_append(sb, "--");
            else if (strcasecmp(ename, "ldquo") == 0 || strcasecmp(ename, "rdquo") == 0 || strcasecmp(ename, "quot") == 0) sb_append(sb, "\"");
            else if (strcasecmp(ename, "lsquo") == 0 || strcasecmp(ename, "rsquo") == 0 || strcasecmp(ename, "apos") == 0) sb_append(sb, "'");
            else if (strcasecmp(ename, "hellip") == 0) sb_append(sb, "...");
            else if (strcasecmp(ename, "amp") == 0) sb_append(sb, "&");
            else if (strcasecmp(ename, "lt") == 0) sb_append(sb, "<");
            else if (strcasecmp(ename, "gt") == 0) sb_append(sb, ">");
        } else if (cur->type == XML_ELEMENT_NODE && cur->name) {
            if (sb->active && sb->end_frag && sb->end_frag[0] != '\0' && node_matches_frag(cur, sb->end_frag)) {
                sb_flush(sb, in_heading);
                sb->stopped = true;
                return;
            }
            if (!sb->active && sb->start_frag && sb->start_frag[0] != '\0' && node_matches_frag(cur, sb->start_frag)) {
                sb->active = true;
            }

            const char *tname = (const char *)cur->name;
            if (is_ignored_tag(tname)) {
                continue;
            }
            if (strcasecmp(tname, "br") == 0) {
                sb_flush_ex(sb, in_heading, false);
                continue;
            }
            if (strcasecmp(tname, "hr") == 0) {
                sb_flush_ex(sb, in_heading, true);
                continue;
            }
            bool block = is_block_tag(tname);
            bool heading = is_heading_tag(tname) || in_heading;
            if (block) {
                sb_flush(sb, in_heading);
                if (strcasecmp(tname, "li") == 0) {
                    sb_append(sb, "* ");
                }
            }
            walk_xhtml_nodes(cur->children, sb, heading);
            if (block) {
                sb_flush(sb, heading);
            }
        }
    }
}

static void find_first_heading_in_dom(xmlNode *node, char *out, size_t out_sz) {
    for (xmlNode *cur = node; cur && out[0] == '\0'; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && cur->name) {
            const char *tname = (const char *)cur->name;
            if (is_heading_tag(tname) || strcasecmp(tname, "title") == 0) {
                xmlChar *txt = xmlNodeGetContent(cur);
                if (txt) {
                    normalize_utf8_text((const char *)txt, out, out_sz);
                    xmlFree(txt);
                    if (out[0] != '\0') return;
                }
            }
        }
        if (cur->children) {
            find_first_heading_in_dom(cur->children, out, out_sz);
        }
    }
}

/* Parse NCX (<navMap> / <navPoint>) recursively */
static void parse_ncx_navpoints(xmlNode *node, const char *ncx_dir) {
    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && cur->name &&
            strcasecmp((const char *)cur->name, "navPoint") == 0) {
            char label[128] = "";
            char src[256] = "";
            char frag[64] = "";
            for (xmlNode *ch = cur->children; ch; ch = ch->next) {
                if (ch->type != XML_ELEMENT_NODE || !ch->name) continue;
                if (strcasecmp((const char *)ch->name, "navLabel") == 0) {
                    xmlChar *txt = xmlNodeGetContent(ch);
                    if (txt) {
                        normalize_utf8_text((const char *)txt, label, sizeof(label));
                        xmlFree(txt);
                    }
                } else if (strcasecmp((const char *)ch->name, "content") == 0) {
                    xmlChar *s = xmlGetProp(ch, (const xmlChar *)"src");
                    if (s) {
                        extract_fragment((const char *)s, frag, sizeof(frag));
                        resolve_relative_path(ncx_dir, (const char *)s, src, sizeof(src));
                        xmlFree(s);
                    }
                }
            }
            if (label[0] != '\0' && src[0] != '\0' && g_chapter_count < MAX_CHAPTERS) {
                if (g_chapter_count == 0 ||
                    strcmp(g_chapters[g_chapter_count - 1].href, src) != 0 ||
                    strcmp(g_chapters[g_chapter_count - 1].frag, frag) != 0) {
                    memset(&g_chapters[g_chapter_count], 0, sizeof(EpubChapter));
                    snprintf(g_chapters[g_chapter_count].title, sizeof(g_chapters[0].title), "%s", label);
                    snprintf(g_chapters[g_chapter_count].href, sizeof(g_chapters[0].href), "%s", src);
                    snprintf(g_chapters[g_chapter_count].frag, sizeof(g_chapters[0].frag), "%s", frag);
                    g_chapters[g_chapter_count].start_spine = -1;
                    g_chapters[g_chapter_count].end_spine = -1;
                    g_chapter_count++;
                }
            }
            parse_ncx_navpoints(cur->children, ncx_dir);
        } else if (cur->children) {
            parse_ncx_navpoints(cur->children, ncx_dir);
        }
    }
}

/* Parse EPUB 3 <nav> links */
static void parse_epub3_nav_links(xmlNode *node, const char *nav_dir, bool inside_nav) {
    for (xmlNode *cur = node; cur; cur = cur->next) {
        bool now_in_nav = inside_nav;
        if (cur->type == XML_ELEMENT_NODE && cur->name) {
            if (strcasecmp((const char *)cur->name, "nav") == 0) {
                now_in_nav = true;
            } else if (inside_nav && strcasecmp((const char *)cur->name, "a") == 0) {
                xmlChar *href = xmlGetProp(cur, (const xmlChar *)"href");
                xmlChar *txt = xmlNodeGetContent(cur);
                if (href && txt && g_chapter_count < MAX_CHAPTERS) {
                    char label[128] = "";
                    char resolved[256] = "";
                    char frag[64] = "";
                    normalize_utf8_text((const char *)txt, label, sizeof(label));
                    extract_fragment((const char *)href, frag, sizeof(frag));
                    resolve_relative_path(nav_dir, (const char *)href, resolved, sizeof(resolved));
                    if (label[0] != '\0' && resolved[0] != '\0') {
                        memset(&g_chapters[g_chapter_count], 0, sizeof(EpubChapter));
                        snprintf(g_chapters[g_chapter_count].title, sizeof(g_chapters[0].title), "%s", label);
                        snprintf(g_chapters[g_chapter_count].href, sizeof(g_chapters[0].href), "%s", resolved);
                        snprintf(g_chapters[g_chapter_count].frag, sizeof(g_chapters[0].frag), "%s", frag);
                        g_chapters[g_chapter_count].start_spine = -1;
                        g_chapters[g_chapter_count].end_spine = -1;
                        g_chapter_count++;
                    }
                }
                if (href) xmlFree(href);
                if (txt) xmlFree(txt);
            }
        }
        if (cur->children) {
            parse_epub3_nav_links(cur->children, nav_dir, now_in_nav);
        }
    }
}

static void parse_xhtml_entry_into_lines(const char *entry_href, const char *start_frag, const char *end_frag) {
    size_t xml_sz = 0;
    char *xml_buf = read_zip_entry(g_epub_zip, entry_href, &xml_sz);
    if (!xml_buf) return;

    int opts = XML_PARSE_RECOVER | XML_PARSE_NOENT | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET;
    xmlDocPtr doc = xmlReadMemory(xml_buf, (int)xml_sz, entry_href, "UTF-8", opts);
    if (!doc) {
        doc = htmlReadMemory(xml_buf, (int)xml_sz, entry_href, "UTF-8",
                             HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    }

    if (doc) {
        xmlNode *root = xmlDocGetRootElement(doc);
        bool wait_for_start = (start_frag && start_frag[0] != '\0' && dom_has_frag(root, start_frag));
        StrBuf sb;
        memset(&sb, 0, sizeof(sb));
        sb.start_frag = wait_for_start ? start_frag : NULL;
        sb.end_frag = (end_frag && end_frag[0] != '\0') ? end_frag : NULL;
        sb.active = !wait_for_start;
        sb.stopped = false;

        walk_xhtml_nodes(root, &sb, false);
        sb_flush(&sb, false);
        free(sb.buf);
        xmlFreeDoc(doc);
    }
    free(xml_buf);
}

static void load_chapter(int chapter_idx) {
    if (g_is_plain_txt) return;
    if (chapter_idx < 0 || chapter_idx >= g_chapter_count) return;

    g_cur_chapter = chapter_idx;
    free_wrapped_lines();

    EpubChapter *ch = &g_chapters[chapter_idx];
    if (ch->start_spine >= 0 && ch->end_spine >= ch->start_spine) {
        for (int s = ch->start_spine; s <= ch->end_spine; s++) {
            const char *s_frag = (s == ch->start_spine && ch->use_start_frag) ? ch->frag : NULL;
            const char *e_frag = (s == ch->end_spine && ch->end_frag[0] != '\0') ? ch->end_frag : NULL;
            parse_xhtml_entry_into_lines(g_spine_hrefs[s], s_frag, e_frag);
        }
    } else {
        const char *s_frag = ch->use_start_frag ? ch->frag : NULL;
        const char *e_frag = (ch->end_frag[0] != '\0') ? ch->end_frag : NULL;
        parse_xhtml_entry_into_lines(ch->href, s_frag, e_frag);
    }

    /* Remove trailing blank line */
    while (g_line_count > 0 && g_lines[g_line_count - 1].is_blank) {
        free(g_lines[g_line_count - 1].text);
        g_line_count--;
    }
    if (g_line_count == 0) {
        wrap_paragraph(ch->title, true);
    }

    g_total_pages = (g_line_count + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
    if (g_total_pages < 1) g_total_pages = 1;
    ch->page_count = g_total_pages;
    g_cur_page = 0;
}

static bool open_epub_book(const char *filepath) {
    if (g_epub_zip) {
        zip_close(g_epub_zip);
        g_epub_zip = NULL;
    }
    free_wrapped_lines();
    g_chapter_count = 0;
    g_cur_chapter = 0;
    g_spine_count = 0;
    g_book_total_pages = 1;
    g_is_plain_txt = false;
    g_book_title[0] = '\0';
    g_book_author[0] = '\0';

    /* Also support plain .txt files gracefully */
    const char *ext = strrchr(filepath, '.');
    if (ext && strcasecmp(ext, ".txt") == 0) {
        FILE *f = fopen(filepath, "rb");
        if (!f) return false;
        g_is_plain_txt = true;
        snprintf(g_plain_txt_path, sizeof(g_plain_txt_path), "%s", filepath);
        const char *base = strrchr(filepath, '/');
        snprintf(g_book_title, sizeof(g_book_title), "%s", base ? (base + 1) : filepath);
        memset(&g_chapters[0], 0, sizeof(EpubChapter));
        snprintf(g_chapters[0].title, sizeof(g_chapters[0].title), "%s", g_book_title);
        g_chapter_count = 1;

        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            wrap_paragraph(line, false);
        }
        fclose(f);
        g_total_pages = (g_line_count + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
        if (g_total_pages < 1) g_total_pages = 1;
        g_chapters[0].page_count = g_total_pages;
        g_book_total_pages = g_total_pages;
        g_cur_page = 0;
        return true;
    }

    int err = 0;
    g_epub_zip = zip_open(filepath, ZIP_RDONLY, &err);
    if (!g_epub_zip) return false;

    /* 1. Parse META-INF/container.xml to locate the .opf rootfile */
    char opf_path[256] = "";
    size_t cont_sz = 0;
    char *cont_xml = read_zip_entry(g_epub_zip, "META-INF/container.xml", &cont_sz);
    if (cont_xml) {
        xmlDocPtr cdoc = xmlReadMemory(cont_xml, (int)cont_sz, "container.xml", NULL,
                                       XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
        if (cdoc) {
            xmlNode *root = xmlDocGetRootElement(cdoc);
            for (xmlNode *n1 = root ? root->children : NULL; n1; n1 = n1->next) {
                if (n1->type == XML_ELEMENT_NODE && n1->name &&
                    strcasecmp((const char *)n1->name, "rootfiles") == 0) {
                    for (xmlNode *n2 = n1->children; n2; n2 = n2->next) {
                        if (n2->type == XML_ELEMENT_NODE && n2->name &&
                            strcasecmp((const char *)n2->name, "rootfile") == 0) {
                            xmlChar *fp = xmlGetProp(n2, (const xmlChar *)"full-path");
                            if (fp) {
                                snprintf(opf_path, sizeof(opf_path), "%s", (const char *)fp);
                                xmlFree(fp);
                                break;
                            }
                        }
                    }
                }
            }
            xmlFreeDoc(cdoc);
        }
        free(cont_xml);
    }

    /* Fallback scan if META-INF/container.xml was missing */
    if (opf_path[0] == '\0') {
        zip_int64_t n = zip_get_num_entries(g_epub_zip, 0);
        for (zip_int64_t i = 0; i < n; i++) {
            const char *name = zip_get_name(g_epub_zip, (zip_uint64_t)i, 0);
            if (name && strstr(name, ".opf")) {
                snprintf(opf_path, sizeof(opf_path), "%s", name);
                break;
            }
        }
    }
    if (opf_path[0] == '\0') {
        zip_close(g_epub_zip);
        g_epub_zip = NULL;
        return false;
    }

    char opf_dir[256] = "";
    extract_dirname(opf_path, opf_dir, sizeof(opf_dir));

    /* 2. Parse the .opf Package Document (metadata, manifest, spine) */
    size_t opf_sz = 0;
    char *opf_xml = read_zip_entry(g_epub_zip, opf_path, &opf_sz);
    if (!opf_xml) {
        zip_close(g_epub_zip);
        g_epub_zip = NULL;
        return false;
    }

    ManifestItem *manifest = (ManifestItem *)calloc(MAX_MANIFEST, sizeof(ManifestItem));
    int manifest_count = 0;
    char spine_ids[MAX_MANIFEST][128];
    int spine_id_count = 0;
    char toc_id[128] = "";

    xmlDocPtr odoc = xmlReadMemory(opf_xml, (int)opf_sz, opf_path, NULL,
                                   XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (odoc) {
        xmlNode *root = xmlDocGetRootElement(odoc);
        for (xmlNode *sec = root ? root->children : NULL; sec; sec = sec->next) {
            if (sec->type != XML_ELEMENT_NODE || !sec->name) continue;
            const char *sname = (const char *)sec->name;

            if (strcasecmp(sname, "metadata") == 0) {
                for (xmlNode *m = sec->children; m; m = m->next) {
                    if (m->type != XML_ELEMENT_NODE || !m->name) continue;
                    if (strcasecmp((const char *)m->name, "title") == 0 && g_book_title[0] == '\0') {
                        xmlChar *t = xmlNodeGetContent(m);
                        if (t) {
                            normalize_utf8_text((const char *)t, g_book_title, sizeof(g_book_title));
                            xmlFree(t);
                        }
                    } else if (strcasecmp((const char *)m->name, "creator") == 0 && g_book_author[0] == '\0') {
                        xmlChar *c = xmlNodeGetContent(m);
                        if (c) {
                            normalize_utf8_text((const char *)c, g_book_author, sizeof(g_book_author));
                            xmlFree(c);
                        }
                    }
                }
            } else if (strcasecmp(sname, "manifest") == 0) {
                for (xmlNode *it = sec->children; it && manifest_count < MAX_MANIFEST; it = it->next) {
                    if (it->type != XML_ELEMENT_NODE || !it->name ||
                        strcasecmp((const char *)it->name, "item") != 0) continue;
                    xmlChar *id = xmlGetProp(it, (const xmlChar *)"id");
                    xmlChar *href = xmlGetProp(it, (const xmlChar *)"href");
                    xmlChar *mt = xmlGetProp(it, (const xmlChar *)"media-type");
                    xmlChar *props = xmlGetProp(it, (const xmlChar *)"properties");
                    if (id && href) {
                        snprintf(manifest[manifest_count].id, sizeof(manifest[0].id), "%s", (const char *)id);
                        resolve_relative_path(opf_dir, (const char *)href,
                                              manifest[manifest_count].href, sizeof(manifest[0].href));
                        if (mt) {
                            snprintf(manifest[manifest_count].media_type, sizeof(manifest[0].media_type),
                                     "%s", (const char *)mt);
                        }
                        if (props && strstr((const char *)props, "nav")) {
                            manifest[manifest_count].is_nav = true;
                        }
                        manifest_count++;
                    }
                    if (id) xmlFree(id);
                    if (href) xmlFree(href);
                    if (mt) xmlFree(mt);
                    if (props) xmlFree(props);
                }
            } else if (strcasecmp(sname, "spine") == 0) {
                xmlChar *toc_attr = xmlGetProp(sec, (const xmlChar *)"toc");
                if (toc_attr) {
                    snprintf(toc_id, sizeof(toc_id), "%s", (const char *)toc_attr);
                    xmlFree(toc_attr);
                }
                for (xmlNode *ir = sec->children; ir && spine_id_count < MAX_MANIFEST; ir = ir->next) {
                    if (ir->type != XML_ELEMENT_NODE || !ir->name ||
                        strcasecmp((const char *)ir->name, "itemref") != 0) continue;
                    xmlChar *idref = xmlGetProp(ir, (const xmlChar *)"idref");
                    if (idref) {
                        snprintf(spine_ids[spine_id_count++], sizeof(spine_ids[0]), "%s", (const char *)idref);
                        xmlFree(idref);
                    }
                }
            }
        }
        xmlFreeDoc(odoc);
    }
    free(opf_xml);

    /* Build ordered spine href table */
    for (int s = 0; s < spine_id_count && g_spine_count < MAX_MANIFEST; s++) {
        for (int m = 0; m < manifest_count; m++) {
            if (strcmp(spine_ids[s], manifest[m].id) == 0) {
                snprintf(g_spine_hrefs[g_spine_count++], sizeof(g_spine_hrefs[0]), "%s", manifest[m].href);
                break;
            }
        }
    }

    if (g_book_title[0] == '\0') {
        const char *base = strrchr(filepath, '/');
        snprintf(g_book_title, sizeof(g_book_title), "%s", base ? (base + 1) : filepath);
    }

    /* 3. Parse Table of Contents (NCX or EPUB 3 nav) to populate Chapter List */
    char ncx_href[256] = "";
    char nav_href[256] = "";
    for (int i = 0; i < manifest_count; i++) {
        if ((toc_id[0] != '\0' && strcmp(manifest[i].id, toc_id) == 0) ||
            strstr(manifest[i].media_type, "dtbncx") != NULL) {
            snprintf(ncx_href, sizeof(ncx_href), "%s", manifest[i].href);
        }
        if (manifest[i].is_nav) {
            snprintf(nav_href, sizeof(nav_href), "%s", manifest[i].href);
        }
    }

    if (ncx_href[0] != '\0') {
        size_t ncx_sz = 0;
        char *ncx_xml = read_zip_entry(g_epub_zip, ncx_href, &ncx_sz);
        if (ncx_xml) {
            char ncx_dir[256] = "";
            extract_dirname(ncx_href, ncx_dir, sizeof(ncx_dir));
            xmlDocPtr ndoc = xmlReadMemory(ncx_xml, (int)ncx_sz, ncx_href, NULL,
                                           XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
            if (ndoc) {
                parse_ncx_navpoints(xmlDocGetRootElement(ndoc), ncx_dir);
                xmlFreeDoc(ndoc);
            }
            free(ncx_xml);
        }
    }

    if (g_chapter_count == 0 && nav_href[0] != '\0') {
        size_t nav_sz = 0;
        char *nav_xml = read_zip_entry(g_epub_zip, nav_href, &nav_sz);
        if (nav_xml) {
            char nav_dir[256] = "";
            extract_dirname(nav_href, nav_dir, sizeof(nav_dir));
            xmlDocPtr ndoc = xmlReadMemory(nav_xml, (int)nav_sz, nav_href, NULL,
                                           XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
            if (ndoc) {
                parse_epub3_nav_links(xmlDocGetRootElement(ndoc), nav_dir, false);
                xmlFreeDoc(ndoc);
            }
            free(nav_xml);
        }
    }

    /* 4. Fallback to spine items if TOC was empty */
    if (g_chapter_count == 0) {
        for (int s = 0; s < g_spine_count && g_chapter_count < MAX_CHAPTERS; s++) {
            char heading[128] = "";
            size_t ch_sz = 0;
            char *ch_xml = read_zip_entry(g_epub_zip, g_spine_hrefs[s], &ch_sz);
            if (ch_xml) {
                xmlDocPtr cdoc = xmlReadMemory(ch_xml, (int)ch_sz, g_spine_hrefs[s], NULL,
                                               XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
                if (cdoc) {
                    find_first_heading_in_dom(xmlDocGetRootElement(cdoc), heading, sizeof(heading));
                    xmlFreeDoc(cdoc);
                }
                free(ch_xml);
            }
            if (heading[0] == '\0') {
                snprintf(heading, sizeof(heading), "Chapter %d", g_chapter_count + 1);
            }
            memset(&g_chapters[g_chapter_count], 0, sizeof(EpubChapter));
            snprintf(g_chapters[g_chapter_count].title, sizeof(g_chapters[0].title), "%s", heading);
            memcpy(g_chapters[g_chapter_count].href, g_spine_hrefs[s], sizeof(g_chapters[0].href));
            g_chapters[g_chapter_count].start_spine = s;
            g_chapters[g_chapter_count].end_spine = s;
            g_chapter_count++;
        }
    }

    free(manifest);

    if (g_chapter_count == 0) {
        zip_close(g_epub_zip);
        g_epub_zip = NULL;
        return false;
    }

    /* Map each chapter to its spine index range and fragment boundaries */
    for (int i = 0; i < g_chapter_count; i++) {
        for (int s = 0; s < g_spine_count; s++) {
            if (strcasecmp(g_chapters[i].href, g_spine_hrefs[s]) == 0) {
                g_chapters[i].start_spine = s;
                break;
            }
        }
    }
    for (int i = 0; i < g_chapter_count; i++) {
        if (i > 0 && strcmp(g_chapters[i].href, g_chapters[i - 1].href) == 0 && g_chapters[i].frag[0] != '\0') {
            g_chapters[i].use_start_frag = true;
        }
        if (i + 1 < g_chapter_count && strcmp(g_chapters[i].href, g_chapters[i + 1].href) == 0 &&
            g_chapters[i + 1].frag[0] != '\0') {
            snprintf(g_chapters[i].end_frag, sizeof(g_chapters[i].end_frag), "%s", g_chapters[i + 1].frag);
            g_chapters[i].end_spine = g_chapters[i].start_spine;
        } else if (g_chapters[i].start_spine >= 0) {
            if (i + 1 < g_chapter_count && g_chapters[i + 1].start_spine > g_chapters[i].start_spine) {
                g_chapters[i].end_spine = g_chapters[i + 1].start_spine - 1;
            } else if (i + 1 == g_chapter_count && g_spine_count > g_chapters[i].start_spine) {
                g_chapters[i].end_spine = g_spine_count - 1;
            } else {
                g_chapters[i].end_spine = g_chapters[i].start_spine;
            }
        }
    }

    /* Compute exact page counts across all chapters so overall book progress is accurate */
    int total_book_pgs = 0;
    for (int i = 0; i < g_chapter_count; i++) {
        load_chapter(i);
        total_book_pgs += g_chapters[i].page_count;
    }
    g_book_total_pages = (total_book_pgs > 0) ? total_book_pgs : 1;

    load_chapter(0);
    return true;
}

/* =========================================================================
 * File Browser (/mnt/media)
 * ========================================================================= */
static int cmp_file_entry(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;
    if (strcmp(fa->name, "..") == 0) return -1;
    if (strcmp(fb->name, "..") == 0) return 1;
    if (fa->is_dir != fb->is_dir) return fa->is_dir ? -1 : 1;
    return strcasecmp(fa->name, fb->name);
}

static bool has_book_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return (strcasecmp(dot, ".epub") == 0 || strcasecmp(dot, ".txt") == 0);
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
        if (!is_dir && !has_book_extension(de->d_name)) continue;

        snprintf(g_files[g_file_count].name, sizeof(g_files[0].name), "%s", de->d_name);
        g_files[g_file_count].is_dir = is_dir;
        g_files[g_file_count].size = st.st_size;
        g_file_count++;
    }
    closedir(d);

    qsort(g_files, (size_t)g_file_count, sizeof(FileEntry), cmp_file_entry);
}

/* =========================================================================
 * UI Rendering: File Browser, Reading View, and Chapter List Modal
 * ========================================================================= */
static void render_file_browser(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(15, 21, 33));

    /* Top Header (y=0..23) */
    fill_rect(0, 0, SCREEN_W, 23, RGB(24, 34, 54));
    fill_rect(0, 23, SCREEN_W, 1, RGB(56, 78, 112));
    draw_pill(5, 4, 42, 15, RGB(37, 99, 235), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_10, 5, 4, 42, 15, "BARD", RGB(255, 255, 255));
    {
        int max_pw = SCREEN_W - 58;
        if (text_width(FONT_UI_12, g_cwd) <= max_pw) {
            draw_text(FONT_UI_12, 53, 4, g_cwd, RGB(226, 232, 240));
        } else {
            char pbuf[256];
            size_t clen = strlen(g_cwd);
            size_t off = 1;
            while (off < clen) {
                snprintf(pbuf, sizeof(pbuf), "...%s", g_cwd + off);
                if (text_width(FONT_UI_12, pbuf) <= max_pw) break;
                off++;
            }
            draw_text(FONT_UI_12, 53, 4, pbuf, RGB(226, 232, 240));
        }
    }

    /* File List Rows (y=26..222) */
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
            draw_pill(8, ry + 3, 36, 14, RGB(88, 40, 136), RGB(192, 132, 252));
            draw_text_centered(FONT_UI_10, 8, ry + 3, 36, 14, "EPUB", RGB(233, 213, 255));

            char sz_str[24];
            if (g_files[idx].size >= 1024 * 1024) {
                snprintf(sz_str, sizeof(sz_str), "%.1fM", (double)g_files[idx].size / (1024.0 * 1024.0));
            } else {
                snprintf(sz_str, sizeof(sz_str), "%.1fK", (double)g_files[idx].size / 1024.0);
            }
            int sz_w = text_width(FONT_UI_10, sz_str);
            draw_text_right(FONT_UI_10, SCREEN_W - 10, ry + 4, sz_str, RGB(148, 163, 184));
            draw_text_clipped(FONT_UI_12, 50, ry + 2, SCREEN_W - 66 - sz_w,
                              g_files[idx].name, sel ? RGB(255, 255, 255) : RGB(241, 245, 249));
        }
    }

    /* Bottom Controls Bar (y=225..239) */
    fill_rect(0, 225, SCREEN_W, 15, RGB(10, 14, 23));
    fill_rect(0, 225, SCREEN_W, 1, RGB(30, 41, 59));
    draw_text_centered(FONT_UI_10, 0, 226, SCREEN_W, 14,
                       "UP/DN:Move  A:Open Book/Dir  B:Up Dir  START:Quit",
                       RGB(148, 163, 184));
}

static void render_reading_view(void) {
    uint32_t bg_col   = g_dark_theme ? RGB(22, 25, 34)    : RGB(250, 246, 236);
    uint32_t txt_col  = g_dark_theme ? RGB(226, 222, 212) : RGB(28, 33, 41);
    uint32_t head_col = g_dark_theme ? RGB(147, 197, 253) : RGB(124, 45, 18);

    /* Full-screen reading canvas (320x240, no top bar or bottom bar) */
    fill_rect(0, 0, SCREEN_W, SCREEN_H, bg_col);

    int start_line = g_cur_page * LINES_PER_PAGE;
    int line_step = 18;
    int y = 6;
    for (int i = 0; i < LINES_PER_PAGE; i++) {
        int lidx = start_line + i;
        if (lidx >= g_line_count) break;
        if (!g_lines[lidx].is_blank && g_lines[lidx].text[0] != '\0') {
            int fid = g_lines[lidx].is_heading ? FONT_BOOK_BOLD16 : FONT_BOOK_15;
            uint32_t col = g_lines[lidx].is_heading ? head_col : txt_col;
            draw_text(fid, TEXT_MARGIN_X, y, g_lines[lidx].text, col);
        }
        y += line_step;
    }

    /* Discrete "Y: Help" indicator in the bottom-right corner */
    int hw = 46, hh = 13;
    int hx = SCREEN_W - hw - 4;
    int hy = SCREEN_H - hh - 3;
    uint32_t h_fill   = g_dark_theme ? RGB(32, 38, 52)    : RGB(238, 232, 218);
    uint32_t h_border = g_dark_theme ? RGB(64, 76, 98)    : RGB(202, 194, 176);
    uint32_t h_txt    = g_dark_theme ? RGB(160, 174, 192) : RGB(108, 100, 88);
    draw_pill(hx, hy, hw, hh, h_fill, h_border);
    draw_text_centered(FONT_UI_10, hx, hy, hw, hh, "Y: Help", h_txt);
}

static void render_help_modal(void) {
    render_reading_view();

    int mx = 12, my = 12, mw = SCREEN_W - 24, mh = SCREEN_H - 24;
    fill_rect(mx, my, mw, mh, RGB(17, 24, 39));
    draw_border(mx, my, mw, mh, RGB(96, 165, 250));
    draw_border(mx + 1, my + 1, mw - 2, mh - 2, RGB(37, 99, 235));

    /* Modal Header */
    fill_rect(mx + 2, my + 2, mw - 4, 22, RGB(30, 41, 59));
    fill_rect(mx + 2, my + 24, mw - 4, 1, RGB(71, 85, 105));
    draw_pill(mx + 6, my + 5, 42, 15, RGB(37, 99, 235), RGB(96, 165, 250));
    draw_text_centered(FONT_UI_10, mx + 6, my + 5, 42, 15, "HELP", RGB(255, 255, 255));
    draw_text_clipped(FONT_UI_12, mx + 54, my + 5, mw - 60, g_book_title, RGB(248, 250, 252));

    /* Chapter Name & Current Progress Section */
    int cy = my + 30;
    fill_rect(mx + 6, cy, mw - 12, 62, RGB(24, 33, 52));
    draw_border(mx + 6, cy, mw - 12, 62, RGB(51, 65, 85));

    char ch_hdr[64];
    snprintf(ch_hdr, sizeof(ch_hdr), "CHAPTER (%d of %d)", g_cur_chapter + 1, g_chapter_count);
    draw_text(FONT_UI_10, mx + 12, cy + 4, ch_hdr, RGB(125, 211, 252));

    const char *ch_title = (g_cur_chapter < g_chapter_count) ? g_chapters[g_cur_chapter].title : g_book_title;
    draw_text_clipped(FONT_UI_12, mx + 12, cy + 17, mw - 24, ch_title, RGB(255, 255, 255));

    /* Compute chapter progress and overall book progress */
    int pages_before = 0;
    for (int i = 0; i < g_cur_chapter && i < g_chapter_count; i++) {
        pages_before += (g_chapters[i].page_count > 0) ? g_chapters[i].page_count : 1;
    }
    int book_cur_pg = pages_before + g_cur_page + 1;
    int book_tot_pg = (g_book_total_pages > 0) ? g_book_total_pages : g_total_pages;
    if (book_cur_pg > book_tot_pg) book_tot_pg = book_cur_pg;
    int ch_pct   = (int)(((g_cur_page + 1) * 100) / (g_total_pages > 0 ? g_total_pages : 1));
    int book_pct = (int)((book_cur_pg * 100) / (book_tot_pg > 0 ? book_tot_pg : 1));

    char prog_str[96];
    snprintf(prog_str, sizeof(prog_str), "Chapter Pg %d/%d (%d%%)   |   Book Pg %d/%d (%d%%)",
             g_cur_page + 1, g_total_pages, ch_pct, book_cur_pg, book_tot_pg, book_pct);
    draw_text(FONT_UI_10, mx + 12, cy + 35, prog_str, RGB(250, 204, 21));

    /* Progress Bar */
    int bx = mx + 12, by = cy + 50, bw = mw - 24, bh = 6;
    fill_rect(bx, by, bw, bh, RGB(15, 23, 42));
    draw_border(bx, by, bw, bh, RGB(71, 85, 105));
    int fill_w = ((bw - 2) * book_cur_pg) / (book_tot_pg > 0 ? book_tot_pg : 1);
    if (fill_w < 2) fill_w = 2;
    if (fill_w > bw - 2) fill_w = bw - 2;
    fill_rect(bx + 1, by + 1, fill_w, bh - 2, RGB(56, 189, 248));

    /* Key-Mappings Section */
    int ky = cy + 68;
    draw_text(FONT_UI_10, mx + 10, ky, "KEY MAPPINGS", RGB(148, 163, 184));
    ky += 14;

    static const struct {
        const char *key;
        const char *desc;
    } kmaps[] = {
        { "SELECT",    "Chapters (Table of Contents)" },
        { "X",         "Files (/mnt/media Browser)"   },
        { "UP / DOWN", "Scroll One Page Up / Down"    },
        { "B",         "Switch Light / Dark Mode"     },
        { "START",     "Quit Reader"                  }
    };

    for (int i = 0; i < 5; i++) {
        int ry = ky + i * 17;
        draw_pill(mx + 10, ry, 68, 14, RGB(30, 58, 95), RGB(56, 189, 248));
        draw_text_centered(FONT_UI_10, mx + 10, ry, 68, 14, kmaps[i].key, RGB(254, 240, 138));
        draw_text(FONT_UI_12, mx + 86, ry - 1, kmaps[i].desc, RGB(226, 232, 240));
    }

    /* Modal Footer */
    fill_rect(mx + 2, my + mh - 18, mw - 4, 16, RGB(15, 23, 42));
    fill_rect(mx + 2, my + mh - 18, mw - 4, 1, RGB(51, 65, 85));
    draw_text_centered(FONT_UI_10, mx, my + mh - 17, mw, 15,
                       "Press Y to close Help and return to reading",
                       RGB(203, 213, 225));
}

static void render_chapter_modal(void) {
    /* Render reading view underneath, then draw Chapter List overlay box */
    render_reading_view();

    int mx = 10, my = 16, mw = SCREEN_W - 20, mh = SCREEN_H - 32;
    fill_rect(mx, my, mw, mh, RGB(17, 24, 39));
    draw_border(mx, my, mw, mh, RGB(96, 165, 250));
    draw_border(mx + 1, my + 1, mw - 2, mh - 2, RGB(37, 99, 235));

    /* Modal Header */
    fill_rect(mx + 2, my + 2, mw - 4, 22, RGB(30, 41, 59));
    fill_rect(mx + 2, my + 24, mw - 4, 1, RGB(71, 85, 105));
    draw_pill(mx + 6, my + 5, 60, 15, RGB(124, 58, 237), RGB(167, 139, 250));
    draw_text_centered(FONT_UI_10, mx + 6, my + 5, 60, 15, "CHAPTERS", RGB(255, 255, 255));
    draw_text_clipped(FONT_UI_12, mx + 72, my + 5, mw - 78, g_book_title, RGB(248, 250, 252));

    /* Chapter Rows */
    int list_y = my + 28;
    int row_h = 20;
    for (int i = 0; i < TOC_ROWS; i++) {
        int cidx = g_toc_scroll + i;
        if (cidx >= g_chapter_count) break;
        int ry = list_y + i * row_h;
        bool sel = (cidx == g_toc_sel);
        bool active = (cidx == g_cur_chapter);

        if (sel) {
            fill_rect(mx + 6, ry, mw - 12, row_h - 1, RGB(37, 99, 235));
            draw_border(mx + 6, ry, mw - 12, row_h - 1, RGB(250, 204, 21));
        } else if (i % 2 == 1) {
            fill_rect(mx + 6, ry, mw - 12, row_h - 1, RGB(24, 33, 52));
        }

        char num_badge[16];
        snprintf(num_badge, sizeof(num_badge), "%2d.", cidx + 1);
        draw_text(FONT_UI_10, mx + 10, ry + 4, num_badge,
                  sel ? RGB(254, 240, 138) : (active ? RGB(56, 189, 248) : RGB(148, 163, 184)));

        char pg_badge[24];
        snprintf(pg_badge, sizeof(pg_badge), "%dp", g_chapters[cidx].page_count);
        int pg_w = text_width(FONT_UI_10, pg_badge);
        draw_text_right(FONT_UI_10, mx + mw - 10, ry + 4, pg_badge,
                        sel ? RGB(254, 240, 138) : RGB(148, 163, 184));

        draw_text_clipped(FONT_UI_12, mx + 34, ry + 2, mw - 50 - pg_w,
                          g_chapters[cidx].title,
                          sel ? RGB(255, 255, 255) : (active ? RGB(125, 211, 252) : RGB(226, 232, 240)));
    }

    /* Modal Footer */
    fill_rect(mx + 2, my + mh - 18, mw - 4, 16, RGB(15, 23, 42));
    fill_rect(mx + 2, my + mh - 18, mw - 4, 1, RGB(51, 65, 85));
    draw_text_centered(FONT_UI_10, mx, my + mh - 17, mw, 15,
                       "UP/DN:Select Chapter   A:Choose   B/SELECT:Close",
                       RGB(203, 213, 225));
}

static void render_ui(SDL_Surface *screen) {
    switch (g_state) {
        case STATE_FILE_BROWSER:
            render_file_browser();
            break;
        case STATE_READING:
            render_reading_view();
            break;
        case STATE_CHAPTER_LIST:
            render_chapter_modal();
            break;
        case STATE_HELP:
            render_help_modal();
            break;
    }
    present_frame(screen);
}

/* =========================================================================
 * Main Entry Point & Event Loop
 * ========================================================================= */
int main(int argc, char *argv[]) {
    LIBXML_TEST_VERSION
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
    SDL_WM_SetCaption("PowKiddy V90 EPUB Reader (Bard)", NULL);

    /* Determine starting directory (/mnt/media on PowKiddy V90 SD card) */
    struct stat st;
    if (argc > 1 && stat(argv[1], &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            scan_directory(argv[1]);
        } else {
            char dir_buf[PATH_MAX];
            extract_dirname(argv[1], dir_buf, sizeof(dir_buf));
            scan_directory(dir_buf[0] ? dir_buf : ".");
            if (open_epub_book(argv[1])) {
                g_state = STATE_READING;
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

    /* Automated demo screenshot generation mode if BARD_DEMO_DIR is set */
    const char *demo_dir = getenv("BARD_DEMO_DIR");
    if (demo_dir && demo_dir[0] != '\0') {
        char path[512];

        /* 1. File Browser screenshot */
        g_state = STATE_FILE_BROWSER;
        for (int i = 0; i < g_file_count; i++) {
            if (!g_files[i].is_dir) { g_file_sel = i; break; }
        }
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/bard_files.bmp", demo_dir);
        save_bmp(path);

        /* 2. Reading View, Help Screen, and Chapter List screenshots */
        if (g_file_sel < g_file_count && !g_files[g_file_sel].is_dir) {
            char full_book[PATH_MAX + 512];
            snprintf(full_book, sizeof(full_book), "%s/%s", g_cwd, g_files[g_file_sel].name);
            if (open_epub_book(full_book)) {
                printf("Book: %s (%d chapters, %d total pages)\n",
                       g_book_title, g_chapter_count, g_book_total_pages);
                for (int c = 0; c < g_chapter_count; c++) {
                    printf("  [%2d] %-44s : %d pages\n",
                           c + 1, g_chapters[c].title, g_chapters[c].page_count);
                }

                g_state = STATE_READING;
                render_ui(screen);
                snprintf(path, sizeof(path), "%s/bard_reading.bmp", demo_dir);
                save_bmp(path);

                /* 3. Help Screen (Y) screenshot */
                g_state = STATE_HELP;
                render_ui(screen);
                snprintf(path, sizeof(path), "%s/bard_help.bmp", demo_dir);
                save_bmp(path);

                /* 4. Chapter List (SELECT) screenshot */
                g_state = STATE_CHAPTER_LIST;
                g_toc_sel = (g_chapter_count > 1) ? 1 : 0;
                g_toc_scroll = 0;
                render_ui(screen);
                snprintf(path, sizeof(path), "%s/bard_chapters.bmp", demo_dir);
                save_bmp(path);
            }
        }

        if (g_epub_zip) zip_close(g_epub_zip);
        free_wrapped_lines();
        xmlCleanupParser();
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

                /* START or MENU (or Q on desktop) quits the reader */
                if (k == V90_BTN_START || k == V90_BTN_MENU || k == SDLK_q) {
                    running = false;
                    break;
                }

                if (g_state == STATE_FILE_BROWSER) {
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
                            } else if (open_epub_book(target)) {
                                g_state = STATE_READING;
                            }
                        }
                    } else if (k == V90_BTN_B || k == SDLK_b || k == V90_BTN_R1) {
                        if (strcmp(g_cwd, "/") != 0) {
                            char parent[PATH_MAX + 32];
                            snprintf(parent, sizeof(parent), "%s/..", g_cwd);
                            scan_directory(parent);
                        }
                    }
                } else if (g_state == STATE_READING) {
                    /* DPAD DOWN: Scroll one whole page forward */
                    if (k == V90_BTN_DOWN || k == V90_BTN_R1 || k == SDLK_PAGEDOWN) {
                        if (g_cur_page + 1 < g_total_pages) {
                            g_cur_page++;
                        } else if (g_cur_chapter + 1 < g_chapter_count) {
                            load_chapter(g_cur_chapter + 1);
                        }
                    }
                    /* DPAD UP: Scroll one whole page backward */
                    else if (k == V90_BTN_UP || k == V90_BTN_L1 || k == SDLK_PAGEUP) {
                        if (g_cur_page > 0) {
                            g_cur_page--;
                        } else if (g_cur_chapter > 0) {
                            load_chapter(g_cur_chapter - 1);
                            g_cur_page = g_total_pages - 1;
                        }
                    }
                    /* SELECT (or Home/C on desktop): Open Chapter List */
                    else if (k == V90_BTN_SELECT || k == SDLK_HOME || k == SDLK_c) {
                        g_state = STATE_CHAPTER_LIST;
                        g_toc_sel = g_cur_chapter;
                        if (g_toc_sel < g_toc_scroll) {
                            g_toc_scroll = g_toc_sel;
                        } else if (g_toc_sel >= g_toc_scroll + TOC_ROWS) {
                            g_toc_scroll = g_toc_sel - TOC_ROWS + 1;
                        }
                    }
                    /* X button: Return to /mnt/media file browser */
                    else if (k == V90_BTN_X || k == SDLK_x) {
                        g_state = STATE_FILE_BROWSER;
                    }
                    /* B button (physical B / SDLK_LCTRL on V90, or B on Linux): Switch Light / Dark mode */
                    else if (k == V90_BTN_B || k == SDLK_b) {
                        g_dark_theme = !g_dark_theme;
                    }
                    /* Y button (or Y/H/Space on desktop): Open Help Screen */
                    else if (k == V90_BTN_Y || k == SDLK_y || k == SDLK_h) {
                        g_state = STATE_HELP;
                    }
                } else if (g_state == STATE_HELP) {
                    /* From Help screen, allow direct shortcuts, B to toggle theme, or Y / A to close */
                    if (k == V90_BTN_SELECT || k == SDLK_HOME || k == SDLK_c) {
                        g_state = STATE_CHAPTER_LIST;
                        g_toc_sel = g_cur_chapter;
                        if (g_toc_sel < g_toc_scroll) {
                            g_toc_scroll = g_toc_sel;
                        } else if (g_toc_sel >= g_toc_scroll + TOC_ROWS) {
                            g_toc_scroll = g_toc_sel - TOC_ROWS + 1;
                        }
                    } else if (k == V90_BTN_X || k == SDLK_x) {
                        g_state = STATE_FILE_BROWSER;
                    } else if (k == V90_BTN_B || k == SDLK_b) {
                        g_dark_theme = !g_dark_theme;
                    } else if (k == V90_BTN_Y || k == SDLK_y || k == SDLK_h ||
                               k == V90_BTN_A || k == SDLK_a) {
                        g_state = STATE_READING;
                    }
                } else if (g_state == STATE_CHAPTER_LIST) {
                    /* DPAD UP/DOWN: Select chapter in TOC */
                    if (k == V90_BTN_UP) {
                        if (g_toc_sel > 0) g_toc_sel--;
                        if (g_toc_sel < g_toc_scroll) g_toc_scroll = g_toc_sel;
                    } else if (k == V90_BTN_DOWN) {
                        if (g_toc_sel + 1 < g_chapter_count) g_toc_sel++;
                        if (g_toc_sel >= g_toc_scroll + TOC_ROWS) {
                            g_toc_scroll = g_toc_sel - TOC_ROWS + 1;
                        }
                    }
                    /* A button: Choose the highlighted chapter */
                    else if (k == V90_BTN_A || k == SDLK_a) {
                        load_chapter(g_toc_sel);
                        g_state = STATE_READING;
                    }
                    /* B or SELECT: Close Chapter List without changing chapter */
                    else if (k == V90_BTN_B || k == SDLK_b ||
                             k == V90_BTN_SELECT || k == SDLK_HOME || k == SDLK_c) {
                        g_state = STATE_READING;
                    }
                }
            }
        }
        render_ui(screen);
        SDL_Delay(16);
    }

    if (g_epub_zip) zip_close(g_epub_zip);
    free_wrapped_lines();
    xmlCleanupParser();
    SDL_Quit();
    return 0;
}
