#include "game/render.h"

#include "core/util.h"

#include <math.h>
#include <stdlib.h>

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
static const SDL_Color TREE_COLOR = {34, 88, 40, 255};

/* Tints of the (light gray) hair and beard sprites. */
static const SDL_Color HAIR_COLORS[HAIR_COLOR_COUNT] = {
    [HAIR_COLOR_BLACK] = {58, 46, 40, 255},     [HAIR_COLOR_DARK_BROWN] = {100, 66, 44, 255},
    [HAIR_COLOR_BROWN] = {146, 102, 64, 255},   [HAIR_COLOR_AUBURN] = {172, 86, 48, 255},
    [HAIR_COLOR_BLOND] = {226, 190, 118, 255},  [HAIR_COLOR_GRAY] = {188, 184, 178, 255},
};
/* Designation marks; index DESIGNATION_NONE is the color of the cancel tool. */
static const SDL_Color DESIGNATION_COLORS[DESIGNATION_COUNT] = {
    [DESIGNATION_NONE] = {230, 70, 60, 255},
    [DESIGNATION_MINE] = {250, 140, 60, 255},
    [DESIGNATION_DECONSTRUCT] = {240, 200, 70, 255},
};
static const SDL_Color STOCKPILE_COLOR = {90, 170, 230, 255};
static const SDL_Color STOCKPILE_REMOVE_COLOR = {230, 70, 60, 255};
static const SDL_Color GATHER_ZONE_COLOR = {225, 170, 90, 255};
static const SDL_Color WOOD_COLOR = {150, 98, 52, 255};
static const SDL_Color STONE_COLOR = {176, 174, 168, 255};
static const SDL_Color BERRY_COLOR = {150, 40, 90, 255};
static const SDL_Color BRANCH_COLOR = {122, 86, 50, 255};
static const SDL_Color TWIG_COLOR = {168, 128, 82, 255};
static const SDL_Color FRESH_GRASS_COLOR = {92, 168, 64, 255};
static const SDL_Color DRY_GRASS_COLOR = {206, 184, 104, 255};
static const SDL_Color FLINT_COLOR = {72, 78, 96, 255};
static const SDL_Color FLAKE_COLOR = {128, 136, 156, 255};
static const SDL_Color SPEAR_COLOR = {176, 134, 84, 255};
static const SDL_Color MAT_COLOR = {164, 132, 82, 255};
static const SDL_Color ANVIL_COLOR = {124, 122, 118, 255};
static const SDL_Color BLUEPRINT_COLOR = {120, 190, 250, 255};
static const SDL_Color BLUEPRINT_REFUSED_COLOR = {230, 70, 60, 255};
static const SDL_Color ITEM_OUTLINE = {28, 24, 20, 255};
static const SDL_Color PROGRESS_BACKGROUND = {20, 20, 24, 220};
static const SDL_Color PROGRESS_COLOR = {120, 220, 110, 255};
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
static SDL_FColor to_fcolor(SDL_Color color)
{
    const float scale = 1.0f / 255.0f;
    return (SDL_FColor){(float)color.r * scale, (float)color.g * scale, (float)color.b * scale,
                        (float)color.a * scale};
}

/* ---- Quad batch ---------------------------------------------------------- */

/*
 * The map is tens of thousands of small rectangles when zoomed out, each with
 * its own shade. Drawn one by one they cost a draw command each; instead they
 * are accumulated as triangles and sent with a few SDL_RenderGeometry() calls.
 * Quads are drawn in the order they were added, like separate fills would be.
 */
#define BATCH_MAX_QUADS 8192

static SDL_Vertex *batch_vertices;
static int *batch_indices;
static int batch_count; /* Quads waiting to be drawn. */

static bool batch_ready(void)
{
    if (batch_vertices != NULL) {
        return true;
    }
    batch_vertices = SDL_malloc(sizeof(*batch_vertices) * 4 * BATCH_MAX_QUADS);
    batch_indices = SDL_malloc(sizeof(*batch_indices) * 6 * BATCH_MAX_QUADS);
    if (batch_vertices == NULL || batch_indices == NULL) {
        render_shutdown();
        return false;
    }
    /* Two triangles per quad; the pattern never changes. */
    for (int quad = 0; quad < BATCH_MAX_QUADS; quad++) {
        static const int CORNERS[6] = {0, 1, 2, 2, 3, 0};
        for (int i = 0; i < 6; i++) {
            batch_indices[quad * 6 + i] = quad * 4 + CORNERS[i];
        }
    }
    return true;
}

static void batch_flush(SDL_Renderer *renderer)
{
    if (batch_count > 0) {
        SDL_RenderGeometry(renderer, NULL, batch_vertices, batch_count * 4, batch_indices,
                           batch_count * 6);
        batch_count = 0;
    }
}

/* Like fill(), but drawn at the next batch_flush() (or when the batch is full). */
static void batch_quad(SDL_Renderer *renderer, SDL_Color color, const SDL_FRect *rect)
{
    if (!batch_ready()) {
        fill(renderer, color, rect); /* Out of memory: slower, but still drawn. */
        return;
    }
    if (batch_count == BATCH_MAX_QUADS) {
        batch_flush(renderer);
    }
    SDL_Vertex *vertex = &batch_vertices[batch_count * 4];
    SDL_FColor fcolor = to_fcolor(color);
    float left = rect->x, top = rect->y, right = rect->x + rect->w, bottom = rect->y + rect->h;
    vertex[0] = (SDL_Vertex){{left, top}, fcolor, {0.0f, 0.0f}};
    vertex[1] = (SDL_Vertex){{right, top}, fcolor, {0.0f, 0.0f}};
    vertex[2] = (SDL_Vertex){{right, bottom}, fcolor, {0.0f, 0.0f}};
    vertex[3] = (SDL_Vertex){{left, bottom}, fcolor, {0.0f, 0.0f}};
    batch_count++;
}

void render_shutdown(void)
{
    SDL_free(batch_vertices);
    SDL_free(batch_indices);
    batch_vertices = NULL;
    batch_indices = NULL;
    batch_count = 0;
}

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
        float top, bottom, left, right, unused;
        camera_world_to_screen(camera, 0.0f, (float)y, &unused, &top);
        camera_world_to_screen(camera, 0.0f, (float)(y + 1), &unused, &bottom);
        camera_world_to_screen(camera, (float)visible.x0, 0.0f, &right, &unused);

        for (int x = visible.x0; x < visible.x1; x++) {
            left = right; /* The right edge of the previous tile. */
            camera_world_to_screen(camera, (float)(x + 1), 0.0f, &right, &unused);
            /* Snap edges to whole pixels so neighboring tiles never leave gaps. */
            SDL_FRect tile = {floorf(left), floorf(top), floorf(right) - floorf(left),
                              floorf(bottom) - floorf(top)};

            size_t index = map_index(map, x, y);
            int variation = tile_variation(x, y);

            if (map->rock[index] != ROCK_NONE) {
                batch_quad(renderer, shade(ROCK_COLORS[map->rock[index]], variation), &tile);
                continue;
            }

            const Plant *plant = &map->plants[index];
            SDL_Color ground = TERRAIN_COLORS[map->terrain[index]];
            if (plant->type == PLANT_GRASS) {
                ground = mix(ground, GRASS_COLOR, 0.35f + 0.5f * plant->growth);
            }
            batch_quad(renderer, shade(ground, variation), &tile);

        }
    }
    batch_flush(renderer);
}

/* Bits of a deterministic hash of a tile and a number (spot positions). */
static uint32_t tile_hash(int x, int y, int n)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)n * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

/* Dirt: dark spots scattered on the tile, more and darker as it gets dirtier;
 * each spot is two crossed rectangles, which reads as a blob. */
#define DIRT_SPOTS_MAX 8

void render_dirt(SDL_Renderer *renderer, const Dirt *dirt, const Camera *camera)
{
    if (dirt->dirty_count == 0) {
        return;
    }
    TileRect visible = camera_visible_tiles(camera, dirt->width, dirt->height);
    float pixels = camera->zoom;
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            int level = dirt_at(dirt, x, y);
            if (level == 0) {
                continue;
            }
            float left, top;
            camera_world_to_screen(camera, (float)x, (float)y, &left, &top);
            int spots = 1 + level * (DIRT_SPOTS_MAX - 1) / DIRT_MAX;
            SDL_Color color = {58, 42, 26, (Uint8)(70 + level)};
            for (int i = 0; i < spots; i++) {
                uint32_t h = tile_hash(x, y, i);
                float size = pixels * (0.12f + 0.12f * (float)(h & 0xffu) / 255.0f);
                float cx = left + pixels * (0.15f + 0.7f * (float)((h >> 8) & 0xffu) / 255.0f);
                float cy = top + pixels * (0.15f + 0.7f * (float)((h >> 16) & 0xffu) / 255.0f);
                SDL_FRect wide = {cx - size * 0.5f, cy - size * 0.3f, size, size * 0.6f};
                SDL_FRect tall = {cx - size * 0.3f, cy - size * 0.5f, size * 0.6f, size};
                batch_quad(renderer, color, &wide);
                batch_quad(renderer, color, &tall);
            }
        }
    }
    batch_flush(renderer);
}

/* Screen rectangle of a tile, snapped to whole pixels like render_map(). */
static SDL_FRect tile_rect(const Camera *camera, int x, int y)
{
    float left, top, right, bottom;
    camera_world_to_screen(camera, (float)x, (float)y, &left, &top);
    camera_world_to_screen(camera, (float)(x + 1), (float)(y + 1), &right, &bottom);
    SDL_FRect rect = {floorf(left), floorf(top), floorf(right) - floorf(left),
                      floorf(bottom) - floorf(top)};
    return rect;
}

/* Rectangle outline `thickness` pixels wide, drawn inwards. */
static void outline(SDL_Renderer *renderer, SDL_FRect rect, float thickness, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i < (int)thickness && rect.w > 2.0f && rect.h > 2.0f; i++) {
        SDL_RenderRect(renderer, &rect);
        rect.x += 1.0f;
        rect.y += 1.0f;
        rect.w -= 2.0f;
        rect.h -= 2.0f;
    }
}

/* Filled rectangle with a dark one-pixel border (batched: see batch_flush()). */
static void fill_outlined(SDL_Renderer *renderer, SDL_FRect rect, SDL_Color color)
{
    batch_quad(renderer, ITEM_OUTLINE, &rect);
    if (rect.w > 2.0f && rect.h > 2.0f) {
        SDL_FRect inner = {rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f};
        batch_quad(renderer, color, &inner);
    }
}

/* Adds the shapes of a stack to the batch: the caller flushes it. */
static void render_item_stack(SDL_Renderer *renderer, const SDL_FRect *tile, ItemType type)
{
    float w = tile->w, h = tile->h;
    switch (type) {
    case ITEM_WOOD: {
        /* Three logs. */
        float log_h = SDL_max(1.0f, floorf(h * 0.16f));
        for (int i = 0; i < 3; i++) {
            float inset_x = (i == 1) ? 0.18f : 0.24f;
            SDL_FRect log = {tile->x + floorf(w * inset_x),
                             tile->y + floorf(h * (0.3f + 0.17f * (float)i)),
                             floorf(w * (1.0f - 2.0f * inset_x)), log_h};
            fill_outlined(renderer, log, shade(WOOD_COLOR, i * 12));
        }
        break;
    }
    case ITEM_STONE_BLOCK: {
        /* Two squared blocks. */
        float size = SDL_max(1.0f, floorf(w * 0.34f));
        for (int i = 0; i < 2; i++) {
            SDL_FRect block = {tile->x + floorf(w * (0.14f + 0.38f * (float)i)),
                               tile->y + floorf(h * 0.44f), size, size};
            fill_outlined(renderer, block, shade(STONE_COLOR, -i * 14));
        }
        break;
    }
    case ITEM_COBBLE:
    case ITEM_FLINT: {
        /* A small heap of pebbles, or dark flint nodules. */
        SDL_Color color = type == ITEM_FLINT ? FLINT_COLOR : STONE_COLOR;
        /* x, y, size, relative to the tile. */
        static const float STONES[4][3] = {{0.24f, 0.56f, 0.2f},
                                           {0.48f, 0.54f, 0.22f},
                                           {0.36f, 0.36f, 0.2f},
                                           {0.62f, 0.38f, 0.16f}};
        for (int i = 0; i < 4; i++) {
            float size = SDL_max(1.0f, floorf(w * STONES[i][2]));
            SDL_FRect stone = {tile->x + floorf(w * STONES[i][0]),
                               tile->y + floorf(h * STONES[i][1]), size, size};
            fill_outlined(renderer, stone, shade(color, -i * 10));
        }
        break;
    }
    case ITEM_CHOPPER: {
        /* One fist-sized stone with a knapped, darker edge. */
        float size = SDL_max(2.0f, floorf(w * 0.42f));
        SDL_FRect stone = {tile->x + floorf((w - size) * 0.5f), tile->y + floorf(h * 0.4f), size,
                           floorf(size * 0.85f)};
        fill_outlined(renderer, stone, shade(STONE_COLOR, 6));
        if (size >= 6.0f) {
            SDL_FRect edge = {stone.x + 1.0f, stone.y + 1.0f, floorf(size * 0.3f),
                              stone.h - 2.0f};
            batch_quad(renderer, shade(STONE_COLOR, -40), &edge);
        }
        break;
    }
    case ITEM_FLAKE: {
        /* A few thin slivers. */
        float thickness = SDL_max(1.0f, floorf(h * 0.08f));
        for (int i = 0; i < 3; i++) {
            SDL_FRect sliver = {tile->x + floorf(w * (0.24f + 0.12f * (float)(i % 2))),
                                tile->y + floorf(h * (0.38f + 0.14f * (float)i)),
                                floorf(w * 0.34f), thickness};
            fill_outlined(renderer, sliver, shade(FLAKE_COLOR, i * 10));
        }
        break;
    }
    case ITEM_HAND_AXE: {
        /* A teardrop: rows narrowing towards the tip. */
        static const float ROWS[4][2] = {
            {0.42f, 0.16f}, {0.34f, 0.3f}, {0.3f, 0.4f}, {0.36f, 0.28f}};
        float row_h = SDL_max(1.0f, floorf(h * 0.12f));
        for (int i = 0; i < 4; i++) {
            SDL_FRect row = {tile->x + floorf(w * ROWS[i][0]),
                             tile->y + floorf(h * 0.3f) + row_h * (float)i,
                             SDL_max(1.0f, floorf(w * ROWS[i][1])), row_h};
            batch_quad(renderer, shade(FLINT_COLOR, i * 8), &row);
        }
        break;
    }
    case ITEM_THROWING_SPEAR:
    case ITEM_THRUSTING_SPEAR: {
        /* A long shaft with a sharpened tip; the thrusting spear is thicker. */
        bool thrusting = type == ITEM_THRUSTING_SPEAR;
        float thickness = SDL_max(1.0f, floorf(h * (thrusting ? 0.12f : 0.08f)));
        SDL_FRect shaft = {tile->x + floorf(w * 0.08f), tile->y + floorf(h * 0.5f),
                           floorf(w * 0.72f), thickness};
        fill_outlined(renderer, shaft, SPEAR_COLOR);
        SDL_FRect tip = {shaft.x + shaft.w, shaft.y, floorf(w * 0.12f), thickness};
        fill_outlined(renderer, tip, shade(SPEAR_COLOR, -50));
        break;
    }
    case ITEM_BRANCHES:
    case ITEM_TWIGS: {
        /* Long thin branches, or a pile of short twigs. */
        bool twigs = type == ITEM_TWIGS;
        int count = twigs ? 4 : 3;
        float length = twigs ? 0.36f : 0.68f;
        float thickness = SDL_max(1.0f, floorf(h * (twigs ? 0.08f : 0.11f)));
        for (int i = 0; i < count; i++) {
            float x = twigs ? 0.2f + 0.24f * (float)(i % 2) : 0.16f + 0.04f * (float)(i % 2);
            SDL_FRect stick = {tile->x + floorf(w * x),
                               tile->y + floorf(h * (0.3f + 0.15f * (float)i)),
                               floorf(w * length), thickness};
            fill_outlined(renderer, stick, shade(twigs ? TWIG_COLOR : BRANCH_COLOR, i * 10));
        }
        break;
    }
    case ITEM_FRESH_GRASS:
    case ITEM_GRASS: {
        /* A sheaf of upright blades. */
        SDL_Color color = type == ITEM_GRASS ? DRY_GRASS_COLOR : FRESH_GRASS_COLOR;
        float blade_w = SDL_max(1.0f, floorf(w * 0.1f));
        for (int i = 0; i < 5; i++) {
            float top = (i % 2 == 0) ? 0.26f : 0.34f;
            SDL_FRect blade = {tile->x + floorf(w * (0.24f + 0.1f * (float)i)),
                               tile->y + floorf(h * top), blade_w,
                               floorf(h * (0.76f - top))};
            fill_outlined(renderer, blade, shade(color, (i % 3) * 10));
        }
        break;
    }
    case ITEM_BERRIES: {
        /* A handful of berries. */
        static const float BERRIES[5][2] = {
            {0.3f, 0.5f}, {0.5f, 0.45f}, {0.42f, 0.62f}, {0.58f, 0.6f}, {0.38f, 0.36f}};
        float size = SDL_max(1.0f, floorf(w * 0.16f));
        for (int i = 0; i < 5; i++) {
            SDL_FRect berry = {tile->x + floorf(w * BERRIES[i][0]),
                               tile->y + floorf(h * BERRIES[i][1]), size, size};
            fill_outlined(renderer, berry, shade(BERRY_COLOR, i * 8));
        }
        break;
    }
    default: break;
    }
}

void render_items(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const Items *items = &game->items;
    TileRect visible = camera_visible_tiles(camera, items->width, items->height);
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            ItemStack stack = items_get(items, x, y);
            if (stack.type != ITEM_NONE) {
                SDL_FRect tile = tile_rect(camera, x, y);
                render_item_stack(renderer, &tile, (ItemType)stack.type);
            }
        }
    }
    batch_flush(renderer);
}

void render_designations(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const Designations *designations = &game->designations;
    if (designations->count == 0) {
        return;
    }
    const Map *map = &game->map;
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    float thickness = camera->zoom >= 24.0f ? 2.0f : 1.0f;

    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            DesignationType type = designation_get(designations, x, y);
            if (type == DESIGNATION_NONE) {
                continue;
            }
            SDL_FRect tile = tile_rect(camera, x, y);
            SDL_FRect mark = inset(&tile, 0.8f);
            outline(renderer, mark, thickness, DESIGNATION_COLORS[type]);

            float progress = designation_progress_fraction(designations, map, x, y);
            if (progress > 0.0f && tile.w >= 6.0f) {
                float height = SDL_max(2.0f, floorf(tile.h * 0.12f));
                SDL_FRect bar = {tile.x + 1.0f, tile.y + tile.h - height - 1.0f, tile.w - 2.0f,
                                 height};
                fill(renderer, PROGRESS_BACKGROUND, &bar);
                bar.w = floorf(bar.w * progress);
                fill(renderer, PROGRESS_COLOR, &bar);
            }
        }
    }
}

void render_designation_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0,
                             int x1, int y1, DesignationType type)
{
    SDL_FRect a = tile_rect(camera, SDL_min(x0, x1), SDL_min(y0, y1));
    SDL_FRect b = tile_rect(camera, SDL_max(x0, x1), SDL_max(y0, y1));
    SDL_FRect area = {a.x, a.y, b.x + b.w - a.x, b.y + b.h - a.y};
    SDL_Color color = DESIGNATION_COLORS[(unsigned)type < DESIGNATION_COUNT ? type : 0];
    SDL_Color translucent = color;
    translucent.a = 50;
    fill(renderer, translucent, &area);
    outline(renderer, area, 2.0f, color);
}

/* Bar at the bottom of a tile, filled to `fraction` (nothing at 0). */
static void progress_bar(SDL_Renderer *renderer, const SDL_FRect *tile, float fraction)
{
    if (fraction <= 0.0f || tile->w < 6.0f) {
        return;
    }
    float height = SDL_max(2.0f, floorf(SDL_min(tile->w, tile->h) * 0.12f));
    SDL_FRect bar = {tile->x + 1.0f, tile->y + tile->h - height - 1.0f, tile->w - 2.0f, height};
    fill(renderer, PROGRESS_BACKGROUND, &bar);
    bar.w = floorf(bar.w * fraction);
    fill(renderer, PROGRESS_COLOR, &bar);
}

/* Zone id of a tile in a layer of zone index + 1 (0 outside), or -1. */
static int zone_id(const uint8_t *layer, int width, int height, int x, int y)
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return -1;
    }
    return (int)layer[(size_t)y * (size_t)width + (size_t)x] - 1;
}

/* Zones as a translucent overlay with their borders; the selected one
 * (index, or -1) is brighter. */
static void render_zone_layer(SDL_Renderer *renderer, const Camera *camera, const uint8_t *layer,
                              int width, int height, int selected, SDL_Color color)
{
    TileRect visible = camera_visible_tiles(camera, width, height);
    SDL_Color normal = color, highlight = color;
    normal.a = 45;
    highlight.a = 90;
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            int index = zone_id(layer, width, height, x, y);
            if (index < 0) {
                continue;
            }
            SDL_FRect tile = tile_rect(camera, x, y);
            fill(renderer, index == selected ? highlight : normal, &tile);
            /* Zone border: edges of the tile that face another zone or none. */
            SDL_Color edge = color;
            edge.a = index == selected ? 255 : 140;
            SDL_SetRenderDrawColor(renderer, edge.r, edge.g, edge.b, edge.a);
            float l = tile.x, t = tile.y, r = tile.x + tile.w - 1.0f, b = tile.y + tile.h - 1.0f;
            if (zone_id(layer, width, height, x - 1, y) != index) {
                SDL_RenderLine(renderer, l, t, l, b);
            }
            if (zone_id(layer, width, height, x + 1, y) != index) {
                SDL_RenderLine(renderer, r, t, r, b);
            }
            if (zone_id(layer, width, height, x, y - 1) != index) {
                SDL_RenderLine(renderer, l, t, r, t);
            }
            if (zone_id(layer, width, height, x, y + 1) != index) {
                SDL_RenderLine(renderer, l, b, r, b);
            }
        }
    }
}

/* Rectangle of tiles being drawn with a zone tool. */
static void render_zone_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                             int y1, SDL_Color color)
{
    SDL_FRect a = tile_rect(camera, SDL_min(x0, x1), SDL_min(y0, y1));
    SDL_FRect b = tile_rect(camera, SDL_max(x0, x1), SDL_max(y0, y1));
    SDL_FRect area = {a.x, a.y, b.x + b.w - a.x, b.y + b.h - a.y};
    SDL_Color translucent = color;
    translucent.a = 50;
    fill(renderer, translucent, &area);
    outline(renderer, area, 2.0f, color);
}

void render_stockpiles(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                       int selected)
{
    const Stockpiles *stockpiles = &game->stockpiles;
    render_zone_layer(renderer, camera, stockpiles->zone, stockpiles->width, stockpiles->height,
                      selected, STOCKPILE_COLOR);
}

void render_stockpile_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                           int y1, bool remove)
{
    render_zone_area(renderer, camera, x0, y0, x1, y1,
                     remove ? STOCKPILE_REMOVE_COLOR : STOCKPILE_COLOR);
}

void render_gather_zones(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                         int selected)
{
    const GatherZones *zones = &game->gather_zones;
    if (zones->used_count == 0) {
        return;
    }
    render_zone_layer(renderer, camera, zones->zone, zones->width, zones->height, selected,
                      GATHER_ZONE_COLOR);
    /* Progress of the job under way on each tile. */
    TileRect visible = camera_visible_tiles(camera, zones->width, zones->height);
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            float progress = gather_zone_progress_fraction(zones, &game->map, x, y);
            if (progress > 0.0f) {
                SDL_FRect tile = tile_rect(camera, x, y);
                progress_bar(renderer, &tile, progress);
            }
        }
    }
}

void render_gather_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                        int y1, bool remove)
{
    render_zone_area(renderer, camera, x0, y0, x1, y1,
                     remove ? STOCKPILE_REMOVE_COLOR : GATHER_ZONE_COLOR);
}

/* Fraction of a recipe's work done on a station's craft in progress. */
static float craft_fraction(const Workstation *station)
{
    const RecipeDef *recipe = recipe_def((RecipeType)station->work_recipe);
    if (recipe == NULL || station->work_input.type == ITEM_NONE) {
        return 0.0f;
    }
    return clampf(station->work_progress / recipe->work, 0.0f, 1.0f);
}


/* The station itself: a mat to sit on; a knapping spot adds a flat anvil stone. */
static void render_station_shape(SDL_Renderer *renderer, const SDL_FRect *tile,
                                 WorkstationType type, Uint8 alpha)
{
    SDL_Color mat = MAT_COLOR, anvil = ANVIL_COLOR, weave = shade(MAT_COLOR, -30);
    SDL_Color edge = ITEM_OUTLINE;
    mat.a = anvil.a = weave.a = edge.a = alpha;
    SDL_FRect area = inset(tile, 0.84f);
    fill(renderer, mat, &area);
    float step = SDL_max(3.0f, floorf(area.h / 4.0f));
    for (float y = area.y + step; y < area.y + area.h - 1.0f; y += step) {
        SDL_FRect line = {area.x, y, area.w, 1.0f};
        fill(renderer, weave, &line);
    }
    if (type == WORKSTATION_KNAPPING_SPOT) {
        SDL_FRect stone = {tile->x + floorf(tile->w * 0.3f), tile->y + floorf(tile->h * 0.34f),
                           floorf(tile->w * 0.4f), floorf(tile->h * 0.28f)};
        fill(renderer, edge, &stone);
        SDL_FRect top = {stone.x + 1.0f, stone.y + 1.0f, stone.w - 2.0f, stone.h - 2.0f};
        fill(renderer, anvil, &top);
    }
}

void render_workstations(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                         int selected)
{
    const Workstations *workstations = &game->workstations;
    if (workstations->used_count == 0) {
        return;
    }
    TileRect visible = camera_visible_tiles(camera, workstations->width, workstations->height);
    for (int i = 0; i < WORKSTATION_MAX; i++) {
        const Workstation *station = workstation_get_const(workstations, i);
        if (station == NULL || station->x < visible.x0 || station->x >= visible.x1 ||
            station->y < visible.y0 || station->y >= visible.y1) {
            continue;
        }
        SDL_FRect tile = tile_rect(camera, station->x, station->y);
        render_station_shape(renderer, &tile, (WorkstationType)station->type, 255);
        if (station->work_input.type != ITEM_NONE) {
            render_item_stack(renderer, &tile, (ItemType)station->work_input.type);
            batch_flush(renderer);
        }
        progress_bar(renderer, &tile, craft_fraction(station));
        if (i == selected) {
            outline(renderer, tile, 2.0f, SELECTION_COLOR);
        }
    }
}

void render_blueprint_preview(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                              WorkstationType type, int x, int y)
{
    SDL_FRect tile = tile_rect(camera, x, y);
    bool allowed = game_can_place_workstation(game, x, y);
    if (allowed) {
        render_station_shape(renderer, &tile, type, 110);
    }
    outline(renderer, tile, 2.0f, allowed ? BLUEPRINT_COLOR : BLUEPRINT_REFUSED_COLOR);
}

void render_workstation_icon(SDL_Renderer *renderer, SDL_FRect rect, WorkstationType type)
{
    render_station_shape(renderer, &rect, type, 255);
}

/* ---- Structures ------------------------------------------------------------- */

/* A structure seen from above, at `alpha` (ghosts of blueprints), filling
 * `rect` (all its tiles) and facing `direction`:
 * - grass bed: a litter of dry grass with a rolled-up end as a pillow, at
 *   its head;
 * - wall: thatch filling the tile, posts as dark dots along the middle;
 * - branch screen: a frame of branches woven across. */
static void render_structure_shape(SDL_Renderer *renderer, const SDL_FRect *tile,
                                   StructureType type, Direction direction, Uint8 alpha)
{
    float w = tile->w, h = tile->h;
    SDL_Color straw = DRY_GRASS_COLOR, dark = shade(DRY_GRASS_COLOR, -40);
    SDL_Color wood = BRANCH_COLOR, edge = ITEM_OUTLINE;
    straw.a = dark.a = wood.a = edge.a = alpha;
    switch (type) {
    case STRUCTURE_GRASS_BED: {
        bool vertical = direction == DIRECTION_NORTH || direction == DIRECTION_SOUTH;
        float across = vertical ? w : h; /* One tile. */
        float side = floorf(across * 0.12f), end = floorf(across * 0.06f);
        SDL_FRect bed = vertical ? (SDL_FRect){tile->x + side, tile->y + end, w - 2.0f * side,
                                               h - 2.0f * end}
                                 : (SDL_FRect){tile->x + end, tile->y + side, w - 2.0f * end,
                                               h - 2.0f * side};
        fill(renderer, edge, &bed);
        SDL_FRect litter = {bed.x + 1.0f, bed.y + 1.0f, bed.w - 2.0f, bed.h - 2.0f};
        fill(renderer, straw, &litter);
        float roll = floorf(across * 0.25f);
        SDL_FRect pillow = litter;
        if (vertical) {
            pillow.y += direction == DIRECTION_NORTH ? litter.h - roll : 0.0f;
            pillow.h = roll;
        } else {
            pillow.x += direction == DIRECTION_WEST ? litter.w - roll : 0.0f;
            pillow.w = roll;
        }
        fill(renderer, dark, &pillow);
        break;
    }
    case STRUCTURE_WALL: {
        fill(renderer, dark, tile);
        float step = SDL_max(2.0f, floorf(w / 6.0f));
        for (float x = tile->x + 1.0f; x < tile->x + w - 1.0f; x += step) {
            SDL_FRect stalk = {x, tile->y, SDL_max(1.0f, floorf(step * 0.5f)), h};
            fill(renderer, straw, &stalk);
        }
        float post = SDL_max(2.0f, floorf(w * 0.14f));
        for (int i = 0; i < 3; i++) {
            SDL_FRect dot = {tile->x + floorf(w * (0.17f + 0.33f * (float)i)) - post * 0.5f,
                             tile->y + floorf((h - post) * 0.5f), post, post};
            fill(renderer, wood, &dot);
        }
        break;
    }
    case STRUCTURE_DOOR: {
        SDL_FRect frame = inset(tile, 0.9f);
        outline(renderer, frame, SDL_max(2.0f, floorf(w * 0.1f)), wood);
        SDL_SetRenderDrawColor(renderer, wood.r, wood.g, wood.b, wood.a);
        float step = SDL_max(3.0f, floorf(frame.w / 4.0f));
        for (float d = step; d < frame.w; d += step) {
            SDL_RenderLine(renderer, frame.x + d, frame.y, frame.x, frame.y + d);
            SDL_RenderLine(renderer, frame.x + frame.w - d, frame.y + frame.h, frame.x + frame.w,
                           frame.y + frame.h - d);
        }
        break;
    }
    default: break;
    }
}

/* Screen rectangle of all the tiles of a structure whose head is at (x, y). */
static SDL_FRect footprint_rect(const Camera *camera, int x, int y, StructureType type,
                                Direction direction)
{
    int length = SDL_max(1, structure_length(type));
    int end_x = x + (length - 1) * direction_dx(direction);
    int end_y = y + (length - 1) * direction_dy(direction);
    SDL_FRect first = tile_rect(camera, SDL_min(x, end_x), SDL_min(y, end_y));
    SDL_FRect last = tile_rect(camera, SDL_max(x, end_x), SDL_max(y, end_y));
    return (SDL_FRect){first.x, first.y, last.x + last.w - first.x, last.y + last.h - first.y};
}

/* Visible tiles, widened so that a long structure with its head just out of
 * sight is still drawn. */
static TileRect visible_heads(const Camera *camera, int width, int height)
{
    TileRect visible = camera_visible_tiles(camera, width, height);
    int margin = STRUCTURE_LENGTH_MAX - 1;
    visible.x0 = SDL_max(0, visible.x0 - margin);
    visible.y0 = SDL_max(0, visible.y0 - margin);
    visible.x1 = SDL_min(width, visible.x1 + margin);
    visible.y1 = SDL_min(height, visible.y1 + margin);
    return visible;
}

void render_structures(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const Map *map = &game->map;
    if (map->structure_counts[STRUCTURE_NONE] < (int)map_tile_count(map)) {
        TileRect visible = visible_heads(camera, map->width, map->height);
        for (int y = visible.y0; y < visible.y1; y++) {
            for (int x = visible.x0; x < visible.x1; x++) {
                if (map_structure_is_origin(map, x, y)) {
                    StructureType type = map_structure(map, x, y);
                    Direction direction = map_structure_direction(map, x, y);
                    SDL_FRect rect = footprint_rect(camera, x, y, type, direction);
                    render_structure_shape(renderer, &rect, type, direction, 255);
                }
            }
        }
    }
    /* Blueprints: a ghost that fills in as the materials arrive, outlined. */
    const Constructions *constructions = &game->constructions;
    if (constructions->used_count == 0) {
        return;
    }
    TileRect visible = visible_heads(camera, constructions->width, constructions->height);
    for (int i = 0; i < CONSTRUCTION_MAX; i++) {
        const ConstructionSite *site = construction_get(constructions, i);
        if (site == NULL || site->x < visible.x0 || site->x >= visible.x1 ||
            site->y < visible.y0 || site->y >= visible.y1) {
            continue;
        }
        int needed = 0, delivered = 0;
        for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
            needed += construction_material_count(site, slot);
            delivered += site->delivered[slot].count;
        }
        float materials = needed > 0 ? (float)delivered / (float)needed : 1.0f;
        StructureType type = (StructureType)site->type;
        Direction direction = (Direction)site->direction;
        SDL_FRect rect = footprint_rect(camera, site->x, site->y, type, direction);
        Uint8 alpha = (Uint8)(70.0f + 110.0f * materials);
        WorkstationType station = construction_workstation(site);
        if (station != WORKSTATION_TYPE_COUNT) {
            render_station_shape(renderer, &rect, station, alpha);
        } else if (!site->refresh) { /* A refresh site sits on the structure itself. */
            render_structure_shape(renderer, &rect, type, direction, alpha);
        }
        float margin = floorf(SDL_min(rect.w, rect.h) * 0.05f); /* As inset(), not square. */
        SDL_FRect frame = {rect.x + margin, rect.y + margin, rect.w - 2.0f * margin,
                           rect.h - 2.0f * margin};
        outline(renderer, frame, 1.0f, BLUEPRINT_COLOR);
        progress_bar(renderer, &rect, construction_work_fraction(site));
    }
}

typedef struct StructurePreview {
    SDL_Renderer *renderer;
    const Camera *camera;
    StructureType type;
    Direction direction;
} StructurePreview;

static void preview_planned(int x, int y, bool allowed, void *userdata)
{
    const StructurePreview *preview = userdata;
    SDL_FRect rect = footprint_rect(preview->camera, x, y, preview->type, preview->direction);
    if (allowed) {
        render_structure_shape(preview->renderer, &rect, preview->type, preview->direction, 110);
    }
    outline(preview->renderer, rect, 1.0f, allowed ? BLUEPRINT_COLOR : BLUEPRINT_REFUSED_COLOR);
}

void render_structure_preview(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                              StructureType type, Direction direction, int x0, int y0, int x1,
                              int y1)
{
    StructurePreview preview = {renderer, camera, type, direction};
    game_plan_structure_rect(game, type, direction, x0, y0, x1, y1, preview_planned, &preview);
}

void render_structure_icon(SDL_Renderer *renderer, SDL_FRect rect, StructureType type)
{
    render_structure_shape(renderer, &rect, type, DIRECTION_SOUTH, 255);
}

/* A zone tile seen from above: its color, its border, and what it holds. */
static void zone_icon(SDL_Renderer *renderer, SDL_FRect rect, SDL_Color color)
{
    SDL_Color translucent = color;
    translucent.a = 90;
    SDL_FRect area = inset(&rect, 0.86f);
    fill(renderer, translucent, &area);
    outline(renderer, area, 2.0f, color);
}

/* Zone icon with a "minus" badge: tiles taken out of zones. */
static void zone_remove_icon(SDL_Renderer *renderer, SDL_FRect rect, SDL_Color color)
{
    zone_icon(renderer, rect, color);
    float w = rect.w, h = rect.h;
    SDL_FRect badge = {rect.x + floorf(w * 0.22f), rect.y + floorf(h * 0.42f), floorf(w * 0.56f),
                       SDL_max(3.0f, floorf(h * 0.16f))};
    fill_outlined(renderer, badge, STOCKPILE_REMOVE_COLOR);
    batch_flush(renderer);
}

void render_tool_icon(SDL_Renderer *renderer, SDL_FRect rect, RenderIcon icon)
{
    float w = rect.w, h = rect.h;
    switch (icon) {
    case RENDER_ICON_GATHER_ZONE_REMOVE: zone_remove_icon(renderer, rect, GATHER_ZONE_COLOR); break;
    case RENDER_ICON_STOCKPILE_REMOVE: zone_remove_icon(renderer, rect, STOCKPILE_COLOR); break;
    case RENDER_ICON_GATHER_ZONE: {
        zone_icon(renderer, rect, GATHER_ZONE_COLOR);
        /* A sapling and a stone. */
        SDL_FRect trunk = {rect.x + floorf(w * 0.3f), rect.y + floorf(h * 0.45f),
                           SDL_max(2.0f, floorf(w * 0.08f)), floorf(h * 0.25f)};
        SDL_FRect crown = {rect.x + floorf(w * 0.2f), rect.y + floorf(h * 0.25f),
                           floorf(w * 0.28f), floorf(h * 0.24f)};
        SDL_FRect stone = {rect.x + floorf(w * 0.58f), rect.y + floorf(h * 0.56f),
                           floorf(w * 0.18f), floorf(h * 0.14f)};
        fill(renderer, WOOD_COLOR, &trunk);
        fill(renderer, TREE_COLOR, &crown);
        fill_outlined(renderer, stone, STONE_COLOR);
        batch_flush(renderer);
        break;
    }
    case RENDER_ICON_STOCKPILE: {
        zone_icon(renderer, rect, STOCKPILE_COLOR);
        /* Two stacked logs. */
        for (int i = 0; i < 2; i++) {
            SDL_FRect log = {rect.x + floorf(w * 0.28f), rect.y + floorf(h * (0.38f + 0.16f * i)),
                             floorf(w * 0.44f), floorf(h * 0.12f)};
            fill_outlined(renderer, log, WOOD_COLOR);
        }
        batch_flush(renderer);
        break;
    }
    case RENDER_ICON_MINE: {
        SDL_FRect area = inset(&rect, 0.8f);
        fill(renderer, ROCK_COLORS[ROCK_GRANITE], &area);
        /* Cracks. */
        SDL_SetRenderDrawColor(renderer, 40, 40, 46, 255);
        SDL_RenderLine(renderer, area.x + area.w * 0.2f, area.y + area.h * 0.3f,
                       area.x + area.w * 0.55f, area.y + area.h * 0.55f);
        SDL_RenderLine(renderer, area.x + area.w * 0.55f, area.y + area.h * 0.55f,
                       area.x + area.w * 0.8f, area.y + area.h * 0.45f);
        SDL_RenderLine(renderer, area.x + area.w * 0.55f, area.y + area.h * 0.55f,
                       area.x + area.w * 0.5f, area.y + area.h * 0.85f);
        outline(renderer, area, 2.0f, DESIGNATION_COLORS[DESIGNATION_MINE]);
        break;
    }
    case RENDER_ICON_DECONSTRUCT: {
        SDL_FRect area = inset(&rect, 0.7f);
        render_structure_shape(renderer, &area, STRUCTURE_WALL, DIRECTION_SOUTH, 255);
        outline(renderer, area, 2.0f, DESIGNATION_COLORS[DESIGNATION_DECONSTRUCT]);
        break;
    }
    case RENDER_ICON_CANCEL: {
        SDL_FRect area = inset(&rect, 0.5f);
        SDL_Color red = DESIGNATION_COLORS[DESIGNATION_NONE];
        SDL_SetRenderDrawColor(renderer, red.r, red.g, red.b, red.a);
        for (float d = -1.5f; d <= 1.5f; d += 0.5f) {
            SDL_RenderLine(renderer, area.x + d, area.y, area.x + area.w + d, area.y + area.h);
            SDL_RenderLine(renderer, area.x + area.w + d, area.y, area.x + d, area.y + area.h);
        }
        break;
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

void render_tile_selection(SDL_Renderer *renderer, const Camera *camera, int x, int y)
{
    outline(renderer, tile_rect(camera, x, y), 2.0f, SELECTION_COLOR);
}

/* ---- Plants and colonists ------------------------------------------------ */

/* Trees reach this many rows above their tile, and one column on each side. */
#define SPRITE_OVERFLOW_ROWS 3
#define SPRITE_OVERFLOW_COLUMNS 1
/* Depth of a plant in its tile (where its trunk or stems meet the ground):
 * things are drawn from the back (top of the screen) to the front. */
#define PLANT_DEPTH 0.91f
/* Feet of a colonist below its position (the middle of its tile), in tiles,
 * and the margin under the feet in the character sprites. */
#define COLONIST_FEET 0.38f
#define COLONIST_SPRITE_MARGIN (8.0f / SPRITE_PIXELS_PER_TILE)
/* Brightness of a sleeping colonist. */
#define COLONIST_SLEEP_SHADE 0.55f
/* Screen area of a colonist sprite (the figure, not the empty margins) in
 * tiles around its feet, and the opacity of a tree standing in front of it:
 * nobody gets lost in a forest. */
#define COLONIST_HALF_WIDTH 0.3f
#define COLONIST_HEIGHT 1.05f
#define TREE_SEE_THROUGH_ALPHA 0.4f

static const SDL_FColor WHITE = {1.0f, 1.0f, 1.0f, 1.0f};

/* ---- Hand animation ------------------------------------------------------ */

/* Where the hand of a working colonist is: offsets from its feet in tiles
 * (x toward the side of the job, y upward) and the angle of the tool in
 * degrees (0: pointing up, positive: leaning toward the job). */
typedef struct HandPose {
    float x, y, angle;
} HandPose;

/* Length of one movement, in ticks: it follows the game speed and stops when
 * the game is paused. */
static const int ACTION_PERIODS[COLONIST_ACTION_COUNT] = {
    [COLONIST_ACTION_STRIKE] = 36,
    [COLONIST_ACTION_CUT] = 30,
    [COLONIST_ACTION_GATHER] = 48,
    [COLONIST_ACTION_EAT] = 60,
};

/* `phase` goes from 0 to 1 over one movement. */
static HandPose hand_pose(ColonistAction action, float phase)
{
    float wave = 0.5f - 0.5f * SDL_cosf(2.0f * SDL_PI_F * phase); /* 0 → 1 → 0, smooth. */
    switch (action) {
    case COLONIST_ACTION_STRIKE: {
        /* Raised slowly, brought down fast. */
        float lift = phase < 0.75f ? SDL_sinf(phase / 0.75f * SDL_PI_F * 0.5f)
                                   : SDL_cosf((phase - 0.75f) / 0.25f * SDL_PI_F * 0.5f);
        return (HandPose){0.31f - 0.05f * lift, 0.3f + 0.3f * lift, 75.0f - 85.0f * lift};
    }
    case COLONIST_ACTION_CUT: {
        float stroke = SDL_sinf(2.0f * SDL_PI_F * phase);
        return (HandPose){0.27f + 0.08f * stroke, 0.36f + 0.02f * wave, 55.0f + 12.0f * stroke};
    }
    case COLONIST_ACTION_GATHER: return (HandPose){0.3f + 0.13f * wave, 0.3f - 0.07f * wave, 0.0f};
    case COLONIST_ACTION_EAT: return (HandPose){0.3f - 0.22f * wave, 0.32f + 0.32f * wave, 0.0f};
    case COLONIST_ACTION_NONE:
    case COLONIST_ACTION_COUNT: break;
    }
    return (HandPose){0.3f, 0.3f, 0.0f};
}

/* Filled ring (an ellipse with a hole), e.g. a selection mark on the ground. */
static void fill_ring(SDL_Renderer *renderer, float cx, float cy, float rx, float ry,
                      float thickness, SDL_Color color)
{
    enum { SEGMENTS = 32 };
    SDL_Vertex vertices[SEGMENTS * 2];
    int indices[SEGMENTS * 6];
    SDL_FColor fcolor = to_fcolor(color);
    for (int i = 0; i < SEGMENTS; i++) {
        float angle = (float)i * (2.0f * SDL_PI_F / SEGMENTS);
        float c = SDL_cosf(angle), s = SDL_sinf(angle);
        vertices[i * 2] = (SDL_Vertex){{cx + rx * c, cy + ry * s}, fcolor, {0.0f, 0.0f}};
        vertices[i * 2 + 1] = (SDL_Vertex){
            {cx + (rx - thickness) * c, cy + (ry - thickness) * s}, fcolor, {0.0f, 0.0f}};
        int next = (i + 1) % SEGMENTS;
        int quad[6] = {i * 2, next * 2, next * 2 + 1, next * 2 + 1, i * 2 + 1, i * 2};
        SDL_memcpy(&indices[i * 6], quad, sizeof(quad));
    }
    SDL_RenderGeometry(renderer, NULL, vertices, SEGMENTS * 2, indices, SEGMENTS * 6);
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
    camera_world_to_screen(camera, x, y + COLONIST_FEET, &points[count].x, &points[count].y);
    count++;
    for (int i = colonist->path_index; i < colonist->path.count && count < MAX_POINTS; i++) {
        TilePos step = colonist->path.steps[i];
        camera_world_to_screen(camera, step.x + 0.5f, step.y + 0.5f + COLONIST_FEET,
                               &points[count].x, &points[count].y);
        count++;
    }
    if (count > 1) {
        SDL_SetRenderDrawColor(renderer, PATH_COLOR.r, PATH_COLOR.g, PATH_COLOR.b, PATH_COLOR.a);
        SDL_RenderLines(renderer, points, count);
    }
}

static SDL_FColor tint(SDL_Color color, float brightness)
{
    SDL_FColor result = to_fcolor(color);
    result.r *= brightness;
    result.g *= brightness;
    result.b *= brightness;
    return result;
}

/* A colonist from its layers: body, head (with the face), beard, hair. */
/* A fist (holding the tool of the job, if any) moving beside a working colonist. */
static void render_hand(Sprites *sprites, const Camera *camera, const Colonist *colonist,
                        uint32_t slot, uint64_t tick, float feet_x, float feet_y,
                        SDL_FColor color)
{
    ItemType tool;
    ColonistAction action = colonist_action(colonist, &tool);
    if (action == COLONIST_ACTION_NONE) {
        return;
    }
    int period = ACTION_PERIODS[action];
    /* Colonists side by side do not move in step. */
    uint64_t step = (tick + (uint64_t)slot * 7u) % (uint64_t)period;
    HandPose pose = hand_pose(action, (float)step / (float)period);
    /* The hand on the side of the job (the right one when it is straight up or down). */
    float side = colonist->target.x < colonist->x ? -1.0f : 1.0f;
    float hand_x = feet_x + side * pose.x * camera->zoom;
    float hand_y = feet_y - pose.y * camera->zoom;
    SpriteId tool_sprite = sprite_tool(tool);
    if (tool_sprite >= 0) {
        sprites_draw_rotated(sprites, tool_sprite, hand_x, hand_y, side * pose.angle, color,
                             side < 0.0f);
    }
    sprites_draw_rotated(sprites, sprite_fist(colonist->appearance.skin), hand_x, hand_y, 0.0f,
                         color, side < 0.0f);
}

static void render_colonist(SDL_Renderer *renderer, Sprites *sprites, const Camera *camera,
                            const Colonist *colonist, uint32_t slot, uint64_t tick,
                            bool selected)
{
    float wx, wy, feet_x, feet_y, anchor_x, anchor_y;
    colonist_position(colonist, &wx, &wy);
    camera_world_to_screen(camera, wx, wy + COLONIST_FEET, &feet_x, &feet_y);
    camera_world_to_screen(camera, wx, wy + COLONIST_FEET + COLONIST_SPRITE_MARGIN, &anchor_x,
                           &anchor_y);
    if (selected) {
        sprites_flush(sprites);
        float rx = SDL_max(4.0f, camera->zoom * 0.3f);
        fill_ring(renderer, feet_x, feet_y, rx, rx * 0.4f, SDL_max(1.5f, camera->zoom * 0.04f),
                  SELECTION_COLOR);
    }
    ColonistAppearance looks = colonist->appearance;
    float brightness = colonist->task == TASK_SLEEP ? COLONIST_SLEEP_SHADE : 1.0f;
    SDL_FColor skin = tint((SDL_Color){255, 255, 255, 255}, brightness);
    SDL_FColor hair = tint(HAIR_COLORS[looks.hair_color % HAIR_COLOR_COUNT], brightness);
    sprites_draw(sprites, sprite_body(looks.body, looks.skin), anchor_x, anchor_y, skin, false);
    sprites_draw(sprites, sprite_head(looks.skin, colonist_expression(colonist)), anchor_x,
                 anchor_y, skin, false);
    if (looks.beard > 0) {
        sprites_draw(sprites, sprite_beard(looks.beard), anchor_x, anchor_y, hair, false);
    }
    if (looks.hair_style > 0) {
        sprites_draw(sprites, sprite_hair(looks.hair_style), anchor_x, anchor_y, hair, false);
    }
    render_hand(sprites, camera, colonist, slot, tick, feet_x, feet_y, skin);
    if (colonist->carried.count > 0) {
        /* What it carries, held in front of it at waist height. */
        sprites_flush(sprites);
        float size = floorf(SDL_max(4.0f, camera->zoom * 0.42f));
        SDL_FRect held = {floorf(feet_x - size * 0.5f), floorf(feet_y - camera->zoom * 0.62f),
                          size, size};
        render_item_stack(renderer, &held, (ItemType)colonist->carried.type);
        batch_flush(renderer);
    }
}

typedef struct DepthEntry {
    float depth;
    uint32_t slot;
    SDL_FRect area; /* On screen. */
} DepthEntry;

static int compare_depth(const void *a, const void *b)
{
    const DepthEntry *x = a, *y = b;
    if (x->depth != y->depth) {
        return x->depth < y->depth ? -1 : 1;
    }
    return x->slot < y->slot ? -1 : (x->slot > y->slot);
}

/* Whether a sprite drawn at `depth` over `rect` would hide the head or chest of
 * a colonist behind it. Only the middle of the sprite counts (its canvas has
 * empty corners): in a forest, only the trees right in front of someone fade. */
static bool hides_colonist(const SDL_FRect *rect, float depth, const DepthEntry *colonists,
                           int count)
{
    SDL_FRect core = {rect->x + rect->w * 0.15f, rect->y, rect->w * 0.7f, rect->h * 0.9f};
    for (int i = 0; i < count && colonists[i].depth < depth; i++) {
        const SDL_FRect *area = &colonists[i].area;
        SDL_FPoint chest = {area->x + area->w * 0.5f, area->y + area->h * 0.4f};
        if (SDL_PointInRectFloat(&chest, &core)) {
            return true;
        }
    }
    return false;
}

static void render_plant(Sprites *sprites, const Camera *camera, const Plant *plant, int x, int y,
                         const DepthEntry *colonists, int colonist_count)
{
    SpriteId id;
    if (plant->type == PLANT_TREE) {
        id = sprite_tree(plant_growth_stage(plant));
    } else if (plant->type == PLANT_BUSH) {
        id = sprite_bush(plant_growth_stage(plant), plant_has_fruit(plant));
    } else {
        return; /* Grass is part of the ground. */
    }
    float sx, sy;
    camera_world_to_screen(camera, (float)x + 0.5f, (float)(y + 1), &sx, &sy);
    SDL_FColor color = WHITE;
    if (plant->type == PLANT_TREE && colonist_count > 0) {
        float width, height;
        sprites_size(sprites, id, &width, &height);
        SDL_FRect rect = {sx - width * 0.5f, sy - height, width, height};
        if (hides_colonist(&rect, (float)y + PLANT_DEPTH, colonists, colonist_count)) {
            color.a = TREE_SEE_THROUGH_ALPHA;
        }
    }
    /* Mirrored on some tiles so that a forest does not look like a stamp. */
    sprites_draw(sprites, id, sx, sy, color, (tile_variation(x, y) & 4) != 0);
}

void render_world_sprites(SDL_Renderer *renderer, Sprites *sprites, const Game *game,
                          const Camera *camera, const EntityId *selected, int selected_count)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (!entity_id_is_none(id) && is_selected(id, selected, selected_count)) {
            render_path(renderer, camera, &game->colonists[slot]);
        }
    }

    const Map *map = &game->map;
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    int x0 = SDL_max(0, visible.x0 - SPRITE_OVERFLOW_COLUMNS);
    int x1 = SDL_min(map->width, visible.x1 + SPRITE_OVERFLOW_COLUMNS);
    int y1 = SDL_min(map->height, visible.y1 + SPRITE_OVERFLOW_ROWS);

    /* Colonists in depth order, merged with the rows of plants below. */
    DepthEntry order[COLONIST_MAX];
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game) && count < COLONIST_MAX;
         slot++) {
        if (entity_id_is_none(game_colonist_id_at(game, slot))) {
            continue;
        }
        float wx, wy;
        colonist_position(&game->colonists[slot], &wx, &wy);
        if (wx < (float)x0 - 1.0f || wx > (float)x1 + 1.0f || wy < (float)visible.y0 - 1.0f ||
            wy > (float)y1 + 1.0f) {
            continue;
        }
        float sx, sy;
        camera_world_to_screen(camera, wx, wy + COLONIST_FEET, &sx, &sy);
        float half_width = COLONIST_HALF_WIDTH * camera->zoom;
        float height = COLONIST_HEIGHT * camera->zoom;
        SDL_FRect area = {sx - half_width, sy - height, 2.0f * half_width, height};
        order[count++] = (DepthEntry){wy + COLONIST_FEET, slot, area};
    }
    qsort(order, (size_t)count, sizeof(order[0]), compare_depth);

    sprites_begin(sprites, camera->zoom);
    uint64_t tick = game->clock.tick;
    int next = 0;
    for (int y = visible.y0; y < y1; y++) {
        while (next < count && order[next].depth < (float)y + PLANT_DEPTH) {
            uint32_t slot = order[next++].slot;
            render_colonist(renderer, sprites, camera, &game->colonists[slot], slot, tick,
                            is_selected(game_colonist_id_at(game, slot), selected,
                                        selected_count));
        }
        for (int x = x0; x < x1; x++) {
            render_plant(sprites, camera, &map->plants[map_index(map, x, y)], x, y, order,
                         count);
        }
    }
    while (next < count) {
        uint32_t slot = order[next++].slot;
        render_colonist(renderer, sprites, camera, &game->colonists[slot], slot, tick,
                        is_selected(game_colonist_id_at(game, slot), selected, selected_count));
    }
    sprites_flush(sprites);
}

void render_selection_box(SDL_Renderer *renderer, float x0, float y0, float x1, float y1)
{
    SDL_FRect rect = {SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0)};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 40);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
    SDL_RenderRect(renderer, &rect);
}
