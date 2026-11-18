/* Alerts: what is wrong in the colony right now, found from its state. */
#include "core/alert.h"
#include "core/game.h"
#include "core/i18n.h"

#include "test_framework.h"

#include <string.h>

static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* A settler with every need met, and food for many days in the colony. */
static Colonist *spawn_fed(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
    }
    return colonist;
}

static const Alert *find(const Alerts *alerts, AlertType type)
{
    for (int i = 0; i < alerts->count; i++) {
        if (alerts->list[i].type == type) {
            return &alerts->list[i];
        }
    }
    return NULL;
}

/* Consciousness under 70 %: in danger. */
static void test_settlers_in_danger(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_BLACKBERRIES, 400));
    Colonist *a = spawn_fed(&game, 10, 10);
    Colonist *b = spawn_fed(&game, 11, 10);
    REQUIRE(a != NULL && b != NULL);
    Alerts alerts;
    alerts_compute(&game, &alerts);
    CHECK(find(&alerts, ALERT_IN_DANGER) == NULL);
    b->health.blood = 0.7f; /* 57 % consciousness. */
    health_update(&b->health, 1e-6f);
    a->health.blood = 0.82f; /* 91 %: nothing to fear. */
    health_update(&a->health, 1e-6f);
    alerts_compute(&game, &alerts);
    const Alert *danger = find(&alerts, ALERT_IN_DANGER);
    REQUIRE(danger != NULL);
    CHECK_EQ_INT(danger->count, 1);
    CHECK(entity_id_equal(danger->first, game_colonist_id_at(&game, 1)));
    CHECK_EQ_INT(alerts.list[0].type, ALERT_IN_DANGER); /* The first of them. */
    game_shutdown(&game);
}

static void test_nothing_to_report_without_settlers(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Alerts alerts;
    alerts_compute(&game, &alerts);
    CHECK_EQ_INT(alerts.count, 0); /* Nobody to starve: no food alert either. */
    game_shutdown(&game);
}

static void test_settlers_in_need_are_counted(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_BLACKBERRIES, 400));
    Colonist *a = spawn_fed(&game, 10, 10);
    Colonist *b = spawn_fed(&game, 11, 10);
    Colonist *c = spawn_fed(&game, 12, 10);
    REQUIRE(a != NULL && b != NULL && c != NULL);
    beds_assign(&game.beds, 1, 20, 20, 0);
    beds_assign(&game.beds, 2, 21, 20, 0);
    beds_assign(&game.beds, 3, 22, 20, 0);
    Alerts alerts;
    alerts_compute(&game, &alerts);
    CHECK_EQ_INT(alerts.count, 0);

    b->needs[NEED_FOOD] = NEED_CRITICAL_THRESHOLD * 0.5f;
    c->needs[NEED_FOOD] = NEED_CRITICAL_THRESHOLD * 0.5f;
    c->needs[NEED_WATER] = NEED_CRITICAL_THRESHOLD * 0.5f;
    a->needs[NEED_REST] = NEED_LOW_THRESHOLD * 0.9f; /* Low is not critical. */
    alerts_compute(&game, &alerts);
    const Alert *hungry = find(&alerts, ALERT_STARVING);
    const Alert *thirsty = find(&alerts, ALERT_THIRSTY);
    REQUIRE(hungry != NULL && thirsty != NULL);
    CHECK_EQ_INT(hungry->count, 2);
    CHECK_EQ_INT(thirsty->count, 1);
    CHECK(find(&alerts, ALERT_EXHAUSTED) == NULL);
    /* The first settler concerned, in slot order. */
    CHECK(entity_id_equal(hungry->first, game_colonist_id_at(&game, 1)));
    CHECK(entity_id_equal(thirsty->first, game_colonist_id_at(&game, 2)));
    CHECK_EQ_INT(alerts.count, 2);
    game_shutdown(&game);
}

static void test_alerts_come_in_a_fixed_order(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *a = spawn_fed(&game, 10, 10);
    REQUIRE(a != NULL);
    a->needs[NEED_REST] = 0.0f;
    a->needs[NEED_FOOD] = 0.0f;
    Alerts alerts;
    alerts_compute(&game, &alerts);
    for (int i = 1; i < alerts.count; i++) {
        CHECK(alerts.list[i - 1].type < alerts.list[i].type);
    }
    CHECK(find(&alerts, ALERT_STARVING) != NULL);
    CHECK(find(&alerts, ALERT_EXHAUSTED) != NULL);
    game_shutdown(&game);
}

static void test_no_food_in_the_colony(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *a = spawn_fed(&game, 10, 10);
    REQUIRE(a != NULL);
    Alerts alerts;
    alerts_compute(&game, &alerts);
    const Alert *none = find(&alerts, ALERT_NO_FOOD);
    REQUIRE(none != NULL);
    CHECK(entity_id_is_none(none->first)); /* Nobody in particular. */

    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_BLACKBERRIES, 400));
    alerts_compute(&game, &alerts);
    CHECK(find(&alerts, ALERT_NO_FOOD) == NULL);
    game_shutdown(&game);
}

static void test_a_breakdown_is_an_alert(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_BLACKBERRIES, 400));
    Colonist *a = spawn_fed(&game, 10, 10);
    REQUIRE(a != NULL);
    a->mood.break_type = MOOD_BREAK_SULK;
    a->mood.break_ticks = 100;
    Alerts alerts;
    alerts_compute(&game, &alerts);
    const Alert *breakdown = find(&alerts, ALERT_BREAKDOWN);
    REQUIRE(breakdown != NULL);
    CHECK_EQ_INT(breakdown->count, 1);
    CHECK(entity_id_equal(breakdown->first, game_colonist_id_at(&game, 0)));
    game_shutdown(&game);
}

static void test_settlers_without_a_bed(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_BLACKBERRIES, 400));
    REQUIRE(spawn_fed(&game, 10, 10) != NULL);
    REQUIRE(spawn_fed(&game, 11, 10) != NULL);
    Alerts alerts;
    alerts_compute(&game, &alerts);
    const Alert *no_bed = find(&alerts, ALERT_NO_BED);
    REQUIRE(no_bed != NULL);
    CHECK_EQ_INT(no_bed->count, 2);
    beds_assign(&game.beds, 1, 20, 20, 0);
    alerts_compute(&game, &alerts);
    no_bed = find(&alerts, ALERT_NO_BED);
    REQUIRE(no_bed != NULL);
    CHECK_EQ_INT(no_bed->count, 1);
    CHECK(entity_id_equal(no_bed->first, game_colonist_id_at(&game, 1)));
    game_shutdown(&game);
}

static void test_texts_exist(void)
{
    for (int type = 0; type < ALERT_TYPE_COUNT; type++) {
        CHECK(alert_name_key((AlertType)type) != NULL);
        CHECK(alert_description_key((AlertType)type) != NULL);
    }
    CHECK(alert_name_key(ALERT_TYPE_COUNT) == NULL);
}

int main(void)
{
    RUN_TEST(test_nothing_to_report_without_settlers);
    RUN_TEST(test_settlers_in_need_are_counted);
    RUN_TEST(test_settlers_in_danger);
    RUN_TEST(test_alerts_come_in_a_fixed_order);
    RUN_TEST(test_no_food_in_the_colony);
    RUN_TEST(test_a_breakdown_is_an_alert);
    RUN_TEST(test_settlers_without_a_bed);
    RUN_TEST(test_texts_exist);
    return test_summary();
}
