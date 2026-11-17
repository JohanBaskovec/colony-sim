/*
 * Settler attributes (docs/ATTRIBUTES.md): innate values of the body and the
 * mind, each a position from -1 to +1 on a continuous scale, 0 being the
 * ordinary settler. There are no all-or-nothing traits.
 *
 * Positions are drawn from a bell curve (most settlers near 0, extremes very
 * rare) and kept in thousandths, so that they are integers: part of the
 * state hash, the same on every machine. Their effects are continuous: a
 * factor from x0.5 to x2 (position 2^p), a thought weight from x0.6 to x1.5,
 * a few degrees, a few points of mood. The player never sees a number: a
 * position is shown as a word ("very quick") from a small distance to
 * ordinary on, in bands.
 */
#ifndef COLONY_CORE_ATTRIBUTES_H
#define COLONY_CORE_ATTRIBUTES_H

#include "core/rng.h"

#include <stdbool.h>
#include <stdint.h>

/* Positions are in thousandths of the range. */
#define ATTRIBUTE_POSITION_MAX 1000
/* The standard deviation of a drawn position (tuning): about 68 % of the
 * settlers lie within it, 1 % beyond 0.77 (see attribute_band()). */
#define ATTRIBUTE_ROLL_DEVIATION 300

/* Effects at the end of the range (position +1; the opposite end for -1). */
#define ATTRIBUTE_FACTOR_MAX 2.0f      /* Factors: x2 (x0.5 at -1). */
#define ATTRIBUTE_SENSITIVE_MAX 1.5f   /* Weight of thoughts: x1.5 (x0.6 at -1). */
#define ATTRIBUTE_STOIC_MIN 0.6f       /* Weight of thoughts at -1. */
#define ATTRIBUTE_TOLERANCE_RANGE 5.0f /* Degrees the comfortable range shifts. */
#define ATTRIBUTE_OUTLOOK_RANGE 15.0f  /* Points of mood. */

/* Distance from 0 (in thousandths) where each word band starts: "a little",
 * "very", "extremely", "exceptionally". For a factor they are x1.1, x1.2, x1.4
 * and x1.7. */
#define ATTRIBUTE_BANDS 4
#define ATTRIBUTE_BAND_STARTS                                                                      \
    {                                                                                              \
        140, 260, 490, 770                                                                         \
    }

typedef enum AttributeType {
    /* Body. */
    ATTRIBUTE_WALKING,        /* Walking speed. */
    ATTRIBUTE_WORK_PACE,      /* Speed of all work, on top of the level, tools and mood. */
    ATTRIBUTE_LEARNING,       /* Learning rate from every source, on top of the age. */
    ATTRIBUTE_SLEEP,          /* Rest regained asleep. */
    ATTRIBUTE_APPETITE,       /* How fast the food need falls. */
    ATTRIBUTE_COLD_TOLERANCE, /* Shifts the coldest comfortable felt temperature. */
    ATTRIBUTE_HEAT_TOLERANCE, /* Shifts the warmest comfortable felt temperature. */
    /* Mind. */
    ATTRIBUTE_OUTLOOK,     /* Points added to the mood: gloomy or cheerful. */
    ATTRIBUTE_TEMPERAMENT, /* Weight of every thought: stoic or sensitive. */
    ATTRIBUTE_COUNT
} AttributeType;

typedef enum AttributeGroup {
    ATTRIBUTE_GROUP_BODY,
    ATTRIBUTE_GROUP_MIND,
    ATTRIBUTE_GROUP_COUNT
} AttributeGroup;

typedef struct Attributes {
    /* -ATTRIBUTE_POSITION_MAX..ATTRIBUTE_POSITION_MAX; 0: ordinary. */
    int16_t position[ATTRIBUTE_COUNT];
} Attributes;

/* Every attribute ordinary. */
void attributes_init(Attributes *attributes);

/* Draws every position on the bell curve from `rng`: the sum of twelve
 * uniform draws, scaled to ATTRIBUTE_ROLL_DEVIATION and clamped. Only
 * integer arithmetic, so the result is the same everywhere. */
void attributes_roll(Attributes *attributes, Rng *rng);

/* Fails on an invalid type or a position out of range. */
bool attributes_set(Attributes *attributes, AttributeType type, int position);

/* Position of an attribute as -1..1 (0 for an invalid type). */
float attribute_position(const Attributes *attributes, AttributeType type);

/* Effects. The factor goes from 1/ATTRIBUTE_FACTOR_MAX to ATTRIBUTE_FACTOR_MAX
 * (walking, work pace, learning, sleep, appetite; 1 for the others). */
float attribute_factor(const Attributes *attributes, AttributeType type);

/* Weight of every thought, ATTRIBUTE_STOIC_MIN to ATTRIBUTE_SENSITIVE_MAX. */
float attribute_thought_weight(const Attributes *attributes);

/* Points of mood added by the outlook, -ATTRIBUTE_OUTLOOK_RANGE to +. */
float attribute_outlook_points(const Attributes *attributes);

/* Degrees the comfortable range reaches further: colder with cold
 * tolerance, hotter with heat tolerance (negative when it is lower). */
float attribute_cold_shift(const Attributes *attributes);
float attribute_heat_shift(const Attributes *attributes);

/* How far from ordinary a position lies, as a word: 0 nothing to say, 1 "a
 * little", 2 "very", 3 "extremely", 4 "exceptionally". */
int attribute_band(int position);

AttributeGroup attribute_group(AttributeType type);

/* Translation keys: the attribute, what it does, and the word for a position
 * (NULL for an ordinary one: nothing to say). */
const char *attribute_name_key(AttributeType type);
const char *attribute_description_key(AttributeType type);
const char *attribute_word_key(AttributeType type, int position);

uint64_t attributes_hash(const Attributes *attributes, uint64_t hash);

#endif /* COLONY_CORE_ATTRIBUTES_H */
