#include "core/item.h"

#include "core/game_clock.h"
#include "core/i18n.h"
#include "core/map.h"
#include "core/util.h"
#include "core/wood.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Units are human-sized pieces with realistic weights:
 * - wood: a log about 1 m long and 11 cm thick, 7 kg of fresh wood (~10 l);
 *   a mature tree gives 60 of them (420 kg: trunk and big branches);
 * - stone block: a rough block of about 20 kg dug out of a rock wall (7.5 l
 *   of rock, irregular so it stacks with gaps);
 * - blackberries: a handful of about 300 g (half a liter, ~190 kcal: ripe
 *   wild fruit, generously counted);
 * - hazelnuts: 100 g in their shells (~285 kcal), which keep for months;
 * - sweet chestnuts: a handful of 150 g in their shells (~250 kcal), a month;
 * - wild apples and pears: a few small ones, 200 g (~100 and ~110 kcal),
 *   sour and hard: apples keep a month, pears two weeks;
 * - mushrooms: 100 g of caps (~30 kcal), more taste than food;
 * - wild garlic, nettles, plantain: a bunch of about 200 g of leaves or young
 *   shoots (~60 kcal for garlic and nettles, ~50 for plantain);
 * - wild carrots: a bunch of thin roots, about 250 g (~100 kcal);
 * - cattail rhizomes: about 250 g of starchy rootstock (~250 kcal, half of it
 *   digested raw); roasted in the embers, most of it (~350 kcal);
 * - roasted carrots: a bunch of wild carrots roasted, softer (~130 kcal);
 * - wood sorrel, wild strawberries, bilberries, rose hips: a bunch of leaves (~40 kcal), a handful
 *   of small fruit (~65 kcal), of bilberries (~170 kcal), of dried hips (~240 kcal: they keep
 *   for two months);
 * - moss (an armful, 0.4 kg), bracken, heather and reeds (bundles of 1 to 1.5 kg, dry): bedding,
 *   thatch and fuel;
 * - cobble: a rounded stone that fits the hand, about 1 kg, picked up from
 *   the ground; tough, the knapper's hammer (its quality is its toughness);
 * - flint: a nodule of about 1 kg, the stone that is knapped (its quality is how
 *   fine and even its grain is);
 * - branches: a dead or cut branch of about 1.5 kg, poorly packed;
 * - twigs: a handful (about 200 g) of kindling;
 * - grass: an armful, 2.5 kg cut fresh, 1 kg once dry (about 12 l either way).
 * Stone blocks never wear out; logs vary with the woodcutter
 * (knots, cracks) and wild food with the gatherer (ripeness, bruises). Fresh grass left on the
 * ground dries in three days. Food lying in the open at 15 °C keeps (rot_days):
 * greens and mushrooms 2 days, blackberries 3, roasted roots 4 (moist and
 * soft), cattail rhizomes 15, wild carrots 20, hazelnuts in their shells 90.
 * Bodies (one to a tile but a hare's, dragged by whoever moves them) rot
 * away in 4 (a hare) to 10 days (a settler, a red deer, a boar).
 * Tools and weapons (docs/KNOWLEDGE.md §6, §8; a plain cobble serves as
 * the knapper's hammer): a chopper (1 kg), a sharp flake (30 g, razor sharp
 * but soon worn out), a hand axe (600 g); a throwing spear (0.8 kg, a long
 * straight branch) and a thrusting spear (1.5 kg, thicker). Fire hardening
 * (docs/KNOWLEDGE.md §5.2) makes spears that last half as long again. The
 * digging stick (1 kg, a stout pointed branch) digs roots up (§7); hardened
 * in the fire, it lasts half as long again too.
 */
#define DEF(id_, key, category_, weight_, bulk_, nutrition_, hp, quality, damage)                  \
    .id = (id_), .name_key = (key), .category = (category_), .weight = (weight_), .bulk = (bulk_), \
    .nutrition = (nutrition_), .max_hit_points = (hp), .has_quality = (quality),                   \
    .damageable = (damage)

/* How a material takes water (ItemWater): fresh, room, equilibrium factor,
 * hours to dry, dry below; weighed fresh (food) or at the reference air. */
#define WATER(fresh_, room_, factor, hours, dry)                                                   \
    .water = {.fresh = (fresh_),                                                                   \
              .room = (room_),                                                                     \
              .equilibrium = (factor),                                                             \
              .dry_hours = (hours),                                                                \
              .dry_below = (dry)}
#define FOOD_WATER(fresh_, room_, factor, hours, dry)                                              \
    .water = {.fresh = (fresh_),                                                                   \
              .room = (room_),                                                                     \
              .equilibrium = (factor),                                                             \
              .dry_hours = (hours),                                                                \
              .dry_below = (dry),                                                                  \
              .weighed_fresh = true}
/* Wooden tools and spears: seasoned wood. */
#define TOOL_WATER WATER(0.15f, 0.5f, 1.0f, 120.0f, 0.25f)
/* Berries and fruit, greens, roots, what was roasted. */
#define FRUIT_WATER FOOD_WATER(0.85f, 0.88f, 1.2f, 1500.0f, 0.2f)
#define GREENS_WATER FOOD_WATER(0.85f, 0.9f, 1.2f, 300.0f, 0.15f)
#define ROOT_WATER FOOD_WATER(0.75f, 0.8f, 1.2f, 1500.0f, 0.15f)
#define ROASTED_WATER FOOD_WATER(0.6f, 0.75f, 1.2f, 1000.0f, 0.15f)

static const ItemDef ITEM_DEFS[ITEM_TYPE_COUNT] = {
    /* id, name, category, weight (kg), bulk, nutrition, hit points, quality?, damageable? */
    [ITEM_NONE] = {DEF("none", NULL, ITEM_CATEGORY_COUNT, 0.0f, 0.0f, 0.0f, 0, false, false)},
    [ITEM_WOOD] = {DEF("wood", N_("item.wood"), ITEM_CATEGORY_RESOURCES, 7.0f, 20.0f, 0.0f, 60,
                       true, true),
                   .length_cm = 70, .form = ITEM_FORM_RIGID, .wooden = true,
                   .wood_name_key = N_("item.wood.of_wood"),
                   WATER(0.45f, 0.6f, 1.0f, 1000.0f, 0.22f)},
    [ITEM_STONE_BLOCK] = {DEF("stone_block", N_("item.stone_block"), ITEM_CATEGORY_RESOURCES, 20.0f,
                              20.0f, 0.0f, 150, false, false),
                          .length_cm = 40, .form = ITEM_FORM_RIGID},
    [ITEM_BLACKBERRIES] = {DEF("blackberries", N_("item.blackberries"), ITEM_CATEGORY_FOOD, 0.3f,
                               1.0f, 0.05f, 10, true, true),
                           .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 3.0f, FRUIT_WATER},
    /* Damageable: a cobble used as a hammer wears out. */
    [ITEM_COBBLE] = {DEF("cobble", N_("item.cobble"), ITEM_CATEGORY_RESOURCES, 1.0f, 1.5f, 0.0f,
                         200, true, true),
                     .length_cm = 10, .form = ITEM_FORM_RIGID},
    [ITEM_BRANCHES] = {DEF("branches", N_("item.branches"), ITEM_CATEGORY_RESOURCES, 1.5f, 10.0f,
                           0.0f, 20, true, true),
                       .length_cm = 150, .form = ITEM_FORM_RIGID, .wooden = true,
                       .wood_name_key = N_("item.branches.of_wood"),
                       WATER(0.45f, 0.6f, 1.0f, 240.0f, 0.22f)},
    [ITEM_TWIGS] = {DEF("twigs", N_("item.twigs"), ITEM_CATEGORY_RESOURCES, 0.2f, 2.0f, 0.0f, 10,
                        false, true),
                    .length_cm = 40, .form = ITEM_FORM_BUNDLE,
                    WATER(0.3f, 0.6f, 1.0f, 24.0f, 0.2f)},
    /* Cut fresh it weighs about four times its dry weight, and dries into
     * hay in a few days out of the rain. */
    [ITEM_GRASS] = {DEF("grass", N_("item.grass"), ITEM_CATEGORY_RESOURCES, 1.0f, 25.0f, 0.0f, 15,
                        false, true),
                    .length_cm = 60, .form = ITEM_FORM_BUNDLE,
                    WATER(0.75f, 0.8f, 1.0f, 60.0f, 0.2f)},
    [ITEM_FLINT] = {DEF("flint", N_("item.flint"), ITEM_CATEGORY_RESOURCES, 1.0f, 1.5f, 0.0f, 150,
                        true, false),
                    .length_cm = 12, .form = ITEM_FORM_RIGID},
    [ITEM_CHOPPER] = {DEF("chopper", N_("item.chopper"), ITEM_CATEGORY_TOOLS, 1.0f, 2.0f, 0.0f, 80,
                          true, true),
                      .length_cm = 12, .form = ITEM_FORM_RIGID},
    [ITEM_FLAKE] = {DEF("flake", N_("item.flake"), ITEM_CATEGORY_TOOLS, 0.03f, 0.2f, 0.0f, 15, true,
                        true),
                    .length_cm = 6, .form = ITEM_FORM_RIGID},
    [ITEM_HAND_AXE] = {DEF("hand_axe", N_("item.hand_axe"), ITEM_CATEGORY_TOOLS, 0.6f, 1.5f, 0.0f,
                           120, true, true),
                       .length_cm = 15, .form = ITEM_FORM_RIGID},
    [ITEM_THROWING_SPEAR] = {DEF("throwing_spear", N_("item.throwing_spear"), ITEM_CATEGORY_WEAPONS,
                                 0.8f, 10.0f, 0.0f, 50, true, true),
                             .length_cm = 180, .form = ITEM_FORM_RIGID, .wooden = true,
                             .wood_name_key = N_("item.throwing_spear.of_wood"), TOOL_WATER},
    [ITEM_THRUSTING_SPEAR] = {DEF("thrusting_spear", N_("item.thrusting_spear"),
                                  ITEM_CATEGORY_WEAPONS, 1.5f, 12.0f, 0.0f, 70, true, true),
                              .length_cm = 220, .form = ITEM_FORM_RIGID, .wooden = true,
                              .wood_name_key = N_("item.thrusting_spear.of_wood"), TOOL_WATER},
    [ITEM_THROWING_SPEAR_HARDENED] = {DEF("throwing_spear_hardened",
                                          N_("item.throwing_spear_hardened"), ITEM_CATEGORY_WEAPONS,
                                          0.8f, 10.0f, 0.0f, 75, true, true),
                                      .length_cm = 180, .form = ITEM_FORM_RIGID, .wooden = true,
                                      .wood_name_key = N_("item.throwing_spear_hardened.of_wood"),
                                      TOOL_WATER},
    [ITEM_THRUSTING_SPEAR_HARDENED] =
        {DEF("thrusting_spear_hardened", N_("item.thrusting_spear_hardened"),
             ITEM_CATEGORY_WEAPONS, 1.5f, 12.0f, 0.0f, 105, true, true),
         .length_cm = 220, .form = ITEM_FORM_RIGID, .wooden = true,
         .wood_name_key = N_("item.thrusting_spear_hardened.of_wood"), TOOL_WATER},
    [ITEM_HAZELNUTS] = {DEF("hazelnuts", N_("item.hazelnuts"), ITEM_CATEGORY_FOOD, 0.1f, 0.4f,
                            0.075f, 100, true, true),
                        .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 90.0f,
                        FOOD_WATER(0.2f, 0.3f, 1.0f, 300.0f, 0.12f)},
    [ITEM_MUSHROOMS] = {DEF("mushrooms", N_("item.mushrooms"), ITEM_CATEGORY_FOOD, 0.1f, 0.5f,
                            0.01f, 5, true, true),
                        .length_cm = 8, .form = ITEM_FORM_RIGID, .rot_days = 2.0f,
                        FOOD_WATER(0.9f, 0.92f, 1.2f, 400.0f, 0.15f)},
    [ITEM_WILD_GARLIC] = {DEF("wild_garlic", N_("item.wild_garlic"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f,
                              0.016f, 5, true, true),
                          .length_cm = 25, .form = ITEM_FORM_BUNDLE, .rot_days = 2.0f,
                          GREENS_WATER},
    [ITEM_NETTLES] = {DEF("nettles", N_("item.nettles"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f, 0.016f, 5,
                          true, true),
                      .length_cm = 40, .form = ITEM_FORM_BUNDLE, .rot_days = 2.0f, GREENS_WATER},
    [ITEM_PLANTAIN] = {DEF("plantain", N_("item.plantain"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f, 0.013f,
                           5, true, true),
                       .length_cm = 25, .form = ITEM_FORM_BUNDLE, .rot_days = 2.0f, GREENS_WATER},
    [ITEM_WILD_CARROTS] = {DEF("wild_carrots", N_("item.wild_carrots"), ITEM_CATEGORY_FOOD, 0.25f,
                               1.0f, 0.027f, 30, true, true),
                           .length_cm = 20, .form = ITEM_FORM_RIGID, .rot_days = 20.0f, ROOT_WATER},
    [ITEM_CATTAIL_RHIZOMES] = {DEF("cattail_rhizomes", N_("item.cattail_rhizomes"),
                                   ITEM_CATEGORY_FOOD, 0.25f, 1.0f, 0.05f, 30, true, true),
                               .length_cm = 30, .form = ITEM_FORM_RIGID, .rot_days = 15.0f,
                               ROOT_WATER},
    [ITEM_DIGGING_STICK] = {DEF("digging_stick", N_("item.digging_stick"), ITEM_CATEGORY_TOOLS,
                                1.0f, 6.0f, 0.0f, 40, true, true),
                            .length_cm = 100, .form = ITEM_FORM_RIGID, .wooden = true,
                            .wood_name_key = N_("item.digging_stick.of_wood"), TOOL_WATER},
    [ITEM_DIGGING_STICK_HARDENED] = {DEF("digging_stick_hardened",
                                         N_("item.digging_stick_hardened"), ITEM_CATEGORY_TOOLS,
                                         1.0f, 6.0f, 0.0f, 60, true, true),
                                     .length_cm = 100, .form = ITEM_FORM_RIGID, .wooden = true,
                                     .wood_name_key = N_("item.digging_stick_hardened.of_wood"),
                                     TOOL_WATER},
    [ITEM_ROASTED_RHIZOMES] = {DEF("roasted_rhizomes", N_("item.roasted_rhizomes"),
                                   ITEM_CATEGORY_FOOD, 0.25f, 1.0f, 0.07f, 15, true, true),
                               .length_cm = 30, .form = ITEM_FORM_RIGID, .rot_days = 4.0f,
                               ROASTED_WATER},
    [ITEM_ROASTED_CARROTS] = {DEF("roasted_carrots", N_("item.roasted_carrots"), ITEM_CATEGORY_FOOD,
                                  0.25f, 1.0f, 0.035f, 15, true, true),
                              .length_cm = 20, .form = ITEM_FORM_RIGID, .rot_days = 4.0f,
                              ROASTED_WATER},
    [ITEM_WOOD_SORREL] = {DEF("wood_sorrel", N_("item.wood_sorrel"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f,
                              0.01f, 5, true, true),
                          .length_cm = 15, .form = ITEM_FORM_BUNDLE, .rot_days = 2.0f,
                          GREENS_WATER},
    [ITEM_WILD_STRAWBERRIES] = {DEF("wild_strawberries", N_("item.wild_strawberries"),
                                    ITEM_CATEGORY_FOOD, 0.2f, 0.4f, 0.017f, 5, true, true),
                                .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 2.0f,
                                FRUIT_WATER},
    [ITEM_BILBERRIES] = {DEF("bilberries", N_("item.bilberries"), ITEM_CATEGORY_FOOD, 0.3f, 0.5f,
                             0.045f, 10, true, true),
                         .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 4.0f, FRUIT_WATER},
    /* Dry, with seeds in them: they keep for months. */
    [ITEM_ROSE_HIPS] = {DEF("rose_hips", N_("item.rose_hips"), ITEM_CATEGORY_FOOD, 0.15f, 0.4f,
                            0.06f, 50, true, true),
                        .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 60.0f,
                        FOOD_WATER(0.4f, 0.6f, 1.0f, 300.0f, 0.15f)},
    [ITEM_CHESTNUTS] = {DEF("chestnuts", N_("item.chestnuts"), ITEM_CATEGORY_FOOD, 0.15f, 0.5f,
                            0.065f, 60, true, true),
                        .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 30.0f,
                        FOOD_WATER(0.5f, 0.6f, 1.0f, 300.0f, 0.12f)},
    /* A few small fruit, firm enough to carry in the arms as mushrooms are. */
    [ITEM_APPLES] = {DEF("apples", N_("item.apples"), ITEM_CATEGORY_FOOD, 0.2f, 0.7f, 0.025f, 40,
                         true, true),
                     .length_cm = 6, .form = ITEM_FORM_RIGID, .rot_days = 30.0f, FRUIT_WATER},
    [ITEM_PEARS] = {DEF("pears", N_("item.pears"), ITEM_CATEGORY_FOOD, 0.2f, 0.7f, 0.028f, 30, true,
                        true),
                    .length_cm = 8, .form = ITEM_FORM_RIGID, .rot_days = 14.0f, FRUIT_WATER},
    [ITEM_MOSS] = {DEF("moss", N_("item.moss"), ITEM_CATEGORY_RESOURCES, 0.4f, 20.0f, 0.0f, 10,
                       false, true),
                   .length_cm = 20, .form = ITEM_FORM_BUNDLE,
                   WATER(0.85f, 0.95f, 1.4f, 120.0f, 0.25f)},
    [ITEM_BRACKEN] = {DEF("bracken", N_("item.bracken"), ITEM_CATEGORY_RESOURCES, 1.0f, 25.0f, 0.0f,
                          15, false, true),
                      .length_cm = 100, .form = ITEM_FORM_BUNDLE,
                      WATER(0.8f, 0.85f, 1.0f, 70.0f, 0.2f)},
    [ITEM_HEATHER] = {DEF("heather", N_("item.heather"), ITEM_CATEGORY_RESOURCES, 1.5f, 15.0f, 0.0f,
                          20, false, true),
                      .length_cm = 50, .form = ITEM_FORM_BUNDLE,
                      WATER(0.5f, 0.7f, 1.0f, 80.0f, 0.2f)},
    [ITEM_REEDS] = {DEF("reeds", N_("item.reeds"), ITEM_CATEGORY_RESOURCES, 1.2f, 20.0f, 0.0f, 20,
                        false, true),
                    .length_cm = 200, .form = ITEM_FORM_BUNDLE,
                    WATER(0.6f, 0.75f, 1.0f, 80.0f, 0.2f)},
    /* A sheet about 40 × 60 cm. Pliable fresh (worked within a day or so of
     * peeling) or once softened in water again; brittle dry. Its betulin
     * keeps it from rotting. */
    [ITEM_BIRCH_BARK] = {DEF("birch_bark", N_("item.birch_bark"), ITEM_CATEGORY_RESOURCES, 0.15f,
                             2.0f, 0.0f, 20, true, true),
                         .length_cm = 60, .form = ITEM_FORM_BUNDLE,
                         WATER(0.45f, 0.6f, 1.0f, 36.0f, 0.25f)},
    [ITEM_WOODEN_SCRAPER] = {DEF("wooden_scraper", N_("item.wooden_scraper"), ITEM_CATEGORY_TOOLS,
                                 0.15f, 0.6f, 0.0f, 40, true, true),
                             .length_cm = 20, .form = ITEM_FORM_RIGID, .wooden = true,
                             .wood_name_key = N_("item.wooden_scraper.of_wood"), TOOL_WATER},
    [ITEM_FLINT_BORER] = {DEF("flint_borer", N_("item.flint_borer"), ITEM_CATEGORY_TOOLS, 0.02f,
                              0.1f, 0.0f, 20, true, true),
                          .length_cm = 5, .form = ITEM_FORM_RIGID},
    /* Cut whole after flowering; dried, retted in the dew or in water, then
     * broken and scraped for their fibre (docs/CONTAINERS.md §4.1). */
    [ITEM_NETTLE_STEMS] = {DEF("nettle_stems", N_("item.nettle_stems"), ITEM_CATEGORY_RESOURCES,
                               0.6f, 6.0f, 0.0f, 20, true, true),
                           .length_cm = 120, .form = ITEM_FORM_BUNDLE, .rets = true,
                           WATER(0.75f, 0.85f, 1.0f, 48.0f, 0.2f)},
    [ITEM_NETTLE_FIBRE] = {DEF("nettle_fibre", N_("item.nettle_fibre"), ITEM_CATEGORY_RESOURCES,
                               0.02f, 0.5f, 0.0f, 20, true, true),
                           .length_cm = 60, .form = ITEM_FORM_LIMP,
                           WATER(0.12f, 0.8f, 1.0f, 24.0f, 0.25f)},
    [ITEM_NETTLE_CORD] = {DEF("nettle_cord", N_("item.nettle_cord"), ITEM_CATEGORY_RESOURCES, 0.03f,
                              0.4f, 0.0f, 40, true, true),
                          .length_cm = 15, .form = ITEM_FORM_LIMP, /* A hank wound up. */
                          WATER(0.12f, 0.6f, 1.0f, 24.0f, 0.25f)},
    /* Cut mature and still green (July to September); woven green, or dried
     * and dampened by the weaver (docs/CONTAINERS.md §4.3). */
    [ITEM_CATTAIL_LEAVES] = {DEF("cattail_leaves", N_("item.cattail_leaves"),
                                 ITEM_CATEGORY_RESOURCES, 0.4f, 5.0f, 0.0f, 20, true, true),
                             .length_cm = 150, .form = ITEM_FORM_BUNDLE, .shrinks_green = true,
                             WATER(0.7f, 0.8f, 1.0f, 48.0f, 0.2f)},
    /* Holds about 10 litres, open; a container to carry in from step C4 (a tool
     * until then, as the bark tray). */
    [ITEM_CATTAIL_BASKET] = {DEF("cattail_basket", N_("item.cattail_basket"), ITEM_CATEGORY_TOOLS,
                                 0.4f, 25.0f, 0.0f, 60, true, true),
                             .length_cm = 35, .form = ITEM_FORM_RIGID,
                             WATER(0.12f, 0.6f, 1.0f, 24.0f, 0.25f)},
    /* Holds about 2 litres; a container to carry in from step C4 of
     * docs/CONTAINERS.md (a tool until then). */
    [ITEM_BARK_TRAY] = {DEF("bark_tray", N_("item.bark_tray"), ITEM_CATEGORY_TOOLS, 0.1f, 5.0f,
                            0.0f, 25, true, true),
                        .length_cm = 30, .form = ITEM_FORM_RIGID,
                        WATER(0.15f, 0.5f, 1.0f, 36.0f, 0.25f)},
    [ITEM_SETTLER_BODY] = {DEF("settler_body", N_("item.settler_body"), ITEM_CATEGORY_BODIES, 65.0f,
                               1000.0f, 0.0f, 100, false, true),
                           .length_cm = 170, .form = ITEM_FORM_LIMP, .rot_days = 10.0f},
    [ITEM_ROE_DEER_CARCASS] = {DEF("roe_deer_carcass", N_("item.roe_deer_carcass"),
                                   ITEM_CATEGORY_BODIES, 22.0f, 500.0f, 0.0f, 100, false, true),
                               .length_cm = 120, .form = ITEM_FORM_LIMP, .rot_days = 8.0f},
    [ITEM_RED_DEER_CARCASS] = {DEF("red_deer_carcass", N_("item.red_deer_carcass"),
                                   ITEM_CATEGORY_BODIES, 120.0f, 1000.0f, 0.0f, 100, false, true),
                               .length_cm = 200, .form = ITEM_FORM_LIMP, .rot_days = 10.0f},
    [ITEM_BOAR_CARCASS] = {DEF("boar_carcass", N_("item.boar_carcass"), ITEM_CATEGORY_BODIES, 70.0f,
                               1000.0f, 0.0f, 100, false, true),
                           .length_cm = 140, .form = ITEM_FORM_LIMP, .rot_days = 10.0f},
    [ITEM_WOLF_CARCASS] = {DEF("wolf_carcass", N_("item.wolf_carcass"), ITEM_CATEGORY_BODIES, 35.0f,
                               700.0f, 0.0f, 100, false, true),
                           .length_cm = 130, .form = ITEM_FORM_LIMP, .rot_days = 8.0f},
    [ITEM_FOX_CARCASS] = {DEF("fox_carcass", N_("item.fox_carcass"), ITEM_CATEGORY_BODIES, 6.0f,
                              150.0f, 0.0f, 100, false, true),
                          .length_cm = 100, .form = ITEM_FORM_LIMP, .rot_days = 5.0f},
    [ITEM_HARE_CARCASS] = {DEF("hare_carcass", N_("item.hare_carcass"), ITEM_CATEGORY_BODIES, 3.5f,
                               60.0f, 0.0f, 100, false, true),
                           .length_cm = 60, .form = ITEM_FORM_LIMP, .rot_days = 4.0f},
};

#undef DEF
#undef WATER
#undef FOOD_WATER
#undef TOOL_WATER
#undef FRUIT_WATER
#undef GREENS_WATER
#undef ROOT_WATER
#undef ROASTED_WATER

static const char *const CATEGORY_KEYS[ITEM_CATEGORY_COUNT] = {
    [ITEM_CATEGORY_FOOD] = N_("item.category.food"),
    [ITEM_CATEGORY_RESOURCES] = N_("item.category.resources"),
    [ITEM_CATEGORY_TOOLS] = N_("item.category.tools"),
    [ITEM_CATEGORY_WEAPONS] = N_("item.category.weapons"),
    [ITEM_CATEGORY_BODIES] = N_("item.category.bodies"),
};

const char *item_category_name_key(ItemCategory category)
{
    return (unsigned)category < ITEM_CATEGORY_COUNT ? CATEGORY_KEYS[category] : NULL;
}

ItemTypeMask item_types_list(const ItemType *types, int count)
{
    ItemTypeMask mask = item_types_none();
    for (int i = 0; i < count; i++) {
        item_types_add(&mask, types[i]);
    }
    return mask;
}

ItemTypeMask item_types_tinder(void)
{
    static const ItemType TINDER[] = {ITEM_GRASS, ITEM_BRACKEN, ITEM_MOSS};
    return item_types_list(TINDER, (int)COLONY_ARRAY_COUNT(TINDER));
}

void item_types_serialize(Serializer *s, ItemTypeMask *mask)
{
    /* A word after another: one word is what a single u64 was. */
    for (size_t w = 0; w < ITEM_TYPE_WORDS; w++) {
        ser_u64(s, &mask->words[w]);
    }
    mask->words[ITEM_TYPE_WORDS - 1] &=
        ITEM_TYPE_COUNT % 64 == 0 ? ~UINT64_C(0) : (UINT64_C(1) << (ITEM_TYPE_COUNT % 64)) - 1;
}

uint64_t item_types_hash_more(ItemTypeMask mask, uint64_t hash)
{
    for (size_t w = 1; w < ITEM_TYPE_WORDS; w++) {
        if (mask.words[w] != 0) {
            hash = hash_fnv1a64(hash, &w, sizeof(w));
            hash = hash_fnv1a64(hash, &mask.words[w], sizeof(mask.words[w]));
        }
    }
    return hash;
}

ItemTypeMask item_types_all(void)
{
    ItemTypeMask mask = item_types_none();
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        item_types_add(&mask, (ItemType)type);
    }
    return mask;
}

ItemTypeMask item_types_in_category(ItemCategory category)
{
    ItemTypeMask mask = item_types_none();
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if (ITEM_DEFS[type].category == category) {
            item_types_add(&mask, (ItemType)type);
        }
    }
    return mask;
}

const ItemDef *item_def(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT ? &ITEM_DEFS[type] : NULL;
}

const char *item_form_name_key(ItemForm form)
{
    static const char *const KEYS[ITEM_FORM_COUNT] = {
        [ITEM_FORM_NONE] = NULL,
        [ITEM_FORM_RIGID] = N_("item.form.rigid"),
        [ITEM_FORM_BUNDLE] = N_("item.form.bundle"),
        [ITEM_FORM_LOOSE] = N_("item.form.loose"),
        [ITEM_FORM_LIMP] = N_("item.form.limp"),
    };
    return (unsigned)form < ITEM_FORM_COUNT ? KEYS[form] : NULL;
}

bool item_is_long(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT && ITEM_DEFS[type].length_cm >= ITEM_LONG_CM;
}

bool item_is_food(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT && ITEM_DEFS[type].nutrition > 0.0f;
}

ItemTypeMask item_types_food(void)
{
    ItemTypeMask mask = item_types_none();
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if (item_is_food((ItemType)type)) {
            item_types_add(&mask, type);
        }
    }
    return mask;
}

bool item_rots(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT && ITEM_DEFS[type].rot_days > 0.0f;
}

float item_rot_rate(float celsius, bool sheltered)
{
    if (celsius <= ITEM_ROT_FROZEN_CELSIUS) {
        return 0.0f;
    }
    float rate = exp2f((celsius - ITEM_ROT_REFERENCE_CELSIUS) / ITEM_ROT_DOUBLING_CELSIUS);
    if (rate > ITEM_ROT_RATE_MAX) {
        rate = ITEM_ROT_RATE_MAX;
    }
    return sheltered ? rate * ITEM_ROT_SHELTERED : rate;
}

/* Rot units an item of a rotting type loses per hour at `rate`. */
static float rot_units_per_hour(const ItemDef *def, float rate)
{
    return rate * (float)ITEM_ROT_STEP * (float)def->max_hit_points / (def->rot_days * 24.0f);
}

/* ---- Moisture ---- */

bool item_takes_water(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT && ITEM_DEFS[type].water.room > 0.0f;
}

/* The sorption curve of wood (moisture at equilibrium by relative humidity),
 * which every material follows times its factor. */
static const float SORPTION_HUMIDITY[] = {0.0f, 0.3f, 0.65f, 0.8f, 0.9f, 1.0f};
static const float SORPTION_MOISTURE[] = {0.0f, 0.06f, 0.12f, 0.16f, 0.2f, 0.28f};

float item_equilibrium_moisture(ItemType type, float humidity)
{
    if (!item_takes_water(type)) {
        return 0.0f;
    }
    const ItemWater *water = &ITEM_DEFS[type].water;
    humidity = clampf(humidity, 0.0f, 1.0f);
    int last = (int)(sizeof(SORPTION_HUMIDITY) / sizeof(SORPTION_HUMIDITY[0])) - 1;
    float moisture = SORPTION_MOISTURE[last];
    for (int i = 1; i <= last; i++) {
        if (humidity <= SORPTION_HUMIDITY[i]) {
            float t = (humidity - SORPTION_HUMIDITY[i - 1]) /
                      (SORPTION_HUMIDITY[i] - SORPTION_HUMIDITY[i - 1]);
            moisture =
                SORPTION_MOISTURE[i - 1] + t * (SORPTION_MOISTURE[i] - SORPTION_MOISTURE[i - 1]);
            break;
        }
    }
    moisture *= water->equilibrium;
    return moisture < water->room ? moisture : water->room;
}

float item_fresh_moisture(ItemType type)
{
    return item_takes_water(type) ? ITEM_DEFS[type].water.fresh : 0.0f;
}

float item_reference_moisture(ItemType type)
{
    if (!item_takes_water(type)) {
        return 0.0f;
    }
    return ITEM_DEFS[type].water.weighed_fresh
               ? ITEM_DEFS[type].water.fresh
               : item_equilibrium_moisture(type, ITEM_MOISTURE_REFERENCE_HUMIDITY);
}

/* The stored form of a moisture (ItemStack.moisture): 1 + units. */
static uint16_t moisture_code(ItemType type, float moisture)
{
    if (!item_takes_water(type)) {
        return 0;
    }
    float room = ITEM_DEFS[type].water.room;
    moisture = clampf(moisture, 0.0f, room);
    return (uint16_t)(1 + lroundf(moisture * (float)ITEM_MOISTURE_UNITS));
}

/* A moisture code made valid for its type: unknown (0) is the reference. */
static uint16_t valid_moisture(ItemType type, uint16_t code)
{
    if (!item_takes_water(type)) {
        return 0;
    }
    if (code == 0) {
        return moisture_code(type, item_reference_moisture(type));
    }
    int most = 1 + (int)lroundf(ITEM_DEFS[type].water.room * (float)ITEM_MOISTURE_UNITS);
    return (uint16_t)clampi(code, 1, most);
}

static float moisture_of(ItemType type, uint16_t code)
{
    if (!item_takes_water(type)) {
        return 0.0f;
    }
    if (code == 0) {
        return item_reference_moisture(type);
    }
    return (float)(code - 1) / (float)ITEM_MOISTURE_UNITS;
}

float item_moisture(const Item *item)
{
    return moisture_of((ItemType)item->type, item->moisture);
}

float item_stack_moisture(ItemStack stack)
{
    return moisture_of((ItemType)stack.type, stack.moisture);
}

float item_stack_retting(ItemStack stack)
{
    return (float)stack.retting / (float)ITEM_RETTING_UNITS;
}

void item_stack_set_retting(ItemStack *stack, float retting)
{
    if ((unsigned)stack->type >= ITEM_TYPE_COUNT || !ITEM_DEFS[stack->type].rets) {
        stack->retting = 0;
        return;
    }
    stack->retting =
        (uint16_t)lroundf(clampf(retting, 0.0f, ITEM_RETTING_MAX) * (float)ITEM_RETTING_UNITS);
}

int item_retting_cap_bonus(float retting)
{
    if (retting <= 0.0f) {
        return 0;
    }
    float bonus = (float)ITEM_RETTING_CAP_BONUS * (retting < 1.0f ? retting : 1.0f);
    if (retting > ITEM_RETTING_OVER) {
        bonus -=
            (float)ITEM_RETTING_OVER_MALUS * (retting - ITEM_RETTING_OVER) / ITEM_RETTING_OVER_STEP;
    }
    return (int)lroundf(bonus);
}

float item_retting_work_factor(float retting)
{
    return 1.0f + clampf(retting, 0.0f, 1.0f);
}

ItemRettingState item_retting_state(float retting)
{
    if (retting <= 0.0f) {
        return ITEM_RETTING_NONE;
    }
    if (retting < 1.0f) {
        return ITEM_RETTING_UNDER_WAY;
    }
    return retting <= ITEM_RETTING_OVER ? ITEM_RETTING_DONE : ITEM_RETTING_OVERDONE;
}

void item_stack_set_moisture(ItemStack *stack, float moisture)
{
    stack->moisture = moisture_code((ItemType)stack->type, moisture);
}

void item_stack_set_fresh(ItemStack *stack)
{
    item_stack_set_moisture(stack, item_fresh_moisture((ItemType)stack->type));
}

bool item_moisture_dry(ItemType type, float moisture)
{
    return !item_takes_water(type) || moisture < ITEM_DEFS[type].water.dry_below;
}

/* Soaked within this much of its room. */
#define SOAKED_MARGIN 0.03f

ItemMoistureState item_moisture_state(ItemType type, float moisture)
{
    if (!item_takes_water(type)) {
        return ITEM_MOISTURE_NONE;
    }
    if (item_moisture_dry(type, moisture)) {
        return ITEM_MOISTURE_DRY;
    }
    return moisture >= ITEM_DEFS[type].water.room - SOAKED_MARGIN ? ITEM_MOISTURE_SOAKED
                                                                  : ITEM_MOISTURE_WET;
}

bool item_moisture_green(ItemType type, float moisture)
{
    if (!item_takes_water(type) || item_moisture_dry(type, moisture)) {
        return false;
    }
    float settled = item_reference_moisture(type), fresh = ITEM_DEFS[type].water.fresh;
    return moisture >= settled + ITEM_GREEN_SHARE * (fresh - settled);
}

bool item_moisture_fits(ItemType type, float moisture, ItemMoistureWant want)
{
    switch (want) {
    case ITEM_WANT_ANY: return true;
    case ITEM_WANT_DRY: return item_moisture_dry(type, moisture);
    case ITEM_WANT_GREEN: return item_moisture_green(type, moisture);
    case ITEM_WANT_WET: {
        ItemMoistureState state = item_moisture_state(type, moisture);
        return state == ITEM_MOISTURE_WET || state == ITEM_MOISTURE_SOAKED;
    }
    case ITEM_WANT_COUNT: break;
    }
    return false;
}

const char *item_moisture_state_key(ItemMoistureState state)
{
    switch (state) {
    case ITEM_MOISTURE_DRY: return N_("item.moisture.state.dry");
    case ITEM_MOISTURE_WET: return N_("item.moisture.state.wet");
    case ITEM_MOISTURE_SOAKED: return N_("item.moisture.state.soaked");
    default: return NULL;
    }
}

float item_moisture_weight_factor(ItemType type, float moisture)
{
    if (!item_takes_water(type)) {
        return 1.0f;
    }
    float room = ITEM_DEFS[type].water.room;
    moisture = clampf(moisture, 0.0f, room);
    return (1.0f - item_reference_moisture(type)) / (1.0f - moisture);
}

float item_rot_moisture_factor(ItemType type, float moisture)
{
    if (!item_takes_water(type)) {
        return 1.0f;
    }
    const ItemWater *water = &ITEM_DEFS[type].water;
    if (moisture > water->fresh) {
        float span = water->room - water->fresh;
        float t = span > 0.0f ? clampf((moisture - water->fresh) / span, 0.0f, 1.0f) : 1.0f;
        return 1.0f + t * (ITEM_ROT_RAINED_ON - 1.0f);
    }
    float halfway = 0.5f * (water->dry_below + water->fresh);
    if (moisture >= halfway) {
        return 1.0f;
    }
    if (moisture <= water->dry_below || halfway <= water->dry_below) {
        return ITEM_ROT_DRY;
    }
    float t = (moisture - water->dry_below) / (halfway - water->dry_below);
    return ITEM_ROT_DRY + t * (1.0f - ITEM_ROT_DRY);
}

float item_drying_speed(float celsius, float sun)
{
    float speed = (1.0f + ITEM_DRYING_SUN * clampf(sun, 0.0f, 1.0f)) *
                  exp2f((celsius - ITEM_DRYING_REFERENCE_CELSIUS) / ITEM_DRYING_DOUBLING_CELSIUS);
    if (celsius <= 0.0f && speed > ITEM_DRYING_FROZEN) {
        speed = ITEM_DRYING_FROZEN; /* Its water is ice. */
    }
    return clampf(speed, 0.02f, 8.0f);
}

float item_moisture_target(ItemType type, ItemConditions here)
{
    if (!item_takes_water(type)) {
        return 0.0f;
    }
    if (here.in_water || here.wetting > 0.0f) {
        return ITEM_DEFS[type].water.room;
    }
    return item_equilibrium_moisture(type, here.humidity);
}

/* Hours an item is soaked through in, lying in water. */
#define SOAK_HOURS 0.25f
/* Rain wets nothing faster than this. */
#define WETTING_MIN_HOURS 0.5f

float item_moisture_hours(ItemType type, float moisture, ItemConditions here)
{
    if (!item_takes_water(type)) {
        return -1.0f;
    }
    float hours = ITEM_DEFS[type].water.dry_hours;
    if (here.in_water) {
        return SOAK_HOURS;
    }
    if (here.wetting > 0.0f) {
        float wet = hours / (ITEM_WETTING_SPEED * here.wetting);
        return wet > WETTING_MIN_HOURS ? wet : WETTING_MIN_HOURS;
    }
    if (item_moisture_target(type, here) < moisture) {
        return hours / (here.drying > 0.02f ? here.drying : 0.02f);
    }
    return hours; /* Damp air soaks in at its own pace, sun or not. */
}

float item_hours_to_dry(ItemType type, float moisture, ItemConditions here)
{
    if (item_moisture_dry(type, moisture)) {
        return 0.0f;
    }
    float dry = ITEM_DEFS[type].water.dry_below;
    float target = item_moisture_target(type, here);
    if (target >= dry - 1e-4f) {
        return -1.0f;
    }
    return item_moisture_hours(type, moisture, here) * logf((moisture - target) / (dry - target));
}

float item_stack_rot_hours(ItemStack stack, float rate)
{
    rate *= item_rot_moisture_factor((ItemType)stack.type, item_stack_moisture(stack));
    if (stack.type == ITEM_NONE || !item_rots((ItemType)stack.type) || rate <= 0.0f) {
        return -1.0f;
    }
    float left = (float)stack.hit_points * (float)ITEM_ROT_STEP - (float)stack.progress;
    return left / rot_units_per_hour(&ITEM_DEFS[stack.type], rate);
}

int item_stack_limit(ItemType type)
{
    if (type == ITEM_NONE || (unsigned)type >= ITEM_TYPE_COUNT) {
        return 0;
    }
    float limit = ITEM_TILE_BULK / ITEM_DEFS[type].bulk;
    return limit < 1.0f ? 1 : (limit > (float)UINT16_MAX ? UINT16_MAX : (int)limit);
}

float item_stack_weight(ItemType type, int count)
{
    const ItemDef *def = item_def(type);
    return def != NULL ? def->weight * (float)count : 0.0f;
}

float item_stack_mass(ItemStack stack)
{
    const ItemDef *def = item_def((ItemType)stack.type);
    if (def == NULL || stack.type == ITEM_NONE) {
        return 0.0f;
    }
    float whole =
        def->weight * (float)stack.count * (def->wooden ? wood_weight(stack.woods) : 1.0f);
    whole *= item_moisture_weight_factor((ItemType)stack.type, item_stack_moisture(stack));
    return whole * (1.0f - (float)stack.eaten / (float)ITEM_EATEN_FULL);
}

static bool is_item_type(ItemType type)
{
    return type != ITEM_NONE && (unsigned)type < ITEM_TYPE_COUNT;
}

ItemStack item_stack_make(ItemType type, int count, int quality, int hit_points)
{
    if (!is_item_type(type) || count <= 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    const ItemDef *def = &ITEM_DEFS[type];
    if (count > UINT16_MAX) {
        count = UINT16_MAX;
    }
    return (ItemStack){
        .type = (uint8_t)type,
        .quality =
            (uint8_t)(def->has_quality ? clampi(quality, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST)
                                       : ITEM_QUALITY_BEST),
        .count = (uint16_t)count,
        .hit_points = (uint16_t)(def->damageable ? clampi(hit_points, 1, def->max_hit_points)
                                                 : def->max_hit_points),
    };
}

/* Rot progress made valid for a type: 0 when it does not rot. */
static uint16_t valid_progress(ItemType type, int progress)
{
    if (ITEM_DEFS[type].rot_days > 0.0f) {
        return (uint16_t)clampi(progress, 0, ITEM_ROT_STEP - 1);
    }
    return 0;
}

ItemStack item_stack_pristine(ItemType type, int count)
{
    const ItemDef *def = item_def(type);
    return item_stack_make(type, count, ITEM_QUALITY_BEST, def != NULL ? def->max_hit_points : 0);
}

float item_stack_eat(ItemStack *stack, float kg)
{
    const ItemDef *def = item_def((ItemType)stack->type);
    if (def == NULL || stack->type == ITEM_NONE || stack->count == 0 || kg <= 0.0f) {
        return 0.0f;
    }
    float whole = def->weight * (float)stack->count;
    float left = item_stack_mass(*stack);
    if (kg >= left) {
        *stack = (ItemStack){.type = ITEM_NONE};
        return left;
    }
    int steps = (int)lroundf(kg / whole * (float)ITEM_EATEN_FULL);
    steps = steps > 1 ? steps : 1;
    int eaten = (int)stack->eaten + steps;
    if (eaten >= ITEM_EATEN_FULL) {
        *stack = (ItemStack){.type = ITEM_NONE};
        return left;
    }
    stack->eaten = (uint16_t)eaten;
    return kg;
}

bool item_stacks_match(ItemStack a, ItemStack b)
{
    return a.type == b.type && a.corpse == b.corpse && a.woods == b.woods &&
           (a.forbidden != 0) == (b.forbidden != 0);
}

int item_hit_points_at_percent(ItemType type, int percent)
{
    if (!is_item_type(type)) {
        return 0;
    }
    int max = ITEM_DEFS[type].max_hit_points;
    int hit_points = (max * clampi(percent, 0, 100) + 50) / 100;
    return clampi(hit_points, 1, max);
}

int item_hit_points_lasting(ItemType type, float hours)
{
    if (!is_item_type(type)) {
        return 0;
    }
    int max = ITEM_DEFS[type].max_hit_points;
    if (!item_rots(type)) {
        return max;
    }
    float hit_points = ceilf(hours * (float)max / (ITEM_DEFS[type].rot_days * 24.0f));
    return hit_points < 1.0f ? 1 : (hit_points > (float)max ? max : (int)hit_points);
}

int item_stack_hit_points_percent(ItemStack stack)
{
    if (!is_item_type((ItemType)stack.type) || stack.count == 0) {
        return 0;
    }
    return (int)stack.hit_points * 100 / ITEM_DEFS[stack.type].max_hit_points;
}

/* Average of two values weighted by item counts, rounded to the nearest:
 * always between the two values. */
static int weighted_average(int a, int count_a, int b, int count_b)
{
    long total = (long)count_a + count_b;
    return (int)(((long)a * count_a + (long)b * count_b + total / 2) / total);
}

bool item_stack_merge(ItemStack *into, ItemStack from)
{
    if (from.type == ITEM_NONE || from.count == 0) {
        return true;
    }
    if (into->type == ITEM_NONE || into->count == 0) {
        *into = from;
        return true;
    }
    if (!item_stacks_match(*into, from)) {
        return false;
    }
    int count = (int)into->count + (int)from.count;
    if (count > UINT16_MAX) {
        count = UINT16_MAX;
    }
    into->quality = (uint8_t)weighted_average(into->quality, into->count, from.quality, from.count);
    into->hit_points =
        (uint16_t)weighted_average(into->hit_points, into->count, from.hit_points, from.count);
    into->progress =
        (uint16_t)weighted_average(into->progress, into->count, from.progress, from.count);
    into->eaten = (uint16_t)weighted_average(into->eaten, into->count, from.eaten, from.count);
    ItemType type = (ItemType)into->type;
    if (item_takes_water(type)) {
        float a = item_stack_moisture(*into), b = item_stack_moisture(from);
        float total = (float)into->count + (float)from.count;
        item_stack_set_moisture(into, (a * (float)into->count + b * (float)from.count) / total);
    }
    into->retting =
        (uint16_t)weighted_average(into->retting, into->count, from.retting, from.count);
    into->count = (uint16_t)count;
    return true;
}

/* ---- The pool of items ---- */

#define ITEM_POOL_FIRST_CAPACITY 1024u

static Item *slot_item(const Items *items, uint32_t slot_plus_one)
{
    return slot_plus_one != 0 ? &items->pool[slot_plus_one - 1] : NULL;
}

/* A free slot (slot + 1), 0 when out of memory. */
static uint32_t alloc_slot(Items *items)
{
    if (items->free_slots != 0) {
        uint32_t slot = items->free_slots;
        items->free_slots = items->pool[slot - 1].next;
        return slot;
    }
    if (items->pool_used == items->pool_capacity) {
        uint32_t capacity =
            items->pool_capacity != 0 ? items->pool_capacity * 2u : ITEM_POOL_FIRST_CAPACITY;
        if (capacity <= items->pool_capacity || capacity > UINT32_MAX / 2u) {
            return 0;
        }
        Item *pool = realloc(items->pool, (size_t)capacity * sizeof(*pool));
        if (pool == NULL) {
            return 0;
        }
        items->pool = pool;
        items->pool_capacity = capacity;
    }
    uint32_t slot = ++items->pool_used;
    items->pool[slot - 1].generation = 0;
    return slot;
}

static void free_slot(Items *items, uint32_t slot)
{
    Item *item = &items->pool[slot - 1];
    uint16_t generation = (uint16_t)(item->generation + 1u);
    *item = (Item){.generation = generation, .next = items->free_slots};
    items->free_slots = slot;
}

/* A well mixed 32-bit value of two (splitmix64's finalizer). */
static uint32_t mix(uint64_t a, uint64_t b)
{
    uint64_t z = a * 0x9E3779B97F4A7C15u + b + 0x632BE59BD9B4E5B9u;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
    return (uint32_t)((z ^ (z >> 31)) >> 16);
}

/* Spots tried for an item dropped on a tile with few items. */
#define ITEM_SPOT_CANDIDATES 4
/* With more items than this on the tile, an item takes any spot (a heap). */
#define ITEM_SPOT_CROWD 12
/* Spots stay this far from the edge of the tile, in 1/256 of a tile. */
#define ITEM_SPOT_MARGIN 40

/* Chooses where on its tile a new item lies, and how it is turned: from a
 * hash of the tile and the stamp clock, never from a random stream, so that
 * the way items look never changes what happens. With few items there, the
 * candidate spot farthest from them wins. */
static void place_item(const Items *items, Item *item, size_t index, uint32_t salt)
{
    uint32_t seed = mix(index, ((uint64_t)items->stamp_clock << 20) ^ salt);
    item->angle = (uint8_t)seed;
    const int span = 256 - 2 * ITEM_SPOT_MARGIN;
    int crowd = 0;
    for (const Item *other = slot_item(items, items->heads[index]); other != NULL;
         other = slot_item(items, other->next)) {
        crowd++;
    }
    int candidates = crowd == 0 || crowd > ITEM_SPOT_CROWD ? 1 : ITEM_SPOT_CANDIDATES;
    long best = -1;
    for (int c = 0; c < candidates; c++) {
        uint32_t h = mix(seed, (uint64_t)c + 1u);
        int sx = ITEM_SPOT_MARGIN + (int)(h & 0xFFFFu) % span;
        int sy = ITEM_SPOT_MARGIN + (int)(h >> 16) % span;
        long nearest = LONG_MAX;
        if (candidates > 1) {
            for (const Item *other = slot_item(items, items->heads[index]); other != NULL;
                 other = slot_item(items, other->next)) {
                long dx = sx - other->spot_x;
                long dy = sy - other->spot_y;
                long distance = dx * dx + dy * dy;
                nearest = distance < nearest ? distance : nearest;
            }
        }
        if (nearest > best) {
            best = nearest;
            item->spot_x = (uint8_t)sx;
            item->spot_y = (uint8_t)sy;
        }
    }
}

/* ---- Tiles ---- */

static bool in_bounds(const Items *items, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < items->width && y < items->height && z < items->depth;
}

static size_t index_of(const Items *items, int x, int y, int z)
{
    return ((size_t)z * (size_t)items->height + (size_t)y) * (size_t)items->width + (size_t)x;
}

static size_t level_of(const Items *items, size_t index)
{
    return index / ((size_t)items->width * (size_t)items->height);
}

/* Whether an item belongs to the same stack as the summary: same type, same
 * dead one, same wood species, forbidden or not alike. */
static bool item_matches(const Item *item, const ItemStack *stack)
{
    return item->type == stack->type && item->corpse == stack->corpse &&
           item->woods == stack->woods && (item->forbidden != 0) == (stack->forbidden != 0);
}

/* Counts an item lying on its tile in (sign 1) or out (sign -1) of the
 * totals. */
static void count_item(Items *items, const Item *item, int sign)
{
    items->totals[item->type] += sign;
    items->item_count += sign;
}

/* The items of the tile at `index` that belong with `key` (same type, same
 * dead one, forbidden or not alike) as a stack: their count and average condition. An
 * empty stack when there are none. */
static ItemStack group_of(const Items *items, size_t index, ItemStack key)
{
    long count = 0, quality = 0, hit_points = 0, progress = 0, eaten = 0, moisture = 0;
    long retting = 0;
    for (const Item *item = slot_item(items, items->heads[index]); item != NULL;
         item = slot_item(items, item->next)) {
        if (item_matches(item, &key)) {
            count++;
            quality += item->quality;
            hit_points += item->hit_points;
            progress += item->progress;
            eaten += item->eaten;
            moisture += item->moisture;
            retting += item->retting;
        }
    }
    if (count == 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return (ItemStack){.type = key.type,
                       .corpse = key.corpse,
                       .woods = key.woods,
                       .forbidden = key.forbidden != 0,
                       .count = (uint16_t)(count < UINT16_MAX ? count : UINT16_MAX),
                       .quality = (uint8_t)((quality + count / 2) / count),
                       .hit_points = (uint16_t)((hit_points + count / 2) / count),
                       .progress = (uint16_t)((progress + count / 2) / count),
                       .eaten = (uint16_t)((eaten + count / 2) / count),
                       .moisture = (uint16_t)((moisture + count / 2) / count),
                       .retting = (uint16_t)((retting + count / 2) / count)};
}

/* An item as the key of its group. */
static ItemStack key_of(const Item *item)
{
    return (ItemStack){.type = item->type,
                       .corpse = item->corpse,
                       .woods = item->woods,
                       .forbidden = item->forbidden != 0,
                       .count = 1};
}

/* What the items of the tile at `index` are forbidden as (Items.forbidden). */
static uint8_t tile_forbidden(const Items *items, size_t index)
{
    bool some = false, all = true;
    for (const Item *item = slot_item(items, items->heads[index]); item != NULL;
         item = slot_item(items, item->next)) {
        some |= item->forbidden != 0;
        all &= item->forbidden != 0;
    }
    return !some ? 0 : all ? ITEM_TILE_ALL_FORBIDDEN : ITEM_TILE_SOME_FORBIDDEN;
}

/* Rebuilds the summary of a tile from its items, whether it holds other
 * groups, whether they are forbidden, and the count of tiles with items on
 * its level. */
static void refresh_tile(Items *items, size_t index)
{
    ItemStack *stack = &items->stacks[index];
    bool had = stack->type != ITEM_NONE;
    const Item *head = slot_item(items, items->heads[index]);
    bool mixed = false;
    if (head == NULL) {
        *stack = (ItemStack){.type = ITEM_NONE};
    } else {
        *stack = group_of(items, index, key_of(head));
        for (const Item *item = head; item != NULL && !mixed; item = slot_item(items, item->next)) {
            mixed = !item_matches(item, stack);
        }
    }
    items->mixed[index] = mixed;
    bool has = stack->type != ITEM_NONE;
    if (had != has) {
        items->level_stacks[level_of(items, index)] += has ? 1u : (uint32_t)-1;
    }
    /* Forbidden items share no group with allowed ones: an unmixed tile is
     * forbidden as its summary is. */
    uint8_t forbidden =
        !mixed ? (stack->forbidden ? ITEM_TILE_ALL_FORBIDDEN : 0) : tile_forbidden(items, index);
    items->forbidden_count += (forbidden != 0) - (items->forbidden[index] != 0);
    items->forbidden[index] = forbidden;
}

/* The newest group of the tile at `index` among `types`, only of allowed
 * items when `allowed` (an empty stack for none). */
static ItemStack newest_of(const Items *items, size_t index, ItemTypeMask types, bool allowed)
{
    const ItemStack *stack = &items->stacks[index];
    if (stack->type == ITEM_NONE) {
        return (ItemStack){.type = ITEM_NONE};
    }
    if ((item_types_has(types, stack->type)) && !(allowed && stack->forbidden)) {
        return *stack;
    }
    if (!items->mixed[index]) {
        return (ItemStack){.type = ITEM_NONE};
    }
    for (const Item *item = slot_item(items, items->heads[index]); item != NULL;
         item = slot_item(items, item->next)) {
        if ((item_types_has(types, item->type)) && !(allowed && item->forbidden)) {
            return group_of(items, index, key_of(item));
        }
    }
    return (ItemStack){.type = ITEM_NONE};
}

/* The group of the tile at `index` that `key` belongs with (an empty stack
 * for none). */
static ItemStack matching_group(const Items *items, size_t index, ItemStack key)
{
    const ItemStack *stack = &items->stacks[index];
    if (stack->type != ITEM_NONE && item_stacks_match(*stack, key)) {
        return *stack;
    }
    return items->mixed[index] ? group_of(items, index, key) : (ItemStack){.type = ITEM_NONE};
}

/* Takes the item `slot` off the tile it lies on (`previous` is the item
 * before it in the list, 0 when it is the first) and frees it. */
static void unlink_item(Items *items, size_t index, uint32_t previous, uint32_t slot)
{
    Item *item = &items->pool[slot - 1];
    if (previous == 0) {
        items->heads[index] = item->next;
    } else {
        items->pool[previous - 1].next = item->next;
    }
    count_item(items, item, -1);
    free_slot(items, slot);
}

/* Frees every item of a tile. */
static void clear_tile(Items *items, size_t index)
{
    while (items->heads[index] != 0) {
        unlink_item(items, index, 0, items->heads[index]);
    }
}

void items_copy_tile(Items *dst, const Items *src, size_t index)
{
    if (dst->stamps[index] == src->stamps[index]) {
        return;
    }
    clear_tile(dst, index);
    /* Same order as in src: append behind the last copied item. */
    uint32_t last = 0;
    for (const Item *item = items_first(src, index); item != NULL; item = items_next(src, item)) {
        uint32_t slot = alloc_slot(dst);
        if (slot == 0) {
            break; /* Out of memory: the picture misses items. */
        }
        Item *copy = &dst->pool[slot - 1];
        uint16_t generation = copy->generation;
        *copy = *item;
        copy->generation = generation;
        copy->next = 0;
        if (last == 0) {
            dst->heads[index] = slot;
        } else {
            dst->pool[last - 1].next = slot;
        }
        last = slot;
        count_item(dst, copy, 1);
    }
    refresh_tile(dst, index);
    dst->stamps[index] = src->stamps[index];
}

bool items_init(Items *items, int width, int height, int depth)
{
    memset(items, 0, sizeof(*items));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    size_t tiles = (size_t)width * (size_t)height * (size_t)depth;
    items->stacks = calloc(tiles, sizeof(*items->stacks));
    items->level_stacks = calloc((size_t)depth, sizeof(*items->level_stacks));
    items->forbidden = calloc(tiles, 1);
    items->heads = calloc(tiles, sizeof(*items->heads));
    items->stamps = calloc(tiles, sizeof(*items->stamps));
    items->mixed = calloc(tiles, 1);
    if (items->stacks == NULL || items->level_stacks == NULL || items->forbidden == NULL ||
        items->heads == NULL || items->stamps == NULL || items->mixed == NULL) {
        items_free(items);
        return false;
    }
    items->width = width;
    items->height = height;
    items->depth = depth;
    return true;
}

void items_free(Items *items)
{
    free(items->stacks);
    free(items->level_stacks);
    free(items->forbidden);
    free(items->pool);
    free(items->heads);
    free(items->stamps);
    free(items->mixed);
    memset(items, 0, sizeof(*items));
}

ItemStack items_get(const Items *items, int x, int y, int z)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return items->stacks[index_of(items, x, y, z)];
}

ItemStack items_get_allowed(const Items *items, int x, int y, int z)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    size_t index = index_of(items, x, y, z);
    if (items->forbidden_count == 0 || items->forbidden[index] == 0) {
        return items->stacks[index];
    }
    return newest_of(items, index, item_types_all(), true);
}

ItemStack items_get_of(const Items *items, int x, int y, int z, ItemTypeMask types)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return newest_of(items, index_of(items, x, y, z), types, false);
}

ItemStack items_get_allowed_of(const Items *items, int x, int y, int z, ItemTypeMask types)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return newest_of(items, index_of(items, x, y, z), types, true);
}

ItemStack items_get_group(const Items *items, int x, int y, int z, ItemStack group)
{
    if (!in_bounds(items, x, y, z) || group.type == ITEM_NONE) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return matching_group(items, index_of(items, x, y, z), group);
}

bool items_mixed(const Items *items, int x, int y, int z)
{
    return in_bounds(items, x, y, z) && items->mixed[index_of(items, x, y, z)] != 0;
}

int items_groups(const Items *items, int x, int y, int z, ItemStack *out, int capacity)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    if (items->stacks[index].type == ITEM_NONE) {
        return 0;
    }
    if (!items->mixed[index] || capacity <= 1) {
        if (capacity > 0) {
            out[0] = items->stacks[index];
        }
        return capacity > 0 ? 1 : 0;
    }
    /* One pass: each item added to its group's sums (groups beyond
     * `capacity` are left out). */
    enum { MOST = 16 };
    long sums[MOST][7] = {{0}};
    capacity = capacity < MOST ? capacity : MOST;
    int count = 0;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        int group = 0;
        while (group < count && !item_matches(item, &out[group])) {
            group++;
        }
        if (group == count) {
            if (count == capacity) {
                continue;
            }
            out[count++] = key_of(item);
        }
        long *sum = sums[group];
        sum[0]++;
        sum[1] += item->quality;
        sum[2] += item->hit_points;
        sum[3] += item->progress;
        sum[4] += item->eaten;
        sum[5] += item->moisture;
        sum[6] += item->retting;
    }
    for (int group = 0; group < count; group++) {
        const long *sum = sums[group];
        long n = sum[0];
        out[group].count = (uint16_t)(n < UINT16_MAX ? n : UINT16_MAX);
        out[group].quality = (uint8_t)((sum[1] + n / 2) / n);
        out[group].hit_points = (uint16_t)((sum[2] + n / 2) / n);
        out[group].progress = (uint16_t)((sum[3] + n / 2) / n);
        out[group].eaten = (uint16_t)((sum[4] + n / 2) / n);
        out[group].moisture = (uint16_t)((sum[5] + n / 2) / n);
        out[group].retting = (uint16_t)((sum[6] + n / 2) / n);
    }
    return count;
}

const Item *items_first(const Items *items, size_t index)
{
    return slot_item(items, items->heads[index]);
}

const Item *items_next(const Items *items, const Item *item)
{
    return slot_item(items, item->next);
}

int items_count_at(const Items *items, int x, int y, int z, ItemType type)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    int count = 0;
    for (const Item *item = items_first(items, index_of(items, x, y, z)); item != NULL;
         item = items_next(items, item)) {
        count += type == ITEM_NONE || item->type == type;
    }
    return count;
}

bool items_forbidden(const Items *items, int x, int y, int z)
{
    return items->forbidden_count > 0 && in_bounds(items, x, y, z) &&
           items->forbidden[index_of(items, x, y, z)] == ITEM_TILE_ALL_FORBIDDEN;
}

bool items_any_forbidden(const Items *items, int x, int y, int z)
{
    return items->forbidden_count > 0 && in_bounds(items, x, y, z) &&
           items->forbidden[index_of(items, x, y, z)] != 0;
}

/* Notes that the items of a tile (an index) changed: a new stamp, a summary
 * rebuilt (and whether they are forbidden), and copied into the picture at
 * once where it is in view. */
static void note_change(Items *items, size_t index)
{
    refresh_tile(items, index);
    items->stamp_clock++; /* Also salts the spots of the next items. */
    items->stamps[index] = items->picture ? UINT32_MAX : items->stamp_clock;
    if (items->mirror != NULL && (items->mirror_when[index >> 6] >> (index & 63) & 1u) != 0) {
        items_copy_tile(items->mirror, items, index);
    }
}

bool items_set_forbidden(Items *items, int x, int y, int z, bool forbidden)
{
    if (!in_bounds(items, x, y, z)) {
        return false;
    }
    size_t index = index_of(items, x, y, z);
    bool changed = false;
    for (uint32_t slot = items->heads[index]; slot != 0; slot = items->pool[slot - 1].next) {
        Item *item = &items->pool[slot - 1];
        changed |= (item->forbidden != 0) != forbidden;
        item->forbidden = forbidden;
    }
    if (!changed) {
        return false;
    }
    note_change(items, index);
    if (!forbidden) {
        items->additions++; /* Something to store again. */
    }
    return true;
}

bool items_set_item_forbidden(Items *items, ItemId id, bool forbidden)
{
    const Item *found = items_by_id(items, id);
    if (found == NULL || (found->forbidden != 0) == forbidden) {
        return false;
    }
    items->pool[id.slot].forbidden = forbidden;
    note_change(items, found->tile);
    if (!forbidden) {
        items->additions++; /* Something to store again. */
    }
    return true;
}

/* A stack made valid for its type, as items_add() keeps it: condition
 * clamped, progress, eaten and corpse only where the type has them; never a
 * load. */
static ItemStack valid_batch(ItemStack batch)
{
    ItemType type = (ItemType)batch.type;
    if (!is_item_type(type) || batch.count == 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    uint16_t progress = valid_progress(type, batch.progress);
    int eaten = ITEM_DEFS[type].category == ITEM_CATEGORY_BODIES ? batch.eaten : 0;
    uint16_t corpse = ITEM_DEFS[type].category == ITEM_CATEGORY_BODIES ? batch.corpse : 0;
    ItemStack valid = item_stack_make(type, batch.count, batch.quality, batch.hit_points);
    valid.progress = progress;
    valid.eaten = (uint16_t)clampi(eaten, 0, ITEM_EATEN_FULL - 1);
    valid.corpse = corpse;
    valid.woods = ITEM_DEFS[type].wooden ? batch.woods : 0;
    valid.forbidden = batch.forbidden != 0;
    valid.made = batch.made;
    valid.maker = batch.maker;
    valid.moisture = valid_moisture(type, batch.moisture);
    valid.retting = ITEM_DEFS[type].rets ? batch.retting : 0;
    return valid;
}

/* How many items of a stack the tile at `index` takes: the type's stack
 * limit, less what lies there already (items of other types too, by their
 * bulk). May be negative. */
static int tile_room(const Items *items, size_t index, const ItemStack *batch)
{
    float other_bulk = 0.0f;
    int own = 0;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        if (item_matches(item, batch)) {
            own++;
        } else {
            other_bulk += ITEM_DEFS[item->type].bulk;
        }
    }
    ItemType type = (ItemType)batch->type;
    return item_stack_limit(type) - own - (int)ceilf(other_bulk / ITEM_DEFS[type].bulk);
}

int items_room(const Items *items, int x, int y, int z, ItemStack batch)
{
    if (!in_bounds(items, x, y, z) || !is_item_type((ItemType)batch.type)) {
        return 0;
    }
    int room = tile_room(items, index_of(items, x, y, z), &batch);
    return room > 0 ? room : 0;
}

/* A new item of a valid stack's condition, not linked anywhere yet: its
 * slot + 1, 0 when out of memory. */
static uint32_t new_item(Items *items, const ItemStack *batch, uint32_t tile)
{
    uint32_t slot = alloc_slot(items);
    if (slot != 0) {
        Item *item = &items->pool[slot - 1];
        *item = (Item){.type = batch->type,
                       .quality = batch->quality,
                       .hit_points = batch->hit_points,
                       .progress = batch->progress,
                       .eaten = batch->eaten,
                       .corpse = batch->corpse,
                       .woods = batch->woods,
                       .moisture = valid_moisture((ItemType)batch->type, batch->moisture),
                       .retting = ITEM_DEFS[batch->type].rets ? batch->retting : 0,
                       .forbidden = batch->forbidden,
                       .made = batch->made != 0 ? batch->made : items->now + 1,
                       .maker = batch->maker,
                       .generation = item->generation,
                       .tile = tile};
    }
    return slot;
}

/* Links an item at the head of a tile's list, at a spot of its own. */
static void lay_item(Items *items, size_t index, uint32_t slot, uint32_t salt)
{
    Item *item = &items->pool[slot - 1];
    item->tile = (uint32_t)index;
    place_item(items, item, index, salt);
    item->next = items->heads[index];
    items->heads[index] = slot;
    count_item(items, item, 1);
}

int items_add(Items *items, int x, int y, int z, ItemStack batch)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    /* Callers may build stacks by hand: keep the condition valid for the type. */
    batch = valid_batch(batch);
    if (batch.type == ITEM_NONE) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    int room = tile_room(items, index, &batch);
    int added = (int)batch.count < room ? (int)batch.count : room;
    for (int i = 0; i < added; i++) {
        uint32_t slot = new_item(items, &batch, (uint32_t)index);
        if (slot == 0) {
            added = i; /* Out of memory: the rest is not added. */
            break;
        }
        lay_item(items, index, slot, (uint32_t)i);
    }
    if (added <= 0) {
        return 0;
    }
    items->additions++;
    note_change(items, index);
    return added;
}

/* ---- Loads ---- */

/* Rebuilds the summary of a load from its items. */
static void refresh_load(const Items *items, ItemStack *load)
{
    const Item *head = slot_item(items, load->held);
    if (head == NULL) {
        *load = (ItemStack){.type = ITEM_NONE};
        return;
    }
    long count = 0, quality = 0, hit_points = 0, progress = 0, eaten = 0, moisture = 0;
    long retting = 0;
    for (const Item *item = head; item != NULL; item = slot_item(items, item->next)) {
        count++;
        quality += item->quality;
        hit_points += item->hit_points;
        progress += item->progress;
        eaten += item->eaten;
        moisture += item->moisture;
        retting += item->retting;
    }
    *load = (ItemStack){.type = head->type,
                        .corpse = head->corpse,
                        .woods = head->woods,
                        .forbidden = head->forbidden != 0,
                        .made = head->made,
                        .maker = head->maker,
                        .count = (uint16_t)(count < UINT16_MAX ? count : UINT16_MAX),
                        .quality = (uint8_t)((quality + count / 2) / count),
                        .hit_points = (uint16_t)((hit_points + count / 2) / count),
                        .progress = (uint16_t)((progress + count / 2) / count),
                        .eaten = (uint16_t)((eaten + count / 2) / count),
                        .moisture = (uint16_t)((moisture + count / 2) / count),
                        .retting = (uint16_t)((retting + count / 2) / count),
                        .held = load->held};
}

/* Turns a load that is only numbers into items of its condition. */
static void materialize(Items *items, ItemStack *load)
{
    if (load->held != 0 || load->count == 0 || load->type == ITEM_NONE) {
        return;
    }
    ItemStack batch = valid_batch(*load);
    for (int i = 0; batch.type != ITEM_NONE && i < (int)batch.count; i++) {
        uint32_t slot = new_item(items, &batch, ITEM_TILE_HELD);
        if (slot == 0) {
            break; /* Out of memory: a smaller load. */
        }
        items->pool[slot - 1].next = load->held;
        load->held = slot;
        items->held_count++;
    }
    refresh_load(items, load);
}

/* Whether a load holds something (its type then decides what it takes). */
static bool load_held(const ItemStack *load)
{
    return load->type != ITEM_NONE && load->count > 0;
}

/* Picks up to `count` items of the group `stack` (the newest first) off the
 * tile at `index` into `load`. */
static int pick_up_group(Items *items, size_t index, ItemStack stack, int count, ItemStack *load)
{
    if (stack.type == ITEM_NONE || count <= 0 ||
        (load_held(load) && !item_stacks_match(*load, stack))) {
        return 0;
    }
    materialize(items, load);
    int taken = 0;
    uint32_t previous = 0;
    uint32_t slot = items->heads[index];
    while (slot != 0 && taken < count) {
        Item *item = &items->pool[slot - 1];
        uint32_t next = item->next;
        if (item_matches(item, &stack)) {
            if (previous == 0) {
                items->heads[index] = next;
            } else {
                items->pool[previous - 1].next = next;
            }
            count_item(items, item, -1);
            item->tile = ITEM_TILE_HELD;
            item->next = load->held;
            load->held = slot;
            items->held_count++;
            taken++;
        } else {
            previous = slot;
        }
        slot = next;
    }
    if (taken > 0) {
        note_change(items, index);
        refresh_load(items, load);
    }
    return taken;
}

int items_pick_up(Items *items, int x, int y, int z, int count, ItemStack *load)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    ItemStack stack = load_held(load) ? matching_group(items, index, *load) : items->stacks[index];
    return pick_up_group(items, index, stack, count, load);
}

int items_pick_up_of(Items *items, int x, int y, int z, ItemTypeMask types, int count,
                     ItemStack *load)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    ItemStack stack = !load_held(load)                      ? newest_of(items, index, types, true)
                      : (item_types_has(types, load->type)) ? matching_group(items, index, *load)
                                                            : (ItemStack){.type = ITEM_NONE};
    return pick_up_group(items, index, stack, count, load);
}

int items_pick_up_group(Items *items, int x, int y, int z, ItemStack group, int count,
                        ItemStack *load)
{
    if (!in_bounds(items, x, y, z) || group.type == ITEM_NONE) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    return pick_up_group(items, index, matching_group(items, index, group), count, load);
}

int items_pick_up_item(Items *items, ItemId id, ItemStack *load)
{
    const Item *found = items_by_id(items, id);
    if (found == NULL || (load_held(load) && !item_stacks_match(*load, item_as_stack(found)))) {
        return 0;
    }
    materialize(items, load);
    size_t index = found->tile;
    uint32_t slot = id.slot + 1, previous = 0;
    for (uint32_t at = items->heads[index]; at != slot; at = items->pool[at - 1].next) {
        previous = at;
    }
    Item *item = &items->pool[id.slot];
    if (previous == 0) {
        items->heads[index] = item->next;
    } else {
        items->pool[previous - 1].next = item->next;
    }
    count_item(items, item, -1);
    item->tile = ITEM_TILE_HELD;
    item->next = load->held;
    load->held = slot;
    items->held_count++;
    note_change(items, index);
    refresh_load(items, load);
    return 1;
}

void items_load_set_forbidden(Items *items, ItemStack *load, bool forbidden)
{
    if (!load_held(load)) {
        return;
    }
    materialize(items, load);
    for (uint32_t slot = load->held; slot != 0; slot = items->pool[slot - 1].next) {
        items->pool[slot - 1].forbidden = forbidden;
    }
    refresh_load(items, load);
}

bool item_woods_allowed(ItemStack stack, WoodMask allowed)
{
    const ItemDef *def = item_def((ItemType)stack.type);
    return allowed == 0 || def == NULL || !def->wooden ||
           (stack.woods != 0 && (stack.woods & ~allowed) == 0);
}

/* The newest group of allowed items of `types` at `index` whose species are
 * allowed. */
static ItemStack newest_of_woods(const Items *items, size_t index, ItemTypeMask types,
                                 WoodMask allowed)
{
    ItemStack newest = newest_of(items, index, types, true);
    if (newest.type == ITEM_NONE || item_woods_allowed(newest, allowed) || !items->mixed[index]) {
        return item_woods_allowed(newest, allowed) ? newest : (ItemStack){.type = ITEM_NONE};
    }
    for (const Item *item = slot_item(items, items->heads[index]); item != NULL;
         item = slot_item(items, item->next)) {
        if ((item_types_has(types, item->type)) && !item->forbidden &&
            item_woods_allowed(item_as_stack(item), allowed)) {
            return group_of(items, index, key_of(item));
        }
    }
    return (ItemStack){.type = ITEM_NONE};
}

ItemStack items_get_allowed_of_woods(const Items *items, int x, int y, int z, ItemTypeMask types,
                                     WoodMask allowed)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return newest_of_woods(items, index_of(items, x, y, z), types, allowed);
}

void items_load_set_woods(Items *items, ItemStack *load, WoodMask woods)
{
    if (load->count == 0 || load->type == ITEM_NONE || !ITEM_DEFS[load->type].wooden) {
        return;
    }
    materialize(items, load);
    for (uint32_t slot = load->held; slot != 0; slot = items->pool[slot - 1].next) {
        items->pool[slot - 1].woods = woods;
    }
    refresh_load(items, load);
}

static bool chosen(const Item *item, const ItemStack *stack, ItemChoice choice)
{
    return item_matches(item, stack) && item->quality >= choice.min_quality &&
           item->quality <= choice.max_quality &&
           item_moisture_fits((ItemType)item->type, item_moisture(item),
                              (ItemMoistureWant)choice.moisture);
}

int items_count_chosen(const Items *items, int x, int y, int z, ItemType type, ItemChoice choice)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    ItemStack stack = newest_of_woods(items, index, item_types_of(type), choice.woods);
    if (stack.type == ITEM_NONE) {
        return 0;
    }
    int count = 0;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        count += chosen(item, &stack, choice);
    }
    return count;
}

float items_soonest_rot_hours(const Items *items, int x, int y, int z, ItemType type,
                              ItemChoice choice)
{
    if (!in_bounds(items, x, y, z) || !item_rots(type)) {
        return -1.0f;
    }
    size_t index = index_of(items, x, y, z);
    ItemStack stack = newest_of_woods(items, index, item_types_of(type), choice.woods);
    if (stack.type == ITEM_NONE) {
        return -1.0f;
    }
    float soonest = -1.0f;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        if (chosen(item, &stack, choice)) {
            float hours = item_stack_rot_hours(item_as_stack(item), 1.0f);
            soonest = soonest < 0.0f || hours < soonest ? hours : soonest;
        }
    }
    return soonest;
}

ItemType items_soonest_rotting(const Items *items, int x, int y, int z, ItemTypeMask types,
                               float *hours)
{
    ItemType type = ITEM_NONE;
    float soonest = -1.0f;
    if (in_bounds(items, x, y, z)) {
        size_t index = index_of(items, x, y, z);
        for (const Item *item = items_first(items, index); item != NULL;
             item = items_next(items, item)) {
            if (item->corpse != 0 || item->forbidden || !(item_types_has(types, item->type)) ||
                !item_rots((ItemType)item->type)) {
                continue;
            }
            float left = item_stack_rot_hours(item_as_stack(item), 1.0f);
            if (type == ITEM_NONE || left < soonest) { /* Ties: the newest. */
                type = (ItemType)item->type;
                soonest = left;
            }
        }
    }
    if (hours != NULL) {
        *hours = soonest;
    }
    return type;
}

ItemType items_chosen_type(const Items *items, int x, int y, int z, ItemTypeMask types,
                           ItemChoice choice, int min_count)
{
    if (!in_bounds(items, x, y, z) || items->heads[index_of(items, x, y, z)] == 0) {
        return ITEM_NONE;
    }
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if ((item_types_has(types, type)) &&
            items_count_chosen(items, x, y, z, (ItemType)type, choice) >= min_count) {
            return (ItemType)type;
        }
    }
    return ITEM_NONE;
}

bool items_any_chosen(const Items *items, ItemTypeMask types, ItemChoice choice)
{
    bool any = false;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT && !any; type++) {
        any = (item_types_has(types, type)) && items->totals[type] > 0;
    }
    for (uint32_t slot = 0; any && slot < items->pool_used; slot++) {
        const Item *item = &items->pool[slot];
        if (item->type == ITEM_NONE || item->tile == ITEM_TILE_HELD || item->forbidden ||
            !(item_types_has(types, item->type))) {
            continue;
        }
        ItemStack key = key_of(item);
        if (chosen(item, &key, choice) && item_woods_allowed(key, choice.woods)) {
            return true;
        }
    }
    return false;
}

int items_pick_up_chosen(Items *items, int x, int y, int z, ItemType type, int count,
                         ItemChoice choice, ItemStack *load)
{
    if (choice.min_quality <= ITEM_QUALITY_WORST && choice.max_quality >= ITEM_QUALITY_BEST &&
        !choice.soonest_rot && choice.woods == 0 && choice.moisture == ITEM_WANT_ANY) {
        return items_pick_up_of(items, x, y, z, item_types_of(type), count, load);
    }
    if (!in_bounds(items, x, y, z) || count <= 0) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    const ItemStack stack = load_held(load)
                                ? matching_group(items, index, *load)
                                : newest_of_woods(items, index, item_types_of(type), choice.woods);
    if (stack.type != type || (load_held(load) && !item_stacks_match(*load, stack)) ||
        !item_woods_allowed(stack, choice.woods)) {
        return 0;
    }
    materialize(items, load);
    int taken = 0;
    while (taken < count) {
        uint32_t best = 0, best_previous = 0, previous = 0;
        long best_left = 0;
        for (uint32_t slot = items->heads[index]; slot != 0;
             previous = slot, slot = items->pool[slot - 1].next) {
            const Item *item = &items->pool[slot - 1];
            if (!chosen(item, &stack, choice)) {
                continue;
            }
            long left = item_rot_left(item);
            if (best == 0 || (choice.soonest_rot && left < best_left)) {
                best = slot;
                best_previous = previous;
                best_left = left;
            }
            if (!choice.soonest_rot) {
                break;
            }
        }
        if (best == 0) {
            break;
        }
        Item *item = &items->pool[best - 1];
        if (best_previous == 0) {
            items->heads[index] = item->next;
        } else {
            items->pool[best_previous - 1].next = item->next;
        }
        count_item(items, item, -1);
        item->tile = ITEM_TILE_HELD;
        item->next = load->held;
        load->held = best;
        items->held_count++;
        taken++;
    }
    if (taken > 0) {
        note_change(items, index);
        refresh_load(items, load);
    }
    return taken;
}

int items_put(Items *items, int x, int y, int z, ItemStack *load)
{
    if (!in_bounds(items, x, y, z) || load->type == ITEM_NONE || load->count == 0) {
        return 0;
    }
    materialize(items, load);
    if (load->held == 0) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    int room = tile_room(items, index, load);
    int put = 0;
    while (put < room && load->held != 0) {
        uint32_t slot = load->held;
        load->held = items->pool[slot - 1].next;
        items->held_count--;
        lay_item(items, index, slot, (uint32_t)put);
        put++;
    }
    if (put == 0) {
        return 0;
    }
    refresh_load(items, load);
    items->additions++;
    note_change(items, index);
    return put;
}

void items_use_up(Items *items, ItemStack *load, int count)
{
    materialize(items, load);
    for (int i = 0; i < count && load->held != 0; i++) {
        uint32_t slot = load->held;
        load->held = items->pool[slot - 1].next;
        items->held_count--;
        free_slot(items, slot);
    }
    refresh_load(items, load);
}

int items_load_move(Items *items, ItemStack *from, ItemStack *to, int count)
{
    if (count <= 0 || !load_held(from) || (load_held(to) && !item_stacks_match(*to, *from))) {
        return 0;
    }
    materialize(items, from);
    if (load_held(to)) {
        materialize(items, to);
    }
    int moved = 0;
    while (moved < count && from->held != 0) {
        uint32_t slot = from->held;
        from->held = items->pool[slot - 1].next;
        items->pool[slot - 1].next = to->held;
        to->held = slot;
        moved++;
    }
    refresh_load(items, from);
    refresh_load(items, to);
    return moved;
}

int items_load_take(Items *items, ItemStack *from, int index, ItemStack *to)
{
    if (index < 0 || !load_held(from) || index >= (int)from->count || load_held(to)) {
        return 0;
    }
    materialize(items, from);
    uint32_t previous = 0, slot = from->held;
    for (int i = 0; i < index && slot != 0; i++) {
        previous = slot;
        slot = items->pool[slot - 1].next;
    }
    if (slot == 0) {
        return 0;
    }
    if (previous == 0) {
        from->held = items->pool[slot - 1].next;
    } else {
        items->pool[previous - 1].next = items->pool[slot - 1].next;
    }
    items->pool[slot - 1].next = 0;
    to->held = slot;
    refresh_load(items, from);
    refresh_load(items, to);
    return 1;
}

int items_load_lowest_quality(const Items *items, ItemStack load)
{
    if (!load_held(&load)) {
        return ITEM_QUALITY_WORST;
    }
    if (load.held == 0) {
        return load.quality;
    }
    int lowest = ITEM_QUALITY_BEST;
    for (const Item *item = slot_item(items, load.held); item != NULL;
         item = slot_item(items, item->next)) {
        lowest = item->quality < lowest ? item->quality : lowest;
    }
    return lowest;
}

const Item *items_load_first(const Items *items, ItemStack load)
{
    return slot_item(items, load.held);
}

/* Takes up to `count` items of the group `stack` off the tile at `index`:
 * gone. Returns what was taken. */
static ItemStack take_group(Items *items, size_t index, const ItemStack stack, int count)
{
    if (stack.type == ITEM_NONE || count <= 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    long taken = 0, quality = 0, hit_points = 0, progress = 0, eaten = 0, moisture = 0;
    long retting = 0;
    uint32_t previous = 0;
    uint32_t slot = items->heads[index];
    while (slot != 0 && taken < count) {
        const Item *item = &items->pool[slot - 1];
        uint32_t next = item->next;
        if (item_matches(item, &stack)) {
            taken++;
            quality += item->quality;
            hit_points += item->hit_points;
            progress += item->progress;
            eaten += item->eaten;
            moisture += item->moisture;
            retting += item->retting;
            unlink_item(items, index, previous, slot);
        } else {
            previous = slot;
        }
        slot = next;
    }
    if (taken == 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    note_change(items, index);
    ItemStack result = stack;
    result.count = (uint16_t)taken;
    result.quality = (uint8_t)((quality + taken / 2) / taken);
    result.hit_points = (uint16_t)((hit_points + taken / 2) / taken);
    result.progress = (uint16_t)((progress + taken / 2) / taken);
    result.eaten = (uint16_t)((eaten + taken / 2) / taken);
    result.moisture = (uint16_t)((moisture + taken / 2) / taken);
    result.retting = (uint16_t)((retting + taken / 2) / taken);
    return result;
}

ItemStack items_take(Items *items, int x, int y, int z, int count)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    size_t index = index_of(items, x, y, z);
    return take_group(items, index, items->stacks[index], count);
}

int items_remove(Items *items, int x, int y, int z, int count)
{
    return items_take(items, x, y, z, count).count;
}

ItemStack items_take_of(Items *items, int x, int y, int z, ItemTypeMask types, int count)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    size_t index = index_of(items, x, y, z);
    return take_group(items, index, newest_of(items, index, types, false), count);
}

ItemStack items_take_group(Items *items, int x, int y, int z, ItemStack group, int count)
{
    if (!in_bounds(items, x, y, z) || group.type == ITEM_NONE) {
        return (ItemStack){.type = ITEM_NONE};
    }
    size_t index = index_of(items, x, y, z);
    return take_group(items, index, matching_group(items, index, group), count);
}

int items_remove_of(Items *items, int x, int y, int z, ItemTypeMask types, int count)
{
    return items_take_of(items, x, y, z, types, count).count;
}

void items_clear(Items *items, int x, int y, int z)
{
    if (!in_bounds(items, x, y, z)) {
        return;
    }
    size_t index = index_of(items, x, y, z);
    while (items->stacks[index].type != ITEM_NONE) {
        take_group(items, index, items->stacks[index], UINT16_MAX);
    }
}

/* ---- Items one by one ---- */

float item_drawn_size(ItemType type)
{
    float bulk = is_item_type(type) ? ITEM_DEFS[type].bulk : 1.0f;
    float size = 0.4f + 0.1f * log2f(1.0f + bulk);
    return size < 0.4f ? 0.4f : size > 1.0f ? 1.0f : size;
}

ItemId items_id(const Items *items, const Item *item)
{
    if (item == NULL || item < items->pool || item >= items->pool + items->pool_used) {
        return ITEM_ID_NONE;
    }
    return (ItemId){(uint32_t)(item - items->pool), item->generation};
}

const Item *items_by_id(const Items *items, ItemId id)
{
    if (id.slot >= items->pool_used) {
        return NULL;
    }
    const Item *item = &items->pool[id.slot];
    return item->type != ITEM_NONE && item->generation == id.generation &&
                   item->tile != ITEM_TILE_HELD
               ? item
               : NULL;
}

bool items_id_held(const Items *items, ItemId id)
{
    if (id.slot >= items->pool_used) {
        return false;
    }
    const Item *item = &items->pool[id.slot];
    return item->type != ITEM_NONE && item->generation == id.generation &&
           item->tile == ITEM_TILE_HELD;
}

ItemId items_at_point(const Items *items, float x, float y, int z)
{
    int tx = (int)floorf(x), ty = (int)floorf(y);
    ItemId found = ITEM_ID_NONE;
    float nearest = 0.0f;
    /* The tile under the point first (its items are drawn over those of
     * the tiles before it), then its neighbors. */
    static const int DX[9] = {0, -1, 0, 1, -1, 1, -1, 0, 1};
    static const int DY[9] = {0, -1, -1, -1, 0, 0, 1, 1, 1};
    for (int n = 0; n < 9 && found.slot == UINT32_MAX; n++) {
        int cx = tx + DX[n], cy = ty + DY[n];
        if (!in_bounds(items, cx, cy, z)) {
            continue;
        }
        for (const Item *item = items_first(items, index_of(items, cx, cy, z)); item != NULL;
             item = items_next(items, item)) {
            float ix = (float)cx + ((float)item->spot_x + 0.5f) / 256.0f;
            float iy = (float)cy + ((float)item->spot_y + 0.5f) / 256.0f;
            float radius = item_drawn_size((ItemType)item->type) * 0.5f;
            float d = (ix - x) * (ix - x) + (iy - y) * (iy - y);
            if (d <= radius * radius && (found.slot == UINT32_MAX || d < nearest)) {
                found = items_id(items, item);
                nearest = d;
            }
        }
    }
    return found;
}

ItemId items_find_like(const Items *items, int x, int y, int z, const Item *like)
{
    if (like == NULL || !in_bounds(items, x, y, z)) {
        return ITEM_ID_NONE;
    }
    for (const Item *item = items_first(items, index_of(items, x, y, z)); item != NULL;
         item = items_next(items, item)) {
        if (item->type == like->type && item->spot_x == like->spot_x &&
            item->spot_y == like->spot_y && item->angle == like->angle) {
            return items_id(items, item);
        }
    }
    return ITEM_ID_NONE;
}

bool items_move(Items *items, ItemId id, int x, int y, int z, int spot_x, int spot_y)
{
    const Item *found = items_by_id(items, id);
    if (found == NULL || !in_bounds(items, x, y, z)) {
        return false;
    }
    uint32_t slot = id.slot + 1;
    size_t from = found->tile;
    uint32_t previous = 0;
    for (uint32_t at = items->heads[from]; at != slot; at = items->pool[at - 1].next) {
        previous = at;
    }
    Item *item = &items->pool[slot - 1];
    count_item(items, item, -1);
    if (previous == 0) {
        items->heads[from] = item->next;
    } else {
        items->pool[previous - 1].next = item->next;
    }
    note_change(items, from);
    size_t to = index_of(items, x, y, z);
    item->tile = (uint32_t)to;
    item->spot_x = (uint8_t)clampi(spot_x, 0, 255);
    item->spot_y = (uint8_t)clampi(spot_y, 0, 255);
    item->next = items->heads[to];
    items->heads[to] = slot;
    count_item(items, item, 1);
    items->additions++;
    note_change(items, to);
    return true;
}

bool items_turn(Items *items, ItemId id, int steps)
{
    if (items_by_id(items, id) == NULL) {
        return false;
    }
    Item *item = &items->pool[id.slot];
    item->angle = (uint8_t)((int)item->angle + steps);
    note_change(items, item->tile);
    return true;
}

ItemStack item_as_stack(const Item *item)
{
    return (ItemStack){.type = item->type,
                       .quality = item->quality,
                       .count = 1,
                       .hit_points = item->hit_points,
                       .progress = item->progress,
                       .eaten = item->eaten,
                       .corpse = item->corpse,
                       .woods = item->woods,
                       .forbidden = item->forbidden != 0,
                       .made = item->made,
                       .maker = item->maker,
                       .moisture = item->moisture,
                       .retting = item->retting};
}

void items_set_now(Items *items, uint64_t tick)
{
    items->now = tick < UINT32_MAX - 1 ? (uint32_t)tick : UINT32_MAX - 1;
}

uint16_t items_maker_register(Items *items, EntityId id, const char *name)
{
    for (int i = 0; i < items->maker_count; i++) {
        if (items->makers[i].id.index == id.index &&
            items->makers[i].id.generation == id.generation) {
            return (uint16_t)(i + 1);
        }
    }
    if (items->maker_count >= ITEM_MAKERS_MAX || name == NULL) {
        return 0;
    }
    ItemMaker *maker = &items->makers[items->maker_count++];
    maker->id = id;
    snprintf(maker->name, sizeof(maker->name), "%s", name);
    return (uint16_t)items->maker_count;
}

EntityId items_maker_id(const Items *items, uint16_t maker)
{
    return maker >= 1 && maker <= items->maker_count ? items->makers[maker - 1].id : ENTITY_NONE;
}

const char *items_maker_name(const Items *items, uint16_t maker)
{
    return maker >= 1 && maker <= items->maker_count ? items->makers[maker - 1].name : NULL;
}

long item_rot_left(const Item *item)
{
    if (!item_rots((ItemType)item->type)) {
        return -1;
    }
    return (long)item->hit_points * ITEM_ROT_STEP - (long)item->progress;
}

/* Whether `a` comes before `b` in an order (not for ties). */
static bool comes_before(const Item *a, const Item *b, ItemOrder order)
{
    switch (order) {
    case ITEM_ORDER_QUALITY: return a->quality > b->quality;
    case ITEM_ORDER_CONDITION:
        return item_stack_hit_points_percent(item_as_stack(a)) >
               item_stack_hit_points_percent(item_as_stack(b));
    case ITEM_ORDER_ROT: {
        long left_a = item_rot_left(a), left_b = item_rot_left(b);
        return left_a >= 0 && (left_b < 0 || left_a < left_b);
    }
    case ITEM_ORDER_NEWEST:
    case ITEM_ORDER_COUNT: break;
    }
    return false;
}

int items_list(const Items *items, size_t index, ItemOrder order, const Item **out, int capacity)
{
    int count = 0, kept = 0;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        count++;
        /* Insertion into the kept ones, after those it does not come before
         * (the list is newest first: ties keep that order). */
        int at = kept;
        while (at > 0 && comes_before(item, out[at - 1], order)) {
            at--;
        }
        if (at >= capacity) {
            continue;
        }
        int last = kept < capacity ? kept : capacity - 1;
        for (int i = last; i > at; i--) {
            out[i] = out[i - 1];
        }
        out[at] = item;
        kept += kept < capacity;
    }
    return count;
}

/* ---- Natural items ---- */

int items_add_natural(Items *items, int x, int y, int z, ItemType type, int count)
{
    return items_add_natural_wood(items, x, y, z, type, count, 0);
}

int items_add_natural_wood(Items *items, int x, int y, int z, ItemType type, int count,
                           WoodMask woods)
{
    if (!in_bounds(items, x, y, z) || !is_item_type(type) || count <= 0) {
        return 0;
    }
    woods = ITEM_DEFS[type].wooden ? woods : 0;
    size_t index = index_of(items, x, y, z);
    ItemStack key = {.type = type, .count = 1, .woods = woods};
    int room = tile_room(items, index, &key);
    count = count < room ? count : room;
    int added = 0;
    for (; added < count; added++) {
        /* Quality: the mean of two draws, most items middling; condition 60 to 100 %. */
        uint32_t h = mix(index, ((uint64_t)items->stamp_clock << 20) ^ (uint64_t)added ^ 0xA5A5u);
        int quality = (int)((h & 0xFFu) % 101u + ((h >> 8) & 0xFFu) % 101u) / 2;
        int percent = 60 + (int)((h >> 16) % 41u);
        ItemStack batch =
            item_stack_make(type, 1, quality, item_hit_points_at_percent(type, percent));
        batch.woods = woods;
        uint32_t slot = new_item(items, &batch, (uint32_t)index);
        if (slot == 0) {
            break;
        }
        lay_item(items, index, slot, (uint32_t)added);
    }
    if (added > 0) {
        items->additions++;
        note_change(items, index);
    }
    return added;
}

int items_count_type(const Items *items, int x, int y, int z, ItemType type)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    int count = 0;
    for (const Item *item = items_first(items, index_of(items, x, y, z)); item != NULL;
         item = items_next(items, item)) {
        count += item->type == type && item->corpse == 0;
    }
    return count;
}

/* Puts the items of a load down around (x, y) as items_drop_load() does, on
 * the tiles `filter` accepts (all when NULL); what finds no room stays in the
 * load. Returns how many found room. */
static int spread_load_where(Items *items, const Map *map, int x, int y, int z, ItemStack *load,
                             ItemDropFilter filter)
{
    enum { SIDE = 2 * ITEM_DROP_RADIUS + 1 };
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    int placed = 0;
    if (map_is_passable(map, x, y, z)) {
        /* Breadth-first search over passable tiles, so that items never land
         * behind a wall; cells are indexed relative to (x, y). */
        bool visited[SIDE * SIDE] = {false};
        int queue[SIDE * SIDE];
        int head = 0, tail = 0;
        queue[tail++] = ITEM_DROP_RADIUS * SIDE + ITEM_DROP_RADIUS;
        visited[queue[0]] = true;
        while (head < tail && load->count > 0) {
            int cell = queue[head++];
            int cx = x + cell % SIDE - ITEM_DROP_RADIUS;
            int cy = y + cell / SIDE - ITEM_DROP_RADIUS;
            if (!items_any_forbidden(items, cx, cy, z) && /* Left alone. */
                (filter == NULL || filter(items->drop_data, items, cx, cy, z, load))) {
                placed += items_put(items, cx, cy, z, load);
            }
            for (int i = 0; i < 4; i++) {
                int rx = cell % SIDE + DX[i];
                int ry = cell / SIDE + DY[i];
                if (rx < 0 || ry < 0 || rx >= SIDE || ry >= SIDE || visited[ry * SIDE + rx]) {
                    continue;
                }
                visited[ry * SIDE + rx] = true;
                if (map_is_passable(map, x + rx - ITEM_DROP_RADIUS, y + ry - ITEM_DROP_RADIUS, z)) {
                    queue[tail++] = ry * SIDE + rx;
                }
            }
        }
    }
    return placed;
}

/* spread_load_where() with the drop filter first, then (what found no room
 * on the tiles it accepts) anywhere. */
static int spread_load(Items *items, const Map *map, int x, int y, int z, ItemStack *load)
{
    int placed = spread_load_where(items, map, x, y, z, load, items->drop_filter);
    if (items->drop_filter != NULL && load->count > 0) {
        placed += spread_load_where(items, map, x, y, z, load, NULL);
    }
    return placed;
}

void items_set_drop_filter(Items *items, ItemDropFilter filter, const void *data)
{
    items->drop_filter = filter;
    items->drop_data = data;
}

int items_drop_load(Items *items, const Map *map, int x, int y, int z, ItemStack *load)
{
    int placed = spread_load(items, map, x, y, z, load);
    items_use_up(items, load, load->count); /* No room: lost. */
    return placed;
}

int items_lift_all(Items *items, int x, int y, int z, ItemStack *loads, int capacity)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    int count = 0;
    while (items->stacks[index].type != ITEM_NONE && count < capacity) {
        loads[count] = (ItemStack){.type = ITEM_NONE};
        if (pick_up_group(items, index, items->stacks[index], UINT16_MAX, &loads[count]) == 0) {
            break;
        }
        count++;
    }
    return count;
}

int items_move_all(Items *items, const Map *map, int x, int y, int z, int to_x, int to_y, int to_z,
                   bool keep)
{
    /* Every group taken off first (the newest first), then each put down in
     * turn: what lands back on the tile is not taken again. */
    ItemStack loads[ITEM_LIFT_MAX];
    int load_count = items_lift_all(items, x, y, z, loads, ITEM_LIFT_MAX), moved = 0;
    for (int i = 0; i < load_count; i++) {
        moved += spread_load(items, map, to_x, to_y, to_z, &loads[i]);
    }
    for (int i = 0; i < load_count; i++) { /* No room: back where it lay, or lost. */
        if (keep) {
            items_put(items, x, y, z, &loads[i]);
        }
        items_use_up(items, &loads[i], loads[i].count);
    }
    return moved;
}

int items_drop(Items *items, const Map *map, int x, int y, int z, ItemStack batch)
{
    batch.held = 0; /* Only numbers. */
    return items_drop_load(items, map, x, y, z, &batch);
}

/* What `hours` do to one item in the conditions of its tile. */
typedef enum ItemFate {
    ITEM_FATE_SAME,
    ITEM_FATE_CHANGED,  /* Its condition: the tile changes. */
    ITEM_FATE_MOISTURE, /* Only its moisture. */
    ITEM_FATE_GONE
} ItemFate;

/* Moves an item's moisture towards its target; whether it changed. */
static bool advance_moisture(Item *item, int hours, ItemConditions here)
{
    ItemType type = (ItemType)item->type;
    float moisture = item_moisture(item);
    float target = item_moisture_target(type, here);
    float tau = item_moisture_hours(type, moisture, here);
    float step = (target - moisture) * (1.0f - expf(-(float)hours / tau));
    /* At least a unit while it is a unit or more away: slow materials near
     * their target would otherwise round to standing still. */
    float unit = 1.0f / (float)ITEM_MOISTURE_UNITS;
    if (fabsf(step) < unit && fabsf(target - moisture) >= unit) {
        step = target > moisture ? unit : -unit;
    }
    uint16_t code = moisture_code(type, moisture + step);
    if (code == item->moisture) {
        return false;
    }
    item->moisture = code;
    return true;
}

/* Rets an item lying wet and warm enough (ItemDef.rets): wetter than it was
 * cut, past ITEM_RETTING_WET_SHARE of the way from fresh to soaked (rain,
 * water; stems drying after the cut do not ret), or under the dew of a damp
 * night in the open; whether it changed. */
static bool advance_retting(Item *item, int hours, ItemConditions here)
{
    const ItemWater *water = &ITEM_DEFS[item->type].water;
    float wet = water->fresh + ITEM_RETTING_WET_SHARE * (water->room - water->fresh);
    if (here.celsius <= ITEM_RETTING_MIN_CELSIUS || (item_moisture(item) < wet && !here.dew)) {
        return false;
    }
    float warmth = (here.celsius - ITEM_RETTING_MIN_CELSIUS) /
                   (ITEM_RETTING_REFERENCE_CELSIUS - ITEM_RETTING_MIN_CELSIUS);
    float rate = (warmth < ITEM_RETTING_WARM_MAX ? warmth : ITEM_RETTING_WARM_MAX) *
                 (here.in_water ? ITEM_RETTING_WATER : 1.0f);
    long units = lroundf((float)ITEM_RETTING_UNITS * rate * (float)hours / ITEM_RETTING_HOURS);
    long most = lroundf(ITEM_RETTING_MAX * (float)ITEM_RETTING_UNITS);
    long retting = (long)item->retting + (units > 1 ? units : 1);
    retting = retting < most ? retting : most;
    if (retting == item->retting) {
        return false;
    }
    item->retting = (uint16_t)retting;
    return true;
}

static ItemFate advance_item(Item *item, int hours, ItemConditions here)
{
    const ItemDef *def = &ITEM_DEFS[item->type];
    bool wet = def->water.room > 0.0f && advance_moisture(item, hours, here);
    bool retted = def->rets && advance_retting(item, hours, here);
    if (def->rot_days > 0.0f && here.rot_rate > 0.0f) {
        float rate = here.rot_rate;
        if (def->water.room > 0.0f) {
            rate *= item_rot_moisture_factor((ItemType)item->type, item_moisture(item));
        } else if (here.rained_on) {
            rate *= ITEM_ROT_RAINED_ON;
        }
        float units = rot_units_per_hour(def, rate) * (float)hours;
        long total = (long)item->progress + lroundf(units);
        long lost = total / ITEM_ROT_STEP;
        if (lost >= item->hit_points) {
            return ITEM_FATE_GONE;
        }
        item->hit_points = (uint16_t)(item->hit_points - lost);
        item->progress = (uint16_t)(total % ITEM_ROT_STEP);
        return ITEM_FATE_CHANGED;
    }
    if (retted) {
        return ITEM_FATE_CHANGED; /* Shown on the tile: a new stamp. */
    }
    return wet ? ITEM_FATE_MOISTURE : ITEM_FATE_SAME;
}

static bool type_changes(ItemType type)
{
    const ItemDef *def = &ITEM_DEFS[type];
    return def->rot_days > 0.0f || def->water.room > 0.0f;
}

int items_advance(Items *items, size_t begin, size_t end, int hours, ItemConditionsFn conditions,
                  const void *context)
{
    size_t count = (size_t)items->width * (size_t)items->height * (size_t)items->depth;
    if (end > count) {
        end = count;
    }
    if (hours <= 0) {
        return 0;
    }
    int rotted = 0;
    const size_t level_size = (size_t)items->width * (size_t)items->height;
    for (size_t i = begin; i < end; i++) {
        if (items->level_stacks[i / level_size] == 0) {
            i = (i / level_size + 1) * level_size - 1; /* Nothing on this level. */
            continue;
        }
        if (items->heads[i] == 0) {
            continue;
        }
        bool asked = false, changed = false, wet = false, counted = false;
        ItemConditions here = ITEM_CONDITIONS_STILL;
        uint32_t previous = 0;
        uint32_t slot = items->heads[i];
        while (slot != 0) {
            Item *item = &items->pool[slot - 1];
            uint32_t next = item->next;
            if (!type_changes((ItemType)item->type)) {
                previous = slot;
                slot = next;
                continue;
            }
            if (!asked) {
                here = conditions != NULL ? conditions(context, i) : here;
                asked = true;
            }
            ItemFate fate = advance_item(item, hours, here);
            if (fate == ITEM_FATE_GONE) {
                items->rotted[item->type]++;
                unlink_item(items, i, previous, slot);
                if (!counted) {
                    rotted++; /* One per tile. */
                    counted = true;
                }
            } else {
                previous = slot;
            }
            changed |= fate == ITEM_FATE_GONE || fate == ITEM_FATE_CHANGED;
            wet |= fate == ITEM_FATE_MOISTURE;
            slot = next;
        }
        if (changed) {
            note_change(items, i);
        } else if (wet) {
            refresh_tile(items, i); /* The summary's moisture; no new stamp. */
        }
    }
    return rotted;
}

/* Notes a problem found by items_check() (the first one is described). */
static void found(ItemsCheck *check, const char *what, long tile)
{
    if (check->problems++ == 0) {
        check->first = what;
        check->first_tile = tile;
    }
}

ItemsCheck items_check(const Items *items)
{
    ItemsCheck check = {0, NULL, -1};
    size_t level_size = (size_t)items->width * (size_t)items->height;
    long totals[ITEM_TYPE_COUNT] = {0};
    long listed = 0;
    int forbidden = 0;
    for (int z = 0; z < items->depth; z++) {
        uint32_t stacks = 0;
        for (size_t i = (size_t)z * level_size; i < (size_t)(z + 1) * level_size; i++) {
            long steps = 0;
            for (const Item *item = items_first(items, i); item != NULL;
                 item = items_next(items, item)) {
                if (++steps > (long)items->pool_used) {
                    found(&check, "a tile's list loops", (long)i);
                    break;
                }
                if (item->type == ITEM_NONE || (unsigned)item->type >= ITEM_TYPE_COUNT) {
                    found(&check, "a free or invalid item lies on a tile", (long)i);
                    continue;
                }
                if (item->tile != (uint32_t)i) {
                    found(&check, "an item lies on another tile than its own", (long)i);
                }
                totals[item->type]++;
                listed++;
            }
            /* The summary and the mixed mark, as refresh_tile() makes them. */
            const Item *head = items_first(items, i);
            ItemStack expected =
                head != NULL ? group_of(items, i, key_of(head)) : (ItemStack){.type = ITEM_NONE};
            const ItemStack *stack = &items->stacks[i];
            if (stack->type != expected.type || stack->count != expected.count ||
                stack->corpse != expected.corpse || stack->woods != expected.woods ||
                stack->forbidden != expected.forbidden || stack->quality != expected.quality ||
                stack->hit_points != expected.hit_points || stack->progress != expected.progress ||
                stack->moisture != expected.moisture || stack->eaten != expected.eaten ||
                stack->retting != expected.retting || stack->held != 0) {
                found(&check, "a tile's summary is not its newest group", (long)i);
            }
            bool mixed = false;
            for (const Item *item = head; item != NULL && !mixed; item = items_next(items, item)) {
                mixed = !item_matches(item, &expected);
            }
            if (mixed != (items->mixed[i] != 0)) {
                found(&check, "a tile's mixed mark is wrong", (long)i);
            }
            stacks += expected.type != ITEM_NONE;
            if (items->forbidden[i] != tile_forbidden(items, i)) {
                found(&check, "a tile's forbidden mark is wrong", (long)i);
            }
            forbidden += items->forbidden[i] != 0;
        }
        if (items->level_stacks[z] != stacks) {
            found(&check, "a level's count of tiles with items is wrong", -1);
        }
    }
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        if (totals[type] != items->totals[type]) {
            found(&check, "the totals of a type are wrong", -1);
            break;
        }
    }
    if (listed != items->item_count) {
        found(&check, "the count of items lying about is wrong", -1);
    }
    if (forbidden != items->forbidden_count) {
        found(&check, "the count of forbidden tiles is wrong", -1);
    }
    /* Every slot used is lying on a tile, in a load, or free: no more, no
     * less (an item in two lists would be counted twice). */
    long held = 0, free_count = 0;
    for (uint32_t slot = 0; slot < items->pool_used; slot++) {
        const Item *item = &items->pool[slot];
        held += item->type != ITEM_NONE && item->tile == ITEM_TILE_HELD;
    }
    for (uint32_t slot = items->free_slots; slot != 0 && free_count <= (long)items->pool_used;
         slot = items->pool[slot - 1].next) {
        if (items->pool[slot - 1].type != ITEM_NONE) {
            found(&check, "an item in use is on the free list", -1);
            break;
        }
        free_count++;
    }
    if (held != items->held_count) {
        found(&check, "the count of items in loads is wrong", -1);
    }
    if (listed + held + free_count != (long)items->pool_used) {
        found(&check, "some slots are neither lying, held nor free", -1);
    }
    return check;
}

bool items_check_load(const Items *items, ItemStack load)
{
    if (load.held == 0) {
        return true; /* Only numbers. */
    }
    long count = 0;
    for (const Item *item = slot_item(items, load.held); item != NULL;
         item = slot_item(items, item->next)) {
        if (++count > (long)items->pool_used || item->tile != ITEM_TILE_HELD ||
            item->type != load.type || item->corpse != load.corpse || item->woods != load.woods ||
            (item->forbidden != 0) != (load.forbidden != 0)) {
            return false;
        }
    }
    return count == load.count;
}

ItemHeaps items_heaps(const Items *items)
{
    ItemHeaps heaps = {0, 0, 0, 0};
    size_t count = (size_t)items->width * (size_t)items->height * (size_t)items->depth;
    for (size_t i = 0; i < count; i++) {
        if (items->stacks[i].type == ITEM_NONE) {
            continue;
        }
        heaps.tiles++;
        if (!items->mixed[i]) {
            continue;
        }
        ItemStack groups[16];
        int x = (int)(i % (size_t)items->width);
        int y = (int)(i / (size_t)items->width % (size_t)items->height);
        int z = (int)(i / ((size_t)items->width * (size_t)items->height));
        int kinds = items_groups(items, x, y, z, groups, 16);
        heaps.mixed++;
        heaps.mixed_groups += kinds;
        heaps.most_groups = kinds > heaps.most_groups ? kinds : heaps.most_groups;
    }
    return heaps;
}

uint64_t items_hash(const Items *items, uint64_t hash)
{
    size_t count = (size_t)items->width * (size_t)items->height * (size_t)items->depth;
    /* Field by field: struct padding bytes are unspecified. */
    for (size_t i = 0; i < count; i++) {
        if (items->heads[i] == 0) {
            continue;
        }
        uint64_t index = i;
        hash = hash_fnv1a64(hash, &index, sizeof(index));
        for (const Item *item = items_first(items, i); item != NULL;
             item = items_next(items, item)) {
            hash = hash_fnv1a64(hash, &item->type, sizeof(item->type));
            hash = hash_fnv1a64(hash, &item->quality, sizeof(item->quality));
            hash = hash_fnv1a64(hash, &item->hit_points, sizeof(item->hit_points));
            hash = hash_fnv1a64(hash, &item->progress, sizeof(item->progress));
            hash = hash_fnv1a64(hash, &item->eaten, sizeof(item->eaten));
            hash = hash_fnv1a64(hash, &item->corpse, sizeof(item->corpse));
            hash = hash_fnv1a64(hash, &item->spot_x, sizeof(item->spot_x));
            hash = hash_fnv1a64(hash, &item->spot_y, sizeof(item->spot_y));
            hash = hash_fnv1a64(hash, &item->angle, sizeof(item->angle));
            if (item->forbidden) { /* Hashes without anything forbidden stay as they were. */
                hash = hash_fnv1a64(hash, &item->forbidden, sizeof(item->forbidden));
            }
            if (item->woods != 0) {
                hash = hash_fnv1a64(hash, &item->woods, sizeof(item->woods));
            }
            if (item->moisture != 0) {
                hash = hash_fnv1a64(hash, &item->moisture, sizeof(item->moisture));
            }
            if (item->retting != 0) { /* Hashes of games without retting stay as they were. */
                hash = hash_fnv1a64(hash, &item->retting, sizeof(item->retting));
            }
        }
    }
    /* Items in loads, by slot. */
    for (uint32_t slot = 0; slot < items->pool_used; slot++) {
        const Item *item = &items->pool[slot];
        if (item->type == ITEM_NONE || item->tile != ITEM_TILE_HELD) {
            continue;
        }
        hash = hash_fnv1a64(hash, &slot, sizeof(slot));
        hash = hash_fnv1a64(hash, &item->type, sizeof(item->type));
        hash = hash_fnv1a64(hash, &item->quality, sizeof(item->quality));
        hash = hash_fnv1a64(hash, &item->hit_points, sizeof(item->hit_points));
        hash = hash_fnv1a64(hash, &item->progress, sizeof(item->progress));
        hash = hash_fnv1a64(hash, &item->eaten, sizeof(item->eaten));
        hash = hash_fnv1a64(hash, &item->corpse, sizeof(item->corpse));
        if (item->forbidden) {
            hash = hash_fnv1a64(hash, &item->forbidden, sizeof(item->forbidden));
        }
        if (item->woods != 0) {
            hash = hash_fnv1a64(hash, &item->woods, sizeof(item->woods));
        }
        if (item->moisture != 0) {
            hash = hash_fnv1a64(hash, &item->moisture, sizeof(item->moisture));
        }
        if (item->retting != 0) {
            hash = hash_fnv1a64(hash, &item->retting, sizeof(item->retting));
        }
    }
    return hash;
}

void item_stack_serialize(Serializer *s, ItemStack *stack)
{
    ser_u8_below(s, &stack->type, ITEM_TYPE_COUNT);
    ser_u8(s, &stack->quality);
    ser_u16(s, &stack->count);
    ser_u16(s, &stack->hit_points);
    ser_u16(s, &stack->progress);
    ser_u16(s, &stack->eaten);
    ser_u16(s, &stack->corpse);
    ser_u8(s, &stack->forbidden);
    ser_u16(s, &stack->woods);
    ser_u32(s, &stack->made);
    ser_u16(s, &stack->maker);
    ser_u16(s, &stack->moisture);
    ser_u16(s, &stack->retting);
    ser_u32(s, &stack->held);
}

void item_id_serialize(Serializer *s, ItemId *id)
{
    ser_u32(s, &id->slot);
    ser_u16(s, &id->generation);
}

static bool stack_is_empty(const ItemStack *stack)
{
    return stack->type == 0 && stack->quality == 0 && stack->count == 0 && stack->hit_points == 0 &&
           stack->progress == 0 && stack->eaten == 0 && stack->corpse == 0 &&
           stack->forbidden == 0 && stack->woods == 0 && stack->made == 0 && stack->maker == 0 &&
           stack->moisture == 0 && stack->retting == 0 && stack->held == 0;
}

/* The summaries of the tiles: those not empty, each after the gap since the
 * last. */
static void stacks_serialize(Serializer *s, Items *items, size_t tiles)
{
    uint64_t listed = 0;
    if (!s->reading) {
        for (size_t i = 0; i < tiles; i++) {
            listed += !stack_is_empty(&items->stacks[i]);
        }
    }
    ser_varint(s, &listed);
    if (!ser_check(s, listed <= tiles)) {
        return;
    }
    if (s->reading) {
        memset(items->stacks, 0, tiles * sizeof(*items->stacks));
    }
    size_t at = 0;
    for (uint64_t n = 0; n < listed && ser_ok(s); n++) {
        uint64_t gap = 0;
        if (!s->reading) {
            while (stack_is_empty(&items->stacks[at])) {
                at++;
                gap++;
            }
        }
        ser_varint(s, &gap);
        if (s->reading && !ser_check(s, gap < tiles - at)) {
            return;
        }
        at += s->reading ? (size_t)gap : 0;
        item_stack_serialize(s, &items->stacks[at]);
        at++;
    }
}

static void item_serialize(Serializer *s, Item *item)
{
    ser_u8_below(s, &item->type, ITEM_TYPE_COUNT);
    ser_u8(s, &item->quality);
    ser_u8(s, &item->spot_x);
    ser_u8(s, &item->spot_y);
    ser_u16(s, &item->hit_points);
    ser_u16(s, &item->progress);
    ser_u16(s, &item->eaten);
    ser_u16(s, &item->corpse);
    ser_u16(s, &item->generation);
    ser_u8(s, &item->angle);
    ser_u8(s, &item->forbidden);
    ser_u16(s, &item->woods);
    ser_u16(s, &item->moisture);
    ser_u16(s, &item->retting);
    ser_u32(s, &item->made);
    ser_u16(s, &item->maker);
    ser_u32(s, &item->next);
    ser_u32(s, &item->tile);
}

void items_serialize(Serializer *s, Items *items)
{
    int width = items->width, height = items->height, depth = items->depth;
    ser_int(s, &width);
    ser_int(s, &height);
    ser_int(s, &depth);
    if (!ser_check(s, width == items->width && height == items->height && depth == items->depth)) {
        return;
    }
    size_t tiles = (size_t)width * (size_t)height * (size_t)depth;
    stacks_serialize(s, items, tiles);
    ser_layer_u8(s, items->mixed, tiles);
    ser_longs(s, items->totals, ITEM_TYPE_COUNT);
    ser_u32s(s, items->level_stacks, (size_t)depth);
    ser_u32(s, &items->additions);
    ser_layer_u8(s, items->forbidden, tiles);
    ser_int(s, &items->forbidden_count);
    uint32_t used = items->pool_used;
    ser_u32(s, &used);
    if (s->reading) {
        if (!ser_alloc(s, (void **)&items->pool, used, sizeof(*items->pool), UINT32_MAX / 4)) {
            return;
        }
        items->pool_used = used;
        items->pool_capacity = used;
    }
    for (uint32_t i = 0; i < used && ser_ok(s); i++) {
        item_serialize(s, &items->pool[i]);
        ser_check(s, items->pool[i].next <= used);
    }
    ser_u32_upto(s, &items->free_slots, used);
    ser_long(s, &items->item_count);
    ser_long(s, &items->held_count);
    ser_layer_u32(s, items->heads, tiles);
    for (size_t i = 0; i < tiles && s->reading && ser_ok(s); i++) {
        ser_check(s, items->heads[i] <= used);
    }
    ser_layer_u32(s, items->stamps, tiles);
    ser_u32(s, &items->stamp_clock);
    ser_bool(s, &items->picture);
    ser_longs(s, items->rotted, ITEM_TYPE_COUNT);
    ser_u32(s, &items->now);
    ser_int_in(s, &items->maker_count, 0, ITEM_MAKERS_MAX);
    for (int i = 0; i < ITEM_MAKERS_MAX; i++) {
        entity_id_serialize(s, &items->makers[i].id);
        ser_bytes(s, items->makers[i].name, sizeof(items->makers[i].name));
    }
    if (s->reading) {
        for (int i = 0; i < ITEM_MAKERS_MAX; i++) {
            items->makers[i].name[sizeof(items->makers[i].name) - 1] = 0;
        }
    }
}
