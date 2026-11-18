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
    [TERRAIN_GRASS_ROOF] = {196, 168, 96, 255},   /* Textured: sprite_ground(). */
    [TERRAIN_FOLIAGE] = {62, 106, 48, 255},       /* Textured: sprite_ground(). */
    [TERRAIN_FOLIAGE_MEDIUM] = {52, 90, 42, 255}, /* Textured: sprite_ground(). */
    [TERRAIN_FOLIAGE_SPARSE] = {42, 74, 36, 255}, /* Textured: sprite_ground(). */
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
/* The shade of the foliage above on the ground under it: darker by this
 * much, or times this for textured ground. */
#define FOLIAGE_SHADOW (-26)
#define FOLIAGE_SHADOW_TINT 0.74f
/* Growth rings are drawn from this zoom on (pixels per tile). */
#define TREE_DETAIL_MIN_ZOOM 10.0f

/* Levels below the one shown (render_below_begin()): drawn this many times
 * smaller, then stretched back with smoothing (a blur), and veiled towards
 * BELOW_HAZE the deeper they are. What lies `depth` levels down keeps
 * BELOW_FLOOR + (1 - BELOW_FLOOR) * BELOW_DECAY^depth of its color: a little
 * less each level, never less than BELOW_FLOOR (never all dark). */
#define BELOW_SCALE 3
#define BELOW_FLOOR 0.4f
#define BELOW_DECAY 0.8f
static const SDL_Color BELOW_HAZE = {14, 16, 24, 255};
static const SDL_Color BELOW_ABYSS = {14, 16, 24, 255}; /* Open space to the bottom. */
/* Edges of the ground over a drop to the levels below. */
/* Ramps: a triangle pointing up on a ramp leading up from the level shown,
 * down over one leading down from it (map_ramp_at()). */
static const SDL_Color RAMP_MARKER_COLOR = {244, 236, 212, 110};
static const SDL_Color RAMP_MARKER_EDGE = {28, 24, 20, 120};
#define RAMP_MARKER_SIZE 0.42f    /* Width, in tiles. */
#define RAMP_MARKER_MIN_ZOOM 6.0f /* Pixels per tile below which none is drawn. */
static const SDL_Color DROP_EDGE_COLOR = {16, 12, 8, 120};
/* Things forbidden to the settlers: a "no entry" sign in the tile's corner;
 * the allow tool's icon is a green sign with a tick. */
static const SDL_Color FORBID_COLOR = {214, 52, 44, 235};
static const SDL_Color FORBID_BACK_COLOR = {250, 244, 236, 210};
static const SDL_Color ALLOW_COLOR = {84, 176, 92, 255};
#define FORBID_MARKER_RADIUS 0.17f    /* In tiles. */
#define FORBID_MARKER_MIN_RADIUS 3.0f /* Pixels. */
#define FORBID_RING_SEGMENTS 16

/* The offscreen image of the levels below, and the render scale it replaced. */
static SDL_Texture *below_texture;
/* The pass over the levels below (render_below_begin() to render_below_end()):
 * the level seen on each visible tile (map_view_level()), so that each level
 * draws only the tiles where nothing higher covers it. */
static bool below_active;
static const Map *below_map;
static int below_shown;
static TileRect below_rect;
static int8_t *below_view;
static size_t below_view_capacity;
static float below_saved_scale_x = 1.0f, below_saved_scale_y = 1.0f;

static const SDL_Color TREE_COLOR = {34, 88, 40, 255};
static const SDL_FColor WHITE = {1.0f, 1.0f, 1.0f, 1.0f};

/* Tints of the (light gray) hair and beard sprites. */
static const SDL_Color HAIR_COLORS[HAIR_COLOR_COUNT] = {
    [HAIR_COLOR_BLACK] = {58, 46, 40, 255},    [HAIR_COLOR_DARK_BROWN] = {100, 66, 44, 255},
    [HAIR_COLOR_BROWN] = {146, 102, 64, 255},  [HAIR_COLOR_AUBURN] = {172, 86, 48, 255},
    [HAIR_COLOR_BLOND] = {226, 190, 118, 255}, [HAIR_COLOR_GRAY] = {188, 184, 178, 255},
};
/* Designation marks; index DESIGNATION_NONE is the color of the cancel tool. */
static const SDL_Color DESIGNATION_COLORS[DESIGNATION_COUNT] = {
    [DESIGNATION_NONE] = {230, 70, 60, 255},         [DESIGNATION_MINE] = {250, 140, 60, 255},
    [DESIGNATION_DECONSTRUCT] = {240, 200, 70, 255}, [DESIGNATION_DIG] = {250, 140, 60, 255},
    [DESIGNATION_DIG_RAMP] = {250, 170, 90, 255},    [DESIGNATION_DIG_DOWN] = {250, 120, 70, 255},
    [DESIGNATION_CHOP] = {240, 90, 80, 255},         [DESIGNATION_HARVEST] = {130, 210, 90, 255},
    [DESIGNATION_UPROOT] = {220, 120, 200, 255},
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
static void batch_gradient_quad(SDL_Renderer *renderer, const SDL_Color colors[4], SDL_FRect rect)
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

/* A filled convex shape of 3 or 4 corners in order, drawn with the batch (a
 * triangle is a quad with two corners at the same place). */
static void batch_shape(SDL_Renderer *renderer, SDL_Color color, const SDL_FPoint *points,
                        int count)
{
    if (!batch_ready()) {
        return;
    }
    if (batch_count == BATCH_MAX_QUADS) {
        batch_flush(renderer);
    }
    SDL_Vertex *vertex = &batch_vertices[batch_count * 4];
    SDL_FColor fcolor = to_fcolor(color);
    for (int i = 0; i < 4; i++) {
        vertex[i] = (SDL_Vertex){points[i < count ? i : count - 1], fcolor, {0.0f, 0.0f}};
    }
    batch_count++;
}

void render_shutdown(void)
{
    if (below_texture != NULL) {
        SDL_DestroyTexture(below_texture);
        below_texture = NULL;
    }
    SDL_free(below_view);
    below_view = NULL;
    below_view_capacity = 0;
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
        if (terrain_is_foliage(map->terrain[index]) && map->rock[index] == ROCK_NONE) {
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

/* The level seen on a tile from the level shown, during the pass. */
static int below_view_level(int x, int y)
{
    if (x >= below_rect.x0 && x < below_rect.x1 && y >= below_rect.y0 && y < below_rect.y1) {
        size_t columns = (size_t)(below_rect.x1 - below_rect.x0);
        return below_view[(size_t)(y - below_rect.y0) * columns + (size_t)(x - below_rect.x0)];
    }
    return map_view_level(below_map, x, y, below_shown); /* A sprite's foot out of view. */
}

/* Whether a tile is thin foliage (leaves and bare branches over nothing: the
 * levels below show through it, see map_view_level()). */
static bool is_thin_foliage(const Map *map, size_t index)
{
    return map->rock[index] == ROCK_NONE && (map->terrain[index] == TERRAIN_FOLIAGE_MEDIUM ||
                                             map->terrain[index] == TERRAIN_FOLIAGE_SPARSE);
}

/* Whether thin foliage on (x, y, z) is to be drawn: it is, over what the
 * levels below show, unless something higher than the level seen covers it. */
static bool thin_foliage_seen(int x, int y, int z)
{
    return !below_active || below_view_level(x, y) < z;
}

/* Whether what stands on (x, y, z) is to be drawn: always on the level shown;
 * on a level below, only where it is what is seen (not under a higher floor). */
static bool tile_seen(int x, int y, int z)
{
    return !below_active || below_view_level(x, y) == z;
}

/* Fills the table of the levels seen; returns the set of the levels below the
 * one shown that are seen somewhere (bit z for level z), 0 if none. */
static uint32_t below_fill_view(const Map *map, const Camera *camera)
{
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    if (visible.x1 <= visible.x0 || visible.y1 <= visible.y0) {
        return 0;
    }
    size_t count = (size_t)(visible.x1 - visible.x0) * (size_t)(visible.y1 - visible.y0);
    if (count > below_view_capacity) {
        int8_t *grown = SDL_realloc(below_view, count);
        if (grown == NULL) {
            return 0;
        }
        below_view = grown;
        below_view_capacity = count;
    }
    uint32_t levels = 0;
    size_t i = 0;
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            int level = map_view_level(map, x, y, camera->level);
            below_view[i++] = (int8_t)level;
            if (level >= 0 && level < camera->level) {
                levels |= 1u << level;
            }
            /* The thin foliage over what is seen is drawn too. */
            for (int z = camera->level - 1; z > level; z--) {
                if (is_thin_foliage(map, map_index(map, x, y, z))) {
                    levels |= 1u << z;
                }
            }
        }
    }
    below_rect = visible;
    return levels;
}

/* How much of its color what lies `depth` levels below the level shown keeps. */
static float below_brightness(int depth)
{
    return BELOW_FLOOR + (1.0f - BELOW_FLOOR) * powf(BELOW_DECAY, (float)depth);
}

bool render_below_begin(SDL_Renderer *renderer, const Map *map, const Camera *camera,
                        uint32_t *levels)
{
    SDL_COMPILE_TIME_ASSERT(below_levels_fit, MAP_MAX_LEVELS <= 32);
    *levels = 0;
    if (camera->level <= 0 || camera->level >= map->depth) {
        return false;
    }
    int width = (int)ceilf(camera->viewport_width / (float)BELOW_SCALE);
    int height = (int)ceilf(camera->viewport_height / (float)BELOW_SCALE);
    if (width <= 0 || height <= 0) {
        return false;
    }
    uint32_t seen = below_fill_view(map, camera);
    if (seen == 0) {
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
    below_active = true;
    below_map = map;
    below_shown = camera->level;
    *levels = seen;
    return true;
}

/* Each level drawn is veiled by its own shade and by those of the levels drawn
 * after it: a shade that takes it from its brightness to that of the next
 * level gives every level exactly below_brightness() of its depth, whichever
 * levels lie between. */
void render_below_shade(SDL_Renderer *renderer, int level, int next_level)
{
    float kept = below_brightness(below_shown - level) / below_brightness(below_shown - next_level);
    float alpha = clampf(1.0f - kept, 0.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, BELOW_HAZE.r, BELOW_HAZE.g, BELOW_HAZE.b,
                           (Uint8)(alpha * 255.0f + 0.5f));
    SDL_RenderFillRect(renderer, NULL);
}

void render_below_end(SDL_Renderer *renderer)
{
    below_active = false;
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderScale(renderer, below_saved_scale_x, below_saved_scale_y);
    SDL_RenderTexture(renderer, below_texture, NULL, NULL);
}

/* Screen rectangles of the visible tiles of the level shown, row by row,
 * their edges snapped to whole pixels so that neighboring tiles never leave
 * gaps; `visit` is called for each tile that is not open space (the levels
 * below show there, see render_below_begin()). */
typedef void (*TileVisitor)(const Map *map, int x, int y, int z, size_t index,
                            const SDL_FRect *tile, void *userdata);

static void visit_ground_tiles(const Map *map, const Camera *camera, TileVisitor visit,
                               void *userdata)
{
    const int z = camera->level;
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    for (int y = visible.y0; y < visible.y1; y++) {
        float top, bottom, left, right, unused;
        camera_world_to_screen(camera, 0.0f, (float)y, &unused, &top);
        camera_world_to_screen(camera, 0.0f, (float)(y + 1), &unused, &bottom);
        camera_world_to_screen(camera, (float)visible.x0, 0.0f, &right, &unused);
        for (int x = visible.x0; x < visible.x1; x++) {
            left = right; /* The right edge of the previous tile. */
            camera_world_to_screen(camera, (float)(x + 1), 0.0f, &right, &unused);
            SDL_FRect tile = {floorf(left), floorf(top), floorf(right) - floorf(left),
                              floorf(bottom) - floorf(top)};
            size_t index = map_index(map, x, y, z);
            if (map_is_open_at(map, index)) {
                continue;
            }
            if (is_thin_foliage(map, index) ? thin_foliage_seen(x, y, z) : tile_seen(x, y, z)) {
                visit(map, x, y, z, index, &tile, userdata);
            }
        }
    }
}

typedef struct GroundPass {
    SDL_Renderer *renderer;
    Sprites *sprites;
    float zoom;
} GroundPass;

/* First pass: the tiles without a texture, as flat colors. */
static void ground_flat(const Map *map, int x, int y, int z, size_t index, const SDL_FRect *tile,
                        void *userdata)
{
    const GroundPass *pass = userdata;
    RockType rock = (RockType)map->rock[index];
    if (rock == ROCK_WOOD) {
        render_trunk(pass->renderer, map, x, y, z, tile, pass->zoom);
    } else if (rock != ROCK_NONE) {
        if (sprite_wall(rock) < 0) {
            batch_quad(pass->renderer, shade(ROCK_COLORS[rock], tile_variation(x, y)), tile);
        }
    } else if (sprite_ground((Terrain)map->terrain[index]) < 0) {
        int shadow = under_foliage(map, x, y, z) ? FOLIAGE_SHADOW : 0;
        batch_quad(pass->renderer,
                   shade(TERRAIN_COLORS[map->terrain[index]], tile_variation(x, y) + shadow), tile);
    }
}

/* Second pass: tile textures, then grass over the ground. */
static void ground_textures(const Map *map, int x, int y, int z, size_t index,
                            const SDL_FRect *tile, void *userdata)
{
    const GroundPass *pass = userdata;
    RockType rock = (RockType)map->rock[index];
    if (rock != ROCK_NONE) {
        sprites_draw_tile(pass->sprites, sprite_wall(rock), tile, WHITE);
        return;
    }
    SDL_FColor color = WHITE;
    if (under_foliage(map, x, y, z)) {
        color.r = color.g = color.b = FOLIAGE_SHADOW_TINT;
    }
    sprites_draw_tile(pass->sprites, sprite_ground((Terrain)map->terrain[index]), tile, color);
    const Plant *plant = &map->plants[index];
    if (plant->type == PLANT_GRASS) {
        sprites_draw_tile(pass->sprites, sprite_grass(plant->growth), tile, color);
    }
}

/* Last pass: what goes over the ground (slopes, drop edges). */
static void ground_overlays(const Map *map, int x, int y, int z, size_t index,
                            const SDL_FRect *tile, void *userdata)
{
    const GroundPass *pass = userdata;
    if (map->rock[index] != ROCK_NONE || is_thin_foliage(map, index)) {
        return;
    }
    render_ground_shape(pass->renderer, map, x, y, z, tile);
}

void render_map(SDL_Renderer *renderer, Sprites *sprites, const Map *map, const Camera *camera)
{
    GroundPass pass = {renderer, sprites, camera->zoom};
    visit_ground_tiles(map, camera, ground_flat, &pass);
    batch_flush(renderer);
    sprites_begin(sprites, camera->zoom);
    visit_ground_tiles(map, camera, ground_textures, &pass);
    sprites_flush(sprites);
    visit_ground_tiles(map, camera, ground_overlays, &pass);
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
/* A triangle in the middle of a tile, pointing up or down: see-through, light
 * with a dark rim (the rim drawn around the inside, not under it, so that
 * both stay see-through). */
static void render_ramp_marker(SDL_Renderer *renderer, const SDL_FRect *tile, bool up)
{
    float cx = tile->x + tile->w * 0.5f, cy = tile->y + tile->h * 0.5f;
    float half = tile->w * RAMP_MARKER_SIZE * 0.5f, rise = tile->h * RAMP_MARKER_SIZE * 0.45f;
    float rim = SDL_max(1.0f, floorf(tile->w * 0.05f)) * 1.8f;
    float tip = up ? -1.0f : 1.0f; /* Which way the point goes on screen. */
    SDL_FPoint inside[3] = {
        {cx, cy + tip * rise}, {cx - half, cy - tip * rise}, {cx + half, cy - tip * rise}};
    SDL_FPoint outside[3] = {{cx, cy + tip * (rise + rim)},
                             {cx - half - rim, cy - tip * (rise + rim * 0.5f)},
                             {cx + half + rim, cy - tip * (rise + rim * 0.5f)}};
    batch_shape(renderer, RAMP_MARKER_COLOR, inside, 3);
    for (int i = 0; i < 3; i++) {
        int next = (i + 1) % 3;
        SDL_FPoint band[4] = {outside[i], outside[next], inside[next], inside[i]};
        batch_shape(renderer, RAMP_MARKER_EDGE, band, 4);
    }
}

void render_ramp_markers(SDL_Renderer *renderer, const Map *map, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
    if (map->slope_count == 0 || camera->zoom < RAMP_MARKER_MIN_ZOOM) {
        return;
    }
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            MapRamp ramp = map_ramp_at(map, x, y, z);
            if (ramp != MAP_RAMP_NONE) {
                SDL_FRect tile = tile_rect(camera, x, y);
                render_ramp_marker(renderer, &tile, ramp == MAP_RAMP_UP);
            }
        }
    }
    batch_flush(renderer);
}

/* A filled disc (a fan of triangles), drawn with the batch. */
static void batch_disc(SDL_Renderer *renderer, SDL_Color color, float cx, float cy, float radius)
{
    for (int i = 0; i < FORBID_RING_SEGMENTS; i++) {
        float a0 = 2.0f * SDL_PI_F * (float)i / FORBID_RING_SEGMENTS;
        float a1 = 2.0f * SDL_PI_F * (float)(i + 1) / FORBID_RING_SEGMENTS;
        SDL_FPoint points[3] = {{cx, cy},
                                {cx + SDL_cosf(a0) * radius, cy + SDL_sinf(a0) * radius},
                                {cx + SDL_cosf(a1) * radius, cy + SDL_sinf(a1) * radius}};
        batch_shape(renderer, color, points, 3);
    }
}

/* A ring between two radii, drawn with the batch. */
static void batch_ring(SDL_Renderer *renderer, SDL_Color color, float cx, float cy, float inner,
                       float outer)
{
    for (int i = 0; i < FORBID_RING_SEGMENTS; i++) {
        float a0 = 2.0f * SDL_PI_F * (float)i / FORBID_RING_SEGMENTS;
        float a1 = 2.0f * SDL_PI_F * (float)(i + 1) / FORBID_RING_SEGMENTS;
        float c0 = SDL_cosf(a0), s0 = SDL_sinf(a0), c1 = SDL_cosf(a1), s1 = SDL_sinf(a1);
        SDL_FPoint points[4] = {{cx + c0 * outer, cy + s0 * outer},
                                {cx + c1 * outer, cy + s1 * outer},
                                {cx + c1 * inner, cy + s1 * inner},
                                {cx + c0 * inner, cy + s0 * inner}};
        batch_shape(renderer, color, points, 4);
    }
}

/* A straight bar from (x0, y0) to (x1, y1), `half` thick on each side. */
static void batch_bar(SDL_Renderer *renderer, SDL_Color color, float x0, float y0, float x1,
                      float y1, float half)
{
    float dx = x1 - x0, dy = y1 - y0;
    float length = SDL_sqrtf(dx * dx + dy * dy);
    if (length <= 0.0f) {
        return;
    }
    float nx = -dy / length * half, ny = dx / length * half;
    SDL_FPoint points[4] = {
        {x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}};
    batch_shape(renderer, color, points, 4);
}

/* The "no entry" sign: a red ring crossed from top left to bottom right. */
static void render_forbid_sign(SDL_Renderer *renderer, float cx, float cy, float radius)
{
    batch_disc(renderer, FORBID_BACK_COLOR, cx, cy, radius);
    batch_ring(renderer, FORBID_COLOR, cx, cy, radius * 0.7f, radius);
    float d = radius * 0.72f * 0.7071f;
    batch_bar(renderer, FORBID_COLOR, cx - d, cy - d, cx + d, cy + d, radius * 0.15f);
}

/* Whether the tile shows something forbidden: a stack, a plant (a tree on
 * its foot and its trunk), a structure (on its head), a roof. */
static bool forbidden_marked(const Game *game, int x, int y, int z)
{
    const Map *map = game_view_map(game);
    if (game_view_items(game)->forbidden_count > 0 &&
        items_forbidden(game_view_items(game), x, y, z)) {
        return true;
    }
    if (map->forbidden_count == 0) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    if ((map->forbidden[index] & (MAP_FORBID_PLANT | MAP_FORBID_STRUCTURE | MAP_FORBID_ROOF)) !=
        0) {
        return map_forbidden(map, x, y, z, MAP_FORBID_PLANT) ||
               map_forbidden(map, x, y, z, MAP_FORBID_STRUCTURE) ||
               map_forbidden(map, x, y, z, MAP_FORBID_ROOF);
    }
    return map->rock[index] == ROCK_WOOD && game_forbidden(game, x, y, z, FORBID_PLANT);
}

/* Whether some zone or plan is forbidden (so that the tiles are worth a look). */
static bool any_zone_or_site_forbidden(const Game *game)
{
    for (int i = 0; i < STOCKPILE_MAX; i++) {
        const Stockpile *stockpile = stockpile_get(&game->stockpiles, i);
        if (stockpile != NULL && stockpile->forbidden) {
            return true;
        }
    }
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        const GatherZone *zone = gather_zone_get(&game->gather_zones, i);
        if (zone != NULL && zone->forbidden) {
            return true;
        }
    }
    for (int i = 0; i < CONSTRUCTION_MAX; i++) {
        const ConstructionSite *site = construction_get(&game->constructions, i);
        if (site != NULL && site->forbidden) {
            return true;
        }
    }
    return false;
}

/* Zones are marked every few tiles (all of a small one), a plan on its head. */
#define FORBID_ZONE_SPACING 3
#define FORBID_ZONE_SMALL 6
static bool zone_or_site_marked(const Game *game, int x, int y, int z)
{
    bool spaced = x % FORBID_ZONE_SPACING == 0 && y % FORBID_ZONE_SPACING == 0;
    const Stockpile *stockpile =
        stockpile_get(&game->stockpiles, stockpile_at(&game->stockpiles, x, y, z));
    if (stockpile != NULL && stockpile->forbidden &&
        (spaced || stockpile->cell_count < FORBID_ZONE_SMALL)) {
        return true;
    }
    const GatherZone *zone =
        gather_zone_get(&game->gather_zones, gather_zone_at(&game->gather_zones, x, y, z));
    if (zone != NULL && zone->forbidden && (spaced || zone->cell_count < FORBID_ZONE_SMALL)) {
        return true;
    }
    const ConstructionSite *site =
        construction_get(&game->constructions, construction_at(&game->constructions, x, y, z));
    return site != NULL && site->forbidden && site->x == x && site->y == y && site->z == z;
}

static void forbid_marker(SDL_Renderer *renderer, const SDL_FRect *tile)
{
    float radius = SDL_max(FORBID_MARKER_MIN_RADIUS, tile->w * FORBID_MARKER_RADIUS);
    render_forbid_sign(renderer, tile->x + tile->w - radius * 1.25f, tile->y + radius * 1.25f,
                       radius);
}

void render_forbidden_markers(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
    const Map *map = game_view_map(game);
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    if (map->forbidden_count > 0 || game_view_items(game)->forbidden_count > 0) {
        for (int y = visible.y0; y < visible.y1; y++) {
            for (int x = visible.x0; x < visible.x1; x++) {
                if (forbidden_marked(game, x, y, z)) {
                    SDL_FRect tile = tile_rect(camera, x, y);
                    forbid_marker(renderer, &tile);
                }
            }
        }
    }
    if (any_zone_or_site_forbidden(game)) {
        for (int y = visible.y0; y < visible.y1; y++) {
            for (int x = visible.x0; x < visible.x1; x++) {
                if (zone_or_site_marked(game, x, y, z)) {
                    SDL_FRect tile = tile_rect(camera, x, y);
                    forbid_marker(renderer, &tile);
                }
            }
        }
    }
    for (int i = 0; i < WORKSTATION_MAX; i++) {
        const Workstation *station = workstation_get_const(&game->workstations, i);
        if (station != NULL && station->forbidden && station->z == z && station->x >= visible.x0 &&
            station->x < visible.x1 && station->y >= visible.y0 && station->y < visible.y1) {
            SDL_FRect tile = tile_rect(camera, station->x, station->y);
            forbid_marker(renderer, &tile);
        }
    }
    batch_flush(renderer);
}

void render_item_moves(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const ItemMoves *moves = &game->item_moves;
    for (int i = 0; i < moves->count; i++) {
        TilePos from = moves->orders[i].source, to = moves->orders[i].destination;
        if (from.z == camera->level && to.z == camera->level) {
            float x0, y0, x1, y1;
            camera_world_to_screen(camera, (float)from.x + 0.5f, (float)from.y + 0.5f, &x0, &y0);
            camera_world_to_screen(camera, (float)to.x + 0.5f, (float)to.y + 0.5f, &x1, &y1);
            SDL_SetRenderDrawColor(renderer, PATH_COLOR.r, PATH_COLOR.g, PATH_COLOR.b,
                                   PATH_COLOR.a);
            SDL_RenderLine(renderer, x0, y0, x1, y1);
        }
        if (to.z == camera->level) {
            render_tile_outline(renderer, camera, to.x, to.y, ALLOW_COLOR);
        }
    }
}

void render_snow_cover(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
    float snow = game->weather.snow;
    if (snow <= 0.0f) {
        return;
    }
    const Map *map = game_view_map(game);
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    SDL_SetRenderDrawColor(renderer, 240, 244, 250, (Uint8)(clampf(snow, 0.0f, 1.0f) * 200.0f));
    for (int y = visible.y0; y < visible.y1; y++) {
        int run = -1; /* First tile of the current run of snowy tiles. */
        for (int x = visible.x0; x <= visible.x1; x++) {
            bool snowy = x < visible.x1 && tile_seen(x, y, z) && !map_is_water(map, x, y, z) &&
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

void render_weather(SDL_Renderer *renderer, const Game *game, const Camera *camera, Uint64 now_ms)
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

/* What needs no material and no work (a crafting spot, a ground bedding) is
 * only marked on the ground: a thick light line with a dark one around it,
 * nothing filled. */
static void free_outline(SDL_Renderer *renderer, SDL_FRect rect, Uint8 alpha)
{
    SDL_Color light = {238, 226, 196, alpha}, dark = ITEM_OUTLINE;
    dark.a = alpha;
    float thickness = SDL_max(3.0f, floorf(SDL_min(rect.w, rect.h) * 0.1f));
    outline(renderer, rect, 1.0f, dark);
    rect.x += 1.0f;
    rect.y += 1.0f;
    rect.w -= 2.0f;
    rect.h -= 2.0f;
    outline(renderer, rect, thickness, light);
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
/* Bodies lying on their side across the tile. A settler's: legs to the left,
 * head to the right, pale; an animal's: its coat, legs stretched out, the size
 * of its species. */
static void render_body(SDL_Renderer *renderer, const SDL_FRect *tile, ItemType type)
{
    float w = tile->w, h = tile->h;
    float cy = tile->y + h * 0.55f;
    if (type == ITEM_SETTLER_BODY) { /* Across the whole tile. */
        SDL_Color skin = {196, 184, 172, 255}, cloth = {104, 88, 70, 255};
        float half = SDL_max(1.0f, h * 0.075f);
        batch_bar(renderer, shade(cloth, -20), tile->x + w * 0.04f, cy - half, tile->x + w * 0.4f,
                  cy - half, half);
        batch_bar(renderer, shade(cloth, -32), tile->x + w * 0.04f, cy + half, tile->x + w * 0.4f,
                  cy + half, half);
        SDL_FRect torso = {floorf(tile->x + w * 0.36f), floorf(cy - h * 0.17f), floorf(w * 0.38f),
                           floorf(h * 0.34f)};
        fill_outlined(renderer, torso, cloth);
        batch_disc(renderer, shade(skin, -40), tile->x + w * 0.85f, cy, w * 0.13f);
        batch_disc(renderer, skin, tile->x + w * 0.85f, cy, w * 0.13f - 1.0f);
        return;
    }
    SDL_Color coat;
    float scale;
    switch (type) {
    case ITEM_ROE_DEER_CARCASS: coat = (SDL_Color){158, 104, 62, 255}, scale = 0.75f; break;
    case ITEM_RED_DEER_CARCASS: coat = (SDL_Color){128, 82, 52, 255}, scale = 1.0f; break;
    case ITEM_BOAR_CARCASS: coat = (SDL_Color){78, 66, 56, 255}, scale = 0.9f; break;
    case ITEM_WOLF_CARCASS: coat = (SDL_Color){118, 114, 106, 255}, scale = 0.8f; break;
    case ITEM_FOX_CARCASS: coat = (SDL_Color){196, 100, 44, 255}, scale = 0.5f; break;
    default: coat = (SDL_Color){150, 130, 104, 255}, scale = 0.45f; break;
    }
    float cx = tile->x + w * 0.5f;
    float length = w * 0.9f * scale, depth = h * 0.38f * scale;
    float leg = SDL_max(1.0f, depth * 0.16f);
    for (int i = 0; i < 4; i++) { /* Two pairs, stretched out under the body. */
        float x =
            cx - length * 0.32f + length * (i < 2 ? 0.0f : 0.6f) + (float)(i % 2) * leg * 2.5f;
        batch_bar(renderer, shade(coat, -40), x, cy, x + depth * 0.4f, cy + depth * 0.9f, leg);
    }
    SDL_FRect body = {floorf(cx - length * 0.4f), floorf(cy - depth * 0.5f), floorf(length * 0.8f),
                      floorf(depth)};
    fill_outlined(renderer, body, coat);
    batch_disc(renderer, shade(coat, -15), cx + length * 0.45f, cy - depth * 0.35f, depth * 0.38f);
}

static void render_item_stack(SDL_Renderer *renderer, const SDL_FRect *tile, ItemType type)
{
    float w = tile->w, h = tile->h;
    switch (type) {
    case ITEM_SETTLER_BODY:
    case ITEM_ROE_DEER_CARCASS:
    case ITEM_RED_DEER_CARCASS:
    case ITEM_BOAR_CARCASS:
    case ITEM_WOLF_CARCASS:
    case ITEM_FOX_CARCASS:
    case ITEM_HARE_CARCASS: render_body(renderer, tile, type); break;
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
            SDL_FRect edge = {stone.x + 1.0f, stone.y + 1.0f, floorf(size * 0.3f), stone.h - 2.0f};
            batch_quad(renderer, shade(STONE_COLOR, -40), &edge);
        }
        break;
    }
    case ITEM_FLAKE: {
        /* A few thin slivers. */
        float thickness = SDL_max(1.0f, floorf(h * 0.08f));
        for (int i = 0; i < 3; i++) {
            SDL_FRect sliver = {tile->x + floorf(w * (0.24f + 0.12f * (float)(i % 2))),
                                tile->y + floorf(h * (0.38f + 0.14f * (float)i)), floorf(w * 0.34f),
                                thickness};
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
                               tile->y + floorf(h * (0.3f + 0.15f * (float)i)), floorf(w * length),
                               thickness};
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
                               tile->y + floorf(h * top), blade_w, floorf(h * (0.76f - top))};
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
            SDL_FRect spot = {tile->x + floorf(w * SPOTS[i][0]), tile->y + floorf(h * SPOTS[i][1]),
                              size, size};
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

/* Branches, twigs and nuts lying on a tile (shed by the trees): the item's icon
 * small, branches in the upper left corner, twigs in the lower right one and
 * nuts in the lower left one, under the items. */
static void render_litter(SDL_Renderer *renderer, const Map *map, int x, int y, int z,
                          const SDL_FRect *tile)
{
    if (map_litter(map, x, y, z, ITEM_BRANCHES) > 0) {
        SDL_FRect corner = {tile->x, tile->y, tile->w * 0.6f, tile->h * 0.6f};
        render_item_stack(renderer, &corner, ITEM_BRANCHES);
    }
    if (map_litter(map, x, y, z, ITEM_TWIGS) > 0) {
        SDL_FRect corner = {tile->x + tile->w * 0.4f, tile->y + tile->h * 0.4f, tile->w * 0.6f,
                            tile->h * 0.6f};
        render_item_stack(renderer, &corner, ITEM_TWIGS);
    }
    if (map_litter(map, x, y, z, ITEM_HAZELNUTS) > 0) {
        SDL_FRect corner = {tile->x, tile->y + tile->h * 0.4f, tile->w * 0.6f, tile->h * 0.6f};
        render_item_stack(renderer, &corner, ITEM_HAZELNUTS);
    }
}

void render_items(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const int z = camera->level; /* The level shown. */
    const Items *items = game_view_items(game);
    TileRect visible = camera_visible_tiles(camera, items->width, items->height);
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            if (!tile_seen(x, y, z)) {
                continue;
            }
            SDL_FRect tile = tile_rect(camera, x, y);
            render_litter(renderer, game_view_map(game), x, y, z, &tile);
            ItemStack stack = items_get(game_items_at(game, x, y, z), x, y, z);
            if (stack.type != ITEM_NONE) {
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
    const Map *map = game_view_map(game);
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

void render_designation_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                             int y1, DesignationType type)
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

void render_forbid_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                        int y1, bool forbid)
{
    SDL_FRect a = tile_rect(camera, SDL_min(x0, x1), SDL_min(y0, y1));
    SDL_FRect b = tile_rect(camera, SDL_max(x0, x1), SDL_max(y0, y1));
    SDL_FRect area = {a.x, a.y, b.x + b.w - a.x, b.y + b.h - a.y};
    SDL_Color color = forbid ? FORBID_COLOR : ALLOW_COLOR;
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
                              ZoneAt zone_at, int width, int height, int selected, SDL_Color color)
{
    const int z = camera->level; /* The level shown. */
    TileRect visible = camera_visible_tiles(camera, width, height);
    SDL_Color normal = color, highlight = color;
    normal.a = 45;
    highlight.a = 90;
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            int index = zone_at(zones, x, y, z);
            if (index < 0 || !tile_seen(x, y, z)) {
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

void render_stockpiles(SDL_Renderer *renderer, const Game *game, const Camera *camera, int selected)
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
static void render_flames(SDL_Renderer *renderer, const SDL_FRect *tile, float time, float strength)
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
    if (!workstation_needs_construction(type)) {
        free_outline(renderer, inset(tile, 0.84f), alpha); /* Nothing built: only marked. */
        return;
    }
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

/* Under the fog: the workstations the colony remembers on the level shown, as
 * it last saw them (one may be gone, or burning no more); only what is in view
 * now gets its progress bar and its selection. */
static void render_known_workstations(SDL_Renderer *renderer, const Game *game,
                                      const Camera *camera, int selected)
{
    const Workstations *workstations = &game->workstations;
    const int z = camera->level; /* The level shown. */
    TileRect visible = camera_visible_tiles(camera, workstations->width, workstations->height);
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            const KnownStation *picture =
                &game->known.stations[map_index(&game->known.map, x, y, z)];
            if (picture->type_plus_one == 0 || !tile_seen(x, y, z)) {
                continue;
            }
            Workstation remembered = {.type = (uint8_t)(picture->type_plus_one - 1),
                                      .burning = picture->burning,
                                      .fuel = (float)picture->fuel * 0.1f};
            remembered.work_input.type = picture->input;
            SDL_FRect tile = tile_rect(camera, x, y);
            render_station_shape(renderer, &tile, (WorkstationType)remembered.type, 255);
            if (hearth_is(&remembered)) {
                render_hearth_fire(renderer, &tile, &remembered);
            }
            if (remembered.work_input.type != ITEM_NONE) {
                render_item_stack(renderer, &tile, (ItemType)remembered.work_input.type);
                batch_flush(renderer);
            }
            int index = vision_seen(&game->vision, x, y, z) ? workstation_at(workstations, x, y, z)
                                                            : WORKSTATION_NONE;
            const Workstation *station = workstation_get_const(workstations, index);
            if (station != NULL) {
                progress_bar(renderer, &tile, craft_fraction(station));
                if (index == selected) {
                    outline(renderer, tile, 2.0f, SELECTION_COLOR);
                }
            }
        }
    }
}

void render_workstations(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                         int selected)
{
    if (game->fog_on) {
        render_known_workstations(renderer, game, camera, selected);
        return;
    }
    const Workstations *workstations = &game->workstations;
    if (workstations->used_count == 0) {
        return;
    }
    TileRect visible = camera_visible_tiles(camera, workstations->width, workstations->height);
    for (int i = 0; i < WORKSTATION_MAX; i++) {
        const Workstation *station = workstation_get_const(workstations, i);
        if (station == NULL || station->z != camera->level || station->x < visible.x0 ||
            station->x >= visible.x1 || station->y < visible.y0 || station->y >= visible.y1 ||
            !tile_seen(station->x, station->y, station->z)) {
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
    bool allowed = game_can_place_workstation(game, x, y, game_build_level(game, x, y, z, false));
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
    if (!structure_needs_construction(type)) {
        bool vertical = direction == DIRECTION_NORTH || direction == DIRECTION_SOUTH;
        float across = vertical ? w : h;
        float side = floorf(across * 0.14f), end = floorf(across * 0.08f);
        free_outline(
            renderer,
            vertical ? (SDL_FRect){tile->x + side, tile->y + end, w - 2.0f * side, h - 2.0f * end}
                     : (SDL_FRect){tile->x + end, tile->y + side, w - 2.0f * end, h - 2.0f * side},
            alpha);
        return;
    }
    switch (type) {
    case STRUCTURE_GRASS_BED: {
        bool vertical = direction == DIRECTION_NORTH || direction == DIRECTION_SOUTH;
        float across = vertical ? w : h; /* One tile. */
        float side = floorf(across * 0.12f), end = floorf(across * 0.06f);
        SDL_FRect bed =
            vertical ? (SDL_FRect){tile->x + side, tile->y + end, w - 2.0f * side, h - 2.0f * end}
                     : (SDL_FRect){tile->x + end, tile->y + side, w - 2.0f * end, h - 2.0f * side};
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
    case STRUCTURE_GROUND_BEDDING: break; /* Only an outline, above. */
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
    const Map *map = game_view_map(game);
    if (map->structure_counts[STRUCTURE_NONE] < (int)map_tile_count(map)) {
        TileRect visible = visible_heads(camera, map->width, map->height);
        for (int y = visible.y0; y < visible.y1; y++) {
            for (int x = visible.x0; x < visible.x1; x++) {
                if (map_structure_is_origin(map, x, y, z) && tile_seen(x, y, z)) {
                    StructureType type = map_structure(map, x, y, z);
                    Direction direction = map_structure_direction(map, x, y, z);
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
        if (site == NULL || site->x < visible.x0 || site->x >= visible.x1 || site->y < visible.y0 ||
            site->y >= visible.y1) {
            continue;
        }
        /* A roof over ground seen from higher up stands on open space the
         * passes skip: it is drawn with the ground under it. */
        bool roof_seen_from_above = below_active && construction_is_roof(site) &&
                                    site->z - 1 == camera->level && below_shown > site->z;
        if (roof_seen_from_above
                ? !tile_seen(site->x, site->y, camera->level)
                : (site->z != camera->level || !tile_seen(site->x, site->y, site->z))) {
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
    const Map *map = game_view_map(game);
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
    (void)z; /* Where it would stand: the tile drawn is the same. */
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

void render_item_icon(SDL_Renderer *renderer, SDL_FRect rect, ItemType type)
{
    render_item_stack(renderer, &rect, type);
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
        SDL_FRect crown = {rect.x + floorf(w * 0.2f), rect.y + floorf(h * 0.25f), floorf(w * 0.28f),
                           floorf(h * 0.24f)};
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
    case RENDER_ICON_DIG_RAMP: { /* A ramp rising to the right, lit. */
        SDL_FRect area = inset(&rect, 0.8f);
        fill(renderer, ROCK_COLORS[ROCK_EARTH], &area);
        SDL_SetRenderDrawColor(renderer, 196, 160, 112, 255);
        float rise = area.h * 0.6f;
        for (float row = 0.0f; row < rise; row += 1.0f) {
            float left = area.x + area.w * 0.15f + area.w * 0.7f * (1.0f - row / rise);
            SDL_RenderLine(renderer, left, area.y + area.h * 0.8f - row, area.x + area.w * 0.85f,
                           area.y + area.h * 0.8f - row);
        }
        outline(renderer, area, 2.0f, DESIGNATION_COLORS[DESIGNATION_DIG_RAMP]);
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
    case RENDER_ICON_CHOP: { /* A cut trunk seen from above, a notch in its side. */
        SDL_FRect area = inset(&rect, 0.8f);
        fill(renderer, BARK_COLOR, &area);
        SDL_FRect wood = inset(&area, 0.8f);
        fill(renderer, ROCK_COLORS[ROCK_WOOD], &wood);
        SDL_FRect ring = inset(&area, 0.45f);
        fill(renderer, RING_COLOR, &ring);
        SDL_FRect heart = inset(&area, 0.25f);
        fill(renderer, ROCK_COLORS[ROCK_WOOD], &heart);
        SDL_FRect notch = {area.x + area.w * 0.62f, area.y + area.h * 0.4f, area.w * 0.38f,
                           area.h * 0.2f};
        fill(renderer, (SDL_Color){40, 28, 20, 255}, &notch);
        outline(renderer, area, 2.0f, DESIGNATION_COLORS[DESIGNATION_CHOP]);
        break;
    }
    case RENDER_ICON_HARVEST: { /* A bush bearing berries. */
        SDL_FRect area = inset(&rect, 0.8f);
        fill(renderer, ROCK_COLORS[ROCK_EARTH], &area);
        SDL_FRect bush = {area.x + area.w * 0.12f, area.y + area.h * 0.18f, area.w * 0.76f,
                          area.h * 0.66f};
        fill(renderer, TREE_COLOR, &bush);
        SDL_FRect leaves = inset(&bush, 0.7f);
        fill(renderer, FRESH_GRASS_COLOR, &leaves);
        static const float SPOTS[4][2] = {
            {0.3f, 0.35f}, {0.6f, 0.3f}, {0.45f, 0.6f}, {0.7f, 0.62f}};
        float size = SDL_max(2.0f, floorf(area.w * 0.14f));
        for (int i = 0; i < 4; i++) {
            SDL_FRect berry = {floorf(area.x + area.w * SPOTS[i][0]),
                               floorf(area.y + area.h * SPOTS[i][1]), size, size};
            fill(renderer, BERRY_COLOR, &berry);
        }
        outline(renderer, area, 2.0f, DESIGNATION_COLORS[DESIGNATION_HARVEST]);
        break;
    }
    case RENDER_ICON_UPROOT: { /* A plant pulled out of the ground, roots and all. */
        SDL_FRect area = inset(&rect, 0.8f);
        SDL_FRect ground = {area.x, area.y + area.h * 0.62f, area.w, area.h * 0.38f};
        fill(renderer, ROCK_COLORS[ROCK_EARTH], &ground);
        float cx = floorf(area.x + area.w * 0.42f);
        float base = area.y + area.h * 0.5f;
        SDL_SetRenderDrawColor(renderer, 230, 214, 180, 255); /* Roots. */
        for (float d = -1.0f; d <= 1.0f; d += 1.0f) {
            SDL_RenderLine(renderer, cx, base, cx + d * area.w * 0.16f, base + area.h * 0.28f);
        }
        SDL_FRect stem = {cx - 1.0f, area.y + area.h * 0.3f, 2.0f, area.h * 0.2f};
        fill(renderer, TREE_COLOR, &stem);
        SDL_FRect leaf = {cx - area.w * 0.16f, area.y + area.h * 0.12f, area.w * 0.32f,
                          area.h * 0.2f};
        fill(renderer, FRESH_GRASS_COLOR, &leaf);
        /* An arrow up beside it. */
        float ax = floorf(area.x + area.w * 0.8f);
        SDL_SetRenderDrawColor(renderer, 236, 220, 190, 255);
        for (float d = -1.0f; d <= 0.0f; d += 1.0f) {
            SDL_RenderLine(renderer, ax + d, area.y + area.h * 0.2f, ax + d,
                           area.y + area.h * 0.7f);
        }
        for (float d = 0.0f; d < area.w * 0.12f; d += 1.0f) {
            SDL_RenderLine(renderer, ax - d, area.y + area.h * 0.2f + d, ax + d - 1.0f,
                           area.y + area.h * 0.2f + d);
        }
        outline(renderer, area, 2.0f, DESIGNATION_COLORS[DESIGNATION_UPROOT]);
        break;
    }
    case RENDER_ICON_DECONSTRUCT: {
        SDL_FRect area = inset(&rect, 0.7f);
        render_structure_shape(renderer, &area, STRUCTURE_WALL, DIRECTION_SOUTH, 255);
        outline(renderer, area, 2.0f, DESIGNATION_COLORS[DESIGNATION_DECONSTRUCT]);
        break;
    }
    case RENDER_ICON_FORBID:
        render_forbid_sign(renderer, rect.x + w * 0.5f, rect.y + h * 0.5f, w * 0.4f);
        batch_flush(renderer);
        break;
    case RENDER_ICON_ALLOW: { /* A green ring with a tick. */
        float cx = rect.x + w * 0.5f, cy = rect.y + h * 0.5f, radius = w * 0.4f;
        batch_disc(renderer, FORBID_BACK_COLOR, cx, cy, radius);
        batch_ring(renderer, ALLOW_COLOR, cx, cy, radius * 0.7f, radius);
        float half = radius * 0.13f;
        batch_bar(renderer, ALLOW_COLOR, cx - radius * 0.42f, cy + radius * 0.02f,
                  cx - radius * 0.1f, cy + radius * 0.34f, half);
        batch_bar(renderer, ALLOW_COLOR, cx - radius * 0.18f, cy + radius * 0.34f,
                  cx + radius * 0.44f, cy - radius * 0.32f, half);
        batch_flush(renderer);
        break;
    }
    case RENDER_ICON_MOVE: { /* An arrow leading to a ring on the ground. */
        float cx = rect.x + w * 0.5f, cy = rect.y + h * 0.5f, half = w * 0.05f;
        SDL_Color color = {236, 230, 210, 255};
        batch_bar(renderer, color, cx - w * 0.36f, cy + h * 0.3f, cx + w * 0.22f, cy - h * 0.28f,
                  half);
        batch_bar(renderer, color, cx + w * 0.3f, cy - h * 0.36f, cx + w * 0.3f, cy - h * 0.06f,
                  half);
        batch_bar(renderer, color, cx + w * 0.3f, cy - h * 0.36f, cx, cy - h * 0.36f, half);
        batch_ring(renderer, ALLOW_COLOR, cx + w * 0.2f, cy + h * 0.28f, w * 0.12f, w * 0.18f);
        batch_flush(renderer);
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
static bool fog_tile_seen(const Game *game, const Camera *camera, int x, int y);

static void lit_quads(SDL_Renderer *renderer, const Camera *camera, TileRect box, SDL_Color color,
                      float darkness, bool glow, const Game *fog)
{
    int columns = box.x1 - box.x0 + 1;
    float flicker = 0.95f + 0.05f * sinf((float)(SDL_GetTicks() % 100000u) * 0.011f);
    for (int y = box.y0; y < box.y1; y++) {
        for (int x = box.x0; x < box.x1; x++) {
            int corner = (y - box.y0) * columns + (x - box.x0);
            float light[4] = {light_corners[corner], light_corners[corner + 1],
                              light_corners[corner + columns + 1], light_corners[corner + columns]};
            if ((glow && light[0] + light[1] + light[2] + light[3] <= 0.0f) ||
                (fog != NULL && !fog_tile_seen(fog, camera, x, y))) {
                continue; /* Nothing lights what is not in view: what is known of it is dim. */
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
static SDL_Color temperature_color(const Theme *theme, float celsius)
{
    const int last = THEME_TEMPERATURE_STEPS - 1;
    float position =
        clampf((celsius - TEMPERATURE_VIEW_COLDEST) / TEMPERATURE_VIEW_STEP, 0.0f, (float)last);
    int from = SDL_min((int)position, last - 1);
    return mix(theme->temperature[from], theme->temperature[from + 1], position - (float)from);
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

void render_temperature(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                        const Theme *theme)
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
            if (map_is_open(&game->map, x, y, z) || !game_tile_seen(game, x, y, z)) {
                continue; /* Nothing here (the levels below show), or out of view. */
            }
            float celsius = temperature_at(temperatures, x, y, z);
            if (burning && near_fire(workstations, x, y, z)) {
                celsius += hearths_radiant_warmth(workstations, &game->map, x, y, z);
            }
            SDL_Color color = temperature_color(theme, celsius);
            color.a = 120;
            SDL_FRect tile = tile_rect(camera, x, y);
            batch_quad(renderer, color, &tile);
        }
    }
    batch_flush(renderer);
}

/* Whether the tile drawn at (x, y) on the level shown is in view now. */
static bool fog_tile_seen(const Game *game, const Camera *camera, int x, int y)
{
    int level = map_view_level(game_view_map(game), x, y, camera->level);
    return level >= 0 && vision_seen(&game->vision, x, y, level);
}

/* The night over the tiles in view that lie outside `box`, in plain darkness.
 * What is not in view is dim whatever the time of day: nobody knows whether
 * something lights it. */
static void fog_night(SDL_Renderer *renderer, const Game *game, const Camera *camera, TileRect box,
                      SDL_Color color)
{
    TileRect visible = camera_visible_tiles(camera, game->map.width, game->map.height);
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            bool lit_box = x >= box.x0 && x < box.x1 && y >= box.y0 && y < box.y1;
            if (!lit_box && fog_tile_seen(game, camera, x, y)) {
                SDL_FRect tile = tile_rect(camera, x, y);
                batch_quad(renderer, color, &tile);
            }
        }
    }
    batch_flush(renderer);
}

void render_fog(SDL_Renderer *renderer, const Game *game, const Camera *camera)
{
    const Map *map = game_view_map(game);
    const int z = camera->level; /* The level shown. */
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    SDL_Color veil = {14, 16, 28, 165}; /* The same dim light by day and by night. */
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            int level = map_view_level(map, x, y, z); /* What is drawn there. */
            if (level >= 0 && !vision_seen(&game->vision, x, y, level)) {
                SDL_FRect tile = tile_rect(camera, x, y);
                batch_quad(renderer, veil, &tile);
            }
        }
    }
    batch_flush(renderer);
}

void render_vision(SDL_Renderer *renderer, const Game *game, const Camera *camera, int slot)
{
    const Map *map = game_view_map(game);
    const Vision *vision = &game->vision;
    const int z = camera->level; /* The level shown. */
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    SDL_Color veil = {8, 8, 20, 150};
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            int level = map_view_level(map, x, y, z); /* What is drawn there. */
            if (level < 0) {
                continue;
            }
            bool seen = slot >= 0 ? vision_viewer_sees(vision, slot, x, y, level)
                                  : vision_seen(vision, x, y, level);
            if (!seen) {
                SDL_FRect tile = tile_rect(camera, x, y);
                batch_quad(renderer, veil, &tile);
            }
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
        if (game->fog_on) { /* Only over what is in view. */
            SDL_Color night = {NIGHT_COLOR.r, NIGHT_COLOR.g, NIGHT_COLOR.b, alpha};
            fog_night(renderer, game, camera, (TileRect){0, 0, 0, 0}, night);
            return;
        }
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
    if (game->fog_on) {
        SDL_Color night = {NIGHT_COLOR.r, NIGHT_COLOR.g, NIGHT_COLOR.b, alpha};
        fog_night(renderer, game, camera, box, night);
    } else {
        for (int i = 0; i < 4; i++) {
            if (around[i].w > 0.0f && around[i].h > 0.0f) {
                SDL_RenderFillRect(renderer, &around[i]);
            }
        }
    }
    const Game *fog = game->fog_on ? game : NULL;
    int columns = box.x1 - box.x0 + 1;
    for (int y = box.y0; y <= box.y1; y++) {
        for (int x = box.x0; x <= box.x1; x++) {
            light_corners[(y - box.y0) * columns + (x - box.x0)] =
                hearths_light_at(workstations, (float)x, (float)y, camera->level);
        }
    }
    lit_quads(renderer, camera, box, NIGHT_COLOR, darkness, false, fog);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    lit_quads(renderer, camera, box, GLOW_COLOR, darkness, true, fog);
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
/* The Z of sleep: how many rise from a sleeper's head at once, and how long
 * one takes to rise and fade (ticks). */
#define SLEEP_Z_COUNT 3
#define SLEEP_Z_TICKS 216
/* Animal sprites leave this much under the feet (the shadow), in tiles. */
#define ANIMAL_SPRITE_MARGIN (8.0f / SPRITE_PIXELS_PER_TILE)
/* Whoever walks hops at every step and rocks from side to side around its
 * feet, one way then the other: how high (tiles), how far (degrees), and how
 * many ticks a step takes at a walk and on the run. */
#define GAIT_HOP_HEIGHT 0.06f
#define GAIT_ROCK_DEGREES 5.5f
#define GAIT_WALK_STEP_TICKS 16
#define GAIT_RUN_STEP_TICKS 6
/* Height of what a colonist carries above its feet, and how far ahead of it
 * when it is seen from the side, in tiles. */
#define COLONIST_CARRY_HEIGHT 0.62f
#define COLONIST_CARRY_AHEAD 0.18f
/* A colonist down lies across its tile: its feet moved left by this much and
 * its body raised by this much (tiles), so that it lies about the middle. */
#define COLONIST_DOWN_SHIFT 0.45f
#define COLONIST_DOWN_LIFT 0.25f
/* Room an animal takes on screen for sorting and for what hides it, in tiles. */
#define ANIMAL_HALF_WIDTH 0.5f
#define ANIMAL_HEIGHT 0.8f
/* Screen area of a colonist sprite (the figure, not the empty margins) in
 * tiles around its feet, and the opacity of a tree standing in front of it:
 * nobody gets lost in a forest. */
#define COLONIST_HALF_WIDTH 0.3f
#define COLONIST_HEIGHT 1.05f
/* Speech bubbles over chatting colonists from this zoom level (pixels per tile). */
#define CHAT_BUBBLE_MIN_ZOOM 14.0f
#define TREE_SEE_THROUGH_ALPHA 0.4f

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
    if (colonist->task == TASK_MOVE && colonist->target.z == camera->level) {
        render_tile_outline(renderer, camera, colonist->target.x, colonist->target.y, PATH_COLOR);
    }
}

/* How a walker's sprite is moved at a tick: lifted (screen pixels) and turned
 * clockwise around its feet (degrees). Nothing when it stands. */
typedef struct Gait {
    float lift;
    float degrees;
} Gait;

/* Walkers side by side do not move in step. */
static Gait gait_at(bool moving, int step_ticks, uint32_t slot, uint64_t tick, float zoom)
{
    if (!moving) {
        return (Gait){0.0f, 0.0f};
    }
    /* Two steps to rock one way and back. */
    uint64_t cycle = (uint64_t)step_ticks * 2u;
    float swing =
        SDL_sinf((float)((tick + (uint64_t)slot * 7u) % cycle) / (float)cycle * 2.0f * SDL_PI_F);
    return (Gait){SDL_fabsf(swing) * GAIT_HOP_HEIGHT * zoom, swing * GAIT_ROCK_DEGREES};
}

/* Where a point `height` screen pixels above the feet goes with a gait. */
static void gait_move(Gait gait, float height, float *x, float *y)
{
    float radians = gait.degrees * (SDL_PI_F / 180.0f);
    *x += height * SDL_sinf(radians);
    *y += height * (1.0f - SDL_cosf(radians)) - gait.lift;
}

/* A sprite of something that walks, anchored like sprites_draw(), moved by
 * its gait around its feet at `feet_y`. */
static void draw_walker(Sprites *sprites, SpriteId id, float anchor_x, float anchor_y, float feet_y,
                        Gait gait, SDL_FColor color, bool flip)
{
    if (gait.lift == 0.0f && gait.degrees == 0.0f) {
        sprites_draw(sprites, id, anchor_x, anchor_y, color, flip);
        return;
    }
    float width, height;
    sprites_size(sprites, id, &width, &height);
    float center_x = anchor_x;
    float center_y = anchor_y - height * 0.5f;
    gait_move(gait, feet_y - center_y, &center_x, &center_y);
    sprites_draw_rotated(sprites, id, center_x, center_y, gait.degrees, color, flip);
}

/* A fist (holding the tool of the job, if any) moving beside a working colonist. */
static void render_hand(Sprites *sprites, const Camera *camera, const Colonist *colonist,
                        uint32_t slot, uint64_t tick, float feet_x, float feet_y, SDL_FColor color)
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

void render_colonist_portrait(SDL_Renderer *renderer, Sprites *sprites, const Colonist *colonist,
                              SDL_FRect rect)
{
    /* Head and shoulders: the figure is drawn larger than the rectangle and
     * cut at its bottom edge. */
    float zoom = rect.h * 1.45f;
    float anchor_x = rect.x + rect.w * 0.5f;
    float anchor_y = rect.y + zoom * (COLONIST_HEIGHT + COLONIST_SPRITE_MARGIN) - rect.h * 0.04f;
    SDL_Rect clip = {(int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h};
    SDL_Rect previous;
    bool clipped = SDL_RenderClipEnabled(renderer);
    SDL_GetRenderClipRect(renderer, &previous);
    SDL_SetRenderClipRect(renderer, &clip);
    ColonistAppearance looks = colonist->appearance;
    SDL_FColor skin = WHITE;
    SDL_FColor hair = to_fcolor(HAIR_COLORS[looks.hair_color % HAIR_COLOR_COUNT]);
    sprites_begin(sprites, zoom);
    sprites_draw(sprites, sprite_body(looks.body, looks.skin, SPRITE_VIEW_FRONT), anchor_x,
                 anchor_y, skin, false);
    sprites_draw(sprites, sprite_head(looks.skin, colonist_expression(colonist), SPRITE_VIEW_FRONT),
                 anchor_x, anchor_y, skin, false);
    if (colonist->task == TASK_SLEEP ||
        (colonist_is_down(colonist) && health_is_unconscious(&colonist->health))) {
        sprites_draw(sprites, sprite_closed_eyes(looks.skin), anchor_x, anchor_y, skin, false);
    }
    if (looks.beard > 0) {
        sprites_draw(sprites, sprite_beard(looks.beard, SPRITE_VIEW_FRONT), anchor_x, anchor_y,
                     hair, false);
    }
    if (looks.hair_style > 0) {
        sprites_draw(sprites, sprite_hair(looks.hair_style, SPRITE_VIEW_FRONT), anchor_x, anchor_y,
                     hair, false);
    }
    sprites_flush(sprites);
    SDL_SetRenderClipRect(renderer, clipped ? &previous : NULL);
}

/* Z's rising from a sleeper's head, up and to the right: each grows, sways and
 * fades in and out over its life, the three a third of a life apart (and
 * sleepers side by side out of step, by slot). `anchor_*`: the sprite's anchor. */
static void render_sleep_zs(Sprites *sprites, const Camera *camera, uint32_t slot, uint64_t tick,
                            float anchor_x, float anchor_y)
{
    for (int i = 0; i < SLEEP_Z_COUNT; i++) {
        uint64_t offset = (uint64_t)slot * 37u + (uint64_t)i * (SLEEP_Z_TICKS / SLEEP_Z_COUNT);
        float life = (float)((tick + offset) % SLEEP_Z_TICKS) / (float)SLEEP_Z_TICKS;
        float x = (24.0f + 30.0f * life + 5.0f * SDL_sinf(life * 7.0f)) / SPRITE_PIXELS_PER_TILE;
        float y = (140.0f + 50.0f * life) / SPRITE_PIXELS_PER_TILE;
        float fade_in = SDL_min(1.0f, life * 6.0f);
        float fade_out = 1.0f - SDL_max(0.0f, life - 0.55f) / 0.45f;
        float alpha = fade_in * fade_out;
        SDL_FColor color = {1.0f, 1.0f, 1.0f, alpha};
        sprites_draw_scaled(sprites, sprite_sleep_z(), anchor_x + x * camera->zoom,
                            anchor_y - y * camera->zoom, 0.5f + 0.65f * life, color);
    }
}

static void render_colonist(SDL_Renderer *renderer, Sprites *sprites, const Camera *camera,
                            const Colonist *colonist, uint32_t slot, uint64_t tick, bool selected)
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
    SDL_FColor skin = WHITE;
    SDL_FColor hair = to_fcolor(HAIR_COLORS[looks.hair_color % HAIR_COLOR_COUNT]);
    /* Seen from the side or from behind while it walks that way; every layer
     * moves as one around the feet. */
    Heading heading = colonist_heading(colonist);
    SpriteView view = heading == HEADING_SOUTH   ? SPRITE_VIEW_FRONT
                      : heading == HEADING_NORTH ? SPRITE_VIEW_BACK
                                                 : SPRITE_VIEW_SIDE;
    bool flip = heading == HEADING_WEST;
    Gait gait =
        gait_at(colonist_is_moving(colonist), GAIT_WALK_STEP_TICKS, slot, tick, camera->zoom);
    /* Down: lying on its side across the tile, head to the right. */
    bool down = colonist_is_down(colonist);
    bool eyes_closed =
        colonist->task == TASK_SLEEP || (down && health_is_unconscious(&colonist->health));
    if (down) {
        gait = (Gait){camera->zoom * COLONIST_DOWN_LIFT, 90.0f};
        anchor_x -= camera->zoom * COLONIST_DOWN_SHIFT;
    }
    /* What it carries is held in front of it at waist height and follows the
     * body: hidden by the body of someone walking away, ahead of someone seen
     * from the side. */
    bool carrying = colonist->carried.count > 0;
    float size = floorf(SDL_max(4.0f, camera->zoom * 0.42f));
    float held_x = feet_x, held_y = feet_y - camera->zoom * COLONIST_CARRY_HEIGHT;
    if (view == SPRITE_VIEW_SIDE) {
        held_x += (flip ? -1.0f : 1.0f) * camera->zoom * COLONIST_CARRY_AHEAD;
    }
    gait_move(gait, camera->zoom * COLONIST_CARRY_HEIGHT - size * 0.5f, &held_x, &held_y);
    SDL_FRect held = {floorf(held_x - size * 0.5f), floorf(held_y), size, size};
    if (carrying && view == SPRITE_VIEW_BACK) {
        sprites_flush(sprites);
        render_item_stack(renderer, &held, (ItemType)colonist->carried.type);
        batch_flush(renderer);
    }
    draw_walker(sprites, sprite_body(looks.body, looks.skin, view), anchor_x, anchor_y, feet_y,
                gait, skin, flip);
    draw_walker(sprites, sprite_head(looks.skin, colonist_expression(colonist), view), anchor_x,
                anchor_y, feet_y, gait, skin, flip);
    if (eyes_closed && view == SPRITE_VIEW_FRONT) {
        draw_walker(sprites, sprite_closed_eyes(looks.skin), anchor_x, anchor_y, feet_y, gait, skin,
                    false);
    }
    if (looks.beard > 0) { /* No sprite from behind: nothing drawn. */
        draw_walker(sprites, sprite_beard(looks.beard, view), anchor_x, anchor_y, feet_y, gait,
                    hair, flip);
    }
    if (looks.hair_style > 0) {
        draw_walker(sprites, sprite_hair(looks.hair_style, view), anchor_x, anchor_y, feet_y, gait,
                    hair, flip);
    }
    render_hand(sprites, camera, colonist, slot, tick, feet_x, feet_y, skin);
    if (colonist->task == TASK_SLEEP) {
        render_sleep_zs(sprites, camera, slot, tick, anchor_x, anchor_y);
    }
    if (carrying && view != SPRITE_VIEW_BACK) {
        sprites_flush(sprites);
        render_item_stack(renderer, &held, (ItemType)colonist->carried.type);
        batch_flush(renderer);
    }
}

/* An animal seen from the side: standing, head down while it eats or drinks,
 * lying asleep; mirrored when it faces left, hopping and rocking as it walks.
 * Seen from the front or from behind while it walks down or up the screen. */
static void render_animal(SDL_Renderer *renderer, Sprites *sprites, const Camera *camera,
                          const Animal *animal, uint32_t slot, uint64_t tick, bool selected)
{
    float wx, wy, feet_x, feet_y, anchor_x, anchor_y;
    animal_position(animal, &wx, &wy);
    camera_world_to_screen(camera, wx, wy + COLONIST_FEET, &feet_x, &feet_y);
    camera_world_to_screen(camera, wx, wy + COLONIST_FEET + ANIMAL_SPRITE_MARGIN, &anchor_x,
                           &anchor_y);
    bool moving = animal_is_moving(animal);
    Heading heading = animal_heading(animal);
    AnimalPose pose = ANIMAL_POSE_STAND;
    if (animal->task == ANIMAL_TASK_SLEEP || animal->task == ANIMAL_TASK_DOWN) {
        pose = ANIMAL_POSE_SLEEP;
    } else if (!moving && (animal->task == ANIMAL_TASK_EAT || animal->task == ANIMAL_TASK_DRINK)) {
        pose = ANIMAL_POSE_EAT;
    } else if (heading == HEADING_SOUTH) {
        pose = ANIMAL_POSE_FRONT;
    } else if (heading == HEADING_NORTH) {
        pose = ANIMAL_POSE_BACK;
    }
    SpriteId id = sprite_animal((AnimalSpecies)animal->species, (AnimalSex)animal->sex, pose);
    if (selected) {
        sprites_flush(sprites);
        float width, height;
        sprites_size(sprites, id, &width, &height);
        float rx = SDL_max(4.0f, width * 0.36f);
        fill_ring(renderer, feet_x, feet_y, rx, SDL_max(2.0f, rx * 0.3f),
                  SDL_max(1.5f, camera->zoom * 0.04f), SELECTION_COLOR);
    }
    Gait gait = gait_at(
        moving, animal->task == ANIMAL_TASK_FLEE ? GAIT_RUN_STEP_TICKS : GAIT_WALK_STEP_TICKS, slot,
        tick, camera->zoom);
    draw_walker(sprites, id, anchor_x, anchor_y, feet_y, gait, WHITE, heading == HEADING_WEST);
}

typedef struct DepthEntry {
    float depth;
    uint32_t slot;
    SDL_FRect area; /* On screen. */
    bool animal;    /* An animal's slot, else a colonist's. */
} DepthEntry;

static int compare_depth(const void *a, const void *b)
{
    const DepthEntry *x = a, *y = b;
    if (x->depth != y->depth) {
        return x->depth < y->depth ? -1 : 1;
    }
    if (x->animal != y->animal) {
        return x->animal ? -1 : 1;
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

/* Whether a plant standing on (x, y, z) is to be drawn: as tile_seen(), but a
 * tree on a level below also shows when only its own trunk or foliage covers
 * its foot (its sprite is the whole tree, seen from above). */
static bool plant_seen(const Map *map, const Plant *plant, int x, int y, int z)
{
    if (!below_active) {
        return true;
    }
    int seen = below_view_level(x, y);
    if (seen == z) {
        return true;
    }
    if ((plant->type != PLANT_TREE && plant->type != PLANT_HAZEL) || seen < z ||
        seen - z >= TREE_MAX_LEVELS) {
        return false;
    }
    size_t index = map_index(map, x, y, seen);
    return map->rock[index] == ROCK_WOOD ||
           (map->rock[index] == ROCK_NONE && terrain_is_foliage(map->terrain[index]));
}

static void render_plant(Sprites *sprites, const Camera *camera, const Map *map, int x, int y,
                         int z, const DepthEntry *colonists, int colonist_count)
{
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    if (plant->type == PLANT_NONE || plant_is_dormant(plant) || !plant_seen(map, plant, x, y, z)) {
        return; /* Nothing above ground, or nothing seen. */
    }
    SpriteId id =
        sprite_plant((PlantType)plant->type, plant_growth_stage(plant), plant_has_food(plant));
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
        if (!tile_seen((int)floorf(wx), (int)floorf(wy), colonist->z)) {
            continue;
        }
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

/* One of the colonists and animals to draw, in depth order. */
static void render_entry(SDL_Renderer *renderer, Sprites *sprites, const Game *game,
                         const Camera *camera, const DepthEntry *entry, const EntityId *selected,
                         int selected_count, EntityId selected_animal)
{
    uint64_t tick = game->clock.tick;
    if (entry->animal) {
        EntityId id = animals_id_at(&game->animals, entry->slot);
        render_animal(renderer, sprites, camera, &game->animals.list[entry->slot], entry->slot,
                      tick, entity_id_equal(id, selected_animal));
    } else {
        render_colonist(
            renderer, sprites, camera, &game->colonists[entry->slot], entry->slot, tick,
            is_selected(game_colonist_id_at(game, entry->slot), selected, selected_count));
    }
}

void render_world_sprites(SDL_Renderer *renderer, Sprites *sprites, const Game *game,
                          const Camera *camera, const EntityId *selected, int selected_count,
                          EntityId selected_animal)
{
    const int z = camera->level; /* The level shown. */
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (!entity_id_is_none(id) && game->colonists[slot].z == z &&
            is_selected(id, selected, selected_count)) {
            render_path(renderer, camera, &game->colonists[slot]);
        }
    }

    const Map *map = game_view_map(game);
    TileRect visible = camera_visible_tiles(camera, map->width, map->height);
    int x0 = SDL_max(0, visible.x0 - SPRITE_OVERFLOW_COLUMNS);
    int x1 = SDL_min(map->width, visible.x1 + SPRITE_OVERFLOW_COLUMNS);
    int y1 = SDL_min(map->height, visible.y1 + SPRITE_OVERFLOW_ROWS);

    /* Colonists and animals in depth order, merged with the rows of plants below. */
    static DepthEntry order[COLONIST_MAX + ANIMAL_MAX];
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game) && count < COLONIST_MAX; slot++) {
        if (entity_id_is_none(game_colonist_id_at(game, slot)) || game->colonists[slot].z != z) {
            continue;
        }
        float wx, wy;
        colonist_position(&game->colonists[slot], &wx, &wy);
        if (!tile_seen((int)floorf(wx), (int)floorf(wy), z)) {
            continue; /* Under a higher floor (the levels below). */
        }
        if (wx < (float)x0 - 1.0f || wx > (float)x1 + 1.0f || wy < (float)visible.y0 - 1.0f ||
            wy > (float)y1 + 1.0f) {
            continue;
        }
        float sx, sy;
        camera_world_to_screen(camera, wx, wy + COLONIST_FEET, &sx, &sy);
        float half_width = COLONIST_HALF_WIDTH * camera->zoom;
        float height = COLONIST_HEIGHT * camera->zoom;
        SDL_FRect area = {sx - half_width, sy - height, 2.0f * half_width, height};
        order[count++] = (DepthEntry){wy + COLONIST_FEET, slot, area, false};
    }
    /* Animals: only those the colony sees now (nobody knows where the others are). */
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        EntityId id = animals_id_at(&game->animals, slot);
        const Animal *animal = &game->animals.list[slot];
        if (entity_id_is_none(id) || animal->z != z) {
            continue;
        }
        float wx, wy;
        animal_position(animal, &wx, &wy);
        if (wx < (float)x0 - 2.0f || wx > (float)x1 + 2.0f || wy < (float)visible.y0 - 1.0f ||
            wy > (float)y1 + 2.0f || !tile_seen((int)floorf(wx), (int)floorf(wy), z) ||
            !game_animal_seen(game, id)) {
            continue;
        }
        float sx, sy;
        camera_world_to_screen(camera, wx, wy + COLONIST_FEET, &sx, &sy);
        float half_width = ANIMAL_HALF_WIDTH * camera->zoom;
        float height = ANIMAL_HEIGHT * camera->zoom;
        SDL_FRect area = {sx - half_width, sy - height, 2.0f * half_width, height};
        order[count++] = (DepthEntry){wy + COLONIST_FEET, slot, area, true};
    }
    qsort(order, (size_t)count, sizeof(order[0]), compare_depth);

    sprites_begin(sprites, camera->zoom);
    int next = 0;
    for (int y = visible.y0; y < y1; y++) {
        while (next < count && order[next].depth < (float)y + PLANT_DEPTH) {
            render_entry(renderer, sprites, game, camera, &order[next++], selected, selected_count,
                         selected_animal);
        }
        for (int x = x0; x < x1; x++) {
            render_plant(sprites, camera, map, x, y, z, order, count);
        }
    }
    while (next < count) {
        render_entry(renderer, sprites, game, camera, &order[next++], selected, selected_count,
                     selected_animal);
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
