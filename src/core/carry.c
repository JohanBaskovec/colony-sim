#include "core/carry.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* Room shares a little over 1 still fit (rounding). */
#define ROOM_EPSILON 1e-4f
#define WEIGHT_EPSILON 1e-4f

/* What a place holds during the search. */
typedef struct PlaceState {
    float used;   /* Share of its room, 0..1 (more for a single item over its room). */
    float weight; /* Kilograms. */
    int items;
    bool loose;      /* Holds loose items (both hands: cupped). */
    bool long_rigid; /* Holds long rigid items (the shoulders: they need steadying). */
    bool limp;       /* Holds a limp item (the shoulders: held by its legs when heavy). */
    bool grip;       /* Both hands: one item wielded with both. */
} PlaceState;

typedef struct Search {
    const CarryBody *body;
    PlaceState places[CARRY_PLACE_COUNT];
    unsigned load_places; /* Bit per place the load may use (the subset tried). */
    ItemType load;
    uint32_t in_hand; /* Bit i: kept item i is kept in a hand. */
    uint32_t grip;    /* Bit i: kept item i is held in both hands. */
    uint32_t work;    /* Bit i: kept item i is a tool in use (in a hand). */
    int work_hands;   /* Hands the work takes, those holding its tools included. */
    unsigned working; /* Bit per hand (CarryPlace) kept empty for the work. */
    int8_t kept_place[CARRY_KEPT_MAX];
} Search;

static float arm(const CarryBody *body, CarrySide side)
{
    float value = body->arm[side];
    return value >= CARRY_ARM_MIN ? value : 0.0f;
}

bool carry_place_usable(const CarryBody *body, CarryPlace place)
{
    switch (place) {
    case CARRY_RIGHT_HAND:
    case CARRY_UNDER_RIGHT_ARM: return arm(body, CARRY_RIGHT) > 0.0f;
    case CARRY_LEFT_HAND:
    case CARRY_UNDER_LEFT_ARM: return arm(body, CARRY_LEFT) > 0.0f;
    case CARRY_BOTH_HANDS: return arm(body, CARRY_RIGHT) > 0.0f && arm(body, CARRY_LEFT) > 0.0f;
    case CARRY_SHOULDERS:
    case CARRY_SLUNG: return body->weight > 0.0f;
    case CARRY_IN_CONTAINER: /* Not a place of the body: filled apart. */
    case CARRY_PLACE_COUNT: break;
    }
    return false;
}

float carry_place_weight_limit(const CarryBody *body, CarryPlace place)
{
    if (!carry_place_usable(body, place)) {
        return 0.0f;
    }
    float right = arm(body, CARRY_RIGHT);
    float left = arm(body, CARRY_LEFT);
    switch (place) {
    case CARRY_RIGHT_HAND: return CARRY_HAND_WEIGHT * body->weight * right;
    case CARRY_LEFT_HAND: return CARRY_HAND_WEIGHT * body->weight * left;
    case CARRY_BOTH_HANDS: return CARRY_BOTH_WEIGHT * body->weight * (right < left ? right : left);
    case CARRY_UNDER_RIGHT_ARM: return CARRY_UNDER_WEIGHT * body->weight * right;
    case CARRY_UNDER_LEFT_ARM: return CARRY_UNDER_WEIGHT * body->weight * left;
    case CARRY_SHOULDERS: return CARRY_SHOULDERS_WEIGHT * body->weight;
    case CARRY_SLUNG: return CARRY_SLUNG_WEIGHT * body->weight;
    case CARRY_IN_CONTAINER:
    case CARRY_PLACE_COUNT: break;
    }
    return 0.0f;
}

/* The share of a place's room one item takes, or a negative value when the
 * place does not take it. */
static float item_share(CarryPlace place, ItemType type)
{
    const ItemDef *def = item_def(type);
    if (def == NULL || type == ITEM_NONE) {
        return -1.0f;
    }
    bool long_item = def->length_cm >= ITEM_LONG_CM;
    bool long_rigid = long_item && def->form == ITEM_FORM_RIGID;
    switch (place) {
    case CARRY_RIGHT_HAND:
    case CARRY_LEFT_HAND:
        switch (def->form) {
        case ITEM_FORM_LOOSE: return def->bulk / CARRY_HAND_LOOSE_ROOM;
        case ITEM_FORM_LIMP: return 1.0f; /* One, by the legs. */
        case ITEM_FORM_RIGID:
            return long_rigid ? 1.0f / (float)CARRY_HAND_LONG : def->bulk / CARRY_HAND_ROOM;
        case ITEM_FORM_BUNDLE: return def->bulk / CARRY_HAND_ROOM;
        case ITEM_FORM_NONE:
        case ITEM_FORM_COUNT: break;
        }
        return -1.0f;
    case CARRY_BOTH_HANDS:
        switch (def->form) {
        case ITEM_FORM_LOOSE: return def->bulk / CARRY_CUPPED_ROOM;
        case ITEM_FORM_RIGID:
            return long_rigid ? 1.0f / (float)CARRY_ARMFUL_LONG
                              : def->bulk / CARRY_ARMFUL_PIECES_ROOM;
        case ITEM_FORM_BUNDLE:
        case ITEM_FORM_LIMP: return def->bulk / CARRY_ARMFUL_ROOM;
        case ITEM_FORM_NONE:
        case ITEM_FORM_COUNT: break;
        }
        return -1.0f;
    case CARRY_UNDER_RIGHT_ARM:
    case CARRY_UNDER_LEFT_ARM:
        if (long_rigid) {
            return 1.0f / (float)CARRY_UNDER_LONG;
        }
        return long_item && def->form == ITEM_FORM_BUNDLE ? def->bulk / CARRY_UNDER_ROOM : -1.0f;
    case CARRY_SHOULDERS:
        if (long_rigid) {
            return 1.0f / (float)CARRY_SHOULDERS_LONG;
        }
        if (long_item && def->form == ITEM_FORM_BUNDLE) {
            return def->bulk / CARRY_SHOULDERS_ROOM;
        }
        if (def->form == ITEM_FORM_LIMP ||
            (def->form == ITEM_FORM_RIGID && def->weight >= CARRY_SHOULDERS_RIGID_KG)) {
            return 1.0f;
        }
        return -1.0f;
    case CARRY_SLUNG: return def->strap ? 1.0f / (float)CARRY_SLUNG_COUNT : -1.0f;
    case CARRY_IN_CONTAINER:
    case CARRY_PLACE_COUNT: break;
    }
    return -1.0f;
}

bool carry_is_container(ItemType type)
{
    const ItemDef *def = item_def(type);
    return def != NULL && def->container_litres > 0.0f;
}

bool carry_container_takes(ItemType type)
{
    const ItemDef *def = item_def(type);
    return def != NULL && type != ITEM_NONE && def->form != ITEM_FORM_NONE &&
           def->length_cm < ITEM_LONG_CM && def->weight <= CARRY_CONTAINER_ITEM_KG &&
           !carry_is_container(type);
}

bool carry_place_takes(CarryPlace place, ItemType type)
{
    if (place == CARRY_IN_CONTAINER) {
        return carry_container_takes(type);
    }
    return (unsigned)place < CARRY_PLACE_COUNT && item_share(place, type) > 0.0f;
}

/* How many more items of `type` fit in a place by its room and weight (the
 * place's own limits only). One item alone always fits a place that takes
 * it and bears its weight, but for loose items. */
static int place_room_for(const Search *search, CarryPlace place, ItemType type)
{
    if (!carry_place_usable(search->body, place) || (search->working & (1u << place))) {
        return 0;
    }
    float share = item_share(place, type);
    if (share <= 0.0f) {
        return 0;
    }
    const PlaceState *state = &search->places[place];
    const ItemDef *def = item_def(type);
    bool loose = def->form == ITEM_FORM_LOOSE;
    /* Both hands hold one thing: cupped loose items or an armful, not both;
     * nothing beside an item wielded with both. */
    if (place == CARRY_BOTH_HANDS && state->items > 0 && (state->loose != loose || state->grip)) {
        return 0;
    }
    float free = 1.0f + ROOM_EPSILON - state->used;
    int by_room = free > 0.0f ? (int)floorf(free / share) : 0;
    if (by_room == 0 && state->items == 0 && !loose) {
        by_room = 1;
    }
    float weight_left =
        carry_place_weight_limit(search->body, place) + WEIGHT_EPSILON - state->weight;
    int by_weight = weight_left > 0.0f ? (int)floorf(weight_left / def->weight) : 0;
    return by_room < by_weight ? by_room : by_weight;
}

static void place_add(Search *search, CarryPlace place, ItemType type, int count)
{
    const ItemDef *def = item_def(type);
    PlaceState *state = &search->places[place];
    state->used += item_share(place, type) * (float)count;
    state->weight += def->weight * (float)count;
    state->items += count;
    state->loose = def->form == ITEM_FORM_LOOSE;
    if (def->form == ITEM_FORM_RIGID && def->length_cm >= ITEM_LONG_CM) {
        state->long_rigid = true;
    }
    if (def->form == ITEM_FORM_LIMP) {
        state->limp = true;
    }
}

static bool in_load(const Search *search, CarryPlace place)
{
    return (search->load_places & (1u << place)) != 0;
}

/* Whether a place is busy: holding something, kept for the load or for the
 * work. */
static bool busy(const Search *search, CarryPlace place)
{
    return search->places[place].items > 0 || in_load(search, place) ||
           (search->working & (1u << place)) != 0;
}

/* A hand free to steady the shoulders: usable, empty, not kept for the
 * load, and both hands free of anything. */
static int free_hands(const Search *search)
{
    if (busy(search, CARRY_BOTH_HANDS)) {
        return 0;
    }
    int free = 0;
    for (CarryPlace hand = CARRY_RIGHT_HAND; hand <= CARRY_LEFT_HAND; hand++) {
        if (carry_place_usable(search->body, hand) && !busy(search, hand)) {
            free++;
        }
    }
    return free;
}

/* The hands the shoulders' load needs: 0, 1 to steady it, 2 to hold it. */
static int shoulders_need(const Search *search, float weight, bool long_rigid, bool limp)
{
    if (weight <= 0.0f) {
        return 0;
    }
    if (limp && weight > CARRY_HELD_WEIGHT * search->body->weight + WEIGHT_EPSILON) {
        return 2;
    }
    return long_rigid || weight > CARRY_STEADY_WEIGHT * search->body->weight + WEIGHT_EPSILON ? 1
                                                                                              : 0;
}

/* Whether the places as they are break no rule between places: both hands
 * holding a thing leave the hands empty, an armful the places under the
 * arms; the shoulders' load has the hands it needs. */
static bool rules_hold(const Search *search)
{
    if (busy(search, CARRY_BOTH_HANDS)) {
        if (busy(search, CARRY_RIGHT_HAND) || busy(search, CARRY_LEFT_HAND)) {
            return false;
        }
        const PlaceState *both = &search->places[CARRY_BOTH_HANDS];
        bool cupped = both->items > 0 ? both->loose || both->grip
                                      : search->load != ITEM_NONE &&
                                            item_def(search->load)->form == ITEM_FORM_LOOSE;
        if (!cupped &&
            (busy(search, CARRY_UNDER_RIGHT_ARM) || busy(search, CARRY_UNDER_LEFT_ARM))) {
            return false;
        }
    }
    const PlaceState *shoulders = &search->places[CARRY_SHOULDERS];
    if (shoulders->items > 0) {
        if (shoulders_need(search, shoulders->weight, shoulders->long_rigid, shoulders->limp) >
            free_hands(search)) {
            return false;
        }
    }
    return true;
}

/* The places a kept item tries, in order (docs/CARRYING.md §6.2): one held
 * with both hands, both; one kept in hand, the better arm's hand, the
 * other, then both hands. */
static int kept_order(const CarryBody *body, ItemType type, bool in_hand, bool grip,
                      CarryPlace *order)
{
    const ItemDef *def = item_def(type);
    int count = 0;
    if (grip) {
        order[count++] = CARRY_BOTH_HANDS;
        return count;
    }
    if (in_hand) {
        bool right_first = body->arm[CARRY_RIGHT] >= body->arm[CARRY_LEFT];
        order[count++] = right_first ? CARRY_RIGHT_HAND : CARRY_LEFT_HAND;
        order[count++] = right_first ? CARRY_LEFT_HAND : CARRY_RIGHT_HAND;
        order[count++] = CARRY_BOTH_HANDS;
        return count;
    }
    if (def->strap) {
        order[count++] = CARRY_SLUNG;
    }
    bool long_item = def->length_cm >= ITEM_LONG_CM;
    if (def->form == ITEM_FORM_LIMP) {
        order[count++] = CARRY_SHOULDERS;
        order[count++] = CARRY_BOTH_HANDS;
        order[count++] = CARRY_RIGHT_HAND;
        order[count++] = CARRY_LEFT_HAND;
    } else if (long_item && def->form != ITEM_FORM_LOOSE) {
        order[count++] = CARRY_UNDER_RIGHT_ARM;
        order[count++] = CARRY_UNDER_LEFT_ARM;
        order[count++] = CARRY_SHOULDERS;
        order[count++] = CARRY_RIGHT_HAND;
        order[count++] = CARRY_LEFT_HAND;
        order[count++] = CARRY_BOTH_HANDS;
    } else {
        order[count++] = CARRY_RIGHT_HAND;
        order[count++] = CARRY_LEFT_HAND;
        order[count++] = CARRY_SHOULDERS; /* A heavy rigid item. */
        order[count++] = CARRY_BOTH_HANDS;
    }
    return count;
}

/* Places a kept item: first where the load does not go, then where it does.
 * False when it fits nowhere. */
static bool place_kept(Search *search, int index, ItemType type)
{
    CarryPlace order[CARRY_PLACE_COUNT + 2];
    bool grip = (search->grip & (1u << index)) != 0;
    int count = kept_order(search->body, type, (search->in_hand & (1u << index)) != 0, grip, order);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < count; i++) {
            CarryPlace place = order[i];
            if (in_load(search, place) != (pass == 1) || place_room_for(search, place, type) < 1) {
                continue;
            }
            Search tried = *search;
            place_add(&tried, place, type, 1);
            tried.places[place].grip = grip;
            if (rules_hold(&tried)) {
                tried.kept_place[index] = (int8_t)place;
                *search = tried;
                return true;
            }
        }
    }
    return false;
}

/* The order the load fills its places in (a container last, apart). */
static const CarryPlace LOAD_ORDER[CARRY_PLACE_COUNT] = {
    CARRY_SHOULDERS,  CARRY_BOTH_HANDS, CARRY_UNDER_RIGHT_ARM, CARRY_UNDER_LEFT_ARM,
    CARRY_RIGHT_HAND, CARRY_LEFT_HAND,  CARRY_SLUNG,           CARRY_IN_CONTAINER,
};

/* How many more items of `type` the containers kept in a hand take (their
 * room, and the weight their hands bear beside them), at most `left`. */
static int container_room(const Search *search, const ItemType *kept, int kept_count, ItemType type,
                          int left)
{
    if (!carry_container_takes(type) || left <= 0) {
        return 0;
    }
    float room = 0.0f, weight = 0.0f;
    for (int i = 0; i < kept_count; i++) {
        const ItemDef *def = item_def(kept[i]);
        int place = search->kept_place[i];
        if (def->container_litres <= 0.0f ||
            (place != CARRY_RIGHT_HAND && place != CARRY_LEFT_HAND && place != CARRY_BOTH_HANDS)) {
            continue;
        }
        room += def->container_litres * CARRY_CONTAINER_BULK_PER_LITRE;
        weight += carry_place_weight_limit(search->body, (CarryPlace)place) - def->weight;
    }
    const ItemDef *def = item_def(type);
    int by_room = (int)floorf((room + ROOM_EPSILON) / def->bulk);
    int by_weight = weight > 0.0f ? (int)floorf((weight + WEIGHT_EPSILON) / def->weight) : 0;
    int count = by_room < by_weight ? by_room : by_weight;
    return count < left ? count : left;
}

/* The most of the load the shoulders may take beside what is there, with
 * the hands the subset leaves free. */
static int shoulders_load_room(const Search *search, ItemType type, int room)
{
    const PlaceState *shoulders = &search->places[CARRY_SHOULDERS];
    const ItemDef *def = item_def(type);
    bool long_rigid = def->form == ITEM_FORM_RIGID && def->length_cm >= ITEM_LONG_CM;
    int hands = free_hands(search);
    while (room > 0) {
        float weight = shoulders->weight + def->weight * (float)room;
        bool limp = def->form == ITEM_FORM_LIMP || shoulders->limp;
        if (shoulders_need(search, weight, long_rigid || shoulders->long_rigid, limp) <= hands) {
            break;
        }
        room--;
    }
    return room;
}

typedef struct Candidate {
    int left_out;
    int load;
    int places;
    int free_hands;
} Candidate;

static bool better(Candidate a, Candidate b)
{
    if (a.left_out != b.left_out) {
        return a.left_out < b.left_out;
    }
    if (a.load != b.load) {
        return a.load > b.load;
    }
    if (a.places != b.places) {
        return a.places < b.places;
    }
    return a.free_hands > b.free_hands;
}

/* Keeps empty the hands the work takes beside those holding its tools: the
 * better arm's first. */
static void reserve_work_hands(Search *search, int kept_count)
{
    int taken = 0;
    for (int i = 0; i < kept_count; i++) {
        if ((search->work & (1u << i)) && search->kept_place[i] >= 0) {
            taken += search->kept_place[i] == CARRY_BOTH_HANDS ? 2 : 1;
        }
    }
    bool right_first = search->body->arm[CARRY_RIGHT] >= search->body->arm[CARRY_LEFT];
    CarryPlace order[2] = {right_first ? CARRY_RIGHT_HAND : CARRY_LEFT_HAND,
                           right_first ? CARRY_LEFT_HAND : CARRY_RIGHT_HAND};
    for (int i = 0; i < 2 && taken < search->work_hands; i++) {
        if (carry_place_usable(search->body, order[i]) && !busy(search, order[i]) &&
            !busy(search, CARRY_BOTH_HANDS)) {
            search->working |= 1u << order[i];
            taken++;
        }
    }
}

/* Tries the load in the places of `subset`; fills `search` and the load
 * per place. */
static Candidate try_subset(Search *search, const ItemType *kept, int kept_count, int load_count,
                            uint32_t *left_out, int *per_place)
{
    Candidate candidate = {0};
    *left_out = 0;
    for (int i = 0; i < kept_count; i++) {
        search->kept_place[i] = -1;
    }
    /* What is held with both hands first, then those kept in hand, then the
     * tools in use and the hands the work keeps empty: the others go around
     * them. */
    for (int round = 0; round < 4; round++) {
        if (round == 3) {
            reserve_work_hands(search, kept_count);
        }
        for (int i = 0; i < kept_count; i++) {
            uint32_t bit = 1u << i;
            bool grip = (search->grip & bit) != 0;
            bool work = !grip && (search->work & bit) != 0;
            bool in_hand = !grip && !work && (search->in_hand & bit) != 0;
            if ((round == 0 && !grip) || (round == 1 && !in_hand) || (round == 2 && !work) ||
                (round == 3 && (grip || work || in_hand))) {
                continue;
            }
            if (!place_kept(search, i, kept[i])) {
                *left_out |= 1u << i;
                candidate.left_out++;
            }
        }
    }
    for (int place = 0; place < CARRY_PLACE_COUNT; place++) {
        per_place[place] = 0;
    }
    if (search->load != ITEM_NONE && load_count > 0) {
        const ItemDef *def = item_def(search->load);
        /* Kept items weigh even where they find no place. */
        float kept_weight = 0.0f;
        for (int i = 0; i < kept_count; i++) {
            kept_weight += item_def(kept[i])->weight;
        }
        float weight_left = search->body->weight + WEIGHT_EPSILON - kept_weight;
        int left = weight_left > 0.0f ? (int)floorf(weight_left / def->weight) : 0;
        if (left > load_count) {
            left = load_count;
        }
        for (int i = 0; i < CARRY_PLACE_COUNT && left > 0; i++) {
            CarryPlace place = LOAD_ORDER[i];
            if (!in_load(search, place)) {
                continue;
            }
            int room = place_room_for(search, place, search->load);
            if (place == CARRY_SHOULDERS) {
                room = shoulders_load_room(search, search->load, room);
            }
            if (room > left) {
                room = left;
            }
            if (room > 0) {
                place_add(search, place, search->load, room);
                per_place[place] = room;
                candidate.load += room;
                candidate.places++;
                left -= room;
            }
        }
        /* What is left goes in a container held in a hand. */
        int contained = container_room(search, kept, kept_count, search->load, left);
        if (contained > 0) {
            per_place[CARRY_IN_CONTAINER] = contained;
            candidate.load += contained;
            left -= contained;
        }
    }
    /* Free hands once the load is in (the subset's hands left empty are free). */
    unsigned subset = search->load_places;
    search->load_places = 0;
    for (int place = 0; place < CARRY_PLACE_COUNT; place++) {
        if (per_place[place] > 0) {
            search->load_places |= 1u << place;
        }
    }
    candidate.free_hands = free_hands(search);
    search->load_places = subset;
    return candidate;
}

static void add_entry(CarryArrangement *out, CarryPlace place, ItemType type, int count, bool kept)
{
    for (int i = 0; i < out->entry_count; i++) {
        CarryEntry *entry = &out->entries[i];
        if (entry->place == place && entry->type == type && entry->kept == kept) {
            entry->count = (uint16_t)(entry->count + count);
            return;
        }
    }
    if (out->entry_count < CARRY_ENTRY_MAX) {
        out->entries[out->entry_count++] =
            (CarryEntry){(uint8_t)place, (uint8_t)type, (uint16_t)count, kept};
    }
}

/* What both hands do and which hand steadies the shoulders, once arranged. */
static void finish(const Search *search, CarryArrangement *out)
{
    const PlaceState *both = &search->places[CARRY_BOTH_HANDS];
    if (out->hold == CARRY_HOLD_NONE && both->items > 0) {
        out->hold = both->grip    ? CARRY_HOLD_GRIP
                    : both->loose ? CARRY_HOLD_CUPPED
                                  : CARRY_HOLD_ARMFUL;
    }
    out->steadying = -1;
    const PlaceState *shoulders = &search->places[CARRY_SHOULDERS];
    if (out->hold == CARRY_HOLD_NONE && shoulders->items > 0) {
        int need =
            shoulders_need(search, shoulders->weight, shoulders->long_rigid, shoulders->limp);
        if (need == 2) {
            out->hold = CARRY_HOLD_SHOULDERS;
        } else if (need == 1) {
            for (CarryPlace hand = CARRY_RIGHT_HAND; hand <= CARRY_LEFT_HAND; hand++) {
                if (carry_place_usable(search->body, hand) && search->places[hand].items == 0) {
                    out->steadying = hand;
                    break;
                }
            }
        }
    }
}

int carry_arrange(const CarryBody *body, const ItemType *kept, int kept_count, ItemType load,
                  int load_count, CarryArrangement *out)
{
    return carry_arrange_held(body, kept, kept_count, 0, load, load_count, out);
}

int carry_arrange_held(const CarryBody *body, const ItemType *kept, int kept_count,
                       uint32_t in_hand, ItemType load, int load_count, CarryArrangement *out)
{
    return carry_arrange_gripped(body, kept, kept_count, in_hand, 0, load, load_count, out);
}

static int arrange(const CarryBody *body, const ItemType *kept, int kept_count, uint32_t in_hand,
                   uint32_t grip, uint32_t work, int work_hands, ItemType load, int load_count,
                   CarryArrangement *out)
{
    in_hand |= grip | work;
    if (kept_count > CARRY_KEPT_MAX) {
        kept_count = CARRY_KEPT_MAX;
    }
    if (load != ITEM_NONE && item_def(load) == NULL) {
        load = ITEM_NONE;
    }
    if (load == ITEM_NONE || load_count < 0) {
        load_count = 0;
    }
    /* The places the load may use at all. */
    unsigned takes = 0;
    if (load_count > 0) {
        for (int place = 0; place < CARRY_PLACE_COUNT; place++) {
            if (carry_place_usable(body, (CarryPlace)place) &&
                item_share((CarryPlace)place, load) > 0.0f) {
                takes |= 1u << place;
            }
        }
    }
    Search best_search = {0};
    Candidate best = {0};
    uint32_t best_left_out = 0;
    int best_per_place[CARRY_PLACE_COUNT] = {0};
    bool found = false;
    /* Every subset of the places the load takes, the empty one first. */
    unsigned subset = 0;
    do {
        Search search = {.body = body,
                         .load_places = subset,
                         .load = load,
                         .in_hand = in_hand,
                         .grip = grip,
                         .work = work,
                         .work_hands = work_hands};
        if (rules_hold(&search)) {
            uint32_t left_out;
            int per_place[CARRY_PLACE_COUNT];
            Candidate candidate =
                try_subset(&search, kept, kept_count, load_count, &left_out, per_place);
            if (!found || better(candidate, best)) {
                found = true;
                best = candidate;
                best_search = search;
                best_left_out = left_out;
                memcpy(best_per_place, per_place, sizeof(per_place));
            }
        }
        subset = (subset - takes) & takes; /* The next subset of `takes`. */
    } while (subset != 0);

    int placed = best.load;
    CarryHold hold = CARRY_HOLD_NONE;
    if (placed == 0 && load_count > 0) {
        /* A single item over every place: carried alone, however heavy. */
        const ItemDef *def = item_def(load);
        bool held = def->form == ITEM_FORM_LIMP &&
                    def->weight <= CARRY_HEAVY_LIMP * body->weight + WEIGHT_EPSILON &&
                    carry_place_usable(body, CARRY_BOTH_HANDS);
        placed = 1;
        hold = held ? CARRY_HOLD_SHOULDERS : CARRY_HOLD_DRAGGED;
        best_per_place[held ? CARRY_SHOULDERS : CARRY_BOTH_HANDS] = 1;
    }
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        out->load = placed;
        out->left_out = best_left_out;
        out->hold = hold;
        out->working = best_search.working;
        for (int i = 0; i < CARRY_KEPT_MAX; i++) {
            out->kept_place[i] = -1;
        }
        for (int i = 0; i < kept_count; i++) {
            if (best_search.kept_place[i] >= 0 && !(best_left_out & (1u << i))) {
                out->kept_place[i] = best_search.kept_place[i];
                add_entry(out, (CarryPlace)best_search.kept_place[i], kept[i], 1, true);
            }
        }
        for (int i = 0; i < CARRY_PLACE_COUNT; i++) {
            CarryPlace place = LOAD_ORDER[i];
            if (best_per_place[place] > 0) {
                add_entry(out, place, load, best_per_place[place], false);
            }
        }
        finish(&best_search, out);
    }
    return placed;
}

int carry_arrange_gripped(const CarryBody *body, const ItemType *kept, int kept_count,
                          uint32_t in_hand, uint32_t grip, ItemType load, int load_count,
                          CarryArrangement *out)
{
    return arrange(body, kept, kept_count, in_hand, grip, 0, 0, load, load_count, out);
}

void carry_arrange_working(const CarryBody *body, const ItemType *kept, int kept_count,
                           uint32_t in_hand, uint32_t grip, uint32_t work, int hands,
                           CarryArrangement *out)
{
    work &= ~grip;
    hands = hands < 0 ? 0 : hands > 2 ? 2 : hands;
    arrange(body, kept, kept_count, in_hand & ~work, grip, work, hands, ITEM_NONE, 0, out);
}

int carry_capacity(const CarryBody *body, const ItemType *kept, int kept_count, ItemType type)
{
    return carry_arrange(body, kept, kept_count, type, UINT16_MAX, NULL);
}

int carry_capacity_held(const CarryBody *body, const ItemType *kept, int kept_count,
                        uint32_t in_hand, ItemType type)
{
    return carry_arrange_held(body, kept, kept_count, in_hand, type, UINT16_MAX, NULL);
}

int carry_capacity_gripped(const CarryBody *body, const ItemType *kept, int kept_count,
                           uint32_t in_hand, uint32_t grip, ItemType type)
{
    return carry_arrange_gripped(body, kept, kept_count, in_hand, grip, type, UINT16_MAX, NULL);
}

int carry_hands_taken(const CarryArrangement *arrangement, const ItemType *kept, int kept_count,
                      uint32_t in_hand)
{
    bool right = false, left = false;
    for (int i = 0; i < kept_count; i++) {
        if (!(in_hand & (1u << i)) || (arrangement->left_out & (1u << i))) {
            continue;
        }
        for (int e = 0; e < arrangement->entry_count; e++) {
            const CarryEntry *entry = &arrangement->entries[e];
            if (!entry->kept || entry->type != kept[i]) {
                continue;
            }
            right |= entry->place == CARRY_RIGHT_HAND || entry->place == CARRY_BOTH_HANDS;
            left |= entry->place == CARRY_LEFT_HAND || entry->place == CARRY_BOTH_HANDS;
        }
    }
    return (right ? 1 : 0) + (left ? 1 : 0);
}
