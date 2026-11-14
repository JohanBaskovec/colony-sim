#include "game/render.h"

#include "core/util.h"

#include <math.h>

static const SDL_Color TERRAIN_COLORS[TERRAIN_COUNT] = {
    [TERRAIN_DEEP_WATER] = {28, 58, 110, 255},
    [TERRAIN_SHALLOW_WATER] = {56, 102, 156, 255},
    [TERRAIN_SAND] = {212, 194, 140, 255},
    [TERRAIN_SOIL] = {122, 94, 64, 255},
    [TERRAIN_RICH_SOIL] = {90, 66, 44, 255},
    [TERRAIN_GRAVEL] = {138, 132, 122, 255},
    [TERRAIN_ROUGH_STONE] = {106, 102, 98, 255},
};

static const SDL_Color ROCK_COLORS[ROCK_COUNT] = {
    [ROCK_NONE] = {0, 0, 0, 0},
    [ROCK_GRANITE] = {92, 92, 100, 255},
    [ROCK_SANDSTONE] = {150, 118, 88, 255},
};

static const SDL_Color GRASS_COLOR = {92, 138, 60, 255};
static const SDL_Color BUSH_COLOR = {64, 112, 48, 255};
static const SDL_Color TREE_COLOR = {34, 88, 40, 255};

/* Small deterministic per-tile brightness variation, so flat areas look less uniform. */
static int tile_variation(int x, int y)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (int)(h % 13u) - 6;
}

static SDL_Color shade(SDL_Color color, int delta)
{
    SDL_Color result = color;
    result.r = (Uint8)clampi(color.r + delta, 0, 255);
    result.g = (Uint8)clampi(color.g + delta, 0, 255);
    result.b = (Uint8)clampi(color.b + delta, 0, 255);
    return result;
}

static SDL_Color mix(SDL_Color a, SDL_Color b, float t)
{
    SDL_Color result;
    result.r = (Uint8)lerpf(a.r, b.r, t);
    result.g = (Uint8)lerpf(a.g, b.g, t);
    result.b = (Uint8)lerpf(a.b, b.b, t);
    result.a = 255;
    return result;
}

static void fill(SDL_Renderer *renderer, SDL_Color color, const SDL_FRect *rect)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, rect);
}

/* Square centered in `tile`, covering `fraction` of its width. */
static SDL_FRect inset(const SDL_FRect *tile, float fraction)
{
    float size = SDL_max(1.0f, floorf(tile->w * fraction));
    SDL_FRect rect = {tile->x + floorf((tile->w - size) * 0.5f),
                      tile->y + floorf((tile->h - size) * 0.5f), size, size};
    return rect;
}

void render_map(SDL_Renderer *renderer, const Map *map, const Camera *camera)
{
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);

    for (int y = visible.y0; y < visible.y1; y++) {
        float top, bottom, unused;
        camera_world_to_screen(camera, 0.0f, (float)y, &unused, &top);
        camera_world_to_screen(camera, 0.0f, (float)(y + 1), &unused, &bottom);

        for (int x = visible.x0; x < visible.x1; x++) {
            float left, right;
            camera_world_to_screen(camera, (float)x, 0.0f, &left, &unused);
            camera_world_to_screen(camera, (float)(x + 1), 0.0f, &right, &unused);
            /* Snap edges to whole pixels so neighboring tiles never leave gaps. */
            SDL_FRect tile = {floorf(left), floorf(top), floorf(right) - floorf(left),
                              floorf(bottom) - floorf(top)};

            size_t index = map_index(map, x, y);
            int variation = tile_variation(x, y);

            if (map->rock[index] != ROCK_NONE) {
                fill(renderer, shade(ROCK_COLORS[map->rock[index]], variation), &tile);
                continue;
            }

            const Plant *plant = &map->plants[index];
            SDL_Color ground = TERRAIN_COLORS[map->terrain[index]];
            if (plant->type == PLANT_GRASS) {
                ground = mix(ground, GRASS_COLOR, 0.35f + 0.5f * plant->growth);
            }
            fill(renderer, shade(ground, variation), &tile);

            if (plant->type == PLANT_BUSH) {
                SDL_FRect rect = inset(&tile, 0.25f + 0.3f * plant->growth);
                fill(renderer, shade(BUSH_COLOR, variation), &rect);
            } else if (plant->type == PLANT_TREE) {
                SDL_FRect rect = inset(&tile, 0.35f + 0.5f * plant->growth);
                fill(renderer, shade(TREE_COLOR, variation), &rect);
            }
        }
    }
}

void render_daylight(SDL_Renderer *renderer, float daylight, float width, float height)
{
    float darkness = 1.0f - clampf(daylight, 0.0f, 1.0f);
    if (darkness <= 0.0f) {
        return;
    }
    SDL_FRect screen = {0.0f, 0.0f, width, height};
    SDL_SetRenderDrawColor(renderer, 8, 12, 40, (Uint8)(darkness * 255.0f));
    SDL_RenderFillRect(renderer, &screen);
}

void render_tile_outline(SDL_Renderer *renderer, const Camera *camera, int x, int y,
                         SDL_Color color)
{
    float left, top, right, bottom;
    camera_world_to_screen(camera, (float)x, (float)y, &left, &top);
    camera_world_to_screen(camera, (float)(x + 1), (float)(y + 1), &right, &bottom);
    SDL_FRect rect = {floorf(left), floorf(top), floorf(right) - floorf(left),
                      floorf(bottom) - floorf(top)};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderRect(renderer, &rect);
}
