/*
 * Settlers for tests: newly arrived settlers have attributes drawn at random
 * (walking, sleep, appetite...), so tests that count on exact timings make
 * theirs ordinary.
 */
#ifndef COLONY_TEST_SETTLERS_H
#define COLONY_TEST_SETTLERS_H

#include "core/colonist.h"
#include "core/game.h"

/* Every attribute of the colonist ordinary (position 0). */
static inline void make_ordinary(Colonist *colonist)
{
    Attributes attributes;
    attributes_init(&attributes);
    colonist_set_attributes(colonist, &attributes);
}

/* Animals too have characters drawn at random (core/animal.h): tests that count
 * on exact speeds and rates make theirs ordinary. */
static inline void make_ordinary_animal(Animal *animal)
{
    Attributes attributes;
    attributes_init(&attributes);
    animal_set_attributes(animal, &attributes);
}

static inline EntityId spawn_ordinary_animal(Game *game, AnimalSpecies species, AnimalSex sex,
                                             int x, int y, int z)
{
    EntityId id = game_spawn_animal(game, species, sex, x, y, z);
    Animal *animal = animals_get(&game->animals, id);
    if (animal != NULL) {
        make_ordinary_animal(animal);
    }
    return id;
}

static inline EntityId debug_spawn_ordinary_animal(Game *game, AnimalSpecies species, AnimalSex sex,
                                                   int x, int y, int z)
{
    EntityId id = game_debug_spawn_animal(game, species, sex, x, y, z);
    Animal *animal = animals_get(&game->animals, id);
    if (animal != NULL) {
        make_ordinary_animal(animal);
    }
    return id;
}

#endif /* COLONY_TEST_SETTLERS_H */
