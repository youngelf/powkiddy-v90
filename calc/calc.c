/*
 * calc - Desk & Scientific Calculator for PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host
 *
 * Screen resolution: 320x240, 16-bit RGB565 on V90 (scaled 2x to 640x480 on Linux X11)
 *
 * Controls on PowKiddy V90 (MiyooCFW 1.3.3) & Linux Host:
 *   D-Pad (UP/DOWN/LEFT/RIGHT) : Navigate keypad grid (wraps around edges)
 *   A (LAlt)                   : Activate selected key
 *   SELECT (Escape)            : Toggle Basic Calculator <-> Scientific Calculator mode
 *   X (LShift)                 : Clear (C / AC)
 *   R1 (Backspace)             : Delete last digit (DEL)
 *   L1 (Tab)                   : Toggle Degrees (DEG) <-> Radians (RAD)
 *   Y (Space)                  : Toggle Help modal
 *   MENU (RCtrl) or 'Q'        : Quit calculator
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>

#include "calc_font.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define SCREEN_BPP 16

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

/* =========================================================================
 * Platform Backend: Native X11 (Linux Host) vs SDL 1.2 (PowKiddy V90)
 * ========================================================================= */
#if defined(HOST_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <time.h>

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
#define SDLK_KP_ENTER   XK_KP_Enter
#define SDLK_RCTRL      XK_Control_R
#define SDLK_UP         XK_Up
#define SDLK_DOWN       XK_Down
#define SDLK_LEFT       XK_Left
#define SDLK_RIGHT      XK_Right
#define SDLK_0          XK_0
#define SDLK_1          XK_1
#define SDLK_2          XK_2
#define SDLK_3          XK_3
#define SDLK_4          XK_4
#define SDLK_5          XK_5
#define SDLK_6          XK_6
#define SDLK_7          XK_7
#define SDLK_8          XK_8
#define SDLK_9          XK_9
#define SDLK_PERIOD     XK_period
#define SDLK_PLUS       XK_plus
#define SDLK_MINUS      XK_minus
#define SDLK_ASTERISK   XK_asterisk
#define SDLK_SLASH      XK_slash
#define SDLK_EQUALS     XK_equal
#define SDLK_PERCENT    XK_percent
#define SDLK_CARET      XK_asciicircum
#define SDLK_a          XK_a
#define SDLK_b          XK_b
#define SDLK_c          XK_c
#define SDLK_d          XK_d
#define SDLK_h          XK_h
#define SDLK_m          XK_m
#define SDLK_q          XK_q
#define SDLK_s          XK_s
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
#ifndef SDLK_PERCENT
#define SDLK_PERCENT 37
#endif
#endif

/* =========================================================================
 * PowKiddy V90 Button Definitions (MiyooCFW 1.3.3)
 * ========================================================================= */
#define V90_BTN_UP      SDLK_UP
#define V90_BTN_DOWN    SDLK_DOWN
#define V90_BTN_LEFT    SDLK_LEFT
#define V90_BTN_RIGHT   SDLK_RIGHT
#define V90_BTN_A       SDLK_LALT       /* East button (A) */
#define V90_BTN_B       SDLK_LCTRL      /* South button (B) */
#define V90_BTN_X       SDLK_LSHIFT     /* North button (X) */
#define V90_BTN_Y       SDLK_SPACE      /* West button (Y) */
#define V90_BTN_L1      SDLK_TAB        /* Left shoulder */
#define V90_BTN_R1      SDLK_BACKSPACE  /* Right shoulder */
#define V90_BTN_SELECT  SDLK_ESCAPE     /* SELECT: Mode switch Desk <-> Sci */
#define V90_BTN_START   SDLK_RETURN     /* START: Quit */
#define V90_BTN_MENU    SDLK_RCTRL      /* MENU: Quit */

/* =========================================================================
 * 24-bit TrueColor Backbuffer & Anti-Aliased Software Renderer
 * ========================================================================= */
static uint32_t g_fb[SCREEN_W * SCREEN_H];

#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

static inline void put_pixel(int x, int y, uint32_t color) {
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
        g_fb[y * SCREEN_W + x] = color;
    }
}

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

static uint32_t brighten_color(uint32_t c, int delta) {
    int r = (int)((c >> 16) & 0xFF) + delta;
    int g = (int)((c >> 8)  & 0xFF) + delta;
    int b = (int)(c & 0xFF)         + delta;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return RGB(r, g, b);
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

static void fill_vgradient(int x, int y, int w, int h, uint32_t top_col, uint32_t bot_col) {
    if (h <= 0) return;
    int r0 = (top_col >> 16) & 0xFF, g0 = (top_col >> 8) & 0xFF, b0 = top_col & 0xFF;
    int r1 = (bot_col >> 16) & 0xFF, g1 = (bot_col >> 8) & 0xFF, b1 = bot_col & 0xFF;
    for (int i = 0; i < h; i++) {
        int r = r0 + ((r1 - r0) * i) / (h > 1 ? h - 1 : 1);
        int g = g0 + ((g1 - g0) * i) / (h > 1 ? h - 1 : 1);
        int b = b0 + ((b1 - b0) * i) / (h > 1 ? h - 1 : 1);
        fill_rect(x, y + i, w, 1, RGB(r, g, b));
    }
}

/* Draw a beveled rounded rectangle button */
static void draw_beveled_box(int x, int y, int w, int h, uint32_t base_col, bool focused) {
    if (w <= 2 || h <= 2) return;
    uint32_t fill = focused ? brighten_color(base_col, 36) : base_col;
    uint32_t top_hi = brighten_color(fill, 26);
    uint32_t bot_sh = brighten_color(fill, -24);

    /* Main interior */
    fill_rect(x + 1, y + 1, w - 2, h - 2, fill);
    /* Rounded edges (1px inset on corners) */
    fill_rect(x + 1, y,     w - 2, 1, top_hi);
    fill_rect(x + 1, y + h - 1, w - 2, 1, bot_sh);
    fill_rect(x,     y + 1, 1, h - 2, top_hi);
    fill_rect(x + w - 1, y + 1, 1, h - 2, bot_sh);

    if (focused) {
        /* Glowing 2px golden-amber cursor ring */
        uint32_t ring_outer = RGB(250, 204, 21); /* #FACC15 */
        uint32_t ring_inner = RGB(254, 240, 138); /* #FEF08A */
        fill_rect(x + 1, y,         w - 2, 1, ring_outer);
        fill_rect(x + 1, y + h - 1, w - 2, 1, ring_outer);
        fill_rect(x,     y + 1,     1, h - 2, ring_outer);
        fill_rect(x + w - 1, y + 1, 1, h - 2, ring_outer);

        fill_rect(x + 1, y + 1,     w - 2, 1, ring_inner);
        fill_rect(x + 1, y + h - 2, w - 2, 1, ring_outer);
        fill_rect(x + 1, y + 1,     1, h - 2, ring_inner);
        fill_rect(x + w - 2, y + 1, 1, h - 2, ring_outer);
    }
}

static void draw_pill(int x, int y, int w, int h, uint32_t fill_col, uint32_t border_col) {
    fill_rect(x + 1, y + 1, w - 2, h - 2, fill_col);
    fill_rect(x + 1, y, w - 2, 1, border_col);
    fill_rect(x + 1, y + h - 1, w - 2, 1, border_col);
    fill_rect(x, y + 1, 1, h - 2, border_col);
    fill_rect(x + w - 1, y + 1, 1, h - 2, border_col);
}

static int text_width(int font_id, const char *s) {
    const CalcFont *f = &g_calc_fonts[font_id];
    int w = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 32 || c > 126) c = '?';
        w += f->glyphs[c - 32].advance;
    }
    return w;
}

static void draw_text(int font_id, int x, int y, const char *s, uint32_t color) {
    const CalcFont *f = &g_calc_fonts[font_id];
    int cur_x = x;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 32 || c > 126) c = '?';
        const CalcGlyph *g = &f->glyphs[c - 32];
        const uint8_t *bmp = &g_calc_font_bitmaps[g->offset];
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
    const CalcFont *f = &g_calc_fonts[font_id];
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

/* Save a 24-bit BMP screenshot if requested */
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
 * Calculator State & Scientific Math Engine
 * ========================================================================= */
typedef enum {
    MODE_DESK = 0,
    MODE_SCI  = 1
} CalcMode;

typedef enum {
    OP_NONE = 0,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_POW,
    OP_MOD
} BinaryOp;

typedef enum {
    KEY_CAT_DIGIT = 0,
    KEY_CAT_OP,
    KEY_CAT_EQUALS,
    KEY_CAT_CLEAR,
    KEY_CAT_MEM,
    KEY_CAT_SCI
} KeyCategory;

#define DESK_ROWS 5
#define DESK_COLS 5

#define SCI_ROWS  5
#define SCI_COLS  8

static const char *const k_desk_grid[DESK_ROWS][DESK_COLS] = {
    { "MC",   "C",   "DEL", "%",   "/" },
    { "MR",   "7",   "8",   "9",   "*" },
    { "M+",   "4",   "5",   "6",   "-" },
    { "M-",   "1",   "2",   "3",   "+" },
    { "sqrt", "0",   ".",   "+/-", "=" }
};

static const char *const k_sci_grid[SCI_ROWS][SCI_COLS] = {
    { "sin",  "cos",  "tan",  "DEG",  "C",   "DEL", "%",   "/" },
    { "asin", "acos", "atan", "pi",   "7",   "8",   "9",   "*" },
    { "log",  "ln",   "exp",  "10^x", "4",   "5",   "6",   "-" },
    { "x^y",  "x^2",  "sqrt", "cbrt", "1",   "2",   "3",   "+" },
    { "1/x",  "n!",   "mod",  "e",    "0",   ".",   "+/-", "=" }
};

static CalcMode g_mode = MODE_DESK;
static bool     g_deg_mode = true;     /* true = Degrees, false = Radians */
static bool     g_show_help = false;
static int      g_cur_row = 4;
static int      g_cur_col = 4;         /* Starts on '=' in Desk Mode */

static char     g_display[64] = "0";
static char     g_expr[64]    = "";
static bool     g_new_entry   = true;  /* Next digit replaces display */
static bool     g_error       = false;

static double   g_acc         = 0.0;   /* First operand */
static double   g_last_rhs    = 0.0;   /* Last second operand for repeat '=' */
static BinaryOp g_pending_op  = OP_NONE;
static BinaryOp g_last_op     = OP_NONE;
static double   g_memory      = 0.0;

static void format_number(double val, char *buf, size_t buf_sz) {
    if (isnan(val) || isinf(val)) {
        snprintf(buf, buf_sz, "Math Error");
        g_error = true;
        g_new_entry = true;
        return;
    }
    /* Avoid -0 */
    if (fabs(val) < 1e-15) val = 0.0;

    /* Snap very close integers (fixes 0.1 + 0.2 = 0.30000000000000004) */
    snprintf(buf, buf_sz, "%.12g", val);

    /* Ensure clean scientific formatting if needed */
    char *e_ptr = strchr(buf, 'e');
    if (!e_ptr) e_ptr = strchr(buf, 'E');
    if (!e_ptr && strchr(buf, '.')) {
        size_t len = strlen(buf);
        while (len > 1 && buf[len - 1] == '0') {
            buf[--len] = '\0';
        }
        if (len > 1 && buf[len - 1] == '.') {
            buf[--len] = '\0';
        }
    }
}

static double parse_display(void) {
    if (g_error) return 0.0;
    return strtod(g_display, NULL);
}

static const char *op_symbol(BinaryOp op) {
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_POW: return "^";
        case OP_MOD: return "mod";
        default:     return "";
    }
}

static void set_error(const char *msg) {
    snprintf(g_display, sizeof(g_display), "%s", msg);
    g_error = true;
    g_new_entry = true;
    g_pending_op = OP_NONE;
}

static double eval_binary(double a, double b, BinaryOp op, bool *err) {
    *err = false;
    switch (op) {
        case OP_ADD: return a + b;
        case OP_SUB: return a - b;
        case OP_MUL: return a * b;
        case OP_DIV:
            if (b == 0.0) { *err = true; return 0.0; }
            return a / b;
        case OP_POW:
            return pow(a, b);
        case OP_MOD:
            if (b == 0.0) { *err = true; return 0.0; }
            return fmod(a, b);
        default:
            return b;
    }
}

static void clear_all(void) {
    snprintf(g_display, sizeof(g_display), "0");
    g_expr[0] = '\0';
    g_new_entry = true;
    g_error = false;
    g_acc = 0.0;
    g_last_rhs = 0.0;
    g_pending_op = OP_NONE;
    g_last_op = OP_NONE;
}

static void backspace_digit(void) {
    if (g_error) {
        clear_all();
        return;
    }
    if (g_new_entry) return;
    size_t len = strlen(g_display);
    if (len <= 1 || (len == 2 && g_display[0] == '-')) {
        snprintf(g_display, sizeof(g_display), "0");
        g_new_entry = true;
    } else {
        g_display[len - 1] = '\0';
    }
}

static void input_digit(char d) {
    if (g_error) clear_all();
    if (g_new_entry) {
        if (d == '.') {
            snprintf(g_display, sizeof(g_display), "0.");
        } else {
            g_display[0] = d;
            g_display[1] = '\0';
        }
        g_new_entry = false;
        return;
    }
    if (d == '.' && strchr(g_display, '.')) return;
    size_t len = strlen(g_display);
    if (len >= 18) return;
    if (strcmp(g_display, "0") == 0 && d != '.') {
        g_display[0] = d;
        g_display[1] = '\0';
    } else {
        g_display[len] = d;
        g_display[len + 1] = '\0';
    }
}

static void toggle_sign(void) {
    if (g_error) return;
    if (strcmp(g_display, "0") == 0) return;
    if (g_display[0] == '-') {
        memmove(g_display, g_display + 1, strlen(g_display));
    } else {
        size_t len = strlen(g_display);
        if (len + 2 < sizeof(g_display)) {
            memmove(g_display + 1, g_display, len + 1);
            g_display[0] = '-';
        }
    }
}

static void start_binary_op(BinaryOp op) {
    if (g_error) return;
    double cur = parse_display();
    if (g_pending_op != OP_NONE && !g_new_entry) {
        bool err = false;
        double res = eval_binary(g_acc, cur, g_pending_op, &err);
        if (err || isnan(res) || isinf(res)) {
            set_error(err ? "Div by 0" : "Math Error");
            return;
        }
        g_acc = res;
        format_number(res, g_display, sizeof(g_display));
    } else {
        g_acc = cur;
    }
    g_pending_op = op;
    g_last_op = OP_NONE;
    char num_buf[32];
    format_number(g_acc, num_buf, sizeof(num_buf));
    snprintf(g_expr, sizeof(g_expr), "%s %s", num_buf, op_symbol(op));
    g_new_entry = true;
}

static void press_equals(void) {
    if (g_error) {
        clear_all();
        return;
    }
    double cur = parse_display();
    if (g_pending_op != OP_NONE) {
        bool err = false;
        double lhs = g_acc;
        double rhs = cur;
        BinaryOp op = g_pending_op;
        double res = eval_binary(lhs, rhs, op, &err);
        char lhs_s[24], rhs_s[24];
        format_number(lhs, lhs_s, sizeof(lhs_s));
        format_number(rhs, rhs_s, sizeof(rhs_s));
        snprintf(g_expr, sizeof(g_expr), "%s %s %s =", lhs_s, op_symbol(op), rhs_s);

        g_last_op = op;
        g_last_rhs = rhs;
        g_pending_op = OP_NONE;
        if (err || isnan(res) || isinf(res)) {
            set_error(err ? "Div by 0" : "Math Error");
            return;
        }
        format_number(res, g_display, sizeof(g_display));
        g_new_entry = true;
    } else if (g_last_op != OP_NONE) {
        bool err = false;
        double lhs = cur;
        double rhs = g_last_rhs;
        double res = eval_binary(lhs, rhs, g_last_op, &err);
        char lhs_s[24], rhs_s[24];
        format_number(lhs, lhs_s, sizeof(lhs_s));
        format_number(rhs, rhs_s, sizeof(rhs_s));
        snprintf(g_expr, sizeof(g_expr), "%s %s %s =", lhs_s, op_symbol(g_last_op), rhs_s);
        if (err || isnan(res) || isinf(res)) {
            set_error(err ? "Div by 0" : "Math Error");
            return;
        }
        format_number(res, g_display, sizeof(g_display));
        g_new_entry = true;
    }
}

static void apply_unary(const char *fn) {
    if (g_error) return;
    double x = parse_display();
    double res = 0.0;
    char x_str[24];
    format_number(x, x_str, sizeof(x_str));

    if (strcmp(fn, "sin") == 0) {
        snprintf(g_expr, sizeof(g_expr), "sin(%s%s)", x_str, g_deg_mode ? " deg" : "");
        if (g_deg_mode) {
            double m = fmod(x, 180.0);
            if (fabs(m) < 1e-12) res = 0.0;
            else res = sin(x * (M_PI / 180.0));
        } else {
            res = sin(x);
        }
    } else if (strcmp(fn, "cos") == 0) {
        snprintf(g_expr, sizeof(g_expr), "cos(%s%s)", x_str, g_deg_mode ? " deg" : "");
        if (g_deg_mode) {
            double m = fmod(fabs(x), 180.0);
            if (fabs(m - 90.0) < 1e-12) res = 0.0;
            else res = cos(x * (M_PI / 180.0));
        } else {
            res = cos(x);
        }
    } else if (strcmp(fn, "tan") == 0) {
        snprintf(g_expr, sizeof(g_expr), "tan(%s%s)", x_str, g_deg_mode ? " deg" : "");
        if (g_deg_mode) {
            double m = fmod(fabs(x), 180.0);
            if (fabs(m - 90.0) < 1e-9) {
                set_error("Math Error");
                return;
            }
            if (fabs(m) < 1e-12) res = 0.0;
            else res = tan(x * (M_PI / 180.0));
        } else {
            if (fabs(cos(x)) < 1e-12) {
                set_error("Math Error");
                return;
            }
            res = tan(x);
        }
    } else if (strcmp(fn, "asin") == 0) {
        snprintf(g_expr, sizeof(g_expr), "asin(%s)", x_str);
        if (x < -1.0 || x > 1.0) { set_error("Math Error"); return; }
        res = asin(x);
        if (g_deg_mode) res *= (180.0 / M_PI);
    } else if (strcmp(fn, "acos") == 0) {
        snprintf(g_expr, sizeof(g_expr), "acos(%s)", x_str);
        if (x < -1.0 || x > 1.0) { set_error("Math Error"); return; }
        res = acos(x);
        if (g_deg_mode) res *= (180.0 / M_PI);
    } else if (strcmp(fn, "atan") == 0) {
        snprintf(g_expr, sizeof(g_expr), "atan(%s)", x_str);
        res = atan(x);
        if (g_deg_mode) res *= (180.0 / M_PI);
    } else if (strcmp(fn, "log") == 0) {
        snprintf(g_expr, sizeof(g_expr), "log10(%s)", x_str);
        if (x <= 0.0) { set_error("Math Error"); return; }
        res = log10(x);
    } else if (strcmp(fn, "ln") == 0) {
        snprintf(g_expr, sizeof(g_expr), "ln(%s)", x_str);
        if (x <= 0.0) { set_error("Math Error"); return; }
        res = log(x);
    } else if (strcmp(fn, "exp") == 0) {
        snprintf(g_expr, sizeof(g_expr), "exp(%s)", x_str);
        res = exp(x);
    } else if (strcmp(fn, "10^x") == 0) {
        snprintf(g_expr, sizeof(g_expr), "10^(%s)", x_str);
        res = pow(10.0, x);
    } else if (strcmp(fn, "x^2") == 0) {
        snprintf(g_expr, sizeof(g_expr), "sqr(%s)", x_str);
        res = x * x;
    } else if (strcmp(fn, "sqrt") == 0) {
        snprintf(g_expr, sizeof(g_expr), "sqrt(%s)", x_str);
        if (x < 0.0) { set_error("Math Error"); return; }
        res = sqrt(x);
    } else if (strcmp(fn, "cbrt") == 0) {
        snprintf(g_expr, sizeof(g_expr), "cbrt(%s)", x_str);
        res = cbrt(x);
    } else if (strcmp(fn, "1/x") == 0) {
        snprintf(g_expr, sizeof(g_expr), "1/(%s)", x_str);
        if (x == 0.0) { set_error("Div by 0"); return; }
        res = 1.0 / x;
    } else if (strcmp(fn, "n!") == 0) {
        snprintf(g_expr, sizeof(g_expr), "(%s)!", x_str);
        if (x < 0.0 && floor(x) == x) { set_error("Math Error"); return; }
        if (x > 170.0) { set_error("Overflow"); return; }
        if (x >= 0.0 && floor(x) == x && x <= 20.0) {
            double f = 1.0;
            for (int i = 2; i <= (int)x; i++) f *= (double)i;
            res = f;
        } else {
            res = tgamma(x + 1.0);
        }
    } else if (strcmp(fn, "%") == 0) {
        if (g_pending_op == OP_ADD || g_pending_op == OP_SUB) {
            res = g_acc * (x / 100.0);
        } else {
            res = x / 100.0;
        }
    } else {
        return;
    }

    format_number(res, g_display, sizeof(g_display));
    g_new_entry = true;
}

static void activate_key_label(const char *lbl) {
    if (strlen(lbl) == 1 && lbl[0] >= '0' && lbl[0] <= '9') {
        input_digit(lbl[0]);
    } else if (strcmp(lbl, ".") == 0) {
        input_digit('.');
    } else if (strcmp(lbl, "+/-") == 0) {
        toggle_sign();
    } else if (strcmp(lbl, "C") == 0) {
        clear_all();
    } else if (strcmp(lbl, "DEL") == 0) {
        backspace_digit();
    } else if (strcmp(lbl, "+") == 0) {
        start_binary_op(OP_ADD);
    } else if (strcmp(lbl, "-") == 0) {
        start_binary_op(OP_SUB);
    } else if (strcmp(lbl, "*") == 0) {
        start_binary_op(OP_MUL);
    } else if (strcmp(lbl, "/") == 0) {
        start_binary_op(OP_DIV);
    } else if (strcmp(lbl, "x^y") == 0) {
        start_binary_op(OP_POW);
    } else if (strcmp(lbl, "mod") == 0) {
        start_binary_op(OP_MOD);
    } else if (strcmp(lbl, "=") == 0) {
        press_equals();
    } else if (strcmp(lbl, "MC") == 0) {
        g_memory = 0.0;
    } else if (strcmp(lbl, "MR") == 0) {
        if (g_error) clear_all();
        format_number(g_memory, g_display, sizeof(g_display));
        g_new_entry = true;
    } else if (strcmp(lbl, "M+") == 0) {
        if (!g_error) {
            g_memory += parse_display();
            g_new_entry = true;
        }
    } else if (strcmp(lbl, "M-") == 0) {
        if (!g_error) {
            g_memory -= parse_display();
            g_new_entry = true;
        }
    } else if (strcmp(lbl, "pi") == 0) {
        if (g_error) clear_all();
        format_number(M_PI, g_display, sizeof(g_display));
        g_new_entry = true;
    } else if (strcmp(lbl, "e") == 0) {
        if (g_error) clear_all();
        format_number(M_E, g_display, sizeof(g_display));
        g_new_entry = true;
    } else if (strcmp(lbl, "DEG") == 0 || strcmp(lbl, "RAD") == 0) {
        g_deg_mode = !g_deg_mode;
    } else {
        apply_unary(lbl);
    }
}

static void toggle_calc_mode(void) {
    if (g_mode == MODE_DESK) {
        g_mode = MODE_SCI;
        /* Keep cursor on the same numpad key when switching Desk -> Sci */
        if (g_cur_col >= 1) {
            g_cur_col += 3;
        } else {
            g_cur_col = 3;
        }
        if (g_cur_col >= SCI_COLS) g_cur_col = SCI_COLS - 1;
    } else {
        g_mode = MODE_DESK;
        if (g_cur_col >= 4) {
            g_cur_col -= 3;
        } else {
            g_cur_col = 0;
        }
        if (g_cur_col >= DESK_COLS) g_cur_col = DESK_COLS - 1;
    }
}

static KeyCategory classify_key(const char *lbl) {
    if ((strlen(lbl) == 1 && lbl[0] >= '0' && lbl[0] <= '9') ||
        strcmp(lbl, ".") == 0 || strcmp(lbl, "+/-") == 0) {
        return KEY_CAT_DIGIT;
    }
    if (strcmp(lbl, "=") == 0) return KEY_CAT_EQUALS;
    if (strcmp(lbl, "C") == 0 || strcmp(lbl, "DEL") == 0) return KEY_CAT_CLEAR;
    if (strcmp(lbl, "+") == 0 || strcmp(lbl, "-") == 0 ||
        strcmp(lbl, "*") == 0 || strcmp(lbl, "/") == 0) {
        return KEY_CAT_OP;
    }
    if (strcmp(lbl, "MC") == 0 || strcmp(lbl, "MR") == 0 ||
        strcmp(lbl, "M+") == 0 || strcmp(lbl, "M-") == 0 ||
        strcmp(lbl, "%") == 0) {
        return KEY_CAT_MEM;
    }
    return KEY_CAT_SCI;
}

static bool is_pending_op_key(const char *lbl) {
    if (g_pending_op == OP_ADD && strcmp(lbl, "+") == 0) return true;
    if (g_pending_op == OP_SUB && strcmp(lbl, "-") == 0) return true;
    if (g_pending_op == OP_MUL && strcmp(lbl, "*") == 0) return true;
    if (g_pending_op == OP_DIV && strcmp(lbl, "/") == 0) return true;
    if (g_pending_op == OP_POW && strcmp(lbl, "x^y") == 0) return true;
    if (g_pending_op == OP_MOD && strcmp(lbl, "mod") == 0) return true;
    return false;
}

/* =========================================================================
 * UI Rendering (320x240, Edge-to-Edge Display, Zero Gray/Black Outer Borders)
 * ========================================================================= */
static void render_ui(SDL_Surface *screen) {
    /* 1. Chassis background */
    fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(15, 20, 32));

    /* 2. Full-width Edge-to-Edge Top Display Panel (x=0..319, y=0..71) */
    fill_vgradient(0, 0, SCREEN_W, 71, RGB(20, 28, 46), RGB(27, 38, 60));
    /* Subtle 1px accent divider under display */
    fill_rect(0, 71, SCREEN_W, 1, RGB(56, 74, 106));

    /* Top Status Bar Badges (y=5..20): [BASIC] [SCI] mode indicator pills */
    if (g_mode == MODE_DESK) {
        draw_pill(6, 5, 42, 15, RGB(37, 99, 235), RGB(96, 165, 250));
        draw_text_centered(FONT_10, 6, 5, 42, 15, "BASIC", RGB(255, 255, 255));

        draw_pill(52, 5, 32, 15, RGB(15, 23, 42), RGB(56, 74, 106));
        draw_text_centered(FONT_10, 52, 5, 32, 15, "SCI", RGB(100, 116, 139));
    } else {
        draw_pill(6, 5, 42, 15, RGB(15, 23, 42), RGB(56, 74, 106));
        draw_text_centered(FONT_10, 6, 5, 42, 15, "BASIC", RGB(100, 116, 139));

        draw_pill(52, 5, 32, 15, RGB(124, 58, 237), RGB(167, 139, 250));
        draw_text_centered(FONT_10, 52, 5, 32, 15, "SCI", RGB(255, 255, 255));
    }

    int badge_x = 88;
    if (fabs(g_memory) > 1e-15) {
        draw_pill(badge_x, 5, 18, 15, RGB(180, 83, 9), RGB(251, 191, 36));
        draw_text_centered(FONT_10, badge_x, 5, 18, 15, "M", RGB(255, 255, 255));
    }

    /* Right-aligned Secondary Expression / Operation History */
    if (g_expr[0] != '\0') {
        draw_text_right(FONT_13, SCREEN_W - 8, 5, g_expr, RGB(148, 163, 184));
    }

    /* Main Large Numeric Readout (y=24..67, right-aligned at x=312) */
    size_t dlen = strlen(g_display);
    int disp_font = FONT_30;
    int disp_y = 26;
    if (dlen > 21) {
        disp_font = FONT_16;
        disp_y = 36;
    } else if (dlen > 15) {
        disp_font = FONT_22;
        disp_y = 31;
    }
    uint32_t disp_col = g_error ? RGB(248, 113, 113) : RGB(248, 250, 252);
    draw_text_right(disp_font, SCREEN_W - 8, disp_y, g_display, disp_col);

    /* 3. Keypad Grid (y=74..223) */
    int rows = (g_mode == MODE_DESK) ? DESK_ROWS : SCI_ROWS;
    int cols = (g_mode == MODE_DESK) ? DESK_COLS : SCI_COLS;
    int grid_top = 74;
    int grid_h = 150;
    int gap = 2;
    int cell_h = (grid_h - (rows - 1) * gap) / rows; /* 28px */
    int total_w = SCREEN_W - 6;                      /* 314px */
    int cell_w = (total_w - (cols - 1) * gap) / cols;
    int grid_left = (SCREEN_W - (cols * cell_w + (cols - 1) * gap)) / 2;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const char *raw_lbl = (g_mode == MODE_DESK) ? k_desk_grid[r][c] : k_sci_grid[r][c];
            const char *lbl = raw_lbl;
            if (strcmp(raw_lbl, "DEG") == 0 && !g_deg_mode) {
                lbl = "RAD";
            }

            int bx = grid_left + c * (cell_w + gap);
            int by = grid_top  + r * (cell_h + gap);
            bool focused = (r == g_cur_row && c == g_cur_col);
            bool active_op = is_pending_op_key(raw_lbl);

            KeyCategory cat = classify_key(raw_lbl);
            uint32_t base_col = RGB(40, 52, 74);
            uint32_t txt_col  = RGB(248, 250, 252);

            switch (cat) {
                case KEY_CAT_DIGIT:
                    base_col = RGB(42, 54, 78);
                    txt_col  = RGB(248, 250, 252);
                    break;
                case KEY_CAT_OP:
                    base_col = active_op ? RGB(59, 130, 246) : RGB(29, 78, 216);
                    txt_col  = RGB(255, 255, 255);
                    break;
                case KEY_CAT_EQUALS:
                    base_col = RGB(5, 150, 105);
                    txt_col  = RGB(255, 255, 255);
                    break;
                case KEY_CAT_CLEAR:
                    base_col = (strcmp(raw_lbl, "C") == 0) ? RGB(185, 28, 28) : RGB(145, 32, 46);
                    txt_col  = RGB(254, 242, 242);
                    break;
                case KEY_CAT_MEM:
                    base_col = RGB(30, 41, 62);
                    txt_col  = RGB(251, 191, 36);
                    break;
                case KEY_CAT_SCI:
                    base_col = active_op ? RGB(99, 102, 241) : RGB(52, 39, 96);
                    txt_col  = (strcmp(raw_lbl, "DEG") == 0)
                               ? (g_deg_mode ? RGB(56, 189, 248) : RGB(251, 191, 36))
                               : RGB(224, 231, 255);
                    break;
            }

            if (focused) {
                txt_col = RGB(255, 255, 255);
            }

            draw_beveled_box(bx, by, cell_w, cell_h, base_col, focused);

            int key_font = (g_mode == MODE_DESK && strlen(lbl) <= 3) ? FONT_16 : FONT_13;
            draw_text_centered(key_font, bx, by, cell_w, cell_h, lbl, txt_col);
        }
    }

    /* 4. Bottom Legend Bar (y=226..239) */
    fill_rect(0, 226, SCREEN_W, 14, RGB(11, 15, 25));
    fill_rect(0, 226, SCREEN_W, 1,  RGB(30, 41, 59));
    draw_text_centered(FONT_10, 0, 227, SCREEN_W, 13,
                       "A:Input  SELECT:Basic/Sci  X:Clr  Y:Help  MENU:Quit",
                       RGB(148, 163, 184));

    if (g_show_help) {
        int mx = 16, my = 18, mw = SCREEN_W - 32, mh = SCREEN_H - 36;
        fill_rect(mx, my, mw, mh, RGB(15, 23, 42));
        draw_pill(mx, my, mw, mh, RGB(15, 23, 42), RGB(59, 130, 246));
        fill_rect(mx + 2, my + 2, mw - 4, 22, RGB(30, 41, 59));
        fill_rect(mx + 2, my + 24, mw - 4, 1, RGB(56, 74, 106));
        draw_pill(mx + 6, my + 5, 42, 15, RGB(37, 99, 235), RGB(96, 165, 250));
        draw_text_centered(FONT_10, mx + 6, my + 5, 42, 15, "HELP", RGB(255, 255, 255));
        draw_text(FONT_13, mx + 54, my + 5, "CALCULATOR CONTROLS", RGB(248, 250, 252));

        static const struct {
            const char *btn;
            const char *desc;
        } k_rows[7] = {
            { "D-PAD",     "Navigate Keypad Grid" },
            { "A Button",  "Input Selected Key" },
            { "SELECT",    "Switch Basic <-> Scientific Mode" },
            { "X Button",  "Clear All (C / AC)" },
            { "R1 / L1",   "R1: Backspace (DEL) | L1: DEG/RAD" },
            { "Y Button",  "Open / Close Help Screen" },
            { "MENU",      "Quit Calculator" }
        };
        int ky = my + 31;
        for (int i = 0; i < 7; i++) {
            int ry = ky + i * 21;
            draw_pill(mx + 8, ry, 68, 16, RGB(30, 58, 95), RGB(56, 189, 248));
            draw_text_centered(FONT_10, mx + 8, ry, 68, 16, k_rows[i].btn, RGB(254, 240, 138));
            draw_text(FONT_10, mx + 84, ry + 3, k_rows[i].desc, RGB(226, 232, 240));
        }
        fill_rect(mx + 2, my + mh - 18, mw - 4, 16, RGB(11, 15, 25));
        draw_text_centered(FONT_10, mx, my + mh - 17, mw, 15,
                           "Press Y, A, or B to close Help",
                           RGB(147, 197, 253));
    }

    present_frame(screen);
}

/* =========================================================================
 * Main Event Loop
 * ========================================================================= */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

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
    SDL_WM_SetCaption("PowKiddy V90 Calculator", NULL);

    /* Automated demo screenshot generation mode if CALC_DEMO_DIR is set */
    const char *demo_dir = getenv("CALC_DEMO_DIR");
    if (demo_dir && demo_dir[0] != '\0') {
        char path[512];

        /* 1. Desk Mode screenshot */
        g_mode = MODE_DESK;
        clear_all();
        activate_key_label("1");
        activate_key_label("2");
        activate_key_label("8");
        activate_key_label("*");
        activate_key_label("4");
        activate_key_label("5");
        activate_key_label("=");
        g_memory = 5760.0;
        g_cur_row = 4;
        g_cur_col = 4; /* '=' focused */
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/calc_desk.bmp", demo_dir);
        save_bmp(path);

        /* 2. Scientific Mode screenshot */
        toggle_calc_mode(); /* Switches to MODE_SCI */
        clear_all();
        g_memory = 42.0;
        activate_key_label("4");
        activate_key_label("5");
        activate_key_label("sin");
        g_cur_row = 0;
        g_cur_col = 0; /* 'sin' focused */
        render_ui(screen);
        snprintf(path, sizeof(path), "%s/calc_sci.bmp", demo_dir);
        save_bmp(path);

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
                int rows = (g_mode == MODE_DESK) ? DESK_ROWS : SCI_ROWS;
                int cols = (g_mode == MODE_DESK) ? DESK_COLS : SCI_COLS;

                /* MENU (or Q on desktop) quits the calculator */
                if (k == V90_BTN_MENU || k == SDLK_q) {
                    running = false;
                    break;
                }
                /* Y (or H on desktop) toggles the Help modal */
                else if (k == V90_BTN_Y || k == SDLK_y || k == SDLK_h) {
                    g_show_help = !g_show_help;
                } else if (g_show_help) {
                    if (k == V90_BTN_A || k == SDLK_a || k == V90_BTN_B || k == SDLK_b ||
                        k == V90_BTN_SELECT) {
                        g_show_help = false;
                    }
                }
                /* SELECT (or Home on Linux desktop) toggles between Basic and Scientific mode */
                else if (k == V90_BTN_SELECT || k == SDLK_HOME) {
                    toggle_calc_mode();
                }
                /* D-Pad Navigation */
                else if (k == V90_BTN_UP) {
                    g_cur_row = (g_cur_row + rows - 1) % rows;
                } else if (k == V90_BTN_DOWN) {
                    g_cur_row = (g_cur_row + 1) % rows;
                } else if (k == V90_BTN_LEFT) {
                    g_cur_col = (g_cur_col + cols - 1) % cols;
                } else if (k == V90_BTN_RIGHT) {
                    g_cur_col = (g_cur_col + 1) % cols;
                }
                /* A (or 'A' on desktop) activates the focused button */
                else if (k == V90_BTN_A || k == SDLK_a) {
                    const char *lbl = (g_mode == MODE_DESK)
                                      ? k_desk_grid[g_cur_row][g_cur_col]
                                      : k_sci_grid[g_cur_row][g_cur_col];
                    activate_key_label(lbl);
                }
                /* X button (or 'X' on desktop): Clear (C/AC) */
                else if (k == V90_BTN_X || k == SDLK_x) {
                    clear_all();
                }
                /* R1 (Backspace): Delete last digit (DEL) */
                else if (k == V90_BTN_R1) {
                    backspace_digit();
                }
                /* L1 (Tab): Toggle DEG / RAD */
                else if (k == V90_BTN_L1) {
                    g_deg_mode = !g_deg_mode;
                }
                /* Direct keyboard shortcuts for desktop convenience */
                else if (k >= SDLK_0 && k <= SDLK_9) {
                    input_digit((char)('0' + (k - SDLK_0)));
                } else if (k == SDLK_PERIOD) {
                    input_digit('.');
                } else if (k == SDLK_PLUS) {
                    start_binary_op(OP_ADD);
                } else if (k == SDLK_MINUS) {
                    start_binary_op(OP_SUB);
                } else if (k == SDLK_ASTERISK) {
                    start_binary_op(OP_MUL);
                } else if (k == SDLK_SLASH) {
                    start_binary_op(OP_DIV);
                } else if (k == SDLK_CARET) {
                    start_binary_op(OP_POW);
                } else if (k == SDLK_PERCENT) {
                    apply_unary("%");
                } else if (k == SDLK_EQUALS) {
                    press_equals();
                } else if (k == SDLK_c) {
                    clear_all();
                }
            }
        }
        render_ui(screen);
        SDL_Delay(16);
    }

    SDL_Quit();
    return 0;
}
