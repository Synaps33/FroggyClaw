#ifndef SDL_COMPAT_SDL_IMAGE_H
#define SDL_COMPAT_SDL_IMAGE_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

SDL_Surface* IMG_LoadPCX_RW(SDL_RWops* src);
SDL_Surface* IMG_LoadPNG_RW(SDL_RWops* src);
SDL_Texture* IMG_LoadTexture(SDL_Renderer* renderer, const char* file);
const char*  IMG_GetError(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_COMPAT_SDL_IMAGE_H */
