#include "core/corpse.h"

#include <stdlib.h>
#include <string.h>

void corpses_init(Corpses *corpses)
{
    memset(corpses, 0, sizeof(*corpses));
}

void corpses_free(Corpses *corpses)
{
    free(corpses->list);
    memset(corpses, 0, sizeof(*corpses));
}

/* The next free record, with its id and tick set; NULL when none can be kept. */
static Corpse *next_record(Corpses *corpses, CorpseKind kind, uint64_t tick)
{
    if (corpses->count >= (int)CORPSES_MAX) {
        return NULL;
    }
    if (corpses->count == corpses->capacity) {
        int capacity = corpses->capacity == 0 ? 4 : corpses->capacity * 2;
        if (capacity > (int)CORPSES_MAX) {
            capacity = (int)CORPSES_MAX;
        }
        Corpse *list = realloc(corpses->list, (size_t)capacity * sizeof(*list));
        if (list == NULL) {
            return NULL;
        }
        corpses->list = list;
        corpses->capacity = capacity;
    }
    Corpse *corpse = &corpses->list[corpses->count++];
    memset(corpse, 0, sizeof(*corpse));
    corpse->id = (uint16_t)corpses->count;
    corpse->kind = (uint8_t)kind;
    corpse->died = tick;
    return corpse;
}

uint16_t corpses_add_settler(Corpses *corpses, const Colonist *dead, uint64_t tick)
{
    Corpse *corpse = next_record(corpses, CORPSE_SETTLER, tick);
    if (corpse == NULL) {
        return 0;
    }
    corpse->colonist = *dead;
    /* What it held is on the ground, and its path belongs to the living. */
    corpse->colonist.path = (Path){0};
    corpse->colonist.path_index = 0;
    corpse->colonist.carried = (ItemStack){.type = ITEM_NONE};
    memset(corpse->colonist.tools, 0, sizeof(corpse->colonist.tools));
    return corpse->id;
}

uint16_t corpses_add_animal(Corpses *corpses, const Animal *dead, uint64_t tick)
{
    Corpse *corpse = next_record(corpses, CORPSE_ANIMAL, tick);
    if (corpse == NULL) {
        return 0;
    }
    corpse->animal = *dead;
    corpse->animal.path = (Path){0};
    corpse->animal.path_index = 0;
    return corpse->id;
}

const Corpse *corpses_get(const Corpses *corpses, uint16_t id)
{
    if (id == 0 || id > corpses->count) {
        return NULL;
    }
    return &corpses->list[id - 1];
}
