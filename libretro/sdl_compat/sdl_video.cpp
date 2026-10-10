/*
 * sdl_compat - video: window/renderer/surface/texture plus a software blitter
 * that writes straight into the host's RGB565 framebuffer.
 *
 * The SF2000 panel is 320x240 RGB565 and there is no GPU, so every draw call
 * ends up as integer work on a uint16_t buffer. The two things that make this
 * fast on a soft-float MIPS32 core are:
 *   - converting surface colour to RGB565 once, at texture creation time, so
 *     the inner blit loop never touches 32-bit pixels;
 *   - keeping per-pixel transparency in a bitmap so the common "opaque run"
 *     case is a memcpy and the transparent case is one bit test per pixel.
 */
#include "sdl_compat.h"
#include "libwap.h"

#include <stdarg.h>

/* ------------------------------------------------------------------ state */

struct SDL_Window {
    int w, h;
    Uint32 flags;
};

static uint16_t* s_fb          = NULL; /* framebuffer owned by the host */
static int       s_fb_w        = 0;
static int       s_fb_h        = 0;
static uint16_t* s_fb_fallback = NULL; /* used if the host never set one */

/* Output scale.
 *
 * The engine lays out the world in window pixels, and the SF2000 panel is only
 * 320x240. Rendering a 640x480 window into it means everything is drawn a
 * viewport twice as wide and sprites come out half size, which is what makes
 * the level readable on such a small screen. The ratio is reduced by the GCD so
 * window 640x480 -> 320x240 stays an exact 1:2 (and 1280x960 an exact 1:4),
 * meaning no rounding drift and no filtering needed. */
static int s_out_x_num = 1, s_out_x_den = 1;
static int s_out_y_num = 1, s_out_y_den = 1;
static int s_window_w = 640, s_window_h = 480;

/* Live texture pixel bytes (RGB565 + 1-bit alpha masks). These allocations
 * are invisible to the engine's resource-cache accounting, which made them
 * the prime suspect in the 52MB handheld heap OOMs. */
static size_t s_tex_bytes = 0;

size_t sdl_compat_texture_bytes(void)
{
    return s_tex_bytes;
}

static size_t tex_footprint(const SDL_Texture* t)
{
    size_t n = (size_t)t->w * (size_t)t->h * sizeof(uint16_t);

    if (t->alphamask != NULL)
        n += (size_t)t->mask_stride_words * (size_t)t->h * sizeof(uint32_t);
    return n;
}

static int int_gcd(int a, int b)
{
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return (a > 0) ? a : 1;
}

static void update_output_scale(int window_w, int window_h)
{
    int g;

    if (window_w <= 0 || window_h <= 0)
        return;

    s_window_w = window_w;
    s_window_h = window_h;

    g = int_gcd(window_w, s_fb_w);
    s_out_x_num = s_fb_w / g;
    s_out_x_den = window_w / g;

    g = int_gcd(window_h, s_fb_h);
    s_out_y_num = s_fb_h / g;
    s_out_y_den = window_h / g;
}

/* Window coordinate -> framebuffer coordinate. */
static inline int out_x(int x) { return (x * s_out_x_num) / s_out_x_den; }
static inline int out_y(int y) { return (y * s_out_y_num) / s_out_y_den; }

void sdl_compat_set_framebuffer(uint16_t* fb, int width, int height)
{
    s_fb   = fb;
    s_fb_w = width;
    s_fb_h = height;
    update_output_scale(s_window_w, s_window_h);
}

uint16_t* sdl_compat_get_framebuffer(void)
{
    return s_fb;
}

static uint16_t* framebuffer(int* out_w, int* out_h)
{
    if (out_w) *out_w = s_fb_w;
    if (out_h) *out_h = s_fb_h;
    if (s_fb != NULL)
        return s_fb;
    if (s_fb_fallback == NULL) {
        s_fb_fallback = (uint16_t*)calloc((size_t)320 * 240, sizeof(uint16_t));
        s_fb_w = 320;
        s_fb_h = 240;
    }
    return s_fb_fallback;
}

/* ------------------------------------------------------------ colour math */

static inline uint16_t pack_rgb565(Uint8 r, Uint8 g, Uint8 b)
{
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static inline int mask_shift(Uint32 m)
{
    int s = 0;
    if (m == 0) return 0;
    while ((m & 1u) == 0u) { m >>= 1; s++; }
    return s;
}

static inline int mask_bits(Uint32 m)
{
    int n = 0;
    while (m) { n += (int)(m & 1u); m >>= 1; }
    return n;
}

/* Unpack an arbitrary 32-bit surface pixel according to its masks. */
static inline void unpack_rgba(const SDL_PixelFormat* f, Uint32 px,
                               int* r, int* g, int* b, int* a, int* opaque)
{
    int vr = (int)((px & f->Rmask) >> f->Rshift);
    int vg = (int)((px & f->Gmask) >> f->Gshift);
    int vb = (int)((px & f->Bmask) >> f->Bshift);

    switch (f->Rloss) { case 3: vr = (vr * 255 + 3) >> 2; break; case 4: vr = (vr * 255 + 7) >> 3; break;
                        case 5: vr = (vr * 255 + 15) >> 4; break; default: break; }
    switch (f->Gloss) { case 2: vg = (vg * 255 + 1) >> 2; break; case 4: vg = (vg * 255 + 7) >> 3; break;
                        case 5: vg = (vg * 255 + 15) >> 4; break; case 6: vg = (vg * 255 + 31) >> 5; break;
                        default: break; }
    switch (f->Bloss) { case 3: vb = (vb * 255 + 3) >> 2; break; case 4: vb = (vb * 255 + 7) >> 3; break;
                        case 5: vb = (vb * 255 + 15) >> 4; break; default: break; }

    *r = vr; *g = vg; *b = vb;
    *a = 255;
    *opaque = 1;
    if (f->Amask) {
        int va = (int)((px & f->Amask) >> f->Ashift);
        switch (f->Aloss) { case 3: va = (va * 255 + 3) >> 2; break; case 4: va = (va * 255 + 7) >> 3; break;
                            case 5: va = (va * 255 + 15) >> 4; break; default: break; }
        *a = va;
        /* Claw's PAL palettes mark the transparent entry with alpha 1, not 0
         * (see libwap/PalFile.cpp), and every opaque entry uses 255. Treat
         * anything below half as transparent so sprite index 0 disappears
         * instead of being drawn as a solid block. */
        *opaque = (va >= 128) ? 1 : 0;
    }
}

/* --------------------------------------------------------------- surfaces */

static SDL_PixelFormat* alloc_format(int depth,
                                     Uint32 rmask, Uint32 gmask,
                                     Uint32 bmask, Uint32 amask,
                                     bool* has_alpha)
{
    SDL_PixelFormat* f = (SDL_PixelFormat*)calloc(1, sizeof(SDL_PixelFormat));
    if (f == NULL)
        return NULL;

    *has_alpha = false;

    if (rmask == 0 && gmask == 0 && bmask == 0 && amask == 0) {
        /* SDL treats an all-zero mask set as "use the depth default". OpenClaw
         * fills such surfaces through SDL_MapRGB only, which forces alpha 255,
         * so treat them as opaque and skip the alpha unpack entirely. */
        if (depth == 32)      { rmask = 0x000000FFu; gmask = 0x0000FF00u; bmask = 0x00FF0000u; amask = 0; }
        else if (depth == 24) { rmask = 0x000000FFu; gmask = 0x0000FF00u; bmask = 0x00FF0000u; amask = 0; }
        else                  { rmask = 0x0000001Fu; gmask = 0x00007E00u; bmask = 0x001F0000u; amask = 0; }
    } else if (amask != 0) {
        *has_alpha = true;
    }

    f->format        = depth; /* the engine only ever compares this by identity */
    f->BitsPerPixel  = (Uint8)depth;
    f->BytesPerPixel = (Uint8)((depth + 7) / 8);
    f->Rmask = rmask; f->Gmask = gmask; f->Bmask = bmask; f->Amask = amask;
    f->Rshift = (Uint8)mask_shift(rmask);
    f->Gshift = (Uint8)mask_shift(gmask);
    f->Bshift = (Uint8)mask_shift(bmask);
    f->Ashift = (Uint8)mask_shift(amask);
    f->Rloss  = (Uint8)(8 - mask_bits(rmask));
    f->Gloss  = (Uint8)(8 - mask_bits(gmask));
    f->Bloss  = (Uint8)(8 - mask_bits(bmask));
    f->Aloss  = (Uint8)(mask_bits(amask) ? (8 - mask_bits(amask)) : 8);
    return f;
}

SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    SDL_Surface* s;
    SDL_PixelFormat* fmt;
    bool has_alpha;

    (void)flags;
    if (width <= 0 || height <= 0 || (depth != 32 && depth != 24 && depth != 16 && depth != 8))
        return NULL;

    fmt = alloc_format(depth, Rmask, Gmask, Bmask, Amask, &has_alpha);
    if (fmt == NULL)
        return NULL;

    s = (SDL_Surface*)calloc(1, sizeof(SDL_Surface));
    if (s == NULL) { free(fmt); return NULL; }

    s->w = width;
    s->h = height;
    s->pitch = width * ((depth + 7) / 8);
    s->format = fmt;
    s->flags = (Uint32)has_alpha;
    s->refcount = 1;
    s->clip_rect.x = 0; s->clip_rect.y = 0;
    s->clip_rect.w = width; s->clip_rect.h = height;
    s->pixels = calloc((size_t)s->pitch, (size_t)height);
    if (s->pixels == NULL) { free(fmt); free(s); return NULL; }
    return s;
}
SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height,
                                            int depth, Uint32 format)
{
    (void)flags; (void)format;
    return SDL_CreateRGBSurface(0, width, height, depth, 0, 0, 0, 0);
}

SDL_Surface* SDL_CreateRGBSurfaceFrom(void* pixels, int width, int height, int depth,
                                      int pitch, Uint32 Rmask, Uint32 Gmask,
                                      Uint32 Bmask, Uint32 Amask)
{
    SDL_Surface* s;
    SDL_PixelFormat* fmt;
    bool has_alpha;

    fmt = alloc_format(depth, Rmask, Gmask, Bmask, Amask, &has_alpha);
    if (fmt == NULL)
        return NULL;
    (void)has_alpha;

    s = (SDL_Surface*)calloc(1, sizeof(SDL_Surface));
    if (s == NULL) { free(fmt); return NULL; }

    s->flags = SDL_SURFACE_EXTERNAL_PIXELS;
    s->format = fmt;
    s->w = width; s->h = height; s->pitch = pitch;
    s->pixels = pixels;
    s->refcount = 1;
    s->clip_rect.x = 0; s->clip_rect.y = 0;
    s->clip_rect.w = width; s->clip_rect.h = height;
    return s;
}

void SDL_FreeSurface(SDL_Surface* s)
{
    if (s == NULL)
        return;
    if (--s->refcount > 0)
        return;
    /* Surfaces created From an external buffer must not free it. */
    if ((s->flags & SDL_SURFACE_EXTERNAL_PIXELS) == 0)
        free(s->pixels);
    free(s->format);
    free(s);
}

int SDL_SetColorKey(SDL_Surface* s, int flag, Uint32 key)
{
    if (s == NULL)
        return -1;
    if (flag) {
        s->has_colorkey = 1;
        s->colorkey = key;
    } else {
        s->has_colorkey = 0;
        s->colorkey = 0;
    }
    return 0;
}

int SDL_FillRect(SDL_Surface* dst, const SDL_Rect* rect, Uint32 color)
{
    int x0, y0, w, h, y, bpp;
    Uint32 mapped;

    if (dst == NULL || dst->pixels == NULL)
        return -1;

    if (rect) { x0 = rect->x; y0 = rect->y; w = rect->w; h = rect->h; }
    else      { x0 = 0; y0 = 0; w = dst->w; h = dst->h; }

    /* Clamp to the surface. */
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > dst->w) w = dst->w - x0;
    if (y0 + h > dst->h) h = dst->h - y0;
    if (w <= 0 || h <= 0)
        return 0;

    mapped = SDL_MapRGBA(dst->format, (Uint8)((color >> dst->format->Rshift) & 0xFF),
                                      (Uint8)((color >> dst->format->Gshift) & 0xFF),
                                      (Uint8)((color >> dst->format->Bshift) & 0xFF),
                                      (Uint8)(dst->format->Amask
                                              ? ((color >> dst->format->Ashift) & 0xFF) : 0xFF));

    bpp = dst->format->BytesPerPixel;
    for (y = 0; y < h; y++) {
        Uint8* row = (Uint8*)dst->pixels + (size_t)(y0 + y) * dst->pitch + (size_t)x0 * bpp;
        int x;
        if (bpp == 4) { Uint32* p = (Uint32*)row; for (x = 0; x < w; x++) p[x] = mapped; }
        else if (bpp == 2) { Uint16* p = (Uint16*)row; uint16_t v = (uint16_t)mapped;
                             for (x = 0; x < w; x++) p[x] = v; }
        else if (bpp == 1) { for (x = 0; x < w; x++) row[x] = (Uint8)mapped; }
        else { for (x = 0; x < w; x++) memcpy(row + (size_t)x * bpp, &mapped, (size_t)bpp); }
    }
    return 0;
}

int SDL_BlitSurface(SDL_Surface* src, const SDL_Rect* srcrect,
                    SDL_Surface* dst, SDL_Rect* dstrect)
{
    /* Only used by code paths we do not exercise (screenshot/debug). */
    (void)src; (void)srcrect; (void)dst; (void)dstrect;
    return -1;
}

SDL_Surface* SDL_ConvertSurfaceFormat(SDL_Surface* src, Uint32 pixel_format, Uint32 flags)
{
    (void)pixel_format; (void)flags;
    return src;
}

Uint32 SDL_MapRGB(const SDL_PixelFormat* f, Uint8 r, Uint8 g, Uint8 b)
{
    Uint32 px = 0;
    px |= ((Uint32)r >> f->Rloss) << f->Rshift;
    px |= ((Uint32)g >> f->Gloss) << f->Gshift;
    px |= ((Uint32)b >> f->Bloss) << f->Bshift;
    if (f->Amask)
        px |= (Uint32)(255u >> f->Aloss) << f->Ashift;
    return px;
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat* f, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    Uint32 px = 0;
    px |= ((Uint32)r >> f->Rloss) << f->Rshift;
    px |= ((Uint32)g >> f->Gloss) << f->Gshift;
    px |= ((Uint32)b >> f->Bloss) << f->Bshift;
    if (f->Amask)
        px |= ((Uint32)a >> f->Aloss) << f->Ashift;
    return px;
}

void SDL_GetRGBA(Uint32 px, const SDL_PixelFormat* f, Uint8* r, Uint8* g, Uint8* b, Uint8* a)
{
    int vr, vg, vb, va, opaque;
    unpack_rgba(f, px, &vr, &vg, &vb, &va, &opaque);
    if (r) *r = (Uint8)vr;
    if (g) *g = (Uint8)vg;
    if (b) *b = (Uint8)vb;
    if (a) *a = (Uint8)va;
}

/* --------------------------------------------------------------- textures */

SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer* r, SDL_Surface* surface)
{
    SDL_Texture* t;
    int w, h, bpp, x, y;
    bool use_colorkey;
    Uint32 colorkey;
    bool any_transparent = false;

    (void)r;
    if (surface == NULL)
        return NULL;

    w = surface->w;
    h = surface->h;
    bpp = surface->format->BytesPerPixel;
    use_colorkey = (surface->has_colorkey != 0);
    colorkey = surface->colorkey;

    t = (SDL_Texture*)calloc(1, sizeof(SDL_Texture));
    if (t == NULL)
        return NULL;

    t->w = w;
    t->h = h;
    t->access = 0;
    t->alpha_mod = 255;
    t->r_mod = 255; t->g_mod = 255; t->b_mod = 255;
    t->blendMode = SDL_BLENDMODE_BLEND;

    t->pixels565 = (uint16_t*)malloc((size_t)w * h * sizeof(uint16_t));
    if (t->pixels565 == NULL) { free(t); return NULL; }

    /* First pass: convert to RGB565 and note whether anything is transparent.
     * Textures that turn out fully opaque skip the bitmap entirely, which is
     * the common case for HUD digits, generated rectangles and font glyphs. */
    for (y = 0; y < h; y++) {
        const Uint8* srcrow = (const Uint8*)surface->pixels + (size_t)y * surface->pitch;
        uint16_t* dstrow = t->pixels565 + (size_t)y * w;

        for (x = 0; x < w; x++) {
            Uint32 px;
            int rr, gg, bb, aa, opaque;

            if (bpp == 4)      px = ((const Uint32*)srcrow)[x];
            else if (bpp == 2) px = ((const Uint16*)srcrow)[x];
            else               px = srcrow[x];

            unpack_rgba(surface->format, px, &rr, &gg, &bb, &aa, &opaque);
            if (use_colorkey && px == colorkey)
                opaque = 0;
            if (!opaque)
                any_transparent = true;
            dstrow[x] = pack_rgb565((Uint8)rr, (Uint8)gg, (Uint8)bb);
        }
    }

    if (any_transparent) {
        t->mask_stride_words = (w + 31) / 32;
        t->alphamask = (uint32_t*)calloc((size_t)t->mask_stride_words * h, sizeof(uint32_t));
        if (t->alphamask == NULL) { free(t->pixels565); free(t); return NULL; }

        for (y = 0; y < h; y++) {
            const Uint8* srcrow = (const Uint8*)surface->pixels + (size_t)y * surface->pitch;
            uint32_t* mrow = t->alphamask + (size_t)y * t->mask_stride_words;

            for (x = 0; x < w; x++) {
                Uint32 px;
                int rr, gg, bb, aa, opaque;
                if (bpp == 4)      px = ((const Uint32*)srcrow)[x];
                else if (bpp == 2) px = ((const Uint16*)srcrow)[x];
                else               px = srcrow[x];
                unpack_rgba(surface->format, px, &rr, &gg, &bb, &aa, &opaque);
                if (use_colorkey && px == colorkey)
                    opaque = 0;
                if (opaque)
                    mrow[x >> 5] |= (1u << (x & 31));
            }
        }
        t->has_alpha = true;
    }

    t->format = any_transparent ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_RGB565;
    s_tex_bytes += tex_footprint(t);
    return t;
}

SDL_Texture* SDL_CreateTextureFromPid(const void* vpid)
{
    const WapPid* pid = (const WapPid*)vpid;
    if (pid == NULL || pid->colors == NULL)
        return NULL;

    int w = (int)pid->width;
    int h = (int)pid->height;
    if (w <= 0 || h <= 0)
        return NULL;

    SDL_Texture* t = (SDL_Texture*)calloc(1, sizeof(SDL_Texture));
    if (t == NULL)
        return NULL;

    t->w = w;
    t->h = h;
    t->access = 0;
    t->alpha_mod = 255;
    t->r_mod = 255; t->g_mod = 255; t->b_mod = 255;
    t->blendMode = SDL_BLENDMODE_BLEND;

    size_t num_pixels = (size_t)w * (size_t)h;
    t->pixels565 = (uint16_t*)malloc(num_pixels * sizeof(uint16_t));
    if (t->pixels565 == NULL) {
        free(t);
        return NULL;
    }

    /* Convert to RGB565 and check for transparent pixels (a < 128) */
    bool any_transparent = false;
    const WAP_ColorRGBA* src = pid->colors;
    for (size_t i = 0; i < num_pixels; i++) {
        uint8_t a = src[i].a;
        if (a < 128)
            any_transparent = true;
        t->pixels565[i] = pack_rgb565(src[i].r, src[i].g, src[i].b);
    }

    if (any_transparent) {
        t->mask_stride_words = (w + 31) / 32;
        t->alphamask = (uint32_t*)calloc((size_t)t->mask_stride_words * (size_t)h, sizeof(uint32_t));
        if (t->alphamask == NULL) {
            free(t->pixels565);
            free(t);
            return NULL;
        }

        for (int y = 0; y < h; y++) {
            uint32_t* mrow = t->alphamask + (size_t)y * t->mask_stride_words;
            const WAP_ColorRGBA* srow = pid->colors + (size_t)y * w;
            for (int x = 0; x < w; x++) {
                if (srow[x].a >= 128) {
                    mrow[x >> 5] |= (1u << (x & 31));
                }
            }
        }
        t->has_alpha = true;
    }

    t->format = any_transparent ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_RGB565;
    s_tex_bytes += tex_footprint(t);
    return t;
}

SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, Uint32 format, int access, int w, int h)
{
    SDL_Texture* t = (SDL_Texture*)calloc(1, sizeof(SDL_Texture));
    (void)renderer; (void)access;
    if (t == NULL)
        return NULL;
    if (w <= 0 || h <= 0) { free(t); return NULL; }

    t->w = w; t->h = h;
    t->format = format;
    t->pixels565 = (uint16_t*)calloc((size_t)w * h, sizeof(uint16_t));
    s_tex_bytes += (size_t)w * h * sizeof(uint16_t);
    t->alpha_mod = 255;
    t->r_mod = 255; t->g_mod = 255; t->b_mod = 255;
    t->blendMode = SDL_BLENDMODE_BLEND;
    return t;
}

void SDL_DestroyTexture(SDL_Texture* t)
{
    if (t == NULL)
        return;
    s_tex_bytes -= tex_footprint(t);
    free(t->pixels565);
    free(t->alphamask);
    free(t);
}

int SDL_QueryTexture(SDL_Texture* t, Uint32* format, int* access, int* w, int* h)
{
    if (t == NULL) return -1;
    if (format) *format = t->format;
    if (access) *access = t->access;
    if (w) *w = t->w;
    if (h) *h = t->h;
    return 0;
}

int SDL_SetTextureBlendMode(SDL_Texture* t, SDL_BlendMode m)
{
    if (t == NULL) return -1;
    t->blendMode = m;
    return 0;
}

int SDL_SetTextureAlphaMod(SDL_Texture* t, Uint8 a)
{
    if (t == NULL) return -1;
    t->alpha_mod = a;
    return 0;
}

int SDL_SetTextureColorMod(SDL_Texture* t, Uint8 r, Uint8 g, Uint8 b)
{
    if (t == NULL) return -1;
    t->r_mod = r; t->g_mod = g; t->b_mod = b;
    return 0;
}

int SDL_UpdateTexture(SDL_Texture* t, const SDL_Rect* rect, const void* pixels, int pitch)
{
    int y, y0, y1;
    if (t == NULL || pixels == NULL) return -1;
    y0 = rect ? rect->y : 0;
    y1 = rect ? rect->y + rect->h : t->h;
    for (y = y0; y < y1; y++) {
        const Uint16* src = (const Uint16*)((const Uint8*)pixels + (size_t)(y - y0) * pitch);
        memcpy(t->pixels565 + (size_t)y * t->w, src, (size_t)t->w * sizeof(uint16_t));
    }
    return 0;
}

int SDL_LockTexture(SDL_Texture* t, const SDL_Rect* rect, void** pixels, int* pitch)
{
    (void)rect;
    if (t == NULL) return -1;
    if (pixels) *pixels = t->pixels565;
    if (pitch)  *pitch  = t->w * (int)sizeof(uint16_t);
    return 0;
}

void SDL_UnlockTexture(SDL_Texture* t)
{
    (void)t;
}

/* ---------------------------------------------------------------- windows */

SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags)
{
    SDL_Window* win = (SDL_Window*)calloc(1, sizeof(SDL_Window));
    int fbw, fbh;
    (void)title; (void)x; (void)y;
    if (win == NULL)
        return NULL;
    win->w = (w > 0) ? w : 320;
    win->h = (h > 0) ? h : 240;
    win->flags = flags;
    framebuffer(&fbw, &fbh);
    update_output_scale(win->w, win->h);
    return win;
}

void SDL_DestroyWindow(SDL_Window* w)
{
    free(w);
}

void SDL_GetWindowSize(SDL_Window* w, int* ww, int* hh)
{
    int fbw, fbh;
    framebuffer(&fbw, &fbh);
    if (ww) *ww = w ? w->w : fbw;
    if (hh) *hh = w ? w->h : fbh;
}

Uint32 SDL_GetWindowFlags(SDL_Window* w)
{
    return w ? w->flags : 0;
}

void SDL_SetWindowSize(SDL_Window* w, int ww, int hh)
{
    if (w == NULL) return;
    if (ww > 0) w->w = ww;
    if (hh > 0) w->h = hh;
    update_output_scale(w->w, w->h);
}

int SDL_SetWindowFullscreen(SDL_Window* w, Uint32 flags)
{
    int fbw, fbh;
    framebuffer(&fbw, &fbh);
    if (w == NULL) return -1;
    if (flags & SDL_WINDOW_FULLSCREEN) {
        w->flags |= SDL_WINDOW_FULLSCREEN;
        w->w = fbw; w->h = fbh;
    } else {
        w->flags &= ~(Uint32)SDL_WINDOW_FULLSCREEN;
    }
    return 0;
}

/* --------------------------------------------------------------- renderer */

SDL_Renderer* SDL_CreateRenderer(SDL_Window* w, int index, Uint32 flags)
{
    SDL_Renderer* r;
    int fbw, fbh;

    (void)index; (void)flags;
    if (w == NULL)
        return NULL;

    r = (SDL_Renderer*)calloc(1, sizeof(SDL_Renderer));
    if (r == NULL)
        return NULL;

    framebuffer(&fbw, &fbh);
    r->window   = w;
    r->width    = w->w;
    r->height   = w->h;
    r->scaleX   = 1.0f;
    r->scaleY   = 1.0f;
    r->framebuffer = framebuffer(NULL, NULL);
    r->drawColor.r = 0; r->drawColor.g = 0; r->drawColor.b = 0; r->drawColor.a = 255;
    r->drawColor565 = 0;
    return r;
}

void SDL_DestroyRenderer(SDL_Renderer* r)
{
    free(r);
}

int SDL_RenderSetScale(SDL_Renderer* r, float sx, float sy)
{
    if (r == NULL) return -1;
    r->scaleX = sx;
    r->scaleY = sy;
    return 0;
}

void SDL_RenderGetScale(SDL_Renderer* r, float* sx, float* sy)
{
    if (sx) *sx = r ? r->scaleX : 1.0f;
    if (sy) *sy = r ? r->scaleY : 1.0f;
}

int SDL_SetRenderDrawColor(SDL_Renderer* r, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    if (r == NULL) return -1;
    r->drawColor.r = cr; r->drawColor.g = cg; r->drawColor.b = cb; r->drawColor.a = ca;
    r->drawColor565 = pack_rgb565(cr, cg, cb);
    return 0;
}

int SDL_GetRenderDrawColor(SDL_Renderer* r, Uint8* cr, Uint8* cg, Uint8* cb, Uint8* ca)
{
    if (r == NULL) return -1;
    if (cr) *cr = r->drawColor.r;
    if (cg) *cg = r->drawColor.g;
    if (cb) *cb = r->drawColor.b;
    if (ca) *ca = r->drawColor.a;
    return 0;
}

int SDL_RenderClear(SDL_Renderer* r)
{
    int fbw, fbh;
    uint16_t* fb;
    size_t n;

    if (r == NULL) return -1;
    fb = framebuffer(&fbw, &fbh);
    n = (size_t)fbw * fbh;
    if (r->drawColor565 == 0) {
        memset(fb, 0, n * sizeof(uint16_t));
    } else {
        uint32_t c32 = ((uint32_t)r->drawColor565 << 16) | (uint32_t)r->drawColor565;
        uint32_t* fb32 = (uint32_t*)fb;
        size_t n32 = n / 2;
        for (size_t i = 0; i < n32; i++)
            fb32[i] = c32;
    }
    return 0;
}

void SDL_RenderPresent(SDL_Renderer* r)
{
    /* The host picks the buffer up after retro_run() returns. */
    (void)r;
}

/* ----------------------------------------------------------------- blitter */

typedef struct {
    const uint16_t* src;        /* texture texel (0,0) */
    int src_stride;             /* texels per texture row */
    const uint32_t* mask;       /* transparency bitmap, NULL when opaque */
    int mask_stride;            /* 32-bit words per texture row */
    uint16_t* dst;              /* first destination pixel */
    int dst_stride;             /* pixels per framebuffer row */
    int dw, dh;                 /* destination size, post-clipping */
    int sx0, sy0;               /* source origin in texels */
    int sw, sh;                 /* source span in texels */
    bool flip_x;
    bool opaque;
    bool modulate;
    /* Exact integer source step per destination pixel/row, or 0 when the
     * mapping needs the general 16.16 path. */
    int step_x, step_y;
    Uint8 r_mod, g_mod, b_mod, alpha_mod;
} blit_t;

static inline uint16_t modulate_pixel(uint16_t p, Uint8 rm, Uint8 gm, Uint8 bm)
{
    Uint32 r  = (((p >> 11) & 0x1Fu) * rm) >> 8;
    Uint32 g  = (((p >> 5)  & 0x3Fu) * gm) >> 8;
    Uint32 bl = (( p        & 0x1Fu) * bm) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* Alpha blend a straight (non-premultiplied) RGB565 source onto the dest.
 * The (x*0x101) / (x*0x101 + 0x80) form keeps 255 mapping to exactly 255. */
static inline uint16_t blend_pixel(uint16_t d, uint16_t s, int a)
{
    int inv = 255 - a;
    int r = (int)((((s >> 11) & 0x1F) * 0x101u * a + ((d >> 11) & 0x1F) * inv * 0x101u) >> 16);
    int g = (int)((((s >> 5)  & 0x3F) * 0x101u * a + ((d >> 5)  & 0x3F) * inv * 0x101u) >> 16);
    int b = (int)((( s        & 0x1F) * 0x101u * a + ( d        & 0x1F) * inv * 0x101u) >> 16);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Exact integer-ratio reduction (the 640x480 -> 320x240 case that every sprite
 * and tile on this console goes through). Walking the source with a constant
 * texel step avoids the 16.16 accumulator and, when nothing needs masking or
 * modulating, reduces to a single load and store per destination pixel.
 * Point sampling (taking every Nth source texel) is what the 2:1 reduction of
 * pixel art wants anyway, and being a fixed stride it cannot drift. */
static void blit_reduce(const blit_t* b)
{
    const int step_x = b->step_x;
    const int step_y = b->step_y;
    int dy;

    /* Fast path: unmodulated 2:1 reduction (the 640x480 -> 320x240 console case) */
    if (step_x == 2 && !b->modulate) {
        if (!b->flip_x) {
            if (!b->mask) {
                for (dy = 0; dy < b->dh; dy++) {
                    const uint16_t* srow = b->src + (size_t)(b->sy0 + dy * step_y) * b->src_stride;
                    uint16_t* drow = b->dst + (size_t)dy * b->dst_stride;
                    int sx = b->sx0;
                    int dx = 0;
                    while (dx + 8 <= b->dw) {
                        drow[dx + 0] = srow[sx + 0];
                        drow[dx + 1] = srow[sx + 2];
                        drow[dx + 2] = srow[sx + 4];
                        drow[dx + 3] = srow[sx + 6];
                        drow[dx + 4] = srow[sx + 8];
                        drow[dx + 5] = srow[sx + 10];
                        drow[dx + 6] = srow[sx + 12];
                        drow[dx + 7] = srow[sx + 14];
                        dx += 8;
                        sx += 16;
                    }
                    while (dx < b->dw) {
                        drow[dx] = srow[sx];
                        dx++;
                        sx += 2;
                    }
                }
                return;
            }

            /* Masked 2:1 reduction: fast 16-pixel chunking with arbitrary sx alignment */
            for (dy = 0; dy < b->dh; dy++) {
                const uint16_t* srow = b->src + (size_t)(b->sy0 + dy * step_y) * b->src_stride;
                const uint32_t* mrow = b->mask + (size_t)(b->sy0 + dy * step_y) * b->mask_stride;
                uint16_t* drow = b->dst + (size_t)dy * b->dst_stride;
                int sx = b->sx0;
                int dx = 0;

                while (dx + 16 <= b->dw) {
                    int w_idx = sx >> 5;
                    int shift = sx & 31;
                    uint32_t w0 = mrow[w_idx];
                    uint32_t bits = (shift == 0) ? w0 : ((w0 >> shift) | (mrow[w_idx + 1] << (32 - shift)));
                    uint32_t even_mask = bits & 0x55555555u;
                    if (even_mask == 0) {
                        dx += 16;
                        sx += 32;
                        continue;
                    }
                    if (even_mask == 0x55555555u) {
                        drow[dx + 0]  = srow[sx + 0];
                        drow[dx + 1]  = srow[sx + 2];
                        drow[dx + 2]  = srow[sx + 4];
                        drow[dx + 3]  = srow[sx + 6];
                        drow[dx + 4]  = srow[sx + 8];
                        drow[dx + 5]  = srow[sx + 10];
                        drow[dx + 6]  = srow[sx + 12];
                        drow[dx + 7]  = srow[sx + 14];
                        drow[dx + 8]  = srow[sx + 16];
                        drow[dx + 9]  = srow[sx + 18];
                        drow[dx + 10] = srow[sx + 20];
                        drow[dx + 11] = srow[sx + 22];
                        drow[dx + 12] = srow[sx + 24];
                        drow[dx + 13] = srow[sx + 26];
                        drow[dx + 14] = srow[sx + 28];
                        drow[dx + 15] = srow[sx + 30];
                        dx += 16;
                        sx += 32;
                        continue;
                    }
                    if (even_mask & (1u << 0))  drow[dx + 0]  = srow[sx + 0];
                    if (even_mask & (1u << 2))  drow[dx + 1]  = srow[sx + 2];
                    if (even_mask & (1u << 4))  drow[dx + 2]  = srow[sx + 4];
                    if (even_mask & (1u << 6))  drow[dx + 3]  = srow[sx + 6];
                    if (even_mask & (1u << 8))  drow[dx + 4]  = srow[sx + 8];
                    if (even_mask & (1u << 10)) drow[dx + 5]  = srow[sx + 10];
                    if (even_mask & (1u << 12)) drow[dx + 6]  = srow[sx + 12];
                    if (even_mask & (1u << 14)) drow[dx + 7]  = srow[sx + 14];
                    if (even_mask & (1u << 16)) drow[dx + 8]  = srow[sx + 16];
                    if (even_mask & (1u << 18)) drow[dx + 9]  = srow[sx + 18];
                    if (even_mask & (1u << 20)) drow[dx + 10] = srow[sx + 20];
                    if (even_mask & (1u << 22)) drow[dx + 11] = srow[sx + 22];
                    if (even_mask & (1u << 24)) drow[dx + 12] = srow[sx + 24];
                    if (even_mask & (1u << 26)) drow[dx + 13] = srow[sx + 26];
                    if (even_mask & (1u << 28)) drow[dx + 14] = srow[sx + 28];
                    if (even_mask & (1u << 30)) drow[dx + 15] = srow[sx + 30];
                    dx += 16;
                    sx += 32;
                }
                while (dx < b->dw) {
                    if (mrow[sx >> 5] & (1u << (sx & 31)))
                        drow[dx] = srow[sx];
                    dx++;
                    sx += 2;
                }
            }
            return;
        } else {
            /* flip_x 2:1 reduction */
            if (!b->mask) {
                for (dy = 0; dy < b->dh; dy++) {
                    const uint16_t* srow = b->src + (size_t)(b->sy0 + dy * step_y) * b->src_stride;
                    uint16_t* drow = b->dst + (size_t)dy * b->dst_stride;
                    int sx = b->sx0 + b->sw - 2;
                    int dx = 0;
                    while (dx + 8 <= b->dw) {
                        drow[dx + 0] = srow[sx - 0];
                        drow[dx + 1] = srow[sx - 2];
                        drow[dx + 2] = srow[sx - 4];
                        drow[dx + 3] = srow[sx - 6];
                        drow[dx + 4] = srow[sx - 8];
                        drow[dx + 5] = srow[sx - 10];
                        drow[dx + 6] = srow[sx - 12];
                        drow[dx + 7] = srow[sx - 14];
                        dx += 8;
                        sx -= 16;
                    }
                    while (dx < b->dw) {
                        drow[dx] = srow[sx];
                        dx++;
                        sx -= 2;
                    }
                }
                return;
            }

            /* Masked flip_x 2:1 reduction: fast 16-pixel chunking */
            for (dy = 0; dy < b->dh; dy++) {
                const uint16_t* srow = b->src + (size_t)(b->sy0 + dy * step_y) * b->src_stride;
                const uint32_t* mrow = b->mask + (size_t)(b->sy0 + dy * step_y) * b->mask_stride;
                uint16_t* drow = b->dst + (size_t)dy * b->dst_stride;
                int sx = b->sx0 + b->sw - 2;
                int dx = 0;

                while (dx + 16 <= b->dw) {
                    int s_start = sx - 30;
                    int w_idx = s_start >> 5;
                    int shift = s_start & 31;
                    uint32_t w0 = mrow[w_idx];
                    uint32_t bits = (shift == 0) ? w0 : ((w0 >> shift) | (mrow[w_idx + 1] << (32 - shift)));
                    uint32_t even_mask = bits & 0x55555555u;
                    if (even_mask == 0) {
                        dx += 16;
                        sx -= 32;
                        continue;
                    }
                    if (even_mask == 0x55555555u) {
                        drow[dx + 0]  = srow[sx - 0];
                        drow[dx + 1]  = srow[sx - 2];
                        drow[dx + 2]  = srow[sx - 4];
                        drow[dx + 3]  = srow[sx - 6];
                        drow[dx + 4]  = srow[sx - 8];
                        drow[dx + 5]  = srow[sx - 10];
                        drow[dx + 6]  = srow[sx - 12];
                        drow[dx + 7]  = srow[sx - 14];
                        drow[dx + 8]  = srow[sx - 16];
                        drow[dx + 9]  = srow[sx - 18];
                        drow[dx + 10] = srow[sx - 20];
                        drow[dx + 11] = srow[sx - 22];
                        drow[dx + 12] = srow[sx - 24];
                        drow[dx + 13] = srow[sx - 26];
                        drow[dx + 14] = srow[sx - 28];
                        drow[dx + 15] = srow[sx - 30];
                        dx += 16;
                        sx -= 32;
                        continue;
                    }
                    if (even_mask & (1u << 30)) drow[dx + 0]  = srow[sx - 0];
                    if (even_mask & (1u << 28)) drow[dx + 1]  = srow[sx - 2];
                    if (even_mask & (1u << 26)) drow[dx + 2]  = srow[sx - 4];
                    if (even_mask & (1u << 24)) drow[dx + 3]  = srow[sx - 6];
                    if (even_mask & (1u << 22)) drow[dx + 4]  = srow[sx - 8];
                    if (even_mask & (1u << 20)) drow[dx + 5]  = srow[sx - 10];
                    if (even_mask & (1u << 18)) drow[dx + 6]  = srow[sx - 12];
                    if (even_mask & (1u << 16)) drow[dx + 7]  = srow[sx - 14];
                    if (even_mask & (1u << 14)) drow[dx + 8]  = srow[sx - 16];
                    if (even_mask & (1u << 12)) drow[dx + 9]  = srow[sx - 18];
                    if (even_mask & (1u << 10)) drow[dx + 10] = srow[sx - 20];
                    if (even_mask & (1u << 8))  drow[dx + 11] = srow[sx - 22];
                    if (even_mask & (1u << 6))  drow[dx + 12] = srow[sx - 24];
                    if (even_mask & (1u << 4))  drow[dx + 13] = srow[sx - 26];
                    if (even_mask & (1u << 2))  drow[dx + 14] = srow[sx - 28];
                    if (even_mask & (1u << 0))  drow[dx + 15] = srow[sx - 30];
                    dx += 16;
                    sx -= 32;
                }
                while (dx < b->dw) {
                    if (mrow[sx >> 5] & (1u << (sx & 31)))
                        drow[dx] = srow[sx];
                    dx++;
                    sx -= 2;
                }
            }
            return;
        }
    }

    for (dy = 0; dy < b->dh; dy++) {
        const uint16_t* srow = b->src + (size_t)(b->sy0 + dy * step_y) * b->src_stride;
        const uint32_t* mrow = b->mask ? (b->mask + (size_t)(b->sy0 + dy * step_y) * b->mask_stride) : NULL;
        uint16_t* drow = b->dst + (size_t)dy * b->dst_stride;
        int sx = b->sx0;
        int dx;

        if (b->flip_x) {
            sx = b->sx0 + b->sw - step_x;
            if (!b->modulate) {
                if (!mrow) {
                    for (dx = 0; dx < b->dw; dx++, sx -= step_x)
                        drow[dx] = srow[sx];
                } else {
                    for (dx = 0; dx < b->dw; dx++, sx -= step_x) {
                        if (mrow[sx >> 5] & (1u << (sx & 31)))
                            drow[dx] = srow[sx];
                    }
                }
            } else {
                for (dx = 0; dx < b->dw; dx++, sx -= step_x) {
                    if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                        continue;
                    drow[dx] = modulate_pixel(srow[sx], b->r_mod, b->g_mod, b->b_mod);
                }
            }
            continue;
        }

        if (!mrow && !b->modulate) {
            for (dx = 0; dx < b->dw; dx++, sx += step_x)
                drow[dx] = srow[sx];
            continue;
        }

        if (!b->modulate) {
            for (dx = 0; dx < b->dw; dx++, sx += step_x) {
                if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                    continue;
                drow[dx] = srow[sx];
            }
            continue;
        }

        if (b->modulate && b->alpha_mod == 255) {
            for (dx = 0; dx < b->dw; dx++, sx += step_x) {
                if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                    continue;
                drow[dx] = modulate_pixel(srow[sx], b->r_mod, b->g_mod, b->b_mod);
            }
            continue;
        }

        for (dx = 0; dx < b->dw; dx++, sx += step_x) {
            if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                continue;
            drow[dx] = blend_pixel(drow[dx], modulate_pixel(srow[sx], b->r_mod, b->g_mod, b->b_mod), b->alpha_mod);
        }
    }
}

static void blit_1to1(const blit_t* b)
{
    int dy;

    if (b->opaque && !b->modulate && !b->flip_x) {
        for (dy = 0; dy < b->dh; dy++) {
            const uint16_t* srow = b->src + (size_t)(b->sy0 + dy) * b->src_stride + b->sx0;
            memcpy(b->dst + (size_t)dy * b->dst_stride, srow, (size_t)b->dw * sizeof(uint16_t));
        }
        return;
    }

    for (dy = 0; dy < b->dh; dy++) {
        const uint16_t* srow = b->src + (size_t)(b->sy0 + dy) * b->src_stride;
        const uint32_t* mrow = b->mask ? (b->mask + (size_t)(b->sy0 + dy) * b->mask_stride) : NULL;
        uint16_t* drow = b->dst + (size_t)dy * b->dst_stride;
        int dx;

        if (b->flip_x) {
            int base = b->sx0 + b->sw;
            for (dx = 0; dx < b->dw; dx++) {
                int sx = base - 1 - dx;
                uint16_t sp = srow[sx];
                if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                    continue;
                drow[dx] = b->modulate ? modulate_pixel(sp, b->r_mod, b->g_mod, b->b_mod) : sp;
            }
        } else if (b->modulate && b->alpha_mod == 255) {
            for (dx = 0; dx < b->dw; dx++) {
                int sx = b->sx0 + dx;
                if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                    continue;
                drow[dx] = modulate_pixel(srow[sx], b->r_mod, b->g_mod, b->b_mod);
            }
        } else if (b->modulate) {
            for (dx = 0; dx < b->dw; dx++) {
                int sx = b->sx0 + dx;
                if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                    continue;
                drow[dx] = blend_pixel(drow[dx], modulate_pixel(srow[sx], b->r_mod, b->g_mod, b->b_mod),
                                       b->alpha_mod);
            }
        } else if (mrow) {
            int sx = b->sx0;
            int dx = 0;
            while (dx < b->dw && (sx & 31) != 0) {
                if (mrow[sx >> 5] & (1u << (sx & 31)))
                    drow[dx] = srow[sx];
                dx++;
                sx++;
            }
            while (dx + 32 <= b->dw) {
                uint32_t m = mrow[sx >> 5];
                if (m == 0) {
                    dx += 32;
                    sx += 32;
                    continue;
                }
                if (m == 0xFFFFFFFFu) {
                    memcpy(drow + dx, srow + sx, 32 * sizeof(uint16_t));
                    dx += 32;
                    sx += 32;
                    continue;
                }
                for (int i = 0; i < 32; i++) {
                    if (m & (1u << i))
                        drow[dx + i] = srow[sx + i];
                }
                dx += 32;
                sx += 32;
            }
            while (dx < b->dw) {
                if (mrow[sx >> 5] & (1u << (sx & 31)))
                    drow[dx] = srow[sx];
                dx++;
                sx++;
            }
        } else {
            memcpy(drow, srow + b->sx0, (size_t)b->dw * sizeof(uint16_t));
        }
    }
}

static void blit_scaled(const blit_t* b)
{
    int step_x = (int)(((int64_t)b->sw << 16) / b->dw);
    int step_y = (int)(((int64_t)b->sh << 16) / b->dh);
    int fy = 0;
    int dy;

    for (dy = 0; dy < b->dh; dy++) {
        int sy = b->sy0 + (fy >> 16);
        const uint16_t* srow = b->src + (size_t)sy * b->src_stride;
        const uint32_t* mrow = b->mask ? (b->mask + (size_t)sy * b->mask_stride) : NULL;
        uint16_t* drow = b->dst + (size_t)dy * b->dst_stride;
        int fx = 0;
        int dx = 0;

        if (b->flip_x) {
            if (!b->modulate) {
                if (!mrow) {
                    for (dx = 0; dx < b->dw; dx++) {
                        drow[dx] = srow[b->sx0 + b->sw - 1 - (fx >> 16)];
                        fx += step_x;
                    }
                } else {
                    while (dx < b->dw) {
                        int sx = b->sx0 + b->sw - 1 - (fx >> 16);
                        uint32_t mask_word = mrow[sx >> 5];
                        if (mask_word == 0) {
                            int span_idx = sx >> 5;
                            uint32_t fx_target = (uint32_t)(b->sx0 + b->sw - span_idx * 32) << 16;
                            if (fx_target > (uint32_t)fx) {
                                int steps = (int)((fx_target - fx + step_x - 1) / (uint32_t)step_x);
                                if (steps > b->dw - dx)
                                    steps = b->dw - dx;
                                if (steps > 0) {
                                    dx += steps;
                                    fx += steps * step_x;
                                    continue;
                                }
                            }
                            fx += step_x;
                            dx++;
                            continue;
                        }
                        if (mask_word == ~0u) {
                            int span_idx = sx >> 5;
                            uint32_t fx_target = (uint32_t)(b->sx0 + b->sw - span_idx * 32) << 16;
                            if (fx_target > (uint32_t)fx) {
                                int steps = (int)((fx_target - fx + step_x - 1) / (uint32_t)step_x);
                                if (steps > b->dw - dx)
                                    steps = b->dw - dx;
                                while (steps > 0) {
                                    drow[dx++] = srow[b->sx0 + b->sw - 1 - (fx >> 16)];
                                    fx += step_x;
                                    steps--;
                                }
                                continue;
                            }
                        }
                        if (mask_word & (1u << (sx & 31))) {
                            drow[dx] = srow[sx];
                        }
                        fx += step_x;
                        dx++;
                    }
                }
            } else {
                for (dx = 0; dx < b->dw; dx++) {
                    int sx = b->sx0 + b->sw - 1 - (fx >> 16);
                    uint16_t sp = srow[sx];
                    fx += step_x;
                    if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                        continue;
                    drow[dx] = (b->alpha_mod == 255)
                        ? modulate_pixel(sp, b->r_mod, b->g_mod, b->b_mod)
                        : blend_pixel(drow[dx], modulate_pixel(sp, b->r_mod, b->g_mod, b->b_mod), b->alpha_mod);
                }
            }
        } else if (!b->modulate) {
            if (!mrow) {
                for (dx = 0; dx < b->dw; dx++) {
                    drow[dx] = srow[b->sx0 + (fx >> 16)];
                    fx += step_x;
                }
            } else {
                while (dx < b->dw) {
                    int sx = b->sx0 + (fx >> 16);
                    uint32_t mask_word = mrow[sx >> 5];
                    if (mask_word == 0) {
                        int span_idx = sx >> 5;
                        uint32_t fx_target = (uint32_t)((span_idx + 1) * 32 - b->sx0) << 16;
                        if (fx_target > (uint32_t)fx) {
                            int steps = (int)((fx_target - fx + step_x - 1) / (uint32_t)step_x);
                            if (steps > b->dw - dx)
                                steps = b->dw - dx;
                            if (steps > 0) {
                                dx += steps;
                                fx += steps * step_x;
                                continue;
                            }
                        }
                        fx += step_x;
                        dx++;
                        continue;
                    }
                    if (mask_word == ~0u) {
                        int span_idx = sx >> 5;
                        uint32_t fx_target = (uint32_t)((span_idx + 1) * 32 - b->sx0) << 16;
                        if (fx_target > (uint32_t)fx) {
                            int steps = (int)((fx_target - fx + step_x - 1) / (uint32_t)step_x);
                            if (steps > b->dw - dx)
                                steps = b->dw - dx;
                            while (steps > 0) {
                                drow[dx++] = srow[b->sx0 + (fx >> 16)];
                                fx += step_x;
                                steps--;
                            }
                            continue;
                        }
                    }
                    if (mask_word & (1u << (sx & 31))) {
                        drow[dx] = srow[sx];
                    }
                    fx += step_x;
                    dx++;
                }
            }
        } else if (b->alpha_mod == 255) {
            for (dx = 0; dx < b->dw; dx++) {
                int sx = b->sx0 + (fx >> 16);
                fx += step_x;
                if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                    continue;
                drow[dx] = modulate_pixel(srow[sx], b->r_mod, b->g_mod, b->b_mod);
            }
        } else {
            for (dx = 0; dx < b->dw; dx++) {
                int sx = b->sx0 + (fx >> 16);
                fx += step_x;
                if (mrow && !(mrow[sx >> 5] & (1u << (sx & 31))))
                    continue;
                drow[dx] = blend_pixel(drow[dx], modulate_pixel(srow[sx], b->r_mod, b->g_mod, b->b_mod),
                                       b->alpha_mod);
            }
        }
        fy += step_y;
    }
}

static int do_render_copy(SDL_Renderer* r, SDL_Texture* t,
                          const SDL_Rect* srcrect, const SDL_Rect* dstrect,
                          bool flip_x)
{
    blit_t b;
    int fbw, fbh;
    uint16_t* fb;
    int sw, sh, sx0, sy0;
    int dx0, dy0, dw, dh;
    int clip_x1, clip_y1;

    if (r == NULL || t == NULL)
        return -1;

    fb = framebuffer(&fbw, &fbh);

    if (srcrect) {
        sx0 = srcrect->x; sy0 = srcrect->y;
        sw  = srcrect->w; sh  = srcrect->h;
    } else {
        sx0 = 0; sy0 = 0; sw = t->w; sh = t->h;
    }

    if (dstrect) {
        dx0 = dstrect->x; dy0 = dstrect->y;
        /* SDL_RenderSetScale scales the destination rect's extent only. */
        if (r && r->scaleX == 1.0f && r->scaleY == 1.0f) {
            dw = dstrect->w;
            dh = dstrect->h;
        } else if (r) {
            dw  = (int)((float)dstrect->w * r->scaleX + 0.5f);
            dh  = (int)((float)dstrect->h * r->scaleY + 0.5f);
        } else {
            dw = dstrect->w;
            dh = dstrect->h;
        }
    } else {
        /* A NULL destination means "the whole window". */
        dx0 = 0; dy0 = 0;
        dw = (r && r->window) ? r->window->w : fbw;
        dh = (r && r->window) ? r->window->h : fbh;
    }

    /* Project the destination into framebuffer space. Mapping both edges
     * (rather than scaling x/y/w/h independently) keeps a 1:2 reduction free
     * of off-by-one seams between neighbouring sprites. */
    {
        int nx0 = out_x(dx0);
        int ny0 = out_y(dy0);
        int nx1 = out_x(dx0 + dw);
        int ny1 = out_y(dy0 + dh);
        dx0 = nx0; dy0 = ny0;
        dw  = nx1 - nx0;
        dh  = ny1 - ny0;
    }

    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return 0;

    /* Clip the source rectangle against the texture. */
    if (sx0 < 0) { sw += sx0; sx0 = 0; }
    if (sy0 < 0) { sh += sy0; sy0 = 0; }
    if (sx0 + sw > t->w) sw = t->w - sx0;
    if (sy0 + sh > t->h) sh = t->h - sy0;
    if (sw <= 0 || sh <= 0)
        return 0;

    clip_x1 = fbw;
    clip_y1 = fbh;

    /* Clip the destination, trimming the matching source span so the
     * source-to-destination mapping stays exact. */
    if (dx0 < 0) {
        int cut = -dx0;
        if (cut >= dw) return 0;
        dx0 = 0;
        if (sw == dw * 2) {
            sx0 += cut * 2;
            sw  -= cut * 2;
        } else if (sw == dw) {
            sx0 += cut;
            sw  -= cut;
        } else {
            sx0 += (int)((int64_t)cut * sw / dw);
            sw  -= (int)((int64_t)cut * sw / dw);
        }
        dw   -= cut;
    }
    if (dy0 < 0) {
        int cut = -dy0;
        if (cut >= dh) return 0;
        dy0 = 0;
        if (sh == dh * 2) {
            sy0 += cut * 2;
            sh  -= cut * 2;
        } else if (sh == dh) {
            sy0 += cut;
            sh  -= cut;
        } else {
            sy0 += (int)((int64_t)cut * sh / dh);
            sh  -= (int)((int64_t)cut * sh / dh);
        }
        dh   -= cut;
    }
    if (dx0 + dw > clip_x1) {
        dw = clip_x1 - dx0;
        if (dw <= 0) return 0;
    }
    if (dy0 + dh > clip_y1) {
        dh = clip_y1 - dy0;
        if (dh <= 0) return 0;
    }
    if (sw <= 0 || sh <= 0)
        return 0;

    memset(&b, 0, sizeof(b));
    b.src        = t->pixels565;
    b.src_stride = t->w;
    b.mask       = t->alphamask;
    b.mask_stride = t->mask_stride_words;
    b.dst        = fb + (size_t)dy0 * fbw + dx0;
    b.dst_stride = fbw;
    b.dw = dw;  b.dh = dh;
    b.sx0 = sx0; b.sy0 = sy0;
    b.sw = sw;   b.sh = sh;
    b.flip_x = flip_x;
    b.opaque = (t->alphamask == NULL);
    b.r_mod = t->r_mod; b.g_mod = t->g_mod; b.b_mod = t->b_mod;
    b.alpha_mod = t->alpha_mod;
    b.modulate = (t->blendMode != SDL_BLENDMODE_NONE) &&
                 (t->r_mod != 255 || t->g_mod != 255 || t->b_mod != 255 || t->alpha_mod != 255);

    if (sw == dw && sh == dh) {
        b.step_x = 1;
        b.step_y = 1;
        blit_1to1(&b);
    } else if (sw % dw == 0 && sh % dh == 0) {
        b.step_x = sw / dw;
        b.step_y = sh / dh;
        blit_reduce(&b);
    } else {
        b.step_x = 0;
        b.step_y = 0;
        blit_scaled(&b);
    }

    return 0;
}

int SDL_RenderCopy(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* sr, const SDL_Rect* dr)
{
    return do_render_copy(r, t, sr, dr, false);
}

int SDL_RenderCopyEx(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* sr, const SDL_Rect* dr,
                     const double angle, const SDL_Point* center, const SDL_RendererFlip flip)
{
    /* OpenClaw never rotates: both call sites pass angle 0 with a NULL centre
     * and only use the horizontal mirror, for mirrored actors. */
    if (angle != 0.0 || center != NULL)
        return do_render_copy(r, t, sr, dr, false);
    return do_render_copy(r, t, sr, dr, (flip & SDL_FLIP_HORIZONTAL) != 0);
}

/* ------------------------------------------------------------- primitives */

int SDL_RenderFillRect(SDL_Renderer* r, const SDL_Rect* rect)
{
    int fbw, fbh, x0, y0, w, h, y;
    uint16_t* fb;

    if (r == NULL) return -1;
    fb = framebuffer(&fbw, &fbh);

    if (rect) {
        int x1 = out_x(rect->x + rect->w);
        int y1 = out_y(rect->y + rect->h);
        x0 = out_x(rect->x);
        y0 = out_y(rect->y);
        w = x1 - x0;
        h = y1 - y0;
    }
    else      { x0 = 0; y0 = 0; w = fbw; h = fbh; }

    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > fbw) w = fbw - x0;
    if (y0 + h > fbh) h = fbh - y0;
    if (w <= 0 || h <= 0) return 0;

    for (y = 0; y < h; y++) {
        uint16_t* row = fb + (size_t)(y0 + y) * fbw + x0;
        int x;
        for (x = 0; x < w; x++)
            row[x] = r->drawColor565;
    }
    return 0;
}

int SDL_RenderDrawRect(SDL_Renderer* r, const SDL_Rect* rect)
{
    if (rect == NULL) return -1;
    SDL_RenderDrawLine(r, rect->x, rect->y, rect->x + rect->w - 1, rect->y);
    SDL_RenderDrawLine(r, rect->x, rect->y + rect->h - 1, rect->x + rect->w - 1, rect->y + rect->h - 1);
    SDL_RenderDrawLine(r, rect->x, rect->y, rect->x, rect->y + rect->h - 1);
    SDL_RenderDrawLine(r, rect->x + rect->w - 1, rect->y, rect->x + rect->w - 1, rect->y + rect->h - 1);
    return 0;
}

int SDL_RenderDrawLine(SDL_Renderer* r, int x1, int y1, int x2, int y2)
{
    int fbw, fbh, dx, dy, sx, sy, err;
    uint16_t* fb;

    if (r == NULL) return -1;
    fb = framebuffer(&fbw, &fbh);

    /* Map into framebuffer space up front: at a reduced output scale a line
     * collapses to a single pixel often enough that walking it unscaled would
     * leave dotted gaps. */
    x1 = out_x(x1); y1 = out_y(y1);
    x2 = out_x(x2); y2 = out_y(y2);

    dx = (x2 >= x1) ? (x2 - x1) : (x1 - x2);
    dy = (y2 >= y1) ? (y1 - y2) : (y2 - y1);
    sx = (x1 < x2) ? 1 : -1;
    sy = (y1 < y2) ? 1 : -1;
    err = dx + dy;

    for (;;) {
        if (x1 >= 0 && x1 < fbw && y1 >= 0 && y1 < fbh)
            fb[(size_t)y1 * fbw + x1] = r->drawColor565;
        if (x1 == x2 && y1 == y2)
            break;
        {
            int e2 = err * 2;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    }
    return 0;
}

int SDL_RenderDrawPoint(SDL_Renderer* r, int x, int y)
{
    int fbw, fbh;
    uint16_t* fb;

    if (r == NULL) return -1;
    fb = framebuffer(&fbw, &fbh);
    x = out_x(x);
    y = out_y(y);
    if (x < 0 || x >= fbw || y < 0 || y >= fbh)
        return 0;
    fb[(size_t)y * fbw + x] = r->drawColor565;
    return 0;
}

int SDL_RenderReadPixels(SDL_Renderer* r, const SDL_Rect* rect, Uint32 format,
                         void* pixels, int pitch)
{
    int fbw, fbh, x0, y0, w, h, x, y;
    uint16_t* fb;

    if (r == NULL || pixels == NULL) return -1;
    (void)format;
    fb = framebuffer(&fbw, &fbh);

    if (rect) { x0 = rect->x; y0 = rect->y; w = rect->w; h = rect->h; }
    else      { x0 = 0; y0 = 0; w = fbw; h = fbh; }

    for (y = 0; y < h; y++) {
        Uint32* orow = (Uint32*)((Uint8*)pixels + (size_t)y * pitch);
        const uint16_t* irow = fb + (size_t)(y0 + y) * fbw;
        for (x = 0; x < w; x++) {
            uint16_t p = irow[x0 + x];
            orow[x] = ((uint32_t)((p >> 11) & 0x1F) << 19) |
                      ((uint32_t)((p >> 5)  & 0x3F) << 10) |
                      ((uint32_t)( p        & 0x1F) << 3)  | 0xFFu;
        }
    }
    return 0;
}
