#include "game/sprites.h"

#include "core/log.h"
#include "core/tree.h"
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
static const char *const HAIR_NAMES[APPEARANCE_HAIR_STYLE_COUNT] = {NULL,    "short", "long",
                                                                    "curly", "bun",   "braids"};
static const char *const BEARD_NAMES[APPEARANCE_BEARD_COUNT] = {NULL, "stubble", "short", "full",
                                                                "mustache"};
/* What the file names of the views end with. */
static const char *const VIEW_SUFFIXES[SPRITE_VIEW_COUNT] = {
    [SPRITE_VIEW_FRONT] = "",
    [SPRITE_VIEW_SIDE] = "_side",
    [SPRITE_VIEW_BACK] = "_back",
};
/* Heads of a skin: every expression from the front, then from the side, and
 * one from behind. Beards have no view from behind. */
#define HEADS_PER_SKIN (EXPRESSION_COUNT * 2 + 1)
#define BEARD_VIEW_COUNT 2
/* Plants with sprites: file name, sizes drawn (the growth stages drawn are
 * shared out among them), the growth stages drawn (a tree's later ones are
 * trunk and foliage tiles, see core/tree.h), and whether a variant shows its
 * food (fruit, nuts). */
typedef struct PlantSprites {
    PlantType type;
    const char *name;
    int sizes;
    int stages;
    bool fruit;
} PlantSprites;
static const PlantSprites PLANT_SPRITES[] = {
    {PLANT_TREE, "tree", TREE_VOLUME_MIN_STAGE, TREE_VOLUME_MIN_STAGE, false},
    {PLANT_HAZEL, "hazel", TREE_VOLUME_MIN_STAGE, TREE_VOLUME_MIN_STAGE, false},
    {PLANT_BRAMBLE, "bramble", PLANT_GROWTH_STAGES, PLANT_GROWTH_STAGES, true},
    {PLANT_WILD_GARLIC, "wild_garlic", 3, PLANT_GROWTH_STAGES, false},
    {PLANT_NETTLE, "nettle", 3, PLANT_GROWTH_STAGES, false},
    {PLANT_PLANTAIN, "plantain", 3, PLANT_GROWTH_STAGES, false},
    {PLANT_WILD_CARROT, "wild_carrot", 3, PLANT_GROWTH_STAGES, false},
    {PLANT_CATTAIL, "cattail", 3, PLANT_GROWTH_STAGES, false},
    {PLANT_MUSHROOMS, "mushrooms", 1, PLANT_GROWTH_STAGES, false},
    {PLANT_MOSS, "moss", 3, PLANT_GROWTH_STAGES, false},
    {PLANT_WOOD_SORREL, "wood_sorrel", 3, PLANT_GROWTH_STAGES, false},
    {PLANT_BRACKEN, "bracken", 3, PLANT_GROWTH_STAGES, false},
    {PLANT_WILD_STRAWBERRY, "wild_strawberry", 3, PLANT_GROWTH_STAGES, true},
    {PLANT_BILBERRY, "bilberry", 3, PLANT_GROWTH_STAGES, true},
    {PLANT_DOG_ROSE, "dog_rose", 3, PLANT_GROWTH_STAGES, true},
    {PLANT_HEATHER, "heather", 3, PLANT_GROWTH_STAGES, false},
    {PLANT_REED, "reed", 3, PLANT_GROWTH_STAGES, false},
};
/* Sum of the sizes (twice for plants with a food variant), checked when the
 * sprites are loaded. */
#define PLANT_SPRITE_COUNT 63

/* Tools with a sprite, and their file names. */
static const ItemType TOOL_TYPES[] = {ITEM_COBBLE, ITEM_CHOPPER, ITEM_FLAKE, ITEM_HAND_AXE};
static const char *const TOOL_NAMES[] = {"cobble", "chopper", "flake", "hand_axe"};
#define TOOL_SPRITE_COUNT ((int)SDL_arraysize(TOOL_TYPES))

/* Animals: a sprite per species (AnimalDef.id in the file name), sex and pose. */
static const char *const ANIMAL_SEX_NAMES[ANIMAL_SEX_COUNT] = {
    [ANIMAL_FEMALE] = "female",
    [ANIMAL_MALE] = "male",
};
static const char *const ANIMAL_POSE_NAMES[ANIMAL_POSE_COUNT] = {
    [ANIMAL_POSE_STAND] = "stand", [ANIMAL_POSE_EAT] = "eat",   [ANIMAL_POSE_SLEEP] = "sleep",
    [ANIMAL_POSE_FRONT] = "front", [ANIMAL_POSE_BACK] = "back",
};
#define ANIMAL_SPRITE_COUNT (ANIMAL_SPECIES_COUNT * ANIMAL_SEX_COUNT * ANIMAL_POSE_COUNT)

/* Tile textures: TILE_SPRITE_SIZE square files, the texture of one tile with
 * a copy of its opposite edges around it (TILE_SPRITE_PADDING pixels on each
 * side), so that filtering at the edge of a tile blends it with the next
 * tile, never with a neighbor in the atlas. */
#define TILE_SPRITE_PADDING SPRITE_ALIGN
#define TILE_SPRITE_SIZE (SPRITE_PIXELS_PER_TILE + 2 * TILE_SPRITE_PADDING)
static const Terrain GROUND_TERRAINS[] = {
    TERRAIN_DEEP_WATER, TERRAIN_SHALLOW_WATER,  TERRAIN_SOIL,          TERRAIN_RICH_SOIL,
    TERRAIN_SAND,       TERRAIN_GRAVEL,         TERRAIN_ROUGH_STONE,   TERRAIN_FOLIAGE,
    TERRAIN_GRASS_ROOF, TERRAIN_FOLIAGE_MEDIUM, TERRAIN_FOLIAGE_SPARSE};
static const char *const GROUND_NAMES[] = {
    "deep_water",  "shallow_water", "soil",       "rich_soil",      "sand",          "gravel",
    "rough_stone", "foliage",       "grass_roof", "foliage_medium", "foliage_sparse"};
static const RockType WALL_ROCKS[] = {ROCK_GRANITE, ROCK_SANDSTONE, ROCK_EARTH};
static const char *const WALL_NAMES[] = {"granite", "sandstone", "earth"};
#define GROUND_SPRITE_COUNT 11
#define WALL_SPRITE_COUNT 3
#define GRASS_SPRITE_COUNT 4 /* Growth stages. */

enum {
    PLANT_FIRST = 0,
    BODY_FIRST = PLANT_FIRST + PLANT_SPRITE_COUNT,
    HEAD_FIRST = BODY_FIRST + APPEARANCE_BODY_COUNT * APPEARANCE_SKIN_COUNT * SPRITE_VIEW_COUNT,
    BEARD_FIRST = HEAD_FIRST + APPEARANCE_SKIN_COUNT * HEADS_PER_SKIN,
    HAIR_FIRST = BEARD_FIRST + (APPEARANCE_BEARD_COUNT - 1) * BEARD_VIEW_COUNT,
    FIST_FIRST = HAIR_FIRST + (APPEARANCE_HAIR_STYLE_COUNT - 1) * SPRITE_VIEW_COUNT,
    TOOL_FIRST = FIST_FIRST + APPEARANCE_SKIN_COUNT,
    EYES_FIRST = TOOL_FIRST + 4,
    SLEEP_Z_FIRST = EYES_FIRST + APPEARANCE_SKIN_COUNT,
    ANIMAL_FIRST = SLEEP_Z_FIRST + 1,
    GROUND_FIRST = ANIMAL_FIRST + ANIMAL_SPRITE_COUNT,
    WALL_FIRST = GROUND_FIRST + GROUND_SPRITE_COUNT,
    GRASS_FIRST = WALL_FIRST + WALL_SPRITE_COUNT,
    SPRITE_COUNT = GRASS_FIRST + GRASS_SPRITE_COUNT
};
SDL_COMPILE_TIME_ASSERT(tool_sprites, SDL_arraysize(TOOL_TYPES) == EYES_FIRST - TOOL_FIRST &&
                                          SDL_arraysize(TOOL_NAMES) == SDL_arraysize(TOOL_TYPES));
SDL_COMPILE_TIME_ASSERT(ground_sprites, SDL_arraysize(GROUND_TERRAINS) == GROUND_SPRITE_COUNT &&
                                            SDL_arraysize(GROUND_NAMES) == GROUND_SPRITE_COUNT);
SDL_COMPILE_TIME_ASSERT(wall_sprites, SDL_arraysize(WALL_ROCKS) == WALL_SPRITE_COUNT &&
                                          SDL_arraysize(WALL_NAMES) == WALL_SPRITE_COUNT);

static bool is_tile_sprite(SpriteId id)
{
    return id >= GROUND_FIRST && id < SPRITE_COUNT;
}

/* Sprites of the plants before entry `index` of PLANT_SPRITES. */
static int plant_sprites_before(int index)
{
    int count = 0;
    for (int i = 0; i < index; i++) {
        count += PLANT_SPRITES[i].sizes * (PLANT_SPRITES[i].fruit ? 2 : 1);
    }
    return count;
}

SpriteId sprite_plant(PlantType type, int stage, bool fruit)
{
    for (int i = 0; i < (int)SDL_arraysize(PLANT_SPRITES); i++) {
        const PlantSprites *plant = &PLANT_SPRITES[i];
        if (plant->type == type) {
            if (stage >= plant->stages) {
                return -1;
            }
            int size = clampi(stage, 0, plant->stages - 1) * plant->sizes / plant->stages;
            return PLANT_FIRST + plant_sprites_before(i) +
                   (fruit && plant->fruit ? plant->sizes : 0) + size;
        }
    }
    return -1;
}

static int view_index(SpriteView view)
{
    return clampi((int)view, 0, SPRITE_VIEW_COUNT - 1);
}

SpriteId sprite_body(int body, int skin, SpriteView view)
{
    return BODY_FIRST +
           (view_index(view) * APPEARANCE_BODY_COUNT + clampi(body, 0, APPEARANCE_BODY_COUNT - 1)) *
               APPEARANCE_SKIN_COUNT +
           clampi(skin, 0, APPEARANCE_SKIN_COUNT - 1);
}

SpriteId sprite_head(int skin, ColonistExpression expression, SpriteView view)
{
    int first = HEAD_FIRST + clampi(skin, 0, APPEARANCE_SKIN_COUNT - 1) * HEADS_PER_SKIN;
    if (view_index(view) == SPRITE_VIEW_BACK) {
        return first + EXPRESSION_COUNT * 2;
    }
    return first + view_index(view) * EXPRESSION_COUNT +
           clampi((int)expression, 0, EXPRESSION_COUNT - 1);
}

SpriteId sprite_beard(int style, SpriteView view)
{
    if (view_index(view) == SPRITE_VIEW_BACK) {
        return -1;
    }
    return BEARD_FIRST + view_index(view) * (APPEARANCE_BEARD_COUNT - 1) +
           clampi(style, 1, APPEARANCE_BEARD_COUNT - 1) - 1;
}

SpriteId sprite_hair(int style, SpriteView view)
{
    return HAIR_FIRST + view_index(view) * (APPEARANCE_HAIR_STYLE_COUNT - 1) +
           clampi(style, 1, APPEARANCE_HAIR_STYLE_COUNT - 1) - 1;
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

SpriteId sprite_closed_eyes(int skin)
{
    return EYES_FIRST + clampi(skin, 0, APPEARANCE_SKIN_COUNT - 1);
}

SpriteId sprite_sleep_z(void)
{
    return SLEEP_Z_FIRST;
}

SpriteId sprite_animal(AnimalSpecies species, AnimalSex sex, AnimalPose pose)
{
    return ANIMAL_FIRST +
           (clampi((int)species, 0, ANIMAL_SPECIES_COUNT - 1) * ANIMAL_SEX_COUNT +
            clampi((int)sex, 0, ANIMAL_SEX_COUNT - 1)) *
               ANIMAL_POSE_COUNT +
           clampi((int)pose, 0, ANIMAL_POSE_COUNT - 1);
}

SpriteId sprite_ground(Terrain terrain)
{
    for (int i = 0; i < GROUND_SPRITE_COUNT; i++) {
        if (GROUND_TERRAINS[i] == terrain) {
            return GROUND_FIRST + i;
        }
    }
    return -1;
}

SpriteId sprite_wall(RockType rock)
{
    for (int i = 0; i < WALL_SPRITE_COUNT; i++) {
        if (WALL_ROCKS[i] == rock) {
            return WALL_FIRST + i;
        }
    }
    return -1;
}

SpriteId sprite_grass(float growth)
{
    return GRASS_FIRST +
           clampi((int)(growth * (float)GRASS_SPRITE_COUNT), 0, GRASS_SPRITE_COUNT - 1);
}

/* File name (without directory nor extension) of a sprite. */
static void sprite_file_name(SpriteId id, char *out, size_t size)
{
    if (id < BODY_FIRST) {
        int index = 0, first = PLANT_FIRST;
        while (index + 1 < (int)SDL_arraysize(PLANT_SPRITES) &&
               id >= PLANT_FIRST + plant_sprites_before(index + 1)) {
            index++;
        }
        first += plant_sprites_before(index);
        const PlantSprites *plant = &PLANT_SPRITES[index];
        int offset = id - first;
        bool fruit = offset >= plant->sizes;
        SDL_snprintf(out, size, fruit ? "%s_fruit_%d" : "%s_%d", plant->name,
                     offset % plant->sizes);
    } else if (id < HEAD_FIRST) {
        int index = id - BODY_FIRST;
        SDL_snprintf(out, size, "body_%s_%d%s",
                     BODY_NAMES[index / APPEARANCE_SKIN_COUNT % APPEARANCE_BODY_COUNT],
                     index % APPEARANCE_SKIN_COUNT,
                     VIEW_SUFFIXES[index / (APPEARANCE_SKIN_COUNT * APPEARANCE_BODY_COUNT)]);
    } else if (id < BEARD_FIRST) {
        int index = id - HEAD_FIRST;
        int skin = index / HEADS_PER_SKIN, head = index % HEADS_PER_SKIN;
        if (head == EXPRESSION_COUNT * 2) {
            SDL_snprintf(out, size, "head_%d_back", skin);
        } else {
            SDL_snprintf(out, size, "head_%d_%s%s", skin, EXPRESSION_NAMES[head % EXPRESSION_COUNT],
                         VIEW_SUFFIXES[head / EXPRESSION_COUNT]);
        }
    } else if (id < HAIR_FIRST) {
        int index = id - BEARD_FIRST, styles = APPEARANCE_BEARD_COUNT - 1;
        SDL_snprintf(out, size, "beard_%s%s", BEARD_NAMES[index % styles + 1],
                     VIEW_SUFFIXES[index / styles]);
    } else if (id < FIST_FIRST) {
        int index = id - HAIR_FIRST, styles = APPEARANCE_HAIR_STYLE_COUNT - 1;
        SDL_snprintf(out, size, "hair_%s%s", HAIR_NAMES[index % styles + 1],
                     VIEW_SUFFIXES[index / styles]);
    } else if (id < TOOL_FIRST) {
        SDL_snprintf(out, size, "fist_%d", id - FIST_FIRST);
    } else if (id < EYES_FIRST) {
        SDL_snprintf(out, size, "tool_%s", TOOL_NAMES[id - TOOL_FIRST]);
    } else if (id < SLEEP_Z_FIRST) {
        SDL_snprintf(out, size, "eyes_closed_%d", id - EYES_FIRST);
    } else if (id < ANIMAL_FIRST) {
        SDL_snprintf(out, size, "sleep_z");
    } else if (id < GROUND_FIRST) {
        int index = id - ANIMAL_FIRST;
        SDL_snprintf(
            out, size, "animal_%s_%s_%s",
            animal_def((AnimalSpecies)(index / (ANIMAL_SEX_COUNT * ANIMAL_POSE_COUNT)))->id,
            ANIMAL_SEX_NAMES[index / ANIMAL_POSE_COUNT % ANIMAL_SEX_COUNT],
            ANIMAL_POSE_NAMES[index % ANIMAL_POSE_COUNT]);
    } else if (id < WALL_FIRST) {
        SDL_snprintf(out, size, "ground_%s", GROUND_NAMES[id - GROUND_FIRST]);
    } else if (id < GRASS_FIRST) {
        SDL_snprintf(out, size, "wall_%s", WALL_NAMES[id - WALL_FIRST]);
    } else {
        SDL_snprintf(out, size, "grass_%d", id - GRASS_FIRST);
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
                const uint8_t *p =
                    source->data +
                    (((size_t)(y * 2 + k / 2) * source->width) + (size_t)(x * 2 + k % 2)) * 4;
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
        LOG_ERROR("Sprite %s is %dx%d: sizes must be multiples of %d", path, surface->w, surface->h,
                  SPRITE_ALIGN);
        SDL_DestroySurface(surface);
        return NULL;
    }
    if (is_tile_sprite(id) && (surface->w != TILE_SPRITE_SIZE || surface->h != TILE_SPRITE_SIZE)) {
        LOG_ERROR("Tile texture %s is %dx%d instead of %dx%d", path, surface->w, surface->h,
                  TILE_SPRITE_SIZE, TILE_SPRITE_SIZE);
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
    SDL_Texture *texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, pixels->width, pixels->height);
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
    if (plant_sprites_before((int)SDL_arraysize(PLANT_SPRITES)) != PLANT_SPRITE_COUNT) {
        LOG_ERROR("PLANT_SPRITE_COUNT does not match the plant sprite table");
        SDL_free(sprites);
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

void sprites_set_additive(Sprites *sprites, bool additive)
{
    sprites_flush(sprites);
    SDL_SetTextureBlendMode(sprites->levels[sprites->level],
                            additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
}

void sprites_size(const Sprites *sprites, SpriteId id, float *width, float *height)
{
    float scale = sprites->pixels_per_tile / (float)SPRITE_PIXELS_PER_TILE;
    bool valid = id >= 0 && id < SPRITE_COUNT;
    *width = valid ? (float)sprites->rects[id].w * scale : 0.0f;
    *height = valid ? (float)sprites->rects[id].h * scale : 0.0f;
}

/* Queues a quad given its four screen corners (clockwise from the top left
 * of the unmirrored sprite). A tile texture shows without its border. */
static void queue_quad(Sprites *sprites, SpriteId id, const SDL_FPoint corners[4], SDL_FColor color,
                       bool flip)
{
    if (sprites->count == SPRITE_BATCH_QUADS) {
        sprites_flush(sprites);
    }
    SDL_Rect rect = sprites->rects[id];
    if (is_tile_sprite(id)) {
        rect = (SDL_Rect){rect.x + TILE_SPRITE_PADDING, rect.y + TILE_SPRITE_PADDING,
                          rect.w - 2 * TILE_SPRITE_PADDING, rect.h - 2 * TILE_SPRITE_PADDING};
    }
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

void sprites_draw_scaled(Sprites *sprites, SpriteId id, float center_x, float center_y, float scale,
                         SDL_FColor color)
{
    if (id < 0 || id >= SPRITE_COUNT) {
        return;
    }
    float width, height;
    sprites_size(sprites, id, &width, &height);
    float half_w = width * scale * 0.5f, half_h = height * scale * 0.5f;
    SDL_FPoint corners[4] = {{center_x - half_w, center_y - half_h},
                             {center_x + half_w, center_y - half_h},
                             {center_x + half_w, center_y + half_h},
                             {center_x - half_w, center_y + half_h}};
    queue_quad(sprites, id, corners, color, false);
}

void sprites_draw_tile(Sprites *sprites, SpriteId id, const SDL_FRect *rect, SDL_FColor color)
{
    if (!is_tile_sprite(id)) {
        return;
    }
    SDL_FPoint corners[4] = {{rect->x, rect->y},
                             {rect->x + rect->w, rect->y},
                             {rect->x + rect->w, rect->y + rect->h},
                             {rect->x, rect->y + rect->h}};
    queue_quad(sprites, id, corners, color, false);
}
