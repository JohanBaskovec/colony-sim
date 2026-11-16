/* Knowledge spreading: learning caps, gains of the day, fading, conversation,
 * apprenticeship and the notices of knowledges about to be lost. */
#include "core/conversation.h"
#include "core/game.h"
#include "core/notice.h"

#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

/* A small game on a flat, empty soil map with no starting colonists, at noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = 0; i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->cobbles[i] = 0;
        map->flint[i] = 0;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map);
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* Rested colonist at `level` in every knowledge, doing only `work`. */
static EntityId spawn_settler(Game *game, int x, int y, double level, WorkType work)
{
    EntityId id = game_spawn_colonist(game, x, y, 0);
    Colonist *colonist = game_colonist(game, id);
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
            colonist->knowledge[k] = level;
            colonist->knowledge_peak[k] = level;
        }
        for (int type = 0; type < WORK_TYPE_COUNT; type++) {
            colonist_set_work_priority(colonist, (WorkType)type,
                                       type == (int)work ? WORK_PRIORITY_HIGHEST
                                                         : WORK_PRIORITY_DISABLED);
        }
        colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
    }
    return id;
}

/* Keeps needs satisfied and the day going (from 20:00 straight to 08:00 the
 * next day, the midnight tick skipped), so that runs are about work. */
static void run_ticks(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
            if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
                for (int need = 0; need < NEED_COUNT; need++) {
                    game->colonists[slot].needs[need] = 1.0f;
                }
            }
        }
        if (game_hour_from_tick(game->clock.tick) >= 20) {
            game->clock.tick += 12 * TICKS_PER_HOUR;
        }
        game_tick(game);
    }
}

static void test_learning_helpers(void)
{
    /* Never past the cap; a level at or above it stays as it is. */
    double learned = knowledge_learn_capped(20.0, 100.0, 10.0, 30, 30.0);
    CHECK_NEAR(learned, 30.0, 1e-9);
    CHECK_NEAR(knowledge_learn_capped(45.0, 1.0, 1.0, 30, 30.0), 45.0, 1e-9);
    CHECK(knowledge_learn_capped(10.0, 1.0, 1.0, 30, 30.0) > 10.0);
    /* Taught rates follow the teacher's level. */
    CHECK_NEAR(knowledge_taught_rate(KNOWLEDGE_APPRENTICE_RATE, 80.0), 1.2, 1e-9);
    CHECK_NEAR(knowledge_taught_rate(KNOWLEDGE_CONVERSATION_RATE, 50.0), 0.15, 1e-9);
    CHECK_NEAR(knowledge_taught_rate(1.0, 0.0), 0.0, 1e-9);
    /* Fading: nothing before 15 days, then a fifteenth of a point a day,
     * never below 80% of the peak. */
    CHECK_NEAR(knowledge_fade_floor(70.0), 56.0, 1e-9);
    CHECK_NEAR(knowledge_fade_day(70.0, 70.0, KNOWLEDGE_FADE_AFTER_DAYS - 1), 70.0, 1e-9);
    CHECK_NEAR(knowledge_fade_day(70.0, 70.0, KNOWLEDGE_FADE_AFTER_DAYS), 70.0 - 1.0 / 15.0,
               1e-9);
    CHECK_NEAR(knowledge_fade_day(56.01, 70.0, 100), 56.0, 1e-9);
    CHECK_NEAR(knowledge_fade_day(50.0, 70.0, 100), 50.0, 1e-9); /* Below the floor: kept. */
}

static void test_gains_of_the_day_and_peak(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Test", 30, 0, 0, 0);
    CHECK(colonist_set_knowledge_level(&colonist, KNOWLEDGE_FIRE, 40.0));
    CHECK_NEAR(colonist.knowledge_peak[KNOWLEDGE_FIRE], 40.0, 1e-9);
    uint64_t tick = 3 * TICKS_PER_DAY + 100;

    /* Chatting stops at 30: nothing more to learn that way at 40. */
    CHECK_NEAR(colonist_learn(&colonist, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_CONVERSATION, 1.0, 1.0,
                              KNOWLEDGE_CONVERSATION_CAP, tick),
               0.0, 1e-12);
    double gain = colonist_learn(&colonist, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_APPRENTICESHIP, 1.2,
                                 1.0, 60.0, tick);
    CHECK(gain > 0.0);
    CHECK_NEAR(colonist.knowledge[KNOWLEDGE_FIRE], 40.0 + gain, 1e-9);
    CHECK_NEAR(colonist.knowledge_peak[KNOWLEDGE_FIRE], 40.0 + gain, 1e-9);
    CHECK_NEAR(colonist_knowledge_gain_today(&colonist, KNOWLEDGE_FIRE,
                                             KNOWLEDGE_SOURCE_APPRENTICESHIP, tick),
               gain, 1e-6);
    CHECK_NEAR(colonist_knowledge_gain_today(&colonist, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_DOING,
                                             tick),
               0.0, 1e-12);
    /* The next day starts from nothing. */
    CHECK_NEAR(colonist_knowledge_gain_today(&colonist, KNOWLEDGE_FIRE,
                                             KNOWLEDGE_SOURCE_APPRENTICESHIP,
                                             tick + TICKS_PER_DAY),
               0.0, 1e-12);
    colonist_learn_by_doing(&colonist, KNOWLEDGE_FIRE, 10, tick + TICKS_PER_DAY);
    CHECK(colonist_knowledge_gain_today(&colonist, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_DOING,
                                        tick + TICKS_PER_DAY) > 0.0);
    CHECK_NEAR(colonist_knowledge_gain_today(&colonist, KNOWLEDGE_FIRE,
                                             KNOWLEDGE_SOURCE_APPRENTICESHIP,
                                             tick + TICKS_PER_DAY),
               0.0, 1e-12);
    /* Doing is practice. */
    CHECK_EQ_UINT(colonist.knowledge_practice[KNOWLEDGE_FIRE], tick + TICKS_PER_DAY);
    CHECK_EQ_INT(colonist_days_unpractised(&colonist, KNOWLEDGE_FIRE, tick + 3 * TICKS_PER_DAY),
                 2);
    colonist_free(&colonist);
}

static void test_unpractised_knowledge_fades(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Test", 30, 0, 0, 0);
    colonist_set_knowledge_level(&colonist, KNOWLEDGE_BUTCHERING, 70.0);
    colonist_set_knowledge_level(&colonist, KNOWLEDGE_FIRE, 70.0);
    colonist_reset_practice(&colonist, 0);

    uint64_t day = TICKS_PER_DAY;
    colonist_fade_knowledge(&colonist, 10 * day);
    CHECK_NEAR(colonist.knowledge[KNOWLEDGE_BUTCHERING], 70.0, 1e-9);
    CHECK(!colonist_knowledge_fading(&colonist, KNOWLEDGE_BUTCHERING, 10 * day));
    CHECK(colonist_knowledge_fading(&colonist, KNOWLEDGE_BUTCHERING, 15 * day));
    /* Fire is kept in practice. */
    colonist_learn_by_doing(&colonist, KNOWLEDGE_FIRE, 1, 15 * day);
    double fire = colonist.knowledge[KNOWLEDGE_FIRE];
    for (int d = 15; d <= 29; d++) {
        colonist_fade_knowledge(&colonist, (uint64_t)d * day);
    }
    CHECK_NEAR(colonist.knowledge[KNOWLEDGE_BUTCHERING], 69.0, 1e-6); /* 15 days: a point. */
    CHECK_NEAR(colonist.knowledge[KNOWLEDGE_FIRE], fire, 1e-9);
    for (int d = 30; d < 30 + 15 * 20; d++) {
        colonist_fade_knowledge(&colonist, (uint64_t)d * day);
    }
    /* Down to 80% of the peak, no further. */
    CHECK_NEAR(colonist.knowledge[KNOWLEDGE_BUTCHERING], 56.0, 1e-9);
    CHECK_NEAR(colonist.knowledge_peak[KNOWLEDGE_BUTCHERING], 70.0, 1e-9);
    CHECK(!colonist_knowledge_fading(&colonist, KNOWLEDGE_BUTCHERING, 400 * day));
    colonist_free(&colonist);
}

static void test_conversation_topics(void)
{
    Rng rng;
    rng_seed(&rng, 7, 1);
    Colonist speaker;
    colonist_init(&speaker, "Speaker", 30, 0, 0, 0);
    CHECK_EQ_INT(conversation_pick_topic(&speaker, 0, &rng), KNOWLEDGE_NONE);
    speaker.knowledge[KNOWLEDGE_FIRE] = 0.5; /* Too little to talk about. */
    CHECK_EQ_INT(conversation_pick_topic(&speaker, 0, &rng), KNOWLEDGE_NONE);
    speaker.knowledge[KNOWLEDGE_FIRE] = 10.0;
    speaker.knowledge[KNOWLEDGE_GATHERING] = 90.0;
    int fire = 0, gathering = 0, others = 0;
    for (int i = 0; i < 2000; i++) {
        KnowledgeType topic = conversation_pick_topic(&speaker, 0, &rng);
        fire += topic == KNOWLEDGE_FIRE;
        gathering += topic == KNOWLEDGE_GATHERING;
        others += topic != KNOWLEDGE_FIRE && topic != KNOWLEDGE_GATHERING;
    }
    CHECK_EQ_INT(others, 0);
    CHECK(gathering > fire * 5); /* About 9 to 1. */
    CHECK(fire > 100);
    /* Settlers talk about what they do: fire practised lately counts twice. */
    uint64_t tick = 5 * TICKS_PER_DAY;
    speaker.knowledge_practice[KNOWLEDGE_FIRE] = tick - 100;
    int recent = 0;
    for (int i = 0; i < 2000; i++) {
        recent += conversation_pick_topic(&speaker, tick, &rng) == KNOWLEDGE_FIRE;
    }
    CHECK(recent > fire * 3 / 2);
    colonist_free(&speaker);
}

static void test_chats_teach_a_little(void)
{
    Rng rng;
    rng_seed(&rng, 11, 2);
    Colonist a, b;
    colonist_init(&a, "A", 30, 0, 0, 0);
    colonist_init(&b, "B", 30, 1, 0, 0);
    EntityId a_id = {0, 1}, b_id = {1, 1};
    a.knowledge[KNOWLEDGE_FIRE] = 80.0;
    b.knowledge[KNOWLEDGE_GATHERING] = 12.0;
    int about_knowledge = 0;
    for (int i = 0; i < 4000; i++) {
        about_knowledge += conversation_hold(&a, a_id, &b, b_id, (uint64_t)i * 250, &rng);
    }
    CHECK(about_knowledge > 1400 && about_knowledge < 1800); /* 40% of 4000. */
    CHECK_EQ_INT(a.chat_ticks, CONVERSATION_TICKS);
    CHECK(entity_id_equal(a.chat_partner, b_id));
    CHECK(entity_id_equal(b.chat_partner, a_id));
    /* B heard about fire up to the cap of conversation; A learns gathering
     * slowly (B knows little of it), never past B's level. */
    CHECK_NEAR(b.knowledge[KNOWLEDGE_FIRE], KNOWLEDGE_CONVERSATION_CAP, 1e-9);
    CHECK(a.knowledge[KNOWLEDGE_GATHERING] > 1.0 && a.knowledge[KNOWLEDGE_GATHERING] < 12.0);
    a.knowledge[KNOWLEDGE_GATHERING] = 11.99;
    for (int i = 0; i < 400; i++) {
        conversation_hold(&a, a_id, &b, b_id, (uint64_t)i * 250, &rng);
    }
    CHECK_NEAR(a.knowledge[KNOWLEDGE_GATHERING], 12.0, 1e-9);
    CHECK_NEAR(b.knowledge[KNOWLEDGE_GATHERING], 12.0, 1e-9);
    colonist_free(&a);
    colonist_free(&b);
}

static void test_conversations_need_nearby_free_settlers(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId a = spawn_settler(&game, 10, 10, 0.0, WORK_HAULING);
    EntityId b = spawn_settler(&game, 12, 11, 0.0, WORK_HAULING);
    EntityId far = spawn_settler(&game, 30, 30, 0.0, WORK_HAULING);
    Colonist *ca = game_colonist(&game, a), *cb = game_colonist(&game, b);
    Colonist *cfar = game_colonist(&game, far);
    REQUIRE(ca != NULL && cb != NULL && cfar != NULL);
    CHECK(colonist_can_chat(ca));
    CHECK(conversation_in_range(ca, cb));
    CHECK(!conversation_in_range(ca, cfar));
    int chats = 0, far_chats = 0;
    for (int i = 0; i < 1000; i++) {
        ca->chat_ticks = cb->chat_ticks = cfar->chat_ticks = 0;
        chats += conversations_update(game.colonists, &game.colonist_pool, game.clock.tick,
                                      &game.rng);
        far_chats += cfar->chat_ticks > 0;
    }
    CHECK(chats > 60 && chats < 140); /* Once an hour: a tenth of the checks. */
    CHECK_EQ_INT(far_chats, 0);
    /* Asleep: no chat. */
    ca->task = TASK_SLEEP;
    CHECK(!colonist_can_chat(ca));
    ca->chat_ticks = cb->chat_ticks = 0;
    int asleep = 0;
    for (int i = 0; i < 200; i++) {
        asleep += conversations_update(game.colonists, &game.colonist_pool, game.clock.tick,
                                       &game.rng);
    }
    CHECK_EQ_INT(asleep, 0);
    game_shutdown(&game);
}

/* A station knapping hand axes forever, flint and hammers ready. */
static int knapping_workshop(Game *game)
{
    int station = workstations_place(&game->workstations, &game->map,
                                     WORKSTATION_KNAPPING_SPOT, 14, 10, 0);
    items_add(&game->items, 18, 12, 0, item_stack_make(ITEM_FLINT, 20, ITEM_QUALITY_FINE, 0));
    items_add(&game->items, 18, 10, 0, item_stack_make(ITEM_COBBLE, 6, ITEM_QUALITY_FINE, 0));
    int bill = workstation_add_bill(&game->workstations, station, RECIPE_HAND_AXE);
    workstation_set_bill_mode(&game->workstations, station, bill, BILL_FOREVER);
    return station;
}

/* Work done at the station after `ticks`, with or without an apprentice. */
static float hand_axe_work(bool apprentice, int ticks)
{
    Game game;
    if (!init_flat_game(&game)) {
        return -1.0f;
    }
    int station = knapping_workshop(&game);
    spawn_settler(&game, 10, 10, 80.0, WORK_CRAFTING);
    if (apprentice) {
        spawn_settler(&game, 10, 14, 55.0, WORK_CRAFTING);
    }
    run_ticks(&game, ticks);
    const Workstation *spot = workstation_get_const(&game.workstations, station);
    float done = spot->work_progress +
                 (float)spot->bills[0].done * recipe_def(RECIPE_HAND_AXE)->work;
    game_shutdown(&game);
    return done;
}

/* Runs until the master works at the station (after at least 1000 ticks, so
 * that an apprentice had time to join). */
static void run_until_at_work(Game *game, const Colonist *master, int max_ticks)
{
    run_ticks(game, 1000);
    for (int i = 1000; i < max_ticks && !colonist_at_work(master); i++) {
        run_ticks(game, 1);
    }
}

static void test_apprentice_assists_a_master(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    knapping_workshop(&game);
    EntityId master_id = spawn_settler(&game, 10, 10, 80.0, WORK_CRAFTING);
    /* Too little for hand axes (70), and nothing else to do. */
    EntityId apprentice_id = spawn_settler(&game, 10, 14, 55.0, WORK_CRAFTING);
    EntityId second_id = spawn_settler(&game, 11, 14, 55.0, WORK_CRAFTING);
    /* Does not do crafting: never an apprentice in it. */
    EntityId idler_id = spawn_settler(&game, 9, 14, 55.0, WORK_HAULING);
    Colonist *master = game_colonist(&game, master_id);
    Colonist *apprentice = game_colonist(&game, apprentice_id);
    Colonist *second = game_colonist(&game, second_id);
    Colonist *idler = game_colonist(&game, idler_id);
    REQUIRE(master != NULL && apprentice != NULL && second != NULL && idler != NULL);
    apprentice->age = 30; /* Learns at the adult pace. */

    run_until_at_work(&game, master, 3000);
    REQUIRE(master->task == TASK_CRAFT);
    CHECK(colonist_at_work(master));
    CHECK_EQ_INT(apprentice->task, TASK_ASSIST);
    CHECK(entity_id_equal(apprentice->mentor, master_id));
    CHECK(entity_id_equal(game_apprentice_of(&game, master_id), apprentice_id));
    /* One apprentice per master. */
    CHECK(second->task != TASK_ASSIST);
    CHECK(idler->task != TASK_ASSIST);
    /* At the master's side, learning. */
    CHECK(abs(apprentice->x - master->x) <= 1 && abs(apprentice->y - master->y) <= 1);
    CHECK(apprentice->knowledge[KNOWLEDGE_STONE_KNAPPING] > 55.0);
    CHECK(colonist_knowledge_gain_today(apprentice, KNOWLEDGE_STONE_KNAPPING,
                                        KNOWLEDGE_SOURCE_APPRENTICESHIP,
                                        game.clock.tick) > 0.0);
    CHECK(second->knowledge[KNOWLEDGE_STONE_KNAPPING] == 55.0);
    CHECK(master->assisted_until >= game.clock.tick);
    CHECK_EQ_UINT(master->knowledge_practice[KNOWLEDGE_STONE_KNAPPING], game.clock.tick);

    /* An hour as an apprentice: about 1.5 × 0.8 × difficulty(55) points. */
    double before = apprentice->knowledge[KNOWLEDGE_STONE_KNAPPING];
    run_ticks(&game, TICKS_PER_HOUR);
    double gained = apprentice->knowledge[KNOWLEDGE_STONE_KNAPPING] - before;
    CHECK(gained > 0.5 && gained <= 1.2 * knowledge_difficulty(55.0) + 1e-6);

    /* Once at the job's level, the apprentice works alone. */
    apprentice->knowledge[KNOWLEDGE_STONE_KNAPPING] = 70.0;
    run_ticks(&game, COLONIST_ASSIST_WAIT_TICKS + 10);
    CHECK(apprentice->task != TASK_ASSIST);

    /* The second settler takes the free place; a master gone, it stops. */
    run_until_at_work(&game, master, 3000);
    for (int i = 0; i < 60 && second->task != TASK_ASSIST; i++) {
        run_ticks(&game, 1); /* At its next step at the latest. */
    }
    CHECK_EQ_INT(second->task, TASK_ASSIST);
    game_remove_colonist(&game, master_id);
    run_ticks(&game, 60); /* At its next step at the latest. */
    /* (It may follow the first apprentice, now able to make hand axes.) */
    CHECK(!(second->task == TASK_ASSIST && entity_id_equal(second->mentor, master_id)));
    game_shutdown(&game);
}

static void test_apprentices_speed_up_the_work(void)
{
    float alone = hand_axe_work(false, 5000);
    float helped = hand_axe_work(true, 5000);
    CHECK(alone > 0.0f);
    CHECK(helped > alone * 1.1f);
}

static int count_notices(const Game *game, uint32_t from, NoticeType type, int knowledge)
{
    int count = 0;
    for (uint32_t n = from; n < game->notices.count; n++) {
        const Notice *notice = notices_get(&game->notices, n);
        if (notice != NULL && notice->type == type && notice->knowledge == knowledge) {
            count++;
        }
    }
    return count;
}

static void test_knowledge_notices(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    CHECK_EQ_UINT(game.notices.count, 0);
    EntityId ids[3];
    for (int i = 0; i < 3; i++) {
        ids[i] = spawn_settler(&game, 10 + i, 10, 60.0, WORK_HAULING);
    }
    /* Newcomers bring knowledges nobody had. */
    game_count_knowledge(&game);
    CHECK_EQ_INT(count_notices(&game, 0, NOTICE_KNOWLEDGE_DISCOVERED, KNOWLEDGE_FIRE), 1);
    CHECK_EQ_INT(count_notices(&game, 0, NOTICE_KNOWLEDGE_MASTERED, KNOWLEDGE_FIRE), 1);
    CHECK(game_knowledge_discovered(&game, KNOWLEDGE_FIRE));
    CHECK_EQ_INT(game_knowledge_masters(&game, KNOWLEDGE_FIRE), 3);

    /* Nothing changed: no notice. */
    uint32_t mark = game.notices.count;
    game_count_knowledge(&game);
    CHECK_EQ_UINT(game.notices.count, mark);

    game_colonist(&game, ids[0])->knowledge[KNOWLEDGE_FIRE] = 40.0;
    game_count_knowledge(&game);
    CHECK_EQ_UINT(game.notices.count, mark); /* Still two masters. */
    game_colonist(&game, ids[1])->knowledge[KNOWLEDGE_FIRE] = 40.0;
    game_count_knowledge(&game);
    REQUIRE(game.notices.count == mark + 1);
    const Notice *last = notices_get(&game.notices, mark);
    CHECK_EQ_INT(last->type, NOTICE_KNOWLEDGE_LAST_MASTER);
    CHECK_EQ_INT(last->knowledge, KNOWLEDGE_FIRE);
    CHECK_STR_EQ(last->name, game_colonist(&game, ids[2])->name);

    /* The last master dies: lost. */
    mark = game.notices.count;
    game_remove_colonist(&game, ids[2]);
    game_count_knowledge(&game);
    CHECK_EQ_INT(count_notices(&game, mark, NOTICE_KNOWLEDGE_LOST, KNOWLEDGE_FIRE), 1);
    CHECK_EQ_INT(count_notices(&game, mark, NOTICE_KNOWLEDGE_FORGOTTEN, KNOWLEDGE_FIRE), 0);
    CHECK(game_knowledge_discovered(&game, KNOWLEDGE_FIRE));
    CHECK_EQ_INT(game_knowledge_masters(&game, KNOWLEDGE_FIRE), 0);

    /* Nobody knows anything of it any more: forgotten, and hidden. */
    mark = game.notices.count;
    game_colonist(&game, ids[0])->knowledge[KNOWLEDGE_FIRE] = 0.0;
    game_colonist(&game, ids[1])->knowledge[KNOWLEDGE_FIRE] = 0.5;
    game_count_knowledge(&game);
    CHECK_EQ_INT(count_notices(&game, mark, NOTICE_KNOWLEDGE_FORGOTTEN, KNOWLEDGE_FIRE), 1);
    CHECK(!game_knowledge_discovered(&game, KNOWLEDGE_FIRE));

    /* Mastered again by practice. */
    mark = game.notices.count;
    game_colonist(&game, ids[1])->knowledge[KNOWLEDGE_FIRE] = 50.0;
    game_count_knowledge(&game);
    CHECK_EQ_INT(count_notices(&game, mark, NOTICE_KNOWLEDGE_DISCOVERED, KNOWLEDGE_FIRE), 1);
    CHECK_EQ_INT(count_notices(&game, mark, NOTICE_KNOWLEDGE_MASTERED, KNOWLEDGE_FIRE), 1);

    /* game_tick() counts on its own. */
    mark = game.notices.count;
    game_colonist(&game, ids[1])->knowledge[KNOWLEDGE_FIRE] = 10.0;
    game.clock.tick = GAME_KNOWLEDGE_CHECK_TICKS * 100 - 1;
    game_tick(&game);
    CHECK_EQ_INT(count_notices(&game, mark, NOTICE_KNOWLEDGE_LOST, KNOWLEDGE_FIRE), 1);
    game_shutdown(&game);
}

static void test_notice_ring(void)
{
    Notices notices;
    memset(&notices, 0, sizeof(notices));
    CHECK(notices_get(&notices, 0) == NULL);
    for (int i = 0; i < NOTICE_MAX + 8; i++) {
        notices_push(&notices, NOTICE_KNOWLEDGE_LOST, i % KNOWLEDGE_COUNT, NULL, (uint64_t)i);
    }
    CHECK(notices_get(&notices, 0) == NULL); /* Forgotten. */
    CHECK(notices_get(&notices, NOTICE_MAX + 8) == NULL); /* Not raised yet. */
    const Notice *last = notices_get(&notices, NOTICE_MAX + 7);
    REQUIRE(last != NULL);
    CHECK_EQ_UINT(last->tick, NOTICE_MAX + 7);
    CHECK_STR_EQ(last->name, "");
    REQUIRE(notices_get(&notices, 8) != NULL);
    CHECK_EQ_UINT(notices_get(&notices, 8)->tick, 8);
    for (int type = 0; type < NOTICE_TYPE_COUNT; type++) {
        CHECK(notice_text_key((NoticeType)type) != NULL);
    }
}

static void test_game_fades_knowledge_daily(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId id = spawn_settler(&game, 10, 10, 70.0, WORK_HAULING);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    CHECK_EQ_UINT(colonist->knowledge_practice[KNOWLEDGE_FIRE], game.clock.tick);
    uint64_t start = game.clock.tick;
    /* The night of day 20: unpractised for more than 15 days. */
    game.clock.tick = 20 * (uint64_t)TICKS_PER_DAY - 1;
    game_tick(&game);
    CHECK(game.clock.tick - start >= 15 * (uint64_t)TICKS_PER_DAY);
    CHECK_NEAR(colonist->knowledge[KNOWLEDGE_FIRE], 70.0 - 1.0 / 15.0, 1e-9);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_learning_helpers);
    RUN_TEST(test_gains_of_the_day_and_peak);
    RUN_TEST(test_unpractised_knowledge_fades);
    RUN_TEST(test_conversation_topics);
    RUN_TEST(test_chats_teach_a_little);
    RUN_TEST(test_conversations_need_nearby_free_settlers);
    RUN_TEST(test_apprentice_assists_a_master);
    RUN_TEST(test_apprentices_speed_up_the_work);
    RUN_TEST(test_knowledge_notices);
    RUN_TEST(test_notice_ring);
    RUN_TEST(test_game_fades_knowledge_daily);
    return test_summary();
}
