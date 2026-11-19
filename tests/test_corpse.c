/* The dead (core/corpse.h): the record kept of a settler that died, and the
 * body that points at it (ItemStack.corpse). */
#include "core/game.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <string.h>

/* A small game on flat soil with no starting settlers, at noon. */
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
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN;
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map);
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* An ordinary settler with nothing to do and every need satisfied. */
static Colonist *spawn_settler(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist == NULL) {
        return NULL;
    }
    make_ordinary(colonist);
    for (int work = 0; work < WORK_TYPE_COUNT; work++) {
        colonist_set_work_priority(colonist, (WorkType)work, WORK_PRIORITY_DISABLED);
    }
    for (int need = 0; need < NEED_COUNT; need++) {
        colonist->needs[need] = 1.0f;
    }
    return colonist;
}

/* The parts of a human body, in the order of its plan. */
enum { HEAD, TORSO };

static ItemStack body_of(uint16_t corpse)
{
    ItemStack body = item_stack_pristine(ITEM_SETTLER_BODY, 1);
    body.corpse = corpse;
    return body;
}

/* Records come in order, keep what the settler was and nothing it held. */
static void test_records(void)
{
    Corpses corpses;
    corpses_init(&corpses);
    CHECK(corpses_get(&corpses, 0) == NULL);
    CHECK(corpses_get(&corpses, 1) == NULL);

    static Colonist dead; /* Big: not on the stack. */
    memset(&dead, 0, sizeof(dead));
    strcpy(dead.name, "Maren");
    dead.age = 31;
    dead.knowledge[KNOWLEDGE_FIRE] = 42.0;
    dead.carried = item_stack_pristine(ITEM_BRANCHES, 3);
    dead.tools[0] = item_stack_pristine(ITEM_COBBLE, 1);
    dead.path.steps = NULL;
    dead.path.count = 4;
    dead.path_index = 2;
    uint16_t first = corpses_add_settler(&corpses, &dead, 1234);
    CHECK_EQ_INT(first, 1);
    strcpy(dead.name, "Tove");
    CHECK_EQ_INT(corpses_add_settler(&corpses, &dead, 5000), 2);

    const Corpse *corpse = corpses_get(&corpses, first);
    REQUIRE(corpse != NULL);
    CHECK_EQ_INT(corpse->id, 1);
    CHECK_EQ_INT(corpse->kind, CORPSE_SETTLER);
    CHECK_EQ_UINT(corpse->died, 1234);
    CHECK_STR_EQ(corpse->colonist.name, "Maren"); /* A copy, not a pointer to the settler. */
    CHECK_EQ_INT(corpse->colonist.age, 31);
    CHECK_NEAR(corpse->colonist.knowledge[KNOWLEDGE_FIRE], 42.0, 0.001);
    CHECK_EQ_INT(corpse->colonist.carried.type, ITEM_NONE);
    CHECK_EQ_INT(corpse->colonist.tools[0].type, ITEM_NONE);
    CHECK(corpse->colonist.path.steps == NULL);
    CHECK_EQ_INT(corpse->colonist.path.count, 0);
    CHECK_EQ_INT(corpse->colonist.path_index, 0);
    CHECK_STR_EQ(corpses_get(&corpses, 2)->colonist.name, "Tove");

    /* Many: the list grows, every record is where its id says. */
    for (int i = 3; i <= 100; i++) {
        dead.age = i;
        CHECK_EQ_INT(corpses_add_settler(&corpses, &dead, (uint64_t)i), i);
    }
    CHECK_EQ_INT(corpses.count, 100);
    CHECK_EQ_INT(corpses_get(&corpses, 57)->colonist.age, 57);
    CHECK_STR_EQ(corpses_get(&corpses, 1)->colonist.name, "Maren");
    CHECK(corpses_get(&corpses, 101) == NULL);
    corpses_free(&corpses);
    CHECK(corpses_get(&corpses, 1) == NULL);
}

/* The bodies of two settlers never share a tile, and keep whose they are. */
static void test_bodies_of_two_never_share_a_stack(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Items *items = &game.items;
    CHECK(!item_stacks_match(body_of(1), body_of(2)));
    CHECK(item_stacks_match(body_of(2), body_of(2)));
    ItemStack merged = body_of(1);
    CHECK(!item_stack_merge(&merged, body_of(2)));

    CHECK_EQ_INT(items_add(items, 10, 10, 0, body_of(1)), 1);
    CHECK_EQ_INT(items_add(items, 10, 10, 0, body_of(2)), 0); /* Not on top of it. */
    CHECK_EQ_INT(items_get(items, 10, 10, 0).corpse, 1);
    /* Dropped near, it lies on another tile. */
    CHECK_EQ_INT(items_drop(items, &game.map, 10, 10, 0, body_of(2)), 1);
    int found = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            ItemStack stack = items_get(items, 10 + dx, 10 + dy, 0);
            if (stack.type == ITEM_SETTLER_BODY && stack.corpse == 2) {
                CHECK(dx != 0 || dy != 0);
                found++;
            }
        }
    }
    CHECK_EQ_INT(found, 1);

    /* A body nobody kept the record of (0) is a different one too. */
    CHECK_EQ_INT(items_add(items, 10, 10, 0, body_of(0)), 0);

    /* Only the bodies of settlers carry a record. */
    ItemStack branches = item_stack_pristine(ITEM_BRANCHES, 2);
    branches.corpse = 7;
    CHECK_EQ_INT(items_add(items, 20, 20, 0, branches), 2);
    CHECK_EQ_INT(items_get(items, 20, 20, 0).corpse, 0);

    /* It moves with the body: taken up and put down, and into the picture of the known world. */
    ItemStack lifted = items_get(items, 10, 10, 0);
    CHECK_EQ_INT(items_remove(items, 10, 10, 0, 1), 1);
    CHECK_EQ_INT(items_add(items, 30, 30, 0, lifted), 1);
    CHECK_EQ_INT(items_get(items, 30, 30, 0).corpse, 1);
    CHECK_EQ_INT(items_get(items, 10, 10, 0).type, ITEM_NONE);
    game_shutdown(&game);
}

/* Whose a body is counts in the hash. */
static void test_hash_sees_the_owner(void)
{
    Game a, b, c;
    REQUIRE(init_flat_game(&a));
    REQUIRE(init_flat_game(&b));
    REQUIRE(init_flat_game(&c));
    REQUIRE(items_add(&a.items, 10, 10, 0, body_of(0)) == 1);
    REQUIRE(items_add(&b.items, 10, 10, 0, body_of(3)) == 1);
    REQUIRE(items_add(&c.items, 10, 10, 0, body_of(0)) == 1);
    CHECK(items_hash(&a.items, 0) != items_hash(&b.items, 0));
    CHECK(items_hash(&a.items, 0) == items_hash(&c.items, 0));
    game_shutdown(&a);
    game_shutdown(&b);
    game_shutdown(&c);
}

/* A settler that dies leaves a body that says whose it was, with all it knew. */
static void test_death_keeps_the_settler(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *dying = spawn_settler(&game, 20, 20);
    REQUIRE(dying != NULL);
    REQUIRE(spawn_settler(&game, 40, 40) != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    char name[COLONIST_NAME_SIZE];
    memcpy(name, dying->name, sizeof(name));
    int age = dying->age;
    REQUIRE(colonist_set_knowledge_level(dying, KNOWLEDGE_FIRE, 61.0));
    REQUIRE(attributes_set(&dying->attributes, ATTRIBUTE_COURAGE, 400));
    dying->carried = item_stack_pristine(ITEM_BRANCHES, 2);
    journal_push(&dying->journal, (JournalEntry){.tick = 99, .event = JOURNAL_THOUGHT});
    CHECK(health_hurt(&dying->health, HEAD, INJURY_CRUSH, 30.0f) > 0.0f);
    game_tick(&game);
    uint64_t died = game.clock.tick; /* Dated like the notice of the death. */
    CHECK(game_colonist(&game, id) == NULL);

    ItemStack body = items_get(&game.items, 20, 20, 0);
    REQUIRE(body.type == ITEM_SETTLER_BODY);
    CHECK_EQ_INT(body.corpse, 1);
    CHECK_EQ_INT(game.corpses.count, 1);
    const Corpse *corpse = corpses_get(&game.corpses, body.corpse);
    REQUIRE(corpse != NULL);
    CHECK_STR_EQ(corpse->colonist.name, name);
    CHECK_EQ_INT(corpse->colonist.age, age);
    CHECK_EQ_UINT(corpse->died, died);
    CHECK_NEAR(colonist_knowledge_level(&corpse->colonist, KNOWLEDGE_FIRE), 61.0, 0.001);
    CHECK_EQ_INT(corpse->colonist.attributes.position[ATTRIBUTE_COURAGE], 400);
    CHECK_EQ_INT(corpse->colonist.health.death, DEATH_VITAL_PART);
    REQUIRE(corpse->colonist.journal.count >= 1);
    bool noted = false;
    for (int i = 0; i < corpse->colonist.journal.count; i++) {
        noted = noted || journal_recent(&corpse->colonist.journal, i)->tick == 99;
    }
    CHECK(noted);
    /* What it carried lies around it, not in the record. */
    CHECK_EQ_INT(corpse->colonist.carried.type, ITEM_NONE);
    CHECK(corpse->colonist.path.steps == NULL);
    game_shutdown(&game);
}

/* Two settlers dying together: two bodies, each its own. */
static void test_two_deaths_on_a_tile(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *a = spawn_settler(&game, 20, 20);
    Colonist *b = spawn_settler(&game, 20, 20);
    REQUIRE(a != NULL && b != NULL);
    REQUIRE(spawn_settler(&game, 40, 40) != NULL);
    char name_a[COLONIST_NAME_SIZE], name_b[COLONIST_NAME_SIZE];
    memcpy(name_a, a->name, sizeof(name_a));
    memcpy(name_b, b->name, sizeof(name_b));
    health_hurt(&a->health, HEAD, INJURY_CRUSH, 30.0f);
    health_hurt(&b->health, HEAD, INJURY_CRUSH, 30.0f);
    game_tick(&game);
    CHECK_EQ_INT(game.corpses.count, 2);
    int found = 0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            ItemStack stack = items_get(&game.items, 20 + dx, 20 + dy, 0);
            if (stack.type != ITEM_SETTLER_BODY) {
                continue;
            }
            const Corpse *corpse = corpses_get(&game.corpses, stack.corpse);
            REQUIRE(corpse != NULL);
            CHECK(strcmp(corpse->colonist.name, name_a) == 0 ||
                  strcmp(corpse->colonist.name, name_b) == 0);
            found++;
        }
    }
    CHECK_EQ_INT(found, 2);
    CHECK(corpses_get(&game.corpses, 1) != corpses_get(&game.corpses, 2));
    CHECK(strcmp(corpses_get(&game.corpses, 1)->colonist.name,
                 corpses_get(&game.corpses, 2)->colonist.name) != 0);
    game_shutdown(&game);
}

/* A wild animal that dies leaves a carcass that says whose it was. */
static void test_animal_death_keeps_the_animal(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId id = game_debug_spawn_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 20, 20, 0);
    Animal *deer = animals_get(&game.animals, id);
    REQUIRE(deer != NULL);
    REQUIRE(attributes_set(&deer->attributes, ATTRIBUTE_COURAGE, -300));
    journal_push(&deer->journal, (JournalEntry){.tick = 77, .event = JOURNAL_THOUGHT});
    /* The neck is vital. */
    health_hurt(&deer->health, 1, INJURY_CUT, 30.0f);
    REQUIRE(health_is_dead(&deer->health));
    int x = deer->x, y = deer->y;
    game_tick(&game);
    CHECK(animals_get(&game.animals, id) == NULL);

    ItemStack carcass = items_get(&game.items, x, y, 0);
    REQUIRE(carcass.type == ITEM_RED_DEER_CARCASS);
    CHECK_EQ_INT(carcass.corpse, 1);
    const Corpse *corpse = corpses_get(&game.corpses, carcass.corpse);
    REQUIRE(corpse != NULL);
    CHECK_EQ_INT(corpse->kind, CORPSE_ANIMAL);
    CHECK_EQ_UINT(corpse->died, game.clock.tick);
    CHECK_EQ_INT(corpse->animal.species, ANIMAL_RED_DEER);
    CHECK_EQ_INT(corpse->animal.sex, ANIMAL_FEMALE);
    CHECK_EQ_INT(corpse->animal.attributes.position[ATTRIBUTE_COURAGE], -300);
    CHECK(health_is_dead(&corpse->animal.health));
    CHECK(corpse->animal.health.death != DEATH_NONE);
    bool noted = false;
    for (int i = 0; i < corpse->animal.journal.count; i++) {
        noted = noted || journal_recent(&corpse->animal.journal, i)->tick == 77;
    }
    CHECK(noted);
    CHECK(corpse->animal.path.steps == NULL);
    /* The settlers' records and the animals' share one list. */
    CHECK_EQ_INT(game.corpses.count, 1);
    game_shutdown(&game);
}

/* A carnivore's bite takes weight off a carcass and leaves it its owner. */
static void test_a_bite_keeps_the_record(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    ItemStack carcass = item_stack_pristine(ITEM_WOLF_CARCASS, 1);
    carcass.corpse = 5;
    REQUIRE(items_add(&game.items, 10, 10, 0, carcass) == 1);
    ItemStack one = items_get(&game.items, 10, 10, 0);
    CHECK(item_stack_eat(&one, 0.5f) > 0.0f);
    CHECK_EQ_INT(items_remove(&game.items, 10, 10, 0, 1), 1);
    CHECK_EQ_INT(items_add(&game.items, 10, 10, 0, one), 1);
    ItemStack left = items_get(&game.items, 10, 10, 0);
    CHECK_EQ_INT(left.corpse, 5);
    CHECK(left.eaten > 0);
    /* Two carcasses of different animals do not share a tile either. */
    ItemStack other = item_stack_pristine(ITEM_WOLF_CARCASS, 1);
    other.corpse = 6;
    CHECK_EQ_INT(items_add(&game.items, 10, 10, 0, other), 0);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_records);
    RUN_TEST(test_bodies_of_two_never_share_a_stack);
    RUN_TEST(test_hash_sees_the_owner);
    RUN_TEST(test_death_keeps_the_settler);
    RUN_TEST(test_two_deaths_on_a_tile);
    RUN_TEST(test_animal_death_keeps_the_animal);
    RUN_TEST(test_a_bite_keeps_the_record);
    return test_summary();
}
