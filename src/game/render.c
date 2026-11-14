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
/* Designation marks; index DESIGNATION_NONE is the color of the cancel tool. */
static const SDL_Color DESIGNATION_COLORS[DESIGNATION_COUNT] = {
    [DESIGNATION_NONE] = {230, 70, 60, 255},
    [DESIGNATION_MINE] = {250, 140, 60, 255},
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

            if (plant->type == PLANT_BUSH) {
                SDL_FRect rect = inset(&tile, 0.25f + 0.3f * plant->growth);
                batch_quad(renderer, shade(BUSH_COLOR, variation), &rect);
            } else if (plant->type == PLANT_TREE) {
                SDL_FRect rect = inset(&tile, 0.35f + 0.5f * plant->growth);
                batch_quad(renderer, shade(TREE_COLOR, variation), &rect);
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
    float height = SDL_max(2.0f, floorf(tile->h * 0.12f));
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
        if (station->built) {
            render_station_shape(renderer, &tile, (WorkstationType)station->type, 255);
            if (station->work_input.type != ITEM_NONE) {
                render_item_stack(renderer, &tile, (ItemType)station->work_input.type);
                batch_flush(renderer);
            }
            progress_bar(renderer, &tile, craft_fraction(station));
        } else {
            /* Blueprint: a ghost of the station, outlined. */
            render_station_shape(renderer, &tile, (WorkstationType)station->type, 90);
            outline(renderer, inset(&tile, 0.9f), 1.0f, BLUEPRINT_COLOR);
            progress_bar(renderer, &tile, workstation_build_fraction(station));
        }
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

static void fill_circle(SDL_Renderer *renderer, float cx, float cy, float radius, SDL_Color color)
{
    enum { SEGMENTS = 24 };
    SDL_Vertex vertices[SEGMENTS + 1];
    int indices[SEGMENTS * 3];
    SDL_FColor fcolor = to_fcolor(color);

    vertices[0] = (SDL_Vertex){{cx, cy}, fcolor, {0.0f, 0.0f}};
    for (int i = 0; i < SEGMENTS; i++) {
        float angle = (float)i * (2.0f * SDL_PI_F / SEGMENTS);
        SDL_FPoint point = {cx + radius * SDL_cosf(angle), cy + radius * SDL_sinf(angle)};
        vertices[i + 1] = (SDL_Vertex){point, fcolor, {0.0f, 0.0f}};
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
        if (colonist->carried.count > 0) {
            /* What it carries, held in front (lower right) of it. */
            float size = floorf(radius * 1.4f);
            SDL_FRect held = {floorf(sx + radius * 0.2f), floorf(sy + radius * 0.1f), size, size};
            render_item_stack(renderer, &held, (ItemType)colonist->carried.type);
            batch_flush(renderer); /* Before the next colonist is drawn over it. */
        }
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
