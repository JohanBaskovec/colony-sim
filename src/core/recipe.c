#include "core/recipe.h"

#include "core/i18n.h"
#include "core/util.h"
#include "core/wood.h"
#include "core/workstation.h"

#include <math.h>

/*
 * Flakes struck from one stone: FLAKES_BASE plus one per FLAKES_LEVEL_STEP
 * points above mastery (3 at 50, 8 at 100). Shaping a hand axe gives
 * HAND_AXE_FLAKES flakes besides the axe; it fails with chance
 * HAND_AXE_FAILURE_AT_MIN at its minimum level, HAND_AXE_FAILURE_PER_LEVEL
 * less per point above (none from 90), and a failure leaves
 * FAILED_FLAKES_BASE flakes plus one per FLAKES_LEVEL_STEP points above
 * mastery. Values from docs/KNOWLEDGE.md §6.2 (tuning).
 */
#define FLAKES_BASE 3
#define FLAKES_LEVEL_STEP 10.0
#define HAND_AXE_FLAKES 2
#define HAND_AXE_FAILURE_AT_MIN 0.40f
#define HAND_AXE_FAILURE_PER_LEVEL 0.02f
#define FAILED_FLAKES_BASE 1

#define CRAFTING_SPOT WORKSTATION_BIT(WORKSTATION_CRAFTING_SPOT)
#define OPEN_HEARTH WORKSTATION_BIT(WORKSTATION_OPEN_HEARTH)
#define BUTCHERING_SPOT WORKSTATION_BIT(WORKSTATION_BUTCHERING_SPOT)
/* Knapping is done with a plain cobble as a hammer. */
#define HAMMER                                                                                     \
    {                                                                                              \
        ITEM_COBBLE                                                                                \
    }
#define CUTTING_TOOL                                                                               \
    {                                                                                              \
        ITEM_FLAKE, ITEM_HAND_AXE                                                                  \
    }

/* A recipe mending a worn `container` with `count` of a material (`m0`, or `m1`) in the
 * state `want`, with a tool `t0` or `t1` (RecipeDef.mends); `key` its name. */
#define MEND(id_, key, container, m0, m1, count, want, t0, t1, work_)                              \
    {                                                                                              \
        .id = id_, .name_key = key, .stations = CRAFTING_SPOT,                                     \
        .ingredients = {{{container, ITEM_NONE}, 1, ITEM_WANT_WORN}, {{m0, m1}, count, want}},     \
        .input_min_quality = ITEM_QUALITY_WORST, .tools = {{t0, t1}}, .work = work_,               \
        .knowledge = KNOWLEDGE_BASKETRY, .min_level = 50.0f, .product = container, .dirt = 1,      \
        .tool_use = TOOL_USE_NONE, .mends = true                                                   \
    }

static const RecipeDef RECIPE_DEFS[RECIPE_COUNT] = {
    [RECIPE_CHOPPER] = {.id = "chopper",
                        .name_key = N_("recipe.chopper"),
                        .stations = CRAFTING_SPOT,
                        .ingredients = {{{ITEM_COBBLE, ITEM_FLINT}, 1, ITEM_WANT_ANY}},
                        .input_min_quality = ITEM_QUALITY_WORST,
                        .tools = {HAMMER},
                        .work = 250.0f,
                        .knowledge = KNOWLEDGE_STONE_KNAPPING,
                        .min_level = 50.0f,
                        .product = ITEM_CHOPPER,
                        .dirt = 6,
                        .tool_use = TOOL_USE_KNAPPING},
    [RECIPE_FLAKES] = {.id = "flakes",
                       .name_key = N_("recipe.flakes"),
                       .stations = CRAFTING_SPOT,
                       .ingredients = {{{ITEM_FLINT, ITEM_COBBLE}, 1, ITEM_WANT_ANY}},
                       .input_min_quality = ITEM_QUALITY_WORST,
                       .tools = {HAMMER},
                       .work = 375.0f,
                       .knowledge = KNOWLEDGE_STONE_KNAPPING,
                       .min_level = 55.0f,
                       .product = ITEM_FLAKE,
                       .dirt = 8,
                       .tool_use = TOOL_USE_KNAPPING},
    [RECIPE_HAND_AXE] = {.id = "hand_axe",
                         .name_key = N_("recipe.hand_axe"),
                         .stations = CRAFTING_SPOT,
                         .ingredients = {{{ITEM_FLINT, ITEM_COBBLE}, 1, ITEM_WANT_ANY}},
                         .input_min_quality = ITEM_QUALITY_WORST,
                         .tools = {HAMMER},
                         .work = 2500.0f,
                         .knowledge = KNOWLEDGE_STONE_KNAPPING,
                         .min_level = 70.0f,
                         .product = ITEM_HAND_AXE,
                         .dirt = 12,
                         .tool_use = TOOL_USE_KNAPPING},
    /* Spears want straight branches: standard quality or better. */
    [RECIPE_THROWING_SPEAR] = {.id = "throwing_spear",
                               .name_key = N_("recipe.throwing_spear"),
                               .stations = CRAFTING_SPOT,
                               .ingredients = {{{ITEM_BRANCHES, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                               .input_min_quality = ITEM_QUALITY_STANDARD,
                               .tools = {CUTTING_TOOL},
                               .work = 3750.0f,
                               .knowledge = KNOWLEDGE_SPEAR_MAKING,
                               .min_level = 50.0f,
                               .product = ITEM_THROWING_SPEAR,
                               .dirt = 4,
                               .tool_use = TOOL_USE_WOODWORKING},
    [RECIPE_THRUSTING_SPEAR] = {.id = "thrusting_spear",
                                .name_key = N_("recipe.thrusting_spear"),
                                .stations = CRAFTING_SPOT,
                                .ingredients = {{{ITEM_BRANCHES, ITEM_NONE}, 2, ITEM_WANT_ANY}},
                                .input_min_quality = ITEM_QUALITY_STANDARD,
                                .tools = {{ITEM_HAND_AXE}, {ITEM_FLAKE}},
                                .work = 6250.0f,
                                .knowledge = KNOWLEDGE_SPEAR_MAKING,
                                .min_level = 60.0f,
                                .product = ITEM_THRUSTING_SPEAR,
                                .dirt = 6,
                                .tool_use = TOOL_USE_WOODWORKING},
    /* Drying and charring a wooden point over the fire. */
    [RECIPE_HARDEN_THROWING_SPEAR] =
        {.id = "harden_throwing_spear",
         .name_key = N_("recipe.harden_throwing_spear"),
         .stations = OPEN_HEARTH,
         .ingredients = {{{ITEM_THROWING_SPEAR, ITEM_NONE}, 1, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_NONE}},
         .work = 750.0f,
         .knowledge = KNOWLEDGE_FIRE,
         .min_level = 55.0f,
         .product = ITEM_THROWING_SPEAR_HARDENED,
         .dirt = 0,
         .tool_use = TOOL_USE_NONE},
    [RECIPE_HARDEN_THRUSTING_SPEAR] =
        {.id = "harden_thrusting_spear",
         .name_key = N_("recipe.harden_thrusting_spear"),
         .stations = OPEN_HEARTH,
         .ingredients = {{{ITEM_THRUSTING_SPEAR, ITEM_NONE}, 1, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_NONE}},
         .work = 750.0f,
         .knowledge = KNOWLEDGE_FIRE,
         .min_level = 55.0f,
         .product = ITEM_THRUSTING_SPEAR_HARDENED,
         .dirt = 0,
         .tool_use = TOOL_USE_NONE},
    /* A stout branch, its end cut to a point: part of gathering. */
    [RECIPE_DIGGING_STICK] = {.id = "digging_stick",
                              .name_key = N_("recipe.digging_stick"),
                              .stations = CRAFTING_SPOT,
                              .ingredients = {{{ITEM_BRANCHES, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                              .input_min_quality = ITEM_QUALITY_WORST,
                              .tools = {CUTTING_TOOL},
                              .work = 375.0f,
                              .knowledge = KNOWLEDGE_GATHERING,
                              .min_level = 50.0f,
                              .product = ITEM_DIGGING_STICK,
                              .dirt = 2,
                              .tool_use = TOOL_USE_WOODWORKING},
    [RECIPE_HARDEN_DIGGING_STICK] =
        {.id = "harden_digging_stick",
         .name_key = N_("recipe.harden_digging_stick"),
         .stations = OPEN_HEARTH,
         .ingredients = {{{ITEM_DIGGING_STICK, ITEM_NONE}, 1, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_NONE}},
         .work = 750.0f,
         .knowledge = KNOWLEDGE_FIRE,
         .min_level = 55.0f,
         .product = ITEM_DIGGING_STICK_HARDENED,
         .dirt = 0,
         .tool_use = TOOL_USE_NONE},
    /* Roots roasted in the embers: more of their starch is digested. */
    [RECIPE_ROAST_RHIZOMES] =
        {.id = "roast_rhizomes",
         .name_key = N_("recipe.roast_rhizomes"),
         .stations = OPEN_HEARTH,
         .ingredients = {{{ITEM_CATTAIL_RHIZOMES, ITEM_NONE}, 4, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_NONE}},
         .work = 250.0f,
         .knowledge = KNOWLEDGE_FIRE,
         .min_level = 50.0f,
         .product = ITEM_ROASTED_RHIZOMES,
         .dirt = 0,
         .tool_use = TOOL_USE_NONE},
    [RECIPE_ROAST_CARROTS] = {.id = "roast_carrots",
                              .name_key = N_("recipe.roast_carrots"),
                              .stations = OPEN_HEARTH,
                              .ingredients = {{{ITEM_WILD_CARROTS, ITEM_NONE}, 4, ITEM_WANT_ANY}},
                              .input_min_quality = ITEM_QUALITY_WORST,
                              .tools = {{ITEM_NONE}},
                              .work = 250.0f,
                              .knowledge = KNOWLEDGE_FIRE,
                              .min_level = 50.0f,
                              .product = ITEM_ROASTED_CARROTS,
                              .dirt = 0,
                              .tool_use = TOOL_USE_NONE},
    /* A branch split and its edge rounded: the blade shapes it (docs/CONTAINERS.md §5). */
    [RECIPE_WOODEN_SCRAPER] = {.id = "wooden_scraper",
                               .name_key = N_("recipe.wooden_scraper"),
                               .stations = CRAFTING_SPOT,
                               .ingredients = {{{ITEM_BRANCHES, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                               .input_min_quality = ITEM_QUALITY_WORST,
                               .tools = {CUTTING_TOOL},
                               .tool_shapes = true,
                               .work = 750.0f,
                               .knowledge = KNOWLEDGE_CORDAGE,
                               .min_level = 50.0f,
                               .product = ITEM_WOODEN_SCRAPER,
                               .dirt = 3,
                               .tool_use = TOOL_USE_WOODWORKING},
    /* A sharp flake retouched to a point with the hammer stone. */
    [RECIPE_FLINT_BORER] = {.id = "flint_borer",
                            .name_key = N_("recipe.flint_borer"),
                            .stations = CRAFTING_SPOT,
                            .ingredients = {{{ITEM_FLAKE, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                            .input_min_quality = ITEM_QUALITY_WORST,
                            .tools = {HAMMER},
                            .work = 500.0f,
                            .knowledge = KNOWLEDGE_STONE_KNAPPING,
                            .min_level = 60.0f,
                            .product = ITEM_FLINT_BORER,
                            .dirt = 2,
                            .tool_use = TOOL_USE_KNAPPING},
    /* A sheet of bark scored, folded at the corners and pinned with twigs:
     * pliable bark only (fresh, or softened in water), dry bark cracks. */
    [RECIPE_BARK_TRAY] = {.id = "bark_tray",
                          .name_key = N_("recipe.bark_tray"),
                          .stations = CRAFTING_SPOT,
                          .ingredients = {{{ITEM_BIRCH_BARK, ITEM_NONE}, 1, ITEM_WANT_WET},
                                          {{ITEM_TWIGS, ITEM_NONE}, 2, ITEM_WANT_ANY}},
                          .input_min_quality = ITEM_QUALITY_WORST,
                          .tools = {{ITEM_FLAKE}},
                          .tool_shapes = true,
                          .work = 1250.0f,
                          .knowledge = KNOWLEDGE_BASKETRY,
                          .min_level = 50.0f,
                          .product = ITEM_BARK_TRAY,
                          .dirt = 2,
                          .tool_use = TOOL_USE_CUTTING},
    /* The dry stems broken on a log with a cobble, then their fibre scraped
     * clean of bark and pith with a dull wooden edge (docs/CONTAINERS.md §4.1). */
    [RECIPE_NETTLE_FIBRE] = {.id = "nettle_fibre",
                             .name_key = N_("recipe.nettle_fibre"),
                             .stations = CRAFTING_SPOT,
                             .ingredients = {{{ITEM_NETTLE_STEMS, ITEM_NONE}, 1, ITEM_WANT_DRY}},
                             .input_min_quality = ITEM_QUALITY_WORST,
                             .tools = {{ITEM_WOODEN_SCRAPER, ITEM_FLAKE}, HAMMER},
                             .tool_shapes = true,
                             .work = 2500.0f, /* An hour a bundle. */
                             .knowledge = KNOWLEDGE_CORDAGE,
                             .min_level = 50.0f,
                             .product = ITEM_NETTLE_FIBRE,
                             .dirt = 4, /* Shives and dust. */
                             .tool_use = TOOL_USE_SCRAPING},
    /* Two-ply cord twisted by reverse wrapping, with bare hands: a handful of
     * fibre makes two hanks of 5 m, 0.75 h each. */
    [RECIPE_NETTLE_CORD] = {.id = "nettle_cord",
                            .name_key = N_("recipe.nettle_cord"),
                            .stations = CRAFTING_SPOT,
                            .ingredients = {{{ITEM_NETTLE_FIBRE, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                            .input_min_quality = ITEM_QUALITY_WORST,
                            .tools = {{ITEM_NONE}},
                            .work = 3750.0f,
                            .knowledge = KNOWLEDGE_CORDAGE,
                            .min_level = 50.0f,
                            .product = ITEM_NETTLE_CORD,
                            .product_count = 2,
                            .dirt = 0,
                            .tool_use = TOOL_USE_NONE},
    /* Three bundles of leaves plaited by hand, trimmed with a flake: 5 h
     * (docs/CONTAINERS.md §7.1). Woven green, it works loose as it dries. */
    [RECIPE_CATTAIL_BASKET] =
        {.id = "cattail_basket",
         .name_key = N_("recipe.cattail_basket"),
         .stations = CRAFTING_SPOT,
         .ingredients = {{{ITEM_CATTAIL_LEAVES, ITEM_NONE}, 3, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_FLAKE}},
         .work = 12500.0f,
         .knowledge = KNOWLEDGE_BASKETRY,
         .min_level = 55.0f,
         .product = ITEM_CATTAIL_BASKET,
         .dirt = 2,
         .tool_use = TOOL_USE_NONE},
    /* Two sheets of pliable bark folded round a rim of rods and sewn through holes
     * pierced with the borer, a lid fitted: 4.5 h (docs/CONTAINERS.md §7.1). */
    [RECIPE_BARK_BOX] =
        {.id = "bark_box",
         .name_key = N_("recipe.bark_box"),
         .stations = CRAFTING_SPOT,
         .ingredients = {{{ITEM_BIRCH_BARK, ITEM_NONE}, 2, ITEM_WANT_WET},
                         {{ITEM_NETTLE_CORD, ITEM_LIME_BAST, ITEM_SINEW_THREAD}, 1, ITEM_WANT_ANY},
                         {{ITEM_WILLOW_RODS, ITEM_HAZEL_RODS}, 1, ITEM_WANT_WET}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_BONE_AWL, ITEM_FLINT_BORER, ITEM_FLAKE}, {ITEM_FLAKE}},
         .tool_shapes = true,
         .work = 11250.0f,
         .knowledge = KNOWLEDGE_BASKETRY,
         .min_level = 65.0f,
         .product = ITEM_BARK_BOX,
         .dirt = 2,
         .tool_use = TOOL_USE_PIERCING},
    /* Pliable rods (green, or dried and soaked) stuck as stakes and woven round:
     * 8 h (docs/CONTAINERS.md §7.1). */
    [RECIPE_FRAME_BASKET] =
        {.id = "frame_basket",
         .name_key = N_("recipe.frame_basket"),
         .stations = CRAFTING_SPOT,
         .ingredients = {{{ITEM_WILLOW_RODS, ITEM_HAZEL_RODS}, 3, ITEM_WANT_WET}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {CUTTING_TOOL},
         .work = 20000.0f,
         .knowledge = KNOWLEDGE_BASKETRY,
         .min_level = 60.0f,
         .product = ITEM_FRAME_BASKET,
         .dirt = 3,
         .tool_use = TOOL_USE_NONE},
    /* The same, tall and stout, with two straps of cord: 14 h. */
    [RECIPE_PACK_BASKET] = {.id = "pack_basket",
                            .name_key = N_("recipe.pack_basket"),
                            .stations = CRAFTING_SPOT,
                            .ingredients = {{{ITEM_WILLOW_RODS, ITEM_HAZEL_RODS}, 6, ITEM_WANT_WET},
                                            {{ITEM_NETTLE_CORD, ITEM_LIME_CORD}, 2, ITEM_WANT_ANY}},
                            .input_min_quality = ITEM_QUALITY_WORST,
                            .tools = {CUTTING_TOOL},
                            .work = 35000.0f,
                            .knowledge = KNOWLEDGE_BASKETRY,
                            .min_level = 70.0f,
                            .product = ITEM_PACK_BASKET,
                            .dirt = 4,
                            .tool_use = TOOL_USE_NONE},
    /* Fresh poles slit along with a flake and their bark lifted off with the bast
     * under it: 0.3 h (docs/CONTAINERS.md §4.2). */
    [RECIPE_LIME_BARK] = {.id = "lime_bark",
                          .name_key = N_("recipe.lime_bark"),
                          .stations = CRAFTING_SPOT,
                          .ingredients = {{{ITEM_LIME_POLES, ITEM_NONE}, 1, ITEM_WANT_WET}},
                          .input_min_quality = ITEM_QUALITY_WORST,
                          .tools = {{ITEM_FLAKE}},
                          .work = 750.0f,
                          .knowledge = KNOWLEDGE_CORDAGE,
                          .min_level = 50.0f,
                          .product = ITEM_LIME_BARK,
                          .product_count = 2,
                          .dirt = 3,
                          .tool_use = TOOL_USE_PEELING},
    /* Retted bark whose layers come apart, parted by hand into strips: 0.5 h (the
     * work below at the pace retted bark gives, twice, recipe_work_factor()). */
    [RECIPE_LIME_BAST] = {.id = "lime_bast",
                          .name_key = N_("recipe.lime_bast"),
                          .stations = CRAFTING_SPOT,
                          .ingredients = {{{ITEM_LIME_BARK, ITEM_NONE}, 1, ITEM_WANT_RETTED}},
                          .input_min_quality = ITEM_QUALITY_WORST,
                          .tools = {{ITEM_NONE}},
                          .work = 2500.0f,
                          .knowledge = KNOWLEDGE_CORDAGE,
                          .min_level = 50.0f,
                          .product = ITEM_LIME_BAST,
                          .dirt = 2,
                          .tool_use = TOOL_USE_NONE},
    /* Bast twisted like nettle fibre, coarser and faster: two hanks, 0.4 h each. */
    [RECIPE_LIME_CORD] = {.id = "lime_cord",
                          .name_key = N_("recipe.lime_cord"),
                          .stations = CRAFTING_SPOT,
                          .ingredients = {{{ITEM_LIME_BAST, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                          .input_min_quality = ITEM_QUALITY_WORST,
                          .tools = {{ITEM_NONE}},
                          .work = 2000.0f,
                          .knowledge = KNOWLEDGE_CORDAGE,
                          .min_level = 50.0f,
                          .product = ITEM_LIME_CORD,
                          .product_count = 2,
                          .dirt = 0,
                          .tool_use = TOOL_USE_NONE},
    /* About 40 m of cord looped with the fingers, row after row (no needle, no
     * knot): 8 h (docs/CONTAINERS.md §7.1). */
    [RECIPE_NET_BAG] = {.id = "net_bag",
                        .name_key = N_("recipe.net_bag"),
                        .stations = CRAFTING_SPOT,
                        .ingredients = {{{ITEM_NETTLE_CORD, ITEM_LIME_CORD}, 8, ITEM_WANT_ANY}},
                        .input_min_quality = ITEM_QUALITY_WORST,
                        .tools = {{ITEM_NONE}},
                        .work = 20000.0f,
                        .knowledge = KNOWLEDGE_BASKETRY,
                        .min_level = 60.0f,
                        .product = ITEM_NET_BAG,
                        .dirt = 0,
                        .tool_use = TOOL_USE_NONE},
    /* Three bundles of cattail leaves or reeds plaited flat, 3 h (docs/CONTAINERS.md
     * §11); green leaves shrink as they dry and work loose. */
    [RECIPE_MAT] = {.id = "mat",
                    .name_key = N_("recipe.mat"),
                    .stations = CRAFTING_SPOT,
                    .ingredients = {{{ITEM_CATTAIL_LEAVES, ITEM_REEDS}, 3, ITEM_WANT_ANY}},
                    .input_min_quality = ITEM_QUALITY_WORST,
                    .tools = {{ITEM_NONE}},
                    .work = 7500.0f,
                    .knowledge = KNOWLEDGE_BASKETRY,
                    .min_level = 50.0f,
                    .product = ITEM_MAT,
                    .dirt = 2,
                    .tool_use = TOOL_USE_NONE},
    /* Thorns scraped off with a flake, each cane split in four along its length:
     * 0.5 h a bundle, two bundles of binding (docs/CONTAINERS.md §11). */
    [RECIPE_SPLIT_CANES] = {.id = "split_canes",
                            .name_key = N_("recipe.split_canes"),
                            .stations = CRAFTING_SPOT,
                            .ingredients = {{{ITEM_BRAMBLE_CANES, ITEM_NONE}, 1, ITEM_WANT_WET}},
                            .input_min_quality = ITEM_QUALITY_WORST,
                            .tools = {{ITEM_FLAKE}},
                            .tool_shapes = true,
                            .work = 1250.0f,
                            .knowledge = KNOWLEDGE_BASKETRY,
                            .min_level = 50.0f,
                            .product = ITEM_SPLIT_CANES,
                            .product_count = 2,
                            .dirt = 2,
                            .tool_use = TOOL_USE_CUTTING},
    /* A coil of dry grass or reeds wound round and sewn to the coil below with split
     * cane through holes opened with the borer: 30 h of patient work, the strongest
     * basket there is. */
    [RECIPE_COILED_BASKET] = {.id = "coiled_basket",
                              .name_key = N_("recipe.coiled_basket"),
                              .stations = CRAFTING_SPOT,
                              .ingredients = {{{ITEM_SPLIT_CANES, ITEM_NONE}, 2, ITEM_WANT_WET},
                                              {{ITEM_GRASS, ITEM_REEDS}, 4, ITEM_WANT_DRY}},
                              .input_min_quality = ITEM_QUALITY_WORST,
                              .tools = {{ITEM_BONE_AWL, ITEM_FLINT_BORER, ITEM_FLAKE}},
                              .work = 75000.0f,
                              .knowledge = KNOWLEDGE_BASKETRY,
                              .min_level = 65.0f,
                              .product = ITEM_COILED_BASKET,
                              .dirt = 2,
                              .tool_use = TOOL_USE_NONE},
    /* Mending (docs/CONTAINERS.md §11): broken leaves plaited anew, 1 h. */
    [RECIPE_MEND_CATTAIL_BASKET] =
        MEND("mend_cattail_basket", N_("recipe.mend_cattail_basket"), ITEM_CATTAIL_BASKET,
             ITEM_CATTAIL_LEAVES, ITEM_NONE, 1, ITEM_WANT_ANY, ITEM_FLAKE, ITEM_NONE, 2500.0f),
    /* Broken weavers replaced with pliable rods, 1.5 h. */
    [RECIPE_MEND_FRAME_BASKET] = MEND("mend_frame_basket", N_("recipe.mend_frame_basket"),
                                      ITEM_FRAME_BASKET, ITEM_WILLOW_RODS, ITEM_HAZEL_RODS, 1,
                                      ITEM_WANT_WET, ITEM_FLAKE, ITEM_HAND_AXE, 3750.0f),
    /* A new rim and weavers, 2 h. */
    [RECIPE_MEND_PACK_BASKET] =
        MEND("mend_pack_basket", N_("recipe.mend_pack_basket"), ITEM_PACK_BASKET, ITEM_WILLOW_RODS,
             ITEM_HAZEL_RODS, 2, ITEM_WANT_WET, ITEM_FLAKE, ITEM_HAND_AXE, 5000.0f),
    /* The seams sewn again, through holes pierced anew, 1 h. */
    [RECIPE_MEND_BARK_BOX] =
        MEND("mend_bark_box", N_("recipe.mend_bark_box"), ITEM_BARK_BOX, ITEM_NETTLE_CORD,
             ITEM_LIME_BAST, 1, ITEM_WANT_ANY, ITEM_FLINT_BORER, ITEM_FLAKE, 2500.0f),
    /* Broken loops knotted and looped again by hand, 1 h. */
    [RECIPE_MEND_NET_BAG] =
        MEND("mend_net_bag", N_("recipe.mend_net_bag"), ITEM_NET_BAG, ITEM_NETTLE_CORD,
             ITEM_LIME_CORD, 1, ITEM_WANT_ANY, ITEM_NONE, ITEM_NONE, 2500.0f),
    /* Broken stitches sewn again with split cane, 2 h. */
    [RECIPE_MEND_COILED_BASKET] =
        MEND("mend_coiled_basket", N_("recipe.mend_coiled_basket"), ITEM_COILED_BASKET,
             ITEM_SPLIT_CANES, ITEM_NONE, 1, ITEM_WANT_WET, ITEM_FLINT_BORER, ITEM_FLAKE, 5000.0f),
    /* A carcass skinned and cut up with a flake (an axe or a chopper for the joints):
     * 40 ticks a kilogram, a red deer about 1.8 h (docs/KNOWLEDGE.md §9.2). */
    [RECIPE_BUTCHER] = {.id = "butcher",
                        .name_key = N_("recipe.butcher"),
                        .stations = BUTCHERING_SPOT,
                        .ingredients = {{{ITEM_ROE_DEER_CARCASS, ITEM_RED_DEER_CARCASS,
                                          ITEM_BOAR_CARCASS, ITEM_HARE_CARCASS, ITEM_WOLF_CARCASS,
                                          ITEM_FOX_CARCASS},
                                         1,
                                         ITEM_WANT_ANY}},
                        .input_min_quality = ITEM_QUALITY_WORST,
                        .tools = {{ITEM_FLAKE, ITEM_HAND_AXE, ITEM_CHOPPER}},
                        .work = 40.0f,
                        .knowledge = KNOWLEDGE_BUTCHERING,
                        .min_level = 50.0f,
                        .product = ITEM_RAW_MEAT,
                        .dirt = 10, /* Blood and offal. */
                        .tool_use = TOOL_USE_BUTCHERING,
                        .butchers = true},
    /* Two bones cracked on a stone with a cobble: 0.1 h. */
    [RECIPE_CRACK_BONES] = {.id = "crack_bones",
                            .name_key = N_("recipe.crack_bones"),
                            .stations = BUTCHERING_SPOT,
                            .ingredients = {{{ITEM_BONE, ITEM_NONE}, 2, ITEM_WANT_ANY}},
                            .input_min_quality = ITEM_QUALITY_WORST,
                            .tools = {HAMMER},
                            .work = 250.0f,
                            .knowledge = KNOWLEDGE_BUTCHERING,
                            .min_level = 50.0f,
                            .product = ITEM_MARROW,
                            .dirt = 2,
                            .tool_use = TOOL_USE_NONE},
    /* Two portions roasted on the embers: 0.1 h. */
    [RECIPE_ROAST_MEAT] = {.id = "roast_meat",
                           .name_key = N_("recipe.roast_meat"),
                           .stations = OPEN_HEARTH,
                           .ingredients = {{{ITEM_RAW_MEAT, ITEM_NONE}, 2, ITEM_WANT_ANY}},
                           .input_min_quality = ITEM_QUALITY_WORST,
                           .tools = {{ITEM_NONE}},
                           .work = 250.0f,
                           .knowledge = KNOWLEDGE_FIRE,
                           .min_level = 50.0f,
                           .product = ITEM_ROASTED_MEAT,
                           .dirt = 0,
                           .tool_use = TOOL_USE_NONE},
    /* Dried sinew pounded on a stone with a cobble, then shredded into fibres: two hanks of
     * thread, 0.5 h. */
    [RECIPE_SINEW_THREAD] = {.id = "sinew_thread",
                             .name_key = N_("recipe.sinew_thread"),
                             .stations = CRAFTING_SPOT,
                             .ingredients = {{{ITEM_SINEW, ITEM_NONE}, 1, ITEM_WANT_DRY}},
                             .input_min_quality = ITEM_QUALITY_WORST,
                             .tools = {HAMMER},
                             .work = 1250.0f,
                             .knowledge = KNOWLEDGE_CORDAGE,
                             .min_level = 50.0f,
                             .product = ITEM_SINEW_THREAD,
                             .product_count = 2,
                             .dirt = 1,
                             .tool_use = TOOL_USE_NONE},
    /* A long bone split with the hammer stone, its splinters grooved and ground to points
     * with a flake: two awls, 0.6 h. */
    [RECIPE_BONE_AWL] = {.id = "bone_awl",
                         .name_key = N_("recipe.bone_awl"),
                         .stations = CRAFTING_SPOT,
                         .ingredients = {{{ITEM_BONE, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                         .input_min_quality = ITEM_QUALITY_WORST,
                         .tools = {{ITEM_FLAKE}, HAMMER},
                         .tool_shapes = true,
                         .work = 1500.0f,
                         .knowledge = KNOWLEDGE_BONE_WORKING,
                         .min_level = 50.0f,
                         .product = ITEM_BONE_AWL,
                         .product_count = 2,
                         .dirt = 3,
                         .tool_use = TOOL_USE_CUTTING},
    /* Thin splinters ground into needles, an eye drilled through each with a borer or an
     * awl: three needles, 1.5 h. */
    [RECIPE_BONE_NEEDLE] = {.id = "bone_needle",
                            .name_key = N_("recipe.bone_needle"),
                            .stations = CRAFTING_SPOT,
                            .ingredients = {{{ITEM_BONE, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                            .input_min_quality = ITEM_QUALITY_WORST,
                            .tools = {{ITEM_FLINT_BORER, ITEM_BONE_AWL}, {ITEM_FLAKE}},
                            .tool_shapes = true,
                            .work = 3750.0f,
                            .knowledge = KNOWLEDGE_BONE_WORKING,
                            .min_level = 65.0f,
                            .product = ITEM_BONE_NEEDLE,
                            .product_count = 3,
                            .dirt = 2,
                            .tool_use = TOOL_USE_PIERCING},
    /* A raw hide scraped of flesh and fat with a dull scraper, then worked soft with fat
     * (as brains or marrow were used): pieces of dressed hide by its size, 1 h a piece
     * (recipe_work_factor()). */
    [RECIPE_DRESS_HIDE] = {.id = "dress_hide",
                           .name_key = N_("recipe.dress_hide"),
                           .stations = CRAFTING_SPOT,
                           .ingredients = {{{ITEM_RAW_HIDE_MEDIUM, ITEM_RAW_HIDE_SMALL,
                                             ITEM_RAW_HIDE_LARGE},
                                            1,
                                            ITEM_WANT_ANY},
                                           {{ITEM_FAT, ITEM_MARROW}, 1, ITEM_WANT_ANY}},
                           .input_min_quality = ITEM_QUALITY_WORST,
                           .tools = {{ITEM_WOODEN_SCRAPER, ITEM_FLAKE}},
                           .tool_shapes = true,
                           .work = 2500.0f,
                           .knowledge = KNOWLEDGE_SKIN_WORKING,
                           .min_level = 50.0f,
                           .product = ITEM_LEATHER,
                           .dirt = 4,
                           .tool_use = TOOL_USE_SCRAPING},
    /* Two pieces sewn with sinew thread (or cord) into a pouch with a strap: 2 h. */
    [RECIPE_HIDE_BAG] =
        {.id = "hide_bag",
         .name_key = N_("recipe.hide_bag"),
         .stations = CRAFTING_SPOT,
         .ingredients = {{{ITEM_LEATHER, ITEM_NONE}, 2, ITEM_WANT_ANY},
                         {{ITEM_SINEW_THREAD, ITEM_NETTLE_CORD, ITEM_LIME_CORD}, 1, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_BONE_NEEDLE, ITEM_BONE_AWL, ITEM_FLINT_BORER}},
         .tool_shapes = true,
         .work = 5000.0f,
         .knowledge = KNOWLEDGE_SKIN_WORKING,
         .min_level = 55.0f,
         .product = ITEM_HIDE_BAG,
         .dirt = 1,
         .tool_use = TOOL_USE_SEWING},
    /* A torn seam sewn again, a patch of hide: 1 h. */
    [RECIPE_MEND_HIDE_BAG] =
        MEND("mend_hide_bag", N_("recipe.mend_hide_bag"), ITEM_HIDE_BAG, ITEM_LEATHER, ITEM_NONE, 1,
             ITEM_WANT_ANY, ITEM_BONE_NEEDLE, ITEM_BONE_AWL, 2500.0f),
    /* Three sheets of bark rolled tight and heated under a mound of hot ashes, the tar
     * scraped from where it gathers: a lump, 1 h at a hearth (docs/KNOWLEDGE.md §13). */
    [RECIPE_BIRCH_TAR] = {.id = "birch_tar",
                          .name_key = N_("recipe.birch_tar"),
                          .stations = OPEN_HEARTH,
                          .ingredients = {{{ITEM_BIRCH_BARK, ITEM_NONE}, 3, ITEM_WANT_ANY}},
                          .input_min_quality = ITEM_QUALITY_WORST,
                          .tools = {{ITEM_NONE}},
                          .work = 2500.0f,
                          .knowledge = KNOWLEDGE_HAFTING,
                          .min_level = 50.0f,
                          .product = ITEM_BIRCH_TAR,
                          .dirt = 1,
                          .tool_use = TOOL_USE_NONE},
    /* The spear's tip split, a flake set in it with warm tar: 1 h. */
    [RECIPE_STONE_TIPPED_THROWING_SPEAR] =
        {.id = "stone_tipped_throwing_spear",
         .name_key = N_("recipe.stone_tipped_throwing_spear"),
         .stations = CRAFTING_SPOT,
         .ingredients = {{{ITEM_THROWING_SPEAR_HARDENED, ITEM_THROWING_SPEAR}, 1, ITEM_WANT_ANY},
                         {{ITEM_FLAKE, ITEM_NONE}, 1, ITEM_WANT_ANY},
                         {{ITEM_BIRCH_TAR, ITEM_NONE}, 1, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {CUTTING_TOOL},
         .work = 2500.0f,
         .knowledge = KNOWLEDGE_HAFTING,
         .min_level = 50.0f,
         .product = ITEM_STONE_TIPPED_THROWING_SPEAR,
         .dirt = 2,
         .tool_use = TOOL_USE_WOODWORKING},
    [RECIPE_STONE_TIPPED_THRUSTING_SPEAR] =
        {.id = "stone_tipped_thrusting_spear",
         .name_key = N_("recipe.stone_tipped_thrusting_spear"),
         .stations = CRAFTING_SPOT,
         .ingredients = {{{ITEM_THRUSTING_SPEAR_HARDENED, ITEM_THRUSTING_SPEAR}, 1, ITEM_WANT_ANY},
                         {{ITEM_FLAKE, ITEM_NONE}, 1, ITEM_WANT_ANY},
                         {{ITEM_BIRCH_TAR, ITEM_NONE}, 1, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {CUTTING_TOOL},
         .work = 2500.0f,
         .knowledge = KNOWLEDGE_HAFTING,
         .min_level = 50.0f,
         .product = ITEM_STONE_TIPPED_THRUSTING_SPEAR,
         .dirt = 2,
         .tool_use = TOOL_USE_WOODWORKING},
    /* A handle cut from a branch and split, the hand axe set in with tar and bound: 1.5 h. */
    [RECIPE_HAFTED_AXE] = {.id = "hafted_axe",
                           .name_key = N_("recipe.hafted_axe"),
                           .stations = CRAFTING_SPOT,
                           .ingredients = {{{ITEM_HAND_AXE, ITEM_NONE}, 1, ITEM_WANT_ANY},
                                           {{ITEM_BRANCHES, ITEM_NONE}, 1, ITEM_WANT_ANY},
                                           {{ITEM_BIRCH_TAR, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                           .input_min_quality = ITEM_QUALITY_WORST,
                           .tools = {{ITEM_FLAKE}},
                           .work = 3750.0f,
                           .knowledge = KNOWLEDGE_HAFTING,
                           .min_level = 55.0f,
                           .product = ITEM_HAFTED_AXE,
                           .dirt = 3,
                           .tool_use = TOOL_USE_WOODWORKING},
};

#undef CRAFTING_SPOT
#undef OPEN_HEARTH
#undef BUTCHERING_SPOT
#undef HAMMER
#undef CUTTING_TOOL
#undef MEND

const RecipeDef *recipe_def(RecipeType recipe)
{
    return (unsigned)recipe < RECIPE_COUNT ? &RECIPE_DEFS[recipe] : NULL;
}

const RecipeIngredient *recipe_ingredient(RecipeType recipe, int index)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && index >= 0 && index < RECIPE_INGREDIENT_MAX &&
                   def->ingredients[index].count > 0
               ? &def->ingredients[index]
               : NULL;
}

int recipe_ingredient_count(RecipeType recipe)
{
    int count = 0;
    while (recipe_ingredient(recipe, count) != NULL) {
        count++;
    }
    return count;
}

ItemTypeMask recipe_ingredient_types(RecipeType recipe, int index)
{
    const RecipeIngredient *ingredient = recipe_ingredient(recipe, index);
    ItemTypeMask mask = item_types_none();
    for (int i = 0; ingredient != NULL && i < RECIPE_INPUT_ALTERNATIVES; i++) {
        if (ingredient->types[i] != ITEM_NONE) {
            item_types_add(&mask, ingredient->types[i]);
        }
    }
    return mask;
}

ItemTypeMask recipe_inputs(RecipeType recipe)
{
    ItemTypeMask mask = item_types_none();
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        mask = item_types_union(mask, recipe_ingredient_types(recipe, i));
    }
    return mask;
}

ItemTypeMask recipe_tool_types(const RecipeDef *def, int slot)
{
    if (def == NULL || slot < 0 || slot >= RECIPE_TOOL_MAX) {
        return item_types_none();
    }
    return item_types_list(def->tools[slot], RECIPE_TOOL_ALTERNATIVES);
}

int recipe_ingredient_of(RecipeType recipe, ItemType type)
{
    if ((unsigned)type >= ITEM_TYPE_COUNT || type == ITEM_NONE) {
        return -1;
    }
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        if (item_types_has(recipe_ingredient_types(recipe, i), type)) {
            return i;
        }
    }
    return -1;
}

bool recipe_accepts_ingredient(RecipeType recipe, int index, ItemStack stack)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && stack.type != ITEM_NONE &&
           item_types_has(recipe_ingredient_types(recipe, index), (ItemType)stack.type) &&
           stack.quality >= def->input_min_quality;
}

bool recipe_accepts_input(RecipeType recipe, ItemStack stack)
{
    return recipe_accepts_ingredient(recipe, recipe_ingredient_of(recipe, (ItemType)stack.type),
                                     stack);
}

bool recipe_doable_at(RecipeType recipe, double level)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && (def->knowledge == KNOWLEDGE_NONE || level >= def->min_level);
}

/* Knapping a cobble rather than flint: it counts as a crude stone (this
 * quality) whatever its own. */
#define COBBLE_KNAPPING_QUALITY 10
/* A tool that shapes the material counts for this share of the average
 * quality that caps the products (docs/CONTAINERS.md §6). */
#define SHAPING_TOOL_SHARE 0.25
/* A flake scrapes fibre too sharply: it cuts some of it (docs/CONTAINERS.md §5). */
#define FLAKE_SCRAPING_CAP_MALUS 10
/* Green cattail leaves shrink as they dry: the basket works loose (§4.3). */
#define GREEN_WEAVING_CAP_MALUS 15

static bool knaps_a_cobble(const RecipeDef *def, ItemStack input)
{
    return def->knowledge == KNOWLEDGE_STONE_KNAPPING && input.type == ITEM_COBBLE;
}

/* The quality an ingredient counts for in the cap. */
static int material_quality(const RecipeDef *def, ItemStack input)
{
    return knaps_a_cobble(def, input) ? COBBLE_KNAPPING_QUALITY : input.quality;
}

ItemQuality recipe_quality_cap_of(RecipeType recipe, const ItemStack inputs[RECIPE_INGREDIENT_MAX],
                                  ItemStack tool)
{
    const RecipeDef *def = recipe_def(recipe);
    if (def == NULL) {
        return ITEM_QUALITY_BEST;
    }
    double sum = 0.0;
    long count = 0;
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        const ItemDef *material = item_def((ItemType)inputs[i].type);
        if (inputs[i].count > 0 && material != NULL && material->has_quality) {
            sum += (double)material_quality(def, inputs[i]) * inputs[i].count;
            count += inputs[i].count;
        }
    }
    const ItemDef *shaping = item_def((ItemType)tool.type);
    bool tool_counts =
        def->tool_shapes && tool.count > 0 && shaping != NULL && shaping->has_quality;
    if (count == 0 && !tool_counts) {
        return ITEM_QUALITY_BEST; /* Nothing caps it. */
    }
    double base = count > 0 ? sum / (double)count : (double)tool.quality;
    if (tool_counts && count > 0) {
        base = (1.0 - SHAPING_TOOL_SHARE) * base + SHAPING_TOOL_SHARE * (double)tool.quality;
    }
    int cap = (int)floor(base + 0.5) + ITEM_QUALITY_CAP_BONUS;
    /* Woven green, what shrinks as it dries works loose. */
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        const ItemDef *material = item_def((ItemType)inputs[i].type);
        if (inputs[i].count > 0 && material != NULL && material->shrinks_green &&
            item_moisture_green((ItemType)inputs[i].type, item_stack_moisture(inputs[i]))) {
            cap -= GREEN_WEAVING_CAP_MALUS;
            break;
        }
    }
    /* Osier rods weave finer than hazel's. */
    if (inputs[0].count > 0 && item_def((ItemType)inputs[0].type) != NULL) {
        cap += item_def((ItemType)inputs[0].type)->craft_cap_bonus;
    }
    /* Retted stems give a finer fibre; over retted, a rotten one. */
    if (inputs[0].count > 0 && item_def((ItemType)inputs[0].type) != NULL &&
        item_def((ItemType)inputs[0].type)->rets) {
        cap += item_retting_cap_bonus(item_stack_retting(inputs[0]));
    }
    if (def->tool_use == TOOL_USE_SCRAPING && tool.count > 0 && tool.type == ITEM_FLAKE) {
        cap -= FLAKE_SCRAPING_CAP_MALUS;
    }
    return clampi(cap, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST);
}

ItemQuality recipe_quality_cap(RecipeType recipe, ItemStack input)
{
    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {input};
    return recipe_quality_cap_of(recipe, inputs, (ItemStack){.type = ITEM_NONE});
}

float recipe_work_factor(RecipeType recipe, const ItemStack inputs[RECIPE_INGREDIENT_MAX])
{
    const ItemDef *main = item_def((ItemType)inputs[0].type);
    if (recipe_def(recipe) == NULL || inputs[0].count <= 0 || main == NULL) {
        return 1.0f;
    }
    if (recipe == RECIPE_DRESS_HIDE) { /* `work` a piece of the hide. */
        int pieces = recipe_hide_pieces((ItemType)inputs[0].type);
        return pieces > 0 ? 1.0f / (float)pieces : 1.0f;
    }
    if (recipe_def(recipe)->butchers) { /* `work` a kilogram of carcass. */
        ItemStack one = inputs[0];
        one.count = 1;
        float kg = item_stack_mass(one);
        return kg > 0.0f ? 1.0f / kg : 1.0f;
    }
    if (main->craft_work_factor > 0.0f) { /* Osier rods: woven faster. */
        return 1.0f / main->craft_work_factor;
    }
    return main->rets ? item_retting_work_factor(item_stack_retting(inputs[0])) : 1.0f;
}

bool recipe_needs_fire(RecipeType recipe)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && (def->stations & WORKSTATION_BIT(WORKSTATION_OPEN_HEARTH));
}

float recipe_failure_chance(RecipeType recipe, double level)
{
    if (recipe != RECIPE_HAND_AXE) {
        return 0.0f;
    }
    float above = (float)level - RECIPE_DEFS[recipe].min_level;
    return clampf(HAND_AXE_FAILURE_AT_MIN - HAND_AXE_FAILURE_PER_LEVEL * above, 0.0f, 1.0f);
}

/* Points above mastery, never negative. */
static int steps_above_mastery(double level)
{
    double above = level - KNOWLEDGE_MASTERY_LEVEL;
    return above > 0.0 ? (int)floor(above / FLAKES_LEVEL_STEP) : 0;
}

/* `count` items of a product made at `level`, no better than `cap`. */
static ItemStack product(ItemType type, int count, double level, Rng *rng, ItemQuality cap)
{
    ItemStack stack = knowledge_product(type, count, level, rng);
    if (stack.count > 0 && stack.quality > cap) {
        stack.quality = (uint8_t)cap;
    }
    return stack;
}

/* The products of recipe_craft_with(), before the wood species: `input` is the
 * main ingredient (the first), `cap` the products' best quality. */
static int craft_products(RecipeType recipe, const RecipeDef *def, ItemStack input, ItemQuality cap,
                          double level, Rng *rng, ItemStack out[RECIPE_PRODUCT_MAX])
{
    switch (recipe) {
    case RECIPE_FLAKES:
        out[0] = product(ITEM_FLAKE, FLAKES_BASE + steps_above_mastery(level), level, rng, cap);
        return 1;
    case RECIPE_HAND_AXE: {
        float failure = recipe_failure_chance(recipe, level);
        if (rng != NULL && failure > 0.0f && rng_next_float(rng) < failure) {
            /* The stone is lost; some flakes are left. */
            out[0] = product(ITEM_FLAKE, FAILED_FLAKES_BASE + steps_above_mastery(level), level,
                             rng, cap);
            return 1;
        }
        out[0] = product(ITEM_HAND_AXE, 1, level, rng, cap);
        out[1] = product(ITEM_FLAKE, HAND_AXE_FLAKES, level, rng, cap);
        return 2;
    }
    case RECIPE_HARDEN_THROWING_SPEAR:
    case RECIPE_HARDEN_THRUSTING_SPEAR:
    case RECIPE_HARDEN_DIGGING_STICK:
        /* The same spear or stick, its quality and wear kept, lasting longer. */
        out[0] = item_stack_make(
            def->product, 1, input.quality,
            item_hit_points_at_percent(def->product, item_stack_hit_points_percent(input)));
        return 1;
    case RECIPE_DRESS_HIDE:
        out[0] = product(def->product, recipe_hide_pieces((ItemType)input.type), level, rng, cap);
        return out[0].count > 0 ? 1 : 0;
    case RECIPE_ROAST_RHIZOMES:
    case RECIPE_ROAST_CARROTS:
    case RECIPE_ROAST_MEAT: {
        /* One roasted root per raw one, keeping no longer than the raw ones
         * would have (as long, though: a root about to rot does not rot
         * sooner for being roasted). */
        out[0] = product(def->product, def->ingredients[0].count, level, rng, cap);
        float hours = item_stack_rot_hours(input, 1.0f);
        if (out[0].count > 0 && hours >= 0.0f) {
            int most = item_hit_points_lasting(def->product, hours);
            if (out[0].hit_points > most) {
                out[0].hit_points = (uint16_t)most;
            }
        }
        return 1;
    }
    default:
        out[0] =
            product(def->product, def->product_count > 0 ? def->product_count : 1, level, rng, cap);
        return 1;
    }
}

int recipe_hide_pieces(ItemType hide)
{
    switch (hide) {
    case ITEM_RAW_HIDE_SMALL: return 1;
    case ITEM_RAW_HIDE_MEDIUM: return 4;
    case ITEM_RAW_HIDE_LARGE: return 12;
    default: return 0;
    }
}

float recipe_butcher_efficiency(double level)
{
    return clampf(0.7f + 0.006f * (float)(level - 50.0), 0.7f, 1.0f);
}

/* `kg` of a product of `type` at `level`, its hit points the carcass's share of its own. */
static int butcher_product(ItemType type, float kg, float condition, double level, Rng *rng,
                           ItemStack *out)
{
    int count = (int)lroundf(kg / item_def(type)->weight);
    if (count <= 0) {
        return 0;
    }
    *out = knowledge_product(type, count, level, rng);
    int hit_points = (int)lroundf((float)out->hit_points * condition);
    out->hit_points = (uint16_t)(hit_points > 1 ? hit_points : 1);
    return 1;
}

/* A carcass cut up (docs/KNOWLEDGE.md §9.3): meat, fat, a hide (if not eaten away), bones and
 * sinew, by the weight left and the butcher's efficiency; its rot passes to them. */
static int butcher_products(ItemStack carcass, double level, Rng *rng,
                            ItemStack out[RECIPE_PRODUCT_MAX])
{
    const ItemDef *def = item_def((ItemType)carcass.type);
    if (def == NULL || carcass.count <= 0) {
        return 0;
    }
    ItemStack one = carcass;
    one.count = 1;
    float yield = item_stack_mass(one) * recipe_butcher_efficiency(level);
    float condition = clampf((float)carcass.hit_points / (float)def->max_hit_points, 0.0f, 1.0f);
    int count = 0;
    count += butcher_product(ITEM_RAW_MEAT, yield * BUTCHER_MEAT_SHARE, condition, level, rng,
                             &out[count]);
    count +=
        butcher_product(ITEM_FAT, yield * BUTCHER_FAT_SHARE, condition, level, rng, &out[count]);
    if ((float)carcass.eaten < BUTCHER_HIDE_EATEN_MAX * (float)ITEM_EATEN_FULL) {
        ItemType hide = def->weight < BUTCHER_HIDE_SMALL_BELOW_KG    ? ITEM_RAW_HIDE_SMALL
                        : def->weight < BUTCHER_HIDE_MEDIUM_BELOW_KG ? ITEM_RAW_HIDE_MEDIUM
                                                                     : ITEM_RAW_HIDE_LARGE;
        count += butcher_product(hide, item_def(hide)->weight, condition, level, rng, &out[count]);
    }
    count +=
        butcher_product(ITEM_BONE, yield * BUTCHER_BONE_SHARE, condition, level, rng, &out[count]);
    count += butcher_product(ITEM_SINEW, yield * BUTCHER_SINEW_SHARE, condition, level, rng,
                             &out[count]);
    return count;
}

/* The species of the first wooden ingredient (0 when none is wooden). */
static WoodMask ingredient_woods(const ItemStack inputs[RECIPE_INGREDIENT_MAX])
{
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        const ItemDef *material = item_def((ItemType)inputs[i].type);
        if (inputs[i].count > 0 && material != NULL && material->wooden) {
            return inputs[i].woods;
        }
    }
    return 0;
}

int recipe_craft_with(RecipeType recipe, const ItemStack inputs[RECIPE_INGREDIENT_MAX],
                      ItemStack tool, double level, Rng *rng, ItemStack out[RECIPE_PRODUCT_MAX])
{
    const RecipeDef *def = recipe_def(recipe);
    if (def == NULL) {
        return 0;
    }
    if (def->butchers) {
        return butcher_products(inputs[0], level, rng, out);
    }
    if (def->mends) { /* The same item, made whole again. */
        const ItemDef *mended = item_def((ItemType)inputs[0].type);
        if (inputs[0].count <= 0 || mended == NULL) {
            return 0;
        }
        out[0] = inputs[0];
        out[0].count = 1;
        out[0].hit_points = (uint16_t)mended->max_hit_points;
        return 1;
    }
    ItemQuality cap = recipe_quality_cap_of(recipe, inputs, tool);
    int count = craft_products(recipe, def, inputs[0], cap, level, rng, out);
    /* What is made of wood keeps every species of its material
     * (docs/TREES.md §6); a new shaft is as good as its wood makes it, within
     * the material's cap (hardening keeps the shaft's quality). */
    bool hardening = recipe == RECIPE_HARDEN_THROWING_SPEAR ||
                     recipe == RECIPE_HARDEN_THRUSTING_SPEAR ||
                     recipe == RECIPE_HARDEN_DIGGING_STICK;
    WoodMask woods = ingredient_woods(inputs);
    for (int i = 0; i < count; i++) {
        if (!item_def((ItemType)out[i].type)->wooden) {
            continue;
        }
        out[i].woods = woods;
        if (!hardening && out[i].count > 0) {
            int quality = out[i].quality + wood_shafts(out[i].woods);
            out[i].quality = (uint8_t)clampi(quality, ITEM_QUALITY_WORST,
                                             quality > out[i].quality ? cap : ITEM_QUALITY_BEST);
        }
    }
    return count;
}

int recipe_craft(RecipeType recipe, ItemStack input, double level, Rng *rng,
                 ItemStack out[RECIPE_PRODUCT_MAX])
{
    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {input};
    return recipe_craft_with(recipe, inputs, (ItemStack){.type = ITEM_NONE}, level, rng, out);
}

ItemStack recipe_average_product(RecipeType recipe, ItemType input, int input_quality,
                                 WoodMask woods, double level)
{
    ItemStack none = {.type = ITEM_NONE};
    const RecipeDef *def = recipe_def(recipe);
    if (def == NULL) {
        return none;
    }
    int given = input != ITEM_NONE ? recipe_ingredient_of(recipe, input) : -1;
    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {{.type = ITEM_NONE}};
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        const RecipeIngredient *ingredient = recipe_ingredient(recipe, i);
        if (ingredient == NULL) {
            continue;
        }
        ItemType type = i == given ? input : ingredient->types[0];
        const ItemDef *material = item_def(type);
        if (type == ITEM_NONE || material == NULL) {
            continue;
        }
        inputs[i] = item_stack_make(type, ingredient->count,
                                    clampi(input_quality, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST),
                                    material->max_hit_points);
        inputs[i].woods = material->wooden ? woods : 0;
    }
    ItemStack made[RECIPE_PRODUCT_MAX];
    int count =
        recipe_craft_with(recipe, inputs, (ItemStack){.type = ITEM_NONE}, level, NULL, made);
    for (int i = 0; i < count; i++) {
        if (made[i].type == def->product) {
            return made[i];
        }
    }
    return item_stack_make(def->product, 0, ITEM_QUALITY_STANDARD, 0);
}
