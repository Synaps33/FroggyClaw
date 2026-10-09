#ifndef SDL_COMPAT_SDL2_GFXPRIMITIVES_H
#define SDL_COMPAT_SDL2_GFXPRIMITIVES_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

int boxRGBA(SDL_Renderer *renderer, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int lineRGBA(SDL_Renderer *renderer, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int circleRGBA(SDL_Renderer *renderer, Sint16 x, Sint16 y, Sint16 rad, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int filledCircleRGBA(SDL_Renderer *renderer, Sint16 x, Sint16 y, Sint16 rad, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int polygonRGBA(SDL_Renderer *renderer, const Sint16 *vx, const Sint16 *vy, int num_vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int filledPolygonRGBA(SDL_Renderer *renderer, const Sint16 *vx, const Sint16 *vy, int num_vertices, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int pixelRGBA(SDL_Renderer *renderer, Sint16 x, Sint16 y, Uint8 r, Uint8 g, Uint8 b, Uint8 a);

#ifdef __cplusplus
}
#endif

#endif /* SDL_COMPAT_SDL2_GFXPRIMITIVES_H */
