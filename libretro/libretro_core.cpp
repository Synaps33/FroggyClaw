/*
 * Captain Claw - libretro core for the Data Frog SF2000 / GB300.
 *
 * The host owns the screen, the clock and the joypad, so this file is a thin
 * adapter around BaseGameApp:
 *
 *   retro_load_game  locate the game assets, bring the engine up
 *   retro_run        translate joypad buttons into SDL key events, advance the
 *                    frame clock, call BaseGameApp::StepLoop(), then hand the
 *                    framebuffer and one frame of audio to the host
 *
 * StepLoop() is the engine's existing single-frame entry point (the Emscripten
 * build drives it the same way), so there is no coroutine or extra threading
 * layer between libretro and the game.
 *
 * SDL itself is replaced by libretro/sdl_compat, a software implementation that
 * renders straight into the host's 320x240 RGB565 framebuffer.
 */
#include "libretro.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <exception>
#include <typeinfo>
#include <cxxabi.h>

#include "sdl_compat.h"

#include "ClawGameApp.h"

#if defined(__mips__)
extern "C" void lcd_bsod(const char *fmt, ...) __attribute__((noreturn));
#else
extern "C" void lcd_bsod(const char *fmt, ...) __attribute__((noreturn));
extern "C" void lcd_bsod(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    abort();
}
#endif

static void openclaw_terminate_handler()
{
    const char* type_name = "unknown";
    char what_str[128] = "(none)";

    std::type_info* t = abi::__cxa_current_exception_type();
    if (t) {
        int status = 0;
        char* demangled = abi::__cxa_demangle(t->name(), NULL, NULL, &status);
        type_name = demangled ? demangled : t->name();
    }

    try {
        throw;
    } catch (const std::exception& e) {
        strncpy(what_str, e.what(), sizeof(what_str) - 1);
        what_str[sizeof(what_str) - 1] = '\0';
    } catch (...) {
        strncpy(what_str, "(non-std exception)", sizeof(what_str) - 1);
        what_str[sizeof(what_str) - 1] = '\0';
    }

#if defined(SF2000) || defined(__mips__)
    lcd_bsod("C++ EXCEPTION:\n%s\n%s", type_name, what_str);
#else
    fprintf(stderr, "C++ EXCEPTION: %s\n%s\n", type_name, what_str);
    abort();
#endif
}

namespace __gnu_cxx {
    void __verbose_terminate_handler()
    {
        openclaw_terminate_handler();
    }
}

/* ------------------------------------------------------------------ config */

#define CORE_NAME    "OpenClaw"
#define CORE_VERSION "1.0"
#define CORE_EXTS    "sf2k|s2k|claw"

/* The panel is 320x240, but the game is laid out in window pixels and the
 * camera viewport is sized from the window (see ClawHumanView). Rendering at
 * 320x240 therefore shows a postage-stamp slice of the level with full-size
 * sprites. The engine instead gets the 640x480 window the game was designed
 * for and sdl_compat reduces it 2:1 on the way out, so four times as much of
 * the level is visible at half sprite size. The reduction is exact. */
#define MAX_FRAME_WIDTH   320
#define MAX_FRAME_HEIGHT  240
#define DEFAULT_FRAME_WIDTH  256
#define DEFAULT_FRAME_HEIGHT 192
#define DEFAULT_RENDER_SCALE 80
#define WINDOW_WIDTH  640
#define WINDOW_HEIGHT 480
#define FRAME_FPS     30.0
#define AUDIO_RATE    22050.0
/* 22050 Hz across 30 frames: 735 frames. */
#define AUDIO_FRAMES  735
#define DEFAULT_MAX_FPS 30

/* ---------------------------------------------------------- frame pacing */

/* The console's run loop calls retro_run() as fast as it can get through it,
 * so on its own the game would run ahead of real time. retro_run() therefore
 * enforces a ceiling on how often a frame may start: when a cap is set, each
 * call waits for the next frame slot to come due.
 *
 * This is a ceiling and not a target. A host that already paces more slowly
 * than the cap is never slowed down any further, because the wait only ever
 * happens while there is still budget left on the current slot.
 *
 * Real time has to come from the firmware: SDL_GetTicks() only returns the
 * virtual clock the core advances by a fixed step per frame, so it cannot be
 * used to measure how long the frame really took. On the console the stock
 * firmware exports a millisecond tick counter and a task delay; both are
 * pinned to fixed addresses by the linker scripts. */
#if defined(__mips__)
extern "C" uint32_t os_get_tick_count(void);
extern "C" int      dly_tsk(unsigned ms);

static uint32_t real_now_ms(void)
{
    return os_get_tick_count();
}

static void real_sleep_ms(uint32_t ms)
{
    if (ms > 0)
        dly_tsk(ms);
}
#else
#include <time.h>
#include <unistd.h>

static uint32_t real_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
    return 0;
}

static void real_sleep_ms(uint32_t ms)
{
    if (ms > 0)
        usleep((useconds_t)ms * 1000u);
}
#endif

/* 0 disables the ceiling and lets the host run the core flat out. */
static int      s_max_fps      = DEFAULT_MAX_FPS;
static uint32_t s_next_due_ms  = 0;  /* real time the next frame may start */
static int      s_pacing_ready = 0;

/* Joypad buttons the game reacts to, in the order the engine expects them. */
typedef struct {
    uint16_t joypad_id;
    SDL_Keycode key;
} key_binding_t;

/* Two bindings per button so the four face buttons can double as the four
 * actions Claw needs, with START/SELECT handling menu navigation and pause. */
static const key_binding_t s_bindings[] = {
    { RETRO_DEVICE_ID_JOYPAD_LEFT,  SDLK_LEFT  },
    { RETRO_DEVICE_ID_JOYPAD_RIGHT, SDLK_RIGHT },
    { RETRO_DEVICE_ID_JOYPAD_UP,    SDLK_UP    },
    { RETRO_DEVICE_ID_JOYPAD_DOWN,  SDLK_DOWN  },
    /* Claw's own defaults: jump on Space, melee on Left Ctrl, shoot on Left
     * Alt, cycle ammo on Left Shift. */
    { RETRO_DEVICE_ID_JOYPAD_A,      SDLK_SPACE  },
    { RETRO_DEVICE_ID_JOYPAD_X,      SDLK_LCTRL  },
    { RETRO_DEVICE_ID_JOYPAD_B,      SDLK_LALT   },
    { RETRO_DEVICE_ID_JOYPAD_Y,      SDLK_LSHIFT },
    /* In menus A confirms and B cancels, matching RETURN/ESCAPE. */
    { RETRO_DEVICE_ID_JOYPAD_START,  SDLK_RETURN },
    { RETRO_DEVICE_ID_JOYPAD_SELECT, SDLK_ESCAPE },
};

#define BINDING_COUNT ((int)(sizeof(s_bindings) / sizeof(s_bindings[0])))

/* --------------------------------------------------------------- callbacks */

static retro_environment_t        s_environ_cb;
static retro_video_refresh_t      s_video_cb;
static retro_audio_sample_t       s_audio_cb;
static retro_audio_sample_batch_t s_audio_batch_cb;
static retro_input_poll_t         s_input_poll_cb;
static retro_input_state_t        s_input_state_cb;

static uint16_t s_framebuffer[MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT];
static int      s_render_width  = DEFAULT_FRAME_WIDTH;
static int      s_render_height = DEFAULT_FRAME_HEIGHT;
static int      s_render_scale  = DEFAULT_RENDER_SCALE;
static int16_t  s_audio_buffer[AUDIO_FRAMES * 2];

extern "C" {

void retro_set_output_buffer_size(int width, int height)
{
    if (width < 160) width = 160;
    if (width > MAX_FRAME_WIDTH) width = MAX_FRAME_WIDTH;
    if (height < 120) height = 120;
    if (height > MAX_FRAME_HEIGHT) height = MAX_FRAME_HEIGHT;
    s_render_width = width;
    s_render_height = height;
    sdl_compat_set_framebuffer(s_framebuffer, s_render_width, s_render_height);
}

void retro_set_render_scale(int scale_percent)
{
    if (scale_percent < 50) scale_percent = 50;
    if (scale_percent > 100) scale_percent = 100;
    s_render_scale = scale_percent;
    int w = (MAX_FRAME_WIDTH * scale_percent) / 100;
    int h = (MAX_FRAME_HEIGHT * scale_percent) / 100;
    w &= ~1;
    h &= ~1;
    retro_set_output_buffer_size(w, h);
}

int retro_get_render_scale(void)
{
    return s_render_scale;
}

void retro_get_output_buffer_size(int* w, int* h)
{
    if (w) *w = s_render_width;
    if (h) *h = s_render_height;
}

void retro_set_max_fps(int max_fps)
{
    if (max_fps < 0) max_fps = 0;
    if (max_fps != 0 && max_fps < 10) max_fps = 10;
    if (max_fps > 240) max_fps = 240;
    s_max_fps = max_fps;
    /* Start a fresh schedule so switching the cap on or off never inherits a
     * slot that is already in the past (that would only add a long stall). */
    s_pacing_ready = 0;
}

int retro_get_max_fps(void)
{
    return s_max_fps;
}

}

/* Blocks until the next frame slot is due. See the frame pacing notes above. */
static void pace_frame(void)
{
    uint32_t period, now;
    int32_t  wait_ms;

    if (s_max_fps <= 0)
        return;

    period = (uint32_t)(1000.0 / (double)s_max_fps);
    if (period == 0)
        period = 1;

    now = real_now_ms();
    if (!s_pacing_ready)
    {
        s_next_due_ms  = now + period;
        s_pacing_ready = 1;
        return;
    }

    if ((int32_t)(now - s_next_due_ms) >= (int32_t)period)
    {
        /* A whole frame behind, which happens after loading a level or when
         * the console is interrupted. Resync instead of trying to catch up by
         * running frames back to back. */
        s_next_due_ms = now + period;
        return;
    }

    wait_ms = (int32_t)(s_next_due_ms - now);
    if (wait_ms > 0)
        real_sleep_ms((uint32_t)wait_ms);

    s_next_due_ms += period;
}

static ClawGameApp* s_app = NULL;
static bool         s_loaded = false;
static bool         s_prev_buttons[BINDING_COUNT];
static char         s_content_dir[512];
static uint32_t     s_frame_ms_accum = 0;
static uint32_t     s_shutdown_requested = false;

static void core_log(enum retro_log_level level, const char* fmt, ...)
{
    struct retro_log_callback logger;
    char buf[480];
    va_list ap;

    if (s_environ_cb == NULL)
        return;
    if (!s_environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logger))
        return;
    if (logger.log == NULL)
        return;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    buf[sizeof(buf) - 1] = '\0';
    if (logger.log)
        logger.log(level, "[openclaw] %s\n", buf);
    else
        fprintf(stderr, "[openclaw] %s\n", buf);
}

/* Heap watermark for chasing the handheld sbrk OOMs: mallinfo().uordblks is
 * the total malloc'd bytes of the core heap. The engine calls this through a
 * weak symbol at load/unload boundaries, and retro_run samples it every two
 * seconds so a session log shows the whole growth curve. */
extern "C" void retro_mem_log(const char* tag)
{
    struct mallinfo mi = mallinfo();

    core_log(RETRO_LOG_INFO, "MEM %s: heap=%u KB",
        tag != NULL ? tag : "?", (unsigned)(mi.uordblks / 1024));
}

/* --------------------------------------------------------------- utilities */

static bool file_readable(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

static void path_dirname(const char* path, char* out, size_t out_size)
{
    const char* slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL) {
        out[0] = '\0';
        return;
    }
    n = (size_t)(slash - path);
    if (n >= out_size)
        n = out_size - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

static bool has_claw_assets(const char* dir)
{
    char probe[600];
    if (dir == NULL || dir[0] == '\0')
        return false;
    snprintf(probe, sizeof(probe), "%s/CLAW.REZ", dir);
    return file_readable(probe);
}

static void append_slash(char* path)
{
    size_t n = strlen(path);
    if (n > 0 && path[n - 1] != '/') {
        if (n + 2 < 512) {
            path[n] = '/';
            path[n + 1] = '\0';
        }
    }
}

/* Work out where CLAW.REZ and ASSETS.ZIP live. The ROM path is only a
 * placeholder (the frontend substitutes a stub when it is absent), so several
 * locations are tried before giving up. */
static bool resolve_content_dir(const struct retro_game_info* info)
{
    static const char* s_fallbacks[] = {
        "/mnt/sda1/roms/claw",
        "/mnt/sdcard/roms/claw",
        "/mnt/sda1/claw",
        "/mnt/sdcard/claw",
        "/sd/roms/claw",
        ".",
    };
    unsigned i;

    s_content_dir[0] = '\0';

    if (info != NULL && info->path != NULL) {
        char dir[512];
        path_dirname(info->path, dir, sizeof(dir));
        if (has_claw_assets(dir)) {
            strncpy(s_content_dir, dir, sizeof(s_content_dir) - 1);
            return true;
        }
    }

    /* GET_GAME_INFO_EXT gives the frontend's own view of the directory, which
     * differs between the SF2000 and GB300 firmwares. */
    if (s_environ_cb != NULL) {
        struct retro_game_info_ext* ext = NULL;
        if (s_environ_cb(RETRO_ENVIRONMENT_GET_GAME_INFO_EXT, &ext) && ext != NULL) {
            if (ext->full_path != NULL) {
                char dir[512];
                path_dirname(ext->full_path, dir, sizeof(dir));
                if (has_claw_assets(dir)) {
                    strncpy(s_content_dir, dir, sizeof(s_content_dir) - 1);
                    return true;
                }
            }
            if (ext->dir != NULL && has_claw_assets(ext->dir)) {
                strncpy(s_content_dir, ext->dir, sizeof(s_content_dir) - 1);
                return true;
            }
        }
    }

    for (i = 0; i < sizeof(s_fallbacks) / sizeof(s_fallbacks[0]); i++) {
        if (has_claw_assets(s_fallbacks[i])) {
            strncpy(s_content_dir, s_fallbacks[i], sizeof(s_content_dir) - 1);
            return true;
        }
    }
    return false;
}

/* A config the engine can parse is mandatory: LoadGameOptions fails the boot if
 * it is missing. Ship one and write it next to the assets when the user has not
 * supplied their own. */
static const char* s_portable_config =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<Configuration>\n"
"  <Display>\n"
"    <Size width=\"320\" height=\"240\" />\n"
"    <Scale>1</Scale>\n"
"    <UseVerticalSync>false</UseVerticalSync>\n"
"    <IsFullscreen>false</IsFullscreen>\n"
"    <IsFullscreenDesktop>false</IsFullscreenDesktop>\n"
"    <RenderScale>80</RenderScale>\n"
"    <MaxFps>30</MaxFps>\n"
"  </Display>\n"
"  <Audio>\n"
"    <Frequency>22050</Frequency>\n"
"    <SoundChannels>1</SoundChannels>\n"
"    <MixingChannels>16</MixingChannels>\n"
"    <ChunkSize>1024</ChunkSize>\n"
"    <SoundVolume>100</SoundVolume>\n"
"    <MusicVolume>0</MusicVolume>\n"
"    <SoundOn>true</SoundOn>\n"
"    <MusicOn>false</MusicOn>\n"
"  </Audio>\n"
"  <Font>\n"
"    <Fontt>clacon.ttf</Fontt>\n"
"    <ConsoleFont font=\"clacon.ttf\" size=\"20\" />\n"
"  </Font>\n"
"  <Assets>\n"
"    <AssetsFolder></AssetsFolder>\n"
"    <RezArchive>CLAW.REZ</RezArchive>\n"
"    <CustomArchive>ASSETS.ZIP</CustomArchive>\n"
"    <ResourceCacheSize>40</ResourceCacheSize>\n"
"    <TempDir></TempDir>\n"
"    <SavesFile>SAVES.XML</SavesFile>\n"
"  </Assets>\n"
"  <Console>\n"
"    <BackgroundImagePath>console02.tga</BackgroundImagePath>\n"
"    <StretchBackgroundImage></StretchBackgroundImage>\n"
"    <WidthRatio>1.0</WidthRatio>\n"
"    <HeightRatio>0.5</HeightRatio>\n"
"    <LineSeparatorHeight>3</LineSeparatorHeight>\n"
"    <CommandPromptOffsetY>10</CommandPromptOffsetY>\n"
"    <ConsoleAnimationSpeed>0.7</ConsoleAnimationSpeed>\n"
"    <FontPath>clacon.ttf</FontPath>\n"
"    <FontColor r=\"255\" g=\"255\" b=\"255\" />\n"
"    <FontHeight>20</FontHeight>\n"
"    <LeftOffset>5</LeftOffset>\n"
"    <CommandPrompt>&gt; </CommandPrompt>\n"
"  </Console>\n"
"  <ControlOptions>\n"
"    <UseAlternateControls>false</UseAlternateControls>\n"
"    <TouchScreen>\n"
"      <Enable>false</Enable>\n"
"      <DistanceThreshold>0.05</DistanceThreshold>\n"
"      <TimeThreshold>100</TimeThreshold>\n"
"    </TouchScreen>\n"
"  </ControlOptions>\n"
"  <DebugOptions>\n"
"    <SkipMenu>false</SkipMenu>\n"
"    <SkipMenuToLevel>1</SkipMenuToLevel>\n"
"    <LastImplementedLevel>14</LastImplementedLevel>\n"
"    <SkipBossFightIntro>false</SkipBossFightIntro>\n"
"    <CpuDelay>0</CpuDelay>\n"
"  </DebugOptions>\n"
"</Configuration>\n";

static bool write_text_file(const char* path, const char* text)
{
    FILE* f = fopen(path, "wb");
    size_t len;

    if (f == NULL)
        return false;
    len = strlen(text);
    if (fwrite(text, 1, len, f) != len) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

static const char* s_default_saves =
"<GameSaves>\n"
"    <Level>\n"
"        <LevelNumber>1</LevelNumber>\n"
"        <LevelName>La Roca</LevelName>\n"
"        <Checkpoint>\n"
"            <CheckpointNumber>0</CheckpointNumber>\n"
"            <Score>0</Score>\n"
"            <Health>100</Health>\n"
"            <Lives>3</Lives>\n"
"            <BulletCount>10</BulletCount>\n"
"            <MagicCount>5</MagicCount>\n"
"            <DynamiteCount>3</DynamiteCount>\n"
"        </Checkpoint>\n"
"    </Level>\n"
"</GameSaves>\n";

static void ensure_saves(void)
{
    char path[600];
    snprintf(path, sizeof(path), "%sSAVES.XML", s_content_dir);
    if (!file_readable(path)) {
        write_text_file(path, s_default_saves);
    }
}

static const char* ensure_config(void)
{
    static char path[600];

    snprintf(path, sizeof(path), "%sconfig.xml", s_content_dir);
    if (file_readable(path))
        return path;

    snprintf(path, sizeof(path), "%sconfig_portable.xml", s_content_dir);
    if (file_readable(path))
        return path;

    if (write_text_file(path, s_portable_config))
        return path;

    /* Assets directory may be read-only; fall back to /tmp or working dir. */
    snprintf(path, sizeof(path), "/tmp/openclaw_config.xml");
    if (write_text_file(path, s_portable_config))
        return path;

    snprintf(path, sizeof(path), "config_portable.xml");
    if (write_text_file(path, s_portable_config))
        return path;

    return NULL;
}

/* ------------------------------------------------------------------- input */

static bool button_down(uint16_t id)
{
    if (s_input_state_cb == NULL)
        return false;
    return s_input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id) != 0;
}

static void poll_input(void)
{
    int i;

    if (s_input_poll_cb != NULL)
        s_input_poll_cb();

    for (i = 0; i < BINDING_COUNT; i++) {
        bool down = button_down(s_bindings[i].joypad_id);
        if (down != s_prev_buttons[i]) {
            sdl_compat_push_key(s_bindings[i].key, down);
            s_prev_buttons[i] = down;
        }
    }
}

/* -------------------------------------------------------------- libretro API */

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info* info)
{
    memset(info, 0, sizeof(*info));
    info->library_name     = CORE_NAME;
    info->library_version  = CORE_VERSION;
    info->valid_extensions = CORE_EXTS;
    /* The ROM is only a launcher stub; the assets are read from disk. */
    info->need_fullpath    = true;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = s_render_width;
    info->geometry.base_height  = s_render_height;
    info->geometry.max_width    = MAX_FRAME_WIDTH;
    info->geometry.max_height   = MAX_FRAME_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = FRAME_FPS;
    info->timing.sample_rate    = AUDIO_RATE;
}

void retro_set_environment(retro_environment_t cb)      { s_environ_cb        = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb)  { s_video_cb          = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)    { s_audio_cb          = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { s_audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb)        { s_input_poll_cb     = cb; }
void retro_set_input_state(retro_input_state_t cb)      { s_input_state_cb    = cb; }

void retro_init(void)
{
    std::set_terminate(openclaw_terminate_handler);

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    int i;

    sdl_compat_set_framebuffer(s_framebuffer, s_render_width, s_render_height);
    sdl_compat_reset();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_AUDIO);

    if (s_environ_cb != NULL)
        s_environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

    for (i = 0; i < BINDING_COUNT; i++)
        s_prev_buttons[i] = false;

    s_shutdown_requested = false;
    s_frame_ms_accum = 0;
    s_pacing_ready = 0;
}

void retro_deinit(void)
{
    if (s_loaded && s_app != NULL) {
        s_app->Terminate();
        delete s_app;
        s_app = NULL;
        s_loaded = false;
    }
    SDL_Quit();
}

bool retro_load_game(const struct retro_game_info* info)
{
    const char* config_path;
    char base_path[600];
    char arg0[] = CORE_NAME;
    char* argv[2];

    if (!resolve_content_dir(info)) {
        core_log(RETRO_LOG_ERROR,
                 "[openclaw] CLAW.REZ not found next to '%s' or in the usual roms folders\n",
                 (info && info->path) ? info->path : "(null)");
        return false;
    }
    append_slash(s_content_dir);

    config_path = ensure_config();
    if (config_path == NULL) {
        core_log(RETRO_LOG_ERROR, "[openclaw] could not provide a config.xml\n");
        return false;
    }
    ensure_saves();

    /* Everything the engine opens is relative to this prefix. */
    strncpy(base_path, s_content_dir, sizeof(base_path) - 1);
    base_path[sizeof(base_path) - 1] = '\0';
    sdl_compat_set_base_path(base_path);

    s_app = new ClawGameApp();      /* the constructor publishes itself as g_pApp */

    if (!g_pApp->LoadGameOptions(config_path)) {
        core_log(RETRO_LOG_ERROR, "[openclaw] LoadGameOptions failed for %s\n", config_path);
        delete s_app;
        s_app = NULL;
        return false;
    }

    /* The panel is 320x240 RGB565 with no vsync, so pin the display and leave
     * the touch recognisers out of the picture entirely. The window stays at
     * the game's own 640x480 so the camera viewport is not needlessly small;
     * sdl_compat scales the result down onto the panel. */
    {
        GameOptions* o = g_pApp->GetGameConfig();
        o->windowWidth        = WINDOW_WIDTH;
        o->windowHeight       = WINDOW_HEIGHT;
        o->scale              = 1;
        o->useVerticalSync    = false;
        o->isFullscreen       = false;
        o->isFullscreenDesktop = false;
        o->assetsFolder       = s_content_dir;
    }
    /* No touchscreen on the console: skip the recognisers entirely. */
    g_pApp->GetControlOptions()->touchScreen.enable = false;
    g_pApp->GetGameConfig()->userDirectory = s_content_dir;

    argv[0] = arg0;
    argv[1] = NULL;

    try {
        if (!g_pApp->Initialize(1, argv)) {
            core_log(RETRO_LOG_ERROR, "[openclaw] engine initialization failed\n");
            delete s_app;
            s_app = NULL;
            return false;
        }
    } catch (const std::exception& e) {
        core_log(RETRO_LOG_ERROR, "[openclaw] Initialize exception: %s\n", e.what());
#if defined(SF2000) || defined(__mips__)
        lcd_bsod("Initialize exception:\n%s", e.what());
#endif
        return false;
    } catch (...) {
        core_log(RETRO_LOG_ERROR, "[openclaw] Unknown exception in Initialize\n");
#if defined(SF2000) || defined(__mips__)
        lcd_bsod("Unknown exception in Initialize");
#endif
        return false;
    }

    s_loaded = true;
    core_log(RETRO_LOG_INFO, "[openclaw] running, assets in %s\n", s_content_dir);
    return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num)
{
    (void)type; (void)info; (void)num;
    return false;
}

void retro_unload_game(void)
{
    retro_deinit();
}

extern "C" void libretro_force_present(void)
{
    if (s_video_cb != NULL)
        s_video_cb(s_framebuffer, s_render_width, s_render_height, s_render_width * sizeof(uint16_t));
}

void retro_run(void)
{
    if (!s_loaded || s_app == NULL)
        return;

#if defined(SF2000) || defined(__mips__)
    /* Two-second heap watermark (30 FPS target) for OOM diagnosis. */
    {
        static uint32_t s_mem_tick = 0;

        if (++s_mem_tick >= 60)
        {
            s_mem_tick = 0;
            retro_mem_log("tick");
        }
    }
#endif

    /* Hold the frame back until its slot is due, so the game keeps real time
     * instead of running as fast as the console can execute it. */
    pace_frame();

    poll_input();

    /* The engine derives its timestep from SDL_GetTicks(); drive it from the
     * target frame rate (33ms at 30 FPS). */
    s_frame_ms_accum = (uint32_t)(1000.0 / FRAME_FPS);
    sdl_compat_advance_ticks(s_frame_ms_accum);

    try {
        s_app->StepLoop();
    } catch (const std::exception& e) {
        core_log(RETRO_LOG_ERROR, "[openclaw] StepLoop exception: %s\n", e.what());
#if defined(SF2000) || defined(__mips__)
        lcd_bsod("StepLoop exception:\n%s", e.what());
#endif
    } catch (...) {
        core_log(RETRO_LOG_ERROR, "[openclaw] Unknown exception in StepLoop\n");
#if defined(SF2000) || defined(__mips__)
        lcd_bsod("Unknown exception in StepLoop");
#endif
    }

    if (s_video_cb != NULL)
        s_video_cb(s_framebuffer, s_render_width, s_render_height, s_render_width * sizeof(uint16_t));

    /* Every frame carries a fixed 1/30th of a second of audio. With the frame
     * cap lifted the core produces those samples far faster than the speaker
     * can play them, which would only produce noise and overrun the console's
     * audio buffer, so an uncapped run stays silent. */
    if (s_max_fps > 0) {
        if (s_audio_batch_cb != NULL) {
            sdl_compat_render_audio(s_audio_buffer, AUDIO_FRAMES);
            s_audio_batch_cb(s_audio_buffer, AUDIO_FRAMES);
        } else if (s_audio_cb != NULL) {
            int i;
            for (i = 0; i < AUDIO_FRAMES; i++)
                s_audio_cb(s_audio_buffer[i * 2], s_audio_buffer[i * 2 + 1]);
        }
    }

    /* The menu's "quit" clears the engine's run flag; tell the host so it can
     * return to the launcher instead of spinning on a frozen frame. */
    if (!s_app->IsRunning() && !s_shutdown_requested) {
        s_shutdown_requested = true;
        if (s_environ_cb != NULL)
            s_environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }
}

void retro_reset(void)
{
    /* Reloading the level list is cheaper than rebuilding the whole engine, and
     * the launcher resets are rare. */
    if (s_loaded && s_app != NULL) {
        s_app->Terminate();
        delete s_app;
        s_app = NULL;
        s_loaded = false;
    }
    retro_init();
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    /* Claw is single player and reads the joypad through RETRO_DEVICE_JOYPAD
     * only, so there is nothing to reconfigure here. The frontend still
     * requires the symbol to be present. */
    (void)port;
    (void)device;
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void* retro_get_memory_data(unsigned id)
{
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    (void)id;
    return 0;
}

/* Save states are not implemented: serialising Box2D plus the resource cache
 * would add far more code than it is worth for a game that saves its own
 * progress through SAVES.XML. */
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void* data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void* data, size_t size) { (void)data; (void)size; return false; }

void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
    (void)index; (void)enabled; (void)code;
}
