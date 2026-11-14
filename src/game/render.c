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

/* Body colors, picked per colonist slot. */
static const SDL_Color COLONIST_COLORS[] = {
    {230, 120, 70, 255}, {90, 160, 230, 255}, {240, 200, 80, 255}, {200, 110, 210, 255},
    {110, 210, 170, 255}, {235, 95, 120, 255}, {170, 190, 90, 255}, {150, 140, 240, 255},
};
static const SDL_Color COLONIST_OUTLINE = {20, 20, 24, 255};
static const SDL_Color SELECTION_COLOR = {255, 255, 255, 255};
static const SDL_Color PATH_COLOR = {255, 255, 255, 140};

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

/* ---- Colonists ----------------------------------------------------------- */

static SDL_FColor to_fcolor(SDL_Color color)
{
    return (SDL_FColor){color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
}

static void fill_circle(SDL_Renderer *renderer, float cx, float cy, float radius, SDL_Color color)
{
    enum { SEGMENTS = 24 };
    SDL_Vertex vertices[SEGMENTS + 1];
    int indices[SEGMENTS * 3];
    SDL_FColor fcolor = to_fcolor(color);

    vertices[0] = (SDL_Vertex){{cx, cy}, fcolor, {0.0f, 0.0f}};
    for (int i = 0; i < SEGMENTS; i++) {
        float angle = (float)i * (2.0f * SDL_PI_F / SEGMENTS);
        vertices[i + 1] = (SDL_Vertex){{cx + radius * SDL_cosf(angle), cy + radius * SDL_sinf(angle)},
                                       fcolor,
                                       {0.0f, 0.0f}};
        indices[i * 3] = 0;
        indices[i * 3 + 1] = i + 1;
        indices[i * 3 + 2] = (i + 1) % SEGMENTS + 1;
    }
    SDL_RenderGeometry(renderer, NULL, vertices, SEGMENTS + 1, indices, SEGMENTS * 3);
}

static bool is_selected(EntityId id, const EntityId *selected, int selected_count)
{
    for (int i = 0; i < selected_count; i++) {
        if (entity_id_equal(id, selected[i])) {
            return true;
        }
    }
    return false;
}

static void render_path(SDL_Renderer *renderer, const Camera *camera, const Colonist *colonist)
{
    enum { MAX_POINTS = 256 };
    SDL_FPoint points[MAX_POINTS];
    int count = 0;

    float x, y;
    colonist_position(colonist, &x, &y);
    camera_world_to_screen(camera, x, y, &points[count].x, &points[count].y);
    count++;
    for (int i = colonist->path_index; i < colonist->path.count && count < MAX_POINTS; i++) {
        TilePos step = colonist->path.steps[i];
        camera_world_to_screen(camera, step.x + 0.5f, step.y + 0.5f, &points[count].x,
                               &points[count].y);
        count++;
    }
    if (count > 1) {
        SDL_SetRenderDrawColor(renderer, PATH_COLOR.r, PATH_COLOR.g, PATH_COLOR.b, PATH_COLOR.a);
        SDL_RenderLines(renderer, points, count);
    }
}

void render_colonists(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                      const EntityId *selected, int selected_count)
{
    float radius = SDL_max(3.0f, camera->zoom * 0.38f);

    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id) || !is_selected(id, selected, selected_count)) {
            continue;
        }
        render_path(renderer, camera, &game->colonists[slot]);
    }

    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id)) {
            continue;
        }
        const Colonist *colonist = &game->colonists[slot];
        float wx, wy, sx, sy;
        colonist_position(colonist, &wx, &wy);
        camera_world_to_screen(camera, wx, wy, &sx, &sy);

        if (is_selected(id, selected, selected_count)) {
            fill_circle(renderer, sx, sy, radius + 3.0f, SELECTION_COLOR);
        }
        fill_circle(renderer, sx, sy, radius + 1.0f, COLONIST_OUTLINE);
        SDL_Color body = COLONIST_COLORS[slot % SDL_arraysize(COLONIST_COLORS)];
        if (colonist->task == TASK_SLEEP) {
            body = shade(body, -70); /* Sleeping colonists look dimmer. */
        }
        fill_circle(renderer, sx, sy, radius, body);
    }
}

void render_selection_box(SDL_Renderer *renderer, float x0, float y0, float x1, float y1)
{
    SDL_FRect rect = {SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0)};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 40);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
    SDL_RenderRect(renderer, &rect);
}
