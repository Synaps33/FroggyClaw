#ifndef SDL_COMPAT_SDL_H
#define SDL_COMPAT_SDL_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic integer types matching SDL */
typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t   Sint8;
typedef int16_t  Sint16;
typedef int32_t  Sint32;
typedef int64_t  Sint64;

typedef enum {
    SDL_FALSE = 0,
    SDL_TRUE = 1
} SDL_bool;

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN

/* Geometry types */
typedef struct SDL_Point {
    int x;
    int y;
} SDL_Point;

typedef struct SDL_Rect {
    int x, y;
    int w, h;
} SDL_Rect;

typedef struct SDL_Color {
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a;
} SDL_Color;

/* Blit & Render enums */
typedef enum {
    SDL_FLIP_NONE       = 0x00000000,
    SDL_FLIP_HORIZONTAL = 0x00000001,
    SDL_FLIP_VERTICAL   = 0x00000002
} SDL_RendererFlip;

typedef enum {
    SDL_BLENDMODE_NONE  = 0x00000000,
    SDL_BLENDMODE_BLEND = 0x00000001,
    SDL_BLENDMODE_ADD   = 0x00000002,
    SDL_BLENDMODE_MOD   = 0x00000004
} SDL_BlendMode;

#define SDL_PIXELFORMAT_UNKNOWN      0
#define SDL_PIXELFORMAT_RGB565       0x15121002
#define SDL_PIXELFORMAT_ARGB8888     0x16362004
#define SDL_PIXELFORMAT_RGBA8888     0x16462004

/* Surface format */
typedef struct SDL_PixelFormat {
    Uint32 format;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint32 Rmask, Gmask, Bmask, Amask;
    Uint8 Rloss, Gloss, Bloss, Aloss;
    Uint8 Rshift, Gshift, Bshift, Ashift;
} SDL_PixelFormat;

/* Surfaces created with SDL_CreateRGBSurfaceFrom borrow their pixel buffer and
 * must not free it; this bit distinguishes them from owned surfaces. */
#define SDL_SURFACE_EXTERNAL_PIXELS 0x80000000u

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    void *lock_data;
    SDL_Rect clip_rect;
    int refcount;
    int has_colorkey;
    Uint32 colorkey;
} SDL_Surface;

/* Window & Renderer & Texture */
typedef struct SDL_Window SDL_Window;

/* Texture storage: RGB565 colour plane plus an optional 1-bit-per-pixel
 * transparency plane. All of the game's sprites are palettised, so per-pixel
 * alpha is always binary - a bitmap is 8x smaller than a byte per pixel and
 * keeps whole-screen blits inside the SF2000's cache budget. */
typedef struct SDL_Texture {
    int w, h;
    Uint32 format;
    int access;
    uint16_t* pixels565;      /* w * h RGB565 texels */
    uint32_t* alphamask;      /* bit set = opaque; NULL means fully opaque */
    int mask_stride_words;    /* 32 texels per word */
    bool has_alpha;
    Uint8 alpha_mod;
    Uint8 r_mod, g_mod, b_mod;
    SDL_BlendMode blendMode;
} SDL_Texture;

typedef struct SDL_Renderer {
    SDL_Window* window;
    int width, height;
    float scaleX, scaleY;
    SDL_Color drawColor;
    uint16_t drawColor565;
    uint16_t* framebuffer; /* Pointer to 320x240 RGB565 buffer */
} SDL_Renderer;

#define SDL_WINDOWPOS_UNDEFINED 0
#define SDL_WINDOW_SHOWN        0x00000004
#define SDL_WINDOW_FULLSCREEN   0x00000001
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000)

#define SDL_RENDERER_ACCELERATED 0x00000002
#define SDL_RENDERER_PRESENTVSYNC 0x00000004

/* Init flags */
#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_EVENTS         0x00004000u

/* RWops */
#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2

typedef struct SDL_RWops {
    Sint64 (*size)(struct SDL_RWops *context);
    Sint64 (*seek)(struct SDL_RWops *context, Sint64 offset, int whence);
    size_t (*read)(struct SDL_RWops *context, void *ptr, size_t size, size_t maxnum);
    size_t (*write)(struct SDL_RWops *context, const void *ptr, size_t size, size_t num);
    int (*close)(struct SDL_RWops *context);
    Uint32 type;
    union {
        struct {
            Uint8 *base;
            Uint8 *here;
            Uint8 *stop;
        } mem;
        struct {
            FILE *fp;
        } stdio;
        void *unknown;
    } hidden;
} SDL_RWops;

/* Keys and Scancodes */
typedef int32_t SDL_Keycode;

enum {
    SDLK_UNKNOWN = 0,
    SDLK_RETURN = '\r',
    SDLK_ESCAPE = 27,
    SDLK_BACKSPACE = '\b',
    SDLK_TAB = '\t',
    SDLK_SPACE = ' ',
    SDLK_a = 'a',
    SDLK_b = 'b',
    SDLK_c = 'c',
    SDLK_d = 'd',
    SDLK_e = 'e',
    SDLK_s = 's',
    SDLK_w = 'w',

    SDLK_UP = (1 << 30) | 82,
    SDLK_DOWN = (1 << 30) | 81,
    SDLK_LEFT = (1 << 30) | 80,
    SDLK_RIGHT = (1 << 30) | 79,
    SDLK_PAGEUP = (1 << 30) | 75,
    SDLK_PAGEDOWN = (1 << 30) | 78,
    SDLK_KP_ENTER = (1 << 30) | 88,
    SDLK_LCTRL = (1 << 30) | 224,
    SDLK_LSHIFT = (1 << 30) | 225,
    SDLK_LALT = (1 << 30) | 226,
    SDLK_RSHIFT = (1 << 30) | 229
};

typedef enum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4,
    SDL_SCANCODE_B = 5,
    SDL_SCANCODE_C = 6,
    SDL_SCANCODE_D = 7,
    SDL_SCANCODE_E = 8,
    SDL_SCANCODE_F = 9,
    SDL_SCANCODE_G = 10,
    SDL_SCANCODE_H = 11,
    SDL_SCANCODE_I = 12,
    SDL_SCANCODE_J = 13,
    SDL_SCANCODE_K = 14,
    SDL_SCANCODE_L = 15,
    SDL_SCANCODE_M = 16,
    SDL_SCANCODE_N = 17,
    SDL_SCANCODE_O = 18,
    SDL_SCANCODE_P = 19,
    SDL_SCANCODE_Q = 20,
    SDL_SCANCODE_R = 21,
    SDL_SCANCODE_S = 22,
    SDL_SCANCODE_T = 23,
    SDL_SCANCODE_U = 24,
    SDL_SCANCODE_V = 25,
    SDL_SCANCODE_W = 26,
    SDL_SCANCODE_X = 27,
    SDL_SCANCODE_Y = 28,
    SDL_SCANCODE_Z = 29,

    SDL_SCANCODE_1 = 30,
    SDL_SCANCODE_2 = 31,
    SDL_SCANCODE_3 = 32,
    SDL_SCANCODE_4 = 33,
    SDL_SCANCODE_5 = 34,
    SDL_SCANCODE_6 = 35,
    SDL_SCANCODE_7 = 36,
    SDL_SCANCODE_8 = 37,
    SDL_SCANCODE_9 = 38,
    SDL_SCANCODE_0 = 39,

    SDL_SCANCODE_RETURN = 40,
    SDL_SCANCODE_ESCAPE = 41,
    SDL_SCANCODE_BACKSPACE = 42,
    SDL_SCANCODE_TAB = 43,
    SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_GRAVE = 53,

    SDL_SCANCODE_RIGHT = 79,
    SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_DOWN = 81,
    SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_PAGEUP = 75,
    SDL_SCANCODE_PAGEDOWN = 78,
    SDL_SCANCODE_HOME = 74,
    SDL_SCANCODE_END = 77,

    SDL_SCANCODE_KP_ENTER = 88,
    SDL_SCANCODE_LCTRL = 224,
    SDL_SCANCODE_LSHIFT = 225,
    SDL_SCANCODE_LALT = 226,
    SDL_SCANCODE_RSHIFT = 229,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;

/* Events */
enum {
    SDL_FIRSTEVENT = 0,
    SDL_QUIT = 0x100,
    SDL_APP_TERMINATING,
    SDL_APP_LOWMEMORY,
    SDL_APP_DIDENTERBACKGROUND,
    SDL_APP_DIDENTERFOREGROUND,
    SDL_WINDOWEVENT = 0x200,
    SDL_KEYDOWN = 0x300,
    SDL_KEYUP,
    SDL_TEXTEDITING,
    SDL_TEXTINPUT,
    SDL_MOUSEMOTION = 0x400,
    SDL_MOUSEBUTTONDOWN,
    SDL_MOUSEBUTTONUP,
    SDL_MOUSEWHEEL,
    SDL_FINGERDOWN = 0x700,
    SDL_FINGERUP,
    SDL_FINGERMOTION,
    SDL_USEREVENT = 0x8000
};

enum {
    SDL_WINDOWEVENT_NONE,
    SDL_WINDOWEVENT_SHOWN,
    SDL_WINDOWEVENT_HIDDEN,
    SDL_WINDOWEVENT_EXPOSED,
    SDL_WINDOWEVENT_MOVED,
    SDL_WINDOWEVENT_RESIZED,
    SDL_WINDOWEVENT_SIZE_CHANGED,
    SDL_WINDOWEVENT_MINIMIZED,
    SDL_WINDOWEVENT_MAXIMIZED,
    SDL_WINDOWEVENT_RESTORED
};

#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_RIGHT  3

/* Keyboard event state */
#define SDL_RELEASED 0
#define SDL_PRESSED  1

/* Key modifier masks (only NONE is needed: OpenClaw ignores modifiers) */
typedef enum {
    KMOD_NONE   = 0x0000,
    KMOD_LSHIFT = 0x0001,
    KMOD_RSHIFT = 0x0002,
    KMOD_LCTRL  = 0x0040,
    KMOD_RCTRL  = 0x0080,
    KMOD_LALT   = 0x0100,
    KMOD_RALT   = 0x0200
} SDL_Keymod;

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    Uint16 mod;
    Uint32 unused;
} SDL_Keysym;

typedef struct SDL_KeyboardEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 state;
    Uint8 repeat;
    Uint8 padding2;
    Uint8 padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseMotionEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint32 state;
    Sint32 x;
    Sint32 y;
    Sint32 xrel;
    Sint32 yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 clicks;
    Uint8 padding1;
    Sint32 x;
    Sint32 y;
} SDL_MouseButtonEvent;

typedef int64_t SDL_FingerID;
typedef struct SDL_TouchFingerEvent {
    Uint32 type;
    Uint32 timestamp;
    int64_t touchId;
    SDL_FingerID fingerId;
    float x;
    float y;
    float dx;
    float dy;
    float pressure;
} SDL_TouchFingerEvent;

typedef struct SDL_WindowEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 event;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint32 data1;
    Sint32 data2;
} SDL_WindowEvent;

typedef struct SDL_UserEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Sint32 code;
    void *data1;
    void *data2;
} SDL_UserEvent;

typedef struct SDL_QuitEvent {
    Uint32 type;
    Uint32 timestamp;
} SDL_QuitEvent;

typedef struct SDL_MouseWheelEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Sint32 x;
    Sint32 y;
    Uint32 direction;
} SDL_MouseWheelEvent;

typedef struct SDL_TextInputEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    char text[32];
} SDL_TextInputEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_TouchFingerEvent tfinger;
    SDL_WindowEvent window;
    SDL_QuitEvent quit;
    SDL_UserEvent user;
    SDL_TextInputEvent text;
    Uint8 padding[64];
} SDL_Event;

/* Threads */
typedef struct SDL_Thread SDL_Thread;
typedef enum {
    SDL_THREAD_PRIORITY_LOW,
    SDL_THREAD_PRIORITY_NORMAL,
    SDL_THREAD_PRIORITY_HIGH
} SDL_ThreadPriority;

typedef int (*SDL_ThreadFunction)(void *data);

/* Logging */
#define SDL_LOG_CATEGORY_APPLICATION 0
void SDL_Log(const char *fmt, ...);
void SDL_LogInfo(int category, const char *fmt, ...);
void SDL_LogWarn(int category, const char *fmt, ...);
void SDL_LogError(int category, const char *fmt, ...);

/* System & Error */
int SDL_Init(Uint32 flags);
void SDL_Quit(void);
Uint32 SDL_WasInit(Uint32 flags);
const char* SDL_GetError(void);
void SDL_ClearError(void);
void SDL_SetMainReady(void);
const char* SDL_GetBasePath(void);
Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);

/* Window */
SDL_Window* SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *window);
void SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
Uint32 SDL_GetWindowFlags(SDL_Window *window);
void SDL_SetWindowSize(SDL_Window *window, int w, int h);
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags);

/* Renderer */
SDL_Renderer* SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
int SDL_RenderSetScale(SDL_Renderer *renderer, float scaleX, float scaleY);
void SDL_RenderGetScale(SDL_Renderer *renderer, float *scaleX, float *scaleY);
int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int SDL_GetRenderDrawColor(SDL_Renderer *renderer, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a);
int SDL_RenderClear(SDL_Renderer *renderer);
void SDL_RenderPresent(SDL_Renderer *renderer);
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect);
int SDL_RenderCopyEx(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect,
                     const double angle, const SDL_Point *center, const SDL_RendererFlip flip);
int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int SDL_RenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int SDL_RenderDrawLine(SDL_Renderer *renderer, int x1, int y1, int x2, int y2);
int SDL_RenderDrawPoint(SDL_Renderer *renderer, int x, int y);
int SDL_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch);

/* Texture */
SDL_Texture* SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h);
SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface);
void SDL_DestroyTexture(SDL_Texture *texture);
int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h);
int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode);
int SDL_SetTextureAlphaMod(SDL_Texture *texture, Uint8 alpha);
int SDL_SetTextureColorMod(SDL_Texture *texture, Uint8 r, Uint8 g, Uint8 b);
int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);
int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch);
void SDL_UnlockTexture(SDL_Texture *texture);
SDL_Texture* SDL_CreateTextureFromPid(const void* vpid);

/* Surface */
SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format);
SDL_Surface* SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth, int pitch,
                                      Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);
int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
SDL_Surface* SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void SDL_GetRGBA(Uint32 pixel, const SDL_PixelFormat *format, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a);

/* Geometry helpers */
SDL_bool SDL_HasIntersection(const SDL_Rect *A, const SDL_Rect *B);
SDL_bool SDL_IntersectRect(const SDL_Rect *A, const SDL_Rect *B, SDL_Rect *result);

/* Events & Input */
void SDL_PumpEvents(void);
int SDL_PollEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);
const Uint8* SDL_GetKeyboardState(int *numkeys);
SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key);
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode);

/* RWops */
SDL_RWops* SDL_RWFromMem(void *mem, int size);
SDL_RWops* SDL_RWFromFile(const char *file, const char *mode);
size_t SDL_RWread(SDL_RWops *context, void *ptr, size_t size, size_t maxnum);
size_t SDL_RWwrite(SDL_RWops *context, const void *ptr, size_t size, size_t num);
Sint64 SDL_RWseek(SDL_RWops *context, Sint64 offset, int whence);
Sint64 SDL_RWtell(SDL_RWops *context);
int SDL_RWclose(SDL_RWops *context);

/* Threads */
SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void SDL_DetachThread(SDL_Thread *thread);
int SDL_SetThreadPriority(SDL_ThreadPriority priority);

#ifdef __cplusplus
}
#endif

#endif /* SDL_COMPAT_SDL_H */
