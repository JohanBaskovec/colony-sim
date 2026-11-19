#include "core/appearance.h"
#include "core/game.h"

#include "test_framework.h"

#include <string.h>

#define ROLLS 4000

static void test_rolls_are_valid_and_cover_every_variant(void)
{
    Rng rng;
    rng_seed(&rng, 99u, 1u);
    int skins[APPEARANCE_SKIN_COUNT] = {0};
    int bodies[APPEARANCE_BODY_COUNT] = {0};
    int hair_styles[APPEARANCE_HAIR_STYLE_COUNT] = {0};
    int beards[APPEARANCE_BEARD_COUNT] = {0};
    int colors[HAIR_COLOR_COUNT] = {0};
    for (int i = 0; i < ROLLS; i++) {
        ColonistAppearance looks = appearance_roll(&rng, 18 + i % 50);
        REQUIRE(appearance_is_valid(looks));
        skins[looks.skin]++;
        bodies[looks.body]++;
        hair_styles[looks.hair_style]++;
        beards[looks.beard]++;
        colors[looks.hair_color]++;
    }
    for (int i = 0; i < APPEARANCE_SKIN_COUNT; i++) {
        CHECK(skins[i] > 0);
    }
    for (int i = 0; i < APPEARANCE_BODY_COUNT; i++) {
        CHECK(bodies[i] > 0);
    }
    for (int i = 0; i < APPEARANCE_HAIR_STYLE_COUNT; i++) {
        CHECK(hair_styles[i] > 0);
    }
    for (int i = 0; i < APPEARANCE_BEARD_COUNT; i++) {
        CHECK(beards[i] > 0);
    }
    for (int i = 0; i < HAIR_COLOR_COUNT; i++) {
        CHECK(colors[i] > 0);
    }
    /* Rates close to the configured chances. */
    float bald = (float)hair_styles[0] / ROLLS;
    float bearded = 1.0f - (float)beards[0] / ROLLS;
    CHECK_NEAR(bald, APPEARANCE_BALD_CHANCE, 0.03);
    CHECK_NEAR(bearded, APPEARANCE_BEARD_CHANCE, 0.04);
}

static void test_gray_hair_comes_with_age(void)
{
    Rng rng;
    rng_seed(&rng, 7u, 3u);
    int young_gray = 0, old_gray = 0;
    for (int i = 0; i < ROLLS; i++) {
        young_gray +=
            appearance_roll(&rng, APPEARANCE_GRAYING_AGE - 1).hair_color == HAIR_COLOR_GRAY;
        old_gray += appearance_roll(&rng, APPEARANCE_GRAY_AGE).hair_color == HAIR_COLOR_GRAY;
    }
    CHECK_EQ_INT(young_gray, 0);
    CHECK(old_gray > ROLLS / 2);
}

static void test_same_seed_same_looks(void)
{
    Rng a, b;
    rng_seed(&a, 1234u, 5u);
    rng_seed(&b, 1234u, 5u);
    for (int i = 0; i < 100; i++) {
        ColonistAppearance x = appearance_roll(&a, 30);
        ColonistAppearance y = appearance_roll(&b, 30);
        CHECK(memcmp(&x, &y, sizeof(x)) == 0);
    }
}

static void test_validation_rejects_out_of_range(void)
{
    ColonistAppearance looks = {0};
    CHECK(appearance_is_valid(looks));
    looks.skin = APPEARANCE_SKIN_COUNT;
    CHECK(!appearance_is_valid(looks));
    looks = (ColonistAppearance){.hair_color = HAIR_COLOR_COUNT};
    CHECK(!appearance_is_valid(looks));
    looks = (ColonistAppearance){.beard = APPEARANCE_BEARD_COUNT};
    CHECK(!appearance_is_valid(looks));
}

static void test_expression_follows_mood(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Test", 30, 0, 0, 0);
    colonist.mood.value = COLONIST_SMILING_FROM;
    CHECK_EQ_INT(colonist_expression(&colonist), EXPRESSION_SMILING);
    colonist.mood.value = MOOD_NEUTRAL;
    CHECK_EQ_INT(colonist_expression(&colonist), EXPRESSION_NEUTRAL);
    colonist.mood.value = COLONIST_SAD_BELOW - 1.0f;
    CHECK_EQ_INT(colonist_expression(&colonist), EXPRESSION_SAD);
    colonist.mood.value = COLONIST_ANGRY_BELOW - 1.0f;
    CHECK_EQ_INT(colonist_expression(&colonist), EXPRESSION_ANGRY);
    /* Angry during a break, whatever the mood. */
    colonist.mood.value = MOOD_NEUTRAL;
    mood_start_break(&colonist.mood, MOOD_BREAK_SULK, TICKS_PER_HOUR);
    CHECK_EQ_INT(colonist_expression(&colonist), EXPRESSION_ANGRY);
    colonist_free(&colonist);
}

static bool init_game(Game *game, uint64_t seed)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = 64;
    params.world.height = 64;
    params.colonist_count = 6;
    return game_init(game, &params);
}

static void test_colonists_get_deterministic_looks(void)
{
    static Game a, b;
    REQUIRE(init_game(&a, 21));
    REQUIRE(init_game(&b, 21));
    REQUIRE(game_colonist_slot_count(&a) == game_colonist_slot_count(&b));
    int distinct = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(&a); slot++) {
        if (entity_id_is_none(game_colonist_id_at(&a, slot))) {
            continue;
        }
        ColonistAppearance x = a.colonists[slot].appearance;
        ColonistAppearance y = b.colonists[slot].appearance;
        CHECK(appearance_is_valid(x));
        CHECK(memcmp(&x, &y, sizeof(x)) == 0);
        ColonistAppearance first = a.colonists[0].appearance;
        distinct += memcmp(&x, &first, sizeof(x)) != 0;
    }
    CHECK(distinct > 0); /* Not everybody looks the same. */
    game_shutdown(&a);
    game_shutdown(&b);
}

int main(void)
{
    RUN_TEST(test_rolls_are_valid_and_cover_every_variant);
    RUN_TEST(test_gray_hair_comes_with_age);
    RUN_TEST(test_same_seed_same_looks);
    RUN_TEST(test_validation_rejects_out_of_range);
    RUN_TEST(test_expression_follows_mood);
    RUN_TEST(test_colonists_get_deterministic_looks);
    return test_summary();
}
