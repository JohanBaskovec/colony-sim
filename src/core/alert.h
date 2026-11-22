/*
 * Alerts: what is wrong in the colony right now, found from its state (a
 * settler about to starve, no food left...), as opposed to notices
 * (core/notice.h), which tell of something that happened. The frontend lists
 * them above the date and each one jumps to the settler concerned. Nothing
 * here is kept: alerts_compute() looks at the game again each time, so an
 * alert goes away by itself when its cause does. Not part of game_hash().
 */
#ifndef COLONY_CORE_ALERT_H
#define COLONY_CORE_ALERT_H

#include "core/game.h"

/* In the order they are listed (the most pressing first). */
typedef enum AlertType {
    ALERT_IN_DANGER, /* Settlers whose consciousness is low (docs/HEALTH.md §11). */
    ALERT_STARVING,  /* Settlers whose food need is critical. */
    ALERT_THIRSTY,   /* ... water. */
    /* Hungry settlers whose last search for food found none they may reach (in their
     * allowed area, core/allowed_area.h). */
    ALERT_FOOD_OUT_OF_REACH,
    ALERT_WATER_OUT_OF_REACH, /* ... thirsty, for water. */
    ALERT_EXHAUSTED,          /* ... rest. */
    ALERT_NO_FOOD,            /* The colony's food will not last a day. */
    ALERT_BREAKDOWN,          /* Settlers breaking down (core/mood.h). */
    ALERT_NO_BED,             /* Settlers sleeping on the ground. */
    ALERT_TYPE_COUNT
} AlertType;

typedef struct Alert {
    AlertType type;
    int count;      /* Settlers concerned (0 for ALERT_NO_FOOD). */
    EntityId first; /* The first of them in slot order, ENTITY_NONE if none in particular. */
} Alert;

typedef struct Alerts {
    Alert list[ALERT_TYPE_COUNT]; /* In AlertType order. */
    int count;
} Alerts;

/* The alerts of the game as it is now. Empty when nobody lives in it. */
void alerts_compute(const Game *game, Alerts *out);

/* Translation keys; NULL when out of range. The name takes {0}, the number of
 * settlers (tr_plural() picks the form); the description is a plain text. */
const char *alert_name_key(AlertType type);
const char *alert_description_key(AlertType type);

#endif /* COLONY_CORE_ALERT_H */
