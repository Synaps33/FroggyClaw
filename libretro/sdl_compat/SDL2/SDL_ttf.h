#ifndef SDL_COMPAT_SDL_TTF_H
#define SDL_COMPAT_SDL_TTF_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TTF_Font {
    int height;
} TTF_Font;

int  TTF_Init(void);
void TTF_Quit(void);

TTF_Font* TTF_OpenFont(const char *file, int ptsize);
void      TTF_CloseFont(TTF_Font *font);
int       TTF_FontHeight(const TTF_Font *font);
int       TTF_SizeText(TTF_Font *font, const char *text, int *w, int *h);
SDL_Surface* TTF_RenderText_Blended(TTF_Font *font, const char *text, SDL_Color fg);
const char* TTF_GetError(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_COMPAT_SDL_TTF_H */
