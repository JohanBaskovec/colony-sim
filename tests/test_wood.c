#include "core/wood.h"

#include "core/hearth.h"
#include "core/i18n.h"
#include "core/item.h"
#include "core/recipe.h"
#include "core/structure.h"
#include "core/tool.h"

#include "test_framework.h"

#include <string.h>

#define OAK wood_bit(PLANT_OAK)
#define ASH wood_bit(PLANT_ASH)
#define BEECH wood_bit(PLANT_BEECH)
#define BIRCH wood_bit(PLANT_BIRCH)
#define CHESTNUT wood_bit(PLANT_CHESTNUT)

static ItemStack of_wood(ItemType type, int count, WoodMask woods)
{
    ItemStack stack = item_stack_pristine(type, count);
    stack.quality = ITEM_QUALITY_STANDARD;
    stack.woods = woods;
    return stack;
}

/* A bit per tree species, the hazel's too; none for other plants. */
static void test_species(void)
{
    WoodMask all = 0;
    for (int type = 0; type < PLANT_COUNT; type++) {
        WoodMask bit = wood_bit((PlantType)type);
        CHECK_EQ_INT(bit != 0, plant_is_tree((uint8_t)type));
        CHECK_EQ_INT(wood_def((PlantType)type) != NULL, plant_is_tree((uint8_t)type));
        if (bit != 0) {
            CHECK_EQ_INT(wood_count(bit), 1);
            CHECK_EQ_INT(wood_tree(bit), type);
            CHECK((all & bit) == 0);
            all |= bit;
        }
    }
    CHECK_EQ_INT(all, WOOD_ALL);
    CHECK_EQ_INT(wood_count(WOOD_ALL), WOOD_SPECIES_COUNT);
    CHECK_EQ_INT(wood_tree(OAK | ASH), PLANT_NONE);
    CHECK_EQ_INT(wood_tree(0), PLANT_NONE);
}

/* A mix takes the mean of its species; no species, the plain values. */
static void test_mixes_take_the_mean(void)
{
    const WoodDef *oak = wood_def(PLANT_OAK), *ash = wood_def(PLANT_ASH);
    CHECK_NEAR(wood_weight(OAK), oak->weight, 1e-6);
    CHECK_NEAR(wood_weight(OAK | ASH), (oak->weight + ash->weight) / 2.0f, 1e-6);
    CHECK_NEAR(wood_toughness(OAK | ASH), (oak->toughness + ash->toughness) / 2.0f, 1e-6);
    CHECK_NEAR(wood_fuel(0), 1.0f, 1e-6);
    CHECK_NEAR(wood_weathering(0), 1.0f, 1e-6);
    CHECK_EQ_INT(wood_shafts(0), 0);
    CHECK_EQ_INT(wood_shafts(ASH), 10);
    CHECK_EQ_INT(wood_shafts(CHESTNUT), -5);
    CHECK_EQ_INT(wood_shafts(ASH | CHESTNUT), 3); /* 2.5, rounded. */
    /* What the species are known for. */
    CHECK(wood_def(PLANT_BEECH)->fuel > wood_def(PLANT_BIRCH)->fuel);
    CHECK(wood_def(PLANT_OAK)->weathering < wood_def(PLANT_BIRCH)->weathering);
    CHECK(wood_def(PLANT_ASH)->toughness > wood_def(PLANT_CHESTNUT)->toughness);
}

/* Only wooden items carry species, and items of different species never
 * share a stack. */
static void test_items_keep_their_species_apart(void)
{
    CHECK(item_def(ITEM_BRANCHES)->wooden && item_def(ITEM_WOOD)->wooden);
    CHECK(item_def(ITEM_THRUSTING_SPEAR_HARDENED)->wooden);
    CHECK(!item_def(ITEM_TWIGS)->wooden && !item_def(ITEM_FLINT)->wooden);
    CHECK(!item_stacks_match(of_wood(ITEM_BRANCHES, 1, OAK), of_wood(ITEM_BRANCHES, 1, BEECH)));
    CHECK(item_stacks_match(of_wood(ITEM_BRANCHES, 1, OAK), of_wood(ITEM_BRANCHES, 3, OAK)));

    Items items;
    REQUIRE(items_init(&items, 8, 8, 1));
    CHECK_EQ_INT(items_add(&items, 2, 2, 0, of_wood(ITEM_BRANCHES, 2, OAK)), 2);
    CHECK_EQ_INT(items_add(&items, 2, 2, 0, of_wood(ITEM_BRANCHES, 1, BEECH)), 1);
    CHECK(items_mixed(&items, 2, 2, 0)); /* Two groups. */
    ItemStack newest = items_get(&items, 2, 2, 0);
    CHECK_EQ_INT(newest.woods, BEECH);
    CHECK_EQ_INT(newest.count, 1);
    /* Twigs carry none, whatever they are given. */
    CHECK_EQ_INT(items_add(&items, 4, 4, 0, of_wood(ITEM_TWIGS, 2, OAK)), 2);
    CHECK_EQ_INT(items_get(&items, 4, 4, 0).woods, 0);
    /* Nature lays them with their species. */
    CHECK_EQ_INT(items_add_natural_wood(&items, 6, 6, 0, ITEM_BRANCHES, 2, ASH), 2);
    CHECK_EQ_INT(items_get(&items, 6, 6, 0).woods, ASH);
    /* The species counts in the hash. */
    Items other;
    REQUIRE(items_init(&other, 8, 8, 1));
    CHECK_EQ_INT(items_add(&other, 2, 2, 0, of_wood(ITEM_BRANCHES, 2, OAK)), 2);
    CHECK_EQ_INT(items_add(&other, 2, 2, 0, of_wood(ITEM_BRANCHES, 1, OAK)), 1);
    CHECK_EQ_INT(items_add(&other, 4, 4, 0, of_wood(ITEM_TWIGS, 2, 0)), 2);
    CHECK_EQ_INT(items_add_natural_wood(&other, 6, 6, 0, ITEM_BRANCHES, 2, ASH), 2);
    CHECK(items_hash(&items, 0) != items_hash(&other, 0));
    items_free(&other);
    items_free(&items);
}

/* Choosing by species: what may be taken, found and picked up. */
static void test_choosing_the_species(void)
{
    CHECK(item_woods_allowed(of_wood(ITEM_BRANCHES, 1, OAK), 0));
    CHECK(item_woods_allowed(of_wood(ITEM_BRANCHES, 1, OAK), OAK | ASH));
    CHECK(!item_woods_allowed(of_wood(ITEM_BRANCHES, 1, OAK | BEECH), OAK | ASH));
    CHECK(!item_woods_allowed(of_wood(ITEM_BRANCHES, 1, 0), OAK)); /* Unknown: not chosen. */
    CHECK(item_woods_allowed(of_wood(ITEM_TWIGS, 1, 0), OAK));     /* Not wooden: any. */

    Items items;
    REQUIRE(items_init(&items, 16, 16, 1));
    items_add(&items, 2, 2, 0, of_wood(ITEM_BRANCHES, 2, OAK));
    items_add(&items, 2, 2, 0, of_wood(ITEM_BRANCHES, 3, BEECH)); /* The newest. */
    ItemTypeMask branches = ITEM_TYPE_BIT(ITEM_BRANCHES);
    CHECK_EQ_INT(items_get_allowed_of_woods(&items, 2, 2, 0, branches, 0).woods, BEECH);
    ItemStack oak = items_get_allowed_of_woods(&items, 2, 2, 0, branches, OAK);
    CHECK_EQ_INT(oak.woods, OAK);
    CHECK_EQ_INT(oak.count, 2);
    CHECK_EQ_INT(items_get_allowed_of_woods(&items, 2, 2, 0, branches, ASH).type, ITEM_NONE);
    ItemChoice choice = ITEM_CHOICE_ANY;
    choice.woods = OAK;
    CHECK_EQ_INT(items_count_chosen(&items, 2, 2, 0, ITEM_BRANCHES, choice), 2);
    ItemStack load = {.type = ITEM_NONE};
    CHECK_EQ_INT(items_pick_up_chosen(&items, 2, 2, 0, ITEM_BRANCHES, 5, choice, &load), 2);
    CHECK_EQ_INT(load.woods, OAK);
    CHECK_EQ_INT(items_count_chosen(&items, 2, 2, 0, ITEM_BRANCHES, choice), 0);
    CHECK_EQ_INT(items_count_at(&items, 2, 2, 0, ITEM_BRANCHES), 3);

    /* A load given other species: every item of it. */
    items_load_set_woods(&items, &load, OAK | ASH);
    CHECK_EQ_INT(load.woods, OAK | ASH);
    CHECK_EQ_INT(load.count, 2);
    CHECK(items_check_load(&items, load));
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    items_drop_load(&items, &map, 5, 5, 0, &load);
    map_free(&map);
    CHECK_EQ_INT(items_get(&items, 5, 5, 0).woods, OAK | ASH);
    CHECK_EQ_INT(items_get(&items, 5, 5, 0).count, 2);
    CHECK_EQ_INT(items_check(&items).problems, 0);

    /* Counted by species: lying about, not forbidden. */
    items_add(&items, 7, 7, 0, of_wood(ITEM_BRANCHES, 4, 0));
    items_add(&items, 8, 8, 0, of_wood(ITEM_TWIGS, 4, 0));
    items_add(&items, 9, 9, 0, of_wood(ITEM_BRANCHES, 6, BIRCH));
    items_set_forbidden(&items, 9, 9, 0, true);
    int counts[WOOD_SPECIES_COUNT];
    CHECK_EQ_INT(wood_count_lying(&items, ITEM_BRANCHES, counts), 4);
    CHECK_EQ_INT(counts[PLANT_BEECH - PLANT_TREE_FIRST], 3);
    CHECK_EQ_INT(counts[PLANT_OAK - PLANT_TREE_FIRST], 2);
    CHECK_EQ_INT(counts[PLANT_ASH - PLANT_TREE_FIRST], 2);
    CHECK_EQ_INT(counts[PLANT_BIRCH - PLANT_TREE_FIRST], 0);
    items_free(&items);
}

/* The structures whose wood may be chosen: those built of branches. */
static void test_wooden_structure_types(void)
{
    CHECK_EQ_INT(structure_wooden_material(STRUCTURE_WALL), ITEM_BRANCHES);
    CHECK_EQ_INT(structure_wooden_material(STRUCTURE_PROP), ITEM_BRANCHES);
    CHECK_EQ_INT(structure_wooden_material(STRUCTURE_GRASS_BED), ITEM_NONE);
    CHECK_EQ_INT(structure_wooden_material(STRUCTURE_GROUND_BEDDING), ITEM_NONE);
    CHECK_EQ_INT(structure_wooden_material(STRUCTURE_TYPE_COUNT), ITEM_NONE);
    CHECK_EQ_INT(structure_wood_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_STANDARD, 0),
                 structure_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_STANDARD));
    CHECK(structure_wood_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_STANDARD, ASH) >
          structure_wood_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_STANDARD, BIRCH));
    CHECK_EQ_INT(structure_wood_max_hit_points(STRUCTURE_NONE, ITEM_QUALITY_STANDARD, ASH), 0);
}

/* Weight, wear and fuel follow the species. */
static void test_effects(void)
{
    float plain = item_stack_mass(of_wood(ITEM_WOOD, 2, 0));
    CHECK_NEAR(item_stack_mass(of_wood(ITEM_WOOD, 2, OAK)), plain * wood_def(PLANT_OAK)->weight,
               1e-4);
    CHECK(item_stack_mass(of_wood(ITEM_WOOD, 2, CHESTNUT)) < plain);
    /* Tough wood wears slower. */
    CHECK(tool_wear_ticks(of_wood(ITEM_DIGGING_STICK, 1, ASH)) > TOOL_WEAR_TICKS);
    CHECK(tool_wear_ticks(of_wood(ITEM_DIGGING_STICK, 1, BIRCH)) < TOOL_WEAR_TICKS);
    CHECK_EQ_INT(tool_wear_ticks(of_wood(ITEM_DIGGING_STICK, 1, 0)), TOOL_WEAR_TICKS);
    CHECK_EQ_INT(tool_wear_ticks(of_wood(ITEM_HAND_AXE, 1, 0)), TOOL_WEAR_TICKS);
    /* Beech burns longest. */
    float branch = hearth_fuel_value(ITEM_BRANCHES);
    CHECK_NEAR(hearth_stack_fuel_value(of_wood(ITEM_BRANCHES, 1, 0)), branch, 1e-6);
    CHECK(hearth_stack_fuel_value(of_wood(ITEM_BRANCHES, 1, BEECH)) >
          hearth_stack_fuel_value(of_wood(ITEM_BRANCHES, 1, BIRCH)));
    CHECK_NEAR(hearth_stack_fuel_value(of_wood(ITEM_TWIGS, 1, 0)), hearth_fuel_value(ITEM_TWIGS),
               1e-6);
}

/* What is made of wood keeps its species; an ash shaft is better, a chestnut
 * one worse, within the material's cap; hardening keeps both. */
static void test_crafts_keep_the_species(void)
{
    ItemStack out[RECIPE_PRODUCT_MAX];
    ItemStack plain_out[RECIPE_PRODUCT_MAX];
    ItemStack ash = of_wood(ITEM_BRANCHES, 2, ASH);
    ItemStack plain = of_wood(ITEM_BRANCHES, 2, 0);
    double level = 60.0;
    REQUIRE(recipe_craft(RECIPE_THRUSTING_SPEAR, plain, level, NULL, plain_out) == 1);
    REQUIRE(recipe_craft(RECIPE_THRUSTING_SPEAR, ash, level, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_THRUSTING_SPEAR);
    CHECK_EQ_INT(out[0].woods, ASH);
    CHECK_EQ_INT(plain_out[0].woods, 0);
    int cap = recipe_quality_cap(RECIPE_THRUSTING_SPEAR, ash);
    CHECK(out[0].quality > plain_out[0].quality || out[0].quality == cap);
    CHECK(out[0].quality <= cap);
    REQUIRE(recipe_craft(RECIPE_THRUSTING_SPEAR, of_wood(ITEM_BRANCHES, 2, CHESTNUT), level, NULL,
                         out) == 1);
    CHECK_EQ_INT(out[0].quality, plain_out[0].quality - 5);
    /* Hardened: same species, same quality. */
    ItemStack spear = of_wood(ITEM_THRUSTING_SPEAR, 1, ASH | OAK);
    spear.quality = 47;
    REQUIRE(recipe_craft(RECIPE_HARDEN_THRUSTING_SPEAR, spear, level, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_THRUSTING_SPEAR_HARDENED);
    CHECK_EQ_INT(out[0].woods, ASH | OAK);
    CHECK_EQ_INT(out[0].quality, 47);
    /* Stone products carry none. */
    ItemStack flint = item_stack_pristine(ITEM_FLINT, 1);
    REQUIRE(recipe_craft(RECIPE_FLAKES, flint, level, NULL, out) >= 1);
    CHECK_EQ_INT(out[0].woods, 0);
}

/* Names: the species in the item's name, listed with "and" (each language
 * its own order); the plain name without any. */
static void test_names(void)
{
    static const char ENGLISH[] = "item.branches = Branches\n"
                                  "item.branches.of_wood = {0} branches\n"
                                  "item.thrusting_spear.of_wood = {0} thrusting spear\n"
                                  "item.flint = Flint\n"
                                  "wood.oak = oak\nwood.ash = ash\nwood.beech = beech\n"
                                  "wood.list.two = {0} and {1}\nwood.list.more = {0}, {1}\n";
    static const char FRENCH[] = "item.branches.of_wood = Branches de {0}\n"
                                 "item.thrusting_spear.of_wood = Épieu en {0}\n"
                                 "wood.oak = chêne\nwood.ash = frêne\nwood.beech = hêtre\n"
                                 "wood.list.two = {0} et {1}\nwood.list.more = {0}, {1}\n";
    I18nCatalog *english = i18n_catalog_create();
    I18nCatalog *french = i18n_catalog_create();
    REQUIRE(english != NULL && french != NULL);
    REQUIRE(i18n_catalog_parse(english, ENGLISH, strlen(ENGLISH), "en"));
    REQUIRE(i18n_catalog_parse(french, FRENCH, strlen(FRENCH), "fr"));
    char name[128];
    i18n_set_catalogs(english, english);
    wood_item_name(ITEM_BRANCHES, OAK, name, sizeof(name));
    CHECK_STR_EQ(name, "Oak branches");
    wood_item_name(ITEM_BRANCHES, 0, name, sizeof(name));
    CHECK_STR_EQ(name, "Branches");
    wood_item_name(ITEM_FLINT, OAK, name, sizeof(name));
    CHECK_STR_EQ(name, "Flint");
    wood_item_name(ITEM_THRUSTING_SPEAR, OAK | ASH, name, sizeof(name));
    CHECK_STR_EQ(name, "Oak and ash thrusting spear");
    wood_list_name(OAK | BEECH | ASH, name, sizeof(name));
    CHECK_STR_EQ(name, "oak, beech and ash");
    wood_list_name(0, name, sizeof(name));
    CHECK_STR_EQ(name, "");
    i18n_set_catalogs(french, english);
    wood_item_name(ITEM_BRANCHES, BEECH, name, sizeof(name));
    CHECK_STR_EQ(name, "Branches de hêtre");
    wood_item_name(ITEM_THRUSTING_SPEAR, OAK | ASH, name, sizeof(name));
    CHECK_STR_EQ(name, "Épieu en chêne et frêne");
    i18n_set_catalogs(NULL, NULL);
    i18n_catalog_destroy(french);
    i18n_catalog_destroy(english);
}

int main(void)
{
    RUN_TEST(test_species);
    RUN_TEST(test_mixes_take_the_mean);
    RUN_TEST(test_items_keep_their_species_apart);
    RUN_TEST(test_effects);
    RUN_TEST(test_crafts_keep_the_species);
    RUN_TEST(test_names);
    RUN_TEST(test_choosing_the_species);
    RUN_TEST(test_wooden_structure_types);
    return test_summary();
}
