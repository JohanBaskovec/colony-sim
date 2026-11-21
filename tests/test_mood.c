/* Moods: thoughts, the mood they lead to, its effects and breaks. */
#include "core/game.h"
#include "core/i18n.h"
#include "core/mood.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <stdlib.h>
#include <string.h>

/* 48 x 48 soil, no plant, no rock, no water, at noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true; /* One level of ground, open air over it. */
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN; /* Open air over it: no trees rising there. */
        map->water[i] = 0;
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) { /* The ground level. */
        map->terrain[i] = TERRAIN_SOIL;
        map->water[i] = 0;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map); /* The layers were written directly. */
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

static Colonist *spawn_rested(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist != NULL) {
        make_ordinary(colonist);
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
    }
    return colonist;
}

static void run_ticks(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

/* Whether a notice of `type` was raised. */
static bool noticed(const Game *game, NoticeType type)
{
    for (uint32_t number = 0; number < game->notices.count; number++) {
        const Notice *notice = notices_get(&game->notices, number);
        if (notice != NULL && notice->type == type) {
            return true;
        }
    }
    return false;
}

static void test_definitions(void)
{
    for (int type = 0; type < THOUGHT_COUNT; type++) {
        const ThoughtDef *def = thought_def((ThoughtType)type);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
        CHECK(i18n_is_valid_key(def->description_key));
        CHECK(strcmp(def->description_key, def->name_key) != 0);
        CHECK(def->value != 0.0f);
        CHECK(def->half_life_hours > 0.0f && def->plateau_hours >= 0.0f);
        CHECK(def->max_stack >= 1 && def->grow_max >= 1.0f);
        if (def->state) {
            CHECK_NEAR(def->plateau_hours, 0.0, 1e-9);
            CHECK_EQ_INT(def->max_stack, 1);
        }
    }
    CHECK(thought_def(THOUGHT_COUNT) == NULL);
    CHECK(thought_def(THOUGHT_HUNGRY)->state);
    CHECK(!thought_def(THOUGHT_SLEPT_ON_GROUND)->state);
    for (int kind = MOOD_BREAK_NONE + 1; kind < MOOD_BREAK_COUNT; kind++) {
        CHECK(i18n_is_valid_key(mood_break_key((MoodBreak)kind)));
    }
    CHECK(mood_break_key(MOOD_BREAK_NONE) == NULL);
    for (int value = 0; value <= 100; value += 5) {
        CHECK(i18n_is_valid_key(mood_level_key((float)value)));
    }
}

static void test_memories(void)
{
    Mood mood;
    mood_init(&mood);
    CHECK_NEAR(mood.value, MOOD_NEUTRAL, 1e-6);
    /* A memory weighs its full value at once; states are not remembered. */
    mood_remember(&mood, THOUGHT_FIRESIDE_STORY);
    mood_remember(&mood, THOUGHT_COLD);
    const ThoughtDef *story = thought_def(THOUGHT_FIRESIDE_STORY);
    CHECK(mood_has(&mood, THOUGHT_FIRESIDE_STORY));
    CHECK(!mood_has(&mood, THOUGHT_COLD));
    CHECK_NEAR(mood.value, MOOD_NEUTRAL + story->value, 1e-5);
    /* Full for its plateau... */
    int plateau = (int)(story->plateau_hours * (float)TICKS_PER_HOUR);
    mood_advance(&mood, plateau);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_FIRESIDE_STORY), story->value, 1e-5);
    CHECK(!mood_fading(&mood, THOUGHT_FIRESIDE_STORY));
    /* ...then halving every half-life, the past plateau ticks only. */
    int half_life = (int)(story->half_life_hours * (float)TICKS_PER_HOUR);
    mood_advance(&mood, half_life);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_FIRESIDE_STORY), story->value / 2.0f, 1e-3);
    CHECK(mood_fading(&mood, THOUGHT_FIRESIDE_STORY));
    CHECK_NEAR(mood.peak[THOUGHT_FIRESIDE_STORY], story->value, 1e-5);
    CHECK_NEAR(mood.value, MOOD_NEUTRAL + story->value / 2.0f, 1e-3);
    /* Coming back: renewed to its full value, the plateau starts over. */
    mood_remember(&mood, THOUGHT_FIRESIDE_STORY);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_FIRESIDE_STORY), story->value, 1e-5);
    CHECK(!mood_fading(&mood, THOUGHT_FIRESIDE_STORY));
    /* Same thing across a plateau that ends within one step. */
    mood_advance(&mood, plateau - 10);
    mood_advance(&mood, 10 + half_life);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_FIRESIDE_STORY), story->value / 2.0f, 1e-3);
    /* In the end it is forgotten, and the mood is neutral again. */
    mood_advance(&mood, 10 * half_life);
    CHECK(!mood_has(&mood, THOUGHT_FIRESIDE_STORY));
    CHECK_NEAR(mood.peak[THOUGHT_FIRESIDE_STORY], 0.0, 1e-9);
    CHECK_NEAR(mood.value, MOOD_NEUTRAL, 1e-6);
}

static void test_memories_add_up(void)
{
    Mood mood;
    mood_init(&mood);
    /* Chats add up to their stack. */
    const ThoughtDef *chat = thought_def(THOUGHT_CHAT);
    REQUIRE(chat->max_stack > 1);
    mood_remember(&mood, THOUGHT_CHAT);
    mood_remember(&mood, THOUGHT_CHAT);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_CHAT), 2.0f * chat->value, 1e-5);
    for (int i = 0; i < chat->max_stack + 2; i++) {
        mood_remember(&mood, THOUGHT_CHAT);
    }
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_CHAT), chat->value * (float)chat->max_stack, 1e-5);
    /* A faded chat adds to what is left of it. */
    int fade = (int)((chat->plateau_hours + chat->half_life_hours) * (float)TICKS_PER_HOUR);
    mood_advance(&mood, fade);
    float left = mood_thought_value(&mood, THOUGHT_CHAT);
    CHECK_NEAR(left, chat->value * (float)chat->max_stack / 2.0f, 1e-3);
    mood_remember(&mood, THOUGHT_CHAT);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_CHAT), left + chat->value, 1e-4);
    /* A renewed memory does not add up. */
    mood_remember(&mood, THOUGHT_SLEPT_COLD);
    mood_remember(&mood, THOUGHT_SLEPT_COLD);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_SLEPT_COLD),
               thought_def(THOUGHT_SLEPT_COLD)->value, 1e-5);
    /* The mood stays within its range. */
    mood_remember(&mood, THOUGHT_DEBUG_GLOOM);
    mood_remember(&mood, THOUGHT_SLEPT_ON_GROUND);
    mood_remember(&mood, THOUGHT_SPOILED_FOOD);
    mood_remember(&mood, THOUGHT_SLEPT_WET);
    CHECK_NEAR(mood.value, MOOD_MIN, 1e-6);
}

static void test_states(void)
{
    Mood mood;
    mood_init(&mood);
    /* Only state bits are kept. */
    mood_set_states(&mood, THOUGHT_BIT(THOUGHT_COLD) | THOUGHT_BIT(THOUGHT_SLEPT_WELL));
    CHECK(mood_has(&mood, THOUGHT_COLD));
    CHECK(!mood_has(&mood, THOUGHT_SLEPT_WELL));
    /* A state sets in over MOOD_STATE_RISE_HOURS. */
    const ThoughtDef *cold = thought_def(THOUGHT_COLD);
    int rise = (int)(MOOD_STATE_RISE_HOURS * (float)TICKS_PER_HOUR);
    mood_advance(&mood, rise / 2);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_COLD), cold->value / 2.0f, 1e-3);
    CHECK(!mood_fading(&mood, THOUGHT_COLD));
    mood_advance(&mood, rise);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_COLD), cold->value, 1e-5);
    CHECK_NEAR(mood.value, MOOD_NEUTRAL + cold->value, 1e-5);
    /* Over: it fades, halving every half-life. */
    mood_set_states(&mood, 0);
    CHECK(mood_has(&mood, THOUGHT_COLD));
    CHECK(mood_fading(&mood, THOUGHT_COLD));
    mood_advance(&mood, (int)(cold->half_life_hours * (float)TICKS_PER_HOUR));
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_COLD), cold->value / 2.0f, 1e-3);
    /* Back on: it sets in again from what was left. */
    mood_set_states(&mood, THOUGHT_BIT(THOUGHT_COLD));
    CHECK(!mood_fading(&mood, THOUGHT_COLD));
    mood_advance(&mood, rise / 2);
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_COLD), cold->value, 1e-3);
    CHECK_EQ_INT(mood.age[THOUGHT_COLD], rise / 2);
    mood_set_states(&mood, 0);
    mood_advance(&mood, 20 * TICKS_PER_HOUR);
    CHECK(!mood_has(&mood, THOUGHT_COLD));
    CHECK_NEAR(mood.value, MOOD_NEUTRAL, 1e-6);
}

/* Hunger weighs more and more the longer it lasts. */
static void test_hunger_grows_worse(void)
{
    const ThoughtDef *hungry = thought_def(THOUGHT_HUNGRY);
    REQUIRE(hungry->grow_max > 1.0f);
    int grow = (int)(hungry->grow_hours * (float)TICKS_PER_HOUR);
    CHECK_NEAR(mood_state_value(THOUGHT_HUNGRY, 0), hungry->value, 1e-6);
    CHECK_NEAR(mood_state_value(THOUGHT_HUNGRY, grow / 2),
               hungry->value * (1.0f + hungry->grow_max) / 2.0f, 1e-4);
    CHECK_NEAR(mood_state_value(THOUGHT_HUNGRY, grow), hungry->value * hungry->grow_max, 1e-5);
    CHECK_NEAR(mood_state_value(THOUGHT_HUNGRY, 10 * grow), hungry->value * hungry->grow_max, 1e-5);
    CHECK_NEAR(mood_state_value(THOUGHT_COLD, 1000000), thought_def(THOUGHT_COLD)->value, 1e-6);

    Mood mood;
    mood_init(&mood);
    mood_set_states(&mood, THOUGHT_BIT(THOUGHT_HUNGRY));
    float before = 0.0f;
    int hours = (int)hungry->grow_hours; /* Worse every hour until it stops growing. */
    REQUIRE(hours >= 2);
    for (int hour = 1; hour <= hours; hour++) {
        for (int step = 0; step < TICKS_PER_HOUR / MOOD_UPDATE_TICKS; step++) {
            mood_advance(&mood, MOOD_UPDATE_TICKS);
        }
        float now = mood_thought_value(&mood, THOUGHT_HUNGRY);
        CHECK(now < before);
        before = now;
    }
    CHECK_NEAR(before, mood_state_value(THOUGHT_HUNGRY, hours * TICKS_PER_HOUR), 1e-3);
    CHECK_NEAR(mood.peak[THOUGHT_HUNGRY], before, 1e-6);
    /* Fed: it fades from the worst it got to. */
    mood_set_states(&mood, 0);
    mood_advance(&mood, (int)(hungry->half_life_hours * (float)TICKS_PER_HOUR));
    CHECK_NEAR(mood_thought_value(&mood, THOUGHT_HUNGRY), before / 2.0f, 1e-3);
    CHECK_NEAR(mood.peak[THOUGHT_HUNGRY], before, 1e-6);
}

static void test_meals(void)
{
    Mood mood;
    mood_init(&mood);
    /* The same food again and again. */
    for (int i = 0; i < MOOD_MEALS - 1; i++) {
        mood_meal(&mood, ITEM_BLACKBERRIES, 100);
    }
    CHECK(!mood_has(&mood, THOUGHT_SAME_FOOD));
    mood_meal(&mood, ITEM_BLACKBERRIES, 100);
    CHECK(mood_has(&mood, THOUGHT_SAME_FOOD));
    CHECK(!mood_has(&mood, THOUGHT_COOKED_MEAL));
    /* Something else: not monotonous any more after a varied diet. */
    mood_meal(&mood, ITEM_HAZELNUTS, 100);
    CHECK(!mood_has(&mood, THOUGHT_VARIED_MEALS));
    mood_meal(&mood, ITEM_ROASTED_CARROTS, 100);
    CHECK(mood_has(&mood, THOUGHT_VARIED_MEALS));
    CHECK(!mood_has(&mood, THOUGHT_SAME_FOOD));
    CHECK(mood_has(&mood, THOUGHT_COOKED_MEAL));
    CHECK(mood_food_cooked(ITEM_ROASTED_RHIZOMES) && !mood_food_cooked(ITEM_CATTAIL_RHIZOMES));
    /* Spoiled food. */
    CHECK(!mood_has(&mood, THOUGHT_SPOILED_FOOD));
    mood_meal(&mood, ITEM_MUSHROOMS, MOOD_SPOILED_PERCENT - 1);
    CHECK(mood_has(&mood, THOUGHT_SPOILED_FOOD));
    /* Not food: no meal. */
    int count = mood.meal_count;
    mood_meal(&mood, ITEM_TWIGS, 100);
    CHECK_EQ_INT(mood.meal_count, count);
}

static void test_effects(void)
{
    CHECK_NEAR(mood_work_factor(MOOD_NEUTRAL), 1.0, 1e-6);
    CHECK_NEAR(mood_work_factor(MOOD_MAX), 1.0f + MOOD_WORK_SPAN, 1e-6);
    CHECK_NEAR(mood_work_factor(MOOD_MIN), 1.0f - MOOD_WORK_SPAN, 1e-6);
    CHECK_NEAR(mood_work_factor(75.0f), 1.0f + MOOD_WORK_SPAN / 2.0f, 1e-6);
    CHECK_NEAR(mood_level_offset(MOOD_MAX), MOOD_LEVEL_SPAN, 1e-6);
    CHECK_NEAR(mood_level_offset(MOOD_MIN), -MOOD_LEVEL_SPAN, 1e-6);
    CHECK_NEAR(mood_level_offset(MOOD_NEUTRAL), 0.0, 1e-9);
    CHECK_NEAR(mood_learn_factor(MOOD_MAX), 1.0 + MOOD_LEARN_SPAN, 1e-6);
    CHECK_NEAR(mood_learn_factor(MOOD_MIN), 1.0 - MOOD_LEARN_SPAN, 1e-6);
    /* Out of range values are clamped. */
    CHECK_NEAR(mood_work_factor(500.0f), 1.0f + MOOD_WORK_SPAN, 1e-6);
}

static void test_breaks(void)
{
    CHECK_NEAR(mood_break_chance(MOOD_MINOR_BREAK_BELOW), 0.0, 1e-9);
    CHECK_NEAR(mood_break_chance(MOOD_NEUTRAL), 0.0, 1e-9);
    CHECK_NEAR(mood_break_chance(MOOD_MIN), MOOD_BREAK_CHANCE_PER_HOUR, 1e-6);
    CHECK(mood_break_chance(5.0f) > mood_break_chance(20.0f));
    CHECK_EQ_INT(mood_break_kind(20.0f, 0.1f), MOOD_BREAK_SULK);
    CHECK_EQ_INT(mood_break_kind(5.0f, 0.1f), MOOD_BREAK_WANDER_OFF);
    CHECK_EQ_INT(mood_break_kind(5.0f, 0.9f), MOOD_BREAK_BINGE);

    Mood mood;
    mood_init(&mood);
    CHECK(mood_can_break(&mood));
    mood_start_break(&mood, MOOD_BREAK_BINGE, TICKS_PER_HOUR);
    CHECK_EQ_INT(mood.break_type, MOOD_BREAK_BINGE);
    CHECK(mood.break_started);
    CHECK_NEAR(mood.binge_left, MOOD_BINGE_NUTRITION, 1e-6);
    CHECK(!mood_can_break(&mood));
    CHECK(!mood_break_tick(&mood, TICKS_PER_HOUR - 1));
    CHECK(mood_break_tick(&mood, 1));
    /* Relief, and a while before the next one. */
    CHECK_EQ_INT(mood.break_type, MOOD_BREAK_NONE);
    CHECK(mood_has(&mood, THOUGHT_RELIEF));
    CHECK(!mood_can_break(&mood));
    mood_break_tick(&mood, MOOD_BREAK_COOLDOWN_HOURS * TICKS_PER_HOUR);
    CHECK(mood_can_break(&mood));
    /* Invalid breaks are ignored. */
    mood_start_break(&mood, MOOD_BREAK_NONE, 100);
    mood_start_break(&mood, MOOD_BREAK_SULK, 0);
    CHECK_EQ_INT(mood.break_type, MOOD_BREAK_NONE);
}

static void test_hash(void)
{
    Mood a, b;
    mood_init(&a);
    mood_init(&b);
    CHECK(mood_hash(&a, 1) == mood_hash(&b, 1));
    mood_remember(&b, THOUGHT_CHAT);
    CHECK(mood_hash(&a, 1) != mood_hash(&b, 1));
}

/* Mood speeds work up or slows it down, and changes what is made. */
static void test_mood_changes_work(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Test", 30, 0, 0, 0);
    colonist_set_knowledge_level(&colonist, KNOWLEDGE_GATHERING, 50.0);
    float neutral = colonist_work_speed(&colonist, KNOWLEDGE_GATHERING);
    CHECK_NEAR(colonist_work_level(&colonist, KNOWLEDGE_GATHERING), 50.0, 1e-9);
    colonist.mood.value = MOOD_MAX;
    CHECK_NEAR(colonist_work_speed(&colonist, KNOWLEDGE_GATHERING),
               neutral * (1.0f + MOOD_WORK_SPAN), 1e-5);
    CHECK_NEAR(colonist_work_level(&colonist, KNOWLEDGE_GATHERING), 50.0 + MOOD_LEVEL_SPAN, 1e-6);
    colonist.mood.value = MOOD_MIN;
    CHECK_NEAR(colonist_work_speed(&colonist, KNOWLEDGE_GATHERING),
               neutral * (1.0f - MOOD_WORK_SPAN), 1e-5);
    /* Within the valid levels. */
    colonist_set_knowledge_level(&colonist, KNOWLEDGE_GATHERING, 2.0);
    CHECK_NEAR(colonist_work_level(&colonist, KNOWLEDGE_GATHERING), KNOWLEDGE_LEVEL_MIN, 1e-9);
    /* Learning: slower when unhappy. */
    Colonist happy;
    colonist_init(&happy, "Happy", 30, 0, 0, 0);
    colonist_set_knowledge_level(&happy, KNOWLEDGE_GATHERING, 2.0);
    happy.mood.value = MOOD_MAX;
    colonist_learn_by_doing(&happy, KNOWLEDGE_GATHERING, TICKS_PER_HOUR, 0);
    colonist_learn_by_doing(&colonist, KNOWLEDGE_GATHERING, TICKS_PER_HOUR, 0);
    CHECK(colonist_knowledge_level(&happy, KNOWLEDGE_GATHERING) >
          colonist_knowledge_level(&colonist, KNOWLEDGE_GATHERING));
    colonist_free(&happy);
    colonist_free(&colonist);
}

/* A night in a bed under a roof is a good memory; on the ground, a bad one. */
static void test_settlers_remember_their_nights(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_structure(&game.map, 13, 10, 0, STRUCTURE_GRASS_BED);
    map_set_structure(&game.map, 14, 10, 0, STRUCTURE_WALL); /* Holds the roof up. */
    map_set_roof(&game.map, 13, 10, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    game.weather.spell = WEATHER_SPELL_MAX; /* A mild night. */
    Colonist *sleeper = spawn_rested(&game, 12, 10);
    Colonist *other = spawn_rested(&game, 30, 30);
    REQUIRE(sleeper != NULL && other != NULL);
    sleeper->needs[NEED_REST] = 0.2f;
    other->needs[NEED_REST] = 0.2f;
    run_ticks(&game, 9 * TICKS_PER_HOUR);
    REQUIRE(sleeper->task != TASK_SLEEP && other->task != TASK_SLEEP);
    CHECK(mood_has(&sleeper->mood, THOUGHT_SLEPT_WELL));
    CHECK(!mood_has(&sleeper->mood, THOUGHT_SLEPT_WET));
    CHECK(mood_has(&other->mood, THOUGHT_SLEPT_ON_GROUND));
    CHECK(!mood_has(&other->mood, THOUGHT_SLEPT_WET)); /* A dry night: no roof needed. */
    CHECK(!mood_has(&other->mood, THOUGHT_SLEPT_WELL));
    CHECK(sleeper->mood.value > other->mood.value);
    game_shutdown(&game);
}

/* It is the rain that spoils a night, not the open sky: under a roof one
 * stays dry, in the open one is soaked by morning. */
static void test_a_night_in_the_rain(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_structure(&game.map, 13, 10, 0, STRUCTURE_GRASS_BED);
    map_set_structure(&game.map, 14, 10, 0, STRUCTURE_WALL); /* Holds the roof up. */
    map_set_roof(&game.map, 13, 10, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    Colonist *sheltered = spawn_rested(&game, 12, 10);
    Colonist *exposed = spawn_rested(&game, 30, 30);
    REQUIRE(sheltered != NULL && exposed != NULL);
    sheltered->needs[NEED_REST] = 0.2f;
    exposed->needs[NEED_REST] = 0.2f;
    for (int i = 0; i < 9 * TICKS_PER_HOUR; i++) {
        game.weather.type = WEATHER_RAIN;
        game.weather.spell = WEATHER_SPELL_MAX;
        run_ticks(&game, 1);
    }
    REQUIRE(sheltered->task != TASK_SLEEP && exposed->task != TASK_SLEEP);
    CHECK(mood_has(&exposed->mood, THOUGHT_SLEPT_WET));
    CHECK(!mood_has(&sheltered->mood, THOUGHT_SLEPT_WET));
    CHECK(!mood_has(&exposed->mood, THOUGHT_SLEPT_WELL));
    game_shutdown(&game);
}

/* Hunger and thirst make settlers miserable; in the end they break down. */
static void test_misery_leads_to_breaks(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.01f; /* Nothing to eat or drink here. */
    colonist->needs[NEED_WATER] = 0.01f;
    run_ticks(&game, MOOD_UPDATE_TICKS);
    CHECK(mood_has(&colonist->mood, THOUGHT_STARVING));
    CHECK(mood_has(&colonist->mood, THOUGHT_PARCHED));
    run_ticks(&game, 2 * TICKS_PER_HOUR);
    CHECK(colonist->mood.value < MOOD_NEUTRAL - 25.0f);
    int hours = 0;
    while (hours < 72 && !noticed(&game, NOTICE_BREAK_SULK) &&
           !noticed(&game, NOTICE_BREAK_WANDER_OFF) && !noticed(&game, NOTICE_BREAK_BINGE)) {
        run_ticks(&game, TICKS_PER_HOUR);
        hours++;
    }
    CHECK(noticed(&game, NOTICE_BREAK_SULK) || noticed(&game, NOTICE_BREAK_WANDER_OFF) ||
          noticed(&game, NOTICE_BREAK_BINGE));
    game_shutdown(&game);
}

/* A sulking settler does no work, then goes back to it. */
static void test_sulking_settler_does_no_work(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    for (int y = 18; y <= 22; y++) {
        for (int x = 18; x <= 22; x++) {
            Plant *bramble = &game.map.plants[map_index(&game.map, x, y, 0)];
            *bramble = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
            plant_settle(bramble, GAME_START_DAY); /* In fruit. */
        }
    }
    int zone = gather_zone_create(&game.gather_zones, &game.map, 18, 18, 22, 22, 0);
    gather_zone_set_enabled(&game.gather_zones, zone, GATHER_BLACKBERRIES, true);
    gather_zone_set_mode(&game.gather_zones, zone, GATHER_BLACKBERRIES, GATHER_FOREVER);
    gather_zone_set_group_mode(&game.gather_zones, zone, GATHER_GROUP_FOOD, GATHER_FOREVER);
    mood_start_break(&colonist->mood, MOOD_BREAK_SULK, 2 * TICKS_PER_HOUR);
    bool worked = false;
    for (int i = 0; i < TICKS_PER_HOUR; i++) {
        game_tick(&game);
        worked = worked || colonist->task == TASK_WORK;
    }
    CHECK(!worked);
    CHECK(noticed(&game, NOTICE_BREAK_SULK));
    CHECK(!colonist->mood.break_started);
    CHECK(colonist_in_break(colonist));
    CHECK_STR_EQ(colonist_activity_key(colonist), mood_break_key(MOOD_BREAK_SULK));
    run_ticks(&game, TICKS_PER_HOUR + 10);
    CHECK(!colonist_in_break(colonist));
    CHECK(mood_has(&colonist->mood, THOUGHT_RELIEF));
    for (int i = 0; i < TICKS_PER_HOUR && !worked; i++) {
        game_tick(&game);
        worked = colonist->task == TASK_WORK;
    }
    CHECK(worked);
    game_shutdown(&game);
}

/* A binge: far more food eaten than needed. */
static void test_binge_eats_the_colony_food(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    items_drop(&game.items, &game.map, 23, 20, 0, item_stack_pristine(ITEM_HAZELNUTS, 60));
    mood_start_break(&colonist->mood, MOOD_BREAK_BINGE, 6 * TICKS_PER_HOUR);
    run_ticks(&game, 2 * TICKS_PER_HOUR);
    long eaten = 60 - game.items.totals[ITEM_HAZELNUTS];
    float nutrition = item_def(ITEM_HAZELNUTS)->nutrition;
    CHECK((float)eaten * nutrition >= MOOD_BINGE_NUTRITION - nutrition);
    CHECK(noticed(&game, NOTICE_BREAK_BINGE));
    CHECK(!colonist_in_break(colonist)); /* Ate it all: over. */
    CHECK(mood_has(&colonist->mood, THOUGHT_SAME_FOOD) == false);
    game_shutdown(&game);
}

/* Going off alone: far away from where it was. */
static void test_settler_goes_off_alone(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 24, 24);
    REQUIRE(colonist != NULL);
    mood_start_break(&colonist->mood, MOOD_BREAK_WANDER_OFF, 6 * TICKS_PER_HOUR);
    run_ticks(&game, 2);
    CHECK_EQ_INT(colonist->task, TASK_WANDER);
    CHECK(colonist->mood.went_off);
    REQUIRE(colonist->path.count > 0);
    TilePos end = colonist->path.steps[colonist->path.count - 1];
    int near = (int)(MOOD_WANDER_OFF_MIN_COST / 2.0f);
    CHECK(abs(end.x - 24) >= near || abs(end.y - 24) >= near);
    game_shutdown(&game);
}

/* Meals: cooked food is a good memory. */
static void test_cooked_meal(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.2f;
    items_drop(&game.items, &game.map, 21, 20, 0, item_stack_pristine(ITEM_ROASTED_RHIZOMES, 10));
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK(colonist->needs[NEED_FOOD] > 0.5f);
    CHECK(mood_has(&colonist->mood, THOUGHT_COOKED_MEAL));
    CHECK_EQ_INT(colonist->mood.meal_count, 1);
    game_shutdown(&game);
}

/* The debug cheat: everyone miserable, for a while. */
static void test_debug_sadden(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->mood.break_cooldown = 100;
    game_debug_sadden(&game);
    CHECK(mood_has(&colonist->mood, THOUGHT_DEBUG_GLOOM));
    CHECK(colonist->mood.value < MOOD_MINOR_BREAK_BELOW);
    CHECK(mood_can_break(&colonist->mood));
    game_shutdown(&game);
}

/* Every thought has its sentence for the journal. */
static void test_journal_sentences(void)
{
    for (int type = 0; type < THOUGHT_COUNT; type++) {
        const char *key = thought_journal_key((ThoughtType)type);
        REQUIRE(key != NULL);
        CHECK(strncmp(key, "thought.", 8) == 0);
    }
    CHECK(thought_journal_key(THOUGHT_COUNT) == NULL);
}

int main(void)
{
    RUN_TEST(test_journal_sentences);
    RUN_TEST(test_definitions);
    RUN_TEST(test_memories);
    RUN_TEST(test_memories_add_up);
    RUN_TEST(test_states);
    RUN_TEST(test_hunger_grows_worse);
    RUN_TEST(test_meals);
    RUN_TEST(test_effects);
    RUN_TEST(test_breaks);
    RUN_TEST(test_hash);
    RUN_TEST(test_mood_changes_work);
    RUN_TEST(test_settlers_remember_their_nights);
    RUN_TEST(test_a_night_in_the_rain);
    RUN_TEST(test_misery_leads_to_breaks);
    RUN_TEST(test_sulking_settler_does_no_work);
    RUN_TEST(test_binge_eats_the_colony_food);
    RUN_TEST(test_settler_goes_off_alone);
    RUN_TEST(test_cooked_meal);
    RUN_TEST(test_debug_sadden);
    return test_summary();
}
