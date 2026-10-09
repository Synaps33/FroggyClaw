/*
 * sdl_compat - core subsystem: init/quit, errors, timing, threads, RWops, logging.
 *
 * These functions are the "flat" parts of SDL2 that OpenClaw touches but that
 * need no emulation: on SF2000 the libretro host owns the display, the clock
 * and the filesystem, so we only track state here.
 */
#include "sdl_compat.h"

#include <stdarg.h>
#include <time.h>

/* ------------------------------------------------------------------ state */

static char     s_error[512]      = {0};
static uint32_t s_ticks           = 0;
static uint32_t s_init_flags      = 0;
static char     s_base_path[512]  = {0};
static int      s_log_enabled     = 1;

void sdl_compat_set_base_path(const char* path)
{
    if (path == NULL) {
        s_base_path[0] = '\0';
        return;
    }
    size_t n = strlen(path);
    if (n >= sizeof(s_base_path))
        n = sizeof(s_base_path) - 1;
    memcpy(s_base_path, path, n);
    s_base_path[n] = '\0';
}

const char* sdl_compat_get_base_path(void)
{
    return s_base_path;
}

void sdl_compat_advance_ticks(uint32_t ms)
{
    s_ticks += ms;
}

/* ------------------------------------------------------------------ error */

static void set_error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_error, sizeof(s_error), fmt, ap);
    va_end(ap);
}

const char* SDL_GetError(void)
{
    return s_error;
}

void SDL_ClearError(void)
{
    s_error[0] = '\0';
}

/* ---------------------------------------------------------------- logging */

void SDL_SetLogEnabled(int enabled)
{
    s_log_enabled = enabled;
}

static void log_print(const char* prefix, const char* fmt, va_list ap)
{
    (void)ap;
    if (!s_log_enabled)
        return;
    fprintf(stderr, "[sdlcompat] %s", prefix);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void SDL_Log(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); log_print("log: ", fmt, ap); va_end(ap);
}

void SDL_LogInfo(int category, const char* fmt, ...)
{
    (void)category;
    va_list ap; va_start(ap, fmt); log_print("info: ", fmt, ap); va_end(ap);
}

void SDL_LogWarn(int category, const char* fmt, ...)
{
    (void)category;
    va_list ap; va_start(ap, fmt); log_print("warn: ", fmt, ap); va_end(ap);
}

void SDL_LogError(int category, const char* fmt, ...)
{
    (void)category;
    va_list ap; va_start(ap, fmt); log_print("error: ", fmt, ap); va_end(ap);
}

/* ------------------------------------------------------------------- init */

int SDL_Init(Uint32 flags)
{
    /* The host provides video and audio; the "subsystems" only gate the flags
     * OpenClaw queries afterwards, so accepting them is enough. */
    s_init_flags |= flags;
    return 0;
}

void SDL_Quit(void)
{
    s_init_flags = 0;
}

Uint32 SDL_WasInit(Uint32 flags)
{
    return flags ? (s_init_flags & flags) : s_init_flags;
}

void SDL_SetMainReady(void)
{
}

const char* SDL_GetBasePath(void)
{
    return s_base_path;
}

/* ------------------------------------------------------------------ timing */

Uint32 SDL_GetTicks(void)
{
    return s_ticks;
}

void SDL_Delay(Uint32 ms)
{
    sdl_compat_advance_ticks(ms);
}

#include <sys/time.h>

Uint64 SDL_GetPerformanceCounter(void)
{
    /* Real monotonic/wall time, unlike SDL_GetTicks which the core advances by a
     * fixed step per frame. Game logic must stay deterministic and independent
     * of how slow rendering is on the console, but the engine's PROFILE_CPU
     * timers do want wall-clock, otherwise every measurement reads zero. */
#if defined(__linux__) && !defined(__mips__)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (Uint64)ts.tv_sec * 1000000000ull + (Uint64)ts.tv_nsec;
#endif
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
        return (Uint64)tv.tv_sec * 1000000ull + (Uint64)tv.tv_usec;
    return (Uint64)s_ticks * 1000ull;
}

Uint64 SDL_GetPerformanceFrequency(void)
{
#if defined(__linux__) && !defined(__mips__)
    return 1000000000ull; /* nanoseconds */
#else
    return 1000000ull;    /* microseconds */
#endif
}

/* --------------------------------------------------------------- geometry */

SDL_bool SDL_HasIntersection(const SDL_Rect* A, const SDL_Rect* B)
{
    if (A == NULL || B == NULL)
        return SDL_FALSE;
    if (A->w <= 0 || A->h <= 0 || B->w <= 0 || B->h <= 0)
        return SDL_FALSE;
    if (A->x + A->w <= B->x) return SDL_FALSE;
    if (B->x + B->w <= A->x) return SDL_FALSE;
    if (A->y + A->h <= B->y) return SDL_FALSE;
    if (B->y + B->h <= A->y) return SDL_FALSE;
    return SDL_TRUE;
}

SDL_bool SDL_IntersectRect(const SDL_Rect* A, const SDL_Rect* B, SDL_Rect* result)
{
    int x0, y0, x1, y1;

    if (!SDL_HasIntersection(A, B))
        return SDL_FALSE;

    x0 = (A->x > B->x) ? A->x : B->x;
    y0 = (A->y > B->y) ? A->y : B->y;
    x1 = (A->x + A->w < B->x + B->w) ? A->x + A->w : B->x + B->w;
    y1 = (A->y + A->h < B->y + B->h) ? A->y + A->h : B->y + B->h;

    if (result) {
        result->x = x0;
        result->y = y0;
        result->w = x1 - x0;
        result->h = y1 - y0;
    }
    return SDL_TRUE;
}

/* --------------------------------------------------------------- RWops ---- */

static Sint64 rw_size_mem(SDL_RWops* c)
{
    return (Sint64)(c->hidden.mem.stop - c->hidden.mem.base);
}

static Sint64 rw_seek_mem(SDL_RWops* c, Sint64 offset, int whence)
{
    Uint8* target;

    switch (whence) {
    case RW_SEEK_SET: target = c->hidden.mem.base + offset; break;
    case RW_SEEK_CUR: target = c->hidden.mem.here + offset; break;
    case RW_SEEK_END: target = c->hidden.mem.stop + offset; break;
    default: set_error("sdl_compat: invalid whence %d", whence); return -1;
    }

    if (target < c->hidden.mem.base || target > c->hidden.mem.stop) {
        set_error("sdl_compat: seek out of range");
        return -1;
    }
    c->hidden.mem.here = target;
    return (Sint64)(target - c->hidden.mem.base);
}

static size_t rw_read_mem(SDL_RWops* c, void* ptr, size_t size, size_t maxnum)
{
    size_t avail, want, i;
    Uint8* dst = (Uint8*)ptr;

    if (size == 0 || maxnum == 0)
        return 0;

    avail = (size_t)(c->hidden.mem.stop - c->hidden.mem.here);
    want  = size * maxnum;
    if (want > avail)
        maxnum = avail / size;

    for (i = 0; i < maxnum * size; i++)
        dst[i] = c->hidden.mem.here[i];

    c->hidden.mem.here += maxnum * size;
    return maxnum;
}

static size_t rw_write_mem(SDL_RWops* c, const void* ptr, size_t size, size_t num)
{
    size_t avail = (size_t)(c->hidden.mem.stop - c->hidden.mem.here);
    size_t want = size * num;

    if (want > avail)
        return 0;

    memcpy(c->hidden.mem.here, ptr, want);
    c->hidden.mem.here += want;
    return num;
}

static int rw_close_free(SDL_RWops* c)
{
    if (c == NULL)
        return 0;
    free(c);
    return 0;
}

SDL_RWops* SDL_RWFromMem(void* mem, int size)
{
    SDL_RWops* rw;

    if (mem == NULL || size <= 0) {
        set_error("sdl_compat: SDL_RWFromMem invalid args");
        return NULL;
    }

    rw = (SDL_RWops*)calloc(1, sizeof(SDL_RWops));
    if (rw == NULL) {
        set_error("sdl_compat: out of memory");
        return NULL;
    }

    rw->size        = rw_size_mem;
    rw->seek        = rw_seek_mem;
    rw->read        = rw_read_mem;
    rw->write       = rw_write_mem;
    rw->close       = rw_close_free;
    rw->type        = 3; /* SDL_RWOPS_MEMORY */
    rw->hidden.mem.base = (Uint8*)mem;
    rw->hidden.mem.here = (Uint8*)mem;
    rw->hidden.mem.stop = (Uint8*)mem + size;
    return rw;
}

static Sint64 rw_size_stdio(SDL_RWops* c)
{
    long cur = ftell(c->hidden.stdio.fp);
    long end;
    if (cur < 0) return -1;
    if (fseek(c->hidden.stdio.fp, 0, SEEK_END) != 0) return -1;
    end = ftell(c->hidden.stdio.fp);
    fseek(c->hidden.stdio.fp, cur, SEEK_SET);
    return (Sint64)(end - cur);
}

static Sint64 rw_seek_stdio(SDL_RWops* c, Sint64 offset, int whence)
{
    int w = (whence == RW_SEEK_SET) ? SEEK_SET
          : (whence == RW_SEEK_CUR) ? SEEK_CUR : SEEK_END;
    if (fseek(c->hidden.stdio.fp, (long)offset, w) != 0)
        return -1;
    return (Sint64)ftell(c->hidden.stdio.fp);
}

static size_t rw_read_stdio(SDL_RWops* c, void* ptr, size_t size, size_t maxnum)
{
    return fread(ptr, size, maxnum, c->hidden.stdio.fp);
}

static size_t rw_write_stdio(SDL_RWops* c, const void* ptr, size_t size, size_t num)
{
    return fwrite(ptr, size, num, c->hidden.stdio.fp);
}

static int rw_close_stdio(SDL_RWops* c)
{
    int r = 0;
    if (c == NULL)
        return 0;
    if (c->hidden.stdio.fp)
        r = fclose(c->hidden.stdio.fp);
    free(c);
    return r;
}

SDL_RWops* SDL_RWFromFile(const char* file, const char* mode)
{
    SDL_RWops* rw;
    FILE* fp;

    if (file == NULL || mode == NULL) {
        set_error("sdl_compat: SDL_RWFromFile invalid args");
        return NULL;
    }

    fp = fopen(file, mode);
    if (fp == NULL) {
        set_error("sdl_compat: could not open %s", file);
        return NULL;
    }

    rw = (SDL_RWops*)calloc(1, sizeof(SDL_RWops));
    if (rw == NULL) {
        fclose(fp);
        set_error("sdl_compat: out of memory");
        return NULL;
    }

    rw->size  = rw_size_stdio;
    rw->seek  = rw_seek_stdio;
    rw->read  = rw_read_stdio;
    rw->write = rw_write_stdio;
    rw->close = rw_close_stdio;
    rw->type  = 2; /* SDL_RWOPS_STDFILE */
    rw->hidden.stdio.fp = fp;
    return rw;
}

size_t SDL_RWread(SDL_RWops* c, void* ptr, size_t size, size_t maxnum)
{
    return c ? c->read(c, ptr, size, maxnum) : 0;
}

size_t SDL_RWwrite(SDL_RWops* c, const void* ptr, size_t size, size_t num)
{
    return c ? c->write(c, ptr, size, num) : 0;
}

Sint64 SDL_RWseek(SDL_RWops* c, Sint64 offset, int whence)
{
    return c ? c->seek(c, offset, whence) : -1;
}

Sint64 SDL_RWtell(SDL_RWops* c)
{
    return c ? c->seek(c, 0, RW_SEEK_CUR) : -1;
}

int SDL_RWclose(SDL_RWops* c)
{
    return c ? c->close(c) : -1;
}

/* ---------------------------------------------------------------- threads */

/* SF2000 has no OS threads worth using: music decoding happens inline instead.
 * Returning a placeholder keeps the (single) caller in Audio.cpp happy. */

SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data)
{
    (void)name;
    if (fn != NULL)
        fn(data); /* run synchronously; decoders here are cheap and non-blocking */
    return (SDL_Thread*)calloc(1, sizeof(int)); /* non-NULL dummy handle */
}

void SDL_DetachThread(SDL_Thread* thread)
{
    (void)thread; /* nothing to join: the work already finished inline */
}

int SDL_SetThreadPriority(SDL_ThreadPriority priority)
{
    (void)priority;
    return 0;
}
