/*
 * Fixed-capacity slot allocator with generational handles.
 *
 * A Pool only manages slot indices; the owner stores the actual data in
 * parallel arrays of the same capacity. Handles (EntityId) combine a slot index
 * with the generation of the slot at allocation time: once the slot is
 * released, every old handle to it becomes detectably stale, so systems can
 * keep references to entities without dangling pointers.
 */
#ifndef COLONY_CORE_POOL_H
#define COLONY_CORE_POOL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct EntityId {
    uint32_t index;
    uint32_t generation; /* 0 is never used by a live entity. */
} EntityId;

#define ENTITY_NONE ((EntityId){0u, 0u})

static inline bool entity_id_equal(EntityId a, EntityId b)
{
    return a.index == b.index && a.generation == b.generation;
}

static inline bool entity_id_is_none(EntityId id)
{
    return id.generation == 0u;
}

typedef struct Pool {
    uint32_t capacity;
    uint32_t count;        /* Live slots. */
    uint32_t high_water;   /* Slots [0, high_water) have been used at least once. */
    uint32_t *generations; /* Current generation of each slot. */
    bool *alive;
    uint32_t *free_list; /* Released slots, reused last-in first-out. */
    uint32_t free_count;
} Pool;

bool pool_init(Pool *pool, uint32_t capacity);
void pool_free(Pool *pool);

/* Returns ENTITY_NONE when the pool is full. */
EntityId pool_alloc(Pool *pool);

/* Returns false if the handle is stale or invalid. */
bool pool_release(Pool *pool, EntityId id);

bool pool_is_alive(const Pool *pool, EntityId id);

/* Handle of a live slot (for iteration), or ENTITY_NONE if the slot is free. */
EntityId pool_id_at(const Pool *pool, uint32_t index);

#endif /* COLONY_CORE_POOL_H */
