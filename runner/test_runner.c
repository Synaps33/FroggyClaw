/*
 * Standalone libretro harness for the OpenClaw SF2000 core.
 *
 * Loads openclaw_libretro.so with dlopen, runs a scripted joypad sequence and
 * dumps the RGB565 framebuffer to PPM so the rendering path can be checked
 * without a console attached. Timing per retro_run() is reported as well, since
 * frame cost is what decides whether the MIPS build keeps up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <time.h>
#include <stdarg.h>
#include <malloc.h>

#include "libretro.h"

#define WIDTH  320
#define HEIGHT 240

static uint16_t s_frame[WIDTH * HEIGHT];
static int       s_frame_count;
static unsigned  s_last_width, s_last_height;
static uint32_t  s_pad_mask;
static int       s_shutdown_requested;
static int64_t   s_audio_frames_total;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}


static void test_log_printf(enum retro_log_level level, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

static void cb_video_refresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
    unsigned y, x;

    if (!data)
        return;

    s_last_width = width;
    s_last_height = height;
    if (width > WIDTH)  width = WIDTH;
    if (height > HEIGHT) height = HEIGHT;

    for (y = 0; y < height; y++) {
        const uint8_t* src = (const uint8_t*)data + y * pitch;
        uint16_t* dst = &s_frame[y * WIDTH];
        for (x = 0; x < width; x++)
            dst[x] = *(const uint16_t*)(src + x * 2);
    }
    s_frame_count++;
}

static void cb_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }

static size_t cb_audio_sample_batch(const int16_t* d, size_t frames)
{
    (void)d;
    s_audio_frames_total += (int64_t)frames;
    return frames;
}

static void cb_input_poll(void) { }

static int16_t cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port; (void)index;
    if (device != RETRO_DEVICE_JOYPAD)
        return 0;
    return (s_pad_mask & (1u << id)) ? 1 : 0;
}

static bool cb_environment(unsigned cmd, void* data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(enum retro_pixel_format*)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (data) {
            struct retro_log_callback* cb = (struct retro_log_callback*)data;
            cb->log = test_log_printf;
            return true;
        }
        return false;
    case RETRO_ENVIRONMENT_SHUTDOWN:
        s_shutdown_requested = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        return false;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool*)data = false;
        return true;
    default:
        return false;
    }
}

static int count_non_black(const uint16_t* fb, int count)
{
    int i, nb = 0;
    for (i = 0; i < count; i++)
        if (fb[i] != 0)
            nb++;
    return nb;
}

static int count_distinct(const uint16_t* fb, int count)
{
    static uint16_t seen[4096];
    int i, n = 0;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < count; i++) {
        uint16_t v = fb[i];
        uint16_t h;
        if (v == 0)
            continue;
        h = (uint16_t)((v * 2654435761u) >> 20);
        seen[h & 4095] = 1;
    }
    for (i = 0; i < 4096; i++)
        if (seen[i])
            n++;
    return n;
}

static void save_ppm(const char* path)
{
    FILE* f = fopen(path, "wb");
    int y, x;

    if (!f)
        return;

    fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            uint16_t p = s_frame[y * WIDTH + x];
            fputc((p >> 11) << 3, f);
            fputc(((p >> 5) & 0x3F) << 2, f);
            fputc((p & 0x1F) << 3, f);
        }
    }
    fclose(f);
    printf("[TEST] wrote %s\n", path);
}

static void* g_handle;

static void* sym(const char* name)
{
    void* p = dlsym(g_handle, name);
    if (!p) {
        fprintf(stderr, "missing symbol %s\n", name);
        exit(1);
    }
    return p;
}

int main(int argc, char** argv)
{
    void* handle;
    struct retro_system_info sys;
    struct retro_system_av_info av;
    struct retro_game_info game;
    const char* so_path   = (argc > 1) ? argv[1] : "./openclaw_libretro.so";
    const char* rom_path  = (argc > 2) ? argv[2] : NULL;
    int total_frames = (argc > 3) ? atoi(argv[3]) : 300;
    int dump_every = (argc > 4) ? atoi(argv[4]) : 30;
    int render_scale = (argc > 5) ? atoi(argv[5]) : 0;
    int i, phase;
    double worst = 0.0, total = 0.0, best = 1e30;
    double steady_total = 0.0;
    int    steady_count = 0;

    unsigned (*retro_api_version)(void);
    void (*retro_get_system_info)(struct retro_system_info*);
    void (*retro_get_system_av_info)(struct retro_system_av_info*);
    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_init)(void);
    void (*retro_deinit)(void);
    bool (*retro_load_game)(const struct retro_game_info*);
    void (*retro_run)(void);
    void (*retro_unload_game)(void);
    unsigned (*retro_get_region)(void);

    handle = dlopen(so_path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    g_handle = handle;

    *(void**)&retro_api_version            = sym("retro_api_version");
    *(void**)&retro_get_system_info        = sym("retro_get_system_info");
    *(void**)&retro_get_system_av_info     = sym("retro_get_system_av_info");
    *(void**)&retro_set_environment        = sym("retro_set_environment");
    *(void**)&retro_set_video_refresh      = sym("retro_set_video_refresh");
    *(void**)&retro_set_audio_sample       = sym("retro_set_audio_sample");
    *(void**)&retro_set_audio_sample_batch = sym("retro_set_audio_sample_batch");
    *(void**)&retro_set_input_poll         = sym("retro_set_input_poll");
    *(void**)&retro_set_input_state        = sym("retro_set_input_state");
    *(void**)&retro_init                   = sym("retro_init");
    *(void**)&retro_deinit                 = sym("retro_deinit");
    *(void**)&retro_load_game              = sym("retro_load_game");
    *(void**)&retro_run                    = sym("retro_run");
    *(void**)&retro_unload_game            = sym("retro_unload_game");
    *(void**)&retro_get_region             = sym("retro_get_region");

    printf("[TEST] API version = %u\n", retro_api_version());

    retro_set_environment(cb_environment);
    retro_set_video_refresh(cb_video_refresh);
    retro_set_audio_sample(cb_audio_sample);
    retro_set_audio_sample_batch(cb_audio_sample_batch);
    retro_set_input_poll(cb_input_poll);
    retro_set_input_state(cb_input_state);

    retro_get_system_info(&sys);
    printf("[TEST] library: %s %s, ext='%s', need_fullpath=%d\n",
           sys.library_name, sys.library_version, sys.valid_extensions, sys.need_fullpath);

    retro_get_system_av_info(&av);
    printf("[TEST] av: %ux%u fps=%f rate=%f\n", av.geometry.base_width,
           av.geometry.base_height, av.timing.fps, av.timing.sample_rate);

    retro_init();

    memset(&game, 0, sizeof(game));
    game.path = rom_path;
    if (render_scale > 0) {
        void (*set_render_scale)(int) = sym("retro_set_render_scale");
        set_render_scale(render_scale);
        retro_get_system_av_info(&av);
        printf("[TEST] av after scale %d: %ux%u\n", render_scale,
               av.geometry.base_width, av.geometry.base_height);
    }
    if (!retro_load_game(&game)) {
        fprintf(stderr, "retro_load_game failed\n");
        retro_deinit();
        return 1;
    }
    printf("[TEST] game loaded\n");

    /* Input script:
     *   frames 0-9     : idle, menu loads
     *   frames 10-14   : START (Single Player)
     *   frames 25-29   : START (New Game -> Level 1)
     *   frames 60-100  : walk right
     *   frames 100-115 : jump
     *   frames 120-140 : melee + walk
     *   frames 150-170 : shoot
     *   frames 180-200 : crouch
     *   frames 210-230 : walk left
     *   frames 240-260 : jump right
     */
    for (i = 0; i < total_frames; i++) {
        s_pad_mask = 0;

        if (i >= 10 && i < 15) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_START);
        } else if (i >= 25 && i < 30) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_START);
        } else if (i >= 40 && i < 45) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_START);
        } else if (i >= 60 && i < 100) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
        } else if (i >= 100 && i < 115) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_A);
        } else if (i >= 120 && i < 140) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_X) |
                           (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
        } else if (i >= 150 && i < 170) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_B);
        } else if (i >= 180 && i < 200) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
        } else if (i >= 210 && i < 230) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
        } else if (i >= 240 && i < 260) {
            s_pad_mask |= (1u << RETRO_DEVICE_ID_JOYPAD_A) |
                           (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
        }

        {
            double t0 = now_ms();
            retro_run();
            double ms = now_ms() - t0;
            total += ms;
            if (ms > worst)
                worst = ms;
            if (ms < best)
                best = ms;
            /* Level loads take seconds and would swamp every other frame,
             * so steady-state timing ignores anything above 50 ms. */
            if (ms < 50.0) {
                steady_total += ms;
                steady_count++;
            }
        }

        if (dump_every > 0 && (i % dump_every) == 0) {
            char name[64];
            snprintf(name, sizeof(name), "frames/frame_%04d.ppm", i);
            save_ppm(name);
            printf("[TEST] frame %4d: frames=%d nonblack=%d colors=%d (%dx%d) [HEAP: %.2f MB]\n",
                   i, s_frame_count, count_non_black(s_frame, WIDTH * HEIGHT),
                   count_distinct(s_frame, WIDTH * HEIGHT), s_last_width, s_last_height,
                   (double)mallinfo2().uordblks / (1024.0 * 1024.0));
            fflush(stdout);
        }
        if (s_shutdown_requested) {
            printf("[TEST] shutdown requested at frame %d\n", i);
            break;
        }
    }

    if (total_frames > 0)
        /* best is the robust figure: this box is shared, so the mean drifts
         * with whatever else is running. */
        printf("[TEST] retro_run steady %.3f ms (%d frames), best %.3f ms, avg %.2f ms, worst %.2f ms\n",
               steady_count ? steady_total / steady_count : 0.0, steady_count,
               best, total / total_frames, worst);
    printf("[TEST] frames presented: %d, audio frames: %lld\n",
           s_frame_count, (long long)s_audio_frames_total);

    retro_unload_game();
    retro_deinit();
    dlclose(handle);
    printf("[TEST] done\n");
    return 0;
}
