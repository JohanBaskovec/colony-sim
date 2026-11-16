/*
 * Trees in the volume. A sapling is a plant like the others, walked through.
 * From TREE_VOLUME_MIN_STAGE on, a tree takes room like a building: its
 * trunk is solid wood (ROCK_WOOD) from its foot up, 1 x 1 tile (2 x 2 for
 * old trees, 3 x 3 for ancient ones, both rare and only from world
 * generation), and its foliage is a floor of branches (TERRAIN_FOLIAGE)
 * around the trunk on its upper levels, the top one covering the trunk too.
 * One walks on the foliage, climbing up to it along the trunk
 * (map_can_climb_tree()), and takes the tree's branches and twigs there.
 *
 * The plant stays on the foot's tile (the north-west one of a wider trunk):
 * growth, stocks and the trunk's size live there, and the shape follows
 * from them (tree_shape()). The trunk and foliage are written into the map
 * as the tree grows (tree_build()); foliage only fills open space (not
 * over a slope or a ladder, whose way up it would close), and a tree grows
 * no further while its trunk has no room (tree_room_to_grow()).
 *
 * A felled tree falls full length to one side, where the woodcutter aims it:
 * away from buildings, else where it does the least harm. Its wood lies
 * along the trunk, its branches and twigs where the crown came down, and
 * what lay on its foliage falls to the ground.
 */
#ifndef COLONY_CORE_TREE_H
#define COLONY_CORE_TREE_H

#include "core/item.h"
#include "core/map.h"
#include "core/structure.h"

#include <stdbool.h>

/* Growth stage (plant_growth_stage()) from which a tree takes room. */
#define TREE_VOLUME_MIN_STAGE 2
/* Widest trunk, in tiles. */
#define TREE_MAX_SIZE 3
/* Farthest the foliage reaches out from the trunk, in tiles. */
#define TREE_MAX_REACH 3
/* Levels the tallest tree takes, its foot's included (one level is 2 m). */
#define TREE_MAX_LEVELS 7
/* Tiles a trunk covers on the ground once fallen, per level of its height. */
#define TREE_FALL_TILES_PER_LEVEL 2
/* Work to fell a tree: a fixed part plus a part proportional to its growth
 * and its trunk's area. */
#define TREE_FELL_BASE_WORK 400.0f
#define TREE_FELL_GROWTH_WORK 1600.0f
/* Hit points a structure under a falling tree loses, in percent of its
 * maximum. It keeps 1 at least: wear finishes it off (and its refresh site
 * goes with it, see constructions_wear_day()). */
#define TREE_FALL_DAMAGE_PERCENT 50

/* The room a tree takes, counted from its foot (the north-west tile of its
 * trunk, on the level of the ground it grows on). */
typedef struct TreeShape {
    int size;   /* Width and depth of the trunk in tiles, 1..TREE_MAX_SIZE. */
    int trunk;  /* Levels of trunk, the foot's included. */
    /* Levels of foliage: from `foliage_from` (1 or more) up to `trunk`, the
     * level over the top of the trunk. */
    int foliage_from;
    int reach;  /* Tiles the foliage reaches out from the trunk. */
} TreeShape;

/* Levels a tree of this shape takes, its foot's included. */
static inline int tree_shape_levels(const TreeShape *shape)
{
    return shape->trunk + 1;
}

/* Trunk width of a plant (1 for a tree of the default size; 0 for other
 * plants). */
int tree_size(const Plant *plant);

/* Work needed to fell a tree (0 for other plants). */
float tree_fell_work(const Plant *plant);

/* Wood a felled tree gives (its cut item, by its growth and its trunk's
 * area; 0 for other plants). */
int tree_wood_yield(const Plant *plant);

/* The shape of a tree that takes room (a tree from TREE_VOLUME_MIN_STAGE on);
 * false for saplings and other plants. */
bool tree_shape(const Plant *plant, TreeShape *out);

/* Whether the tile at (dx, dy, dz) from the foot holds trunk, or foliage. */
bool tree_trunk_holds(const TreeShape *shape, int dx, int dy, int dz);
bool tree_foliage_holds(const TreeShape *shape, int dx, int dy, int dz);

/* A tree found in the map: its foot and its shape. */
typedef struct TreeRef {
    int x, y, z;
    TreeShape shape;
} TreeRef;

/* The tree whose trunk or foliage is on a tile (a tile of foliage two trees
 * share: the first found); false when there is none. */
bool tree_at(const Map *map, int x, int y, int z, TreeRef *out);

/* The plant of a tree found by tree_at(). */
const Plant *tree_plant(const Map *map, const TreeRef *tree);

/* Whether the trunk of a tree of `shape` with its foot on (x, y, z) has
 * room: in the map, the foot's tiles without rock other than its own trunk
 * nor any structure, the tiles above it open space, its own trunk or its own
 * foliage (that the trunk grows into). Items and settlers in the way are the
 * caller's matter. */
bool tree_room_to_grow(const Map *map, int x, int y, int z, const TreeShape *shape);

/* Whether a tile becomes trunk when the tree on (x, y, z) takes `shape`
 * (its foot's tile included). */
bool tree_grows_into(int x, int y, int z, const TreeShape *shape, int tx, int ty, int tz);

/* Writes the trunk and foliage of the tree on (x, y, z) into the map
 * (map_set_rock(), map_set_terrain()): trunk on its tiles, foliage on those
 * of its tiles that are open space, not over a slope or a ladder. Nothing
 * for a sapling. Check tree_room_to_grow() first. */
void tree_build(Map *map, int x, int y, int z);

/* Takes the trunk and foliage of the tree on (x, y, z) out of the map (the
 * plant stays): foliage another tree shares stays, what lay on the rest
 * falls to the ground below it (settlers there fall by themselves). */
void tree_clear(Map *map, Items *items, int x, int y, int z);

/* Tiles a tree of `levels` levels covers once fallen. */
int tree_fall_length(int levels);

/* How a tree falls from (x, y, z) in a direction: a band as wide as its
 * trunk from the trunk's side, as long as tree_fall_length() but stopping
 * before rock and the edge of the map, on the foot's level; the structure
 * tiles in it. */
typedef struct TreeFall {
    Direction direction;
    int length;  /* Tiles reached, 0 .. tree_fall_length(). */
    int harmed;  /* Structure tiles among them. */
} TreeFall;
TreeFall tree_fall_toward(const Map *map, int x, int y, int z, Direction direction);

/* Where a woodcutter aims the tree on (x, y, z): the direction with the
 * fewest structures in the way, then the one where the trunk lies longest,
 * then the first in the order south, east, north, west. */
TreeFall tree_fall_aim(const Map *map, int x, int y, int z);

/*
 * Fells the tree on (x, y, z) the way tree_fall_aim() says: the plant goes
 * with its trunk and foliage (tree_clear()), structures in the way lose
 * TREE_FALL_DAMAGE_PERCENT of their hit points, and what it gives is dropped
 * (items_drop()): `wood` shared out along the trunk, the branches and twigs
 * of its stocks where the crown came down. Where the trunk lies over a
 * drop, its share falls to the ground below. A sapling is cut where it
 * stands (length 0). Returns how it fell (length 0 and nothing done
 * without a tree).
 */
TreeFall tree_fell(Map *map, Items *items, int x, int y, int z, ItemStack wood);

#endif /* COLONY_CORE_TREE_H */
