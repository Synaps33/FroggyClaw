#ifndef SDL_COMPAT_SDL_MIXER_H
#define SDL_COMPAT_SDL_MIXER_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIX_DEFAULT_FORMAT 0x8010
#define MIX_MAX_VOLUME     128

typedef struct Mix_Chunk {
    int allocated;
    Uint8 *abuf;
    Uint32 alen;
    Uint8 volume;
    int freq;
    int channels;
    int bits;
} Mix_Chunk;

typedef struct Mix_Music {
    void *data;
} Mix_Music;

int  Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
void Mix_CloseAudio(void);
int  Mix_AllocateChannels(int numchans);
int  Mix_ReserveChannels(int num);
int  Mix_GroupChannels(int from, int to, int tag);
int  Mix_GroupAvailable(int tag);

int  Mix_Volume(int channel, int volume);
int  Mix_VolumeChunk(Mix_Chunk *chunk, int volume);
int  Mix_VolumeMusic(int volume);

int  Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops);
int  Mix_PlayMusic(Mix_Music *music, int loops);

void Mix_Pause(int channel);
void Mix_Resume(int channel);
int  Mix_HaltChannel(int channel);

void Mix_PauseMusic(void);
void Mix_ResumeMusic(void);
int  Mix_HaltMusic(void);

int  Mix_SetPosition(int channel, Sint16 angle, Uint8 distance);
int  Mix_SetDistance(int channel, Uint8 distance);

Mix_Chunk* Mix_LoadWAV_RW(SDL_RWops *src, int freesrc);
void       Mix_FreeChunk(Mix_Chunk *chunk);

Mix_Music* Mix_LoadMUS_RW(SDL_RWops *src, int freesrc);
void       Mix_FreeMusic(Mix_Music *music);

int  Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);
const char* Mix_GetError(void);

#ifdef __cplusplus
}
#endif

#endif /* SDL_COMPAT_SDL_MIXER_H */
