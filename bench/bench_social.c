/*
 * Social simulation: runs a colony for some in-game days and reports its
 * social life (docs/SOCIAL.md). To tune chats, arguments, brawls, apologies
 * and company. Not part of the test suite (use an optimized build:
 * the headless-release preset).
 *
 *     bench_social [colonists] [days] [scenario] [seed] [setup] [map_size]
 *     bench_social suite [days] [seed]
 *
 * One colony: every day, the chats held and what they turned into, the
 * company the settlers keep and the relationships among the living; at the
 * end, the totals, every settler's opinion of every other and the state
 * hash. Scenarios: see core/scenario.h (1 = gathering, the default; 4 =
 * feuds: two clans who loathe each other). `setup`, applied after the
 * scenario: `discord` (every settler takes against every other,
 * game_debug_discord()), `feud` (two clans, game_debug_feud(): as scenario
 * 4, on any scenario), `abrasive` (every settler unkind and bold), `kind`,
 * `loners` (the least sociable), `sociable` (the most), or `-` for none.
 *
 * `suite`: a line for each of a set of cases (an ordinary colony, an idle
 * one, feuds, discord, abrasive, kind, loners, sociable, feuds and discord
 * among idle settlers, a pair, a crowd), to compare them at a glance.
 */
#include "core/game.h"
#include "core/scenario.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const KIND_NAMES[CHAT_KIND_COUNT] = {
    [CHAT_SMALL_TALK] = "small talk",
    [CHAT_KNOWLEDGE] = "knowledge",
    [CHAT_EXPLAIN] = "explain",
    [CHAT_STORY] = "story",
    [CHAT_LIE] = "lie",
    [CHAT_COMPLIMENT] = "compliment",
    [CHAT_INSULT] = "insult",
    [CHAT_GOSSIP] = "gossip",
    [CHAT_REPORT] = "report",
    [CHAT_ARGUMENT] = "argument",
    [CHAT_APOLOGY] = "apology",
};

static const char *const LEVEL_NAMES[RELATION_LEVEL_COUNT] = {"enemies", "rivals", "acquainted",
                                                              "friends", "close"};

/* The company the settlers kept, sampled every CONVERSATION_CHECK_TICKS
 * over the living settlers awake. */
typedef struct CompanySamples {
    long samples;
    long together;  /* Someone within chatting range. */
    long lonely;    /* Company below COLONIST_LONELY_BELOW. */
    long good;      /* THOUGHT_GOOD_COMPANY. */
    long unwelcome; /* THOUGHT_UNWELCOME_COMPANY. */
    double company; /* Sum of the company need. */
} CompanySamples;

static bool living(const Game *game, uint32_t slot)
{
    EntityId id = game_colonist_id_at(game, slot);
    return !entity_id_is_none(id) && !health_is_dead(&game->colonists[slot].health);
}

static int living_count(const Game *game)
{
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        count += living(game, slot);
    }
    return count;
}

static void sample_company(const Game *game, CompanySamples *out)
{
    uint32_t slots = game_colonist_slot_count(game);
    for (uint32_t who = 0; who < slots; who++) {
        const Colonist *colonist = &game->colonists[who];
        if (!living(game, who) || colonist_is_asleep(colonist)) {
            continue;
        }
        bool together = false;
        for (uint32_t whom = 0; whom < slots && !together; whom++) {
            const Colonist *other = &game->colonists[whom];
            int dx = other->x - colonist->x, dy = other->y - colonist->y;
            together = whom != who && living(game, whom) && !colonist_is_asleep(other) &&
                       other->z == colonist->z &&
                       dx * dx + dy * dy <= CONVERSATION_RANGE * CONVERSATION_RANGE;
        }
        out->samples++;
        out->together += together;
        out->lonely += colonist->company < COLONIST_LONELY_BELOW;
        out->good += (colonist->social_states & THOUGHT_BIT(THOUGHT_GOOD_COMPANY)) != 0;
        out->unwelcome += (colonist->social_states & THOUGHT_BIT(THOUGHT_UNWELCOME_COMPANY)) != 0;
        out->company += colonist->company;
    }
}

static double percent(long part, long whole)
{
    return whole > 0 ? 100.0 * (double)part / (double)whole : 0.0;
}

static uint32_t total_chats(const ConversationCounts *counts)
{
    uint32_t total = 0;
    for (int kind = 0; kind < CHAT_KIND_COUNT; kind++) {
        total += counts->kinds[kind];
    }
    return total;
}

static uint32_t arguments(const ConversationCounts *counts)
{
    return counts->kinds[CHAT_ARGUMENT] + counts->answered_back;
}

/* Relationships among the living, one way each. */
static void count_levels(const Game *game, int levels[RELATION_LEVEL_COUNT])
{
    memset(levels, 0, sizeof(int) * RELATION_LEVEL_COUNT);
    uint32_t slots = game_colonist_slot_count(game);
    for (uint32_t who = 0; who < slots; who++) {
        for (uint32_t whom = 0; whom < slots; whom++) {
            if (who != whom && living(game, who) && living(game, whom)) {
                levels[game->social.bonds[who][whom].level]++;
            }
        }
    }
}

/* One line: the day's chats, company and the relationships now. */
static void print_day(const Game *game, int day, const ConversationCounts *before,
                      const CompanySamples *company)
{
    const ConversationCounts *now = &game->chat_counts;
    int levels[RELATION_LEVEL_COUNT];
    count_levels(game, levels);
    printf("day %3d: %3u chats, %2u insults, %2u arguments, %2u brawls, %2u reports, %2u apologies "
           "(%u refused) | together %3.0f%%, company %.2f, lonely %3.0f%% | %d alive:",
           day, total_chats(now) - total_chats(before),
           now->kinds[CHAT_INSULT] - before->kinds[CHAT_INSULT], arguments(now) - arguments(before),
           now->brawls - before->brawls, now->kinds[CHAT_REPORT] - before->kinds[CHAT_REPORT],
           now->kinds[CHAT_APOLOGY] - before->kinds[CHAT_APOLOGY], now->refused - before->refused,
           percent(company->together, company->samples),
           company->samples > 0 ? company->company / (double)company->samples : 0.0,
           percent(company->lonely, company->samples), living_count(game));
    for (int level = 0; level < RELATION_LEVEL_COUNT; level++) {
        printf(" %s %d", LEVEL_NAMES[level], levels[level]);
    }
    printf("\n");
}

static void print_totals(const Game *game)
{
    const ConversationCounts *counts = &game->chat_counts;
    printf("\n%u chats:", total_chats(counts));
    for (int kind = 0; kind < CHAT_KIND_COUNT; kind++) {
        printf(" %s %u%s", KIND_NAMES[kind], counts->kinds[kind],
               kind + 1 < CHAT_KIND_COUNT ? "," : "\n");
    }
    printf("lies found out %u, insults answered back %u, brawls %u, apologies accepted %u, "
           "refused %u\n",
           counts->lies_found_out, counts->answered_back, counts->brawls, counts->forgiven,
           counts->refused);
}

/* Who thinks what of whom: a row per settler (kindness, confidence and
 * sociability in percent of their range), its opinion of each other one. */
static void print_opinions(const Game *game)
{
    uint32_t slots = game_colonist_slot_count(game);
    printf("\nopinions (row of column)   kind  bold  soc.");
    for (uint32_t whom = 0; whom < slots; whom++) {
        if (!entity_id_is_none(game_colonist_id_at(game, whom))) {
            printf(" %5.5s", game->colonists[whom].name);
        }
    }
    printf("\n");
    for (uint32_t who = 0; who < slots; who++) {
        if (entity_id_is_none(game_colonist_id_at(game, who))) {
            continue;
        }
        const Colonist *colonist = &game->colonists[who];
        const Attributes *attributes = &colonist->attributes;
        printf("%-12.12s%s %4.0f %4.0f %4.0f       ", colonist->name,
               health_is_dead(&colonist->health) ? "+" : " ",
               100.0f * attribute_position(attributes, ATTRIBUTE_KINDNESS),
               100.0f * attribute_position(attributes, ATTRIBUTE_CONFIDENCE),
               100.0f * attribute_position(attributes, ATTRIBUTE_SOCIABILITY));
        for (uint32_t whom = 0; whom < slots; whom++) {
            if (entity_id_is_none(game_colonist_id_at(game, whom))) {
                continue;
            }
            if (whom == who) {
                printf("     .");
            } else {
                printf(" %5.0f", social_opinion(&game->social, game->colonists, (int)who, (int)whom,
                                                game->clock.tick));
            }
        }
        printf("\n");
    }
}

/* Every living settler's attribute set to a fraction of its range. */
static void set_everyone(Game *game, AttributeType type, float fraction)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!living(game, slot)) {
            continue;
        }
        Colonist *colonist = &game->colonists[slot];
        Attributes attributes = colonist->attributes;
        attributes_set(&attributes, type, (int)(fraction * (float)ATTRIBUTE_POSITION_MAX));
        colonist_set_attributes(colonist, &attributes);
    }
}

static bool apply_setup(Game *game, const char *setup)
{
    if (strcmp(setup, "discord") == 0) {
        game_debug_discord(game);
    } else if (strcmp(setup, "feud") == 0) {
        game_debug_feud(game);
    } else if (strcmp(setup, "abrasive") == 0) {
        set_everyone(game, ATTRIBUTE_KINDNESS, -0.8f);
        set_everyone(game, ATTRIBUTE_CONFIDENCE, 0.6f);
    } else if (strcmp(setup, "kind") == 0) {
        set_everyone(game, ATTRIBUTE_KINDNESS, 0.8f);
    } else if (strcmp(setup, "loners") == 0) {
        set_everyone(game, ATTRIBUTE_SOCIABILITY, -1.0f);
    } else if (strcmp(setup, "sociable") == 0) {
        set_everyone(game, ATTRIBUTE_SOCIABILITY, 1.0f);
    } else if (strcmp(setup, "-") != 0) {
        return false;
    }
    return true;
}

static bool start(Game *game, int colonists, int scenario, uint64_t seed, const char *setup,
                  int size)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = size;
    params.world.height = size;
    params.colonist_count = colonists;
    if (!game_init(game, &params)) {
        fprintf(stderr, "initialization failed\n");
        return false;
    }
    if (!scenario_apply(game, (Scenario)scenario) || !apply_setup(game, setup)) {
        fprintf(stderr, "unknown scenario %d or setup %s\n", scenario, setup);
        game_shutdown(game);
        return false;
    }
    return true;
}

/* A day of ticks, sampling the company. */
static void run_day(Game *game, CompanySamples *company)
{
    for (int tick = 0; tick < TICKS_PER_DAY; tick++) {
        game_tick(game);
        if (game->clock.tick % CONVERSATION_CHECK_TICKS == 1) {
            sample_company(game, company);
        }
    }
}

static int run_one(int colonists, int days, int scenario, uint64_t seed, const char *setup,
                   int size)
{
    static Game game;
    if (!start(&game, colonists, scenario, seed, setup, size)) {
        return EXIT_FAILURE;
    }
    printf("map %dx%d, seed %llu, scenario %d, setup %s: %u colonists, %d days\n", size, size,
           (unsigned long long)seed, scenario, setup, game.colonist_pool.count, days);
    for (int day = 1; day <= days; day++) {
        ConversationCounts before = game.chat_counts;
        CompanySamples company = {0};
        run_day(&game, &company);
        print_day(&game, day, &before, &company);
    }
    print_totals(&game);
    print_opinions(&game);
    printf("\nstate hash: %016llx\n", (unsigned long long)game_hash(&game));
    game_shutdown(&game);
    return EXIT_SUCCESS;
}

typedef struct SuiteCase {
    const char *name;
    int colonists;
    int scenario;
    const char *setup;
} SuiteCase;

static const SuiteCase SUITE[] = {
    {"ordinary", 8, SCENARIO_GATHERING, "-"},
    {"idle", 8, SCENARIO_NONE, "-"},
    {"feuds", 8, SCENARIO_FEUDS, "-"},
    {"discord", 8, SCENARIO_GATHERING, "discord"},
    {"abrasive", 8, SCENARIO_GATHERING, "abrasive"},
    {"kind", 8, SCENARIO_GATHERING, "kind"},
    {"loners", 8, SCENARIO_GATHERING, "loners"},
    {"sociable", 8, SCENARIO_GATHERING, "sociable"},
    {"feud-idle", 8, SCENARIO_NONE, "feud"},
    {"disc-idle", 8, SCENARIO_NONE, "discord"},
    {"pair", 2, SCENARIO_GATHERING, "-"},
    {"crowd", 24, SCENARIO_GATHERING, "-"},
};

/* A line per case: chats per settler and day (the first day apart: they
 * start side by side), what they turned into over the whole run, the
 * company kept, and the relationships at the end. */
static int run_suite(int days, uint64_t seed)
{
    static Game game;
    printf("seed %llu, %d days; chats per settler and day (first day, then the rest), totals, "
           "company kept by the settlers awake\n\n",
           (unsigned long long)seed, days);
    printf("%-9s %4s %6s %6s %5s %5s %5s %5s %5s %7s %5s %5s %5s %5s %5s %6s  %s\n", "case", "sett",
           "day 1", "later", "ins.", "arg.", "brawl", "repor", "apol.", "refused", "tog%", "comp",
           "lone%", "good%", "unwe%", "alive", "enemies/rivals/acq./friends/close");
    for (size_t i = 0; i < sizeof(SUITE) / sizeof(SUITE[0]); i++) {
        const SuiteCase *test = &SUITE[i];
        if (!start(&game, test->colonists, test->scenario, seed, test->setup, 128)) {
            return EXIT_FAILURE;
        }
        CompanySamples company = {0};
        run_day(&game, &company);
        uint32_t first = total_chats(&game.chat_counts);
        for (int day = 2; day <= days; day++) {
            run_day(&game, &company);
        }
        const ConversationCounts *counts = &game.chat_counts;
        int levels[RELATION_LEVEL_COUNT];
        count_levels(&game, levels);
        double settlers = (double)test->colonists;
        printf("%-9s %4d %6.1f %6.1f %5u %5u %5u %5u %5u %7u %5.0f %5.2f %5.0f %5.0f %5.0f %6d  "
               "%d/%d/%d/%d/%d\n",
               test->name, test->colonists, (double)first / settlers,
               days > 1 ? (double)(total_chats(counts) - first) / settlers / (days - 1) : 0.0,
               counts->kinds[CHAT_INSULT], arguments(counts), counts->brawls,
               counts->kinds[CHAT_REPORT], counts->kinds[CHAT_APOLOGY], counts->refused,
               percent(company.together, company.samples),
               company.samples > 0 ? company.company / (double)company.samples : 0.0,
               percent(company.lonely, company.samples), percent(company.good, company.samples),
               percent(company.unwelcome, company.samples), living_count(&game), levels[0],
               levels[1], levels[2], levels[3], levels[4]);
        fflush(stdout);
        game_shutdown(&game);
    }
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "suite") == 0) {
        int days = argc > 2 ? atoi(argv[2]) : 5;
        uint64_t seed = argc > 3 ? strtoull(argv[3], NULL, 10) : 42;
        return run_suite(days, seed);
    }
    int colonists = argc > 1 ? atoi(argv[1]) : 8;
    int days = argc > 2 ? atoi(argv[2]) : 10;
    int scenario = argc > 3 ? atoi(argv[3]) : SCENARIO_GATHERING;
    uint64_t seed = argc > 4 ? strtoull(argv[4], NULL, 10) : 42;
    const char *setup = argc > 5 ? argv[5] : "-";
    int size = argc > 6 ? atoi(argv[6]) : 128;
    return run_one(colonists, days, scenario, seed, setup, size);
}
