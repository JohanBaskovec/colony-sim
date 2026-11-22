/* Carrying by hand: places, what they take, arrangements and capacities
 * (docs/CARRYING.md). */
#include "core/carry.h"

#include "test_framework.h"

static CarryBody standard_body(void)
{
    return (CarryBody){.weight = 40.0f, .arm = {1.0f, 1.0f}};
}

static int capacity(ItemType type)
{
    CarryBody body = standard_body();
    return carry_capacity(&body, NULL, 0, type);
}

/* The load of an arrangement in a place. */
static int load_in(const CarryArrangement *arrangement, CarryPlace place)
{
    int count = 0;
    for (int i = 0; i < arrangement->entry_count; i++) {
        const CarryEntry *entry = &arrangement->entries[i];
        if (entry->place == place && !entry->kept) {
            count += entry->count;
        }
    }
    return count;
}

static int kept_in(const CarryArrangement *arrangement, CarryPlace place, ItemType type)
{
    int count = 0;
    for (int i = 0; i < arrangement->entry_count; i++) {
        const CarryEntry *entry = &arrangement->entries[i];
        if (entry->place == place && entry->kept && entry->type == type) {
            count += entry->count;
        }
    }
    return count;
}

/* docs/CARRYING.md §5: a settler of strength 10, both arms whole, nothing kept. */
static void test_the_table_of_capacities(void)
{
    CHECK_EQ_INT(capacity(ITEM_WOOD), 5);
    CHECK_EQ_INT(capacity(ITEM_STONE_BLOCK), 1);
    CHECK_EQ_INT(capacity(ITEM_COBBLE), 8);
    CHECK_EQ_INT(capacity(ITEM_FLINT), 8);
    CHECK_EQ_INT(capacity(ITEM_BRANCHES), 9);
    CHECK_EQ_INT(capacity(ITEM_TWIGS), 30);
    CHECK_EQ_INT(capacity(ITEM_GRASS), 6);
    CHECK_EQ_INT(capacity(ITEM_BRACKEN), 6);
    CHECK_EQ_INT(capacity(ITEM_REEDS), 7);
    CHECK_EQ_INT(capacity(ITEM_HEATHER), 4);
    CHECK_EQ_INT(capacity(ITEM_MOSS), 3);
    CHECK_EQ_INT(capacity(ITEM_NETTLES), 40);
    CHECK_EQ_INT(capacity(ITEM_WILD_GARLIC), 40);
    CHECK_EQ_INT(capacity(ITEM_BLACKBERRIES), 2);
    CHECK_EQ_INT(capacity(ITEM_BILBERRIES), 4);
    CHECK_EQ_INT(capacity(ITEM_HAZELNUTS), 5);
    CHECK_EQ_INT(capacity(ITEM_WILD_STRAWBERRIES), 5);
    CHECK_EQ_INT(capacity(ITEM_ROSE_HIPS), 5);
    CHECK_EQ_INT(capacity(ITEM_CHESTNUTS), 4);
    CHECK_EQ_INT(capacity(ITEM_APPLES), 17);
    CHECK_EQ_INT(capacity(ITEM_PEARS), 17);
    CHECK_EQ_INT(capacity(ITEM_MUSHROOMS), 24);
    CHECK_EQ_INT(capacity(ITEM_WILD_CARROTS), 12);
    CHECK_EQ_INT(capacity(ITEM_FLAKE), 60);
    CHECK_EQ_INT(capacity(ITEM_CHOPPER), 6);
    CHECK_EQ_INT(capacity(ITEM_HAND_AXE), 8);
    CHECK_EQ_INT(capacity(ITEM_DIGGING_STICK), 9);
    CHECK_EQ_INT(capacity(ITEM_THROWING_SPEAR), 9);
    CHECK_EQ_INT(capacity(ITEM_THRUSTING_SPEAR), 9);
    CHECK_EQ_INT(capacity(ITEM_HARE_CARCASS), 3);
    CHECK_EQ_INT(capacity(ITEM_FOX_CARCASS), 3);
    CHECK_EQ_INT(capacity(ITEM_ROE_DEER_CARCASS), 1);
    CHECK_EQ_INT(capacity(ITEM_WOLF_CARCASS), 1);
    CHECK_EQ_INT(capacity(ITEM_BOAR_CARCASS), 1);
    CHECK_EQ_INT(capacity(ITEM_RED_DEER_CARCASS), 1);
    CHECK_EQ_INT(capacity(ITEM_NONE), 0);
}

static void test_how_loads_are_carried(void)
{
    CarryBody body = standard_body();
    CarryArrangement arrangement;

    /* Logs: one under each arm, three on the shoulders, a hand steadying them. */
    CHECK_EQ_INT(carry_arrange(&body, NULL, 0, ITEM_WOOD, 10, &arrangement), 5);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_UNDER_RIGHT_ARM), 1);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_UNDER_LEFT_ARM), 1);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_SHOULDERS), 3);
    CHECK(arrangement.steadying == CARRY_RIGHT_HAND || arrangement.steadying == CARRY_LEFT_HAND);
    CHECK(arrangement.hold == CARRY_HOLD_NONE);

    /* Cobbles in the arms. */
    CHECK_EQ_INT(carry_arrange(&body, NULL, 0, ITEM_COBBLE, 20, &arrangement), 8);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_BOTH_HANDS), 8);
    CHECK(arrangement.hold == CARRY_HOLD_ARMFUL);

    /* Berries in cupped hands. */
    CHECK_EQ_INT(carry_arrange(&body, NULL, 0, ITEM_BLACKBERRIES, 20, &arrangement), 2);
    CHECK(arrangement.hold == CARRY_HOLD_CUPPED);

    /* No more than asked for, in as few places as can be. */
    CHECK_EQ_INT(carry_arrange(&body, NULL, 0, ITEM_WOOD, 1, &arrangement), 1);
    CHECK_EQ_INT(arrangement.entry_count, 1);
    CHECK_EQ_INT(carry_arrange(&body, NULL, 0, ITEM_THRUSTING_SPEAR, 2, &arrangement), 2);
    CHECK_EQ_INT(arrangement.entry_count, 1);
    CHECK_EQ_INT(arrangement.load, 2);

    /* A wolf on the shoulders, held by its legs. */
    CHECK_EQ_INT(carry_arrange(&body, NULL, 0, ITEM_WOLF_CARCASS, 1, &arrangement), 1);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_SHOULDERS), 1);
    CHECK(arrangement.hold == CARRY_HOLD_SHOULDERS);
    /* A boar, too heavy for any place, still on the shoulders (slowed down); a
     * red deer dragged. */
    CHECK_EQ_INT(carry_arrange(&body, NULL, 0, ITEM_BOAR_CARCASS, 1, &arrangement), 1);
    CHECK(arrangement.hold == CARRY_HOLD_SHOULDERS);
    CHECK_EQ_INT(carry_arrange(&body, NULL, 0, ITEM_RED_DEER_CARCASS, 1, &arrangement), 1);
    CHECK(arrangement.hold == CARRY_HOLD_DRAGGED);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_BOTH_HANDS), 1);
}

static void test_places(void)
{
    CarryBody body = standard_body();
    CHECK(carry_place_takes(CARRY_UNDER_RIGHT_ARM, ITEM_THRUSTING_SPEAR));
    CHECK(carry_place_takes(CARRY_SHOULDERS, ITEM_ROE_DEER_CARCASS));
    CHECK(carry_place_takes(CARRY_SHOULDERS, ITEM_STONE_BLOCK)); /* A heavy rigid item. */
    CHECK(!carry_place_takes(CARRY_SHOULDERS, ITEM_COBBLE));
    CHECK(!carry_place_takes(CARRY_UNDER_LEFT_ARM, ITEM_HAND_AXE));
    CHECK(!carry_place_takes(CARRY_UNDER_LEFT_ARM, ITEM_BLACKBERRIES));
    CHECK(!carry_place_takes(CARRY_SLUNG, ITEM_THRUSTING_SPEAR)); /* No strap. */
    CHECK(!carry_place_takes(CARRY_RIGHT_HAND, ITEM_NONE));
    CHECK(!carry_place_takes(CARRY_PLACE_COUNT, ITEM_COBBLE));
    CHECK_NEAR(carry_place_weight_limit(&body, CARRY_RIGHT_HAND), 10.0, 1e-4);
    CHECK_NEAR(carry_place_weight_limit(&body, CARRY_BOTH_HANDS), 30.0, 1e-4);
    CHECK_NEAR(carry_place_weight_limit(&body, CARRY_UNDER_LEFT_ARM), 8.0, 1e-4);
    CHECK_NEAR(carry_place_weight_limit(&body, CARRY_SHOULDERS), 40.0, 1e-4);
    /* A hurt arm bears less, a useless one nothing. */
    body.arm[CARRY_LEFT] = 0.5f;
    CHECK_NEAR(carry_place_weight_limit(&body, CARRY_LEFT_HAND), 5.0, 1e-4);
    CHECK_NEAR(carry_place_weight_limit(&body, CARRY_BOTH_HANDS), 15.0, 1e-4);
    body.arm[CARRY_LEFT] = 0.1f;
    CHECK(!carry_place_usable(&body, CARRY_LEFT_HAND));
    CHECK(!carry_place_usable(&body, CARRY_UNDER_LEFT_ARM));
    CHECK(!carry_place_usable(&body, CARRY_BOTH_HANDS));
    CHECK(carry_place_usable(&body, CARRY_RIGHT_HAND));
    CHECK(carry_place_usable(&body, CARRY_SHOULDERS));
    CHECK_NEAR(carry_place_weight_limit(&body, CARRY_LEFT_HAND), 0.0, 1e-6);
}

static void test_a_lost_arm(void)
{
    CarryBody body = {.weight = 40.0f, .arm = {0.0f, 1.0f}}; /* No right arm. */
    CarryArrangement arrangement;
    /* No armful: one cobble in the hand left. */
    CHECK_EQ_INT(carry_capacity(&body, NULL, 0, ITEM_COBBLE), 1);
    /* Loose items flat on the palm only. */
    CHECK_EQ_INT(carry_capacity(&body, NULL, 0, ITEM_HAZELNUTS), 1);
    CHECK_EQ_INT(carry_capacity(&body, NULL, 0, ITEM_BLACKBERRIES), 1); /* Over the palm: alone. */
    /* Spears: two in the hand, two under the arm, three on the shoulders but
     * then the only hand steadies them: four at most beside them. */
    int spears = carry_arrange(&body, NULL, 0, ITEM_THRUSTING_SPEAR, 20, &arrangement);
    CHECK_EQ_INT(spears, 5);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_RIGHT_HAND), 0);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_UNDER_RIGHT_ARM), 0);
    /* A roe deer needs both hands on its legs: dragged. */
    CHECK_EQ_INT(carry_arrange(&body, NULL, 0, ITEM_ROE_DEER_CARCASS, 1, &arrangement), 1);
    CHECK(arrangement.hold == CARRY_HOLD_DRAGGED);
    /* A weak arm carries less than a whole one. */
    CarryBody weak = {.weight = 40.0f, .arm = {0.25f, 0.25f}};
    CHECK(carry_capacity(&weak, NULL, 0, ITEM_COBBLE) < capacity(ITEM_COBBLE));
    CHECK(carry_capacity(&weak, NULL, 0, ITEM_WOOD) < capacity(ITEM_WOOD));
}

static void test_kept_items(void)
{
    CarryBody body = standard_body();
    CarryArrangement arrangement;
    /* A spear kept goes under an arm, leaving the hands free. */
    ItemType spear = ITEM_THRUSTING_SPEAR;
    CHECK_EQ_INT(carry_arrange(&body, &spear, 1, ITEM_NONE, 0, &arrangement), 0);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_UNDER_RIGHT_ARM, spear), 1);
    CHECK_EQ_INT(arrangement.left_out, 0);
    /* A hand axe kept takes a hand. */
    ItemType axe = ITEM_HAND_AXE;
    carry_arrange(&body, &axe, 1, ITEM_NONE, 0, &arrangement);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_RIGHT_HAND, axe), 1);
    /* Beside an armful of cobbles, it goes into the armful: a cobble less. */
    CHECK_EQ_INT(carry_arrange(&body, &axe, 1, ITEM_COBBLE, 20, &arrangement), 7);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_BOTH_HANDS, axe), 1);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_BOTH_HANDS), 7);
    /* Beside berries, it stays in a hand and the berries go in the other. */
    CHECK_EQ_INT(carry_arrange(&body, &axe, 1, ITEM_HAZELNUTS, 20, &arrangement), 1);
    /* Kept spear and logs: the spear under an arm, the logs around it. */
    CHECK_EQ_INT(carry_arrange(&body, &spear, 1, ITEM_WOOD, 20, &arrangement), 5);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_UNDER_RIGHT_ARM, spear) +
                     kept_in(&arrangement, CARRY_UNDER_LEFT_ARM, spear) +
                     kept_in(&arrangement, CARRY_SHOULDERS, spear),
                 1);
    /* The kept items' weight counts, even where they find no place: a load is
     * then a single item. */
    CarryBody weak = {.weight = 10.0f, .arm = {1.0f, 1.0f}};
    ItemType deer = ITEM_ROE_DEER_CARCASS;
    CHECK_EQ_INT(carry_arrange(&weak, &deer, 1, ITEM_TWIGS, 20, &arrangement), 1);
    CHECK_EQ_INT((int)arrangement.left_out, 1);
    /* Short tools fit in the hands only: three are one too many. */
    ItemType tools[] = {ITEM_HAND_AXE, ITEM_CHOPPER, ITEM_COBBLE};
    carry_arrange(&body, tools, 3, ITEM_NONE, 0, &arrangement);
    CHECK(arrangement.left_out != 0);
    /* The same, deterministic. */
    CarryArrangement again;
    carry_arrange(&body, tools, 3, ITEM_NONE, 0, &again);
    CHECK_EQ_INT((int)again.left_out, (int)arrangement.left_out);
    CHECK_EQ_INT(again.entry_count, arrangement.entry_count);
}

static void test_items_kept_in_hand(void)
{
    CarryBody body = standard_body();
    CarryArrangement arrangement;
    /* A spear kept in hand: in the right hand, not under an arm. */
    ItemType spear = ITEM_THRUSTING_SPEAR;
    carry_arrange_held(&body, &spear, 1, 1u, ITEM_NONE, 0, &arrangement);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_RIGHT_HAND, spear), 1);
    CHECK_EQ_INT(carry_hands_taken(&arrangement, &spear, 1, 1u), 1);
    CHECK_EQ_INT(carry_hands_taken(&arrangement, &spear, 1, 0u), 0);
    /* Beside logs: no armful (it needs both hands), the spear stays in hand. */
    int logs = carry_arrange_held(&body, &spear, 1, 1u, ITEM_WOOD, 20, &arrangement);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_RIGHT_HAND, spear), 1);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_BOTH_HANDS), 0);
    CHECK(logs > 0 && logs <= carry_capacity(&body, &spear, 1, ITEM_WOOD));
    CHECK_EQ_INT(carry_capacity_held(&body, &spear, 1, 1u, ITEM_WOOD), logs);
    /* The better arm's hand: the left one when the right is weak. */
    CarryBody hurt = {.weight = 40.0f, .arm = {0.5f, 1.0f}};
    carry_arrange_held(&hurt, &spear, 1, 1u, ITEM_NONE, 0, &arrangement);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_LEFT_HAND, spear), 1);
    /* Two in hand take both hands; with one arm, the second finds none. */
    ItemType tools[] = {ITEM_HAND_AXE, ITEM_CHOPPER};
    carry_arrange_held(&body, tools, 2, 3u, ITEM_NONE, 0, &arrangement);
    CHECK_EQ_INT(arrangement.left_out, 0);
    CHECK_EQ_INT(carry_hands_taken(&arrangement, tools, 2, 3u), 2);
    CarryBody one_arm = {.weight = 40.0f, .arm = {0.0f, 1.0f}};
    carry_arrange_held(&one_arm, tools, 2, 3u, ITEM_NONE, 0, &arrangement);
    CHECK(arrangement.left_out != 0);
    CHECK_EQ_INT(carry_hands_taken(&arrangement, tools, 2, 3u), 1);
}

/* A weapon wielded with both hands (docs/COMBAT.md §7): alone in both
 * hands, the places under the arms left free (no armful), no hand for
 * anything else nor to steady the shoulders. */
static void test_gripped_with_both_hands(void)
{
    CarryBody body = standard_body();
    CarryArrangement arrangement;
    ItemType kept[] = {ITEM_THRUSTING_SPEAR, ITEM_THROWING_SPEAR, ITEM_HAND_AXE};
    carry_arrange_gripped(&body, kept, 3, 0u, 1u, ITEM_NONE, 0, &arrangement);
    CHECK_EQ_INT(arrangement.hold, CARRY_HOLD_GRIP);
    CHECK_EQ_INT(arrangement.kept_place[0], CARRY_BOTH_HANDS);
    CHECK_EQ_INT(arrangement.kept_place[1], CARRY_UNDER_RIGHT_ARM); /* Still free. */
    CHECK(arrangement.kept_place[2] != CARRY_RIGHT_HAND &&
          arrangement.kept_place[2] != CARRY_LEFT_HAND);
    CHECK_EQ_INT(arrangement.kept_place[3], -1); /* No such item. */
    CHECK_EQ_INT(carry_hands_taken(&arrangement, kept, 3, 1u), 2);
    /* A load goes under the arms or on the shoulders, never in the hands. */
    int branches = carry_arrange_gripped(&body, kept, 1, 0u, 1u, ITEM_BRANCHES, 20, &arrangement);
    CHECK(branches > 0);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_BOTH_HANDS), 0);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_RIGHT_HAND) + load_in(&arrangement, CARRY_LEFT_HAND),
                 0);
    CHECK_EQ_INT(arrangement.steadying, -1);
    CHECK_EQ_INT(carry_capacity_gripped(&body, kept, 1, 0u, 1u, ITEM_BRANCHES), branches);
    /* Cobbles take an armful or a hand: none left. */
    CHECK_EQ_INT(carry_arrange_gripped(&body, kept, 1, 0u, 1u, ITEM_COBBLE, 5, &arrangement), 1);
    CHECK(arrangement.hold == CARRY_HOLD_DRAGGED || arrangement.left_out == 0);
    /* Something else kept in hand finds no hand. */
    ItemType two[] = {ITEM_THRUSTING_SPEAR, ITEM_COBBLE};
    carry_arrange_gripped(&body, two, 2, 2u, 1u, ITEM_NONE, 0, &arrangement);
    CHECK_EQ_INT(arrangement.left_out, 2u);
    /* With one arm, no grip. */
    CarryBody one_arm = {.weight = 40.0f, .arm = {0.0f, 1.0f}};
    carry_arrange_gripped(&one_arm, kept, 1, 0u, 1u, ITEM_NONE, 0, &arrangement);
    CHECK_EQ_INT(arrangement.left_out, 1u);
}

/* Working (docs/CARRYING.md §6.3): the tools in use in hand, the hands the
 * action takes kept empty, the rest elsewhere or set aside (left out). */
static void test_working_hands(void)
{
    CarryBody body = standard_body();
    CarryArrangement arrangement;
    unsigned right = 1u << CARRY_RIGHT_HAND, left = 1u << CARRY_LEFT_HAND;
    /* Nothing kept: gathering takes both hands, eating the better arm's. */
    carry_arrange_working(&body, NULL, 0, 0u, 0u, 0u, 2, &arrangement);
    CHECK_EQ_UINT(arrangement.working, right | left);
    CHECK_EQ_INT(arrangement.load, 0);
    carry_arrange_working(&body, NULL, 0, 0u, 0u, 0u, 1, &arrangement);
    CHECK_EQ_UINT(arrangement.working, right);
    CarryBody hurt = {.weight = 40.0f, .arm = {0.5f, 1.0f}};
    carry_arrange_working(&hurt, NULL, 0, 0u, 0u, 0u, 1, &arrangement);
    CHECK_EQ_UINT(arrangement.working, left);
    /* The tool in use in the better hand, the other hand kept for the work;
     * a spear under an arm; another short tool has no place left. */
    ItemType kept[] = {ITEM_FLAKE, ITEM_THRUSTING_SPEAR, ITEM_HAND_AXE};
    carry_arrange_working(&body, kept, 3, 0u, 0u, 4u, 2, &arrangement);
    CHECK_EQ_INT(arrangement.kept_place[2], CARRY_RIGHT_HAND);
    CHECK_EQ_UINT(arrangement.working, left);
    CHECK_EQ_INT(arrangement.kept_place[1], CARRY_UNDER_RIGHT_ARM);
    CHECK_EQ_UINT(arrangement.left_out, 1u);
    /* Without the work's hands, both short tools have a hand. */
    carry_arrange_working(&body, kept, 3, 0u, 0u, 4u, 0, &arrangement);
    CHECK_EQ_UINT(arrangement.working, 0u);
    CHECK_EQ_UINT(arrangement.left_out, 0u);
    CHECK_EQ_INT(arrangement.kept_place[2], CARRY_RIGHT_HAND);
    /* Gathering, no tool: both hands empty, the places under the arms
     * stay (no armful), the short ones are set aside. */
    carry_arrange_working(&body, kept, 3, 0u, 0u, 0u, 2, &arrangement);
    CHECK_EQ_UINT(arrangement.working, right | left);
    CHECK_EQ_INT(arrangement.kept_place[1], CARRY_UNDER_RIGHT_ARM);
    CHECK_EQ_UINT(arrangement.left_out, 5u);
    /* What is kept in hand keeps its hand: eating takes the other one... */
    ItemType spear = ITEM_THRUSTING_SPEAR;
    carry_arrange_working(&body, &spear, 1, 1u, 0u, 0u, 1, &arrangement);
    CHECK_EQ_INT(arrangement.kept_place[0], CARRY_RIGHT_HAND);
    CHECK_EQ_UINT(arrangement.working, left);
    /* ...and the work lacks a hand rather than set it aside. */
    carry_arrange_working(&body, &spear, 1, 1u, 0u, 0u, 2, &arrangement);
    CHECK_EQ_INT(arrangement.kept_place[0], CARRY_RIGHT_HAND);
    CHECK_EQ_UINT(arrangement.left_out, 0u);
    CHECK_EQ_UINT(arrangement.working, left);
    /* The ordinary arrangements keep no hand. */
    carry_arrange(&body, kept, 3, ITEM_NONE, 0, &arrangement);
    CHECK_EQ_UINT(arrangement.working, 0u);
}

static void test_strength(void)
{
    CarryBody strong = {.weight = 60.0f, .arm = {1.0f, 1.0f}};
    CarryBody weak = {.weight = 22.0f, .arm = {1.0f, 1.0f}};
    CHECK(carry_capacity(&strong, NULL, 0, ITEM_WOOD) > capacity(ITEM_WOOD));
    CHECK(carry_capacity(&weak, NULL, 0, ITEM_WOOD) < capacity(ITEM_WOOD));
    CHECK(carry_capacity(&strong, NULL, 0, ITEM_COBBLE) >= capacity(ITEM_COBBLE));
    /* A strong settler carries a boar on the shoulders without dragging it. */
    CarryArrangement arrangement;
    CHECK_EQ_INT(carry_arrange(&strong, NULL, 0, ITEM_BOAR_CARCASS, 1, &arrangement), 1);
    CHECK(arrangement.hold == CARRY_HOLD_SHOULDERS);
    /* Nothing can be carried by a body that bears nothing, but a single item
     * is still dragged. */
    CarryBody none = {.weight = 0.0f, .arm = {0.0f, 0.0f}};
    CHECK_EQ_INT(carry_arrange(&none, NULL, 0, ITEM_COBBLE, 5, &arrangement), 1);
    CHECK(arrangement.hold == CARRY_HOLD_DRAGGED);
}

/* A container held in a hand (docs/CONTAINERS.md §7.2): a place of its own for small items,
 * its room its litres, within what its hand bears; the hand holds nothing else. Long or heavy
 * items do not go in, nor another container. */
static void test_containers(void)
{
    CarryBody body = standard_body();
    CHECK(carry_is_container(ITEM_CATTAIL_BASKET) && carry_is_container(ITEM_BARK_TRAY));
    CHECK(!carry_is_container(ITEM_FLAKE));
    CHECK(carry_container_takes(ITEM_BLACKBERRIES) && carry_container_takes(ITEM_HAZELNUTS));
    CHECK(carry_container_takes(ITEM_TWIGS) && carry_container_takes(ITEM_NETTLE_CORD));
    CHECK(carry_container_takes(ITEM_NETTLE_FIBRE) && carry_container_takes(ITEM_COBBLE));
    CHECK(!carry_container_takes(ITEM_BRANCHES));     /* Long. */
    CHECK(!carry_container_takes(ITEM_STONE_BLOCK));  /* Heavy. */
    CHECK(!carry_container_takes(ITEM_BARK_TRAY));    /* A container. */
    CHECK(!carry_container_takes(ITEM_HARE_CARCASS)); /* Long and limp. */
    CHECK(carry_place_takes(CARRY_IN_CONTAINER, ITEM_BLACKBERRIES));
    CHECK(!carry_place_usable(&body, CARRY_IN_CONTAINER)); /* Not a place of the body. */

    /* Blackberries: two in cupped hands, the basket's ten litres (20 bulk) beside it. */
    CHECK_EQ_INT(capacity(ITEM_BLACKBERRIES), 2);
    ItemType basket[1] = {ITEM_CATTAIL_BASKET};
    CarryArrangement arrangement;
    CHECK_EQ_INT(carry_arrange(&body, basket, 1, ITEM_BLACKBERRIES, 100, &arrangement), 20);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_IN_CONTAINER), 20);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_RIGHT_HAND, ITEM_CATTAIL_BASKET), 1);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_RIGHT_HAND), 0); /* The basket's hand: nothing else. */
    ItemType tray[1] = {ITEM_BARK_TRAY};
    CHECK_EQ_INT(carry_capacity(&body, tray, 1, ITEM_BLACKBERRIES), 4); /* Two litres. */
    /* A heavy basket is held in both arms, against the chest: cobbles fill its room (20 bulk:
     * 13 of them), within what both hands bear; hurt arms bear less of them. */
    CHECK_EQ_INT(carry_arrange(&body, basket, 1, ITEM_COBBLE, 100, &arrangement), 13);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_BOTH_HANDS, ITEM_CATTAIL_BASKET), 1);
    CarryBody hurt = {.weight = 40.0f, .arm = {0.4f, 0.4f}};
    int weak = carry_capacity(&hurt, basket, 1, ITEM_COBBLE);
    CHECK(weak > 0 && weak < 13);
    /* Long things: the basket only takes a hand. */
    CHECK(carry_capacity(&body, basket, 1, ITEM_BRANCHES) <= capacity(ITEM_BRANCHES));
}

/* A pack basket rides on the back (docs/CONTAINERS.md §7.2): forty litres of small things,
 * the hands left free (berries cupped beside it, a basket in a hand); nothing on the shoulders
 * beside it. */
static void test_the_back(void)
{
    CarryBody body = standard_body();
    CHECK(carry_place_usable(&body, CARRY_BACK));
    CHECK(carry_place_takes(CARRY_BACK, ITEM_PACK_BASKET));
    CHECK(!carry_place_takes(CARRY_BACK, ITEM_FRAME_BASKET) &&
          !carry_place_takes(CARRY_BACK, ITEM_WOOD));
    CHECK(carry_is_container(ITEM_FRAME_BASKET) && carry_is_container(ITEM_BARK_BOX));
    CHECK(carry_container_takes(ITEM_HAZELNUTS) && !carry_container_takes(ITEM_HAZEL_RODS));

    ItemType pack[1] = {ITEM_PACK_BASKET};
    CarryArrangement arrangement;
    int berries = carry_arrange(&body, pack, 1, ITEM_BLACKBERRIES, 500, &arrangement);
    CHECK_EQ_INT(kept_in(&arrangement, CARRY_BACK, ITEM_PACK_BASKET), 1);
    CHECK_EQ_INT(load_in(&arrangement, CARRY_IN_CONTAINER), 80); /* Forty litres. */
    CHECK_EQ_INT(load_in(&arrangement, CARRY_BOTH_HANDS), 2);    /* The hands still cup two. */
    CHECK_EQ_INT(berries, 82);
    ItemType both[2] = {ITEM_PACK_BASKET, ITEM_FRAME_BASKET};
    CHECK_EQ_INT(carry_capacity(&body, both, 2, ITEM_BLACKBERRIES), 80 + 40);
    ItemType box[1] = {ITEM_BARK_BOX};
    CHECK_EQ_INT(carry_capacity(&body, box, 1, ITEM_BLACKBERRIES), 6 + 0); /* Three litres. */

    /* A log goes on the shoulders: the pack basket moves off the back for it. */
    int logs = carry_arrange(&body, pack, 1, ITEM_WOOD, 10, &arrangement);
    CHECK(logs > 0);
    CHECK(kept_in(&arrangement, CARRY_BACK, ITEM_PACK_BASKET) == 0 ||
          load_in(&arrangement, CARRY_SHOULDERS) == 0);
}

int main(void)
{
    RUN_TEST(test_the_table_of_capacities);
    RUN_TEST(test_how_loads_are_carried);
    RUN_TEST(test_places);
    RUN_TEST(test_a_lost_arm);
    RUN_TEST(test_kept_items);
    RUN_TEST(test_items_kept_in_hand);
    RUN_TEST(test_gripped_with_both_hands);
    RUN_TEST(test_working_hands);
    RUN_TEST(test_strength);
    RUN_TEST(test_containers);
    RUN_TEST(test_the_back);
    return test_summary();
}
