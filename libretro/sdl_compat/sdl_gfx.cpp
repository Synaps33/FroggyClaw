/*
 * sdl_compat - SDL2_gfx primitives.
 *
 * Only used by PhysicsDebugDrawer, which is behind a debug option, but the
 * linker still needs the symbols. These forward to the SDL_Renderer fills and
 * lines already implemented in the video layer.
 */
#include "sdl_compat.h"

int boxRGBA(SDL_Renderer* r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2,
            Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    SDL_Rect rect;
    int x0 = (x1 < x2) ? x1 : x2;
    int y0 = (y1 < y2) ? y1 : y2;

    rect.x = x0;
    rect.y = y0;
    rect.w = (x1 > x2 ? x1 - x2 : x2 - x1) + 1;
    rect.h = (y1 > y2 ? y1 - y2 : y2 - y1) + 1;

    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    return SDL_RenderDrawRect(r, &rect);
}

int lineRGBA(SDL_Renderer* r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2,
             Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    return SDL_RenderDrawLine(r, x1, y1, x2, y2);
}

int circleRGBA(SDL_Renderer* r, Sint16 x, Sint16 y, Sint16 rad,
               Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    int steps = (rad > 0) ? rad * 8 : 8;
    int i;
    double pi = 3.14159265358979323846;

    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    for (i = 0; i < steps; i++) {
        double a = (double)i * (2.0 * pi) / (double)steps;
        int px = x + (int)(rad * __builtin_cos(a) + 0.5);
        int py = y + (int)(rad * __builtin_sin(a) + 0.5);
        SDL_RenderDrawPoint(r, px, py);
    }
    return 0;
}

int filledCircleRGBA(SDL_Renderer* r, Sint16 x, Sint16 y, Sint16 rad,
                     Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    SDL_Rect rect;

    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    rect.x = x - rad;
    rect.y = y - rad;
    rect.w = rad * 2 + 1;
    rect.h = rad * 2 + 1;

    /* Scanline fill: cheap and avoids pulling in a rasteriser. */
    {
        int row;
        for (row = 0; row < rect.h; row++) {
            int dy = row - rad;
            int span = (int)(0.5 + (double)rad * (1.0 - (double)(dy * dy) /
                                        (double)(rad * rad + 1)));
            SDL_Rect line;
            line.x = rect.x + rad - span;
            line.y = rect.y + row;
            line.w = span * 2 + 1;
            line.h = 1;
            SDL_RenderFillRect(r, &line);
        }
    }
    return 0;
}

int pixelRGBA(SDL_Renderer* r, Sint16 x, Sint16 y,
              Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    return SDL_RenderDrawPoint(r, x, y);
}

/* Even-odd scanline fill of a polygon given as flat X/Y arrays. */
static int fill_polygon(SDL_Renderer* r, const Sint16* vx, const Sint16* vy,
                        int n, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    int min_y = 0x7FFF, max_y = -0x7FFF, i, y;

    if (n < 3)
        return -1;

    for (i = 0; i < n; i++) {
        if (vy[i] < min_y) min_y = vy[i];
        if (vy[i] > max_y) max_y = vy[i];
    }

    SDL_SetRenderDrawColor(r, red, green, blue, alpha);

    for (y = min_y; y <= max_y; y++) {
        Sint16 xs[64];
        int count = 0;

        for (i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int y0 = vy[i], y1 = vy[j];

            /* Skip horizontal edges; include vertices exactly on the scanline. */
            if (y0 == y1)
                continue;
            if ((y < y0 && y < y1) || (y >= y0 && y >= y1))
                continue;

            xs[count++] = (Sint16)(vx[i] + (y - y0) * (vx[j] - vx[i]) / (y1 - y0));
            if (count >= (int)(sizeof(xs) / sizeof(xs[0])) - 1)
                break;
        }
        if (count < 2)
            continue;

        /* Insertion sort: the span counts here are tiny. */
        for (i = 1; i < count; i++) {
            Sint16 key = xs[i];
            int k = i - 1;
            while (k >= 0 && xs[k] > key) { xs[k + 1] = xs[k]; k--; }
            xs[k + 1] = key;
        }

        for (i = 0; i + 1 < count; i += 2) {
            SDL_Rect line;
            line.x = xs[i];
            line.y = y;
            line.w = xs[i + 1] - xs[i] + 1;
            line.h = 1;
            SDL_RenderFillRect(r, &line);
        }
    }
    return 0;
}

int polygonRGBA(SDL_Renderer* r, const Sint16* vx, const Sint16* vy, int n,
                Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    int i;

    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        SDL_RenderDrawLine(r, vx[i], vy[i], vx[j], vy[j]);
    }
    return 0;
}

int filledPolygonRGBA(SDL_Renderer* r, const Sint16* vx, const Sint16* vy, int n,
                      Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    return fill_polygon(r, vx, vy, n, red, green, blue, alpha);
}
