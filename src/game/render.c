#include "game/render.h"

#include "core/hearth.h"
#include "core/tree.h"
#include "core/util.h"

#include <limits.h>
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
    [TERRAIN_GRASS_ROOF] = {196, 168, 96, 255}, /* Its thatch: see render_structures(). */
    [TERRAIN_FOLIAGE] = {62, 106, 48, 255}, /* Leaf clumps over it: render_foliage(). */
};

static const SDL_Color ROCK_COLORS[ROCK_COUNT] = {
    [ROCK_NONE] = {0, 0, 0, 0},
    [ROCK_GRANITE] = {92, 92, 100, 255},
    [ROCK_SANDSTONE] = {150, 118, 88, 255},
    [ROCK_EARTH] = {104, 78, 54, 255},
    [ROCK_WOOD] = {150, 110, 70, 255}, /* The cut face of a trunk: render_trunk(). */
};
/* A trunk's bark, around its cut face, and its growth rings. */
static const SDL_Color BARK_COLOR = {78, 54, 34, 255};
static const SDL_Color RING_COLOR = {120, 84, 50, 255};
/* Leaf clumps on foliage, darker and lighter than its floor. */
static const SDL_Color LEAVES_DARK = {38, 76, 34, 255};
static const SDL_Color LEAVES_LIGHT = {104, 150, 66, 255};
/* The shade of the foliage above on the ground under it. */
#define FOLIAGE_SHADOW (-26)
/* Leaf clumps and growth rings are drawn from this zoom on (pixels per tile). */
#define TREE_DETAIL_MIN_ZOOM 10.0f

/* Levels below the one shown (render_below_begin()): drawn this many times
 * smaller, then stretched back with smoothing (a blur), each level deeper
 * darkened by BELOW_SHADE more. */
#define BELOW_SCALE 3
static const SDL_Color BELOW_SHADE = {14, 16, 24, 70};
static const SDL_Color BELOW_ABYSS = {14, 16, 24, 255}; /* Deeper than drawn. */
/* Edges of the ground over a drop to the levels below. */
static const SDL_Color DROP_EDGE_COLOR = {16, 12, 8, 120};

/* The offscreen image of the levels below, and the render scale it replaced. */
static SDL_Texture *below_texture;
static float below_saved_scale_x = 1.0f, below_saved_scale_y = 1.0f;

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
    [DESIGNATION_DIG] = {250, 140, 60, 255},
    [DESIGNATION_DIG_RAMP] = {250, 170, 90, 255},
    [DESIGNATION_DIG_DOWN] = {250, 120, 70, 255},
};
/* Where a roof could rest, while planning roofs. */
static const SDL_Color ROOF_SUPPORT_COLOR = {120, 220, 140, 60};
static const SDL_Color STOCKPILE_COLOR = {90, 170, 230, 255};
static const SDL_Color STOCKPILE_REMOVE_COLOR = {230, 70, 60, 255};
static const SDL_Color GATHER_ZONE_COLOR = {225, 170, 90, 255};
static const SDL_Color WOOD_COLOR = {150, 98, 52, 255};
static const SDL_Color STONE_COLOR = {176, 174, 168, 255};
static const SDL_Color BERRY_COLOR = {70, 28, 64, 255};
static const SDL_Color HAZELNUT_COLOR = {150, 104, 56, 255};
static const SDL_Color MUSHROOM_CAP_COLOR = {178, 132, 84, 255};
static const SDL_Color MUSHROOM_STEM_COLOR = {226, 214, 190, 255};
static const SDL_Color WILD_GARLIC_COLOR = {84, 170, 70, 255};
static const SDL_Color NETTLE_COLOR = {56, 112, 52, 255};
static const SDL_Color PLANTAIN_COLOR = {106, 146, 68, 255};
static const SDL_Color WILD_CARROT_COLOR = {222, 206, 168, 255};
static const SDL_Color RHIZOME_COLOR = {176, 140, 100, 255};
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
static const SDL_Color HARDENED_SPEAR_COLOR = {128, 88, 52, 255};
static const SDL_Color PIT_COLOR = {46, 38, 34, 255};
static const SDL_Color ASH_COLOR = {98, 94, 90, 255};
static const SDL_Color FLAME_COLOR = {240, 118, 34, 255};
static const SDL_Color FLAME_CORE_COLOR = {255, 222, 96, 255};
static const SDL_Color NIGHT_COLOR = {8, 12, 40, 255};
static const SDL_Color GLOW_COLOR = {255, 136, 48, 255};

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

/* Night: light at the tile corners of the area lit by fires (render_daylight()). */
static float *light_corners;
static size_t light_corner_capacity;

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

/* Like batch_quad(), each corner (clockwise from the top left) its own color. */
static void batch_gradient_quad(SDL_Renderer *renderer, const SDL_Color colors[4],
                                SDL_FRect rect)
{
    if (!batch_ready()) {
        fill(renderer, colors[0], &rect);
        return;
    }
    if (batch_count == BATCH_MAX_QUADS) {
        batch_flush(renderer);
    }
    SDL_Vertex *vertex = &batch_vertices[batch_count * 4];
    float left = rect.x, top = rect.y, right = rect.x + rect.w, bottom = rect.y + rect.h;
    vertex[0] = (SDL_Vertex){{left, top}, to_fcolor(colors[0]), {0.0f, 0.0f}};
    vertex[1] = (SDL_Vertex){{right, top}, to_fcolor(colors[1]), {0.0f, 0.0f}};
    vertex[2] = (SDL_Vertex){{right, bottom}, to_fcolor(colors[2]), {0.0f, 0.0f}};
    vertex[3] = (SDL_Vertex){{left, bottom}, to_fcolor(colors[3]), {0.0f, 0.0f}};
    batch_count++;
}

void render_shutdown(void)
{
    if (below_texture != NULL) {
        SDL_DestroyTexture(below_texture);
        below_texture = NULL;
    }
    SDL_free(light_corners);
    light_corners = NULL;
    light_corner_capacity = 0;
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

/* Bits of a deterministic hash of a tile and a number (spot positions). */
static uint32_t tile_hash(int x, int y, int n)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)n * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

/* Whether the ground of a tile is in the shade of foliage above it (the
 * first floor over it, within a tree's height). */
static bool under_foliage(const Map *map, int x, int y, int z)
{
    for (int above = z + 1; above < map->depth && above < z + TREE_MAX_LEVELS; above++) {
        size_t index = map_index(map, x, y, above);
        if (map->terrain[index] == TERRAIN_FOLIAGE && map->rock[index] == ROCK_NONE) {
            return true;
        }
        if (!map_is_open_at(map, index)) {
            return false;
        }
    }
    return false;
}

/* A tile of trunk, as the cut face of a wall of wood: bark on the sides
 * facing out of the trunk; a single tile also shows growth rings. */
static void render_trunk(SDL_Renderer *renderer, const Map *map, int x, int y, int z,
                         const SDL_FRect *tile, float zoom)
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    batch_quad(renderer, shade(ROCK_COLORS[ROCK_WOOD], tile_variation(x, y) / 2), tile);
    float bark = SDL_max(1.0f, floorf(tile->w * 0.14f));
    int sides = 0;
    for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (map_in_bounds(map, nx, ny, z) && map->rock[map_index(map, nx, ny, z)] == ROCK_WOOD) {
            continue;
        }
        sides++;
        SDL_FRect strip = *tile;
        if (DX[d] > 0) {
            strip.x += strip.w - bark;
        }
        if (DY[d] > 0) {
            strip.y += strip.h - bark;
        }
        if (DX[d] != 0) {
            strip.w = bark;
        } else {
            strip.h = bark;
        }
        batch_quad(renderer, BARK_COLOR, &strip);
    }
    if (sides == 4 && zoom >= TREE_DETAIL_MIN_ZOOM) {
        SDL_FRect ring = inset(tile, 0.5f);
        SDL_FRect heart = inset(tile, 0.36f);
        SDL_FRect pith = inset(tile, 0.1f);
        batch_quad(renderer, RING_COLOR, &ring);
        batch_quad(renderer, ROCK_COLORS[ROCK_WOOD], &heart);
        batch_quad(renderer, RING_COLOR, &pith);
    }
}

/* Leaf clumps scattered over a tile of foliage, at fixed places per tile. */
static void render_foliage(SDL_Renderer *renderer, int x, int y, const SDL_FRect *tile,
                           float zoom)
{
    if (zoom < TREE_DETAIL_MIN_ZOOM) {
        return;
    }
    for (int i = 0; i < 6; i++) {
        uint32_t h = tile_hash(x, y, 100 + i);
        float size = tile->w * (0.18f + 0.14f * (float)(h & 0xffu) / 255.0f);
        float cx = tile->x + tile->w * (0.1f + 0.8f * (float)((h >> 8) & 0xffu) / 255.0f);
        float cy = tile->y + tile->h * (0.1f + 0.8f * (float)((h >> 16) & 0xffu) / 255.0f);
        SDL_FRect wide = {floorf(cx - size * 0.5f), floorf(cy - size * 0.3f), floorf(size),
                          floorf(size * 0.6f)};
        SDL_FRect tall = {floorf(cx - size * 0.3f), floorf(cy - size * 0.5f), floorf(size * 0.6f),
                          floorf(size)};
        SDL_Color color = (h >> 24) & 1u ? LEAVES_LIGHT : LEAVES_DARK;
        batch_quad(renderer, color, &wide);
        batch_quad(renderer, color, &tall);
    }
}

/* Slopes lighter towards the higher ground they lead up to; a dark edge on
 * each side of the tile over a drop to the levels below. */
static void render_ground_shape(SDL_Renderer *renderer, const Map *map, int x, int y, int z,
                                const SDL_FRect *tile)
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    if (map_is_slope(map, x, y, z)) {
        for (int d = 0; d < 4; d++) {
            if (!map_can_climb(map, x, y, z, x + DX[d], y + DY[d])) {
                continue;
            }
            const SDL_Color low = {0, 0, 0, 40}, high = {255, 250, 235, 50};
            /* Corners clockwise from the top left: the high side lit. */
            SDL_Color corners[4] = {low, low, low, low};
            if (DX[d] > 0) {
                corners[1] = corners[2] = high;
            } else if (DX[d] < 0) {
                corners[0] = corners[3] = high;
            } else if (DY[d] > 0) {
                corners[2] = corners[3] = high;
            } else {
                corners[0] = corners[1] = high;
            }
            batch_gradient_quad(renderer, corners, *tile);
        }
    }
    float edge = SDL_max(1.0f, floorf(tile->w * 0.08f));
    for (int d = 0; d < 4; d++) {
        if (!map_is_open(map, x + DX[d], y + DY[d], z)) {
            continue;
        }
        SDL_FRect strip = *tile;
        if (DX[d] > 0) {
            strip.x += strip.w - edge;
        }
        if (DY[d] > 0) {
            strip.y += strip.h - edge;
        }
        if (DX[d] != 0) {
            strip.w = edge;
        } else {
            strip.h = edge;
        }
        batch_quad(renderer, DROP_EDGE_COLOR, &strip);
    }
}

bool render_below_begin(SDL_Renderer *renderer, const Map *map, const Camera *camera)
{
    if (camera->level <= 0 || camera->level >= map->depth) {
        return false;
    }
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    bool open = false;
    for (int y = visible.y0; y < visible.y1 && !open; y++) {
        for (int x = visible.x0; x < visible.x1 && !open; x++) {
            open = map_is_open(map, x, y, camera->level);
        }
    }
    int width = (int)ceilf(camera->viewport_width / (float)BELOW_SCALE);
    int height = (int)ceilf(camera->viewport_height / (float)BELOW_SCALE);
    if (!open || width <= 0 || height <= 0) {
        return false;
    }
    if (below_texture != NULL && (below_texture->w != width || below_texture->h != height)) {
        SDL_DestroyTexture(below_texture);
        below_texture = NULL;
    }
    if (below_texture == NULL) {
        below_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, width, height);
        if (below_texture == NULL) {
            return false;
        }
        SDL_SetTextureScaleMode(below_texture, SDL_SCALEMODE_LINEAR);
    }
    SDL_GetRenderScale(renderer, &below_saved_scale_x, &below_saved_scale_y);
    if (!SDL_SetRenderTarget(renderer, below_texture)) {
        return false;
    }
    SDL_SetRenderScale(renderer, below_saved_scale_x / (float)BELOW_SCALE,
                       below_saved_scale_y / (float)BELOW_SCALE);
    SDL_SetRenderDrawColor(renderer, BELOW_ABYSS.r, BELOW_ABYSS.g, BELOW_ABYSS.b, 255);
    SDL_RenderClear(renderer);
    return true;
}

void render_below_shade(SDL_Renderer *renderer)
{
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, BELOW_SHADE.r, BELOW_SHADE.g, BELOW_SHADE.b, BELOW_SHADE.a);
    SDL_RenderFillRect(renderer, NULL);
}

void render_below_end(SDL_Renderer *renderer)
{
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderScale(renderer, below_saved_scale_x, below_saved_scale_y);
    SDL_RenderTexture(renderer, below_texture, NULL, NULL);
}

void render_map(SDL_Renderer *renderer, const Map *map, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
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

            size_t index = map_index(map, x, y, z);
            int variation = tile_variation(x, y);

            if (map_is_open_at(map, index)) {
                continue; /* The levels below show (render_below_begin()). */
            }
            if (map->rock[index] == ROCK_WOOD) {
                render_trunk(renderer, map, x, y, z, &tile, camera->zoom);
                continue;
            }
            if (map->rock[index] != ROCK_NONE) {
                batch_quad(renderer, shade(ROCK_COLORS[map->rock[index]], variation), &tile);
                continue;
            }

            const Plant *plant = &map->plants[index];
            SDL_Color ground = TERRAIN_COLORS[map->terrain[index]];
            if (plant->type == PLANT_GRASS) {
                ground = mix(ground, GRASS_COLOR, 0.35f + 0.5f * plant->growth);
            }
            int shadow = under_foliage(map, x, y, z) ? FOLIAGE_SHADOW : 0;
            batch_quad(renderer, shade(ground, variation + shadow), &tile);
            if (map->terrain[index] == TERRAIN_FOLIAGE) {
                render_foliage(renderer, x, y, &tile, camera->zoom);
            }
            render_ground_shape(renderer, map, x, y, z, &tile);
        }
    }
    batch_flush(renderer);
}

/* Dirt: dark spots scattered on the tile, more and darker as it gets dirtier;
 * each spot is two crossed rectangles, which reads as a blob. */
#define DIRT_SPOTS_MAX 8

void render_dirt(SDL_Renderer *renderer, const Dirt *dirt, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
    if (dirt->dirty_count == 0) {
        return;
    }
    TileRect visible = camera_visible_tiles(camera, dirt->width, dirt->height);
    float pixels = camera->zoom;
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            int level = dirt_at(dirt, x, y, z);
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

/* Snow lying on open ground: a white veil over each tile under the open
 * sky (not on water, rock or under a roof), thicker as the cover grows. */
void render_snow_cover(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
    float snow = game->weather.snow;
    if (snow <= 0.0f) {
        return;
    }
    const Map *map = &game->map;
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    SDL_SetRenderDrawColor(renderer, 240, 244, 250, (Uint8)(clampf(snow, 0.0f, 1.0f) * 200.0f));
    for (int y = visible.y0; y < visible.y1; y++) {
        int run = -1; /* First tile of the current run of snowy tiles. */
        for (int x = visible.x0; x <= visible.x1; x++) {
            bool snowy = x < visible.x1 && !map_is_water(map, x, y, z) &&
                         map->rock[map_index(map, x, y, z)] == ROCK_NONE &&
                         map_has_floor(map, x, y, z) &&
                         temperature_cover(map, x, y, z) == TEMPERATURE_COVER_OPEN;
            if (snowy && run < 0) {
                run = x;
            } else if (!snowy && run >= 0) {
                SDL_FRect first = tile_rect(camera, run, y);
                SDL_FRect last = tile_rect(camera, x - 1, y);
                SDL_FRect rect = {first.x, first.y, last.x + last.w - first.x, first.h};
                SDL_RenderFillRect(renderer, &rect);
                run = -1;
            }
        }
    }
}

/* A number in [0, 1) from an index and a salt, for falling drops. */
static float particle_roll(uint32_t index, uint32_t salt)
{
    uint32_t h = index * 0x9e3779b1u ^ salt * 0x85ebca77u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return (float)(h >> 8) / (float)(1u << 24);
}

void render_weather(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                    Uint64 now_ms)
{
    WeatherType type = (WeatherType)game->weather.type;
    float width = camera->viewport_width, height = camera->viewport_height;
    float seconds = (float)(now_ms % 600000u) / 1000.0f;
    if (type == WEATHER_FOG) {
        SDL_SetRenderDrawColor(renderer, 205, 210, 215, 90);
        SDL_FRect screen = {0.0f, 0.0f, width, height};
        SDL_RenderFillRect(renderer, &screen);
        return;
    }
    Precipitation falling = weather_precipitation(&game->weather, game->clock.tick);
    if (falling == PRECIPITATION_NONE) {
        return;
    }
    bool storm = type == WEATHER_STORM;
    bool snow = falling == PRECIPITATION_SNOW;
    /* As many drops on any screen: so many per million pixels. */
    float density = snow ? 300.0f : storm ? 600.0f : 300.0f;
    int count = (int)(density * width * height / 1.0e6f);
    float speed = snow ? 60.0f : storm ? 1100.0f : 800.0f; /* Pixels a second. */
    float wind = snow ? 15.0f : storm ? 0.35f * speed : 0.08f * speed;
    float length = snow ? 0.0f : storm ? 22.0f : 16.0f;
    if (snow) {
        SDL_SetRenderDrawColor(renderer, 245, 248, 255, 220);
    } else {
        SDL_SetRenderDrawColor(renderer, 170, 190, 220, 150);
    }
    for (int i = 0; i < count; i++) {
        float fall = (height + length) * (0.6f + 0.4f * particle_roll((uint32_t)i, 3));
        float y = fmodf(particle_roll((uint32_t)i, 1) * (height + length) +
                            seconds * speed * (fall / (height + length)),
                        height + length) -
                  length;
        float drift = wind * (y + length) / speed;
        float x = fmodf(particle_roll((uint32_t)i, 2) * width + drift, width);
        if (snow) {
            x += 6.0f * sinf(seconds * 1.5f + (float)i); /* Flakes sway. */
            SDL_FRect flake = {x, y, 3.0f, 3.0f};
            SDL_RenderFillRect(renderer, &flake);
        } else {
            float slant = length * wind / speed;
            SDL_RenderLine(renderer, x, y, x + slant, y + length);
        }
    }
    /* Now and then, lightning: one flash somewhere in each 7 second window. */
    if (storm && !snow) {
        uint32_t window = (uint32_t)(now_ms / 7000u);
        Uint64 flash = (Uint64)window * 7000u + (Uint64)(particle_roll(window, 4) * 6800.0f);
        if (now_ms >= flash && now_ms < flash + 120u) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 110);
            SDL_FRect screen = {0.0f, 0.0f, width, height};
            SDL_RenderFillRect(renderer, &screen);
        }
    }
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
    case ITEM_THRUSTING_SPEAR:
    case ITEM_THROWING_SPEAR_HARDENED:
    case ITEM_THRUSTING_SPEAR_HARDENED: {
        /* A long shaft with a sharpened tip; the thrusting spear is thicker,
         * a fire-hardened one darker with a charred tip. */
        bool thrusting = type == ITEM_THRUSTING_SPEAR || type == ITEM_THRUSTING_SPEAR_HARDENED;
        bool hardened =
            type == ITEM_THROWING_SPEAR_HARDENED || type == ITEM_THRUSTING_SPEAR_HARDENED;
        SDL_Color wood = hardened ? HARDENED_SPEAR_COLOR : SPEAR_COLOR;
        float thickness = SDL_max(1.0f, floorf(h * (thrusting ? 0.12f : 0.08f)));
        SDL_FRect shaft = {tile->x + floorf(w * 0.08f), tile->y + floorf(h * 0.5f),
                           floorf(w * 0.72f), thickness};
        fill_outlined(renderer, shaft, wood);
        SDL_FRect tip = {shaft.x + shaft.w, shaft.y, floorf(w * 0.12f), thickness};
        fill_outlined(renderer, tip, shade(wood, hardened ? -90 : -50));
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
    case ITEM_BLACKBERRIES:
    case ITEM_HAZELNUTS: {
        /* A handful of berries, or a few nuts. */
        static const float SPOTS[5][2] = {
            {0.3f, 0.5f}, {0.5f, 0.45f}, {0.42f, 0.62f}, {0.58f, 0.6f}, {0.38f, 0.36f}};
        bool nuts = type == ITEM_HAZELNUTS;
        float size = SDL_max(1.0f, floorf(w * (nuts ? 0.18f : 0.16f)));
        for (int i = 0; i < 5; i++) {
            SDL_FRect spot = {tile->x + floorf(w * SPOTS[i][0]),
                              tile->y + floorf(h * SPOTS[i][1]), size, size};
            fill_outlined(renderer, spot, shade(nuts ? HAZELNUT_COLOR : BERRY_COLOR, i * 8));
        }
        break;
    }
    case ITEM_MUSHROOMS: {
        /* Three caps on pale stems. */
        static const float CAPS[3][2] = {{0.24f, 0.42f}, {0.5f, 0.34f}, {0.42f, 0.56f}};
        float cap_w = SDL_max(2.0f, floorf(w * 0.24f));
        float cap_h = SDL_max(1.0f, floorf(h * 0.1f));
        for (int i = 0; i < 3; i++) {
            float x = tile->x + floorf(w * CAPS[i][0]), y = tile->y + floorf(h * CAPS[i][1]);
            SDL_FRect stem = {x + floorf(cap_w * 0.35f), y + cap_h,
                              SDL_max(1.0f, floorf(cap_w * 0.3f)), cap_h};
            fill_outlined(renderer, stem, MUSHROOM_STEM_COLOR);
            SDL_FRect cap = {x, y, cap_w, cap_h};
            fill_outlined(renderer, cap, shade(MUSHROOM_CAP_COLOR, i * 10));
        }
        break;
    }
    case ITEM_WILD_GARLIC:
    case ITEM_NETTLES:
    case ITEM_PLANTAIN: {
        /* A bunch of leaves: broad for wild garlic, narrow for plantain. */
        SDL_Color color = type == ITEM_WILD_GARLIC ? WILD_GARLIC_COLOR
                          : type == ITEM_NETTLES   ? NETTLE_COLOR
                                                   : PLANTAIN_COLOR;
        float leaf_w = SDL_max(1.0f, floorf(w * (type == ITEM_PLANTAIN ? 0.09f : 0.14f)));
        for (int i = 0; i < 4; i++) {
            float top = (i % 2 == 0) ? 0.3f : 0.38f;
            SDL_FRect leaf = {tile->x + floorf(w * (0.24f + 0.13f * (float)i)),
                              tile->y + floorf(h * top), leaf_w, floorf(h * (0.72f - top))};
            fill_outlined(renderer, leaf, shade(color, (i % 3) * 12));
        }
        break;
    }
    case ITEM_WILD_CARROTS:
    case ITEM_CATTAIL_RHIZOMES:
    case ITEM_ROASTED_CARROTS:
    case ITEM_ROASTED_RHIZOMES: {
        /* Thin pale roots lying side by side, or thick knobbly rhizomes;
         * browned with charred spots once roasted. */
        bool rhizomes = type == ITEM_CATTAIL_RHIZOMES || type == ITEM_ROASTED_RHIZOMES;
        bool roasted = type == ITEM_ROASTED_CARROTS || type == ITEM_ROASTED_RHIZOMES;
        SDL_Color color = shade(rhizomes ? RHIZOME_COLOR : WILD_CARROT_COLOR, roasted ? -70 : 0);
        float thickness = SDL_max(1.0f, floorf(h * (rhizomes ? 0.13f : 0.08f)));
        for (int i = 0; i < 3; i++) {
            SDL_FRect root = {tile->x + floorf(w * (0.18f + 0.06f * (float)(i % 2))),
                              tile->y + floorf(h * (0.34f + 0.14f * (float)i)),
                              floorf(w * (rhizomes ? 0.58f : 0.5f)), thickness};
            fill_outlined(renderer, root, shade(color, -i * 10));
            if (roasted) {
                SDL_FRect spot = {root.x + floorf(root.w * (0.3f + 0.2f * (float)i)), root.y,
                                  SDL_max(1.0f, floorf(root.w * 0.15f)), thickness};
                SDL_SetRenderDrawColor(renderer, 40, 28, 20, 255);
                SDL_RenderFillRect(renderer, &spot);
            }
        }
        break;
    }
    case ITEM_DIGGING_STICK:
    case ITEM_DIGGING_STICK_HARDENED: {
        /* A short stout stick with a point; darker once hardened in the fire. */
        bool hardened = type == ITEM_DIGGING_STICK_HARDENED;
        SDL_Color wood = hardened ? HARDENED_SPEAR_COLOR : SPEAR_COLOR;
        float thickness = SDL_max(1.0f, floorf(h * 0.13f));
        SDL_FRect shaft = {tile->x + floorf(w * 0.18f), tile->y + floorf(h * 0.48f),
                           floorf(w * 0.5f), thickness};
        fill_outlined(renderer, shaft, wood);
        SDL_FRect tip = {shaft.x + shaft.w, shaft.y, floorf(w * 0.12f), thickness};
        fill_outlined(renderer, tip, shade(wood, hardened ? -90 : -50));
        break;
    }
    default: break;
    }
}

void render_items(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
    const Items *items = &game->items;
    TileRect visible = camera_visible_tiles(camera, items->width, items->height);
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            ItemStack stack = items_get(items, x, y, z);
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
    const int z = camera->level; /* The level shown. */
    const Designations *designations = &game->designations;
    if (designations->count == 0) {
        return;
    }
    const Map *map = &game->map;
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    float thickness = camera->zoom >= 24.0f ? 2.0f : 1.0f;

    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            DesignationType type = designation_get(designations, x, y, z);
            if (type == DESIGNATION_NONE) {
                continue;
            }
            SDL_FRect tile = tile_rect(camera, x, y);
            SDL_FRect mark = inset(&tile, 0.8f);
            outline(renderer, mark, thickness, DESIGNATION_COLORS[type]);

            float progress = designation_progress_fraction(designations, map, x, y, z);
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

/* Zone index of a tile, or -1 outside any zone and off the map. */
typedef int (*ZoneAt)(const void *zones, int x, int y, int z);

static int stockpile_zone_at(const void *zones, int x, int y, int z)
{
    return stockpile_at(zones, x, y, z);
}

static int gather_zone_zone_at(const void *zones, int x, int y, int z)
{
    return gather_zone_at(zones, x, y, z);
}

/* The zones of the level shown as a translucent overlay with their borders;
 * the selected one (index, or -1) is brighter. */
static void render_zone_layer(SDL_Renderer *renderer, const Camera *camera, const void *zones,
                              ZoneAt zone_at, int width, int height, int selected,
                              SDL_Color color)
{
    const int z = camera->level; /* The level shown. */
    TileRect visible = camera_visible_tiles(camera, width, height);
    SDL_Color normal = color, highlight = color;
    normal.a = 45;
    highlight.a = 90;
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            int index = zone_at(zones, x, y, z);
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
            if (zone_at(zones, x - 1, y, z) != index) {
                SDL_RenderLine(renderer, l, t, l, b);
            }
            if (zone_at(zones, x + 1, y, z) != index) {
                SDL_RenderLine(renderer, r, t, r, b);
            }
            if (zone_at(zones, x, y - 1, z) != index) {
                SDL_RenderLine(renderer, l, t, r, t);
            }
            if (zone_at(zones, x, y + 1, z) != index) {
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
    render_zone_layer(renderer, camera, stockpiles, stockpile_zone_at, stockpiles->width,
                      stockpiles->height, selected, STOCKPILE_COLOR);
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
    const int z = camera->level; /* The level shown. */
    const GatherZones *zones = &game->gather_zones;
    if (zones->used_count == 0) {
        return;
    }
    render_zone_layer(renderer, camera, zones, gather_zone_zone_at, zones->width, zones->height,
                      selected, GATHER_ZONE_COLOR);
    /* Progress of the job under way on each tile. */
    TileRect visible = camera_visible_tiles(camera, zones->width, zones->height);
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            float progress = gather_zone_progress_fraction(zones, &game->map, x, y, z);
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


/* An open hearth: a ring of stones around a pit with ashes. */
static void render_hearth_shape(SDL_Renderer *renderer, const SDL_FRect *tile, Uint8 alpha)
{
    SDL_Color pit = PIT_COLOR, ash = ASH_COLOR, edge = ITEM_OUTLINE;
    pit.a = ash.a = edge.a = alpha;
    SDL_FRect hole = inset(tile, 0.6f);
    fill(renderer, pit, &hole);
    SDL_FRect ashes = inset(tile, 0.3f);
    fill(renderer, ash, &ashes);
    float size = SDL_max(2.0f, floorf(tile->w * 0.18f));
    float cx = tile->x + tile->w * 0.5f, cy = tile->y + tile->h * 0.5f;
    for (int i = 0; i < 8; i++) {
        float angle = (float)i * (SDL_PI_F / 4.0f);
        SDL_FRect stone = {floorf(cx + cosf(angle) * tile->w * 0.33f - size * 0.5f),
                           floorf(cy + sinf(angle) * tile->h * 0.33f - size * 0.5f), size, size};
        fill(renderer, edge, &stone);
        SDL_Color color = shade(ANVIL_COLOR, (i % 3) * 12 - 12);
        color.a = alpha;
        SDL_FRect top = {stone.x + 1.0f, stone.y + 1.0f, stone.w - 2.0f, stone.h - 2.0f};
        fill(renderer, color, &top);
    }
}

/* Flames over the middle of a tile, flickering with `time` (seconds), as
 * tall as `strength` (0..1) allows. */
static void render_flames(SDL_Renderer *renderer, const SDL_FRect *tile, float time,
                          float strength)
{
    float width = SDL_max(2.0f, floorf(tile->w * 0.12f));
    float cx = tile->x + tile->w * 0.5f, base = tile->y + floorf(tile->h * 0.58f);
    for (int k = 0; k < 3; k++) {
        float phase = time * 9.0f + (float)k * 2.1f;
        float tall = (k == 1 ? 0.3f : 0.2f) + 0.06f * sinf(phase) + 0.03f * sinf(phase * 2.7f);
        float height = SDL_max(2.0f, floorf(tile->h * tall * (0.5f + 0.5f * strength)));
        SDL_FRect flame = {floorf(cx + ((float)k - 1.0f) * width * 0.9f - width * 0.5f),
                           base - height, width, height};
        fill(renderer, FLAME_COLOR, &flame);
        SDL_FRect core = {flame.x + floorf(width * 0.25f), flame.y + floorf(height * 0.35f),
                          SDL_max(1.0f, floorf(width * 0.5f)), floorf(height * 0.65f)};
        fill(renderer, FLAME_CORE_COLOR, &core);
    }
}

/* The fuel in a hearth (up to three sticks across the pit) and its fire. */
static void render_hearth_fire(SDL_Renderer *renderer, const SDL_FRect *tile,
                               const Workstation *station)
{
    float fill_fraction = clampf(station->fuel / HEARTH_FUEL_CAPACITY, 0.0f, 1.0f);
    int sticks = station->fuel > 0.0f ? 1 + (int)(fill_fraction * 2.99f) : 0;
    float thickness = SDL_max(1.0f, floorf(tile->h * 0.07f));
    for (int i = 0; i < sticks; i++) {
        float offset = ((float)i - (float)(sticks - 1) * 0.5f) * thickness * 1.6f;
        SDL_FRect stick = {tile->x + floorf(tile->w * 0.3f),
                           floorf(tile->y + tile->h * 0.54f + offset), floorf(tile->w * 0.4f),
                           thickness};
        fill_outlined(renderer, stick, shade(BRANCH_COLOR, -i * 14));
    }
    if (station->burning) {
        /* Real time: flames keep moving while the game is paused. */
        float time = (float)(SDL_GetTicks() % 100000u) / 1000.0f;
        render_flames(renderer, tile, time, clampf(station->fuel / 5.0f, 0.0f, 1.0f));
    }
}

/* The station itself: a mat to sit on; a knapping spot adds a flat anvil stone;
 * an open hearth is a ring of stones. */
static void render_station_shape(SDL_Renderer *renderer, const SDL_FRect *tile,
                                 WorkstationType type, Uint8 alpha)
{
    if (workstation_def(type)->hearth) {
        render_hearth_shape(renderer, tile, alpha);
        return;
    }
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
        if (station == NULL || station->z != camera->level || station->x < visible.x0 ||
            station->x >= visible.x1 || station->y < visible.y0 || station->y >= visible.y1) {
            continue;
        }
        SDL_FRect tile = tile_rect(camera, station->x, station->y);
        render_station_shape(renderer, &tile, (WorkstationType)station->type, 255);
        if (hearth_is(station)) {
            render_hearth_fire(renderer, &tile, station);
        }
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
    const int z = camera->level; /* The level shown. */
    SDL_FRect tile = tile_rect(camera, x, y);
    bool allowed = game_can_place_workstation(game, x, y, z);
    if (allowed) {
        render_station_shape(renderer, &tile, type, 110);
    }
    outline(renderer, tile, 2.0f, allowed ? BLUEPRINT_COLOR : BLUEPRINT_REFUSED_COLOR);
}

void render_workstation_icon(SDL_Renderer *renderer, SDL_FRect rect, WorkstationType type)
{
    render_station_shape(renderer, &rect, type, 255);
    if (workstation_def(type)->hearth) {
        render_flames(renderer, &rect, 0.0f, 1.0f);
    }
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
    case STRUCTURE_PROP: {
        /* Seen from above: the cross piece under the ceiling, the post's top
         * in the middle. */
        float thick = SDL_max(2.0f, floorf(w * 0.16f));
        SDL_FRect beam = {tile->x + floorf(w * 0.1f), tile->y + floorf((h - thick) * 0.5f),
                          w - 2.0f * floorf(w * 0.1f), thick};
        fill(renderer, edge, &beam);
        SDL_FRect inner = {beam.x + 1.0f, beam.y + 1.0f, beam.w - 2.0f, beam.h - 2.0f};
        fill(renderer, wood, &inner);
        float post = SDL_max(3.0f, floorf(w * 0.34f));
        SDL_FRect top = {tile->x + floorf((w - post) * 0.5f), tile->y + floorf((h - post) * 0.5f),
                         post, post};
        fill(renderer, edge, &top);
        SDL_FRect core = {top.x + 1.0f, top.y + 1.0f, top.w - 2.0f, top.h - 2.0f};
        fill(renderer, shade(wood, 20), &core);
        break;
    }
    case STRUCTURE_LADDER: {
        /* Two side poles and their rungs, seen leaning from above. */
        float pole = SDL_max(2.0f, floorf(w * 0.1f));
        SDL_FRect left = {tile->x + floorf(w * 0.22f), tile->y + 1.0f, pole, h - 2.0f};
        SDL_FRect right = {tile->x + w - floorf(w * 0.22f) - pole, tile->y + 1.0f, pole, h - 2.0f};
        float rung = SDL_max(1.0f, floorf(h * 0.07f));
        for (int i = 0; i < 4; i++) {
            SDL_FRect bar = {left.x, tile->y + floorf(h * (0.16f + 0.22f * (float)i)),
                             right.x + pole - left.x, rung};
            fill(renderer, wood, &bar);
        }
        fill(renderer, edge, &left);
        fill(renderer, edge, &right);
        SDL_FRect left_core = {left.x + 0.5f, left.y, left.w - 1.0f, left.h};
        SDL_FRect right_core = {right.x + 0.5f, right.y, right.w - 1.0f, right.h};
        fill(renderer, wood, &left_core);
        fill(renderer, wood, &right_core);
        break;
    }
    case STRUCTURE_ROOF: {
        /* Rows of thatch, overlapping: they line up from one tile to the next. */
        fill(renderer, straw, tile);
        float step = SDL_max(3.0f, floorf(h / 4.0f));
        for (float y = tile->y + step - 1.0f; y < tile->y + h; y += step) {
            SDL_FRect row = {tile->x, y, w, SDL_max(1.0f, floorf(step * 0.25f))};
            fill(renderer, dark, &row);
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
    const int z = camera->level; /* The level shown. */
    const Map *map = &game->map;
    if (map->structure_counts[STRUCTURE_NONE] < (int)map_tile_count(map)) {
        TileRect visible = visible_heads(camera, map->width, map->height);
        for (int y = visible.y0; y < visible.y1; y++) {
            for (int x = visible.x0; x < visible.x1; x++) {
                if (map_structure_is_origin(map, x, y, z)) {
                    StructureType type = map_structure(map, x, y, z);
                    Direction direction = map_structure_direction(map, x, y, z);
                    SDL_FRect rect = footprint_rect(camera, x, y, type, direction);
                    render_structure_shape(renderer, &rect, type, direction, 255);
                }
            }
        }
    }
    /* Built roofs: the floor of their level, over the huts below. */
    if (map->roof_count > 0) {
        TileRect visible = camera_visible_tiles(camera, map->width, map->height);
        for (int y = visible.y0; y < visible.y1; y++) {
            for (int x = visible.x0; x < visible.x1; x++) {
                StructureType roof = map_roof(map, x, y, z);
                if (roof != STRUCTURE_NONE) {
                    SDL_FRect rect = tile_rect(camera, x, y);
                    render_structure_shape(renderer, &rect, roof, DIRECTION_SOUTH, 255);
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
        if (site == NULL || site->z != camera->level || site->x < visible.x0 ||
            site->x >= visible.x1 || site->y < visible.y0 || site->y >= visible.y1) {
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

void render_roof_support(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
    const Map *map = &game->map;
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            if (map_is_open(map, x, y, z) &&
                construction_roof_held(&game->constructions, map, x, y, z)) {
                SDL_FRect rect = tile_rect(camera, x, y);
                fill(renderer, ROOF_SUPPORT_COLOR, &rect);
            }
        }
    }
}

typedef struct StructurePreview {
    SDL_Renderer *renderer;
    const Camera *camera;
    StructureType type;
    Direction direction;
} StructurePreview;

static void preview_planned(int x, int y, int z, bool allowed, void *userdata)
{
    (void)z; /* The level shown. */
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
    const int z = camera->level; /* The level shown. */
    StructurePreview preview = {renderer, camera, type, direction};
    game_plan_structure_rect(game, type, direction, x0, y0, x1, y1, z, preview_planned, &preview);
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
    case RENDER_ICON_DIG:
    case RENDER_ICON_DIG_RAMP: {
        SDL_FRect area = inset(&rect, 0.8f);
        fill(renderer, ROCK_COLORS[ROCK_EARTH], &area);
        if (icon == RENDER_ICON_DIG) { /* A dark hole dug into it. */
            SDL_FRect hole = {area.x + area.w * 0.3f, area.y + area.h * 0.35f, area.w * 0.4f,
                              area.h * 0.4f};
            fill(renderer, (SDL_Color){40, 28, 20, 255}, &hole);
        } else { /* A ramp rising to the right, lit. */
            SDL_SetRenderDrawColor(renderer, 196, 160, 112, 255);
            float rise = area.h * 0.6f;
            for (float row = 0.0f; row < rise; row += 1.0f) {
                float left = area.x + area.w * 0.15f + area.w * 0.7f * (1.0f - row / rise);
                SDL_RenderLine(renderer, left, area.y + area.h * 0.8f - row,
                               area.x + area.w * 0.85f, area.y + area.h * 0.8f - row);
            }
        }
        outline(renderer, area, 2.0f,
                DESIGNATION_COLORS[icon == RENDER_ICON_DIG ? DESIGNATION_DIG
                                                           : DESIGNATION_DIG_RAMP]);
        break;
    }
    case RENDER_ICON_DIG_DOWN: { /* A hole in the ground, an arrow into it. */
        SDL_FRect area = inset(&rect, 0.8f);
        fill(renderer, ROCK_COLORS[ROCK_EARTH], &area);
        SDL_FRect hole = {area.x + area.w * 0.2f, area.y + area.h * 0.55f, area.w * 0.6f,
                          area.h * 0.3f};
        fill(renderer, (SDL_Color){40, 28, 20, 255}, &hole);
        SDL_SetRenderDrawColor(renderer, 236, 220, 190, 255);
        float cx = area.x + area.w * 0.5f;
        for (float d = -1.0f; d <= 1.0f; d += 1.0f) {
            SDL_RenderLine(renderer, cx + d, area.y + area.h * 0.12f, cx + d,
                           area.y + area.h * 0.6f);
        }
        for (float d = 0.0f; d < area.w * 0.18f; d += 1.0f) {
            SDL_RenderLine(renderer, cx - d, area.y + area.h * 0.6f - d, cx + d,
                           area.y + area.h * 0.6f - d);
        }
        outline(renderer, area, 2.0f, DESIGNATION_COLORS[DESIGNATION_DIG_DOWN]);
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

/* ---- Night ---------------------------------------------------------------- */

/* How much of the darkness the full light of a fire takes away, and how
 * strong its warm glow is. */
#define FIRE_LIGHT_STRENGTH 0.85f
#define FIRE_GLOW_STRENGTH 0.3f

/* Tiles that burning hearths of level z light, within `visible` (empty: x0 >=
 * x1). */
static TileRect lit_tiles(const Workstations *workstations, TileRect visible, int z)
{
    TileRect box = {INT_MAX, INT_MAX, INT_MIN, INT_MIN};
    int reach = (int)ceilf(HEARTH_LIGHT_RADIUS);
    for (int i = 0; i < WORKSTATION_MAX; i++) {
        const Workstation *station = workstation_get_const(workstations, i);
        if (station == NULL || !station->burning || station->z != z) {
            continue;
        }
        box.x0 = SDL_min(box.x0, station->x - reach);
        box.y0 = SDL_min(box.y0, station->y - reach);
        box.x1 = SDL_max(box.x1, station->x + reach + 1);
        box.y1 = SDL_max(box.y1, station->y + reach + 1);
    }
    box.x0 = SDL_max(box.x0, visible.x0);
    box.y0 = SDL_max(box.y0, visible.y0);
    box.x1 = SDL_min(box.x1, visible.x1);
    box.y1 = SDL_min(box.y1, visible.y1);
    return box;
}

/* One quad per tile of `box`, each corner colored `color` with an alpha from
 * the light there. */
static void lit_quads(SDL_Renderer *renderer, const Camera *camera, TileRect box,
                      SDL_Color color, float darkness, bool glow)
{
    int columns = box.x1 - box.x0 + 1;
    float flicker = 0.95f + 0.05f * sinf((float)(SDL_GetTicks() % 100000u) * 0.011f);
    for (int y = box.y0; y < box.y1; y++) {
        for (int x = box.x0; x < box.x1; x++) {
            int corner = (y - box.y0) * columns + (x - box.x0);
            float light[4] = {light_corners[corner], light_corners[corner + 1],
                              light_corners[corner + columns + 1],
                              light_corners[corner + columns]};
            if (glow && light[0] + light[1] + light[2] + light[3] <= 0.0f) {
                continue;
            }
            SDL_Color colors[4];
            for (int i = 0; i < 4; i++) {
                float alpha = glow ? darkness * FIRE_GLOW_STRENGTH * light[i] * flicker
                                   : darkness * (1.0f - FIRE_LIGHT_STRENGTH * light[i] * flicker);
                colors[i] = color;
                colors[i].a = (Uint8)(clampf(alpha, 0.0f, 1.0f) * 255.0f);
            }
            batch_gradient_quad(renderer, colors, tile_rect(camera, x, y));
        }
    }
    batch_flush(renderer);
}

/* Colors of the temperature view, every 10 degrees from -10 to 30 °C. */
#define TEMPERATURE_VIEW_COLDEST (-10.0f)
#define TEMPERATURE_VIEW_STEP 10.0f
static const SDL_Color TEMPERATURE_COLORS[] = {
    {40, 60, 200, 255}, {60, 150, 235, 255}, {90, 200, 150, 255}, {240, 210, 70, 255},
    {225, 60, 40, 255},
};

static SDL_Color temperature_color(float celsius)
{
    const int last = (int)SDL_arraysize(TEMPERATURE_COLORS) - 1;
    float position = clampf((celsius - TEMPERATURE_VIEW_COLDEST) / TEMPERATURE_VIEW_STEP, 0.0f,
                            (float)last);
    int from = SDL_min((int)position, last - 1);
    return mix(TEMPERATURE_COLORS[from], TEMPERATURE_COLORS[from + 1], position - (float)from);
}

/* Whether a tile is within reach of the warmth of a burning hearth. */
static bool near_fire(const Workstations *workstations, int x, int y, int z)
{
    for (int i = 0; i < WORKSTATION_MAX; i++) {
        const Workstation *station = &workstations->list[i];
        if (station->used && station->burning && station->z == z &&
            abs(station->x - x) <= HEARTH_RADIANT_RADIUS &&
            abs(station->y - y) <= HEARTH_RADIANT_RADIUS) {
            return true;
        }
    }
    return false;
}

void render_temperature(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
    const Temperatures *temperatures = &game->temperatures;
    const Workstations *workstations = &game->workstations;
    TileRect visible = camera_visible_tiles(camera, temperatures->width, temperatures->height);
    /* Burning hearths are few: tiles away from all of them only have the air's. */
    bool burning = false;
    for (int i = 0; i < WORKSTATION_MAX && !burning; i++) {
        burning = workstations->list[i].used && workstations->list[i].burning;
    }
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            if (map_is_open(&game->map, x, y, z)) {
                continue; /* Nothing here: the levels below show. */
            }
            float celsius = temperature_at(temperatures, x, y, z);
            if (burning && near_fire(workstations, x, y, z)) {
                celsius += hearths_radiant_warmth(workstations, &game->map, x, y, z);
            }
            SDL_Color color = temperature_color(celsius);
            color.a = 120;
            SDL_FRect tile = tile_rect(camera, x, y);
            batch_quad(renderer, color, &tile);
        }
    }
    batch_flush(renderer);
}

void render_daylight(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    /* Clouds and rain dim the day (and the night a little more). */
    float light = game_daylight(game->clock.tick) * weather_light(&game->weather);
    float darkness = 1.0f - clampf(light, 0.0f, 1.0f);
    if (darkness <= 0.0f) {
        return;
    }
    Uint8 alpha = (Uint8)(darkness * 255.0f);
    SDL_SetRenderDrawColor(renderer, NIGHT_COLOR.r, NIGHT_COLOR.g, NIGHT_COLOR.b, alpha);
    float width = camera->viewport_width, height = camera->viewport_height;
    const Workstations *workstations = &game->workstations;
    TileRect visible = camera_visible_tiles(camera, workstations->width, workstations->height);
    TileRect box = lit_tiles(workstations, visible, camera->level);
    size_t corners = box.x0 < box.x1 && box.y0 < box.y1
                         ? (size_t)(box.x1 - box.x0 + 1) * (size_t)(box.y1 - box.y0 + 1)
                         : 0;
    if (corners > light_corner_capacity) {
        float *grown = SDL_realloc(light_corners, corners * sizeof(*light_corners));
        if (grown != NULL) {
            light_corners = grown;
            light_corner_capacity = corners;
        }
    }
    if (corners == 0 || corners > light_corner_capacity) {
        SDL_FRect screen = {0.0f, 0.0f, width, height};
        SDL_RenderFillRect(renderer, &screen);
        return;
    }
    /* Plain darkness around the lit area, a gradient inside it. */
    SDL_FRect first = tile_rect(camera, box.x0, box.y0);
    SDL_FRect last = tile_rect(camera, box.x1 - 1, box.y1 - 1);
    float left = first.x, top = first.y, right = last.x + last.w, bottom = last.y + last.h;
    SDL_FRect around[4] = {{0.0f, 0.0f, width, top},
                           {0.0f, bottom, width, height - bottom},
                           {0.0f, top, left, bottom - top},
                           {right, top, width - right, bottom - top}};
    for (int i = 0; i < 4; i++) {
        if (around[i].w > 0.0f && around[i].h > 0.0f) {
            SDL_RenderFillRect(renderer, &around[i]);
        }
    }
    int columns = box.x1 - box.x0 + 1;
    for (int y = box.y0; y <= box.y1; y++) {
        for (int x = box.x0; x <= box.x1; x++) {
            light_corners[(y - box.y0) * columns + (x - box.x0)] =
                hearths_light_at(workstations, (float)x, (float)y, camera->level);
        }
    }
    lit_quads(renderer, camera, box, NIGHT_COLOR, darkness, false);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    lit_quads(renderer, camera, box, GLOW_COLOR, darkness, true);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
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
/* Speech bubbles over chatting colonists from this zoom level (pixels per tile). */
#define CHAT_BUBBLE_MIN_ZOOM 14.0f
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
    if (plant->type == PLANT_NONE || plant_is_dormant(plant)) {
        return; /* Nothing above ground. */
    }
    SpriteId id = sprite_plant((PlantType)plant->type, plant_growth_stage(plant),
                               plant_has_food(plant));
    if (id < 0) {
        return; /* Grass is part of the ground. */
    }
    float sx, sy;
    camera_world_to_screen(camera, (float)x + 0.5f, (float)(y + 1), &sx, &sy);
    SDL_FColor color = WHITE;
    bool tall = plant->type == PLANT_TREE || plant->type == PLANT_HAZEL;
    if (tall && colonist_count > 0) {
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

/* A small speech bubble over the head of colonists who are chatting. */
static void render_chat_bubbles(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    if (camera->zoom < CHAT_BUBBLE_MIN_ZOOM) {
        return;
    }
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = &game->colonists[slot];
        if (entity_id_is_none(game_colonist_id_at(game, slot)) || colonist->z != camera->level ||
            (colonist->chat_ticks <= 0 && !colonist_telling(colonist, &game->workstations))) {
            continue;
        }
        float wx, wy, sx, sy;
        colonist_position(colonist, &wx, &wy);
        camera_world_to_screen(camera, wx, wy + COLONIST_FEET, &sx, &sy);
        float width = floorf(camera->zoom * 0.42f), height = floorf(camera->zoom * 0.24f);
        SDL_FRect bubble = {floorf(sx + camera->zoom * 0.08f),
                            floorf(sy - COLONIST_HEIGHT * camera->zoom - height * 0.6f), width,
                            height};
        SDL_SetRenderDrawColor(renderer, 245, 245, 238, 230);
        SDL_RenderFillRect(renderer, &bubble);
        SDL_FRect tail = {bubble.x + 1.0f, bubble.y + height, SDL_max(2.0f, width * 0.18f),
                          SDL_max(2.0f, height * 0.3f)};
        SDL_RenderFillRect(renderer, &tail);
        float dot = SDL_max(1.0f, floorf(height * 0.22f));
        SDL_SetRenderDrawColor(renderer, 60, 60, 66, 255);
        for (int i = 0; i < 3; i++) {
            SDL_FRect rect = {floorf(bubble.x + width * (0.25f + 0.25f * (float)i) - dot * 0.5f),
                              floorf(bubble.y + (height - dot) * 0.5f), dot, dot};
            SDL_RenderFillRect(renderer, &rect);
        }
    }
}

void render_world_sprites(SDL_Renderer *renderer, Sprites *sprites, const Game *game,
                          const Camera *camera, const EntityId *selected, int selected_count)
{
    const int z = camera->level; /* The level shown. */
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (!entity_id_is_none(id) && game->colonists[slot].z == z &&
            is_selected(id, selected, selected_count)) {
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
        if (entity_id_is_none(game_colonist_id_at(game, slot)) || game->colonists[slot].z != z) {
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
            render_plant(sprites, camera, &map->plants[map_index(map, x, y, z)], x, y, order,
                         count);
        }
    }
    while (next < count) {
        uint32_t slot = order[next++].slot;
        render_colonist(renderer, sprites, camera, &game->colonists[slot], slot, tick,
                        is_selected(game_colonist_id_at(game, slot), selected, selected_count));
    }
    sprites_flush(sprites);
    render_chat_bubbles(renderer, game, camera);
}

void render_selection_box(SDL_Renderer *renderer, float x0, float y0, float x1, float y1)
{
    SDL_FRect rect = {SDL_min(x0, x1), SDL_min(y0, y1), SDL_fabsf(x1 - x0), SDL_fabsf(y1 - y0)};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 40);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
    SDL_RenderRect(renderer, &rect);
}
