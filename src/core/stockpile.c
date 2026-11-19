#include "core/stockpile.h"

#include "core/util.h"

#include <stdlib.h>
#include <string.h>

/* Groups of a tile counted as stored (a stockpile tile holds one, more only
 * when items were dropped there). */
#define STOCKPILE_GROUPS_COUNTED 8

bool stockpiles_init(Stockpiles *stockpiles, int width, int height, int depth)
{
    memset(stockpiles, 0, sizeof(*stockpiles));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    stockpiles->zone =
        calloc((size_t)width * (size_t)height * (size_t)depth, sizeof(*stockpiles->zone));
    if (stockpiles->zone == NULL) {
        return false;
    }
    stockpiles->width = width;
    stockpiles->height = height;
    stockpiles->depth = depth;
    stockpiles->next_number = 1;
    return true;
}

void stockpiles_free(Stockpiles *stockpiles)
{
    free(stockpiles->zone);
    memset(stockpiles, 0, sizeof(*stockpiles));
}

static bool in_bounds(const Stockpiles *stockpiles, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < stockpiles->width && y < stockpiles->height &&
           z < stockpiles->depth;
}

static size_t index_of(const Stockpiles *stockpiles, int x, int y, int z)
{
    return ((size_t)z * (size_t)stockpiles->height + (size_t)y) * (size_t)stockpiles->width +
           (size_t)x;
}

bool stockpile_can_cover(const Map *map, int x, int y, int z)
{
    return map_is_passable(map, x, y, z) &&
           terrain_def((Terrain)map->terrain[map_index(map, x, y, z)])->buildable;
}

/* Clips a rectangle given by two corners in any order to the zone layer. */
static void clip_rect(const Stockpiles *stockpiles, int x0, int y0, int x1, int y1, int *min_x,
                      int *min_y, int *max_x, int *max_y)
{
    *min_x = clampi(x0 < x1 ? x0 : x1, 0, stockpiles->width - 1);
    *max_x = clampi(x0 < x1 ? x1 : x0, 0, stockpiles->width - 1);
    *min_y = clampi(y0 < y1 ? y0 : y1, 0, stockpiles->height - 1);
    *max_y = clampi(y0 < y1 ? y1 : y0, 0, stockpiles->height - 1);
}

static bool is_valid(const Stockpiles *stockpiles, int index)
{
    return index >= 0 && index < STOCKPILE_MAX && stockpiles->list[index].used;
}

/* Makes the bounding box include a tile (the first tile defines it). */
static void grow_bounds(Stockpile *stockpile, int x, int y, int z)
{
    if (stockpile->max_x < stockpile->min_x) {
        stockpile->min_x = stockpile->max_x = x;
        stockpile->min_y = stockpile->max_y = y;
        stockpile->min_z = stockpile->max_z = z;
        return;
    }
    stockpile->min_x = x < stockpile->min_x ? x : stockpile->min_x;
    stockpile->max_x = x > stockpile->max_x ? x : stockpile->max_x;
    stockpile->min_y = y < stockpile->min_y ? y : stockpile->min_y;
    stockpile->max_y = y > stockpile->max_y ? y : stockpile->max_y;
    stockpile->min_z = z < stockpile->min_z ? z : stockpile->min_z;
    stockpile->max_z = z > stockpile->max_z ? z : stockpile->max_z;
}

static void clear_bounds(Stockpile *stockpile)
{
    stockpile->min_x = stockpile->min_y = stockpile->min_z = 0;
    stockpile->max_x = stockpile->max_y = stockpile->max_z = -1; /* Empty. */
}

/* Recomputes the bounding box after tiles were removed. */
static void shrink_bounds(Stockpiles *stockpiles, int index)
{
    Stockpile *stockpile = &stockpiles->list[index];
    Stockpile old = *stockpile;
    clear_bounds(stockpile);
    for (int z = old.min_z; z <= old.max_z; z++) {
        for (int y = old.min_y; y <= old.max_y; y++) {
            for (int x = old.min_x; x <= old.max_x; x++) {
                if (stockpiles->zone[index_of(stockpiles, x, y, z)] == index + 1) {
                    grow_bounds(stockpile, x, y, z);
                }
            }
        }
    }
}

/* Adds the free, coverable tiles of a rectangle that no layer of `blocked`
 * (see stockpiles_add_rect()) covers. */
static int extend(Stockpiles *stockpiles, const Map *map, int index, int x0, int y0, int x1, int y1,
                  int z, const uint8_t *const *blocked, bool view)
{
    if (!is_valid(stockpiles, index)) {
        return 0;
    }
    int min_x, min_y, max_x, max_y;
    clip_rect(stockpiles, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    int added = 0;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int level = view ? map_view_level(map, x, y, z) : z;
            if (level < 0) {
                continue;
            }
            size_t i = index_of(stockpiles, x, y, level);
            uint8_t *zone = &stockpiles->zone[i];
            if (*zone == 0 && !layers_any_set(blocked, i) &&
                stockpile_can_cover(map, x, y, level)) {
                *zone = (uint8_t)(index + 1);
                added++;
                grow_bounds(&stockpiles->list[index], x, y, level);
            }
        }
    }
    stockpiles->list[index].cell_count += added;
    if (added > 0) {
        stockpiles->revision++;
    }
    return added;
}

int stockpile_extend(Stockpiles *stockpiles, const Map *map, int index, int x0, int y0, int x1,
                     int y1, int z)
{
    return extend(stockpiles, map, index, x0, y0, x1, y1, z, NULL, false);
}

static int create(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1, int z,
                  const uint8_t *const *blocked, bool view)
{
    int index = STOCKPILE_NONE;
    for (int i = 0; i < STOCKPILE_MAX; i++) {
        if (!stockpiles->list[i].used) {
            index = i;
            break;
        }
    }
    if (index == STOCKPILE_NONE) {
        return STOCKPILE_NONE;
    }
    Stockpile *stockpile = &stockpiles->list[index];
    *stockpile =
        (Stockpile){.used = true,
                    .number = stockpiles->next_number,
                    .allowed = item_types_all() & ~item_types_in_category(ITEM_CATEGORY_BODIES),
                    .quality = {ITEM_QUALITY_WORST, ITEM_QUALITY_BEST},
                    .hit_points = {0, 100},
                    .priority = STOCKPILE_PRIORITY_DEFAULT,
                    .radius = STOCKPILE_RADIUS_DEFAULT};
    clear_bounds(stockpile);
    if (extend(stockpiles, map, index, x0, y0, x1, y1, z, blocked, view) == 0) {
        stockpile->used = false;
        return STOCKPILE_NONE;
    }
    stockpiles->used_count++;
    stockpiles->next_number++;
    return index;
}

int stockpile_create(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1, int z)
{
    return create(stockpiles, map, x0, y0, x1, y1, z, NULL, false);
}

/* Moves every tile of `from` to `into` and deletes `from`. */
static void merge(Stockpiles *stockpiles, int into, int from)
{
    Stockpile *source = &stockpiles->list[from];
    Stockpile *target = &stockpiles->list[into];
    for (int z = source->min_z; z <= source->max_z; z++) {
        for (int y = source->min_y; y <= source->max_y; y++) {
            for (int x = source->min_x; x <= source->max_x; x++) {
                uint8_t *zone = &stockpiles->zone[index_of(stockpiles, x, y, z)];
                if (*zone == from + 1) {
                    *zone = (uint8_t)(into + 1);
                    grow_bounds(target, x, y, z);
                }
            }
        }
    }
    target->cell_count += source->cell_count;
    source->used = false;
    source->cell_count = 0;
    clear_bounds(source);
    stockpiles->used_count--;
    stockpiles->revision++;
}

static int add_rect(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1, int z,
                    int preferred, const uint8_t *const *blocked, bool view)
{
    int min_x, min_y, max_x, max_y;
    clip_rect(stockpiles, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    bool overlapped[STOCKPILE_MAX] = {false};
    int into = is_valid(stockpiles, preferred) ? preferred : STOCKPILE_NONE;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int level = view ? map_view_level(map, x, y, z) : z;
            if (level < 0) {
                continue;
            }
            int zone = stockpiles->zone[index_of(stockpiles, x, y, level)] - 1;
            if (zone == STOCKPILE_NONE || overlapped[zone]) {
                continue;
            }
            overlapped[zone] = true;
            if (into == STOCKPILE_NONE ||
                (into != preferred &&
                 stockpiles->list[zone].number < stockpiles->list[into].number)) {
                into = zone; /* The oldest, unless one is preferred. */
            }
        }
    }
    if (into == STOCKPILE_NONE) {
        return create(stockpiles, map, x0, y0, x1, y1, z, blocked, view);
    }
    for (int zone = 0; zone < STOCKPILE_MAX; zone++) {
        if (overlapped[zone] && zone != into) {
            merge(stockpiles, into, zone);
        }
    }
    extend(stockpiles, map, into, x0, y0, x1, y1, z, blocked, view);
    return into;
}

int stockpiles_add_rect(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1,
                        int z, int preferred, const uint8_t *const *blocked)
{
    return add_rect(stockpiles, map, x0, y0, x1, y1, z, preferred, blocked, false);
}

int stockpiles_add_view_rect(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1,
                             int z, int preferred, const uint8_t *const *blocked)
{
    return add_rect(stockpiles, map, x0, y0, x1, y1, z, preferred, blocked, true);
}

/* Takes the tiles of a rectangle out of their stockpile, or out of stockpile
 * `only` alone (STOCKPILE_NONE: any). */
static int remove_rect(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1,
                       int z, int only)
{
    int min_x, min_y, max_x, max_y;
    clip_rect(stockpiles, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    int removed = 0;
    bool touched[STOCKPILE_MAX] = {false};
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int level = map != NULL ? map_view_level(map, x, y, z) : z;
            if (level < 0) {
                continue;
            }
            uint8_t *zone = &stockpiles->zone[index_of(stockpiles, x, y, level)];
            if (*zone != 0 && (only == STOCKPILE_NONE || *zone == only + 1)) {
                Stockpile *stockpile = &stockpiles->list[*zone - 1];
                touched[*zone - 1] = true;
                if (--stockpile->cell_count == 0) {
                    stockpile->used = false;
                    stockpiles->used_count--;
                }
                *zone = 0;
                removed++;
            }
        }
    }
    for (int i = 0; i < STOCKPILE_MAX; i++) {
        if (touched[i]) {
            shrink_bounds(stockpiles, i);
        }
    }
    if (removed > 0) {
        stockpiles->revision++; /* Items there may now need hauling. */
    }
    return removed;
}

int stockpiles_remove_rect(Stockpiles *stockpiles, int x0, int y0, int x1, int y1, int z)
{
    return remove_rect(stockpiles, NULL, x0, y0, x1, y1, z, STOCKPILE_NONE);
}

int stockpiles_remove_view_rect(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1,
                                int y1, int z)
{
    return remove_rect(stockpiles, map, x0, y0, x1, y1, z, STOCKPILE_NONE);
}

int stockpile_extend_view(Stockpiles *stockpiles, const Map *map, int index, int x0, int y0, int x1,
                          int y1, int z, const uint8_t *const *blocked)
{
    return extend(stockpiles, map, index, x0, y0, x1, y1, z, blocked, true);
}

int stockpile_shrink_view(Stockpiles *stockpiles, const Map *map, int index, int x0, int y0, int x1,
                          int y1, int z)
{
    return is_valid(stockpiles, index) ? remove_rect(stockpiles, map, x0, y0, x1, y1, z, index) : 0;
}

void stockpile_delete(Stockpiles *stockpiles, int index)
{
    if (!is_valid(stockpiles, index)) {
        return;
    }
    Stockpile *stockpile = &stockpiles->list[index];
    for (int z = stockpile->min_z; z <= stockpile->max_z; z++) {
        for (int y = stockpile->min_y; y <= stockpile->max_y; y++) {
            for (int x = stockpile->min_x; x <= stockpile->max_x; x++) {
                uint8_t *zone = &stockpiles->zone[index_of(stockpiles, x, y, z)];
                if (*zone == index + 1) {
                    *zone = 0;
                }
            }
        }
    }
    stockpile->used = false;
    stockpile->cell_count = 0;
    clear_bounds(stockpile);
    stockpiles->used_count--;
    stockpiles->revision++;
}

const Stockpile *stockpile_get(const Stockpiles *stockpiles, int index)
{
    return is_valid(stockpiles, index) ? &stockpiles->list[index] : NULL;
}

int stockpile_at(const Stockpiles *stockpiles, int x, int y, int z)
{
    if (!in_bounds(stockpiles, x, y, z)) {
        return STOCKPILE_NONE;
    }
    return (int)stockpiles->zone[index_of(stockpiles, x, y, z)] - 1;
}

void stockpile_set_allowed(Stockpiles *stockpiles, int index, ItemTypeMask allowed)
{
    if (is_valid(stockpiles, index)) {
        stockpiles->list[index].allowed = allowed & item_types_all();
        stockpiles->revision++;
    }
}

static IntRange valid_range(IntRange range, int max)
{
    int a = clampi(range.min, 0, max);
    int b = clampi(range.max, 0, max);
    return a <= b ? (IntRange){a, b} : (IntRange){b, a};
}

static bool same_range(IntRange a, IntRange b)
{
    return a.min == b.min && a.max == b.max;
}

void stockpile_set_quality_range(Stockpiles *stockpiles, int index, IntRange range)
{
    if (is_valid(stockpiles, index)) {
        range = valid_range(range, ITEM_QUALITY_BEST);
        if (!same_range(range, stockpiles->list[index].quality)) {
            stockpiles->list[index].quality = range;
            stockpiles->revision++;
        }
    }
}

void stockpile_set_hit_points_range(Stockpiles *stockpiles, int index, IntRange range)
{
    if (is_valid(stockpiles, index)) {
        range = valid_range(range, 100);
        if (!same_range(range, stockpiles->list[index].hit_points)) {
            stockpiles->list[index].hit_points = range;
            stockpiles->revision++;
        }
    }
}

static bool in_range(IntRange range, int value)
{
    return value >= range.min && value <= range.max;
}

bool stockpile_accepts(const Stockpile *stockpile, ItemStack stack)
{
    return stockpile != NULL && stack.type != ITEM_NONE && stack.count > 0 &&
           (stockpile->allowed & ITEM_TYPE_BIT(stack.type)) &&
           in_range(stockpile->quality, stack.quality) &&
           in_range(stockpile->hit_points, item_stack_hit_points_percent(stack));
}

void stockpile_set_priority(Stockpiles *stockpiles, int index, int priority)
{
    if (is_valid(stockpiles, index)) {
        stockpiles->list[index].priority =
            clampi(priority, STOCKPILE_PRIORITY_HIGHEST, STOCKPILE_PRIORITY_LOWEST);
        stockpiles->revision++;
    }
}

void stockpile_set_radius(Stockpiles *stockpiles, int index, int radius)
{
    if (!is_valid(stockpiles, index)) {
        return;
    }
    radius = radius <= 0 || radius > STOCKPILE_RADIUS_MAX
                 ? STOCKPILE_RADIUS_UNLIMITED
                 : clampi(radius, STOCKPILE_RADIUS_MIN, STOCKPILE_RADIUS_MAX);
    if (stockpiles->list[index].radius != radius) {
        stockpiles->list[index].radius = radius;
        stockpiles->revision++;
    }
}

/* Tiles from v to the range lo..hi (0 inside). */
static int outside(int v, int lo, int hi)
{
    return v < lo ? lo - v : (v > hi ? v - hi : 0);
}

bool stockpile_in_radius(const Stockpile *stockpile, int x, int y, int z)
{
    if (stockpile->radius == STOCKPILE_RADIUS_UNLIMITED) {
        return true;
    }
    int dx = outside(x, stockpile->min_x, stockpile->max_x);
    int dy = outside(y, stockpile->min_y, stockpile->max_y);
    int dz = outside(z, stockpile->min_z, stockpile->max_z);
    return dx * dx + dy * dy + dz * dz <= stockpile->radius * stockpile->radius;
}

/* Every tile up to 20, then by 2, 4 and 8, unlimited last. */
static const int RADIUS_STEPS[] = {
    2,  3,   4,   5,   6,   7,   8,   9,   10,  11,
    12, 13,  14,  15,  16,  17,  18,  19,  20,  22,
    24, 26,  28,  30,  32,  34,  36,  38,  40,  44,
    48, 52,  56,  60,  64,  68,  72,  76,  80,  88,
    96, 104, 112, 120, 128, 136, 144, 152, 160, STOCKPILE_RADIUS_UNLIMITED};
#define RADIUS_STEP_COUNT ((int)(sizeof(RADIUS_STEPS) / sizeof(RADIUS_STEPS[0])))
_Static_assert(STOCKPILE_RADIUS_MIN == 2 && STOCKPILE_RADIUS_MAX == 160,
               "RADIUS_STEPS goes from the minimum to the maximum");

int stockpile_radius_steps(void)
{
    return RADIUS_STEP_COUNT;
}

int stockpile_radius_of_step(int step)
{
    return RADIUS_STEPS[clampi(step, 0, RADIUS_STEP_COUNT - 1)];
}

int stockpile_radius_step(int radius)
{
    if (radius == STOCKPILE_RADIUS_UNLIMITED || radius > STOCKPILE_RADIUS_MAX) {
        return RADIUS_STEP_COUNT - 1;
    }
    int best = 0;
    for (int step = 1; step < RADIUS_STEP_COUNT - 1; step++) {
        if (abs(RADIUS_STEPS[step] - radius) < abs(RADIUS_STEPS[best] - radius)) {
            best = step;
        }
    }
    return best;
}

void stockpile_set_forbidden(Stockpiles *stockpiles, int index, bool forbidden)
{
    if (is_valid(stockpiles, index) && stockpiles->list[index].forbidden != forbidden) {
        stockpiles->list[index].forbidden = forbidden;
        stockpiles->revision++;
    }
}

bool stockpile_forbidden_at(const Stockpiles *stockpiles, int x, int y, int z)
{
    const Stockpile *stockpile = stockpile_get(stockpiles, stockpile_at(stockpiles, x, y, z));
    return stockpile != NULL && stockpile->forbidden;
}

int stockpile_storage_priority(const Stockpiles *stockpiles, int x, int y, int z, ItemStack stack)
{
    const Stockpile *stockpile = stockpile_get(stockpiles, stockpile_at(stockpiles, x, y, z));
    return stockpile_accepts(stockpile, stack) ? stockpile->priority : 0;
}

void stockpiles_count_stored(const Stockpiles *stockpiles, const Items *items,
                             long stored[ITEM_TYPE_COUNT])
{
    for (int index = 0; index < STOCKPILE_MAX; index++) {
        const Stockpile *stockpile = &stockpiles->list[index];
        if (!stockpile->used) {
            continue;
        }
        for (int z = stockpile->min_z; z <= stockpile->max_z; z++) {
            for (int y = stockpile->min_y; y <= stockpile->max_y; y++) {
                for (int x = stockpile->min_x; x <= stockpile->max_x; x++) {
                    if (stockpiles->zone[index_of(stockpiles, x, y, z)] != index + 1) {
                        continue;
                    }
                    ItemStack groups[STOCKPILE_GROUPS_COUNTED];
                    int count = items_groups(items, x, y, z, groups, STOCKPILE_GROUPS_COUNTED);
                    for (int i = 0; i < count; i++) {
                        if (stockpile_accepts(stockpile, groups[i])) {
                            stored[groups[i].type] += groups[i].count;
                        }
                    }
                }
            }
        }
    }
}

uint64_t stockpiles_hash(const Stockpiles *stockpiles, uint64_t hash)
{
    hash = hash_fnv1a64(hash, stockpiles->zone,
                        (size_t)stockpiles->width * (size_t)stockpiles->height *
                            (size_t)stockpiles->depth);
    for (int i = 0; i < STOCKPILE_MAX; i++) {
        const Stockpile *s = &stockpiles->list[i];
        if (s->used) {
            int32_t fields[9] = {i,
                                 s->number,
                                 (int32_t)s->allowed,
                                 s->priority,
                                 s->cell_count,
                                 s->quality.min,
                                 s->quality.max,
                                 s->hit_points.min,
                                 s->hit_points.max};
            hash = hash_fnv1a64(hash, fields, sizeof(fields));
            uint32_t high = (uint32_t)(s->allowed >> 32); /* Only then: as before without. */
            if (high != 0) {
                hash = hash_fnv1a64(hash, &high, sizeof(high));
            }
            if (s->forbidden) { /* Hashes of games that forbid nothing stay as they were. */
                hash = hash_fnv1a64(hash, &s->forbidden, sizeof(s->forbidden));
            }
            int32_t radius = s->radius;
            hash = hash_fnv1a64(hash, &radius, sizeof(radius));
        }
    }
    return hash;
}
