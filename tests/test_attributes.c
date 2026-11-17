/* Settler attributes: positions, their effects, words, and what they change in the game. */
#include "core/attributes.h"
#include "core/colonist.h"
#include "core/game.h"
#include "core/i18n.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <math.h>
#include <string.h>

/* 48 x 48 soil, no plant, no rock, no water, at noon. */
static bool init_flat_game(Game *game, uint64_t seed)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true; /* One level of ground, open air over it. */
    params.world.seed = seed;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN; /* Open air over it: no trees rising there. */
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) { /* The ground level. */
        map->terrain[i] = TERRAIN_SOIL;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map); /* The layers were written directly. */
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* A settler with one attribute at `position` and every other ordinary, doing
 * nothing in particular. */
static Colonist *spawn_with(Game *game, int x, int y, AttributeType type, int position)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist == NULL) {
        return NULL;
    }
    Attributes attributes;
    attributes_init(&attributes);
    if (!attributes_set(&attributes, type, position)) {
        return NULL;
    }
    colonist_set_attributes(colonist, &attributes);
    for (int work = 0; work < WORK_TYPE_COUNT; work++) {
        colonist_set_work_priority(colonist, (WorkType)work, WORK_PRIORITY_DISABLED);
    }
    for (int need = 0; need < NEED_COUNT; need++) {
        colonist->needs[need] = 1.0f;
    }
    return colonist;
}

static void test_ordinary_changes_nothing(void)
{
    Attributes attributes;
    attributes_init(&attributes);
    for (int type = 0; type < ATTRIBUTE_COUNT; type++) {
        CHECK_EQ_INT(attributes.position[type], 0);
        CHECK_NEAR(attribute_factor(&attributes, (AttributeType)type), 1.0, 1e-6);
        CHECK(attribute_word_key((AttributeType)type, 0) == NULL);
    }
    CHECK_NEAR(attribute_thought_weight(&attributes), 1.0, 1e-6);
    CHECK_NEAR(attribute_outlook_points(&attributes), 0.0, 1e-6);
    CHECK_NEAR(attribute_cold_shift(&attributes), 0.0, 1e-6);
    CHECK_NEAR(attribute_heat_shift(&attributes), 0.0, 1e-6);
}

static void test_effects_at_the_ends_of_the_range(void)
{
    Attributes high, low;
    attributes_init(&high);
    attributes_init(&low);
    for (int type = 0; type < ATTRIBUTE_COUNT; type++) {
        REQUIRE(attributes_set(&high, (AttributeType)type, ATTRIBUTE_POSITION_MAX));
        REQUIRE(attributes_set(&low, (AttributeType)type, -ATTRIBUTE_POSITION_MAX));
    }
    /* Factors double or halve: as far from ordinary either way. */
    static const AttributeType FACTORS[] = {ATTRIBUTE_WALKING, ATTRIBUTE_WORK_PACE,
                                            ATTRIBUTE_LEARNING, ATTRIBUTE_SLEEP,
                                            ATTRIBUTE_APPETITE};
    for (size_t i = 0; i < sizeof(FACTORS) / sizeof(FACTORS[0]); i++) {
        CHECK_NEAR(attribute_factor(&high, FACTORS[i]), 2.0, 1e-5);
        CHECK_NEAR(attribute_factor(&low, FACTORS[i]), 0.5, 1e-5);
    }
    CHECK_NEAR(attribute_thought_weight(&high), 1.5, 1e-5);
    CHECK_NEAR(attribute_thought_weight(&low), 0.6, 1e-5);
    CHECK_NEAR(attribute_outlook_points(&high), 15.0, 1e-5);
    CHECK_NEAR(attribute_outlook_points(&low), -15.0, 1e-5);
    CHECK_NEAR(attribute_cold_shift(&high), 5.0, 1e-5);
    CHECK_NEAR(attribute_cold_shift(&low), -5.0, 1e-5);
    CHECK_NEAR(attribute_heat_shift(&high), 5.0, 1e-5);
    CHECK_NEAR(attribute_heat_shift(&low), -5.0, 1e-5);

    /* In between, continuous: halfway up is x2^0.5. */
    Attributes half;
    attributes_init(&half);
    REQUIRE(attributes_set(&half, ATTRIBUTE_WALKING, ATTRIBUTE_POSITION_MAX / 2));
    CHECK_NEAR(attribute_factor(&half, ATTRIBUTE_WALKING), sqrtf(2.0f), 1e-4);
    /* Only the attributes that are factors have one. */
    CHECK_NEAR(attribute_factor(&high, ATTRIBUTE_OUTLOOK), 1.0, 1e-6);
}

static void test_setting_a_position_is_checked(void)
{
    Attributes attributes;
    attributes_init(&attributes);
    CHECK(attributes_set(&attributes, ATTRIBUTE_SLEEP, 250));
    CHECK_EQ_INT(attributes.position[ATTRIBUTE_SLEEP], 250);
    CHECK(!attributes_set(&attributes, ATTRIBUTE_SLEEP, ATTRIBUTE_POSITION_MAX + 1));
    CHECK(!attributes_set(&attributes, ATTRIBUTE_SLEEP, -ATTRIBUTE_POSITION_MAX - 1));
    CHECK(!attributes_set(&attributes, ATTRIBUTE_COUNT, 0));
    CHECK_EQ_INT(attributes.position[ATTRIBUTE_SLEEP], 250);
    CHECK_NEAR(attribute_position(&attributes, ATTRIBUTE_SLEEP), 0.25, 1e-6);
    CHECK_NEAR(attribute_position(&attributes, ATTRIBUTE_COUNT), 0.0, 1e-9);
}

static void test_bands_and_words(void)
{
    CHECK_EQ_INT(attribute_band(0), 0);
    CHECK_EQ_INT(attribute_band(139), 0);
    CHECK_EQ_INT(attribute_band(140), 1);
    CHECK_EQ_INT(attribute_band(259), 1);
    CHECK_EQ_INT(attribute_band(260), 2);
    CHECK_EQ_INT(attribute_band(489), 2);
    CHECK_EQ_INT(attribute_band(490), 3);
    CHECK_EQ_INT(attribute_band(769), 3);
    CHECK_EQ_INT(attribute_band(770), 4);
    CHECK_EQ_INT(attribute_band(1000), 4);
    /* The same on both sides. */
    for (int position = 0; position <= ATTRIBUTE_POSITION_MAX; position += 10) {
        CHECK_EQ_INT(attribute_band(-position), attribute_band(position));
    }
    /* The bands are even steps of the effect: x1.1, x1.2, x1.4, x1.7. */
    CHECK_NEAR(exp2f(0.14f), 1.1, 0.005);
    CHECK_NEAR(exp2f(0.26f), 1.2, 0.005);
    CHECK_NEAR(exp2f(0.49f), 1.4, 0.005);
    CHECK_NEAR(exp2f(0.77f), 1.7, 0.01);

    /* Every attribute has a word for each band and side, all different, and
     * a name and a description. */
    for (int type = 0; type < ATTRIBUTE_COUNT; type++) {
        CHECK(attribute_name_key((AttributeType)type) != NULL);
        CHECK(attribute_description_key((AttributeType)type) != NULL);
        const char *seen[2 * ATTRIBUTE_BANDS];
        int count = 0;
        for (int band = 1; band <= ATTRIBUTE_BANDS; band++) {
            int starts[ATTRIBUTE_BANDS] = ATTRIBUTE_BAND_STARTS;
            for (int side = -1; side <= 1; side += 2) {
                const char *key = attribute_word_key((AttributeType)type, side * starts[band - 1]);
                REQUIRE(key != NULL);
                for (int i = 0; i < count; i++) {
                    CHECK(strcmp(seen[i], key) != 0);
                }
                seen[count++] = key;
            }
        }
    }
    CHECK(attribute_name_key(ATTRIBUTE_COUNT) == NULL);
    CHECK(attribute_description_key(ATTRIBUTE_COUNT) == NULL);
    CHECK(attribute_word_key(ATTRIBUTE_COUNT, 900) == NULL);
    CHECK_EQ_INT(attribute_group(ATTRIBUTE_WALKING), ATTRIBUTE_GROUP_BODY);
    CHECK_EQ_INT(attribute_group(ATTRIBUTE_OUTLOOK), ATTRIBUTE_GROUP_MIND);
    CHECK_EQ_INT(attribute_group(ATTRIBUTE_TEMPERAMENT), ATTRIBUTE_GROUP_MIND);
}

static void test_roll_is_deterministic(void)
{
    Rng a, b, c;
    rng_seed(&a, 7, 1);
    rng_seed(&b, 7, 1);
    rng_seed(&c, 8, 1);
    Attributes first, second, other;
    attributes_roll(&first, &a);
    attributes_roll(&second, &b);
    attributes_roll(&other, &c);
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    CHECK(memcmp(&first, &other, sizeof(first)) != 0);
    CHECK(attributes_hash(&first, 1) == attributes_hash(&second, 1));
    CHECK(attributes_hash(&first, 1) != attributes_hash(&other, 1));
}

/* Most settlers are ordinary, the extremes are very rare: a bell curve. */
static void test_roll_is_a_bell_curve(void)
{
    Rng rng;
    rng_seed(&rng, 42, 3);
    enum { SAMPLES = 4000 };
    long sum = 0;
    double square = 0.0;
    int bands[ATTRIBUTE_BANDS + 1] = {0};
    int high = 0, low = 0, total = 0;
    int widest = 0;
    for (int sample = 0; sample < SAMPLES; sample++) {
        Attributes attributes;
        attributes_roll(&attributes, &rng);
        for (int type = 0; type < ATTRIBUTE_COUNT; type++) {
            int position = attributes.position[type];
            CHECK(position >= -ATTRIBUTE_POSITION_MAX && position <= ATTRIBUTE_POSITION_MAX);
            sum += position;
            square += (double)position * position;
            bands[attribute_band(position)]++;
            high += position > 0;
            low += position < 0;
            total++;
            widest = position > widest ? position : (-position > widest ? -position : widest);
        }
    }
    double mean = (double)sum / total;
    double deviation = sqrt(square / total - mean * mean);
    CHECK(fabs(mean) < 10.0); /* Centered on the ordinary settler. */
    CHECK_NEAR(deviation, ATTRIBUTE_ROLL_DEVIATION, 12.0);
    CHECK(abs(high - low) < total / 40); /* Symmetric. */
    /* Shares of the bands (35, 25, 29, 9 and 1 percent), within sampling noise. */
    CHECK_NEAR((double)bands[0] / total, 0.354, 0.01);
    CHECK_NEAR((double)bands[1] / total, 0.254, 0.01);
    CHECK_NEAR((double)bands[2] / total, 0.288, 0.01);
    CHECK_NEAR((double)bands[3] / total, 0.094, 0.008);
    CHECK_NEAR((double)bands[4] / total, 0.010, 0.004);
    CHECK(bands[0] > bands[2] && bands[2] > bands[3] && bands[3] > bands[4]);
    CHECK(widest <= ATTRIBUTE_POSITION_MAX);
}

static void test_outlook_and_temperament_set_the_mood(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    CHECK_NEAR(colonist.mood.value, MOOD_NEUTRAL, 1e-6);

    Attributes attributes;
    attributes_init(&attributes);
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_OUTLOOK, ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(&colonist, &attributes);
    CHECK_NEAR(colonist.mood.value, MOOD_NEUTRAL + ATTRIBUTE_OUTLOOK_RANGE, 1e-4);

    /* A good memory: the outlook is added to it. */
    mood_remember(&colonist.mood, THOUGHT_FIRESIDE_STORY);
    float story = thought_def(THOUGHT_FIRESIDE_STORY)->value;
    CHECK_NEAR(colonist.mood.value, MOOD_NEUTRAL + ATTRIBUTE_OUTLOOK_RANGE + story, 1e-4);

    /* The temperament scales what thoughts weigh, not the outlook. */
    attributes_init(&attributes);
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_TEMPERAMENT, ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(&colonist, &attributes);
    CHECK_NEAR(colonist.mood.value, MOOD_NEUTRAL + ATTRIBUTE_SENSITIVE_MAX * story, 1e-4);
    CHECK_NEAR(mood_thought_value(&colonist.mood, THOUGHT_FIRESIDE_STORY),
               ATTRIBUTE_SENSITIVE_MAX * story, 1e-4);
    CHECK_NEAR(mood_thought_peak(&colonist.mood, THOUGHT_FIRESIDE_STORY),
               ATTRIBUTE_SENSITIVE_MAX * story, 1e-4);

    attributes_init(&attributes);
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_TEMPERAMENT, -ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(&colonist, &attributes);
    CHECK_NEAR(colonist.mood.value, MOOD_NEUTRAL + ATTRIBUTE_STOIC_MIN * story, 1e-4);

    /* The sum of what each thought shows is what the mood is. */
    mood_set_states(&colonist.mood, THOUGHT_BIT(THOUGHT_STARVING));
    mood_advance(&colonist.mood, 2 * TICKS_PER_HOUR);
    float shown = MOOD_NEUTRAL;
    for (int type = 0; type < THOUGHT_COUNT; type++) {
        shown += mood_thought_value(&colonist.mood, (ThoughtType)type);
    }
    CHECK_NEAR(colonist.mood.value, shown, 1e-3);
    colonist_free(&colonist);
}

static void test_work_pace_and_learning(void)
{
    Colonist ordinary, quick, slow;
    colonist_init(&ordinary, "Ada", 30, 0, 0, 0);
    colonist_init(&quick, "Bo", 30, 0, 0, 0);
    colonist_init(&slow, "Cy", 30, 0, 0, 0);
    Colonist *all[] = {&ordinary, &quick, &slow};
    for (int i = 0; i < 3; i++) {
        REQUIRE(colonist_set_knowledge_level(all[i], KNOWLEDGE_FIRE, 40.0));
    }
    Attributes attributes;
    attributes_init(&attributes);
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_WORK_PACE, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_LEARNING, ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(&quick, &attributes);
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_WORK_PACE, -ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_LEARNING, -ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(&slow, &attributes);

    double base = colonist_work_speed(&ordinary, KNOWLEDGE_FIRE);
    CHECK_NEAR(colonist_work_speed(&quick, KNOWLEDGE_FIRE), 2.0 * base, 1e-5);
    CHECK_NEAR(colonist_work_speed(&slow, KNOWLEDGE_FIRE), 0.5 * base, 1e-5);
    CHECK_NEAR(colonist_work_speed(&quick, KNOWLEDGE_NONE),
               2.0 * colonist_work_speed(&ordinary, KNOWLEDGE_NONE), 1e-5);

    /* Learning, from any source, on top of the age. */
    uint64_t tick = 100;
    double gain =
        colonist_learn(&ordinary, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_DOING, 1.0, 1.0, 100.0, tick);
    double fast =
        colonist_learn(&quick, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_DOING, 1.0, 1.0, 100.0, tick);
    double slowly =
        colonist_learn(&slow, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_DOING, 1.0, 1.0, 100.0, tick);
    CHECK(gain > 0.0);
    CHECK_NEAR(fast, 2.0 * gain, 1e-6);
    CHECK_NEAR(slowly, 0.5 * gain, 1e-6);
    for (int i = 0; i < 3; i++) {
        colonist_free(all[i]);
    }
}

/* Ticks a settler hungry for the food 10 tiles away takes to get there. */
static int ticks_to_walk(int position)
{
    Game game;
    if (!init_flat_game(&game, 5)) {
        return -1;
    }
    items_add(&game.items, 30, 20, 0, item_stack_pristine(ITEM_BLACKBERRIES, 400));
    Colonist *colonist = spawn_with(&game, 20, 20, ATTRIBUTE_WALKING, position);
    int ticks = -1;
    if (colonist != NULL) {
        colonist->needs[NEED_FOOD] = 0.2f;
        for (int tick = 0; tick < 4 * TICKS_PER_SECOND * 10 && ticks < 0; tick++) {
            game_tick(&game);
            if (colonist->x == 30 && colonist->y == 20) {
                ticks = tick;
            }
        }
    }
    game_shutdown(&game);
    return ticks;
}

static void test_walking_speed(void)
{
    int ordinary = ticks_to_walk(0);
    int quick = ticks_to_walk(ATTRIBUTE_POSITION_MAX);
    int slow = ticks_to_walk(-ATTRIBUTE_POSITION_MAX);
    int a_little = ticks_to_walk(250);
    REQUIRE(ordinary > 0 && quick > 0 && slow > 0 && a_little > 0);
    /* 10 tiles at 3.5 a second (the settler goes at once: the search takes a tick). */
    CHECK(abs(ordinary - 10 * TICKS_PER_SECOND * 2 / 7) < 10);
    CHECK(abs(quick * 2 - ordinary) < 12);
    CHECK(abs(slow - ordinary * 2) < 12);
    /* Continuous: a little quicker is a little sooner. */
    CHECK(a_little < ordinary && a_little > quick);
}

/* Food need lost over `ticks` with nothing to eat in sight and no need to. */
static float food_lost(int position, int ticks)
{
    Game game;
    if (!init_flat_game(&game, 5)) {
        return -1.0f;
    }
    Colonist *colonist = spawn_with(&game, 20, 20, ATTRIBUTE_APPETITE, position);
    float lost = -1.0f;
    if (colonist != NULL) {
        colonist->needs[NEED_FOOD] = 0.9f;
        for (int tick = 0; tick < ticks; tick++) {
            colonist->needs[NEED_WATER] = 1.0f;
            game_tick(&game);
        }
        lost = 0.9f - colonist->needs[NEED_FOOD];
    }
    game_shutdown(&game);
    return lost;
}

static void test_appetite_sets_how_fast_hunger_comes(void)
{
    float ordinary = food_lost(0, 3000);
    float big = food_lost(ATTRIBUTE_POSITION_MAX, 3000);
    float small = food_lost(-ATTRIBUTE_POSITION_MAX, 3000);
    REQUIRE(ordinary > 0.0f);
    CHECK_NEAR(ordinary, 3000.0f * NEED_FOOD_DECAY_PER_TICK, 1e-4);
    CHECK_NEAR(big, 2.0f * ordinary, 2e-4);
    CHECK_NEAR(small, 0.5f * ordinary, 2e-4);
}

/* Rest regained over `ticks` of sleep on the ground at night. */
static float rest_gained(int position, int ticks)
{
    Game game;
    if (!init_flat_game(&game, 5)) {
        return -1.0f;
    }
    game.clock.tick = 23 * TICKS_PER_HOUR;
    Colonist *colonist = spawn_with(&game, 20, 20, ATTRIBUTE_SLEEP, position);
    float gained = -1.0f;
    if (colonist != NULL) {
        colonist->needs[NEED_REST] = 0.1f;
        game_tick(&game);
        if (colonist->task == TASK_SLEEP) {
            float before = colonist->needs[NEED_REST];
            for (int tick = 0; tick < ticks; tick++) {
                colonist->needs[NEED_FOOD] = colonist->needs[NEED_WATER] = 1.0f;
                game_tick(&game);
            }
            gained = colonist->needs[NEED_REST] - before;
        }
    }
    game_shutdown(&game);
    return gained;
}

static void test_sleep_sets_the_rest_regained(void)
{
    float ordinary = rest_gained(0, 1500);
    float sound = rest_gained(ATTRIBUTE_POSITION_MAX, 1500);
    float poor = rest_gained(-ATTRIBUTE_POSITION_MAX, 1500);
    REQUIRE(ordinary > 0.0f && sound > 0.0f && poor > 0.0f);
    CHECK_NEAR(sound, 2.0f * ordinary, 0.003);
    CHECK_NEAR(poor, 0.5f * ordinary, 0.003);
}

static void test_new_settlers_differ_and_follow_the_seed(void)
{
    Game first, second, other;
    REQUIRE(init_flat_game(&first, 11));
    REQUIRE(init_flat_game(&second, 11));
    REQUIRE(init_flat_game(&other, 12));
    Attributes seen[8];
    int different = 0;
    for (int i = 0; i < 8; i++) {
        Colonist *a = game_colonist(&first, game_spawn_colonist(&first, 10 + i, 10, 0));
        Colonist *b = game_colonist(&second, game_spawn_colonist(&second, 10 + i, 10, 0));
        Colonist *c = game_colonist(&other, game_spawn_colonist(&other, 10 + i, 10, 0));
        REQUIRE(a != NULL && b != NULL && c != NULL);
        CHECK(memcmp(&a->attributes, &b->attributes, sizeof(Attributes)) == 0);
        seen[i] = a->attributes;
        for (int j = 0; j < i; j++) {
            different += memcmp(&seen[j], &seen[i], sizeof(Attributes)) != 0;
        }
        /* Each settler's mood has the character that follows. */
        CHECK_NEAR(a->mood.outlook, attribute_outlook_points(&a->attributes), 1e-6);
        CHECK_NEAR(a->mood.sensitivity, attribute_thought_weight(&a->attributes), 1e-6);
    }
    CHECK_EQ_INT(different, 8 * 7 / 2);
    CHECK(game_hash(&first) == game_hash(&second));
    CHECK(game_hash(&first) != game_hash(&other));
    game_shutdown(&first);
    game_shutdown(&second);
    game_shutdown(&other);
}

/* The state hash sees an attribute change. */
static void test_hash_sees_attributes(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 5));
    Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 10, 10, 0));
    REQUIRE(colonist != NULL);
    uint64_t before = game_hash(&game);
    Attributes attributes = colonist->attributes;
    attributes.position[ATTRIBUTE_WALKING] = (int16_t)(attributes.position[ATTRIBUTE_WALKING] + 1);
    colonist->attributes = attributes;
    CHECK(game_hash(&game) != before);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_ordinary_changes_nothing);
    RUN_TEST(test_effects_at_the_ends_of_the_range);
    RUN_TEST(test_setting_a_position_is_checked);
    RUN_TEST(test_bands_and_words);
    RUN_TEST(test_roll_is_deterministic);
    RUN_TEST(test_roll_is_a_bell_curve);
    RUN_TEST(test_outlook_and_temperament_set_the_mood);
    RUN_TEST(test_work_pace_and_learning);
    RUN_TEST(test_walking_speed);
    RUN_TEST(test_appetite_sets_how_fast_hunger_comes);
    RUN_TEST(test_sleep_sets_the_rest_regained);
    RUN_TEST(test_new_settlers_differ_and_follow_the_seed);
    RUN_TEST(test_hash_sees_attributes);
    return test_summary();
}
