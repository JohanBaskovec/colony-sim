#include "core/alert.h"

#include "core/gather_zone.h"
#include "core/i18n.h"

/* Food in the colony lasts less than this many days: an alert. */
#define ALERT_FOOD_DAYS 1

/* Counts a settler for an alert, keeping the first one. */
static void count_for(Alert *alert, EntityId id)
{
    if (alert->count == 0) {
        alert->first = id;
    }
    alert->count++;
}

/* Whether the food in store lasts less than ALERT_FOOD_DAYS. */
static bool food_runs_short(const Game *game)
{
    ItemTypeMask food = 0;
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        if (item_is_food((ItemType)type)) {
            food |= ITEM_TYPE_BIT(type);
        }
    }
    return gather_food_days(food, game->stock, game_daily_food(game)) < ALERT_FOOD_DAYS;
}

void alerts_compute(const Game *game, Alerts *out)
{
    Alert found[ALERT_TYPE_COUNT];
    for (int type = 0; type < ALERT_TYPE_COUNT; type++) {
        found[type] = (Alert){(AlertType)type, 0, ENTITY_NONE};
    }
    int settlers = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        const Colonist *colonist = game_colonist_const(game, id);
        if (colonist == NULL) {
            continue;
        }
        settlers++;
        if (health_in_danger(&colonist->health)) {
            count_for(&found[ALERT_IN_DANGER], id);
        }
        if (need_level(colonist->needs[NEED_FOOD]) == NEED_LEVEL_CRITICAL) {
            count_for(&found[ALERT_STARVING], id);
        }
        if (need_level(colonist->needs[NEED_WATER]) == NEED_LEVEL_CRITICAL) {
            count_for(&found[ALERT_THIRSTY], id);
        }
        if (colonist->needs[NEED_FOOD] < NEED_LOW_THRESHOLD &&
            colonist->search_failures[SEARCH_FOOD] > 0) {
            count_for(&found[ALERT_FOOD_OUT_OF_REACH], id);
        }
        if (colonist->needs[NEED_WATER] < NEED_LOW_THRESHOLD &&
            colonist->search_failures[SEARCH_WATER] > 0) {
            count_for(&found[ALERT_WATER_OUT_OF_REACH], id);
        }
        if (need_level(colonist->needs[NEED_REST]) == NEED_LEVEL_CRITICAL) {
            count_for(&found[ALERT_EXHAUSTED], id);
        }
        if (colonist->mood.break_type != MOOD_BREAK_NONE) {
            count_for(&found[ALERT_BREAKDOWN], id);
        }
        /* The bed of a settler is owned by its slot (worker id = slot + 1). */
        TilePos bed;
        if (!beds_owned(&game->beds, (uint8_t)(slot + 1), &bed)) {
            count_for(&found[ALERT_NO_BED], id);
        }
    }
    bool no_food = settlers > 0 && food_runs_short(game);

    /* Those that apply, in order. */
    out->count = 0;
    for (int type = 0; type < ALERT_TYPE_COUNT; type++) {
        if (found[type].count > 0 || (type == ALERT_NO_FOOD && no_food)) {
            out->list[out->count++] = found[type];
        }
    }
}

const char *alert_name_key(AlertType type)
{
    static const char *const KEYS[ALERT_TYPE_COUNT] = {
        [ALERT_IN_DANGER] = NP_("alert.in_danger"),
        [ALERT_STARVING] = NP_("alert.starving"),
        [ALERT_THIRSTY] = NP_("alert.thirsty"),
        [ALERT_EXHAUSTED] = NP_("alert.exhausted"),
        [ALERT_NO_FOOD] = NP_("alert.no_food"),
        [ALERT_BREAKDOWN] = NP_("alert.breakdown"),
        [ALERT_NO_BED] = NP_("alert.no_bed"),
        [ALERT_FOOD_OUT_OF_REACH] = NP_("alert.food_out_of_reach"),
        [ALERT_WATER_OUT_OF_REACH] = NP_("alert.water_out_of_reach"),
    };
    return (unsigned)type < ALERT_TYPE_COUNT ? KEYS[type] : NULL;
}

const char *alert_description_key(AlertType type)
{
    static const char *const KEYS[ALERT_TYPE_COUNT] = {
        [ALERT_IN_DANGER] = N_("alert.in_danger.description"),
        [ALERT_STARVING] = N_("alert.starving.description"),
        [ALERT_THIRSTY] = N_("alert.thirsty.description"),
        [ALERT_EXHAUSTED] = N_("alert.exhausted.description"),
        [ALERT_NO_FOOD] = N_("alert.no_food.description"),
        [ALERT_BREAKDOWN] = N_("alert.breakdown.description"),
        [ALERT_NO_BED] = N_("alert.no_bed.description"),
        [ALERT_FOOD_OUT_OF_REACH] = N_("alert.food_out_of_reach.description"),
        [ALERT_WATER_OUT_OF_REACH] = N_("alert.water_out_of_reach.description"),
    };
    return (unsigned)type < ALERT_TYPE_COUNT ? KEYS[type] : NULL;
}
