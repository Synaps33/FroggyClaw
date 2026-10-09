/*
 * sdl_compat - image loading: PCX by hand, PNG/TGA through stb_image.
 *
 * OpenClaw ships three formats we care about: PCX for the console background
 * and a couple of menu art pieces, PNG for the credits/touchscreen art in
 * ASSETS.ZIP, and TGA for the console backdrop. Decoders return 32-bit RGBA
 * surfaces; the video layer converts those to RGB565 once.
 */
#include "sdl_compat.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_NO_HDR
#define STBI_NO_JPEG
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

static char s_img_error[256] = {0};

const char* IMG_GetError(void)
{
    return s_img_error;
}

static void img_set_error(const char* msg)
{
    strncpy(s_img_error, msg, sizeof(s_img_error) - 1);
    s_img_error[sizeof(s_img_error) - 1] = '\0';
}

/* Read an entire SDL_RWops into a heap buffer. Caller frees. */
static Uint8* slurp(SDL_RWops* src, size_t* out_len)
{
    Sint64 total;
    Uint8* buf;
    size_t got;

    if (src == NULL || out_len == NULL)
        return NULL;

    total = src->size(src);
    if (total <= 0 || total > (Sint64)64 * 1024 * 1024) {
        img_set_error("IMG: unsupported or empty image buffer");
        return NULL;
    }

    buf = (Uint8*)malloc((size_t)total + 1);
    if (buf == NULL) {
        img_set_error("IMG: out of memory");
        return NULL;
    }

    got = src->read(src, buf, 1, (size_t)total);
    if (got != (size_t)total) {
        free(buf);
        img_set_error("IMG: short read");
        return NULL;
    }

    *out_len = got;
    return buf;
}

/* Allocate a 32-bit RGBA surface (r,g,b,a byte order in memory). */
static SDL_Surface* new_rgba_surface(int w, int h)
{
    return SDL_CreateRGBSurface(0, w, h, 32,
                                0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
}

/* -------------------------------------------------------------------- PCX */

static Uint16 pcx_u16(const Uint8* p)
{
    return (Uint16)((Uint32)p[0] | ((Uint32)p[1] << 8));
}

/* PCX as used by Captain Claw: 8-bit paletted, one plane, RLE per scanline. */
static SDL_Surface* load_pcx(const Uint8* buf, size_t len)
{
    SDL_Surface* surf;
    int width, height, bpp, planes, bytes_per_line;
    size_t pos = 128;
    int x = 0, y = 0;
    const Uint8* palette = NULL;

    if (len < 128 || buf[0] != 0x0A) {
        img_set_error("IMG_LoadPCX_RW: not a PCX file");
        return NULL;
    }

    bpp   = buf[3];
    width = pcx_u16(buf + 8) - pcx_u16(buf + 4) + 1;
    height = pcx_u16(buf + 10) - pcx_u16(buf + 6) + 1;
    planes = buf[65];
    bytes_per_line = pcx_u16(buf + 66);

    if (bpp != 8 || planes != 1 || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096) {
        img_set_error("IMG_LoadPCX_RW: unsupported PCX variant");
        return NULL;
    }

    surf = new_rgba_surface(width, height);
    if (surf == NULL)
        return NULL;

    /* RLE decode one byte per pixel. The 256-colour palette sits in the last
     * 769 bytes, introduced by a 0x0C marker. */
    for (y = 0; y < height; y++) {
        Uint8* row = (Uint8*)surf->pixels + (size_t)y * surf->pitch;
        int written = 0;

        while (written < bytes_per_line) {
            Uint8 v;

            if (pos >= len)
                goto fail;
            v = buf[pos++];

            if (buf[2] == 1) { /* RLE encoded */
                int run = 1;
                if ((v & 0xC0) == 0xC0) {
                    run = v & 0x3F;
                    if (pos >= len)
                        goto fail;
                    v = buf[pos++];
                }
                while (run-- > 0 && written < bytes_per_line) {
                    if (x < width)
                        row[x++] = v;
                    written++;
                }
            } else {
                if (x < width)
                    row[x++] = v;
                written++;
            }
        }
        x = 0;
    }

    /* Locate the trailing palette. */
    if (len >= 769 && buf[len - 769] == 0x0C)
        palette = buf + len - 768;
    else if (len >= 768)
        palette = buf + len - 768;

    if (palette != NULL) {
        Uint32 pal32[256];
        int i;
        for (i = 0; i < 256; i++) {
            pal32[i] = SDL_MapRGBA(surf->format, palette[i * 3 + 0],
                                                 palette[i * 3 + 1],
                                                 palette[i * 3 + 2], 255);
        }
        for (y = 0; y < height; y++) {
            Uint8* srcrow = (Uint8*)surf->pixels + (size_t)y * surf->pitch;
            Uint32* row = (Uint32*)srcrow;
            for (x = width - 1; x >= 0; x--) {
                row[x] = pal32[srcrow[x]];
            }
        }
    } else {
        /* No palette: expand the greyscale ramp so the image is not garbage. */
        Uint32 pal32[256];
        int i;
        for (i = 0; i < 256; i++) {
            pal32[i] = SDL_MapRGBA(surf->format, (Uint8)i, (Uint8)i, (Uint8)i, 255);
        }
        for (y = 0; y < height; y++) {
            Uint8* srcrow = (Uint8*)surf->pixels + (size_t)y * surf->pitch;
            Uint32* row = (Uint32*)srcrow;
            for (x = width - 1; x >= 0; x--) {
                row[x] = pal32[srcrow[x]];
            }
        }
    }

    return surf;

fail:
    SDL_FreeSurface(surf);
    img_set_error("IMG_LoadPCX_RW: truncated PCX data");
    return NULL;
}

/* --------------------------------------------------------- PNG / TGA via stb */

static SDL_Surface* load_with_stb(const Uint8* buf, size_t len)
{
    int w = 0, h = 0, comp = 0;
    stbi_uc* rgba;
    SDL_Surface* surf;
    int y;

    rgba = stbi_load_from_memory(buf, (int)len, &w, &h, &comp, 4);
    if (rgba == NULL) {
        img_set_error(stbi_failure_reason() ? stbi_failure_reason() : "IMG: decode failed");
        return NULL;
    }

    surf = new_rgba_surface(w, h);
    if (surf == NULL) {
        stbi_image_free(rgba);
        return NULL;
    }

    for (y = 0; y < h; y++)
        memcpy((Uint8*)surf->pixels + (size_t)y * surf->pitch,
               rgba + (size_t)y * w * 4, (size_t)w * 4);

    stbi_image_free(rgba);
    return surf;
}

/* ------------------------------------------------------------- public API */

SDL_Surface* IMG_LoadPCX_RW(SDL_RWops* src)
{
    size_t len = 0;
    Uint8* buf = slurp(src, &len);
    SDL_Surface* surf;

    if (buf == NULL)
        return NULL;

    surf = load_pcx(buf, len);
    free(buf);
    return surf;
}

SDL_Surface* IMG_LoadPNG_RW(SDL_RWops* src)
{
    size_t len = 0;
    Uint8* buf = slurp(src, &len);
    SDL_Surface* surf;

    if (buf == NULL)
        return NULL;

    surf = load_with_stb(buf, len);
    free(buf);
    return surf;
}

SDL_Texture* IMG_LoadTexture(SDL_Renderer* renderer, const char* file)
{
    SDL_RWops* rw;
    SDL_Surface* surf;
    SDL_Texture* tex;
    const char* ext;
    size_t len = 0;
    Uint8* buf;

    if (renderer == NULL || file == NULL) {
        img_set_error("IMG_LoadTexture: invalid arguments");
        return NULL;
    }

    rw = SDL_RWFromFile(file, "rb");
    if (rw == NULL) {
        img_set_error("IMG_LoadTexture: could not open file");
        return NULL;
    }

    buf = slurp(rw, &len);
    SDL_RWclose(rw);
    if (buf == NULL)
        return NULL;

    ext = strrchr(file, '.');
    if (ext != NULL && (ext[1] == 'p' || ext[1] == 'P'))
        surf = load_with_stb(buf, len);
    else
        surf = load_with_stb(buf, len); /* TGA and anything else stb handles */

    free(buf);
    if (surf == NULL)
        return NULL;

    tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}
