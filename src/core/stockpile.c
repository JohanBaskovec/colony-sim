#include "core/stockpile.h"

#include "core/util.h"

#include <stdlib.h>
#include <string.h>

bool stockpiles_init(Stockpiles *stockpiles, int width, int height)
{
    memset(stockpiles, 0, sizeof(*stockpiles));
    if (width <= 0 || height <= 0) {
        return false;
    }
    stockpiles->zone = calloc((size_t)width * (size_t)height, sizeof(*stockpiles->zone));
    if (stockpiles->zone == NULL) {
        return false;
    }
    stockpiles->width = width;
    stockpiles->height = height;
    stockpiles->next_number = 1;
    return true;
}

void stockpiles_free(Stockpiles *stockpiles)
{
    free(stockpiles->zone);
    memset(stockpiles, 0, sizeof(*stockpiles));
}

static bool in_bounds(const Stockpiles *stockpiles, int x, int y)
{
    return x >= 0 && y >= 0 && x < stockpiles->width && y < stockpiles->height;
}

static size_t index_of(const Stockpiles *stockpiles, int x, int y)
{
    return (size_t)y * (size_t)stockpiles->width + (size_t)x;
}

bool stockpile_can_cover(const Map *map, int x, int y)
{
    return map_is_passable(map, x, y) &&
           terrain_def((Terrain)map->terrain[map_index(map, x, y)])->buildable;
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
static void grow_bounds(Stockpile *stockpile, int x, int y)
{
    if (stockpile->max_x < stockpile->min_x) {
        stockpile->min_x = stockpile->max_x = x;
        stockpile->min_y = stockpile->max_y = y;
        return;
    }
    stockpile->min_x = x < stockpile->min_x ? x : stockpile->min_x;
    stockpile->max_x = x > stockpile->max_x ? x : stockpile->max_x;
    stockpile->min_y = y < stockpile->min_y ? y : stockpile->min_y;
    stockpile->max_y = y > stockpile->max_y ? y : stockpile->max_y;
}

static void clear_bounds(Stockpile *stockpile)
{
    stockpile->min_x = stockpile->min_y = 0;
    stockpile->max_x = stockpile->max_y = -1; /* Empty. */
}

/* Recomputes the bounding box after tiles were removed. */
static void shrink_bounds(Stockpiles *stockpiles, int index)
{
    Stockpile *stockpile = &stockpiles->list[index];
    Stockpile old = *stockpile;
    clear_bounds(stockpile);
    for (int y = old.min_y; y <= old.max_y; y++) {
        for (int x = old.min_x; x <= old.max_x; x++) {
            if (stockpiles->zone[index_of(stockpiles, x, y)] == index + 1) {
                grow_bounds(stockpile, x, y);
            }
        }
    }
}

int stockpile_extend(Stockpiles *stockpiles, const Map *map, int index, int x0, int y0, int x1,
                     int y1)
{
    if (!is_valid(stockpiles, index)) {
        return 0;
    }
    int min_x, min_y, max_x, max_y;
    clip_rect(stockpiles, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    int added = 0;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            uint8_t *zone = &stockpiles->zone[index_of(stockpiles, x, y)];
            if (*zone == 0 && stockpile_can_cover(map, x, y)) {
                *zone = (uint8_t)(index + 1);
                added++;
                grow_bounds(&stockpiles->list[index], x, y);
            }
        }
    }
    stockpiles->list[index].cell_count += added;
    if (added > 0) {
        stockpiles->revision++;
    }
    return added;
}

int stockpile_create(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1)
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
    *stockpile = (Stockpile){.used = true,
                             .number = stockpiles->next_number,
                             .allowed = item_types_all(),
                             .quality = {0, ITEM_QUALITY_BEST},
                             .hit_points = {0, 100},
                             .priority = STOCKPILE_PRIORITY_DEFAULT};
    clear_bounds(stockpile);
    if (stockpile_extend(stockpiles, map, index, x0, y0, x1, y1) == 0) {
        stockpile->used = false;
        return STOCKPILE_NONE;
    }
    stockpiles->used_count++;
    stockpiles->next_number++;
    return index;
}

/* Moves every tile of `from` to `into` and deletes `from`. */
static void merge(Stockpiles *stockpiles, int into, int from)
{
    Stockpile *source = &stockpiles->list[from];
    Stockpile *target = &stockpiles->list[into];
    for (int y = source->min_y; y <= source->max_y; y++) {
        for (int x = source->min_x; x <= source->max_x; x++) {
            uint8_t *zone = &stockpiles->zone[index_of(stockpiles, x, y)];
            if (*zone == from + 1) {
                *zone = (uint8_t)(into + 1);
                grow_bounds(target, x, y);
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

int stockpiles_add_rect(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1,
                        int preferred)
{
    int min_x, min_y, max_x, max_y;
    clip_rect(stockpiles, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    bool overlapped[STOCKPILE_MAX] = {false};
    int into = is_valid(stockpiles, preferred) ? preferred : STOCKPILE_NONE;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int zone = stockpiles->zone[index_of(stockpiles, x, y)] - 1;
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
        return stockpile_create(stockpiles, map, x0, y0, x1, y1);
    }
    for (int zone = 0; zone < STOCKPILE_MAX; zone++) {
        if (overlapped[zone] && zone != into) {
            merge(stockpiles, into, zone);
        }
    }
    stockpile_extend(stockpiles, map, into, x0, y0, x1, y1);
    return into;
}

int stockpiles_remove_rect(Stockpiles *stockpiles, int x0, int y0, int x1, int y1)
{
    int min_x, min_y, max_x, max_y;
    clip_rect(stockpiles, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    int removed = 0;
    bool touched[STOCKPILE_MAX] = {false};
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            uint8_t *zone = &stockpiles->zone[index_of(stockpiles, x, y)];
            if (*zone != 0) {
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

void stockpile_delete(Stockpiles *stockpiles, int index)
{
    if (!is_valid(stockpiles, index)) {
        return;
    }
    Stockpile *stockpile = &stockpiles->list[index];
    for (int y = stockpile->min_y; y <= stockpile->max_y; y++) {
        for (int x = stockpile->min_x; x <= stockpile->max_x; x++) {
            uint8_t *zone = &stockpiles->zone[index_of(stockpiles, x, y)];
            if (*zone == index + 1) {
                *zone = 0;
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

int stockpile_at(const Stockpiles *stockpiles, int x, int y)
{
    if (!in_bounds(stockpiles, x, y)) {
        return STOCKPILE_NONE;
    }
    return (int)stockpiles->zone[index_of(stockpiles, x, y)] - 1;
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

int stockpile_storage_priority(const Stockpiles *stockpiles, int x, int y, ItemStack stack)
{
    const Stockpile *stockpile = stockpile_get(stockpiles, stockpile_at(stockpiles, x, y));
    return stockpile_accepts(stockpile, stack) ? stockpile->priority : 0;
}

uint64_t stockpiles_hash(const Stockpiles *stockpiles, uint64_t hash)
{
    hash = hash_fnv1a64(hash, stockpiles->zone,
                        (size_t)stockpiles->width * (size_t)stockpiles->height);
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
        }
    }
    return hash;
}
