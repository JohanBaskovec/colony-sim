#include "core/map.h"

#include "core/game_clock.h"
#include "core/i18n.h"
#include "core/temperature.h"
#include "core/tree.h"
#include "core/util.h"
#include "core/wood.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const TerrainDef TERRAIN_DEFS[TERRAIN_COUNT] = {
    [TERRAIN_DEEP_WATER] = {"deep_water", N_("terrain.deep_water"), 0.0f, 0.0f, false, false},
    [TERRAIN_SHALLOW_WATER] = {"shallow_water", N_("terrain.shallow_water"), 3.0f, 0.0f, false,
                               false},
    [TERRAIN_SAND] = {"sand", N_("terrain.sand"), 1.4f, 0.1f, true, false},
    [TERRAIN_SOIL] = {"soil", N_("terrain.soil"), 1.0f, 1.0f, true, true},
    [TERRAIN_RICH_SOIL] = {"rich_soil", N_("terrain.rich_soil"), 1.0f, 1.4f, true, true},
    [TERRAIN_GRAVEL] = {"gravel", N_("terrain.gravel"), 1.1f, 0.5f, true, true},
    [TERRAIN_ROUGH_STONE] = {"rough_stone", N_("terrain.rough_stone"), 1.2f, 0.0f, true, true},
    [TERRAIN_OPEN] = {"open", N_("terrain.open"), 0.0f, 0.0f, false, false},
    [TERRAIN_GRASS_ROOF] = {"grass_roof", N_("structure.roof"), 0.0f, 0.0f, false, false,
                            STRUCTURE_ROOF},
    /* Branches underfoot, leaves in the face: slow going. */
    [TERRAIN_FOLIAGE] = {"foliage", N_("terrain.foliage"), 1.5f, 0.0f, false, false},
    /* Too thin to stand on. */
    [TERRAIN_FOLIAGE_MEDIUM] = {"foliage_medium", N_("terrain.foliage_medium"), 0.0f, 0.0f, false,
                                false},
    [TERRAIN_FOLIAGE_SPARSE] = {"foliage_sparse", N_("terrain.foliage_sparse"), 0.0f, 0.0f, false,
                                false},
};

static const RockDef ROCK_DEFS[ROCK_COUNT] = {
    [ROCK_NONE] = {"none", NULL, 0.0f, ITEM_NONE, 0},
    [ROCK_GRANITE] = {"granite", N_("rock.granite"), 3500.0f, ITEM_STONE_BLOCK, 10},
    [ROCK_SANDSTONE] = {"sandstone", N_("rock.sandstone"), 2500.0f, ITEM_STONE_BLOCK, 12},
    [ROCK_EARTH] = {"earth", N_("rock.earth"), 1200.0f, ITEM_NONE, 0},
    [ROCK_WOOD] = {"wood", N_("rock.wood"), 0.0f, ITEM_NONE, 0}, /* Felled (core/tree.h). */
};

/* Seasons' days, for the plant tables. */
#define SPRING(day) SEASON_DAY(SEASON_SPRING, day)
#define SUMMER(day) SEASON_DAY(SEASON_SUMMER, day)
#define AUTUMN(day) SEASON_DAY(SEASON_AUTUMN, day)
#define WINTER(day) SEASON_DAY(SEASON_WINTER, day)
/* Plants grow from spring to autumn: what some products' days say. */
#define GROWING_SEASON PLANT_DAYS(SPRING(1), AUTUMN(DAYS_PER_SEASON))

/*
 * Wild plants of a temperate climate (docs/PLANTS.md §7: what each one's
 * temperatures and hardiness are, and why; docs/KNOWLEDGE.md §7 for the food).
 * Food values: see the items (core/item.c). A stock grows back one item per
 * `regrow_hours`: a bramble ripens two handfuls a day at the end of summer,
 * a hazel a little over one nut a day in autumn.
 */
static const PlantDef PLANT_DEFS[PLANT_COUNT] = {
    [PLANT_NONE] = {.id = "none", .move_cost = 1.0f},
    [PLANT_GRASS] = {.id = "grass",
                     .name_key = N_("plant.grass"),
                     .transpiration = 1.0f,
                     .water = {0.05f, 0.5f, 1.4f, 2.2f, 20.0f, 10.0f},
                     .spread_chance = 0.15f,
                     .spread_growth = 0.5f,
                     .spread_range = 1,
                     .growth_per_day = 0.35f,
                     .min_fertility = 0.3f,
                     .move_cost = 1.0f,
                     .t_min = 5.0f,
                     .t_opt_low = 15.0f,
                     .t_opt_high = 25.0f,
                     .t_max = 35.0f,
                     .t_frost_active = -5.0f,
                     .t_frost_hardened = -25.0f,
                     .t_lethal_heat = 45.0f,
                     .rest = PLANT_REST_SLEEPS,
                     .light_min = 0.2f,
                     .light_full = 0.8f,
                     .dark_days = 10.0f,
                     /* Armfuls cut at the ground, which grows from nothing again: the
                      * stock follows the growth (an armful is there within the hour
                      * once the grass is tall enough). */
                     .products = {{.item = ITEM_FRESH_GRASS,
                                   .capacity = 3,
                                   .regrow_hours = 1,
                                   .min_growth = 0.5f}},
                     .harvest = PLANT_HARVEST_RESETS,
                     .grazed = true},
    /* The trees (docs/TREES.md §2). cut_yield: logs of 7 kg, the usable wood of a
     * mature tree (see item.c). Their dead branches and twigs fall to the ground and
     * lie on their foliage (core/tree.h): no stock on the plant. The oak: slow,
     * wanting light, on most ground; it bears flooding. */
    [PLANT_OAK] = {.id = "oak",
                   .name_key = N_("plant.oak"),
                   .transpiration = 1.0f,
                   .water = {0.0f, 0.3f, 1.5f, 2.4f, 60.0f, 40.0f},
                   .spread_chance = 0.03f,
                   .spread_growth = 1.0f,
                   .spread_range = 5,
                   .deciduous = true,
                   .growth_per_day = 0.020f,
                   .min_fertility = 0.5f,
                   .move_cost = 1.6f,
                   .t_min = 5.0f,
                   .t_opt_low = 18.0f,
                   .t_opt_high = 25.0f,
                   .t_max = 35.0f,
                   .t_frost_active = -3.0f,
                   .t_frost_hardened = -30.0f,
                   .t_lethal_heat = 45.0f,
                   .rest = PLANT_REST_SLEEPS,
                   .light_min = 0.15f,
                   .light_full = 0.7f,
                   .dark_days = 30.0f,
                   .cut_item = ITEM_WOOD,
                   .cut_yield = 70},
    /* The beech grows in the shade of others, on drained ground; its young leaves
     * fear late frost. */
    [PLANT_BEECH] = {.id = "beech",
                     .name_key = N_("plant.beech"),
                     .transpiration = 1.0f,
                     .water = {0.1f, 0.4f, 1.2f, 1.5f, 40.0f, 10.0f},
                     .spread_chance = 0.03f,
                     .spread_growth = 1.0f,
                     .spread_range = 3,
                     .deciduous = true,
                     .growth_per_day = 0.022f,
                     .min_fertility = 0.6f,
                     .move_cost = 1.6f,
                     .t_min = 5.0f,
                     .t_opt_low = 16.0f,
                     .t_opt_high = 23.0f,
                     .t_max = 33.0f,
                     .t_frost_active = -2.0f,
                     .t_frost_hardened = -30.0f,
                     .t_lethal_heat = 42.0f,
                     .rest = PLANT_REST_SLEEPS,
                     .light_min = 0.03f,
                     .light_full = 0.4f,
                     .dark_days = 60.0f,
                     .cut_item = ITEM_WOOD,
                     .cut_yield = 65},
    /* The sweet chestnut wants warmth and drained, poorer ground: chestnuts fall in autumn. */
    [PLANT_CHESTNUT] = {.id = "chestnut",
                        .name_key = N_("plant.chestnut"),
                        .transpiration = 1.0f,
                        .water = {0.05f, 0.3f, 1.1f, 1.4f, 50.0f, 10.0f},
                        .spread_chance = 0.03f,
                        .spread_growth = 1.0f,
                        .spread_range = 3,
                        .deciduous = true,
                        .growth_per_day = 0.030f,
                        .min_fertility = 0.4f,
                        .move_cost = 1.6f,
                        .t_min = 7.0f,
                        .t_opt_low = 20.0f,
                        .t_opt_high = 27.0f,
                        .t_max = 37.0f,
                        .t_frost_active = -3.0f,
                        .t_frost_hardened = -20.0f,
                        .t_lethal_heat = 45.0f,
                        .rest = PLANT_REST_SLEEPS,
                        .light_min = 0.1f,
                        .light_full = 0.6f,
                        .dark_days = 30.0f,
                        .cut_item = ITEM_WOOD,
                        .cut_yield = 60,
                        .fall_item = ITEM_CHESTNUTS,
                        .fall_days = PLANT_DAYS(AUTUMN(4), AUTUMN(13)),
                        .fall_yield = 40},
    /* The ash: fast, on rich and moist ground, its winged seeds carried far. */
    [PLANT_ASH] = {.id = "ash",
                   .name_key = N_("plant.ash"),
                   .transpiration = 1.0f,
                   .water = {0.3f, 0.8f, 1.7f, 2.3f, 30.0f, 30.0f},
                   .spread_chance = 0.04f,
                   .spread_growth = 1.0f,
                   .spread_range = 5,
                   .deciduous = true,
                   .growth_per_day = 0.040f,
                   .min_fertility = 0.8f,
                   .move_cost = 1.6f,
                   .t_min = 5.0f,
                   .t_opt_low = 18.0f,
                   .t_opt_high = 25.0f,
                   .t_max = 35.0f,
                   .t_frost_active = -3.0f,
                   .t_frost_hardened = -30.0f,
                   .t_lethal_heat = 45.0f,
                   .rest = PLANT_REST_SLEEPS,
                   .light_min = 0.1f,
                   .light_full = 0.6f,
                   .dark_days = 30.0f,
                   .cut_item = ITEM_WOOD,
                   .cut_yield = 55},
    /* The birch: a pioneer, fast and hardy, on any ground but wanting full light. */
    [PLANT_BIRCH] = {.id = "birch",
                     .name_key = N_("plant.birch"),
                     .transpiration = 0.8f,
                     .water = {0.0f, 0.2f, 1.6f, 2.2f, 60.0f, 20.0f},
                     .spread_chance = 0.05f,
                     .spread_growth = 1.0f,
                     .spread_range = 6,
                     .deciduous = true,
                     .growth_per_day = 0.060f,
                     .min_fertility = 0.1f,
                     .poor_soil = true,
                     .move_cost = 1.6f,
                     .t_min = 4.0f,
                     .t_opt_low = 15.0f,
                     .t_opt_high = 22.0f,
                     .t_max = 32.0f,
                     .t_frost_active = -4.0f,
                     .t_frost_hardened = -40.0f,
                     .t_lethal_heat = 42.0f,
                     .rest = PLANT_REST_SLEEPS,
                     .light_min = 0.2f,
                     .light_full = 0.7f,
                     .dark_days = 20.0f,
                     .cut_item = ITEM_WOOD,
                     .cut_yield = 30},
    /* The wild apple tree, small and low, at edges and in clearings: its apples fall from the end
       of summer. */
    [PLANT_WILD_APPLE] = {.id = "wild_apple",
                          .name_key = N_("plant.wild_apple"),
                          .transpiration = 1.0f,
                          .water = {0.1f, 0.4f, 1.3f, 1.8f, 40.0f, 20.0f},
                          .spread_chance = 0.01f,
                          .spread_growth = 1.0f,
                          .spread_range = 3,
                          .deciduous = true,
                          .growth_per_day = 0.030f,
                          .min_fertility = 0.5f,
                          .move_cost = 1.4f,
                          .t_min = 5.0f,
                          .t_opt_low = 17.0f,
                          .t_opt_high = 25.0f,
                          .t_max = 35.0f,
                          .t_frost_active = -3.0f,
                          .t_frost_hardened = -30.0f,
                          .t_lethal_heat = 45.0f,
                          .rest = PLANT_REST_SLEEPS,
                          .light_min = 0.15f,
                          .light_full = 0.6f,
                          .dark_days = 30.0f,
                          .cut_item = ITEM_WOOD,
                          .cut_yield = 20,
                          .fall_item = ITEM_APPLES,
                          .fall_days = PLANT_DAYS(SUMMER(12), AUTUMN(10)),
                          .fall_yield = 40},
    /* The wild pear tree, narrow, alone at the edges: its pears fall in autumn. */
    [PLANT_WILD_PEAR] = {.id = "wild_pear",
                         .name_key = N_("plant.wild_pear"),
                         .transpiration = 1.0f,
                         .water = {0.05f, 0.35f, 1.3f, 1.8f, 50.0f, 20.0f},
                         .spread_chance = 0.01f,
                         .spread_growth = 1.0f,
                         .spread_range = 3,
                         .deciduous = true,
                         .growth_per_day = 0.025f,
                         .min_fertility = 0.5f,
                         .move_cost = 1.6f,
                         .t_min = 5.0f,
                         .t_opt_low = 17.0f,
                         .t_opt_high = 26.0f,
                         .t_max = 36.0f,
                         .t_frost_active = -3.0f,
                         .t_frost_hardened = -30.0f,
                         .t_lethal_heat = 45.0f,
                         .rest = PLANT_REST_SLEEPS,
                         .light_min = 0.15f,
                         .light_full = 0.6f,
                         .dark_days = 30.0f,
                         .cut_item = ITEM_WOOD,
                         .cut_yield = 30,
                         .fall_item = ITEM_PEARS,
                         .fall_days = PLANT_DAYS(AUTUMN(1), AUTUMN(12)),
                         .fall_yield = 30},
    /* A small tree: its hazelnuts fall to the ground in autumn (items), its
     * dead branches and twigs like those of the other trees (core/tree.h). */
    [PLANT_HAZEL] = {.id = "hazel",
                     .name_key = N_("plant.hazel"),
                     .transpiration = 1.0f,
                     .water = {0.05f, 0.4f, 1.3f, 1.9f, 40.0f, 20.0f},
                     .spread_chance = 0.03f,
                     .spread_growth = 1.0f,
                     .spread_range = 3,
                     .deciduous = true,
                     .growth_per_day = 0.04f,
                     .min_fertility = 0.6f,
                     .move_cost = 1.4f,
                     .t_min = 4.0f,
                     .t_opt_low = 15.0f,
                     .t_opt_high = 22.0f,
                     .t_max = 32.0f,
                     .t_frost_active = -3.0f,
                     .t_frost_hardened = -30.0f,
                     .t_lethal_heat = 42.0f,
                     .rest = PLANT_REST_SLEEPS,
                     .light_min = 0.1f,
                     .light_full = 0.5f,
                     .dark_days = 30.0f,
                     .cut_item = ITEM_WOOD,
                     .cut_yield = 20,
                     .fall_item = ITEM_HAZELNUTS,
                     .fall_days = PLANT_DAYS(AUTUMN(1), AUTUMN(DAYS_PER_SEASON)),
                     .fall_yield = 30},
    /* Thorny canes: slow to walk through. */
    [PLANT_BRAMBLE] = {.id = "bramble",
                       .name_key = N_("plant.bramble"),
                       .transpiration = 1.0f,
                       .water = {0.05f, 0.4f, 1.4f, 1.9f, 30.0f, 15.0f},
                       .spread_chance = 0.06f,
                       .spread_growth = 0.6f,
                       .spread_range = 1,
                       .growth_per_day = 0.12f,
                       .min_fertility = 0.5f,
                       .move_cost = 2.0f,
                       .t_min = 5.0f,
                       .t_opt_low = 18.0f,
                       .t_opt_high = 25.0f,
                       .t_max = 35.0f,
                       .t_frost_active = -5.0f,
                       .t_frost_hardened = -20.0f,
                       .t_lethal_heat = 45.0f,
                       .rest = PLANT_REST_SLEEPS,
                       .light_min = 0.05f,
                       .light_full = 0.5f,
                       .dark_days = 30.0f,
                       .products = {{.item = ITEM_BLACKBERRIES,
                                     .capacity = 9,
                                     .regrow_hours = 12,
                                     .warmth_from = 220.0f, /* The end of summer. */
                                     .lost_below = 10.0f,   /* Rotten in the autumn chill. */
                                     .min_growth = 0.5f}}},
    /* Easily mistaken for lily of the valley or autumn crocus. */
    [PLANT_WILD_GARLIC] = {.id = "wild_garlic",
                           .name_key = N_("plant.wild_garlic"),
                           .transpiration = 0.8f,
                           .water = {0.1f, 0.7f, 1.6f, 2.1f, 10.0f, 20.0f},
                           .spread_chance = 0.04f,
                           .spread_growth = 0.6f,
                           .spread_range = 1,
                           .growth_per_day = 0.25f,
                           .min_fertility = 1.2f,
                           .move_cost = 1.0f,
                           .t_min = 2.0f,
                           .t_opt_low = 8.0f,
                           .t_opt_high = 15.0f,
                           .t_max = 20.0f,
                           .t_frost_active = -5.0f,
                           .t_frost_hardened = -20.0f,
                           .t_lethal_heat = 30.0f,
                           .rest = PLANT_REST_DIES_BACK,
                           .rests_in_heat = true,
                           .light_min = 0.02f,
                           .light_full = 0.2f,
                           .dark_days = 30.0f,
                           .products = {{.item = ITEM_WILD_GARLIC,
                                         .capacity = 3,
                                         .regrow_hours = 24,
                                         .warmth_until =
                                             145.0f, /* Spring leaves, before it flowers. */
                                         .min_growth = 0.3f,
                                         .confusable = true}}},
    /* Young shoots in spring, and again when it grows back in autumn. */
    [PLANT_NETTLE] = {.id = "nettle",
                      .name_key = N_("plant.nettle"),
                      .transpiration = 1.0f,
                      .water = {0.1f, 0.7f, 1.6f, 2.1f, 15.0f, 15.0f},
                      .spread_chance = 0.08f,
                      .spread_growth = 0.6f,
                      .spread_range = 1,
                      .growth_per_day = 0.2f,
                      .min_fertility = 1.2f,
                      .move_cost = 1.2f,
                      .t_min = 5.0f,
                      .t_opt_low = 15.0f,
                      .t_opt_high = 25.0f,
                      .t_max = 32.0f,
                      .t_frost_active = -2.0f,
                      .t_frost_hardened = -25.0f,
                      .t_lethal_heat = 40.0f,
                      .rest = PLANT_REST_DIES_BACK,
                      .light_min = 0.05f,
                      .light_full = 0.4f,
                      .dark_days = 25.0f,
                      .products = {{.item = ITEM_NETTLES,
                                    .capacity = 3,
                                    .regrow_hours = 24,
                                    .warmth_until = 100.0f, /* Young shoots, tough later. */
                                    .min_growth = 0.2f}}},
    [PLANT_PLANTAIN] = {.id = "plantain",
                        .name_key = N_("plant.plantain"),
                        .transpiration = 0.8f,
                        .water = {0.05f, 0.4f, 1.4f, 2.1f, 25.0f, 15.0f},
                        .spread_chance = 0.06f,
                        .spread_growth = 0.6f,
                        .spread_range = 2,
                        .growth_per_day = 0.15f,
                        .min_fertility = 0.6f,
                        .move_cost = 1.0f,
                        .t_min = 5.0f,
                        .t_opt_low = 15.0f,
                        .t_opt_high = 25.0f,
                        .t_max = 35.0f,
                        .t_frost_active = -5.0f,
                        .t_frost_hardened = -20.0f,
                        .t_lethal_heat = 45.0f,
                        .rest = PLANT_REST_SLEEPS,
                        .light_min = 0.2f,
                        .light_full = 0.8f,
                        .dark_days = 10.0f,
                        .products = {{.item = ITEM_PLANTAIN,
                                      .capacity = 2,
                                      .regrow_hours = 24,
                                      .warmth_from = 17.0f, /* Leaves from spring to autumn. */
                                      .lost_below = 5.0f,
                                      .min_growth = 0.3f}}},
    /* Its root is tender from autumn until early spring, woody once it
     * flowers; easily mistaken for hemlock. */
    [PLANT_WILD_CARROT] = {.id = "wild_carrot",
                           .name_key = N_("plant.wild_carrot"),
                           .transpiration = 0.8f,
                           .water = {0.0f, 0.3f, 1.2f, 1.8f, 30.0f, 8.0f},
                           .spread_chance = 0.04f,
                           .spread_growth = 0.8f,
                           .spread_range = 2,
                           .growth_per_day = 0.08f,
                           .min_fertility = 0.6f,
                           .move_cost = 1.0f,
                           .t_min = 4.0f,
                           .t_opt_low = 15.0f,
                           .t_opt_high = 22.0f,
                           .t_max = 32.0f,
                           .t_frost_active = -5.0f,
                           .t_frost_hardened = -15.0f,
                           .t_lethal_heat = 40.0f,
                           .rest = PLANT_REST_SLEEPS,
                           .light_min = 0.2f,
                           .light_full = 0.8f,
                           .dark_days = 10.0f,
                           .products = {{.item = ITEM_WILD_CARROTS,
                                         .capacity = 3,
                                         .regrow_hours = 24,
                                         .warmth_from = 360.0f, /* Tender roots from autumn. */
                                         .min_growth = 0.6f,
                                         .dug = true,
                                         .confusable = true}},
                           .harvest = PLANT_HARVEST_RESETS},
    /* In the shallows and on the shore. A spreading rootstock: digging part
     * of it up leaves the rest growing. */
    [PLANT_CATTAIL] = {.id = "cattail",
                       .name_key = N_("plant.cattail"),
                       .transpiration = 1.2f,
                       .water = {1.3f, 1.8f, 3.0f, 3.5f, 15.0f, 30.0f},
                       .spread_chance = 0.08f,
                       .spread_growth = 0.6f,
                       .spread_range = 1,
                       .growth_per_day = 0.1f,
                       .wetland = true,
                       .move_cost = 1.3f,
                       .t_min = 10.0f,
                       .t_opt_low = 20.0f,
                       .t_opt_high = 30.0f,
                       .t_max = 38.0f,
                       .t_frost_active = -5.0f,
                       .t_frost_hardened = -25.0f,
                       .t_lethal_heat = 45.0f,
                       .rest = PLANT_REST_SLEEPS,
                       .light_min = 0.2f,
                       .light_full = 0.8f,
                       .dark_days = 10.0f,
                       .products = {{.item = ITEM_CATTAIL_RHIZOMES,
                                     .capacity = 4,
                                     .regrow_hours = 48,
                                     .min_growth = 0.5f,
                                     .dug = true}}},
    /* Appear fully grown, picked or gone within days (see core/game.c). Some
     * are deadly: a matter for skilled gatherers. */
    [PLANT_MUSHROOMS] = {.id = "mushrooms",
                         .name_key = N_("plant.mushrooms"),
                         .move_cost = 1.0f,
                         /* Fungi: they live between cool and mild, no light. The first
                          * hard frost kills them. */
                         .t_min = 2.0f,
                         .t_opt_low = 8.0f,
                         .t_opt_high = 18.0f,
                         .t_max = 25.0f,
                         .t_frost_active = -1.0f,
                         .t_frost_hardened = -1.0f,
                         .t_lethal_heat = 35.0f,
                         .products = {{.item = ITEM_MUSHROOMS,
                                       .capacity = 3,
                                       .regrow_hours = 24,
                                       .lost_below = 3.0f, /* Gone at the first frosts. */
                                       .confusable = true}},
                         .harvest = PLANT_HARVEST_REMOVES},
    /* The carpet of the dark forest floor, on any ground, slow to come back
     * once peeled off (docs/PLANTS.md §10): bedding and tinder. */
    [PLANT_MOSS] =
        {.id = "moss",
         .name_key = N_("plant.moss"),
         .transpiration = 0.4f,
         .water = {0.0f, 0.6f, 1.9f, 2.5f, 60.0f, 20.0f},
         .spread_chance = 0.05f,
         .spread_growth = 0.5f,
         .spread_range = 1,
         .growth_per_day = 0.04f,
         .min_fertility = 0.1f,
         .poor_soil = true,
         .move_cost = 1.0f,
         .t_min = 1.0f,
         .t_opt_low = 8.0f,
         .t_opt_high = 18.0f,
         .t_max = 28.0f,
         .t_frost_active = -10.0f,
         .t_frost_hardened = -35.0f,
         .t_lethal_heat = 38.0f,
         .rest = PLANT_REST_SLEEPS,
         .light_min = 0.01f,
         .light_full = 0.15f,
         .dark_days = 60.0f,
         .products = {{.item = ITEM_MOSS, .capacity = 2, .regrow_hours = 1, .min_growth = 0.5f}},
         .harvest = PLANT_HARVEST_RESETS},
    /* Wood sorrel: clover-like leaves, sour, in the deepest shade. */
    [PLANT_WOOD_SORREL] = {.id = "wood_sorrel",
                           .name_key = N_("plant.wood_sorrel"),
                           .transpiration = 0.6f,
                           .water = {0.1f, 0.6f, 1.5f, 2.0f, 10.0f, 15.0f},
                           .spread_chance = 0.06f,
                           .spread_growth = 0.5f,
                           .spread_range = 1,
                           .growth_per_day = 0.15f,
                           .min_fertility = 0.6f,
                           .move_cost = 1.0f,
                           .t_min = 3.0f,
                           .t_opt_low = 8.0f,
                           .t_opt_high = 18.0f,
                           .t_max = 26.0f,
                           .t_frost_active = -5.0f,
                           .t_frost_hardened = -25.0f,
                           .t_lethal_heat = 35.0f,
                           .rest = PLANT_REST_SLEEPS,
                           .light_min = 0.02f,
                           .light_full = 0.15f,
                           .dark_days = 40.0f,
                           .products = {{.item = ITEM_WOOD_SORREL,
                                         .capacity = 2,
                                         .regrow_hours = 24,
                                         .warmth_from = 15.0f, /* Leaves from spring on. */
                                         .min_growth = 0.3f}}},
    /* Bracken: fronds that die down in winter. Cut once brown, in autumn,
     * they are dry bedding, thatch and tinder; cut, they only come back the
     * next year. Poisonous to eat. */
    [PLANT_BRACKEN] = {.id = "bracken",
                       .name_key = N_("plant.bracken"),
                       .transpiration = 1.0f,
                       .water = {0.1f, 0.4f, 1.2f, 1.6f, 25.0f, 6.0f},
                       .spread_chance = 0.08f,
                       .spread_growth = 0.6f,
                       .spread_range = 1,
                       .growth_per_day = 0.15f,
                       .min_fertility = 0.1f,
                       .poor_soil = true,
                       .move_cost = 1.3f,
                       .t_min = 6.0f,
                       .t_opt_low = 15.0f,
                       .t_opt_high = 25.0f,
                       .t_max = 33.0f,
                       .t_frost_active = -2.0f, /* Young fronds are tender. */
                       .t_frost_hardened = -20.0f,
                       .t_lethal_heat = 42.0f,
                       .rest = PLANT_REST_DIES_BACK,
                       .light_min = 0.1f,
                       .light_full = 0.6f,
                       .dark_days = 20.0f,
                       .products = {{.item = ITEM_BRACKEN,
                                     .capacity = 3,
                                     .regrow_hours = 24,
                                     .warmth_from = 270.0f, /* Brown at the start of autumn. */
                                     .min_growth = 0.6f}},
                       .harvest = PLANT_HARVEST_RESETS},
    /* Runners in clearings and along the woods: small sweet fruit at the
     * start of summer. */
    [PLANT_WILD_STRAWBERRY] = {.id = "wild_strawberry",
                               .name_key = N_("plant.wild_strawberry"),
                               .transpiration = 0.7f,
                               .water = {0.05f, 0.5f, 1.3f, 1.8f, 15.0f, 10.0f},
                               .spread_chance = 0.12f,
                               .spread_growth = 0.5f,
                               .spread_range = 1,
                               .growth_per_day = 0.15f,
                               .min_fertility = 0.6f,
                               .move_cost = 1.0f,
                               .t_min = 5.0f,
                               .t_opt_low = 15.0f,
                               .t_opt_high = 24.0f,
                               .t_max = 32.0f,
                               .t_frost_active = -5.0f,
                               .t_frost_hardened = -20.0f,
                               .t_lethal_heat = 42.0f,
                               .rest = PLANT_REST_SLEEPS,
                               .light_min = 0.08f,
                               .light_full = 0.5f,
                               .dark_days = 20.0f,
                               .products = {{.item = ITEM_WILD_STRAWBERRIES,
                                             .capacity = 3,
                                             .regrow_hours = 24,
                                             .warmth_from = 105.0f,  /* The start of summer, */
                                             .warmth_until = 215.0f, /* for a few weeks. */
                                             .min_growth = 0.5f}}},
    /* Bilberry: a low shrub of poor, sandy or stony ground, in the open or
     * under thin woods; berries in the middle of summer. */
    [PLANT_BILBERRY] = {.id = "bilberry",
                        .name_key = N_("plant.bilberry"),
                        .transpiration = 0.6f,
                        .water = {0.1f, 0.6f, 1.4f, 1.7f, 15.0f, 8.0f},
                        .spread_chance = 0.03f,
                        .spread_growth = 0.7f,
                        .spread_range = 1,
                        .growth_per_day = 0.05f,
                        .min_fertility = 0.1f,
                        .poor_soil = true,
                        .move_cost = 1.1f,
                        .t_min = 5.0f,
                        .t_opt_low = 12.0f,
                        .t_opt_high = 20.0f,
                        .t_max = 28.0f,
                        .t_frost_active = -5.0f,
                        .t_frost_hardened = -30.0f,
                        .t_lethal_heat = 38.0f,
                        .rest = PLANT_REST_SLEEPS,
                        .light_min = 0.06f,
                        .light_full = 0.4f,
                        .dark_days = 30.0f,
                        .products = {{.item = ITEM_BILBERRIES,
                                      .capacity = 5,
                                      .regrow_hours = 24,
                                      .warmth_from = 160.0f,
                                      .warmth_until = 290.0f,
                                      .lost_below = 10.0f,
                                      .min_growth = 0.5f}}},
    /* Dog rose: a thorny shrub of the open and of the edges of woods. Its
     * hips ripen in autumn and stay on through the winter, until the bush
     * wakes in spring: food when there is little else. */
    [PLANT_DOG_ROSE] = {.id = "dog_rose",
                        .name_key = N_("plant.dog_rose"),
                        .transpiration = 0.9f,
                        .water = {0.0f, 0.3f, 1.2f, 1.8f, 40.0f, 10.0f},
                        .spread_chance = 0.02f,
                        .spread_growth = 0.8f,
                        .spread_range = 3,
                        .growth_per_day = 0.06f,
                        .min_fertility = 0.5f,
                        .move_cost = 2.0f,
                        .t_min = 5.0f,
                        .t_opt_low = 15.0f,
                        .t_opt_high = 25.0f,
                        .t_max = 35.0f,
                        .t_frost_active = -3.0f,
                        .t_frost_hardened = -30.0f,
                        .t_lethal_heat = 45.0f,
                        .rest = PLANT_REST_SLEEPS,
                        .light_min = 0.15f,
                        .light_full = 0.7f,
                        .dark_days = 25.0f,
                        .products = {{.item = ITEM_ROSE_HIPS,
                                      .capacity = 6,
                                      .regrow_hours = 24,
                                      .warmth_from = 330.0f, /* Red at the start of autumn. */
                                      .min_growth = 0.5f}}},
    /* Heather: an evergreen low shrub of sand and gravel, in the sun. Cut,
     * it is bedding, thatch and fuel, and slow to grow back. */
    [PLANT_HEATHER] = {.id = "heather",
                       .name_key = N_("plant.heather"),
                       .transpiration = 0.6f,
                       .water = {0.0f, 0.2f, 1.1f, 1.5f, 40.0f, 6.0f},
                       .spread_chance = 0.03f,
                       .spread_growth = 0.7f,
                       .spread_range = 2,
                       .growth_per_day = 0.04f,
                       .min_fertility = 0.1f,
                       .poor_soil = true,
                       .move_cost = 1.2f,
                       .t_min = 5.0f,
                       .t_opt_low = 12.0f,
                       .t_opt_high = 22.0f,
                       .t_max = 32.0f,
                       .t_frost_active = -5.0f,
                       .t_frost_hardened = -30.0f,
                       .t_lethal_heat = 42.0f,
                       .rest = PLANT_REST_SLEEPS,
                       .light_min = 0.25f,
                       .light_full = 0.8f,
                       .dark_days = 15.0f,
                       .products = {{.item = ITEM_HEATHER,
                                     .capacity = 2,
                                     .regrow_hours = 1,
                                     .min_growth = 0.6f}},
                       .harvest = PLANT_HARVEST_RESETS},
    /* Common reed: in the shallows, taller than a man. Its stems stand dry
     * from autumn through the winter, when they are cut: the best thatch.
     * The rootstock grows them again in spring. */
    [PLANT_REED] = {.id = "reed",
                    .name_key = N_("plant.reed"),
                    .transpiration = 1.2f,
                    .water = {1.2f, 1.7f, 3.0f, 3.5f, 20.0f, 30.0f},
                    .spread_chance = 0.08f,
                    .spread_growth = 0.6f,
                    .spread_range = 1,
                    .growth_per_day = 0.12f,
                    .wetland = true,
                    .move_cost = 1.4f,
                    .t_min = 7.0f,
                    .t_opt_low = 18.0f,
                    .t_opt_high = 30.0f,
                    .t_max = 38.0f,
                    .t_frost_active = -5.0f,
                    .t_frost_hardened = -30.0f,
                    .t_lethal_heat = 45.0f,
                    .rest = PLANT_REST_SLEEPS,
                    .light_min = 0.25f,
                    .light_full = 0.8f,
                    .dark_days = 15.0f,
                    .products = {{.item = ITEM_REEDS,
                                  .capacity = 3,
                                  .regrow_hours = 24,
                                  .warmth_from = 260.0f, /* Ripe and drying from autumn. */
                                  .min_growth = 0.6f}},
                    .harvest = PLANT_HARVEST_RESETS},
};

#undef SPRING
#undef SUMMER
#undef AUTUMN
#undef WINTER
#undef GROWING_SEASON

const TerrainDef *terrain_def(Terrain terrain)
{
    return ((unsigned)terrain < TERRAIN_COUNT) ? &TERRAIN_DEFS[terrain] : NULL;
}

const RockDef *rock_def(RockType rock)
{
    return ((unsigned)rock < ROCK_COUNT) ? &ROCK_DEFS[rock] : NULL;
}

const PlantDef *plant_def(PlantType plant)
{
    return ((unsigned)plant < PLANT_COUNT) ? &PLANT_DEFS[plant] : NULL;
}

const PlantProduct *plant_product(PlantType type, int slot)
{
    if ((unsigned)type >= PLANT_COUNT || (unsigned)slot >= PLANT_PRODUCTS_MAX ||
        PLANT_DEFS[type].products[slot].item == ITEM_NONE) {
        return NULL;
    }
    return &PLANT_DEFS[type].products[slot];
}

ItemType plant_stock_item(PlantType type, int slot)
{
    const PlantProduct *product = plant_product(type, slot);
    return product != NULL ? product->item : ITEM_NONE;
}

int plant_stock_capacity(const Plant *plant, int slot)
{
    const PlantProduct *product = plant_product((PlantType)plant->type, slot);
    if (product == NULL) {
        return 0;
    }
    float growth = clampf(plant->growth, 0.0f, 1.0f);
    if (growth < product->min_growth) {
        return 0;
    }
    /* Wider trees (core/tree.h) hold more, by the area of their trunk. */
    int area = plant->size > 1 ? plant->size * plant->size : 1;
    return (int)((float)(product->capacity * area) * growth + 0.5f);
}

/* Regrowth hours a full stock holds. */
static int stock_limit(const PlantDef *def, const Plant *plant, int slot)
{
    return plant_stock_capacity(plant, slot) * def->products[slot].regrow_hours;
}

int plant_stock_available(const Plant *plant, int slot)
{
    int capacity = plant_stock_capacity(plant, slot);
    if (capacity <= 0) {
        return 0;
    }
    int available = plant->stock[slot] / PLANT_DEFS[plant->type].products[slot].regrow_hours;
    return available < capacity ? available : capacity;
}

int plant_take_stock(Plant *plant, int slot, int count)
{
    int available = plant_stock_available(plant, slot);
    int taken = count < available ? count : available;
    if (taken <= 0) {
        return 0;
    }
    int hours = taken * PLANT_DEFS[plant->type].products[slot].regrow_hours;
    plant->stock[slot] = (uint16_t)(plant->stock[slot] - hours);
    return taken;
}

bool plant_days_contain(PlantDays days, int day_of_year)
{
    return day_of_year >= 0 && day_of_year < DAYS_PER_YEAR && ((days >> day_of_year) & 1u);
}

bool plant_product_in_season(const Plant *plant, int slot)
{
    const PlantProduct *product = plant_product((PlantType)plant->type, slot);
    if (product == NULL) {
        return false;
    }
    return plant->warmth >= product->warmth_from &&
           (product->warmth_until <= 0.0f || plant->warmth < product->warmth_until) &&
           (product->lost_below == 0.0f ||
            (float)plant->climate / PLANT_CLIMATE_SCALE >= product->lost_below);
}

void plant_regrow_stocks(Plant *plant, int hours)
{
    if (hours < 0 || (unsigned)plant->type >= PLANT_COUNT) {
        return;
    }
    const PlantDef *def = &PLANT_DEFS[plant->type];
    for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
        if (def->products[slot].item == ITEM_NONE) {
            continue;
        }
        if (!plant_product_in_season(plant, slot)) {
            plant->stock[slot] = 0; /* Fallen, rotten or woody. */
            continue;
        }
        /* A stock above the limit (the plant was eaten down) drops to it. */
        plant->stock[slot] =
            (uint16_t)clampi((int)plant->stock[slot] + hours, 0, stock_limit(def, plant, slot));
    }
}

float plant_temperature_response(PlantType type, float celsius)
{
    if ((unsigned)type >= PLANT_COUNT) {
        return 0.0f;
    }
    const PlantDef *def = &PLANT_DEFS[type];
    if (celsius <= def->t_min || celsius >= def->t_max) {
        return 0.0f;
    }
    if (celsius < def->t_opt_low) {
        return (celsius - def->t_min) / (def->t_opt_low - def->t_min);
    }
    if (celsius > def->t_opt_high) {
        return (def->t_max - celsius) / (def->t_max - def->t_opt_high);
    }
    return 1.0f;
}

float plant_light_response(PlantType type, float light)
{
    if ((unsigned)type >= PLANT_COUNT) {
        return 0.0f;
    }
    const PlantDef *def = &PLANT_DEFS[type];
    if (def->light_full <= def->light_min) {
        return 1.0f; /* Needs none. */
    }
    return clampf((light - def->light_min) / (def->light_full - def->light_min), 0.0f, 1.0f);
}

float plant_water_response(PlantType type, float moisture)
{
    if ((unsigned)type >= PLANT_COUNT) {
        return 0.0f;
    }
    const PlantWater *water = &PLANT_DEFS[type].water;
    if (water->max <= water->min) {
        return 1.0f; /* Needs nothing. */
    }
    if (moisture <= water->min || moisture >= water->max) {
        return 0.0f;
    }
    if (moisture < water->opt_low) {
        return (moisture - water->min) / (water->opt_low - water->min);
    }
    if (moisture > water->opt_high) {
        return (water->max - moisture) / (water->max - water->opt_high);
    }
    return 1.0f;
}

/* The water a plant meets, 0 .. 1: 1 when its ground is not known. */
static float water_response(const Plant *plant, const PlantConditions *conditions)
{
    return conditions->moisture_known
               ? plant_water_response((PlantType)plant->type, conditions->moisture)
               : 1.0f;
}

/* What drought or drowning roots take of a plant's health in an hour (0:
 * nothing). Only an awake plant dries out; drowning reaches a resting one. */
static float water_harm(const PlantDef *def, PlantState state, const PlantConditions *conditions)
{
    const PlantWater *water = &def->water;
    if (!conditions->moisture_known || water->max <= water->min) {
        return 0.0f;
    }
    if (state == PLANT_STATE_AWAKE && conditions->moisture < water->min &&
        water->drought_days > 0.0f) {
        return 1.0f / (water->drought_days * (float)HOURS_PER_DAY);
    }
    if (conditions->moisture > water->max && water->flood_days > 0.0f) {
        return 1.0f / (water->flood_days * (float)HOURS_PER_DAY);
    }
    return 0.0f;
}

float plant_frost_limit(const Plant *plant)
{
    const PlantDef *def = &PLANT_DEFS[plant->type];
    return lerpf(def->t_frost_active, def->t_frost_hardened, (float)plant->hardening / 255.0f);
}

float plant_health(const Plant *plant)
{
    return 1.0f - (float)plant->damage / (float)PLANT_DAMAGE_MAX;
}

static PlantState plant_state_of(const Plant *plant)
{
    return (PlantState)(plant->state & ~PLANT_CLIMATE_KNOWN);
}

static void set_state(Plant *plant, PlantState state)
{
    plant->state = (uint8_t)(state | (plant->state & PLANT_CLIMATE_KNOWN));
}

/* The state a plant takes in a climate that has lasted: no hysteresis. */
static PlantState state_in_climate(const PlantDef *def, float climate)
{
    if (climate < def->t_min) {
        return def->rest == PLANT_REST_DIES_BACK ? PLANT_STATE_DIED_BACK : PLANT_STATE_RESTING;
    }
    if (def->rests_in_heat && climate > def->t_max) {
        return PLANT_STATE_SUMMER_DORMANT;
    }
    return PLANT_STATE_AWAKE;
}

/* Whether the state is that of a plant with nothing above ground. */
static bool state_is_underground(PlantState state)
{
    return state == PLANT_STATE_DIED_BACK || state == PLANT_STATE_SUMMER_DORMANT;
}

static void set_climate(Plant *plant, float celsius)
{
    plant->climate =
        (int16_t)clampi((int)lroundf(celsius * PLANT_CLIMATE_SCALE), INT16_MIN, INT16_MAX);
    plant->state |= PLANT_CLIMATE_KNOWN;
}

/* What dies back leaves nothing above ground. */
static void die_back(Plant *plant)
{
    plant->growth = 0.0f;
    plant->warmth = 0.0f;
    memset(plant->stock, 0, sizeof(plant->stock));
}

/* The fertility a plant grows by on ground of `fertility`: 1.0 for the
 * plants of the shallows, and for those of poor ground wherever they live
 * (0 below their minimum, where they do not grow). */
static float growing_fertility(const PlantDef *def, float fertility)
{
    if (def->wetland) {
        return 1.0f;
    }
    if (def->poor_soil) {
        return fertility >= def->min_fertility ? 1.0f : 0.0f;
    }
    return fertility;
}

/* One hour of a plant's life. Returns false when it died. */
static bool advance_hour(Plant *plant, const PlantDef *def, const PlantConditions *conditions)
{
    float temperature = conditions->temperature;
    if (!(plant->state & PLANT_CLIMATE_KNOWN)) {
        /* Made by hand, not settled: its climate is what it finds. */
        set_climate(plant, temperature);
        PlantState state = state_in_climate(def, temperature);
        set_state(plant, state);
        plant->hardening = temperature < def->t_min ? 255 : 0;
        if (state_is_underground(state)) {
            die_back(plant);
        }
    }
    float climate = (float)plant->climate / PLANT_CLIMATE_SCALE;
    climate += (temperature - climate) / PLANT_CLIMATE_HOURS;
    set_climate(plant, climate);
    climate = (float)plant->climate / PLANT_CLIMATE_SCALE;

    /* Rest and wake: a few days of cold (or, for some, of heat) and the other way round. */
    PlantState state = plant_state_of(plant);
    PlantState wanted = state_in_climate(def, climate);
    if (state == PLANT_STATE_AWAKE && wanted != PLANT_STATE_AWAKE) {
        set_state(plant, wanted);
        if (state_is_underground(wanted)) {
            die_back(plant);
        }
    } else if (state == PLANT_STATE_SUMMER_DORMANT) {
        if (climate < def->t_min) {
            set_state(plant,
                      PLANT_STATE_DIED_BACK); /* The winter has come: now it waits for spring. */
        }
    } else if (state != PLANT_STATE_AWAKE &&
               state_in_climate(def, climate - PLANT_REST_HYSTERESIS) == PLANT_STATE_AWAKE &&
               state_in_climate(def, climate + PLANT_REST_HYSTERESIS) == PLANT_STATE_AWAKE) {
        set_state(plant, PLANT_STATE_AWAKE);
        plant->warmth = 0.0f; /* A new year of growth: the fruit ripens again. */
    }
    state = plant_state_of(plant);

    /* The cold hardens, the warmth softens. */
    if (climate < def->t_min) {
        plant->hardening = (uint8_t)clampi(plant->hardening + PLANT_HARDEN_PER_HOUR, 0, 255);
    } else {
        plant->hardening = (uint8_t)clampi(plant->hardening - PLANT_SOFTEN_PER_HOUR, 0, 255);
    }

    /* An awake plant gathers the warmth that ripens its products. */
    if (plant_state_of(plant) == PLANT_STATE_AWAKE && temperature > def->t_min) {
        plant->warmth += (temperature - def->t_min) / (float)HOURS_PER_DAY;
    }

    /* Frost and heat wear it down (not what waits under the ground), good weather mends it. */
    if (!state_is_underground(state)) {
        float limit = plant_frost_limit(plant);
        float harm = 0.0f;
        if (temperature < limit) {
            harm = PLANT_FROST_DAMAGE * (limit - temperature);
        } else if (temperature > def->t_lethal_heat) {
            harm = PLANT_HEAT_DAMAGE * (temperature - def->t_lethal_heat);
        } else if ((harm = water_harm(def, state, conditions)) > 0.0f) {
            /* Drying out, or drowning. */
        } else if (state == PLANT_STATE_AWAKE && def->dark_days > 0.0f &&
                   conditions->light_peak < def->light_min) {
            harm = 1.0f / (def->dark_days * (float)HOURS_PER_DAY); /* Starving in the dark. */
        }
        float damage = (float)plant->damage + harm * (float)PLANT_DAMAGE_MAX;
        if (harm == 0.0f && state == PLANT_STATE_AWAKE) {
            damage -= PLANT_RECOVERY * (float)PLANT_DAMAGE_MAX;
        }
        if (plant_is_tree(plant->type) && plant->growth >= TREE_CROWN_FROM) {
            damage = fminf(damage, (1.0f - PLANT_TREE_HEALTH_FLOOR) * (float)PLANT_DAMAGE_MAX);
        } else if (damage >= (float)PLANT_DAMAGE_MAX) {
            return false;
        }
        plant->damage = (uint16_t)clampf(damage, 0.0f, (float)(PLANT_DAMAGE_MAX - 1));
    }

    /* Stocks grow back while it is awake; waiting plants only keep within their limits. */
    if (state_is_underground(state)) {
        memset(plant->stock, 0, sizeof(plant->stock));
    } else {
        plant_regrow_stocks(plant, state == PLANT_STATE_AWAKE ? 1 : 0);
    }

    /* Growth. */
    float fertility = growing_fertility(def, conditions->fertility);
    if (state == PLANT_STATE_AWAKE && plant->growth < 1.0f && fertility >= def->min_fertility) {
        float rate = def->growth_per_day / PLANT_BEST_DAY_HOURS * fertility *
                     plant_temperature_response((PlantType)plant->type, temperature) *
                     plant_light_response((PlantType)plant->type, conditions->light) *
                     water_response(plant, conditions) * plant_health(plant);
        plant->growth = fminf(plant->growth + rate, 1.0f);
    }
    return true;
}

void plant_advance(Plant *plant, const PlantConditions *conditions, int hours)
{
    if (plant->type == PLANT_NONE || (unsigned)plant->type >= PLANT_COUNT) {
        return;
    }
    const PlantDef *def = &PLANT_DEFS[plant->type];
    for (int hour = 0; hour < hours; hour++) {
        if (!advance_hour(plant, def, conditions)) {
            memset(plant, 0, sizeof(*plant)); /* Dead: the tile is bare. */
            return;
        }
    }
    if (def->harvest == PLANT_HARVEST_REMOVES) {
        bool bare = true;
        for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
            bare = bare && plant_stock_available(plant, slot) == 0;
        }
        if (bare) {
            memset(plant, 0, sizeof(*plant));
        }
    }
}

float plant_growth_rate(const Plant *plant, const PlantConditions *conditions)
{
    const PlantDef *def = &PLANT_DEFS[plant->type];
    float fertility = growing_fertility(def, conditions->fertility);
    if (plant_state_of(plant) != PLANT_STATE_AWAKE || fertility < def->min_fertility) {
        return 0.0f;
    }
    return def->growth_per_day * fertility *
           plant_temperature_response((PlantType)plant->type, conditions->temperature) *
           plant_light_response((PlantType)plant->type, conditions->light_peak) *
           water_response(plant, conditions) * plant_health(plant);
}

const char *plant_status_name_key(PlantStatus status)
{
    static const char *const KEYS[PLANT_STATUS_COUNT] = {
        [PLANT_STATUS_GROWING] = N_("plant.status.growing"),
        [PLANT_STATUS_SLOWED_COLD] = N_("plant.status.slowed_cold"),
        [PLANT_STATUS_SLOWED_HOT] = N_("plant.status.slowed_hot"),
        [PLANT_STATUS_SLOWED_DARK] = N_("plant.status.slowed_dark"),
        [PLANT_STATUS_TOO_DARK] = N_("plant.status.too_dark"),
        [PLANT_STATUS_TOO_COLD] = N_("plant.status.too_cold"),
        [PLANT_STATUS_TOO_HOT] = N_("plant.status.too_hot"),
        [PLANT_STATUS_FROST] = N_("plant.status.frost"),
        [PLANT_STATUS_HEAT] = N_("plant.status.heat"),
        [PLANT_STATUS_SLOWED_DRY] = N_("plant.status.slowed_dry"),
        [PLANT_STATUS_SLOWED_WET] = N_("plant.status.slowed_wet"),
        [PLANT_STATUS_DROUGHT] = N_("plant.status.drought"),
        [PLANT_STATUS_WATERLOGGED] = N_("plant.status.waterlogged"),
        [PLANT_STATUS_RESTING] = N_("plant.status.resting"),
        [PLANT_STATUS_DIED_BACK] = N_("plant.status.died_back"),
    };
    return (unsigned)status < PLANT_STATUS_COUNT ? KEYS[status] : KEYS[PLANT_STATUS_GROWING];
}

PlantStatus plant_status(const Plant *plant, const PlantConditions *conditions)
{
    const PlantDef *def = &PLANT_DEFS[plant->type];
    switch (plant_state_of(plant)) {
    case PLANT_STATE_DIED_BACK:
    case PLANT_STATE_SUMMER_DORMANT: return PLANT_STATUS_DIED_BACK;
    case PLANT_STATE_RESTING: return PLANT_STATUS_RESTING;
    case PLANT_STATE_AWAKE: break;
    }
    float temperature = conditions->temperature;
    if (temperature < plant_frost_limit(plant)) {
        return PLANT_STATUS_FROST;
    }
    if (temperature > def->t_lethal_heat) {
        return PLANT_STATUS_HEAT;
    }
    if (water_harm(def, PLANT_STATE_AWAKE, conditions) > 0.0f) {
        return conditions->moisture < def->water.min ? PLANT_STATUS_DROUGHT
                                                     : PLANT_STATUS_WATERLOGGED;
    }
    float response = plant_temperature_response((PlantType)plant->type, temperature);
    if (response <= 0.0f) {
        return temperature < def->t_opt_low ? PLANT_STATUS_TOO_COLD : PLANT_STATUS_TOO_HOT;
    }
    float light = plant_light_response((PlantType)plant->type, conditions->light_peak);
    if (light <= 0.0f) {
        return PLANT_STATUS_TOO_DARK;
    }
    if (response < 1.0f) {
        return temperature < def->t_opt_low ? PLANT_STATUS_SLOWED_COLD : PLANT_STATUS_SLOWED_HOT;
    }
    if (water_response(plant, conditions) < 1.0f) {
        return conditions->moisture < def->water.opt_low ? PLANT_STATUS_SLOWED_DRY
                                                         : PLANT_STATUS_SLOWED_WET;
    }
    return light < 1.0f ? PLANT_STATUS_SLOWED_DARK : PLANT_STATUS_GROWING;
}

int plant_growth_stage(const Plant *plant)
{
    int stage = (int)(clampf(plant->growth, 0.0f, 1.0f) * (float)PLANT_GROWTH_STAGES);
    return clampi(stage, 0, PLANT_GROWTH_STAGES - 1);
}

int plant_food_slot(PlantType type)
{
    for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
        ItemType item = plant_stock_item(type, slot);
        if (item != ITEM_NONE && item_is_food(item)) {
            return slot;
        }
    }
    return -1;
}

bool plant_has_food(const Plant *plant)
{
    int slot = plant_food_slot((PlantType)plant->type);
    return slot >= 0 && plant_stock_available(plant, slot) > 0;
}

void plant_harvested(Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    Plant *plant = &map->plants[map_index(map, x, y, z)];
    switch (plant_def((PlantType)plant->type)->harvest) {
    case PLANT_HARVEST_KEEPS: break;
    case PLANT_HARVEST_RESETS:
        plant->growth = 0.0f; /* Grows again from what is left in the ground. */
        memset(plant->stock, 0, sizeof(plant->stock));
        break;
    case PLANT_HARVEST_REMOVES: map_remove_plant(map, x, y, z); break;
    }
}

bool plant_is_dormant(const Plant *plant)
{
    return state_is_underground(plant_state_of(plant));
}

/* The degree-days a plant that woke at the start of the year has gathered
 * above `t_min` by a day of the year, in the expected climate (a table made
 * once: world generation settles every plant). */
static float expected_warmth(const PlantDef *def, int day_of_year)
{
    static float table[PLANT_COUNT][DAYS_PER_YEAR];
    static bool ready[PLANT_COUNT];
    int type = (int)(def - PLANT_DEFS);
    if (!ready[type]) {
        float sum = 0.0f;
        for (int day = 0; day < DAYS_PER_YEAR; day++) {
            float mean = temperature_season_mean((uint64_t)day * TICKS_PER_DAY + TICKS_PER_DAY / 2);
            sum += mean > def->t_min ? mean - def->t_min : 0.0f;
            table[type][day] = sum;
        }
        ready[type] = true;
    }
    return table[type][clampi(day_of_year, 0, DAYS_PER_YEAR - 1)];
}

void plant_settle(Plant *plant, int day_of_year)
{
    if ((unsigned)plant->type >= PLANT_COUNT) {
        return;
    }
    const PlantDef *def = &PLANT_DEFS[plant->type];
    /* The climate it was born into: the expected one of that day. */
    float climate =
        temperature_season_mean((uint64_t)day_of_year * TICKS_PER_DAY + TICKS_PER_DAY / 2);
    set_climate(plant, climate);
    PlantState state = state_in_climate(def, climate);
    set_state(plant, state);
    plant->hardening = climate < def->t_min ? 255 : 0;
    plant->warmth = expected_warmth(def, day_of_year);
    if (state_is_underground(state)) {
        die_back(plant);
    }
    for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
        bool full = !state_is_underground(state) && def->products[slot].item != ITEM_NONE &&
                    plant_product_in_season(plant, slot);
        int limit = full ? stock_limit(def, plant, slot) : 0;
        plant->stock[slot] = (uint16_t)clampi(limit, 0, UINT16_MAX);
    }
}

bool map_init(Map *map, int width, int height, int depth)
{
    memset(map, 0, sizeof(*map));
    if (width < MAP_MIN_SIZE || height < MAP_MIN_SIZE || width > MAP_MAX_SIZE ||
        height > MAP_MAX_SIZE || depth < 1 || depth > MAP_MAX_LEVELS) {
        return false;
    }

    size_t count = (size_t)width * (size_t)height * (size_t)depth;
    map->terrain = malloc(count);
    map->rock = calloc(count, 1);
    map->slope = calloc(count, 1);
    map->plants = calloc(count, sizeof(Plant));
    map->cobbles = calloc(count, 1);
    map->flint = calloc(count, 1);
    map->branches = calloc(count, 1);
    map->twigs = calloc(count, 1);
    map->nuts = calloc(count, 1);
    map->tree_species = calloc(count, 1);
    map->structure = calloc(count, 1);
    map->structure_part = calloc(count, 1);
    map->structure_quality = calloc(count, 1);
    map->structure_hit_points = calloc(count, sizeof(*map->structure_hit_points));
    map->structure_woods = calloc(count, sizeof(*map->structure_woods));
    map->roof_quality = calloc(count, 1);
    map->roof_woods = calloc(count, sizeof(*map->roof_woods));
    map->forbidden = calloc(count, 1);
    map->doors = calloc(count, 1);
    size_t blocks = (size_t)((width + MAP_LIGHT_BLOCK - 1) / MAP_LIGHT_BLOCK) *
                    (size_t)((height + MAP_LIGHT_BLOCK - 1) / MAP_LIGHT_BLOCK);
    map->light_stamps = calloc(blocks, sizeof(*map->light_stamps));
    map->light_solid_stamps = calloc(blocks, sizeof(*map->light_solid_stamps));
    if (map->light_stamps == NULL || map->light_solid_stamps == NULL || map->doors == NULL ||
        map->forbidden == NULL || map->terrain == NULL || map->rock == NULL || map->slope == NULL ||
        map->plants == NULL || map->cobbles == NULL || map->flint == NULL ||
        map->branches == NULL || map->twigs == NULL || map->nuts == NULL ||
        map->tree_species == NULL || map->structure == NULL || map->structure_part == NULL ||
        map->structure_quality == NULL || map->structure_hit_points == NULL ||
        map->structure_woods == NULL || map->roof_quality == NULL || map->roof_woods == NULL) {
        map_free(map);
        return false;
    }
    size_t level = (size_t)width * (size_t)height;
    memset(map->terrain, TERRAIN_SOIL, level);
    memset(map->terrain + level, TERRAIN_OPEN, count - level);
    map->structure_counts[STRUCTURE_NONE] = (int)count;
    map->width = width;
    map->height = height;
    map->depth = depth;
    map->top_ground_level = depth - 1;
    return true;
}

void map_free(Map *map)
{
    free(map->terrain);
    free(map->rock);
    free(map->slope);
    free(map->plants);
    free(map->cobbles);
    free(map->flint);
    free(map->branches);
    free(map->twigs);
    free(map->nuts);
    free(map->tree_species);
    free(map->structure);
    free(map->structure_part);
    free(map->structure_quality);
    free(map->structure_hit_points);
    free(map->structure_woods);
    free(map->roof_quality);
    free(map->roof_woods);
    free(map->forbidden);
    free(map->light_stamps);
    free(map->light_solid_stamps);
    free(map->doors);
    memset(map, 0, sizeof(*map));
}

static void log_topology_change(Map *map, int x, int y, int z, bool opened)
{
    map->topology_revision++;
    map->topology_log[map->topology_revision % MAP_TOPOLOGY_LOG_SIZE] =
        (MapTopologyChange){(int16_t)x, (int16_t)y, (int16_t)z, opened, false};
}

static void log_build_change(Map *map, int x, int y, int z)
{
    map->build_revision++;
    map->build_log[map->build_revision % MAP_BUILD_LOG_SIZE] =
        (MapBuildChange){(int16_t)x, (int16_t)y, (int16_t)z};
}

/* Whether a terrain is open space or thin foliage: nobody can stand on it. */
static bool unstandable_air(uint8_t terrain)
{
    return terrain == TERRAIN_OPEN || terrain == TERRAIN_FOLIAGE_MEDIUM ||
           terrain == TERRAIN_FOLIAGE_SPARSE;
}

static int light_block_columns(const Map *map)
{
    return (map->width + MAP_LIGHT_BLOCK - 1) / MAP_LIGHT_BLOCK;
}

/* A change to the light in the column (x, y); `solid`: to what stops it
 * outright, not only to the foliage. */
static void light_changed_at(Map *map, int x, int y, bool solid)
{
    map->light_revision++;
    if (solid) {
        map->light_solid_revision = map->light_revision;
    }
    if (x >= 0 && y >= 0 && x < map->width && y < map->height) {
        size_t block =
            (size_t)((y / MAP_LIGHT_BLOCK) * light_block_columns(map) + x / MAP_LIGHT_BLOCK);
        map->light_stamps[block] = map->light_revision;
        if (solid) {
            map->light_solid_stamps[block] = map->light_revision;
        }
    }
}

void map_light_changed(Map *map, int x, int y)
{
    light_changed_at(map, x, y, true);
}

/* A change to the light of the whole map (`solid`: as light_changed_at()). */
static void light_changed_everywhere(Map *map, bool solid)
{
    map->light_revision++;
    if (solid) {
        map->light_solid_revision = map->light_revision;
    }
    size_t blocks = (size_t)light_block_columns(map) *
                    (size_t)((map->height + MAP_LIGHT_BLOCK - 1) / MAP_LIGHT_BLOCK);
    for (size_t i = 0; i < blocks; i++) {
        map->light_stamps[i] = map->light_revision;
        if (solid) {
            map->light_solid_stamps[i] = map->light_revision;
        }
    }
}

/* The latest of the stamps of the blocks of (x0, y0) .. (x1, y1). */
static uint32_t latest_stamp(const Map *map, const uint32_t *stamps, int x0, int y0, int x1, int y1)
{
    x0 = clampi(x0, 0, map->width - 1) / MAP_LIGHT_BLOCK;
    y0 = clampi(y0, 0, map->height - 1) / MAP_LIGHT_BLOCK;
    x1 = clampi(x1, 0, map->width - 1) / MAP_LIGHT_BLOCK;
    y1 = clampi(y1, 0, map->height - 1) / MAP_LIGHT_BLOCK;
    int columns = light_block_columns(map);
    uint32_t latest = 0;
    for (int by = y0; by <= y1; by++) {
        for (int bx = x0; bx <= x1; bx++) {
            uint32_t stamp = stamps[by * columns + bx];
            latest = stamp > latest ? stamp : latest;
        }
    }
    return latest;
}

uint32_t map_light_solid_stamp(const Map *map, int x0, int y0, int x1, int y1)
{
    return latest_stamp(map, map->light_solid_stamps, x0, y0, x1, y1);
}

uint32_t map_light_stamp(const Map *map, int x0, int y0, int x1, int y1)
{
    return latest_stamp(map, map->light_stamps, x0, y0, x1, y1);
}

bool map_set_door_kept_open(Map *map, int x, int y, int z, bool open)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    if (map->structure[index] != STRUCTURE_DOOR) {
        return false;
    }
    uint8_t before = map->doors[index];
    uint8_t after = (uint8_t)(open ? before | MAP_DOOR_KEPT_OPEN : before & ~MAP_DOOR_KEPT_OPEN);
    if (after != before) {
        map->doors[index] = after;
        map->doors_kept_open += open ? 1 : -1;
        map_light_changed(map, x, y);
        log_build_change(map, x, y, z);
    }
    return true;
}

void map_set_door_passing(Map *map, size_t index, bool passing)
{
    uint8_t before = map->doors[index];
    uint8_t after =
        (uint8_t)(passing ? before | MAP_DOOR_PASSING : before & ~(unsigned)MAP_DOOR_PASSING);
    if (after == before) {
        return;
    }
    map->doors[index] = after; /* Only the sight: the light is not stamped. */
}

void map_set_terrain(Map *map, int x, int y, int z, Terrain terrain)
{
    if (!map_in_bounds(map, x, y, z) || (unsigned)terrain >= TERRAIN_COUNT) {
        return;
    }
    size_t index = map_index(map, x, y, z);
    uint8_t old = map->terrain[index];
    if (old == terrain) {
        return;
    }
    map->terrain[index] = (uint8_t)terrain;
    /* Floors stop the light (a built roof another way than the ground);
     * foliage lets some through; two kinds of ground change nothing. */
    bool floor_before = old != TERRAIN_OPEN && !terrain_is_foliage(old);
    bool floor_after = terrain != TERRAIN_OPEN && !terrain_is_foliage(terrain);
    if (floor_before != floor_after || TERRAIN_DEFS[old].built != TERRAIN_DEFS[terrain].built) {
        light_changed_at(map, x, y, true);
    } else if (!floor_before) {
        light_changed_at(map, x, y, false); /* Foliage grown, thinned or gone. */
    }
    if (old == TERRAIN_SHALLOW_WATER || old == TERRAIN_DEEP_WATER ||
        terrain == TERRAIN_SHALLOW_WATER || terrain == TERRAIN_DEEP_WATER) {
        map->water_revision++;
    }
    /* Trees: open space, thin foliage (nobody stands on it) and dense foliage
     * (a floor) change into each other all the time as crowns spread. */
    bool tree_terrain = (unstandable_air(old) || old == TERRAIN_FOLIAGE) &&
                        (unstandable_air(terrain) || terrain == TERRAIN_FOLIAGE);
    if (tree_terrain && map_climb_cost(map, x, y, z - 1) == 0.0f) {
        /* Only a floor appearing or going matters, and the first is simple to
         * add (no slope or ladder below leads up into the tile: trees are
         * only built where none does). */
        if (old == TERRAIN_FOLIAGE || terrain == TERRAIN_FOLIAGE) {
            log_topology_change(map, x, y, z, terrain == TERRAIN_FOLIAGE);
        }
        return;
    }
    /* May also turn neighbors into (or out of) drinking spots. */
    log_topology_change(map, x, y, z, false);
}

void map_set_rock(Map *map, int x, int y, int z, RockType rock)
{
    if (!map_in_bounds(map, x, y, z) || (unsigned)rock >= ROCK_COUNT) {
        return;
    }
    size_t index = map_index(map, x, y, z);
    bool was_passable = map_move_cost_at(map, index) > 0.0f;
    bool was_rock = map->rock[index] != ROCK_NONE;
    bool was_open = map_is_open_at(map, index);
    RockType old_rock = (RockType)map->rock[index];
    map->rock[index] = (uint8_t)rock;
    if (old_rock != rock) {
        map_light_changed(map, x, y);
    }
    bool is_passable = map_move_cost_at(map, index) > 0.0f;
    if (was_passable != is_passable) {
        log_topology_change(map, x, y, z, is_passable);
    } else if (was_open != map_is_open_at(map, index)) {
        /* Over a slope or a ladder: closes or opens its way up. Elsewhere,
         * nobody stood there and nobody does when it is a trunk rising into
         * thin foliage (or felled): nothing for the connectivity to know. */
        bool trunk = rock == ROCK_WOOD || old_rock == ROCK_WOOD;
        if (!trunk || map_climb_cost(map, x, y, z - 1) != 0.0f) {
            log_topology_change(map, x, y, z, false);
        }
    } else if (was_rock != (rock != ROCK_NONE)) {
        log_build_change(map, x, y, z); /* Rock where a wall stood: roofs, warmth. */
    }
}

void map_set_slope(Map *map, int x, int y, int z, bool slope)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    size_t index = map_index(map, x, y, z);
    if ((map->slope[index] != 0) != slope) {
        map->slope[index] = slope;
        map->slope_count += slope ? 1 : -1;
        log_topology_change(map, x, y, z, false);
    }
}

#define PART_DIRECTION_MASK 0x3u
#define PART_INDEX_SHIFT 2

/* Clears bits of Map.forbidden on a tile (an index in bounds). */
static void clear_forbidden(Map *map, size_t index, unsigned bits)
{
    if (map->forbidden[index] & bits) {
        map->forbidden[index] = (uint8_t)(map->forbidden[index] & ~bits);
        map->forbidden_count -= map->forbidden[index] == 0;
    }
}

/* Puts a structure tile (or none) on a tile: counts, plant, topology. */
static void set_tile_structure(Map *map, int x, int y, int z, StructureType type, uint8_t part,
                               ItemQuality quality, int hit_points, WoodMask woods)
{
    size_t index = map_index(map, x, y, z);
    StructureType old = (StructureType)map->structure[index];
    bool was_passable = map_move_cost_at(map, index) > 0.0f;
    /* A new structure (or none): what was forbidden there went with the old
     * one, and with the plant the builders cleared. */
    clear_forbidden(map, index,
                    MAP_FORBID_STRUCTURE | (type != STRUCTURE_NONE ? MAP_FORBID_PLANT : 0u));
    map->structure_counts[old]--;
    map->structure_counts[type]++;
    map->structure[index] = (uint8_t)type;
    if (structure_stops_light(old) || structure_stops_light(type)) {
        map_light_changed(map, x, y);
    }
    map->doors_kept_open -= (map->doors[index] & MAP_DOOR_KEPT_OPEN) != 0;
    map->doors[index] = 0; /* A new door is closed. */
    map->structure_part[index] = part;
    map->structure_quality[index] = (uint8_t)quality;
    map->structure_hit_points[index] = (uint16_t)hit_points;
    map->structure_woods[index] = woods;
    log_build_change(map, x, y, z);
    if (type != STRUCTURE_NONE) {
        map->plants[index] = (Plant){0}; /* Trampled, cut or dug out by the builders. */
    }
    bool is_passable = map_move_cost_at(map, index) > 0.0f;
    if (was_passable != is_passable) {
        log_topology_change(map, x, y, z, is_passable);
    }
}

bool map_structure_origin(const Map *map, int x, int y, int z, int *origin_x, int *origin_y)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    if (map->structure[index] == STRUCTURE_NONE) {
        return false;
    }
    uint8_t part = map->structure_part[index];
    Direction direction = (Direction)(part & PART_DIRECTION_MASK);
    int offset = part >> PART_INDEX_SHIFT;
    *origin_x = x - offset * direction_dx(direction);
    *origin_y = y - offset * direction_dy(direction);
    return true;
}

bool map_structure_is_origin(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) &&
           map->structure[map_index(map, x, y, z)] != STRUCTURE_NONE &&
           (map->structure_part[map_index(map, x, y, z)] >> PART_INDEX_SHIFT) == 0;
}

Direction map_structure_direction(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z) || map->structure[map_index(map, x, y, z)] == STRUCTURE_NONE) {
        return DIRECTION_SOUTH;
    }
    return (Direction)(map->structure_part[map_index(map, x, y, z)] & PART_DIRECTION_MASK);
}

void map_remove_structure(Map *map, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return;
    }
    StructureType type = map_structure(map, x, y, z);
    Direction direction = map_structure_direction(map, x, y, z);
    for (int part = 0; part < structure_length(type); part++) {
        int px = origin_x + part * direction_dx(direction);
        int py = origin_y + part * direction_dy(direction);
        if (map_structure(map, px, py, z) == type) {
            set_tile_structure(map, px, py, z, STRUCTURE_NONE, 0, ITEM_QUALITY_WORST, 0, 0);
        }
    }
}

bool map_place_structure(Map *map, int x, int y, int z, StructureType type, Direction direction,
                         ItemQuality quality)
{
    return map_place_structure_wood(map, x, y, z, type, direction, quality, 0);
}

bool map_place_structure_wood(Map *map, int x, int y, int z, StructureType type,
                              Direction direction, ItemQuality quality, WoodMask woods)
{
    if (type == STRUCTURE_NONE || (unsigned)type >= STRUCTURE_TYPE_COUNT ||
        structure_is_roof(type) || /* See map_set_roof(). */
        (unsigned)direction >= DIRECTION_COUNT) {
        return false;
    }
    int length = structure_length(type);
    int dx = direction_dx(direction), dy = direction_dy(direction);
    for (int part = 0; part < length; part++) {
        if (!map_in_bounds(map, x + part * dx, y + part * dy, z)) {
            return false;
        }
    }
    quality = (ItemQuality)clampi((int)quality, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST);
    bool same = true; /* The same structure already there: only its condition changes. */
    for (int part = 0; part < length; part++) {
        size_t index = map_index(map, x + part * dx, y + part * dy, z);
        same = same && map->structure[index] == (uint8_t)type &&
               map->structure_part[index] ==
                   (uint8_t)((unsigned)direction | ((unsigned)part << PART_INDEX_SHIFT));
    }
    if (same) {
        size_t head = map_index(map, x, y, z);
        map->structure_quality[head] = (uint8_t)quality;
        map->structure_woods[head] = woods;
        map->structure_hit_points[head] =
            (uint16_t)structure_wood_max_hit_points(type, quality, woods);
        return true;
    }
    for (int part = 0; part < length; part++) {
        map_remove_structure(map, x + part * dx, y + part * dy, z);
    }
    for (int part = 0; part < length; part++) {
        bool head = part == 0;
        set_tile_structure(map, x + part * dx, y + part * dy, z, type,
                           (uint8_t)((unsigned)direction | ((unsigned)part << PART_INDEX_SHIFT)),
                           head ? quality : ITEM_QUALITY_WORST,
                           head ? structure_wood_max_hit_points(type, quality, woods) : 0,
                           head ? woods : 0);
    }
    return true;
}

void map_set_structure(Map *map, int x, int y, int z, StructureType type)
{
    if (type == STRUCTURE_NONE) {
        map_remove_structure(map, x, y, z);
    } else {
        map_place_structure(map, x, y, z, type, DIRECTION_SOUTH, ITEM_QUALITY_STANDARD);
    }
}

ItemQuality map_structure_quality(const Map *map, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return ITEM_QUALITY_WORST;
    }
    return (ItemQuality)map->structure_quality[map_index(map, origin_x, origin_y, z)];
}

int map_structure_hit_points(const Map *map, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return 0;
    }
    return map->structure_hit_points[map_index(map, origin_x, origin_y, z)];
}

WoodMask map_structure_woods(const Map *map, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return 0;
    }
    return map->structure_woods[map_index(map, origin_x, origin_y, z)];
}

int map_structure_max_hit_points(const Map *map, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return 0;
    }
    size_t index = map_index(map, origin_x, origin_y, z);
    return structure_wood_max_hit_points((StructureType)map->structure[index],
                                         (ItemQuality)map->structure_quality[index],
                                         map->structure_woods[index]);
}

void map_set_structure_hit_points(Map *map, int x, int y, int z, int hit_points)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return;
    }
    int max = map_structure_max_hit_points(map, x, y, z);
    map->structure_hit_points[map_index(map, origin_x, origin_y, z)] =
        (uint16_t)clampi(hit_points, 1, max);
}

StructureType map_structure(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) ? (StructureType)map->structure[map_index(map, x, y, z)]
                                       : STRUCTURE_NONE;
}

void map_topology_changed(Map *map)
{
    log_topology_change(map, 0, 0, 0, false);
    map->topology_log[map->topology_revision % MAP_TOPOLOGY_LOG_SIZE].anywhere = true;
    map->water_revision++;
    light_changed_everywhere(map, true);
}

float map_move_cost_at(const Map *map, size_t index)
{
    if (map->rock[index] != ROCK_NONE || map_locked_at(map, index)) {
        return 0.0f;
    }
    float cost = TERRAIN_DEFS[map->terrain[index]].move_cost;
    return cost * PLANT_DEFS[map->plants[index].type].move_cost *
           structure_def((StructureType)map->structure[index])->move_cost;
}

float map_move_cost(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) ? map_move_cost_at(map, map_index(map, x, y, z)) : 0.0f;
}

bool map_is_passable(const Map *map, int x, int y, int z)
{
    return map_move_cost(map, x, y, z) > 0.0f;
}

bool map_has_floor(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && map->terrain[map_index(map, x, y, z)] != TERRAIN_OPEN;
}

bool map_is_open(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && map_is_open_at(map, map_index(map, x, y, z));
}

int map_view_level(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return -1;
    }
    size_t index = map_index(map, x, y, z);
    const size_t layer = (size_t)map->width * (size_t)map->height;
    for (int level = z; level >= 0; level--, index -= layer) {
        if (!map_sees_through_at(map, index)) {
            return level;
        }
    }
    return -1;
}

bool map_is_slope(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && map->slope[map_index(map, x, y, z)] != 0;
}

float map_climb_cost(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) ? map_climb_cost_at(map, map_index(map, x, y, z)) : 0.0f;
}

bool map_beside_trunk(const Map *map, int x, int y, int z)
{
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (map_in_bounds(map, nx, ny, z) && map->rock[map_index(map, nx, ny, z)] == ROCK_WOOD) {
            return true;
        }
    }
    return false;
}

bool map_can_climb_tree(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z + 1) &&
           map->terrain[map_index(map, x, y, z + 1)] == TERRAIN_FOLIAGE &&
           map_is_passable(map, x, y, z + 1) && map_is_passable(map, x, y, z) &&
           map_beside_trunk(map, x, y, z);
}

bool map_can_climb(const Map *map, int x, int y, int z, int nx, int ny)
{
    return abs(nx - x) + abs(ny - y) == 1 && map_climb_cost(map, x, y, z) > 0.0f &&
           map_is_passable(map, x, y, z) && map_is_open(map, x, y, z + 1) &&
           map_is_passable(map, nx, ny, z + 1);
}

/* Whether (x, y, z) is a slope one can walk up from. */
static bool slope_leads_up(const Map *map, int x, int y, int z)
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    if (!map_is_slope(map, x, y, z)) {
        return false;
    }
    for (int d = 0; d < 4; d++) {
        if (map_can_climb(map, x, y, z, x + DX[d], y + DY[d])) {
            return true;
        }
    }
    return false;
}

MapRamp map_ramp_at(const Map *map, int x, int y, int z)
{
    if (slope_leads_up(map, x, y, z)) {
        return MAP_RAMP_UP;
    }
    if (map_is_open(map, x, y, z) && slope_leads_up(map, x, y, z - 1)) {
        return MAP_RAMP_DOWN;
    }
    return MAP_RAMP_NONE;
}

bool map_ladder_fits(const Map *map, int x, int y, int z)
{
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    if (!map_in_bounds(map, x, y, z + 1) || !map_is_open(map, x, y, z + 1)) {
        return false;
    }
    for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (map_has_floor(map, nx, ny, z + 1) && map->rock[map_index(map, nx, ny, z + 1)] == 0 &&
            map_roof(map, nx, ny, z + 1) == STRUCTURE_NONE) { /* Nobody stands on a roof. */
            return true;
        }
    }
    return false;
}

int map_level_links(const Map *map, int x, int y, int z, TilePos out[MAP_MAX_LEVEL_LINKS])
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    int count = 0;
    if (!map_is_passable(map, x, y, z)) {
        return 0;
    }
    for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (map_can_climb(map, x, y, z, nx, ny)) {
            out[count++] = (TilePos){(int16_t)nx, (int16_t)ny, (int16_t)(z + 1)};
        }
        if (map_can_climb(map, nx, ny, z - 1, x, y)) {
            out[count++] = (TilePos){(int16_t)nx, (int16_t)ny, (int16_t)(z - 1)};
        }
    }
    if (map_can_climb_tree(map, x, y, z)) {
        out[count++] = (TilePos){(int16_t)x, (int16_t)y, (int16_t)(z + 1)};
    }
    if (map_can_climb_tree(map, x, y, z - 1)) {
        out[count++] = (TilePos){(int16_t)x, (int16_t)y, (int16_t)(z - 1)};
    }
    return count;
}

bool map_is_buildable(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    return map->rock[index] == ROCK_NONE && map->slope[index] == 0 &&
           TERRAIN_DEFS[map->terrain[index]].buildable;
}

StructureType map_roof(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return STRUCTURE_NONE;
    }
    size_t index = map_index(map, x, y, z);
    return map->rock[index] == ROCK_NONE ? TERRAIN_DEFS[map->terrain[index]].built : STRUCTURE_NONE;
}

/* The terrain a roof is built as (TERRAIN_COUNT for other types). */
static Terrain roof_terrain(StructureType roof)
{
    for (int terrain = 0; roof != STRUCTURE_NONE && terrain < TERRAIN_COUNT; terrain++) {
        if (TERRAIN_DEFS[terrain].built == roof) {
            return (Terrain)terrain;
        }
    }
    return TERRAIN_COUNT;
}

ItemQuality map_roof_quality(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) ? (ItemQuality)map->roof_quality[map_index(map, x, y, z)]
                                       : ITEM_QUALITY_WORST;
}

WoodMask map_roof_woods(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) ? map->roof_woods[map_index(map, x, y, z)] : 0;
}

void map_set_roof(Map *map, int x, int y, int z, StructureType roof, ItemQuality quality)
{
    map_set_roof_wood(map, x, y, z, roof, quality, 0);
}

void map_set_roof_wood(Map *map, int x, int y, int z, StructureType roof, ItemQuality quality,
                       WoodMask woods)
{
    Terrain terrain = roof_terrain(roof);
    if (!map_in_bounds(map, x, y, z) || (roof != STRUCTURE_NONE && terrain == TERRAIN_COUNT)) {
        return;
    }
    size_t index = map_index(map, x, y, z);
    bool had_roof = map_roof(map, x, y, z) != STRUCTURE_NONE;
    if (!had_roof && (roof == STRUCTURE_NONE || !map_is_open_at(map, index))) {
        return; /* Nothing to take down, or no room for a roof. */
    }
    map->roof_count += (roof != STRUCTURE_NONE) - had_roof;
    if (roof == STRUCTURE_NONE) {
        clear_forbidden(map, index, MAP_FORBID_ROOF);
    }
    map->roof_quality[index] =
        roof != STRUCTURE_NONE
            ? (uint8_t)clampi((int)quality, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST)
            : 0;
    map->roof_woods[index] = roof != STRUCTURE_NONE ? woods : 0;
    map_set_terrain(map, x, y, z, roof != STRUCTURE_NONE ? terrain : TERRAIN_OPEN);
    log_build_change(map, x, y, z); /* Roofs, warmth. */
}

/* Rock, or a structure that blocks movement: what a roof rests on. */
static bool holds_up_roof(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    return map->rock[index] != ROCK_NONE || structure_blocks((StructureType)map->structure[index]);
}

/* Whether rock or a wall within ROOF_SUPPORT_RANGE of (x, y) on level z
 * holds up what lies over that level. */
static bool held_up(const Map *map, int x, int y, int z)
{
    for (int dy = -ROOF_SUPPORT_RANGE; dy <= ROOF_SUPPORT_RANGE; dy++) {
        for (int dx = -ROOF_SUPPORT_RANGE; dx <= ROOF_SUPPORT_RANGE; dx++) {
            if (holds_up_roof(map, x + dx, y + dy, z)) {
                return true;
            }
        }
    }
    return false;
}

bool map_roof_supported(const Map *map, int x, int y, int z)
{
    return held_up(map, x, y, z - 1);
}

StructureType map_roof_over(const Map *map, int x, int y, int z)
{
    int cover = map_cover_level(map, x, y, z);
    return cover >= 0 ? map_roof(map, x, y, cover) : STRUCTURE_NONE;
}

bool map_has_ceiling(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z) || !map_in_bounds(map, x, y, z + 1) ||
        map->rock[map_index(map, x, y, z)] != ROCK_NONE) {
        return false;
    }
    size_t over = map_index(map, x, y, z + 1);
    return !map_is_open_at(map, over) &&
           (map->rock[over] != ROCK_NONE || !terrain_is_foliage(map->terrain[over]));
}

bool map_ceiling_supported(const Map *map, int x, int y, int z)
{
    return !map_has_ceiling(map, x, y, z) || held_up(map, x, y, z);
}

/* Whether something other than the tile (ex, ey) holds up (x, y, z). */
static bool supported_without(const Map *map, int x, int y, int z, int ex, int ey)
{
    for (int dy = -ROOF_SUPPORT_RANGE; dy <= ROOF_SUPPORT_RANGE; dy++) {
        for (int dx = -ROOF_SUPPORT_RANGE; dx <= ROOF_SUPPORT_RANGE; dx++) {
            if ((x + dx != ex || y + dy != ey) && holds_up_roof(map, x + dx, y + dy, z)) {
                return true;
            }
        }
    }
    return false;
}

bool map_dig_keeps_ceilings(const Map *map, int x, int y, int z, bool opens_above)
{
    if (!map_in_bounds(map, x, y, z)) {
        return true;
    }
    for (int dy = -ROOF_SUPPORT_RANGE; dy <= ROOF_SUPPORT_RANGE; dy++) {
        for (int dx = -ROOF_SUPPORT_RANGE; dx <= ROOF_SUPPORT_RANGE; dx++) {
            int cx = x + dx, cy = y + dy;
            /* The tile itself has rock on it: what its ceiling would be. */
            size_t over = map_in_bounds(map, x, y, z + 1) ? map_index(map, x, y, z + 1) : 0;
            bool ceiling = dx == 0 && dy == 0 ? !opens_above && map_in_bounds(map, x, y, z + 1) &&
                                                    !map_is_open_at(map, over) &&
                                                    (map->rock[over] != ROCK_NONE ||
                                                     !terrain_is_foliage(map->terrain[over]))
                                              : map_has_ceiling(map, cx, cy, z);
            if (ceiling && !supported_without(map, cx, cy, z, x, y)) {
                return false;
            }
        }
    }
    return true;
}

int map_cover_level(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z) || map->rock[map_index(map, x, y, z)] != ROCK_NONE) {
        return -1;
    }
    for (int above = z + 1; above < map->depth; above++) {
        size_t index = map_index(map, x, y, above);
        if (!map_is_open_at(map, index) &&
            (map->rock[index] != ROCK_NONE || !terrain_is_foliage(map->terrain[index]))) {
            return above;
        }
    }
    return -1;
}

float map_foliage_light(Terrain terrain, float bare)
{
    switch (terrain) {
    case TERRAIN_FOLIAGE: return lerpf(MAP_LIGHT_FOLIAGE_DENSE, MAP_LIGHT_BARE_DENSE, bare);
    case TERRAIN_FOLIAGE_MEDIUM:
        return lerpf(MAP_LIGHT_FOLIAGE_MEDIUM, MAP_LIGHT_BARE_MEDIUM, bare);
    case TERRAIN_FOLIAGE_SPARSE:
        return lerpf(MAP_LIGHT_FOLIAGE_SPARSE, MAP_LIGHT_BARE_SPARSE, bare);
    default: return 1.0f;
    }
}

/* map_light_cover() with the crowns `bare` (0 in leaf .. 1). */
static float light_cover(const Map *map, int x, int y, int z, float bare)
{
    if (!map_in_bounds(map, x, y, z)) {
        return 0.0f;
    }
    float light = 1.0f;
    for (int above = z + 1; above < map->depth; above++) {
        size_t index = map_index(map, x, y, above);
        if (map->rock[index] == ROCK_WOOD) {
            continue; /* A trunk is a tree's own, not a roof. */
        }
        if (map->rock[index] != ROCK_NONE) {
            return 0.0f;
        }
        if (terrain_is_foliage(map->terrain[index])) {
            light *= map_foliage_light((Terrain)map->terrain[index], bare);
            continue;
        }
        if (!map_is_open_at(map, index)) {
            return 0.0f; /* A roof, or the ground of a level above. */
        }
    }
    return light;
}

float map_light_cover(const Map *map, int x, int y, int z)
{
    return light_cover(map, x, y, z, map_bare_fraction(map));
}

float map_light_cover_in_leaf(const Map *map, int x, int y, int z)
{
    return light_cover(map, x, y, z, 0.0f);
}

void map_set_bare(Map *map, int steps)
{
    uint8_t bare = (uint8_t)clampi(steps, 0, MAP_BARE_STEPS);
    if (bare != map->bare) {
        map->bare = bare;
        light_changed_everywhere(map, false);
    }
}

float map_bare_fraction(const Map *map)
{
    return (float)map->bare / (float)MAP_BARE_STEPS;
}

float plant_bare_target(float climate)
{
    return 1.0f - clampf((climate - PLANT_LEAF_BARE_BELOW) /
                             (PLANT_LEAF_FULL_ABOVE - PLANT_LEAF_BARE_BELOW),
                         0.0f, 1.0f);
}

/* Whether a terrain is ground plants grow on (not water, stone, open space,
 * roofs nor foliage). */
static bool rooting_ground(Terrain terrain)
{
    switch (terrain) {
    case TERRAIN_SAND:
    case TERRAIN_SOIL:
    case TERRAIN_RICH_SOIL:
    case TERRAIN_GRAVEL: return true;
    default: return false;
    }
}

/* Whether a tile is water, or next to it (8 neighbors). */
static bool water_near(const Map *map, int x, int y, int z)
{
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (map_is_water(map, x + dx, y + dy, z)) {
                return true;
            }
        }
    }
    return false;
}

/* Whether a tile of shallow water is at the shore: next to the land (4 neighbors). */
static bool at_shore(const Map *map, int x, int y, int z)
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
        if (map_in_bounds(map, x + DX[d], y + DY[d], z) &&
            !map_is_water(map, x + DX[d], y + DY[d], z)) {
            return true;
        }
    }
    return false;
}

/* Whether a tree or a trunk stands within `range` tiles of (x, y, z). */
static bool tree_or_trunk_near(const Map *map, int x, int y, int z, int range)
{
    for (int ty = y - range; ty <= y + range; ty++) {
        for (int tx = x - range; tx <= x + range; tx++) {
            if (map_in_bounds(map, tx, ty, z)) {
                size_t index = map_index(map, tx, ty, z);
                if (plant_is_tree(map->plants[index].type) || map->rock[index] == ROCK_WOOD) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool plant_light_suits(const Map *map, PlantType type, int x, int y, int z, float light)
{
    if ((unsigned)type >= PLANT_COUNT || !map_in_bounds(map, x, y, z)) {
        return false;
    }
    const PlantDef *def = &PLANT_DEFS[type];
    if (def->dark_days <= 0.0f) {
        return true;
    }
    float now = map_light_cover(map, x, y, z);
    float in_leaf = def->rests_in_heat ? now : map_light_cover_in_leaf(map, x, y, z);
    if (light < 0.0f) {
        light = in_leaf;
    } else if (now > 0.0f) {
        light *= in_leaf / now; /* The crowns over it in leaf. */
    }
    return light * PLANT_SPREAD_SUN >= def->light_min;
}

float plant_dark_share_borne(PlantType type)
{
    const PlantDef *def = plant_def(type);
    if (def == NULL || type == PLANT_NONE) {
        return 0.0f;
    }
    if (def->dark_days <= 0.0f) {
        return 1.0f;
    }
    /* share * harm = (1 - share) * mend, in an hour. */
    float harm = 1.0f / (def->dark_days * (float)HOURS_PER_DAY);
    return PLANT_RECOVERY / (PLANT_RECOVERY + harm);
}

bool plant_can_take_root(const Map *map, PlantType type, int x, int y, int z, float light)
{
    if ((unsigned)type >= PLANT_COUNT || type == PLANT_NONE || !map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    if (map->plants[index].type != PLANT_NONE || map->rock[index] != ROCK_NONE ||
        map->structure[index] != STRUCTURE_NONE) {
        return false;
    }
    const PlantDef *def = &PLANT_DEFS[type];
    Terrain terrain = (Terrain)map->terrain[index];
    if (def->wetland) {
        bool shallows = terrain == TERRAIN_SHALLOW_WATER;
        if (shallows ? !at_shore(map, x, y, z)
                     : !rooting_ground(terrain) || !water_near(map, x, y, z)) {
            return false;
        }
    } else if (!rooting_ground(terrain) || TERRAIN_DEFS[terrain].fertility < def->min_fertility) {
        return false;
    }
    if (!plant_light_suits(map, type, x, y, z, light)) {
        return false;
    }
    return !plant_is_tree(type) || !tree_or_trunk_near(map, x, y, z, TREE_SPREAD_SPACING);
}

bool plant_spread_target(const Map *map, int x, int y, int z, float roll, int *out_x, int *out_y)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    uint8_t type = map->plants[map_index(map, x, y, z)].type;
    int range = (unsigned)type < PLANT_COUNT ? PLANT_DEFS[type].spread_range : 0;
    if (range <= 0) {
        return false;
    }
    int side = 2 * range + 1;
    int others = side * side - 1;
    int pick = clampi((int)(roll * (float)others), 0, others - 1);
    if (pick >= others / 2) {
        pick++; /* Skips the plant's own tile, in the middle. */
    }
    int tx = x + pick % side - range, ty = y + pick / side - range;
    if (!map_in_bounds(map, tx, ty, z)) {
        return false;
    }
    *out_x = tx;
    *out_y = ty;
    return true;
}

Plant plant_offspring(const Plant *parent)
{
    return (Plant){.type = parent->type,
                   .growth = PLANT_SPREAD_GROWTH,
                   .warmth = parent->warmth,
                   .climate = parent->climate,
                   .hardening = parent->hardening,
                   .state = (uint8_t)(PLANT_STATE_AWAKE | (parent->state & PLANT_CLIMATE_KNOWN))};
}

bool map_is_roofed(const Map *map, int x, int y, int z)
{
    return map_cover_level(map, x, y, z) >= 0;
}

bool map_natural_roof(const Map *map, int x, int y, int z)
{
    int cover = map_cover_level(map, x, y, z);
    return cover >= 0 && map_roof(map, x, y, cover) == STRUCTURE_NONE;
}

void map_copy_tile(Map *dst, const Map *src, size_t index)
{
    dst->slope_count += (src->slope[index] != 0) - (dst->slope[index] != 0);
    dst->structure_counts[dst->structure[index]]--;
    dst->structure_counts[src->structure[index]]++;
    bool had_roof = terrain_def((Terrain)dst->terrain[index])->built != STRUCTURE_NONE;
    bool has_roof = terrain_def((Terrain)src->terrain[index])->built != STRUCTURE_NONE;
    dst->roof_count += has_roof - had_roof;
    dst->forbidden_count += (src->forbidden[index] != 0) - (dst->forbidden[index] != 0);
    dst->doors_kept_open += ((src->doors[index] & MAP_DOOR_KEPT_OPEN) != 0) -
                            ((dst->doors[index] & MAP_DOOR_KEPT_OPEN) != 0);
    if (dst->terrain[index] != src->terrain[index] || dst->rock[index] != src->rock[index] ||
        structure_stops_light(dst->structure[index]) !=
            structure_stops_light(src->structure[index]) ||
        ((dst->doors[index] ^ src->doors[index]) & MAP_DOOR_KEPT_OPEN) != 0) {
        size_t column = index % map_level_tile_count(dst);
        map_light_changed(dst, (int)(column % (size_t)dst->width),
                          (int)(column / (size_t)dst->width));
    }
    dst->terrain[index] = src->terrain[index];
    dst->rock[index] = src->rock[index];
    dst->slope[index] = src->slope[index];
    dst->plants[index] = src->plants[index];
    dst->cobbles[index] = src->cobbles[index];
    dst->flint[index] = src->flint[index];
    dst->branches[index] = src->branches[index];
    dst->twigs[index] = src->twigs[index];
    dst->nuts[index] = src->nuts[index];
    dst->tree_species[index] = src->tree_species[index];
    dst->structure[index] = src->structure[index];
    dst->structure_part[index] = src->structure_part[index];
    dst->structure_quality[index] = src->structure_quality[index];
    dst->structure_hit_points[index] = src->structure_hit_points[index];
    dst->structure_woods[index] = src->structure_woods[index];
    dst->roof_quality[index] = src->roof_quality[index];
    dst->roof_woods[index] = src->roof_woods[index];
    dst->forbidden[index] = src->forbidden[index];
    dst->doors[index] = src->doors[index];
}

uint64_t map_hash(const Map *map)
{
    uint64_t hash = HASH_FNV1A64_INIT;
    int32_t size[2] = {map->width, map->height};
    hash = hash_fnv1a64(hash, size, sizeof(size));
    if (map->depth != 1) { /* Hashes of one-level maps stay as they were. */
        hash = hash_fnv1a64(hash, &map->depth, sizeof(map->depth));
    }
    if (map->top_ground_level != map->depth - 1) {
        hash = hash_fnv1a64(hash, &map->top_ground_level, sizeof(map->top_ground_level));
    }
    if (map->surface_level != 0) {
        hash = hash_fnv1a64(hash, &map->surface_level, sizeof(map->surface_level));
    }

    size_t count = map_tile_count(map);
    if (map->bare != 0) { /* Hashes of maps in full leaf stay as they were. */
        hash = hash_fnv1a64(hash, &map->bare, sizeof(map->bare));
    }
    hash = hash_fnv1a64(hash, map->terrain, count);
    hash = hash_fnv1a64(hash, map->rock, count);
    if (map->slope_count > 0) { /* Hashes of maps without slopes stay as they were. */
        hash = hash_fnv1a64(hash, map->slope, count);
    }
    hash = hash_fnv1a64(hash, map->cobbles, count);
    hash = hash_fnv1a64(hash, map->flint, count);
    hash = hash_fnv1a64(hash, map->branches, count);
    hash = hash_fnv1a64(hash, map->twigs, count);
    hash = hash_fnv1a64(hash, map->nuts, count);
    hash = hash_fnv1a64(hash, map->structure, count);
    hash = hash_fnv1a64(hash, map->structure_part, count);
    hash = hash_fnv1a64(hash, map->structure_quality, count);
    hash =
        hash_fnv1a64(hash, map->structure_hit_points, count * sizeof(*map->structure_hit_points));
    if (map->roof_count > 0) { /* Hashes of maps without roofs stay as they were. */
        hash = hash_fnv1a64(hash, map->roof_quality, count);
    }
    /* The species of structures and roofs: those of no known species leave
     * the hash as it was. */
    for (size_t i = 0; i < count; i++) {
        if (map->structure_woods[i] != 0 || map->roof_woods[i] != 0) {
            uint64_t at[2] = {i, (uint64_t)map->structure_woods[i] << 16 | map->roof_woods[i]};
            hash = hash_fnv1a64(hash, at, sizeof(at));
        }
    }
    if (map->forbidden_count > 0) { /* Likewise without anything forbidden. */
        hash = hash_fnv1a64(hash, map->forbidden, count);
    }
    if (map->doors_kept_open > 0) { /* Likewise without doors kept open. */
        for (size_t i = 0; i < count; i++) {
            if ((map->doors[i] & MAP_DOOR_KEPT_OPEN) != 0) {
                uint64_t at = i;
                hash = hash_fnv1a64(hash, &at, sizeof(at));
            }
        }
    }
    /* Hash plant fields one by one: struct padding bytes are unspecified. */
    for (size_t i = 0; i < count; i++) {
        const Plant *plant = &map->plants[i];
        hash = hash_fnv1a64(hash, &plant->type, sizeof(plant->type));
        if (plant->size != 0) { /* Rare: big trees. */
            hash = hash_fnv1a64(hash, &plant->size, sizeof(plant->size));
        }
        hash = hash_fnv1a64(hash, plant->stock, sizeof(plant->stock));
        hash = hash_fnv1a64(hash, &plant->growth, sizeof(plant->growth));
    }
    return hash;
}

/* The items a type of litter lying on the ground counts (twigs, dry or wet:
 * twigs left in the rain get wet like any). */
static ItemTypeMask litter_mask(ItemType type)
{
    return type == ITEM_TWIGS ? ITEM_TYPE_BIT(ITEM_TWIGS) | ITEM_TYPE_BIT(ITEM_WET_TWIGS)
                              : ITEM_TYPE_BIT(type);
}

/* Items of a type of litter on a tile of Map.litter. */
static int litter_items(const Map *map, int x, int y, int z, ItemType type)
{
    int count = items_count_type(map->litter, x, y, z, type);
    return type == ITEM_TWIGS ? count + items_count_type(map->litter, x, y, z, ITEM_WET_TWIGS)
                              : count;
}

/* Takes up to `count` items of a type of litter off a tile of Map.litter:
 * gone. Returns how many. */
static int remove_litter_items(Map *map, int x, int y, int z, ItemType type, int count)
{
    int removed = 0;
    while (removed < count) {
        int now = items_remove_of(map->litter, x, y, z, litter_mask(type), count - removed);
        if (now <= 0) {
            break;
        }
        removed += now;
    }
    return removed;
}

/* The layer holding stones of a type, or NULL. */
static uint8_t *stone_layer(const Map *map, ItemType type)
{
    switch (type) {
    case ITEM_COBBLE: return map->cobbles;
    case ITEM_FLINT: return map->flint;
    default: return NULL;
    }
}

int map_stones(const Map *map, int x, int y, int z, ItemType type)
{
    if (map->litter != NULL) {
        return stone_layer(map, type) != NULL ? litter_items(map, x, y, z, type) : 0;
    }
    const uint8_t *layer = stone_layer(map, type);
    return layer != NULL && map_in_bounds(map, x, y, z) ? layer[map_index(map, x, y, z)] : 0;
}

void map_set_stones(Map *map, int x, int y, int z, int cobbles, int flint)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    flint = clampi(flint, 0, MAP_STONES_MAX);
    cobbles = clampi(cobbles, 0, MAP_STONES_MAX - flint);
    if (map->litter != NULL) {
        static const ItemType TYPES[2] = {ITEM_COBBLE, ITEM_FLINT};
        int wanted[2] = {cobbles, flint};
        for (int i = 0; i < 2; i++) {
            int now = litter_items(map, x, y, z, TYPES[i]);
            if (now > wanted[i]) {
                remove_litter_items(map, x, y, z, TYPES[i], now - wanted[i]);
            } else if (now < wanted[i]) {
                items_add_natural(map->litter, x, y, z, TYPES[i], wanted[i] - now);
            }
        }
        return;
    }
    size_t index = map_index(map, x, y, z);
    map->flint[index] = (uint8_t)flint;
    map->cobbles[index] = (uint8_t)cobbles;
}

bool map_take_stone(Map *map, int x, int y, int z, ItemType type)
{
    uint8_t *layer = stone_layer(map, type);
    if (layer != NULL && map->litter != NULL) {
        return remove_litter_items(map, x, y, z, type, 1) == 1;
    }
    if (layer == NULL || !map_in_bounds(map, x, y, z) || layer[map_index(map, x, y, z)] == 0) {
        return false;
    }
    layer[map_index(map, x, y, z)]--;
    return true;
}

/* The most of a type of litter (what trees let fall) a tile holds; 0 for
 * other types. */
static int litter_max(ItemType type)
{
    switch (type) {
    case ITEM_BRANCHES: return MAP_BRANCHES_MAX;
    case ITEM_TWIGS: return MAP_TWIGS_MAX;
    case ITEM_HAZELNUTS:
    case ITEM_CHESTNUTS:
    case ITEM_APPLES:
    case ITEM_PEARS: return MAP_NUTS_MAX;
    default: return 0;
    }
}

/* The layer holding branches, twigs or nuts in a new world, and the most a
 * tile holds; NULL for other types (the fruit of the other trees only falls
 * in a running game, as items). */
static uint8_t *litter_layer(const Map *map, ItemType type, int *max)
{
    *max = litter_max(type);
    switch (type) {
    case ITEM_BRANCHES: return map->branches;
    case ITEM_TWIGS: return map->twigs;
    case ITEM_HAZELNUTS: return map->nuts;
    default: return NULL;
    }
}

/* Whether the type lies as items (Map.litter) rather than in a layer. */
static bool litter_is_items(const Map *map, ItemType type)
{
    return map->litter != NULL && litter_max(type) > 0;
}

int map_litter(const Map *map, int x, int y, int z, ItemType type)
{
    if (litter_is_items(map, type)) {
        return litter_items(map, x, y, z, type);
    }
    int max;
    const uint8_t *layer = litter_layer(map, type, &max);
    return layer != NULL && map_in_bounds(map, x, y, z) ? layer[map_index(map, x, y, z)] : 0;
}

bool map_add_litter(Map *map, int x, int y, int z, ItemType type)
{
    return map_add_litter_wood(map, x, y, z, type, 0);
}

bool map_add_litter_wood(Map *map, int x, int y, int z, ItemType type, WoodMask woods)
{
    int max;
    uint8_t *layer = litter_layer(map, type, &max);
    if (litter_is_items(map, type)) {
        return map_in_bounds(map, x, y, z) && litter_items(map, x, y, z, type) < max &&
               items_add_natural_wood(map->litter, x, y, z, type, 1, woods) == 1;
    }
    if (layer == NULL || !map_in_bounds(map, x, y, z) || layer[map_index(map, x, y, z)] >= max) {
        return false;
    }
    layer[map_index(map, x, y, z)]++;
    return true;
}

int map_take_litter(Map *map, int x, int y, int z, ItemType type)
{
    if (litter_is_items(map, type)) {
        return remove_litter_items(map, x, y, z, type, INT_MAX);
    }
    int max;
    uint8_t *layer = litter_layer(map, type, &max);
    if (layer == NULL || !map_in_bounds(map, x, y, z)) {
        return 0;
    }
    int count = layer[map_index(map, x, y, z)];
    layer[map_index(map, x, y, z)] = 0;
    return count;
}

size_t map_count_plants(const Map *map, PlantType type)
{
    size_t total = 0;
    size_t count = map_tile_count(map);
    for (size_t i = 0; i < count; i++) {
        if (map->plants[i].type == type) {
            total++;
        }
    }
    return total;
}

size_t map_count_trees(const Map *map)
{
    size_t total = 0;
    size_t count = map_tile_count(map);
    for (size_t i = 0; i < count; i++) {
        total += plant_is_tree(map->plants[i].type);
    }
    return total;
}

int map_take_litter_count(Map *map, int x, int y, int z, ItemType type, int count)
{
    if (litter_is_items(map, type)) {
        return remove_litter_items(map, x, y, z, type, count);
    }
    int max;
    uint8_t *layer = litter_layer(map, type, &max);
    if (layer == NULL || !map_in_bounds(map, x, y, z) || count <= 0) {
        return 0;
    }
    uint8_t *tile = &layer[map_index(map, x, y, z)];
    int taken = count < *tile ? count : *tile;
    *tile = (uint8_t)(*tile - taken);
    return taken;
}

bool map_plant_blocks_building(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && plant_is_tree(map->plants[map_index(map, x, y, z)].type);
}

bool map_has_edible_plant(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    const PlantProduct *food =
        plant_product((PlantType)plant->type, plant_food_slot((PlantType)plant->type));
    return food != NULL && !food->dug && !food->confusable && plant_has_food(plant) &&
           !map_forbidden(map, x, y, z, MAP_FORBID_PLANT);
}

/* The tile whose Map.forbidden holds `what` for (x, y, z), in bounds: the
 * structure's head for MAP_FORBID_STRUCTURE. False when there is no such
 * thing there. */
static bool forbid_tile(const Map *map, int x, int y, int z, MapForbid what, size_t *index)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    switch (what) {
    case MAP_FORBID_PLANT:
        *index = map_index(map, x, y, z);
        return map->plants[*index].type != PLANT_NONE;
    case MAP_FORBID_STRUCTURE: {
        int head_x, head_y;
        if (!map_structure_origin(map, x, y, z, &head_x, &head_y)) {
            return false;
        }
        *index = map_index(map, head_x, head_y, z);
        return true;
    }
    case MAP_FORBID_ROOF:
        *index = map_index(map, x, y, z);
        return map_roof(map, x, y, z) != STRUCTURE_NONE;
    }
    return false;
}

bool map_forbidden(const Map *map, int x, int y, int z, MapForbid what)
{
    size_t index;
    return map->forbidden_count > 0 && forbid_tile(map, x, y, z, what, &index) &&
           (map->forbidden[index] & what) != 0;
}

void map_remove_plant(Map *map, int x, int y, int z)
{
    if (map_in_bounds(map, x, y, z)) {
        size_t index = map_index(map, x, y, z);
        memset(&map->plants[index], 0, sizeof(Plant));
        clear_forbidden(map, index, MAP_FORBID_PLANT);
    }
}

bool map_can_forbid(const Map *map, int x, int y, int z, MapForbid what)
{
    size_t index;
    return forbid_tile(map, x, y, z, what, &index);
}

bool map_set_forbidden(Map *map, int x, int y, int z, MapForbid what, bool forbidden)
{
    size_t index;
    if (!forbid_tile(map, x, y, z, what, &index) ||
        ((map->forbidden[index] & what) != 0) == forbidden) {
        return false;
    }
    bool was_passable = map_move_cost_at(map, index) > 0.0f;
    float was_climb = map_climb_cost_at(map, index);
    if (forbidden) {
        map->forbidden_count += map->forbidden[index] == 0;
        map->forbidden[index] = (uint8_t)(map->forbidden[index] | what);
    } else {
        clear_forbidden(map, index, what);
    }
    bool is_passable = map_move_cost_at(map, index) > 0.0f;
    if (was_passable != is_passable || was_climb != map_climb_cost_at(map, index)) {
        int tx, ty, tz;
        map_tile_of(map, index, &tx, &ty, &tz);
        /* A door or a ladder (un)locked; a ladder's way up is no simple opening. */
        log_topology_change(map, tx, ty, tz, was_passable != is_passable && is_passable);
    }
    return true;
}

bool map_tree_near(const Map *map, int x, int y, int z, int range)
{
    for (int ty = y - range; ty <= y + range; ty++) {
        for (int tx = x - range; tx <= x + range; tx++) {
            if (map_in_bounds(map, tx, ty, z)) {
                const Plant *plant = &map->plants[map_index(map, tx, ty, z)];
                if (plant_is_tree(plant->type) && plant->growth >= 1.0f) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool map_is_water(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    uint8_t terrain = map->terrain[map_index(map, x, y, z)];
    return terrain == TERRAIN_SHALLOW_WATER || terrain == TERRAIN_DEEP_WATER;
}

bool map_is_drinking_spot(const Map *map, int x, int y, int z)
{
    if (!map_is_passable(map, x, y, z)) {
        return false;
    }
    return map_is_water(map, x, y, z) || map_is_water(map, x - 1, y, z) ||
           map_is_water(map, x + 1, y, z) || map_is_water(map, x, y - 1, z) ||
           map_is_water(map, x, y + 1, z);
}

int map_ground_level(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y, 0)) {
        return 0;
    }
    for (int z = map->depth - 1; z >= 0; z--) {
        size_t index = map_index(map, x, y, z);
        Terrain terrain = (Terrain)map->terrain[index];
        if (terrain_is_foliage(terrain)) {
            continue; /* Trees are left out. */
        }
        if (map->rock[index] == ROCK_WOOD) {
            if (terrain != TERRAIN_OPEN) {
                return z; /* The ground under the foot of a trunk. */
            }
            continue;
        }
        if (map->rock[index] != ROCK_NONE) {
            return z + 1 < map->depth ? z + 1 : z;
        }
        if (terrain != TERRAIN_OPEN && TERRAIN_DEFS[terrain].built == STRUCTURE_NONE) {
            return z; /* Not a roof. */
        }
    }
    return 0;
}

/* The species of the tree over a tile, from the crowns (Map.tree_species)
 * nearest above it: what the branches lying there fell from (PLANT_NONE when
 * no crown is near). */
static PlantType tree_over(const Map *map, int x, int y, int z)
{
    for (int ring = 0; ring <= TREE_MAX_REACH; ring++) {
        for (int dz = 1; dz <= TREE_MAX_LEVELS && z + dz < map->depth; dz++) {
            for (int dy = -ring; dy <= ring; dy++) {
                for (int dx = -ring; dx <= ring; dx++) {
                    if ((abs(dx) != ring && abs(dy) != ring) ||
                        !map_in_bounds(map, x + dx, y + dy, z + dz)) {
                        continue;
                    }
                    size_t index = map_index(map, x + dx, y + dy, z + dz);
                    bool tree =
                        map->rock[index] == ROCK_WOOD ||
                        (map->rock[index] == ROCK_NONE && terrain_is_foliage(map->terrain[index]));
                    if (tree && plant_is_tree(map->tree_species[index])) {
                        return (PlantType)map->tree_species[index];
                    }
                }
            }
        }
    }
    return PLANT_NONE;
}

void map_litter_to_items(Map *map, Items *items)
{
    struct {
        uint8_t *layer;
        ItemType type;
    } kinds[] = {{map->cobbles, ITEM_COBBLE},
                 {map->flint, ITEM_FLINT},
                 {map->branches, ITEM_BRANCHES},
                 {map->twigs, ITEM_TWIGS},
                 {map->nuts, ITEM_HAZELNUTS}};
    for (int z = 0; z < map->depth; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                size_t index = map_index(map, x, y, z);
                for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
                    if (kinds[k].layer[index] != 0) {
                        /* Branches of the species of the crown they fell from. */
                        WoodMask woods =
                            kinds[k].type == ITEM_BRANCHES ? wood_bit(tree_over(map, x, y, z)) : 0;
                        items_add_natural_wood(items, x, y, z, kinds[k].type, kinds[k].layer[index],
                                               woods);
                        kinds[k].layer[index] = 0;
                    }
                }
            }
        }
    }
    map->litter = items;
}

/* The coordinates of a tile index. */
static void index_coords(const Map *map, size_t index, int *x, int *y, int *z)
{
    size_t level = map_level_tile_count(map);
    *z = (int)(index / level);
    *y = (int)(index % level / (size_t)map->width);
    *x = (int)(index % (size_t)map->width);
}

void map_clear_litter_at(Map *map, size_t index, ItemTypeMask types)
{
    static const ItemType TYPES[] = {ITEM_COBBLE,    ITEM_FLINT,     ITEM_BRANCHES, ITEM_TWIGS,
                                     ITEM_HAZELNUTS, ITEM_CHESTNUTS, ITEM_APPLES,   ITEM_PEARS};
    int x, y, z;
    index_coords(map, index, &x, &y, &z);
    for (size_t t = 0; t < sizeof(TYPES) / sizeof(TYPES[0]); t++) {
        if ((types & ITEM_TYPE_BIT(TYPES[t])) == 0) {
            continue;
        }
        if (TYPES[t] == ITEM_COBBLE || TYPES[t] == ITEM_FLINT) {
            while (map_take_stone(map, x, y, z, TYPES[t])) {
            }
        } else {
            map_take_litter(map, x, y, z, TYPES[t]);
        }
    }
}

void map_set_stones_at(Map *map, size_t index, int cobbles, int flint)
{
    int x, y, z;
    index_coords(map, index, &x, &y, &z);
    map_set_stones(map, x, y, z, cobbles, flint);
}

void map_clear_litter(Map *map)
{
    ItemTypeMask types = ITEM_TYPE_BIT(ITEM_COBBLE) | ITEM_TYPE_BIT(ITEM_FLINT) |
                         ITEM_TYPE_BIT(ITEM_BRANCHES) | ITEM_TYPE_BIT(ITEM_TWIGS);
    size_t count = map_tile_count(map);
    for (size_t i = 0; i < count; i++) {
        map_clear_litter_at(map, i, types);
    }
}
