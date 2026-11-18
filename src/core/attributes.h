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
    /* Body again: added later, they come last so that the earlier ones are
     * still rolled the same. */
    ATTRIBUTE_TOUGHNESS,      /* Damage taken from blows. */
    ATTRIBUTE_PAIN_TOLERANCE, /* Pain felt from injuries. */
    /* Mind again, added later too. */
    ATTRIBUTE_COURAGE, /* How hurt the settler must be to break off a fight. */
    /* Body again, for the settlers' running (later) and the animals' flight. */
    ATTRIBUTE_RUNNING,   /* Running speed (the animals' flight and chase). */
    ATTRIBUTE_ENDURANCE, /* How long it can run flat out, and how fast it recovers. */
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

/* The attributes of an animal: drawn like a settler's, but it neither learns
 * nor feels the weight of its thoughts: its learning and its temperament sit
 * at the very bottom of their range (-100 %). Its other attributes change
 * its species' values (docs/ATTRIBUTES.md section 6). */
void attributes_roll_animal(Attributes *attributes, Rng *rng);

/* Fails on an invalid type or a position out of range. */
bool attributes_set(Attributes *attributes, AttributeType type, int position);

/* Position of an attribute as -1..1 (0 for an invalid type). */
float attribute_position(const Attributes *attributes, AttributeType type);

/* Effects. The factor goes from 1/ATTRIBUTE_FACTOR_MAX to ATTRIBUTE_FACTOR_MAX
 * (walking, work pace, learning, sleep, appetite; 1 for the others). */
float attribute_factor(const Attributes *attributes, AttributeType type);

/* Factors of the health (docs/HEALTH.md): damage taken (toughness) and pain
 * felt (pain tolerance), from ATTRIBUTE_FACTOR_MAX at -1 down to its inverse
 * at +1. */
float attribute_damage_factor(const Attributes *attributes);
float attribute_pain_factor(const Attributes *attributes);

/* How hurt a fight must have left the settler (combat_hurt_fraction(), 0..1)
 * before it breaks off, from its courage: 0.4 when ordinary, down to 0.08 at
 * the most fearful, up to 1.1 (never) at the very bravest (docs/COMBAT.md). */
#define ATTRIBUTE_NERVE_ORDINARY 0.4f
float attribute_nerve(const Attributes *attributes);

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
