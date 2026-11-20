#include "core/tree.h"

#include "core/game_clock.h"

#include <math.h>
#include <string.h>

/* The order in which directions are tried when nothing tells them apart. */
static const Direction AIM_ORDER[DIRECTION_COUNT] = {DIRECTION_SOUTH, DIRECTION_EAST,
                                                     DIRECTION_NORTH, DIRECTION_WEST};

/* The species (docs/TREES.md §2 and §3). The oak's crown is the broad,
 * dense one of the default tree; the beech's and the chestnut's are denser,
 * the ash's, the birch's and the wild pear's thinner (no dense foliage by
 * the trunk: not climbable); the birch and the wild pear are narrow, the
 * wild apple and the hazel low. */
static const TreeDef TREE_DEFS[PLANT_TREE_LAST - PLANT_TREE_FIRST + 1] = {
    [PLANT_OAK - PLANT_TREE_FIRST] = {.second_level_at = 0.49f,
                                      .third_level_at = 0.55f,
                                      .reach = 2,
                                      .raggedness = 0.9f,
                                      .crown_branches = 8,
                                      .crown_twigs = 10,
                                      .shed_twigs = 1.0f,
                                      .shed_branches = 1.5f,
                                      .can_be_old = true,
                                      .can_be_ancient = true},
    [PLANT_BEECH - PLANT_TREE_FIRST] = {.second_level_at = 0.49f,
                                        .third_level_at = 0.55f,
                                        .reach = 2,
                                        .density_shift = 1,
                                        .raggedness = 0.6f,
                                        .crown_branches = 5,
                                        .crown_twigs = 14,
                                        .shed_twigs = 1.2f,
                                        .shed_branches = 0.7f,
                                        .can_be_old = true,
                                        .can_be_ancient = true},
    [PLANT_CHESTNUT - PLANT_TREE_FIRST] = {.second_level_at = 0.49f,
                                           .third_level_at = 0.55f,
                                           .reach = 2,
                                           .density_shift = 1,
                                           .raggedness = 0.9f,
                                           .crown_branches = 6,
                                           .crown_twigs = 10,
                                           .shed_twigs = 1.0f,
                                           .shed_branches = 1.0f,
                                           .can_be_old = true,
                                           .can_be_ancient = true},
    [PLANT_ASH - PLANT_TREE_FIRST] = {.second_level_at = 0.45f,
                                      .third_level_at = 0.52f,
                                      .reach = 2,
                                      .density_shift = -1,
                                      .raggedness = 0.9f,
                                      .crown_branches = 5,
                                      .crown_twigs = 8,
                                      .shed_twigs = 0.8f,
                                      .shed_branches = 0.8f,
                                      .can_be_old = true},
    [PLANT_BIRCH - PLANT_TREE_FIRST] = {.second_level_at = 0.42f,
                                        .third_level_at = 0.5f,
                                        .reach = 1,
                                        .density_shift = -1,
                                        .raggedness = 0.5f,
                                        .crown_branches = 3,
                                        .crown_twigs = 14,
                                        .shed_twigs = 1.5f,
                                        .shed_branches = 1.0f},
    [PLANT_WILD_APPLE - PLANT_TREE_FIRST] = {.second_level_at = 0.5f,
                                             .reach = 2,
                                             .raggedness = 1.0f,
                                             .crown_branches = 4,
                                             .crown_twigs = 10,
                                             .shed_twigs = 0.7f,
                                             .shed_branches = 0.5f},
    [PLANT_WILD_PEAR - PLANT_TREE_FIRST] = {.second_level_at = 0.49f,
                                            .third_level_at = 0.56f,
                                            .reach = 1,
                                            .density_shift = -1,
                                            .raggedness = 0.5f,
                                            .crown_branches = 3,
                                            .crown_twigs = 8,
                                            .shed_twigs = 0.7f,
                                            .shed_branches = 0.5f},
    /* A hazel stays small: two levels of trunk at most. */
    [PLANT_HAZEL - PLANT_TREE_FIRST] = {.second_level_at = 0.52f,
                                        .reach = 2,
                                        .raggedness = 0.9f,
                                        .crown_branches = 6,
                                        .crown_twigs = 10,
                                        .shed_twigs = 1.0f,
                                        .shed_branches = 1.0f},
};
_Static_assert(PLANT_TREE_LAST - PLANT_TREE_FIRST + 1 == 8, "a TreeDef per tree species");

/* Shapes of the old and ancient trees, which only world generation makes: a
 * bare trunk under their crown (the reach is the species'). A tree of the
 * default size grows: its shape follows from its growth (tree_shape()). */
static const TreeShape OLD_SHAPE = {
    .size = 2, .trunk = 5, .foliage_from = 3, .growth = TREE_FULL_GROWTH};
static const TreeShape ANCIENT_SHAPE = {
    .size = 3, .trunk = 6, .foliage_from = 3, .growth = TREE_FULL_GROWTH};
_Static_assert(6 + 1 == TREE_MAX_LEVELS, "the ancient tree is the tallest");

const TreeDef *tree_def(PlantType type)
{
    return plant_is_tree(type) ? &TREE_DEFS[type - PLANT_TREE_FIRST] : NULL;
}

int tree_size(const Plant *plant)
{
    const TreeDef *def = tree_def((PlantType)plant->type);
    if (def == NULL) {
        return 0;
    }
    int size = plant->size < TREE_MAX_SIZE ? plant->size : TREE_MAX_SIZE;
    if (size >= 3 && def->can_be_ancient) {
        return 3;
    }
    return size >= 2 && def->can_be_old ? 2 : 1;
}

float tree_fell_work(const Plant *plant)
{
    int area = tree_size(plant) * tree_size(plant);
    return area > 0 ? TREE_FELL_BASE_WORK + TREE_FELL_GROWTH_WORK * plant->growth * (float)area
                    : 0.0f;
}

int tree_wood_yield(const Plant *plant)
{
    int area = tree_size(plant) * tree_size(plant);
    const PlantDef *def = plant_def((PlantType)plant->type);
    if (area == 0 || def == NULL) {
        return 0;
    }
    return (int)((float)(def->cut_yield * area) * plant->growth + 0.5f);
}

float tree_shed_chance(const Plant *plant, ItemType item)
{
    TreeShape shape;
    if (!tree_shape(plant, 0, 0, 0, &shape)) {
        return 0.0f;
    }
    /* Old and ancient trees are grown; a younger one sheds twigs only. */
    bool grown = shape.size > 1 || plant_growth_stage(plant) >= PLANT_GROWTH_STAGES - 1;
    float area = (float)(shape.size * shape.size);
    const TreeDef *def = tree_def((PlantType)plant->type);
    if (item == ITEM_TWIGS) {
        return TREE_SHED_TWIG_CHANCE * def->shed_twigs * area * (grown ? 1.0f : 0.5f);
    }
    return item == ITEM_BRANCHES && grown ? TREE_SHED_BRANCH_CHANCE * def->shed_branches * area
                                          : 0.0f;
}

float tree_fall_chance(const Plant *plant, int day_of_year, ItemType *item)
{
    const PlantDef *def = plant_def((PlantType)plant->type);
    if (!plant_is_tree(plant->type) || def->fall_item == ITEM_NONE ||
        !plant_days_contain(def->fall_days, day_of_year)) {
        return 0.0f;
    }
    int days = 0;
    for (int day = 0; day < DAYS_PER_YEAR; day++) {
        days += plant_days_contain(def->fall_days, day);
    }
    *item = def->fall_item;
    return (float)def->fall_yield * plant->growth / (float)(days * HOURS_PER_DAY);
}

int tree_crown_yield(const Plant *plant, ItemType item)
{
    const TreeDef *def = tree_def((PlantType)plant->type);
    if (def == NULL) {
        return 0;
    }
    int area = tree_size(plant) * tree_size(plant);
    int full = item == ITEM_BRANCHES ? def->crown_branches
               : item == ITEM_TWIGS  ? def->crown_twigs
                                     : 0;
    return (int)((float)(full * area) * plant->growth + 0.5f);
}

/* Scrambles bits (the finalizer of MurmurHash3). */
static uint32_t mix_bits(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

bool tree_shape(const Plant *plant, int x, int y, int z, TreeShape *out)
{
    if (!plant_is_tree(plant->type) || plant_is_dormant(plant)) {
        return false;
    }
    const TreeDef *def = tree_def((PlantType)plant->type);
    int size = tree_size(plant);
    if (size > 1) {
        *out = size == 2 ? OLD_SHAPE : ANCIENT_SHAPE;
        out->reach = def->reach + (size == 3 ? 1 : 0);
    } else {
        if (plant_growth_stage(plant) < TREE_VOLUME_MIN_STAGE) {
            return false;
        }
        float growth = plant->growth;
        bool third = def->third_level_at > 0.0f && growth >= def->third_level_at;
        bool second = def->second_level_at > 0.0f && growth >= def->second_level_at;
        *out = (TreeShape){.size = 1,
                           .trunk = third ? 3 : (second ? 2 : 1),
                           .foliage_from = 1,
                           .reach = def->reach,
                           .growth = growth};
    }
    out->density_shift = (int8_t)def->density_shift;
    out->raggedness = def->raggedness;
    out->seed = mix_bits((uint32_t)x * 0x9E3779B1u ^
                         mix_bits((uint32_t)y * 0x85EBCA77u ^ mix_bits((uint32_t)z + 0x27D4EB2Fu)));
    return true;
}

/* How far a coordinate is outside the trunk's span 0..size-1. */
static int outside(int d, int size)
{
    return d < 0 ? -d : (d >= size ? d - size + 1 : 0);
}

bool tree_trunk_holds(const TreeShape *shape, int dx, int dy, int dz)
{
    return dz >= 0 && dz < shape->trunk && dx >= 0 && dy >= 0 && dx < shape->size &&
           dy < shape->size;
}

/* A number in 0..1 that is the same every time for a tile of the tree. */
static float foliage_noise(const TreeShape *shape, int dx, int dy, int dz, uint32_t salt)
{
    uint32_t h = shape->seed + salt * 0x9E3779B9u;
    h = mix_bits(h ^ (uint32_t)(dx + 16) * 0x85EBCA77u);
    h = mix_bits(h ^ (uint32_t)(dy + 16) * 0xC2B2AE3Du);
    h = mix_bits(h ^ (uint32_t)(dz + 16) * 0x27D4EB2Fu);
    return (float)(h >> 8) / 16777216.0f;
}

/* The foliage of a tile that comes in when the tree has grown to `comes`, as
 * dense as `final` in the end: none before, then sparse, thickening. */
static TreeDensity grown_foliage(const TreeShape *shape, float comes, TreeDensity final)
{
    if (shape->growth < comes) {
        return TREE_DENSITY_NONE;
    }
    int density = TREE_DENSITY_SPARSE + (int)((shape->growth - comes) / TREE_DENSIFY_GROWTH);
    return density < (int) final ? (TreeDensity)density : final;
}

/* A density moved by the tree's species (TreeDef.density_shift), sparse to
 * dense. */
static TreeDensity shifted(const TreeShape *shape, TreeDensity density)
{
    int shifted = (int)density + shape->density_shift;
    return (TreeDensity)(shifted < TREE_DENSITY_SPARSE  ? TREE_DENSITY_SPARSE
                         : shifted > TREE_DENSITY_DENSE ? TREE_DENSITY_DENSE
                                                        : shifted);
}

TreeDensity tree_foliage_density(const TreeShape *shape, int dx, int dy, int dz)
{
    if (dz < shape->foliage_from || dz > shape->trunk) {
        return TREE_DENSITY_NONE;
    }
    int ex = outside(dx, shape->size), ey = outside(dy, shape->size);
    if (ex == 0 && ey == 0) {
        /* Over the trunk: the first foliage of a tree. */
        return dz == shape->trunk
                   ? grown_foliage(shape, TREE_CROWN_FROM, shifted(shape, TREE_DENSITY_DENSE))
                   : TREE_DENSITY_NONE;
    }
    /* A rounded crown... */
    int reach = shape->reach;
    int limit = reach * reach + reach;
    int distance = ex * ex + ey * ey;
    if (ex > reach || ey > reach || distance > limit) {
        return TREE_DENSITY_NONE;
    }
    /* ...with ragged edges: the further out, the likelier a tile is missing
     * (never one next to the trunk, which keeps the tree climbable). The
     * noise depends on the tile, not on the tree's stage: a tree that grows
     * only adds foliage. */
    float outwards = (float)distance / (float)limit;
    if (outwards > 0.5f &&
        foliage_noise(shape, dx, dy, dz, 1) < (outwards - 0.5f) * shape->raggedness) {
        return TREE_DENSITY_NONE;
    }
    /* Thickest next to the trunk, thinning out ring by ring; the outer rings
     * are patchy. */
    int ring = ex > ey ? ex : ey;
    TreeDensity density =
        shifted(shape, ring <= 1 ? TREE_DENSITY_DENSE
                                 : (ring == 2 ? TREE_DENSITY_MEDIUM : TREE_DENSITY_SPARSE));
    if (ring >= 2 && density > TREE_DENSITY_SPARSE &&
        foliage_noise(shape, dx, dy, dz, 2) < TREE_FOLIAGE_PATCHINESS) {
        density--;
    }
    /* The crown grows tile by tile, nearest first (with some disorder): the
     * tile comes in at a growth that rises with its distance. It starts
     * sparse and thickens with the tree's age. */
    float far =
        sqrtf((float)distance) - 1.0f + TREE_CROWN_DISORDER * foliage_noise(shape, dx, dy, dz, 3);
    float comes = TREE_CROWN_FROM +
                  TREE_CROWN_SPREAD * far / (sqrtf((float)limit) - 1.0f + TREE_CROWN_DISORDER);
    return grown_foliage(shape, comes, density);
}

bool tree_foliage_holds(const TreeShape *shape, int dx, int dy, int dz)
{
    return tree_foliage_density(shape, dx, dy, dz) != TREE_DENSITY_NONE;
}

TreeDensity tree_terrain_density(Terrain terrain)
{
    switch (terrain) {
    case TERRAIN_FOLIAGE: return TREE_DENSITY_DENSE;
    case TERRAIN_FOLIAGE_MEDIUM: return TREE_DENSITY_MEDIUM;
    case TERRAIN_FOLIAGE_SPARSE: return TREE_DENSITY_SPARSE;
    default: return TREE_DENSITY_NONE;
    }
}

Terrain tree_density_terrain(TreeDensity density)
{
    switch (density) {
    case TREE_DENSITY_DENSE: return TERRAIN_FOLIAGE;
    case TREE_DENSITY_MEDIUM: return TERRAIN_FOLIAGE_MEDIUM;
    default: return TERRAIN_FOLIAGE_SPARSE;
    }
}

/* The tree whose trunk is on (x, y, z), a ROCK_WOOD tile. */
static bool tree_of_trunk(const Map *map, int x, int y, int z, TreeRef *out)
{
    int bottom = z;
    while (bottom > 0 && map->rock[map_index(map, x, y, bottom - 1)] == ROCK_WOOD) {
        bottom--;
    }
    for (int dy = 0; dy < TREE_MAX_SIZE; dy++) {
        for (int dx = 0; dx < TREE_MAX_SIZE; dx++) {
            int fx = x - dx, fy = y - dy;
            TreeShape shape;
            if (!map_in_bounds(map, fx, fy, bottom) ||
                !tree_shape(&map->plants[map_index(map, fx, fy, bottom)], fx, fy, bottom, &shape) ||
                !tree_trunk_holds(&shape, dx, dy, z - bottom)) {
                continue;
            }
            *out = (TreeRef){fx, fy, bottom, shape};
            return true;
        }
    }
    return false;
}

/* The tree whose foliage is on (x, y, z), but the one with its foot on
 * `skip` (NULL: none). Its trunk is on the same level, or on the level below
 * (the foliage over the top of the trunk). */
static bool tree_of_foliage(const Map *map, int x, int y, int z, const TreeRef *skip, TreeRef *out)
{
    /* Nearest trunk tiles first, ring by ring (often in the first two):
     * called for every tile of foliage a job search looks at. */
    for (int ring = 0; ring <= TREE_MAX_REACH; ring++) {
        for (int below = 0; below <= 1 && z - below >= 0; below++) {
            int tz = z - below;
            for (int dy = -ring; dy <= ring; dy++) {
                /* The whole row on the ring's edges, its two ends between. */
                int step = (dy == -ring || dy == ring || ring == 0) ? 1 : 2 * ring;
                for (int dx = -ring; dx <= ring; dx += step) {
                    int tx = x + dx, ty = y + dy;
                    TreeRef tree;
                    if (!map_in_bounds(map, tx, ty, tz) ||
                        map->rock[map_index(map, tx, ty, tz)] != ROCK_WOOD ||
                        !tree_of_trunk(map, tx, ty, tz, &tree) ||
                        (skip != NULL && tree.x == skip->x && tree.y == skip->y &&
                         tree.z == skip->z) ||
                        !tree_foliage_holds(&tree.shape, x - tree.x, y - tree.y, z - tree.z)) {
                        continue;
                    }
                    *out = tree;
                    return true;
                }
            }
        }
    }
    return false;
}

bool tree_at(const Map *map, int x, int y, int z, TreeRef *out)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    if (map->rock[index] == ROCK_WOOD) {
        return tree_of_trunk(map, x, y, z, out);
    }
    return map->rock[index] == ROCK_NONE && terrain_is_foliage(map->terrain[index]) &&
           tree_of_foliage(map, x, y, z, NULL, out);
}

const Plant *tree_plant(const Map *map, const TreeRef *tree)
{
    return &map->plants[map_index(map, tree->x, tree->y, tree->z)];
}

bool tree_grows_into(int x, int y, int z, const TreeShape *shape, int tx, int ty, int tz)
{
    return tree_trunk_holds(shape, tx - x, ty - y, tz - z);
}

bool tree_room_to_grow(const Map *map, int x, int y, int z, const TreeShape *shape)
{
    for (int dz = 0; dz < shape->trunk; dz++) {
        for (int dy = 0; dy < shape->size; dy++) {
            for (int dx = 0; dx < shape->size; dx++) {
                int tx = x + dx, ty = y + dy, tz = z + dz;
                if (!map_in_bounds(map, tx, ty, tz)) {
                    return false;
                }
                size_t index = map_index(map, tx, ty, tz);
                RockType rock = (RockType)map->rock[index];
                if (rock == ROCK_WOOD) {
                    continue; /* Its own trunk (trunks never touch: see worldgen). */
                }
                if (rock != ROCK_NONE || map->structure[index] != STRUCTURE_NONE) {
                    return false;
                }
                if (dz == 0 || map_is_open_at(map, index)) {
                    continue;
                }
                /* Its own foliage (which the new shape no longer holds as
                 * foliage: the plant already has it), not ground, a roof nor
                 * the foliage of another tree. */
                TreeRef self = {x, y, z, *shape}, other;
                if (!terrain_is_foliage(map->terrain[index]) ||
                    tree_of_foliage(map, tx, ty, tz, &self, &other)) {
                    return false;
                }
            }
        }
    }
    return true;
}

void tree_build(Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    TreeShape shape;
    uint8_t species = map->plants[map_index(map, x, y, z)].type;
    if (!tree_shape(&map->plants[map_index(map, x, y, z)], x, y, z, &shape)) {
        return;
    }
    for (int dz = 0; dz < shape.trunk && z + dz < map->depth; dz++) {
        for (int dy = 0; dy < shape.size; dy++) {
            for (int dx = 0; dx < shape.size; dx++) {
                if (dz > 0) { /* Over its foot, nothing under the wood. */
                    map_set_terrain(map, x + dx, y + dy, z + dz, TERRAIN_OPEN);
                }
                map_set_rock(map, x + dx, y + dy, z + dz, ROCK_WOOD);
                map->tree_species[map_index(map, x + dx, y + dy, z + dz)] = species;
            }
        }
    }
    int span = shape.size + 2 * shape.reach;
    for (int dz = shape.foliage_from; dz <= shape.trunk && z + dz < map->depth; dz++) {
        for (int dy = -shape.reach; dy < span - shape.reach; dy++) {
            for (int dx = -shape.reach; dx < span - shape.reach; dx++) {
                int tx = x + dx, ty = y + dy, tz = z + dz;
                TreeDensity density = tree_foliage_density(&shape, dx, dy, dz);
                if (density == TREE_DENSITY_NONE || !map_in_bounds(map, tx, ty, tz)) {
                    continue;
                }
                size_t index = map_index(map, tx, ty, tz);
                if (map_is_open_at(map, index)) {
                    if (map_climb_cost(map, tx, ty, tz - 1) == 0.0f) {
                        map_set_terrain(map, tx, ty, tz, tree_density_terrain(density));
                        map->tree_species[index] = species;
                    }
                } else if (map->rock[index] == ROCK_NONE &&
                           terrain_is_foliage(map->terrain[index]) &&
                           tree_terrain_density((Terrain)map->terrain[index]) < density) {
                    /* Foliage that thickens (never the other way): the
                     * thicker tree's, where two crowns meet. */
                    map_set_terrain(map, tx, ty, tz, tree_density_terrain(density));
                    map->tree_species[index] = species;
                }
            }
        }
    }
}

/* Whether things fall through a tile: open space, or branches too thin to
 * hold anything (medium and sparse foliage). */
static bool lets_fall(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && map_sees_through_at(map, map_index(map, x, y, z));
}

/* The level where something dropped over (x, y, z) comes to rest: z on a
 * floor, the first floor below over open space or thin foliage (-1 if none). */
static int landing_level(const Map *map, int x, int y, int z)
{
    while (z >= 0 && lets_fall(map, x, y, z)) {
        z--;
    }
    return z;
}

/* The level where what a tree sheds over (x, y, z) comes to rest: twigs,
 * branches and nuts go through every density of foliage (and open space)
 * down to the first floor that is not a tree's: they never stay in a crown
 * (-1 if none). */
static int litter_landing_level(const Map *map, int x, int y, int z)
{
    while (z >= 0 && (lets_fall(map, x, y, z) ||
                      (map->rock[map_index(map, x, y, z)] == ROCK_NONE &&
                       terrain_is_foliage(map->terrain[map_index(map, x, y, z)])))) {
        z--;
    }
    return z;
}

/* What lies on (x, y, z), its floor gone, falls to the ground below. */
static void drop_items_from(Map *map, Items *items, int x, int y, int z)
{
    if (items == NULL) {
        return;
    }
    if (items_get(items, x, y, z).count == 0) {
        return;
    }
    int level = landing_level(map, x, y, z);
    if (level >= 0) {
        items_move_all(items, map, x, y, z, x, y, level, false);
    } else {
        items_clear(items, x, y, z);
    }
}

/* Whether branches and twigs can lie on a tile: a floor, not rock, water,
 * open space, foliage of any density nor a wall. */
static bool holds_litter(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    Terrain terrain = (Terrain)map->terrain[index];
    return map->rock[index] == ROCK_NONE && terrain != TERRAIN_OPEN &&
           !terrain_is_foliage(terrain) && terrain != TERRAIN_DEEP_WATER &&
           terrain != TERRAIN_SHALLOW_WATER &&
           !structure_blocks((StructureType)map->structure[index]);
}

/* The branches, twigs and nuts on (x, y, z), its floor gone, fall to the
 * ground below (what does not fit there lies as items), while they lie in the
 * map's layers (a running game keeps them as items: drop_items_from()). */
static void drop_litter_from(Map *map, Items *items, int x, int y, int z)
{
    static const ItemType KINDS[] = {ITEM_BRANCHES, ITEM_TWIGS, ITEM_HAZELNUTS};
    if (map->litter != NULL) {
        return;
    }
    for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); i++) {
        int count = map_take_litter(map, x, y, z, KINDS[i]);
        int level = count > 0 ? litter_landing_level(map, x, y, z) : -1;
        int left = 0;
        for (int n = 0; n < count; n++) {
            left += level < 0 || !holds_litter(map, x, y, level) ||
                    !map_add_litter(map, x, y, level, KINDS[i]);
        }
        if (left > 0 && level >= 0 && items != NULL) {
            ItemStack stack = item_stack_pristine(KINDS[i], left);
            stack.quality = ITEM_QUALITY_STANDARD;
            items_drop(items, map, x, y, level, stack);
        }
    }
}

void tree_clear(Map *map, Items *items, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    TreeShape shape;
    if (!tree_shape(&map->plants[map_index(map, x, y, z)], x, y, z, &shape)) {
        return;
    }
    TreeRef self = {x, y, z, shape};
    /* Which foliage another tree shares is found while the trunks stand. */
    int span = shape.size + 2 * shape.reach;
    for (int dz = shape.foliage_from; dz <= shape.trunk && z + dz < map->depth; dz++) {
        for (int dy = -shape.reach; dy < span - shape.reach; dy++) {
            for (int dx = -shape.reach; dx < span - shape.reach; dx++) {
                int tx = x + dx, ty = y + dy, tz = z + dz;
                TreeRef other;
                if (!tree_foliage_holds(&shape, dx, dy, dz) || !map_in_bounds(map, tx, ty, tz) ||
                    map->rock[map_index(map, tx, ty, tz)] != ROCK_NONE ||
                    !terrain_is_foliage(map->terrain[map_index(map, tx, ty, tz)])) {
                    continue;
                }
                if (tree_of_foliage(map, tx, ty, tz, &self, &other)) {
                    map->tree_species[map_index(map, tx, ty, tz)] =
                        tree_plant(map, &other)->type; /* The other tree's now. */
                    continue;
                }
                map_set_terrain(map, tx, ty, tz, TERRAIN_OPEN);
                map->tree_species[map_index(map, tx, ty, tz)] = PLANT_NONE;
            }
        }
    }
    for (int dz = 0; dz < shape.trunk && z + dz < map->depth; dz++) {
        for (int dy = 0; dy < shape.size; dy++) {
            for (int dx = 0; dx < shape.size; dx++) {
                map_set_rock(map, x + dx, y + dy, z + dz, ROCK_NONE);
                map->tree_species[map_index(map, x + dx, y + dy, z + dz)] = PLANT_NONE;
            }
        }
    }
    /* Then what lay up there falls, from the lowest level up (so that it
     * does not stop on foliage that goes too). */
    for (int dz = shape.foliage_from; dz <= shape.trunk && z + dz < map->depth; dz++) {
        for (int dy = -shape.reach; dy < span - shape.reach; dy++) {
            for (int dx = -shape.reach; dx < span - shape.reach; dx++) {
                int tx = x + dx, ty = y + dy, tz = z + dz;
                if (tree_foliage_holds(&shape, dx, dy, dz) && map_is_open(map, tx, ty, tz)) {
                    drop_items_from(map, items, tx, ty, tz);
                    drop_litter_from(map, items, tx, ty, tz);
                }
            }
        }
    }
}

bool tree_shed(Map *map, int x, int y, int z, ItemType item, uint32_t pick)
{
    TreeShape shape;
    if (!map_in_bounds(map, x, y, z) ||
        !tree_shape(&map->plants[map_index(map, x, y, z)], x, y, z, &shape)) {
        return false;
    }
    int span = shape.size + 2 * shape.reach;
    int tiles = 0;
    for (int pass = 0; pass < 2; pass++) { /* Count the foliage, then find the chosen tile. */
        int chosen = tiles > 0 ? (int)(pick % (uint32_t)tiles) : 0;
        int seen = 0;
        for (int dz = shape.foliage_from; dz <= shape.trunk && z + dz < map->depth; dz++) {
            for (int dy = -shape.reach; dy < span - shape.reach; dy++) {
                for (int dx = -shape.reach; dx < span - shape.reach; dx++) {
                    if (!tree_foliage_holds(&shape, dx, dy, dz)) {
                        continue;
                    }
                    if (pass == 0) {
                        tiles++;
                        continue;
                    }
                    if (seen++ != chosen) {
                        continue;
                    }
                    int tx = x + dx, ty = y + dy, tz = z + dz;
                    /* Through the crown, down to the first floor below it. */
                    int level = litter_landing_level(map, tx, ty, tz);
                    return level >= 0 && holds_litter(map, tx, ty, level) &&
                           map_add_litter(map, tx, ty, level, item);
                }
            }
        }
        if (tiles == 0) {
            return false;
        }
    }
    return false;
}

void tree_scatter_litter(Map *map, int x, int y, int z, Rng *rng)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    static const ItemType KINDS[] = {ITEM_BRANCHES, ITEM_TWIGS};
    for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); i++) {
        float expected =
            tree_shed_chance(plant, KINDS[i]) * (float)(HOURS_PER_DAY * TREE_SCATTER_DAYS);
        int count = (int)expected;
        count += rng_next_float(rng) < expected - (float)count ? 1 : 0;
        for (int n = 0; n < count; n++) {
            tree_shed(map, x, y, z, KINDS[i], rng_next_u32(rng));
        }
    }
}

int tree_fall_length(int levels)
{
    return levels > 0 ? levels * TREE_FALL_TILES_PER_LEVEL : 0;
}

void tree_fall_band_tile(int x, int y, int size, Direction direction, int along, int across,
                         int *tx, int *ty)
{
    int dx = direction_dx(direction), dy = direction_dy(direction);
    if (dx != 0) {
        *tx = dx > 0 ? x + size - 1 + along : x - along;
        *ty = y + across;
    } else {
        *tx = x + across;
        *ty = dy > 0 ? y + size - 1 + along : y - along;
    }
    if (along == 0) { /* The foot itself. */
        *tx = x + (dx != 0 ? 0 : across);
        *ty = y + (dy != 0 ? 0 : across);
    }
}

TreeFall tree_fall_toward(const Map *map, int x, int y, int z, Direction direction)
{
    TreeFall fall = {direction, 0, 0};
    TreeShape shape;
    if (!map_in_bounds(map, x, y, z) ||
        !tree_shape(&map->plants[map_index(map, x, y, z)], x, y, z, &shape)) {
        return fall;
    }
    int length = tree_fall_length(tree_shape_levels(&shape));
    for (int i = 1; i <= length; i++) {
        int harmed = 0;
        bool blocked = false;
        for (int j = 0; j < shape.size && !blocked; j++) {
            int tx, ty;
            tree_fall_band_tile(x, y, shape.size, direction, i, j, &tx, &ty);
            blocked =
                !map_in_bounds(map, tx, ty, z) || map->rock[map_index(map, tx, ty, z)] != ROCK_NONE;
            harmed += !blocked && map_structure(map, tx, ty, z) != STRUCTURE_NONE;
        }
        if (blocked) {
            break; /* The trunk comes to rest against rock or the edge. */
        }
        fall.length = i;
        fall.harmed += harmed;
    }
    return fall;
}

TreeFall tree_fall_aim(const Map *map, int x, int y, int z)
{
    TreeFall best = tree_fall_toward(map, x, y, z, AIM_ORDER[0]);
    for (int i = 1; i < DIRECTION_COUNT; i++) {
        TreeFall fall = tree_fall_toward(map, x, y, z, AIM_ORDER[i]);
        bool longer = fall.harmed == best.harmed && fall.length > best.length;
        if (fall.harmed < best.harmed || longer) {
            best = fall;
        }
    }
    return best;
}

/* Drops `count` items like `like` (same type, quality and condition) on a
 * tile, or on the ground below it over a drop. */
static void drop_share(Map *map, Items *items, int x, int y, int z, ItemStack like, int count)
{
    if (items == NULL || count <= 0 || like.type == ITEM_NONE) {
        return;
    }
    int level = landing_level(map, x, y, z);
    if (level < 0) {
        return;
    }
    like.count = (uint16_t)count;
    items_drop(items, map, x, y, level, like);
}

/* Shares `stack` out over tiles first..last of the middle of the band (as
 * evenly as whole items allow, the first tiles getting the rest). */
static void drop_along(Map *map, Items *items, int x, int y, int z, int size, Direction direction,
                       int first, int last, ItemStack stack)
{
    int tiles = last - first + 1;
    if (tiles <= 0 || stack.count == 0) {
        return;
    }
    int each = stack.count / tiles, rest = stack.count % tiles;
    for (int i = first; i <= last; i++) {
        int count = each + (i - first < rest ? 1 : 0);
        int tx, ty;
        tree_fall_band_tile(x, y, size, direction, i, size / 2, &tx, &ty);
        drop_share(map, items, tx, ty, z, stack, count);
    }
}

TreeFall tree_fell(Map *map, Items *items, int x, int y, int z, ItemStack wood)
{
    TreeFall fall = {DIRECTION_SOUTH, 0, 0};
    if (!map_in_bounds(map, x, y, z)) {
        return fall;
    }
    Plant *plant = &map->plants[map_index(map, x, y, z)];
    if (!plant_is_tree(plant->type)) {
        return fall;
    }
    int size = tree_size(plant);
    fall = tree_fall_aim(map, x, y, z);
    for (int i = 1; i <= fall.length; i++) {
        for (int j = 0; j < size; j++) {
            int tx, ty;
            tree_fall_band_tile(x, y, size, fall.direction, i, j, &tx, &ty);
            StructureType type = map_structure(map, tx, ty, z);
            if (type != STRUCTURE_NONE) {
                int max = structure_max_hit_points(type, map_structure_quality(map, tx, ty, z));
                int damage = max * TREE_FALL_DAMAGE_PERCENT / 100;
                map_set_structure_hit_points(map, tx, ty, z,
                                             map_structure_hit_points(map, tx, ty, z) - damage);
            }
        }
    }
    ItemStack branches = item_stack_pristine(ITEM_BRANCHES, tree_crown_yield(plant, ITEM_BRANCHES));
    /* Green twigs, wet with sap: they dry like those picked from wet ground. */
    ItemStack twigs = item_stack_pristine(ITEM_WET_TWIGS, tree_crown_yield(plant, ITEM_TWIGS));
    branches.quality = twigs.quality = ITEM_QUALITY_STANDARD;
    tree_clear(map, items, x, y, z);
    map_remove_plant(map, x, y, z);
    /* The trunk: its foot's tile and the first two thirds of the line; the
     * crown on the rest (on the foot when nothing lies beyond it). */
    int crown_from = fall.length - fall.length / 3 + 1;
    int trunk_to = crown_from - 1;
    drop_along(map, items, x, y, z, size, fall.direction, 0, trunk_to, wood);
    int crown_first = crown_from <= fall.length ? crown_from : 0;
    int crown_last = crown_from <= fall.length ? fall.length : 0;
    drop_along(map, items, x, y, z, size, fall.direction, crown_first, crown_last, branches);
    drop_along(map, items, x, y, z, size, fall.direction, crown_first, crown_last, twigs);
    map->tree_fall_revision++;
    map->tree_fall_log[map->tree_fall_revision % MAP_TREE_FALL_LOG_SIZE] = (MapTreeFall){
        (int16_t)x,           (int16_t)y,       (int16_t)z, (uint8_t)size, (uint8_t)fall.direction,
        (uint8_t)fall.length, (uint8_t)trunk_to};
    return fall;
}
