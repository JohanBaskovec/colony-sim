#include "core/tree.h"

#include <string.h>

/* The order in which directions are tried when nothing tells them apart. */
static const Direction AIM_ORDER[DIRECTION_COUNT] = {DIRECTION_SOUTH, DIRECTION_EAST,
                                                     DIRECTION_NORTH, DIRECTION_WEST};

/* Shapes of the trees that take room. A tree of the default size grows a
 * level of trunk (and of foliage) per stage, so that growing only adds to
 * it; old and ancient trees are grown, with a bare trunk under their crown. */
static const TreeShape SHAPES_BY_STAGE[PLANT_GROWTH_STAGES] = {
    [2] = {1, 1, 1, 1},
    [3] = {1, 2, 1, 1},
    [4] = {1, 3, 1, 1},
};
static const TreeShape OLD_SHAPE = {2, 5, 3, 2};
static const TreeShape ANCIENT_SHAPE = {3, 6, 3, 3};
_Static_assert(6 + 1 == TREE_MAX_LEVELS, "the ancient tree is the tallest");

int tree_size(const Plant *plant)
{
    if (plant->type != PLANT_TREE) {
        return 0;
    }
    return plant->size > 1 ? (plant->size < TREE_MAX_SIZE ? plant->size : TREE_MAX_SIZE) : 1;
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

bool tree_shape(const Plant *plant, TreeShape *out)
{
    if (plant->type != PLANT_TREE || plant_is_dormant(plant)) {
        return false;
    }
    int size = tree_size(plant);
    if (size > 1) {
        *out = size == 2 ? OLD_SHAPE : ANCIENT_SHAPE;
        return true;
    }
    int stage = plant_growth_stage(plant);
    if (stage < TREE_VOLUME_MIN_STAGE) {
        return false;
    }
    *out = SHAPES_BY_STAGE[stage];
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

bool tree_foliage_holds(const TreeShape *shape, int dx, int dy, int dz)
{
    if (dz < shape->foliage_from || dz > shape->trunk) {
        return false;
    }
    int ex = outside(dx, shape->size), ey = outside(dy, shape->size);
    if (ex == 0 && ey == 0) {
        return dz == shape->trunk; /* Over the top of the trunk. */
    }
    /* A rounded crown: the corners of the wider ones are left out. */
    int reach = shape->reach;
    return ex <= reach && ey <= reach && ex * ex + ey * ey <= reach * reach + reach;
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
                !tree_shape(&map->plants[map_index(map, fx, fy, bottom)], &shape) ||
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
static bool tree_of_foliage(const Map *map, int x, int y, int z, const TreeRef *skip,
                            TreeRef *out)
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
    return map->rock[index] == ROCK_NONE && map->terrain[index] == TERRAIN_FOLIAGE &&
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
                TreeRef owner;
                if (map->terrain[index] != TERRAIN_FOLIAGE || !tree_at(map, tx, ty, tz, &owner) ||
                    owner.x != x || owner.y != y || owner.z != z) {
                    return false; /* Ground, a roof, or another tree's foliage. */
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
    if (!tree_shape(&map->plants[map_index(map, x, y, z)], &shape)) {
        return;
    }
    for (int dz = 0; dz < shape.trunk && z + dz < map->depth; dz++) {
        for (int dy = 0; dy < shape.size; dy++) {
            for (int dx = 0; dx < shape.size; dx++) {
                if (dz > 0) { /* Over its foot, nothing under the wood. */
                    map_set_terrain(map, x + dx, y + dy, z + dz, TERRAIN_OPEN);
                }
                map_set_rock(map, x + dx, y + dy, z + dz, ROCK_WOOD);
            }
        }
    }
    int span = shape.size + 2 * shape.reach;
    for (int dz = shape.foliage_from; dz <= shape.trunk && z + dz < map->depth; dz++) {
        for (int dy = -shape.reach; dy < span - shape.reach; dy++) {
            for (int dx = -shape.reach; dx < span - shape.reach; dx++) {
                int tx = x + dx, ty = y + dy, tz = z + dz;
                if (tree_foliage_holds(&shape, dx, dy, dz) && map_is_open(map, tx, ty, tz) &&
                    map_climb_cost(map, tx, ty, tz - 1) == 0.0f) {
                    map_set_terrain(map, tx, ty, tz, TERRAIN_FOLIAGE);
                }
            }
        }
    }
}

/* The level where something dropped over (x, y, z) comes to rest: z on a
 * floor, the first floor below over open space (-1 if none). */
static int landing_level(const Map *map, int x, int y, int z)
{
    while (z >= 0 && map_is_open(map, x, y, z)) {
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
    ItemStack stack = items_get(items, x, y, z);
    if (stack.count == 0) {
        return;
    }
    items_remove(items, x, y, z, stack.count);
    int level = landing_level(map, x, y, z);
    if (level >= 0) {
        items_drop(items, map, x, y, level, stack);
    }
}

void tree_clear(Map *map, Items *items, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    TreeShape shape;
    if (!tree_shape(&map->plants[map_index(map, x, y, z)], &shape)) {
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
                    map->terrain[map_index(map, tx, ty, tz)] != TERRAIN_FOLIAGE ||
                    tree_of_foliage(map, tx, ty, tz, &self, &other)) {
                    continue;
                }
                map_set_terrain(map, tx, ty, tz, TERRAIN_OPEN);
            }
        }
    }
    for (int dz = 0; dz < shape.trunk && z + dz < map->depth; dz++) {
        for (int dy = 0; dy < shape.size; dy++) {
            for (int dx = 0; dx < shape.size; dx++) {
                map_set_rock(map, x + dx, y + dy, z + dz, ROCK_NONE);
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
                }
            }
        }
    }
}

int tree_fall_length(int levels)
{
    return levels > 0 ? levels * TREE_FALL_TILES_PER_LEVEL : 0;
}

/* Tile `across` (0..size-1) of the band a trunk of `size` covers `along`
 * tiles from its side (0: the foot's own tiles) in a direction. */
static void band_tile(int x, int y, int size, Direction direction, int along, int across,
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
        !tree_shape(&map->plants[map_index(map, x, y, z)], &shape)) {
        return fall;
    }
    int length = tree_fall_length(tree_shape_levels(&shape));
    for (int i = 1; i <= length; i++) {
        int harmed = 0;
        bool blocked = false;
        for (int j = 0; j < shape.size && !blocked; j++) {
            int tx, ty;
            band_tile(x, y, shape.size, direction, i, j, &tx, &ty);
            blocked = !map_in_bounds(map, tx, ty, z) ||
                      map->rock[map_index(map, tx, ty, z)] != ROCK_NONE;
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

/* Drops `count` items like `like` (same type, grade and condition) on a
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
        band_tile(x, y, size, direction, i, size / 2, &tx, &ty);
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
    if (plant->type != PLANT_TREE) {
        return fall;
    }
    int size = tree_size(plant);
    fall = tree_fall_aim(map, x, y, z);
    for (int i = 1; i <= fall.length; i++) {
        for (int j = 0; j < size; j++) {
            int tx, ty;
            band_tile(x, y, size, fall.direction, i, j, &tx, &ty);
            StructureType type = map_structure(map, tx, ty, z);
            if (type != STRUCTURE_NONE) {
                int max = structure_max_hit_points(type, map_structure_quality(map, tx, ty, z));
                int damage = max * TREE_FALL_DAMAGE_PERCENT / 100;
                map_set_structure_hit_points(map, tx, ty, z,
                                             map_structure_hit_points(map, tx, ty, z) - damage);
            }
        }
    }
    ItemStack branches =
        item_stack_pristine(plant_stock_item(PLANT_TREE, PLANT_STOCK_BRANCHES),
                            plant_stock_available(plant, PLANT_STOCK_BRANCHES));
    ItemStack twigs = item_stack_pristine(plant_stock_item(PLANT_TREE, PLANT_STOCK_TWIGS),
                                          plant_stock_available(plant, PLANT_STOCK_TWIGS));
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
    return fall;
}
