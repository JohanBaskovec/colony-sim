/*
 * Looks of a colonist: skin tone, body shape, hair and beard, rolled once when the
 * colonist is created. The values are indices into the sprite sets of the
 * frontend; the core only picks them (deterministically) and keeps them.
 */
#ifndef COLONY_CORE_APPEARANCE_H
#define COLONY_CORE_APPEARANCE_H

#include "core/serialize.h"

#include "core/rng.h"

#include <stdbool.h>
#include <stdint.h>

#define APPEARANCE_SKIN_COUNT 5
#define APPEARANCE_BODY_COUNT 3
#define APPEARANCE_HAIR_STYLE_COUNT 6 /* Style 0 is bald. */
#define APPEARANCE_BEARD_COUNT 5      /* Style 0 is clean-shaven. */

/* Chance of being bald, and of having a beard. */
#define APPEARANCE_BALD_CHANCE 0.1f
#define APPEARANCE_BEARD_CHANCE 0.4f
/* Gray hair: likely from APPEARANCE_GRAY_AGE, possible from APPEARANCE_GRAYING_AGE. */
#define APPEARANCE_GRAYING_AGE 40
#define APPEARANCE_GRAY_AGE 52

typedef enum HairColor {
    HAIR_COLOR_BLACK,
    HAIR_COLOR_DARK_BROWN,
    HAIR_COLOR_BROWN,
    HAIR_COLOR_AUBURN,
    HAIR_COLOR_BLOND,
    HAIR_COLOR_GRAY,
    HAIR_COLOR_COUNT
} HairColor;

typedef struct ColonistAppearance {
    uint8_t skin;       /* 0 .. APPEARANCE_SKIN_COUNT - 1, light to dark. */
    uint8_t body;       /* Shape, thin to heavy: 0 .. APPEARANCE_BODY_COUNT - 1. */
    uint8_t hair_style; /* 0 (bald) .. APPEARANCE_HAIR_STYLE_COUNT - 1. */
    uint8_t hair_color; /* HairColor, also the color of the beard. */
    uint8_t beard;      /* 0 (none) .. APPEARANCE_BEARD_COUNT - 1. */
} ColonistAppearance;

/* Picks a random appearance; older colonists are more likely to be gray. */
ColonistAppearance appearance_roll(Rng *rng, int age);

/* Whether every field is in range. */
bool appearance_is_valid(ColonistAppearance appearance);

/* Saves or loads (docs/SAVE.md) an appearance. */
void appearance_serialize(Serializer *s, ColonistAppearance *appearance);

#endif /* COLONY_CORE_APPEARANCE_H */
