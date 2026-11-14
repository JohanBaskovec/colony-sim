#include "core/pool.h"

#include <stdlib.h>
#include <string.h>

bool pool_init(Pool *pool, uint32_t capacity)
{
    memset(pool, 0, sizeof(*pool));
    if (capacity == 0) {
        return false;
    }
    pool->generations = calloc(capacity, sizeof(*pool->generations));
    pool->alive = calloc(capacity, sizeof(*pool->alive));
    pool->free_list = malloc(capacity * sizeof(*pool->free_list));
    if (pool->generations == NULL || pool->alive == NULL || pool->free_list == NULL) {
        pool_free(pool);
        return false;
    }
    pool->capacity = capacity;
    return true;
}

void pool_free(Pool *pool)
{
    free(pool->generations);
    free(pool->alive);
    free(pool->free_list);
    memset(pool, 0, sizeof(*pool));
}

EntityId pool_alloc(Pool *pool)
{
    uint32_t index;
    if (pool->free_count > 0) {
        index = pool->free_list[--pool->free_count];
    } else if (pool->high_water < pool->capacity) {
        index = pool->high_water++;
    } else {
        return ENTITY_NONE;
    }

    pool->generations[index]++;
    if (pool->generations[index] == 0u) {
        pool->generations[index] = 1u; /* Skip 0 on wrap-around: it means "none". */
    }
    pool->alive[index] = true;
    pool->count++;
    return (EntityId){index, pool->generations[index]};
}

bool pool_is_alive(const Pool *pool, EntityId id)
{
    return id.generation != 0u && id.index < pool->high_water && pool->alive[id.index] &&
           pool->generations[id.index] == id.generation;
}

bool pool_release(Pool *pool, EntityId id)
{
    if (!pool_is_alive(pool, id)) {
        return false;
    }
    pool->alive[id.index] = false;
    pool->free_list[pool->free_count++] = id.index;
    pool->count--;
    return true;
}

EntityId pool_id_at(const Pool *pool, uint32_t index)
{
    if (index >= pool->high_water || !pool->alive[index]) {
        return ENTITY_NONE;
    }
    return (EntityId){index, pool->generations[index]};
}
