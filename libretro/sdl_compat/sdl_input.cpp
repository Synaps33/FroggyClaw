/*
 * sdl_compat - input: a small event queue plus the keyboard state array.
 *
 * The libretro core translates joypad buttons into the SDL key events that
 * OpenClaw's ActorController and menu code already expect, so no game logic
 * needs to change.
 */
#include "sdl_compat.h"

#define EVENT_QUEUE_SIZE 64

static SDL_Event  s_events[EVENT_QUEUE_SIZE];
static int        s_event_head = 0;   /* next slot to read */
static int        s_event_tail = 0;   /* next slot to write */
static int        s_event_count = 0;
static Uint8      s_key_states[SDL_NUM_SCANCODES];
static uint32_t   s_event_timestamp = 0;

void sdl_compat_reset(void)
{
    s_event_head = s_event_tail = s_event_count = 0;
    memset(s_key_states, 0, sizeof(s_key_states));
}

static void queue_event(const SDL_Event* ev)
{
    if (s_event_count >= EVENT_QUEUE_SIZE) {
        /* Drop the oldest: stale input is worse than a dropped key-up. */
        s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
        s_event_count--;
    }
    s_events[s_event_tail] = *ev;
    s_event_timestamp++;
    s_event_tail = (s_event_tail + 1) % EVENT_QUEUE_SIZE;
    s_event_count++;
}

void sdl_compat_push_key(SDL_Keycode key, bool down)
{
    SDL_Event ev;
    SDL_Scancode sc = SDL_GetScancodeFromKey(key);

    memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.timestamp = s_event_timestamp;
    ev.key.windowID = 1;
    ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.repeat = 0;
    ev.key.keysym.scancode = sc;
    ev.key.keysym.sym = key;
    ev.key.keysym.mod = KMOD_NONE;

    if (sc != SDL_SCANCODE_UNKNOWN)
        s_key_states[sc] = down ? 1 : 0;

    queue_event(&ev);
}

void sdl_compat_queue_quit(void)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_QUIT;
    ev.quit.timestamp = s_event_timestamp;
    queue_event(&ev);
}

void SDL_PumpEvents(void)
{
    /* The host pushes input in before calling the frame step; nothing to do. */
}

int SDL_PollEvent(SDL_Event* ev)
{
    if (s_event_count == 0 || ev == NULL)
        return 0;
    *ev = s_events[s_event_head];
    s_event_head = (s_event_head + 1) % EVENT_QUEUE_SIZE;
    s_event_count--;
    return 1;
}

int SDL_PushEvent(SDL_Event* ev)
{
    if (ev == NULL)
        return 0;
    queue_event(ev);
    return 1;
}

const Uint8* SDL_GetKeyboardState(int* numkeys)
{
    if (numkeys)
        *numkeys = SDL_NUM_SCANCODES;
    return s_key_states;
}

SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key)
{
    switch (key) {
    case SDLK_a: return SDL_SCANCODE_A;
    case SDLK_b: return SDL_SCANCODE_B;
    case SDLK_c: return SDL_SCANCODE_C;
    case SDLK_d: return SDL_SCANCODE_D;
    case SDLK_e: return SDL_SCANCODE_E;
    case SDLK_s: return SDL_SCANCODE_S;
    case SDLK_w: return SDL_SCANCODE_W;
    case SDLK_RETURN: return SDL_SCANCODE_RETURN;
    case SDLK_KP_ENTER: return SDL_SCANCODE_KP_ENTER;
    case SDLK_ESCAPE: return SDL_SCANCODE_ESCAPE;
    case SDLK_BACKSPACE: return SDL_SCANCODE_BACKSPACE;
    case SDLK_TAB: return SDL_SCANCODE_TAB;
    case SDLK_SPACE: return SDL_SCANCODE_SPACE;
    case SDLK_UP: return SDL_SCANCODE_UP;
    case SDLK_DOWN: return SDL_SCANCODE_DOWN;
    case SDLK_LEFT: return SDL_SCANCODE_LEFT;
    case SDLK_RIGHT: return SDL_SCANCODE_RIGHT;
    case SDLK_PAGEUP: return SDL_SCANCODE_PAGEUP;
    case SDLK_PAGEDOWN: return SDL_SCANCODE_PAGEDOWN;
    case SDLK_LCTRL: return SDL_SCANCODE_LCTRL;
    case SDLK_LSHIFT: return SDL_SCANCODE_LSHIFT;
    case SDLK_LALT: return SDL_SCANCODE_LALT;
    case SDLK_RSHIFT: return SDL_SCANCODE_RSHIFT;
    case '0': return SDL_SCANCODE_0;
    case '1': return SDL_SCANCODE_1;
    case '2': return SDL_SCANCODE_2;
    case '3': return SDL_SCANCODE_3;
    case '4': return SDL_SCANCODE_4;
    case '5': return SDL_SCANCODE_5;
    case '6': return SDL_SCANCODE_6;
    case '7': return SDL_SCANCODE_7;
    case '8': return SDL_SCANCODE_8;
    case '9': return SDL_SCANCODE_9;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode sc)
{
    switch (sc) {
    case SDL_SCANCODE_A: return SDLK_a;
    case SDL_SCANCODE_B: return 'b';
    case SDL_SCANCODE_C: return 'c';
    case SDL_SCANCODE_D: return SDLK_d;
    case SDL_SCANCODE_E: return SDLK_e;
    case SDL_SCANCODE_S: return SDLK_s;
    case SDL_SCANCODE_W: return SDLK_w;
    case SDL_SCANCODE_RETURN: return SDLK_RETURN;
    case SDL_SCANCODE_KP_ENTER: return SDLK_KP_ENTER;
    case SDL_SCANCODE_ESCAPE: return SDLK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return SDLK_BACKSPACE;
    case SDL_SCANCODE_TAB: return SDLK_TAB;
    case SDL_SCANCODE_SPACE: return SDLK_SPACE;
    case SDL_SCANCODE_UP: return SDLK_UP;
    case SDL_SCANCODE_DOWN: return SDLK_DOWN;
    case SDL_SCANCODE_LEFT: return SDLK_LEFT;
    case SDL_SCANCODE_RIGHT: return SDLK_RIGHT;
    case SDL_SCANCODE_PAGEUP: return SDLK_PAGEUP;
    case SDL_SCANCODE_PAGEDOWN: return SDLK_PAGEDOWN;
    case SDL_SCANCODE_LCTRL: return SDLK_LCTRL;
    case SDL_SCANCODE_LSHIFT: return SDLK_LSHIFT;
    case SDL_SCANCODE_LALT: return SDLK_LALT;
    case SDL_SCANCODE_RSHIFT: return SDLK_RSHIFT;
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_1: return '1';
    case SDL_SCANCODE_2: return '2';
    case SDL_SCANCODE_3: return '3';
    case SDL_SCANCODE_4: return '4';
    case SDL_SCANCODE_5: return '5';
    case SDL_SCANCODE_6: return '6';
    case SDL_SCANCODE_7: return '7';
    case SDL_SCANCODE_8: return '8';
    case SDL_SCANCODE_9: return '9';
    default: return SDLK_UNKNOWN;
    }
}
