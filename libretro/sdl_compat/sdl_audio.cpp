/*
 * sdl_compat - audio: a small SDL_mixer replacement.
 *
 * Sound effects are PCM WAV, which decodes to a plain byte buffer, so a mixer
 * is just a table of voices stepped once per output frame. Music in OpenClaw
 * arrives as XMI-converted MIDI; there is no soundfont synthesizer on the
 * console, so Mix_LoadMUS_RW fails cleanly and the engine carries on without
 * music rather than trying to decode it.
 *
 * Output is 22050 Hz stereo signed 16-bit, matching what the libretro host
 * expects, and is rendered one video frame's worth at a time.
 */
#include "sdl_compat.h"

#define MIX_MAX_CHANNELS     32
#define MIX_MIX_FREQUENCY   22050
#define MIX_MIX_CHANNELS        2

typedef struct {
    Mix_Chunk* chunk;
    bool       active;
    int        position;    /* byte offset into the chunk's sample data */
    int        step;        /* byte advance per output frame */
    int        loops_left;  /* -1 = forever */
    int        volume;      /* 0..MIX_MAX_VOLUME */
} voice_t;

static voice_t s_voices[MIX_MAX_CHANNELS];
static int     s_channel_tag[MIX_MAX_CHANNELS];
static int     s_num_channels = MIX_MAX_CHANNELS;
static int     s_reserved = 16;
static int     s_spec_freq = 0;
static Uint16  s_spec_format = 0;
static int     s_spec_channels = 0;
static bool    s_audio_open = false;
static int     s_music_volume = MIX_MAX_VOLUME;
static char    s_mix_error[128] = {0};

/* Mix_Chunk sample data, always normalised to signed 16-bit. */
typedef struct {
    Sint16* samples;   /* interleaved, s16 */
    size_t  frames;    /* frames per channel */
    int     channels;
    int     freq;
} pcm_t;

/* Decoded PCM backing store for each Mix_Chunk we hand out. */
typedef struct chunk_priv {
    pcm_t pcm;
} chunk_priv_t;

static int mix_set_error(const char* msg)
{
    strncpy(s_mix_error, msg, sizeof(s_mix_error) - 1);
    s_mix_error[sizeof(s_mix_error) - 1] = '\0';
    return -1;
}

const char* Mix_GetError(void)
{
    return s_mix_error;
}

/* ------------------------------------------------------------------ setup */

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize)
{
    int i;
    (void)format; (void)chunksize;
    memset(s_voices, 0, sizeof(s_voices));
    for (i = 0; i < MIX_MAX_CHANNELS; i++)
        s_channel_tag[i] = -1;
    s_num_channels = MIX_MAX_CHANNELS;
    s_reserved = 0;
    s_spec_freq = frequency ? frequency : MIX_MIX_FREQUENCY;
    s_spec_channels = channels ? channels : MIX_MIX_CHANNELS;
    s_spec_format = MIX_DEFAULT_FORMAT;
    s_audio_open = true;
    return 0;
}

void Mix_CloseAudio(void)
{
    Mix_HaltChannel(-1);
    Mix_HaltMusic();
    s_audio_open = false;
}

int Mix_AllocateChannels(int numchans)
{
    int i;
    if (numchans < 0)
        return -1;
    if (numchans > MIX_MAX_CHANNELS)
        numchans = MIX_MAX_CHANNELS;
    s_num_channels = numchans;
    memset(s_voices, 0, sizeof(s_voices));
    for (i = 0; i < MIX_MAX_CHANNELS; i++)
        s_channel_tag[i] = -1;
    return s_num_channels;
}

int Mix_ReserveChannels(int num)
{
    if (num < 0)
        return -1;
    if (num > s_num_channels)
        num = s_num_channels;
    s_reserved = num;
    return num;
}

int Mix_GroupChannels(int from, int to, int tag)
{
    int i, count = 0;
    if (from < 0) from = 0;
    if (to >= s_num_channels) to = s_num_channels - 1;
    for (i = from; i <= to; i++) {
        s_channel_tag[i] = tag;
        count++;
    }
    return count;
}

int Mix_GroupAvailable(int tag)
{
    int i;
    for (i = 0; i < s_num_channels; i++) {
        if (s_channel_tag[i] == tag && !s_voices[i].active)
            return i;
    }
    /* Fallback: if all channels with this tag are active, steal the first in the group */
    for (i = 0; i < s_num_channels; i++) {
        if (s_channel_tag[i] == tag)
            return i;
    }
    return -1;
}

int Mix_QuerySpec(int* frequency, Uint16* format, int* channels)
{
    if (!s_audio_open)
        return 0;
    if (frequency) *frequency = s_spec_freq;
    if (format)   *format   = s_spec_format;
    if (channels) *channels = s_spec_channels;
    return 1;
}

/* ------------------------------------------------------------------ WAV */

static Uint32 rd_u32le(const Uint8* p)
{
    return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}

static Uint16 rd_u16le(const Uint8* p)
{
    return (Uint16)((Uint32)p[0] | ((Uint32)p[1] << 8));
}

/* Decode a RIFF/WAVE buffer into normalised s16 PCM. */
static bool decode_wav(const Uint8* buf, size_t len, pcm_t* out)
{
    size_t pos = 12;
    bool have_fmt = false;
    Uint16 fmt_tag = 0, channels = 0, bits = 0;
    Uint32 rate = 0;
    const Uint8* data = NULL;
    size_t data_len = 0;

    if (len < 44 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
        return false;

    while (pos + 8 <= len) {
        Uint32 chunk_size = rd_u32le(buf + pos + 4);
        const Uint8* body = buf + pos + 8;

        if (pos + 8 + chunk_size > len)
            chunk_size = (Uint32)(len - pos - 8);

        if (memcmp(buf + pos, "fmt ", 4) == 0 && chunk_size >= 16) {
            fmt_tag  = rd_u16le(body + 0);
            channels = rd_u16le(body + 2);
            rate     = rd_u32le(body + 4);
            bits     = rd_u16le(body + 14);
            have_fmt = true;
        } else if (memcmp(buf + pos, "data", 4) == 0) {
            data = body;
            data_len = chunk_size;
        }

        pos += 8 + chunk_size + (chunk_size & 1); /* chunks are word aligned */
    }

    if (!have_fmt || data == NULL || data_len == 0)
        return false;
    if (fmt_tag != 1)          /* PCM only; reject float/ADPCM */
        return false;
    if (channels < 1 || channels > 2)
        return false;
    if (bits != 8 && bits != 16)
        return false;
    if (rate == 0)
        return false;

    out->channels = channels;
    out->freq = (int)rate;

    if (bits == 16) {
        size_t frames = data_len / (2u * (size_t)channels);
        out->samples = (Sint16*)malloc((frames ? frames : 1) * (size_t)channels * sizeof(Sint16));
        if (out->samples == NULL)
            return false;
        for (size_t i = 0; i < frames * (size_t)channels; i++) {
            Sint16 v = (Sint16)rd_u16le(data + i * 2);
            out->samples[i] = v;
        }
        out->frames = frames;
    } else {
        size_t frames = data_len / (size_t)channels;
        out->samples = (Sint16*)malloc((frames ? frames : 1) * (size_t)channels * sizeof(Sint16));
        if (out->samples == NULL)
            return false;
        for (size_t i = 0; i < frames * (size_t)channels; i++)
            out->samples[i] = (Sint16)(((int)data[i] - 128) << 8);
        out->frames = frames;
    }
    return true;
}

/* Mix_Chunk.albuf holds the original WAV bytes so the engine's own bookkeeping
 * (alen, volume) keeps working; the decoded PCM hangs off the same allocation. */
static const chunk_priv_t* chunk_pcm(const Mix_Chunk* c)
{
    return (const chunk_priv_t*)c->abuf;
}

static Mix_Chunk* make_chunk_from_wav(const Uint8* wav, size_t wav_len, bool takes_ownership)
{
    Mix_Chunk* c;
    chunk_priv_t* priv;

    c = (Mix_Chunk*)calloc(1, sizeof(Mix_Chunk));
    if (c == NULL)
        return NULL;

    priv = (chunk_priv_t*)calloc(1, sizeof(chunk_priv_t));
    if (priv == NULL) { free(c); return NULL; }

    if (!decode_wav(wav, wav_len, &priv->pcm)) {
        free(priv);
        free(c);
        return NULL;
    }

    c->allocated = takes_ownership ? 1 : 0;
    c->abuf = (Uint8*)priv;
    c->alen = (Uint32)(priv->pcm.frames * (size_t)priv->pcm.channels * sizeof(Sint16));
    c->volume = MIX_MAX_VOLUME;
    c->freq = priv->pcm.freq;
    c->channels = priv->pcm.channels;
    c->bits = 16;
    return c;
}

Mix_Chunk* Mix_LoadWAV_RW(SDL_RWops* src, int freesrc)
{
    Sint64 total;
    Uint8* buf;
    size_t got;
    Mix_Chunk* c;

    if (src == NULL)
        return NULL;

    total = src->size(src);
    if (total <= 0 || total > (Sint64)8 * 1024 * 1024) {
        if (freesrc) SDL_RWclose(src);
        return NULL;
    }

    buf = (Uint8*)malloc((size_t)total);
    if (buf == NULL) {
        if (freesrc) SDL_RWclose(src);
        return NULL;
    }

    got = src->read(src, buf, 1, (size_t)total);
    if (freesrc)
        SDL_RWclose(src);

    c = (got == (size_t)total) ? make_chunk_from_wav(buf, got, true) : NULL;
    free(buf);

    if (c == NULL)
        mix_set_error("Mix_LoadWAV_RW: unsupported or malformed WAV");
    return c;
}

void Mix_FreeChunk(Mix_Chunk* c)
{
    if (c == NULL)
        return;
    Mix_HaltChannel(-1);
    if (c->abuf != NULL) {
        chunk_priv_t* priv = (chunk_priv_t*)c->abuf;
        free(priv->pcm.samples);
        free(priv);
    }
    free(c);
}

/* ----------------------------------------------------------------- music */

Mix_Music* Mix_LoadMUS_RW(SDL_RWops* src, int freesrc)
{
    /* MIDI needs a soundfont synth we do not ship on the console. Fail with a
     * clear message; Audio::PlayMusic logs it and continues silently. */
    if (src != NULL && freesrc)
        SDL_RWclose(src);
    mix_set_error("Mix_LoadMUS_RW: MIDI playback is not supported on this platform");
    return NULL;
}

void Mix_FreeMusic(Mix_Music* m)
{
    (void)m;
}

int Mix_PlayMusic(Mix_Music* m, int loops)
{
    (void)m; (void)loops;
    mix_set_error("Mix_PlayMusic: MIDI playback is not supported on this platform");
    return -1;
}

void Mix_PauseMusic(void) { }
void Mix_ResumeMusic(void) { }

int Mix_HaltMusic(void)
{
    return 0;
}

int Mix_VolumeMusic(int volume)
{
    s_music_volume = volume;
    return volume;
}

/* --------------------------------------------------------------- channels */

int Mix_PlayChannel(int channel, Mix_Chunk* chunk, int loops)
{
    const chunk_priv_t* priv;
    voice_t* v;
    int i;

    if (chunk == NULL)
        return mix_set_error("Mix_PlayChannel: NULL chunk");

    priv = chunk_pcm(chunk);
    if (priv == NULL || priv->pcm.samples == NULL)
        return mix_set_error("Mix_PlayChannel: chunk has no PCM data");

    /* -1 means "first free voice", matching SDL_mixer. */
    if (channel < 0) {
        for (i = 0; i < s_num_channels; i++) {
            if (!s_voices[i].active) { channel = i; break; }
        }
        if (channel < 0)
            channel = 0; /* steal the first voice */
    }
    if (channel >= s_num_channels)
        return mix_set_error("Mix_PlayChannel: channel out of range");

    v = &s_voices[channel];
    v->chunk = chunk;
    v->active = true;
    v->position = 0;
    /* Resample by byte-step so a 44.1 kHz source plays at the 22.05 kHz rate. */
    v->step = (int)(((int64_t)priv->pcm.freq * 2 * (int64_t)priv->pcm.channels << 16) /
                    (MIX_MIX_FREQUENCY * 2 * (int64_t)priv->pcm.channels)) / 65536;
    if (v->step < 1)
        v->step = 1;
    v->loops_left = loops;
    v->volume = (chunk->volume != 0) ? chunk->volume : MIX_MAX_VOLUME;
    return channel;
}

int Mix_VolumeChunk(Mix_Chunk* c, int volume)
{
    if (c == NULL)
        return -1;
    c->volume = (Uint8)volume;
    return volume;
}

int Mix_Volume(int channel, int volume)
{
    int i, first = 0, last = s_num_channels - 1;

    if (channel >= 0) {
        if (channel >= s_num_channels)
            return -1;
        s_voices[channel].volume = volume;
        return volume;
    }
    for (i = first; i <= last; i++)
        s_voices[i].volume = volume;
    return volume;
}

void Mix_Pause(int channel)
{
    if (channel < 0) {
        int i;
        for (i = 0; i < s_num_channels; i++) s_voices[i].active = false;
    } else if (channel < s_num_channels) {
        s_voices[channel].active = false;
    }
}

void Mix_Resume(int channel)
{
    (void)channel; /* voices end on their own; nothing is left to resume */
}

int Mix_HaltChannel(int channel)
{
    int i, first = 0, last = s_num_channels - 1;

    if (channel >= 0) {
        if (channel >= s_num_channels)
            return -1;
        memset(&s_voices[channel], 0, sizeof(voice_t));
        return 0;
    }
    for (i = first; i <= last; i++)
        memset(&s_voices[i], 0, sizeof(voice_t));
    return 0;
}

int Mix_SetPosition(int channel, Sint16 angle, Uint8 distance)
{
    (void)angle; (void)distance;
    /* No panning table on the console; positional audio would cost more than it
     * is worth here, so treat every request as "straight ahead". */
    return (channel >= 0 && channel < s_num_channels) ? 1 : 0;
}

int Mix_SetDistance(int channel, Uint8 distance)
{
    (void)distance;
    return (channel >= 0 && channel < s_num_channels) ? 1 : 0;
}

/* ------------------------------------------------------------------ mixer */

void sdl_compat_render_audio(int16_t* out, size_t frames)
{
    size_t f;

    if (out == NULL)
        return;

    memset(out, 0, frames * 2 * sizeof(int16_t));
    if (!s_audio_open)
        return;

    int active_indices[16];
    int n_active = 0;
    for (int i = 0; i < s_num_channels; i++) {
        if (s_voices[i].active && s_voices[i].chunk != NULL)
            active_indices[n_active++] = i;
    }
    if (n_active == 0)
        return;

    for (f = 0; f < frames; f++) {
        int32_t mix_l = 0;
        int32_t mix_r = 0;

        for (int a = 0; a < n_active; a++) {
            int i = active_indices[a];
            voice_t* v = &s_voices[i];
            const pcm_t* pcm;
            int idx;
            Sint32 l, r;

            if (!v->active || v->chunk == NULL)
                continue;

            pcm = &chunk_pcm(v->chunk)->pcm;
            if (pcm->samples == NULL || pcm->channels == 0) {
                v->active = false;
                continue;
            }

            idx = v->position / 2; /* byte offset -> sample index */
            if (idx < 0 || (size_t)idx >= pcm->frames * (size_t)pcm->channels) {
                if (v->loops_left != 0) {
                    if (v->loops_left > 0)
                        v->loops_left--;
                    v->position = 0;
                    v->step = (pcm->freq * 2 * pcm->channels) / MIX_MIX_FREQUENCY;
                    if (v->step < 1) v->step = 1;
                    idx = 0;
                } else {
                    memset(v, 0, sizeof(*v));
                    continue;
                }
            }

            if (pcm->channels == 1) {
                l = r = pcm->samples[idx];
            } else {
                l = pcm->samples[(size_t)idx * 2];
                r = pcm->samples[(size_t)idx * 2 + 1];
            }

            l = (l * v->volume) / MIX_MAX_VOLUME;
            r = (r * v->volume) / MIX_MAX_VOLUME;
            mix_l += l;
            mix_r += r;
            v->position += v->step;
        }

        if (mix_l > 32767) mix_l = 32767; else if (mix_l < -32768) mix_l = -32768;
        if (mix_r > 32767) mix_r = 32767; else if (mix_r < -32768) mix_r = -32768;
        out[f * 2]     = (int16_t)mix_l;
        out[f * 2 + 1] = (int16_t)mix_r;
    }
}
