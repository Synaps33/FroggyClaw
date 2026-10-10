#ifndef SDL_COMPAT_H
#define SDL_COMPAT_H

#include "SDL2/SDL.h"
#include "SDL2/SDL_image.h"
#include "SDL2/SDL_mixer.h"
#include "SDL2/SDL_ttf.h"
#include "SDL2/SDL2_gfxPrimitives.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Libretro interface hooks */
void sdl_compat_set_framebuffer(uint16_t* fb, int width, int height);
uint16_t* sdl_compat_get_framebuffer(void);
void sdl_compat_set_base_path(const char* path);
const char* sdl_compat_get_base_path(void);
void sdl_compat_push_key(SDL_Keycode key, bool down);
void sdl_compat_queue_quit(void);
void sdl_compat_render_audio(int16_t* out_samples, size_t num_frames);
void sdl_compat_advance_ticks(uint32_t ms);
void sdl_compat_reset(void);
void SDL_SetLogEnabled(int enabled);

/* Memory diagnostics: live bytes owned by the compat layer, for the sbrk
 * OOM hunt on the 52MB handheld heap. */
size_t sdl_compat_texture_bytes(void);
size_t sdl_compat_audio_bytes(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_COMPAT_H */
