#include "game/sprites.h"

#include "core/log.h"
#include "core/util.h"

#include <math.h>

/* Copies of the atlas, each half the size of the previous one: 128 sprite
 * pixels per tile down to 4, the smallest zoom. */
#define SPRITE_LEVELS 6
/* Sprite sizes are multiples of this, and so are their places in the atlas,
 * so that every level can be computed by halving without mixing sprites. */
#define SPRITE_ALIGN 32
/* Empty space between sprites: filtering never reaches a neighbor. */
#define SPRITE_GUTTER SPRITE_ALIGN
#define ATLAS_WIDTH 2048
#define SPRITE_BATCH_QUADS 4096

/* File names of the variants, as written by tools/generate_sprites.py. */
static const char *const BODY_NAMES[APPEARANCE_BODY_COUNT] = {"thin", "average", "heavy"};
static const char *const EXPRESSION_NAMES[EXPRESSION_COUNT] = {
    [EXPRESSION_NEUTRAL] = "neutral",
    [EXPRESSION_SMILING] = "smiling",
    [EXPRESSION_SAD] = "sad",
    [EXPRESSION_ANGRY] = "angry",
};
static const char *const HAIR_NAMES[APPEARANCE_HAIR_STYLE_COUNT] = {
    NULL, "short", "long", "curly", "bun", "braids"};
static const char *const BEARD_NAMES[APPEARANCE_BEARD_COUNT] = {NULL, "stubble", "short", "full",
                                                               "mustache"};
/* Tools with a sprite, and their file names. */
static const ItemType TOOL_TYPES[] = {ITEM_COBBLE, ITEM_CHOPPER, ITEM_FLAKE, ITEM_HAND_AXE};
static const char *const TOOL_NAMES[] = {"cobble", "chopper", "flake", "hand_axe"};
#define TOOL_SPRITE_COUNT ((int)SDL_arraysize(TOOL_TYPES))

enum {
    TREE_FIRST = 0,
    BUSH_FIRST = TREE_FIRST + PLANT_GROWTH_STAGES,
    BUSH_FRUIT_FIRST = BUSH_FIRST + PLANT_GROWTH_STAGES,
    BODY_FIRST = BUSH_FRUIT_FIRST + PLANT_GROWTH_STAGES,
    HEAD_FIRST = BODY_FIRST + APPEARANCE_BODY_COUNT * APPEARANCE_SKIN_COUNT,
    BEARD_FIRST = HEAD_FIRST + APPEARANCE_SKIN_COUNT * EXPRESSION_COUNT,
    HAIR_FIRST = BEARD_FIRST + APPEARANCE_BEARD_COUNT - 1,
    FIST_FIRST = HAIR_FIRST + APPEARANCE_HAIR_STYLE_COUNT - 1,
    TOOL_FIRST = FIST_FIRST + APPEARANCE_SKIN_COUNT,
    SPRITE_COUNT = TOOL_FIRST + 4
};
SDL_COMPILE_TIME_ASSERT(tool_sprites, SDL_arraysize(TOOL_TYPES) == SPRITE_COUNT - TOOL_FIRST &&
                                          SDL_arraysize(TOOL_NAMES) == SDL_arraysize(TOOL_TYPES));

SpriteId sprite_tree(int stage)
{
    return TREE_FIRST + clampi(stage, 0, PLANT_GROWTH_STAGES - 1);
}

SpriteId sprite_bush(int stage, bool fruit)
{
    return (fruit ? BUSH_FRUIT_FIRST : BUSH_FIRST) + clampi(stage, 0, PLANT_GROWTH_STAGES - 1);
}

SpriteId sprite_body(int body, int skin)
{
    return BODY_FIRST + clampi(body, 0, APPEARANCE_BODY_COUNT - 1) * APPEARANCE_SKIN_COUNT +
           clampi(skin, 0, APPEARANCE_SKIN_COUNT - 1);
}

SpriteId sprite_head(int skin, ColonistExpression expression)
{
    return HEAD_FIRST + clampi(skin, 0, APPEARANCE_SKIN_COUNT - 1) * EXPRESSION_COUNT +
           clampi((int)expression, 0, EXPRESSION_COUNT - 1);
}

SpriteId sprite_beard(int style)
{
    return BEARD_FIRST + clampi(style, 1, APPEARANCE_BEARD_COUNT - 1) - 1;
}

SpriteId sprite_hair(int style)
{
    return HAIR_FIRST + clampi(style, 1, APPEARANCE_HAIR_STYLE_COUNT - 1) - 1;
}

SpriteId sprite_fist(int skin)
{
    return FIST_FIRST + clampi(skin, 0, APPEARANCE_SKIN_COUNT - 1);
}

SpriteId sprite_tool(ItemType type)
{
    for (int i = 0; i < TOOL_SPRITE_COUNT; i++) {
        if (TOOL_TYPES[i] == type) {
            return TOOL_FIRST + i;
        }
    }
    return -1;
}

/* File name (without directory nor extension) of a sprite. */
static void sprite_file_name(SpriteId id, char *out, size_t size)
{
    if (id < BUSH_FIRST) {
        SDL_snprintf(out, size, "tree_%d", id - TREE_FIRST);
    } else if (id < BUSH_FRUIT_FIRST) {
        SDL_snprintf(out, size, "bush_%d", id - BUSH_FIRST);
    } else if (id < BODY_FIRST) {
        SDL_snprintf(out, size, "bush_berries_%d", id - BUSH_FRUIT_FIRST);
    } else if (id < HEAD_FIRST) {
        int index = id - BODY_FIRST;
        SDL_snprintf(out, size, "body_%s_%d", BODY_NAMES[index / APPEARANCE_SKIN_COUNT],
                     index % APPEARANCE_SKIN_COUNT);
    } else if (id < BEARD_FIRST) {
        int index = id - HEAD_FIRST;
        SDL_snprintf(out, size, "head_%d_%s", index / EXPRESSION_COUNT,
                     EXPRESSION_NAMES[index % EXPRESSION_COUNT]);
    } else if (id < HAIR_FIRST) {
        SDL_snprintf(out, size, "beard_%s", BEARD_NAMES[id - BEARD_FIRST + 1]);
    } else if (id < FIST_FIRST) {
        SDL_snprintf(out, size, "hair_%s", HAIR_NAMES[id - HAIR_FIRST + 1]);
    } else if (id < TOOL_FIRST) {
        SDL_snprintf(out, size, "fist_%d", id - FIST_FIRST);
    } else {
        SDL_snprintf(out, size, "tool_%s", TOOL_NAMES[id - TOOL_FIRST]);
    }
}

struct Sprites {
    SDL_Renderer *renderer;
    SDL_Texture *levels[SPRITE_LEVELS];
    int atlas_width, atlas_height; /* Of level 0. */
    SDL_Rect rects[SPRITE_COUNT];  /* Places in level 0. */
    /* Batch. */
    int level;
    float pixels_per_tile;
    SDL_Vertex vertices[SPRITE_BATCH_QUADS * 4];
    int indices[SPRITE_BATCH_QUADS * 6];
    int count;
};

/* RGBA pixels of one atlas level, 4 bytes per pixel, rows without padding. */
typedef struct Pixels {
    uint8_t *data;
    int width, height;
} Pixels;

/* Half-size copy. Colors are averaged weighted by their opacity, so that
 * transparent pixels do not darken the edges. */
static bool halve(const Pixels *source, Pixels *out)
{
    out->width = source->width / 2;
    out->height = source->height / 2;
    out->data = SDL_malloc((size_t)out->width * (size_t)out->height * 4);
    if (out->data == NULL) {
        return false;
    }
    for (int y = 0; y < out->height; y++) {
        for (int x = 0; x < out->width; x++) {
            int color[3] = {0, 0, 0}, plain[3] = {0, 0, 0};
            int alpha = 0;
            for (int k = 0; k < 4; k++) {
                const uint8_t *p = source->data + (((size_t)(y * 2 + k / 2) * source->width) +
                                                   (size_t)(x * 2 + k % 2)) * 4;
                for (int c = 0; c < 3; c++) {
                    color[c] += p[c] * p[3];
                    plain[c] += p[c];
                }
                alpha += p[3];
            }
            uint8_t *q = out->data + ((size_t)y * out->width + (size_t)x) * 4;
            for (int c = 0; c < 3; c++) {
                q[c] = (uint8_t)(alpha > 0 ? (color[c] + alpha / 2) / alpha : (plain[c] + 2) / 4);
            }
            q[3] = (uint8_t)((alpha + 2) / 4);
        }
    }
    return true;
}

static SDL_Surface *load_sprite(const char *assets_dir, SpriteId id)
{
    char name[64], path[1200];
    sprite_file_name(id, name, sizeof(name));
    SDL_snprintf(path, sizeof(path), "%ssprites/%s.png", assets_dir, name);
    SDL_Surface *loaded = SDL_LoadPNG(path);
    if (loaded == NULL) {
        LOG_ERROR("Cannot load sprite %s: %s", path, SDL_GetError());
        return NULL;
    }
    SDL_Surface *surface = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (surface == NULL) {
        LOG_ERROR("Cannot convert sprite %s: %s", path, SDL_GetError());
        return NULL;
    }
    if (surface->w % SPRITE_ALIGN != 0 || surface->h % SPRITE_ALIGN != 0 ||
        surface->w + SPRITE_GUTTER > ATLAS_WIDTH) {
        LOG_ERROR("Sprite %s is %dx%d: sizes must be multiples of %d", path, surface->w,
                  surface->h, SPRITE_ALIGN);
        SDL_DestroySurface(surface);
        return NULL;
    }
    return surface;
}

/* Shelf packing, tallest sprites first. Fills `rects` and the atlas height. */
static void pack(Sprites *sprites, SDL_Surface *const surfaces[SPRITE_COUNT])
{
    int order[SPRITE_COUNT];
    for (int i = 0; i < SPRITE_COUNT; i++) {
        int j = i;
        while (j > 0 && surfaces[order[j - 1]]->h < surfaces[i]->h) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    int x = 0, shelf_y = 0, shelf_height = 0;
    for (int i = 0; i < SPRITE_COUNT; i++) {
        const SDL_Surface *surface = surfaces[order[i]];
        if (x + surface->w + SPRITE_GUTTER > ATLAS_WIDTH) {
            x = 0;
            shelf_y += shelf_height;
            shelf_height = 0;
        }
        sprites->rects[order[i]] = (SDL_Rect){x, shelf_y, surface->w, surface->h};
        x += surface->w + SPRITE_GUTTER;
        shelf_height = SDL_max(shelf_height, surface->h + SPRITE_GUTTER);
    }
    sprites->atlas_width = ATLAS_WIDTH;
    sprites->atlas_height = shelf_y + shelf_height;
}

static SDL_Texture *create_level_texture(SDL_Renderer *renderer, const Pixels *pixels)
{
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STATIC, pixels->width,
                                             pixels->height);
    if (texture == NULL) {
        LOG_ERROR("Cannot create the sprite atlas: %s", SDL_GetError());
        return NULL;
    }
    SDL_UpdateTexture(texture, NULL, pixels->data, pixels->width * 4);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return texture;
}

/* Builds the atlas from the loaded surfaces and uploads every level. */
static bool build_atlas(Sprites *sprites, SDL_Surface *const surfaces[SPRITE_COUNT])
{
    pack(sprites, surfaces);
    int max_size = (int)SDL_GetNumberProperty(SDL_GetRendererProperties(sprites->renderer),
                                              SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
    if (max_size > 0 && (sprites->atlas_width > max_size || sprites->atlas_height > max_size)) {
        LOG_ERROR("Sprite atlas %dx%d exceeds the texture size limit %d", sprites->atlas_width,
                  sprites->atlas_height, max_size);
        return false;
    }
    Pixels level = {SDL_calloc((size_t)sprites->atlas_width * (size_t)sprites->atlas_height, 4),
                    sprites->atlas_width, sprites->atlas_height};
    if (level.data == NULL) {
        LOG_ERROR("Out of memory for the sprite atlas");
        return false;
    }
    for (int i = 0; i < SPRITE_COUNT; i++) {
        const SDL_Surface *surface = surfaces[i];
        SDL_Rect rect = sprites->rects[i];
        for (int row = 0; row < surface->h; row++) {
            SDL_memcpy(level.data + ((size_t)(rect.y + row) * level.width + (size_t)rect.x) * 4,
                       (const uint8_t *)surface->pixels + (size_t)row * (size_t)surface->pitch,
                       (size_t)surface->w * 4);
        }
    }
    bool ok = true;
    for (int k = 0; k < SPRITE_LEVELS && ok; k++) {
        sprites->levels[k] = create_level_texture(sprites->renderer, &level);
        ok = sprites->levels[k] != NULL;
        if (ok && k + 1 < SPRITE_LEVELS) {
            Pixels smaller;
            ok = halve(&level, &smaller);
            if (!ok) {
                LOG_ERROR("Out of memory for the sprite atlas");
            }
            SDL_free(level.data);
            level = ok ? smaller : (Pixels){0};
        }
    }
    SDL_free(level.data);
    return ok;
}

Sprites *sprites_create(SDL_Renderer *renderer, const char *assets_dir)
{
    Sprites *sprites = SDL_calloc(1, sizeof(*sprites));
    if (sprites == NULL) {
        LOG_ERROR("Out of memory for the sprites");
        return NULL;
    }
    sprites->renderer = renderer;
    for (int quad = 0; quad < SPRITE_BATCH_QUADS; quad++) {
        static const int CORNERS[6] = {0, 1, 2, 2, 3, 0};
        for (int i = 0; i < 6; i++) {
            sprites->indices[quad * 6 + i] = quad * 4 + CORNERS[i];
        }
    }
    SDL_Surface *surfaces[SPRITE_COUNT] = {0};
    bool ok = true;
    for (int i = 0; i < SPRITE_COUNT && ok; i++) {
        surfaces[i] = load_sprite(assets_dir, i);
        ok = surfaces[i] != NULL;
    }
    if (ok) {
        ok = build_atlas(sprites, surfaces);
    }
    for (int i = 0; i < SPRITE_COUNT; i++) {
        SDL_DestroySurface(surfaces[i]);
    }
    if (!ok) {
        sprites_destroy(sprites);
        return NULL;
    }
    LOG_INFO("Loaded %d sprites into a %dx%d atlas", SPRITE_COUNT, sprites->atlas_width,
             sprites->atlas_height);
    return sprites;
}

void sprites_destroy(Sprites *sprites)
{
    if (sprites == NULL) {
        return;
    }
    for (int k = 0; k < SPRITE_LEVELS; k++) {
        if (sprites->levels[k] != NULL) {
            SDL_DestroyTexture(sprites->levels[k]);
        }
    }
    SDL_free(sprites);
}

void sprites_begin(Sprites *sprites, float pixels_per_tile)
{
    sprites_flush(sprites);
    /* The smallest copy that is still at least as detailed as the screen. */
    int level = 0;
    while (level + 1 < SPRITE_LEVELS &&
           (float)(SPRITE_PIXELS_PER_TILE >> (level + 1)) >= pixels_per_tile * 0.999f) {
        level++;
    }
    sprites->level = level;
    sprites->pixels_per_tile = pixels_per_tile;
}

void sprites_flush(Sprites *sprites)
{
    if (sprites->count > 0) {
        SDL_RenderGeometry(sprites->renderer, sprites->levels[sprites->level], sprites->vertices,
                           sprites->count * 4, sprites->indices, sprites->count * 6);
        sprites->count = 0;
    }
}

void sprites_size(const Sprites *sprites, SpriteId id, float *width, float *height)
{
    float scale = sprites->pixels_per_tile / (float)SPRITE_PIXELS_PER_TILE;
    bool valid = id >= 0 && id < SPRITE_COUNT;
    *width = valid ? (float)sprites->rects[id].w * scale : 0.0f;
    *height = valid ? (float)sprites->rects[id].h * scale : 0.0f;
}

/* Queues a quad given its four screen corners (clockwise from the top left
 * of the unmirrored sprite). */
static void queue_quad(Sprites *sprites, SpriteId id, const SDL_FPoint corners[4],
                       SDL_FColor color, bool flip)
{
    if (sprites->count == SPRITE_BATCH_QUADS) {
        sprites_flush(sprites);
    }
    SDL_Rect rect = sprites->rects[id];
    /* Texture coordinates are the same at every level: they are fractions. */
    float u0 = (float)rect.x / (float)sprites->atlas_width;
    float u1 = (float)(rect.x + rect.w) / (float)sprites->atlas_width;
    float v0 = (float)rect.y / (float)sprites->atlas_height;
    float v1 = (float)(rect.y + rect.h) / (float)sprites->atlas_height;
    if (flip) {
        float swap = u0;
        u0 = u1;
        u1 = swap;
    }
    SDL_Vertex *vertex = &sprites->vertices[sprites->count * 4];
    vertex[0] = (SDL_Vertex){corners[0], color, {u0, v0}};
    vertex[1] = (SDL_Vertex){corners[1], color, {u1, v0}};
    vertex[2] = (SDL_Vertex){corners[2], color, {u1, v1}};
    vertex[3] = (SDL_Vertex){corners[3], color, {u0, v1}};
    sprites->count++;
}

void sprites_draw(Sprites *sprites, SpriteId id, float anchor_x, float anchor_y, SDL_FColor color,
                  bool flip)
{
    if (id < 0 || id >= SPRITE_COUNT) {
        return;
    }
    float width, height;
    sprites_size(sprites, id, &width, &height);
    float left = floorf(anchor_x - width * 0.5f);
    float top = floorf(anchor_y - height);
    SDL_FPoint corners[4] = {
        {left, top}, {left + width, top}, {left + width, top + height}, {left, top + height}};
    queue_quad(sprites, id, corners, color, flip);
}

void sprites_draw_rotated(Sprites *sprites, SpriteId id, float center_x, float center_y,
                          float degrees, SDL_FColor color, bool flip)
{
    if (id < 0 || id >= SPRITE_COUNT) {
        return;
    }
    float width, height;
    sprites_size(sprites, id, &width, &height);
    float radians = degrees * (SDL_PI_F / 180.0f);
    float c = SDL_cosf(radians), s = SDL_sinf(radians);
    static const float SIGNS[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    SDL_FPoint corners[4];
    for (int i = 0; i < 4; i++) {
        float x = SIGNS[i][0] * width * 0.5f;
        float y = SIGNS[i][1] * height * 0.5f;
        /* Clockwise on screen, where y points down. */
        corners[i] = (SDL_FPoint){center_x + x * c - y * s, center_y + x * s + y * c};
    }
    queue_quad(sprites, id, corners, color, flip);
}
