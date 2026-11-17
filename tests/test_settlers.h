/*
 * Settlers for tests: newly arrived settlers have attributes drawn at random
 * (walking, sleep, appetite...), so tests that count on exact timings make
 * theirs ordinary.
 */
#ifndef COLONY_TEST_SETTLERS_H
#define COLONY_TEST_SETTLERS_H

#include "core/colonist.h"

/* Every attribute of the colonist ordinary (position 0). */
static inline void make_ordinary(Colonist *colonist)
{
    Attributes attributes;
    attributes_init(&attributes);
    colonist_set_attributes(colonist, &attributes);
}

#endif /* COLONY_TEST_SETTLERS_H */
