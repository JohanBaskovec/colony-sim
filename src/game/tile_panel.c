#include "game/tile_panel.h"

#include "core/crown.h"
#include "core/i18n.h"
#include "core/temperature.h"
#include "core/tree.h"
#include "game/hud.h"
#include "game/inspector.h"
#include "game/item_info.h"

#define TEXT_SIZE 256
#define TILE_PANEL_GROUPS 8 /* Kinds of items listed on a heap's tile. */

/* What the player clicked in the rows, applied once the window is drawn
 * (the selection changes under the rows otherwise). */
typedef struct ThingClick {
    bool tile; /* The tile's items together. */
    bool item; /* One item of the tile's list: `pick`. */
    ItemPick pick;
    bool carry; /* The Carry order for `carried` to `carrier`. */
    ItemId carried;
    EntityId carrier;
} ThingClick;

typedef struct ThingRows {
    App *app;
    TileThing thing;
    int x, y, z;
    ThingClick *click;
} ThingRows;

/* The plant of the thing, as the colony knows it: on a tile of trunk or
 * foliage, its tree's; `foot` receives the tile it grows on (where orders and
 * forbidding go). NULL without one. */
static const Plant *thing_plant(const App *app, int x, int y, int z, TilePos *foot, TreeRef *tree,
                                bool *is_tree)
{
    const Map *map = game_view_map(&app->game);
    *foot = (TilePos){(int16_t)x, (int16_t)y, (int16_t)z};
    *is_tree = false;
    if (!map_in_bounds(map, x, y, z)) {
        return NULL;
    }
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    if (tree_at(map, x, y, z, tree)) {
        *is_tree = true;
        *foot = (TilePos){(int16_t)tree->x, (int16_t)tree->y, (int16_t)tree->z};
        return tree_plant(map, tree);
    }
    return plant->type != PLANT_NONE ? plant : NULL;
}

/* The item the window of one item is about (THING_ITEM), NULL when gone. */
static const Item *thing_item(const ThingRows *rows)
{
    if (rows->thing != THING_ITEM) {
        return NULL;
    }
    ItemPick pick = rows->app->selected_item;
    return game_pick_item(&rows->app->game, &pick);
}

/* The record of the settler whose body the window of some items is about
 * (core/corpse.h), NULL for anything else. */
static const Corpse *thing_corpse(const ThingRows *rows)
{
    const Game *game = &rows->app->game;
    if (rows->thing == THING_ITEM) {
        const Item *item = thing_item(rows);
        return item != NULL ? corpses_get(&game->corpses, item->corpse) : NULL;
    }
    if (rows->thing != THING_ITEMS) {
        return NULL;
    }
    ItemStack stack = items_get(game_view_items(game), rows->x, rows->y, rows->z);
    return corpses_get(&game->corpses, stack.corpse);
}

/* The items of the box selected, as the real items name them (those gone
 * left out). Returns how many. */
static int box_real_items(const App *app, ItemId *out)
{
    int count = 0;
    for (int i = 0; i < app->selected_box_item_count; i++) {
        ItemPick pick = app->selected_box_items[i];
        ItemId id = game_pick_real_item(&app->game, &pick);
        if (id.slot != UINT32_MAX) {
            out[count++] = id;
        }
    }
    return count;
}

/* What the player may forbid of the thing (FORBID_TARGET_COUNT: nothing). */
static ForbidTarget thing_forbid_target(TileThing thing)
{
    switch (thing) {
    case THING_STRUCTURE: return FORBID_STRUCTURE;
    case THING_ROOF: return FORBID_ROOF;
    case THING_ITEMS: return FORBID_ITEMS;
    case THING_PLANT: return FORBID_PLANT;
    case THING_SITE: return FORBID_SITE;
    case THING_ITEM: /* (Items one by one: see thing_actions().) */
    case THING_ITEM_BOX:
    case THING_PLANT_BOX: /* (Plants together: see thing_actions().) */
    case THING_TILE:
    case THING_COUNT: break;
    }
    return FORBID_TARGET_COUNT;
}

/* The window's title: the thing's name. */
static void thing_title(const ThingRows *rows, char *out, size_t size)
{
    const Game *game = &rows->app->game;
    const Map *map = game_view_map(game);
    int x = rows->x, y = rows->y, z = rows->z;
    out[0] = '\0';
    switch (rows->thing) {
    case THING_STRUCTURE:
        SDL_strlcpy(out, tr(structure_def(map_structure(map, x, y, z))->name_key), size);
        break;
    case THING_ROOF:
        SDL_strlcpy(out, tr(structure_def(map_roof(map, x, y, z))->name_key), size);
        break;
    case THING_SITE: {
        const ConstructionSite *site =
            construction_get(&game->constructions, construction_at(&game->constructions, x, y, z));
        if (site != NULL) {
            TR_FORMAT(out, size, tr(site->refresh ? "tile_panel.refresh" : "tile_panel.site"),
                      tr(construction_name_key(site)));
        }
        break;
    }
    case THING_ITEMS: {
        ItemStack stack = items_get(game_view_items(game), x, y, z);
        const Corpse *corpse = thing_corpse(rows);
        if (items_mixed(game_view_items(game), x, y, z)) {
            SDL_strlcpy(out, tr("tile_panel.heap"), size);
        } else if (corpse != NULL && corpse->kind == CORPSE_SETTLER) {
            TR_FORMAT(out, size, tr("tile_panel.body"), corpse->colonist.name);
        } else if (stack.type != ITEM_NONE) {
            SDL_strlcpy(out, hud_item_name(stack), size);
        } else { /* Only items lying where they fell: the newest one's name. */
            const Item *item = items_first(game_view_items(game), map_index(map, x, y, z));
            SDL_strlcpy(out, item != NULL ? hud_item_name(item_as_stack(item)) : "", size);
        }
        break;
    }
    case THING_PLANT: {
        TilePos foot;
        TreeRef tree;
        bool is_tree;
        const Plant *plant = thing_plant(rows->app, x, y, z, &foot, &tree, &is_tree);
        if (plant != NULL) {
            SDL_strlcpy(out, tr(plant_def((PlantType)plant->type)->name_key), size);
        }
        break;
    }
    case THING_ITEM: {
        const Item *item = thing_item(rows);
        const Corpse *corpse = thing_corpse(rows);
        if (corpse != NULL && corpse->kind == CORPSE_SETTLER) {
            TR_FORMAT(out, size, tr("tile_panel.body"), corpse->colonist.name);
        } else if (item != NULL) {
            SDL_strlcpy(out, hud_item_name(item_as_stack(item)), size);
        }
        break;
    }
    case THING_ITEM_BOX: {
        char count[32];
        int items = rows->app->selected_box_item_count;
        tr_integer(count, sizeof(count), items);
        TR_FORMAT(out, size, tr_plural("tile_panel.box_items", items), count);
        break;
    }
    case THING_PLANT_BOX:
        SDL_strlcpy(out, tr(plant_def((PlantType)rows->app->selected_plant_type)->name_key), size);
        break;
    case THING_TILE: {
        char a[32], b[32];
        tr_integer(a, sizeof(a), x);
        tr_integer(b, sizeof(b), y);
        TR_FORMAT(out, size, tr("hud.tile.position"), a, b);
        break;
    }
    case THING_COUNT: break;
    }
}

/* Kinds of items the window of a box lists. */
#define BOX_KINDS_LISTED 10

/* What the items of a box are: their total weight, and a line per kind. */
static void box_item_rows(UiLayout *layout, const ThingRows *rows)
{
    const App *app = rows->app;
    long counts[ITEM_TYPE_COUNT] = {0};
    float masses[ITEM_TYPE_COUNT] = {0};
    float total = 0.0f;
    int forbidden = 0, items = 0;
    for (int i = 0; i < app->selected_box_item_count; i++) {
        ItemPick pick = app->selected_box_items[i];
        const Item *item = game_pick_item(&app->game, &pick);
        if (item == NULL) {
            continue;
        }
        float mass = item_stack_mass(item_as_stack(item));
        counts[item->type]++;
        masses[item->type] += mass;
        total += mass;
        forbidden += item->forbidden != 0;
        items++;
    }
    char text[TEXT_SIZE], weight[32], count[32];
    hud_weight_text(weight, sizeof(weight), total);
    TR_FORMAT(text, sizeof(text), tr("tile_panel.item_weight"), weight);
    ui_layout_label(layout, text, 0.0f, false);
    int listed = 0, left = items;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT && listed < BOX_KINDS_LISTED; type++) {
        if (counts[type] == 0) {
            continue;
        }
        tr_integer(count, sizeof(count), counts[type]);
        hud_weight_text(weight, sizeof(weight), masses[type]);
        TR_FORMAT(text, sizeof(text), tr("tile_panel.group"),
                  tr(item_def((ItemType)type)->name_key), count, weight);
        ui_layout_label(layout, text, 0.0f, false);
        left -= (int)counts[type];
        listed++;
    }
    if (left > 0) {
        tr_integer(count, sizeof(count), left);
        TR_FORMAT(text, sizeof(text), tr_plural("tile_panel.more_items", left), count);
        ui_layout_label(layout, text, 0.0f, true);
    }
    if (forbidden > 0) {
        tr_integer(count, sizeof(count), forbidden);
        TR_FORMAT(text, sizeof(text), tr_plural("tile_panel.box_forbidden", forbidden), count);
        ui_layout_label(layout, text, 0.0f, true);
    }
}

/* The settlers who may carry the item (docs/CARRYING.md §7.2): a click on
 * one gives it the Carry order; those who cannot are dimmed, the reason in
 * their tooltip. */
static void carried_by_rows(UiLayout *layout, const ThingRows *rows)
{
    const Game *game = &rows->app->game;
    ItemPick pick = rows->app->selected_item;
    ItemId id = game_pick_real_item(game, &pick);
    if (id.slot == UINT32_MAX) {
        return; /* Only remembered there. */
    }
    bool heading = false;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId colonist_id = game_colonist_id_at(game, slot);
        const Colonist *colonist = game_colonist_const(game, colonist_id);
        if (colonist == NULL || health_is_dead(&colonist->health)) {
            continue;
        }
        if (!heading) {
            ui_layout_label(layout, tr("tile_panel.carried_by"), 0.0f, true);
            heading = true;
        }
        CarryRefusal refusal = game_carry_refusal(game, colonist_id, id);
        SDL_FRect row = {layout->x, layout->y, layout->width, layout->row};
        bool clicked =
            ui_layout_link(layout, colonist->name, UI_LAYOUT_INDENT, refusal != CARRY_REFUSAL_NONE);
        if (layout->draw && ui_hovered(layout->ui, row)) {
            const char *line = refusal != CARRY_REFUSAL_NONE
                                   ? tr(carry_refusal_key(refusal))
                                   : tr("tile_panel.carried_by.description");
            ui_tooltip(layout->ui, &line, 1);
        }
        if (clicked && refusal == CARRY_REFUSAL_NONE) {
            rows->click->carry = true;
            rows->click->carried = id;
            rows->click->carrier = colonist_id;
        }
    }
}

/* What there is to know of one item: its weight, quality and condition, when
 * it rots away, whether it lies where it fell, what else lies with it, and
 * who may carry it. */
static void single_item_rows(UiLayout *layout, const ThingRows *rows)
{
    const Game *game = &rows->app->game;
    const Item *item = thing_item(rows);
    if (item == NULL) {
        return;
    }
    const Corpse *corpse = thing_corpse(rows);
    if (corpse != NULL) {
        hud_corpse_rows(layout, game, corpse);
    }
    ItemStack one = item_as_stack(item);
    char text[TEXT_SIZE], weight[32];
    hud_weight_text(weight, sizeof(weight), item_stack_mass(one));
    TR_FORMAT(text, sizeof(text), tr("tile_panel.item_weight"), weight);
    ui_layout_label(layout, text, 0.0f, false);
    if (hud_item_condition_text(text, sizeof(text), one)) {
        ui_layout_label(layout, text, 0.0f, false);
    }
    if (item_rots((ItemType)one.type)) {
        hud_rot_text(text, sizeof(text),
                     item_stack_rot_hours(one, game_item_rot_rate(game, (ItemType)one.type, rows->x,
                                                                  rows->y, rows->z)));
        ui_layout_label(layout, text, 0.0f, false);
    }
    if (item_takes_water((ItemType)one.type)) {
        char moisture[TEXT_SIZE];
        item_info_moisture_text(moisture, sizeof(moisture), (ItemType)one.type,
                                item_stack_moisture(one));
        TR_FORMAT(text, sizeof(text), tr("hud.tile.item_moisture"), moisture);
        ui_layout_label(layout, text, 0.0f, false);
    }
    char retting[TEXT_SIZE];
    if (item_info_retting_text(retting, sizeof(retting), one)) {
        TR_FORMAT(text, sizeof(text), tr("hud.tile.item_retting"), retting);
        ui_layout_label(layout, text, 0.0f, false);
    }
    const Items *items = game_items_at(game, rows->x, rows->y, rows->z);
    size_t index = map_index(game_view_map(game), rows->x, rows->y, rows->z);
    int others = -1;
    for (const Item *other = items_first(items, index); other != NULL;
         other = items_next(items, other)) {
        others++;
    }
    if (others > 0) {
        char count[32];
        tr_integer(count, sizeof(count), others);
        TR_FORMAT(text, sizeof(text), tr_plural("tile_panel.item_others", others), count);
        if (ui_layout_link(layout, text, 0.0f, true)) {
            rows->click->tile = true; /* A click selects the tile's items. */
        }
    }
    if (item->forbidden) {
        ui_layout_label(layout, tr("tile_panel.left_alone"), 0.0f, true);
    }
    carried_by_rows(layout, rows);
}

/* Items of a tile listed in the inspector at most. */
#define ITEM_LIST_LINES 40

/* The heading of a tile's item list and the buttons that sort it. */
static void list_order_buttons(UiLayout *layout, App *app)
{
    static const char *const ORDER_KEYS[ITEM_ORDER_COUNT] = {
        N_("tile_panel.order_newest"), N_("tile_panel.order_quality"),
        N_("tile_panel.order_condition"), N_("tile_panel.order_rot")};
    ui_layout_label(layout, tr("tile_panel.items_here"), 0.0f, true);
    float x = layout->x, width = 0.0f;
    for (int order = 0; order < ITEM_ORDER_COUNT; order++) {
        const char *label = tr(ORDER_KEYS[order]);
        float button = ui_button_width(layout->ui, label);
        if (layout->draw &&
            ui_toggle_button(layout->ui, (SDL_FRect){x, layout->y, button, layout->button_height},
                             label, app->items_order == (ItemOrder)order)) {
            app->items_order = (ItemOrder)order;
        }
        x += button + UI_LAYOUT_GAP;
        width += button + UI_LAYOUT_GAP;
    }
    ui_layout_measure(layout, width);
    layout->y += layout->row + layout->ui->theme.gap;
}

/* The items lying on the tile one by one, in the order chosen with the
 * buttons above them (quality, condition, rot); a single item only when
 * nothing else tells of it (`alone`: no stack, only items lying where they
 * fell). */
static void item_list_rows(UiLayout *layout, const ThingRows *rows, bool alone)
{
    App *app = rows->app;
    const Game *game = &app->game;
    const Items *items = game_items_at(game, rows->x, rows->y, rows->z);
    size_t index = map_index(game_view_map(game), rows->x, rows->y, rows->z);
    const Item *list[ITEM_LIST_LINES];
    int count = items_list(items, index, app->items_order, list, ITEM_LIST_LINES);
    if (count == 0 || (count == 1 && !alone)) {
        return; /* (One item of a stack: the rows above tell of it.) */
    }
    if (count > 1) {
        list_order_buttons(layout, app);
    }
    int shown = count < ITEM_LIST_LINES ? count : ITEM_LIST_LINES;
    for (int i = 0; i < shown; i++) {
        ItemStack one = item_as_stack(list[i]);
        char condition[TEXT_SIZE], line[TEXT_SIZE];
        const char *name = hud_item_name(one);
        if (!hud_item_condition_text(condition, sizeof(condition), one)) {
            SDL_strlcpy(condition, "", sizeof(condition));
        }
        char after[TEXT_SIZE] = ""; /* How long it keeps, or how wet it is. */
        if (item_rots((ItemType)one.type)) {
            float rate = game_item_rot_rate(game, (ItemType)one.type, rows->x, rows->y, rows->z);
            hud_rot_text(after, sizeof(after), item_stack_rot_hours(one, rate));
        } else if (item_takes_water((ItemType)one.type)) {
            item_info_moisture_text(after, sizeof(after), (ItemType)one.type,
                                    item_stack_moisture(one));
        }
        if (after[0] != '\0' && condition[0] != '\0') {
            TR_FORMAT(line, sizeof(line), tr("tile_panel.item_line_rot"), name, condition, after);
        } else if (after[0] != '\0') {
            TR_FORMAT(line, sizeof(line), tr("tile_panel.item_line"), name, after);
        } else if (condition[0] != '\0') {
            TR_FORMAT(line, sizeof(line), tr("tile_panel.item_line"), name, condition);
        } else {
            SDL_strlcpy(line, name, sizeof(line));
        }
        /* Forbidden: dimmed. A click selects the item. */
        if (ui_layout_link(layout, line, 0.0f, list[i]->forbidden)) {
            rows->click->item = true;
            rows->click->pick = (ItemPick){items_id(items, list[i]), (int16_t)rows->x,
                                           (int16_t)rows->y, (int16_t)rows->z};
        }
    }
    if (count > shown) {
        char more[32], line[TEXT_SIZE];
        tr_integer(more, sizeof(more), count - shown);
        TR_FORMAT(line, sizeof(line), tr_plural("tile_panel.more_items", count - shown), more);
        ui_layout_label(layout, line, 0.0f, true);
    }
}

/* What hangs in a block of foliage (core/crown.h), one by one: "Wild apple
 * (on the tree)". */
static void crown_rows(UiLayout *layout, const ThingRows *rows)
{
    const Map *map = game_view_map(&rows->app->game);
    if (map->crowns == NULL) {
        return;
    }
    CrownItem list[ITEM_LIST_LINES];
    int count = crowns_list(map->crowns, rows->x, rows->y, rows->z, list, ITEM_LIST_LINES);
    int shown = count < ITEM_LIST_LINES ? count : ITEM_LIST_LINES;
    for (int i = 0; i < shown; i++) {
        ItemStack one = crown_item_stack(list[i]);
        char condition[TEXT_SIZE], line[TEXT_SIZE], name[TEXT_SIZE];
        if (hud_item_condition_text(condition, sizeof(condition), one)) {
            TR_FORMAT(name, sizeof(name), tr("tile_panel.item_line"), hud_item_name(one),
                      condition);
        } else {
            SDL_strlcpy(name, hud_item_name(one), sizeof(name));
        }
        TR_FORMAT(line, sizeof(line), tr("crown.on_the_tree"), name);
        ui_layout_label(layout, line, 0.0f, false);
    }
    if (count > shown) {
        char more[32], line[TEXT_SIZE];
        tr_integer(more, sizeof(more), count - shown);
        TR_FORMAT(line, sizeof(line), tr_plural("tile_panel.more_items", count - shown), more);
        ui_layout_label(layout, line, 0.0f, true);
    }
}

static void plant_rows(UiLayout *layout, const ThingRows *rows)
{
    const Game *game = &rows->app->game;
    TilePos foot;
    TreeRef tree;
    bool is_tree;
    const Plant *plant = thing_plant(rows->app, rows->x, rows->y, rows->z, &foot, &tree, &is_tree);
    if (plant == NULL) {
        return;
    }
    char a[32], b[32], text[TEXT_SIZE];
    if (is_tree) {
        int levels = tree_shape_levels(&tree.shape);
        tr_integer(a, sizeof(a), levels);
        TR_FORMAT(text, sizeof(text), tr_plural("hud.tile.tree_levels", levels), a);
        ui_layout_label(layout, text, 0.0f, false);
    }
    /* What it meets on its tile: the temperature, the light, the ground. */
    PlantConditions conditions = game_plant_conditions(game, foot.x, foot.y, foot.z);
    if (plant_is_dormant(plant)) {
        ui_layout_label(layout, tr("tile_panel.dormant"), 0.0f, false);
    } else {
        tr_integer(a, sizeof(a), (long long)(plant->growth * 100.0f + 0.5f));
        TR_FORMAT(text, sizeof(text), tr("tile_panel.growth"), a);
        ui_layout_label(layout, text, 0.0f, false);
        TR_FORMAT(text, sizeof(text), tr("tile_panel.condition"),
                  tr(plant_status_name_key(plant_status(plant, &conditions))));
        ui_layout_label(layout, text, 0.0f, false);
        float rate = plant_growth_rate(plant, &conditions);
        if (rate > 0.0f && plant->growth < 1.0f) {
            tr_integer(a, sizeof(a), (long long)(rate * 100.0f + 0.5f));
            TR_FORMAT(text, sizeof(text), tr("tile_panel.growth_rate"), a);
            ui_layout_label(layout, text, 0.0f, false);
        }
    }
    if (plant->damage > 0) {
        tr_integer(a, sizeof(a), (long long)(plant_health(plant) * 100.0f + 0.5f));
        TR_FORMAT(text, sizeof(text), tr("tile_panel.plant_health"), a);
        ui_layout_label(layout, text, 0.0f, false);
    }
    for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
        const PlantProduct *product = plant_product((PlantType)plant->type, slot);
        if (product == NULL || plant_stock_capacity(plant, slot) <= 0) {
            continue;
        }
        const char *item = tr(item_def(product->item)->name_key);
        if (!plant_product_in_season(plant, slot)) {
            TR_FORMAT(text, sizeof(text), tr("hud.tile.plant_out_of_season"), item);
        } else {
            tr_integer(a, sizeof(a), plant_stock_available(plant, slot));
            tr_integer(b, sizeof(b), plant_stock_capacity(plant, slot));
            TR_FORMAT(text, sizeof(text), tr("hud.tile.plant_stock"), item, a, b);
        }
        ui_layout_label(layout, text, 0.0f, false);
    }
    if (is_tree) {
        crown_rows(layout, rows);
    }
}

/* The order on a tile ("Order: Take apart (40% done)"), if any. */
static void order_row(UiLayout *layout, const Game *game, int x, int y, int z)
{
    DesignationType designation = designation_get(&game->designations, x, y, z);
    if (designation == DESIGNATION_NONE) {
        return;
    }
    char a[32], text[TEXT_SIZE];
    const char *name = tr(designation_def(designation)->name_key);
    float progress =
        designation_progress_fraction(&game->designations, game_view_map(game), x, y, z);
    if (progress > 0.0f) {
        tr_integer(a, sizeof(a), (long long)(progress * 100.0f));
        TR_FORMAT(text, sizeof(text), tr("hud.tile.designation_progress"), name, a);
    } else {
        TR_FORMAT(text, sizeof(text), tr("hud.tile.designation"), name);
    }
    ui_layout_label(layout, text, 0.0f, false);
}

static void thing_rows(UiLayout *layout, void *userdata)
{
    const ThingRows *rows = userdata;
    const Game *game = &rows->app->game;
    const Map *map = game_view_map(game);
    int x = rows->x, y = rows->y, z = rows->z;
    char text[TEXT_SIZE];
    if (game->fog_on && rows->thing != THING_ITEM_BOX && rows->thing != THING_PLANT_BOX &&
        rows->thing != THING_TILE && !game_tile_seen(game, x, y, z)) {
        ui_layout_label(layout, tr("hud.tile.remembered"), 0.0f, true);
    }
    switch (rows->thing) {
    case THING_STRUCTURE: {
        StructureType structure = map_structure(map, x, y, z);
        if (hud_structure_condition_text(text, sizeof(text), map, x, y, z)) {
            ui_layout_label(layout, text, 0.0f, false);
        }
        if (map_structure_woods(map, x, y, z) != 0) {
            hud_wood_text(text, sizeof(text), map_structure_woods(map, x, y, z));
            ui_layout_label(layout, text, 0.0f, false);
        }
        if (structure_is_bed(structure)) {
            hud_bed_owner_text(rows->app, text, sizeof(text), x, y, z);
            ui_layout_label(layout, text, 0.0f, false);
        }
        if (game_door_kept_open(game, x, y, z)) {
            ui_layout_label(layout, tr("tile_panel.kept_open"), 0.0f, false);
        }
        order_row(layout, game, x, y, z);
        break;
    }
    case THING_ROOF: {
        char quality[16];
        hud_quality_text(quality, sizeof(quality), map_roof_quality(map, x, y, z));
        TR_FORMAT(text, sizeof(text), tr("hud.item_quality"), quality);
        ui_layout_label(layout, text, 0.0f, false);
        if (map_roof_woods(map, x, y, z) != 0) {
            hud_wood_text(text, sizeof(text), map_roof_woods(map, x, y, z));
            ui_layout_label(layout, text, 0.0f, false);
        }
        order_row(layout, game, x, y, z);
        break;
    }
    case THING_SITE: {
        const ConstructionSite *site =
            construction_get(&game->constructions, construction_at(&game->constructions, x, y, z));
        if (site == NULL) {
            break;
        }
        for (int line = 0; line <= STRUCTURE_MATERIAL_SLOTS; line++) {
            if (hud_site_line(site, line, text, sizeof(text))) {
                ui_layout_label(layout, text, 0.0f, false);
            }
        }
        if (site->workstation == 0 &&
            structure_wooden_material((StructureType)site->type) != ITEM_NONE) {
            /* What was brought so far, else what is wanted. */
            WoodMask delivered = construction_woods(site);
            hud_wood_text(text, sizeof(text), delivered != 0 ? delivered : site->wood_choice);
            ui_layout_label(layout, text, 0.0f, false);
        }
        if (gather_clear_work(map, x, y, z) > 0.0f) {
            TR_FORMAT(
                text, sizeof(text), tr("blueprint.clear_first"),
                tr(plant_def((PlantType)map->plants[map_index(map, x, y, z)].type)->name_key));
            ui_layout_label(layout, text, 0.0f, true);
        }
        break;
    }
    case THING_ITEMS: {
        ItemStack stack = items_get(game_view_items(game), x, y, z);
        const Corpse *corpse = thing_corpse(rows);
        if (corpse != NULL) {
            hud_corpse_rows(layout, game, corpse);
        }
        char count[32], weight[32];
        ItemStack groups[TILE_PANEL_GROUPS];
        int group_count = items_groups(game_view_items(game), x, y, z, groups, TILE_PANEL_GROUPS);
        if (group_count > 1) { /* A heap: a line per kind. */
            for (int i = 0; i < group_count; i++) {
                tr_integer(count, sizeof(count), groups[i].count);
                hud_weight_text(weight, sizeof(weight), item_stack_mass(groups[i]));
                TR_FORMAT(text, sizeof(text), tr("tile_panel.group"), hud_item_name(groups[i]),
                          count, weight);
                ui_layout_label(layout, text, 0.0f, false);
            }
        } else if (stack.type != ITEM_NONE) {
            tr_integer(count, sizeof(count), stack.count);
            hud_weight_text(weight, sizeof(weight), item_stack_mass(stack));
            TR_FORMAT(text, sizeof(text), tr("tile_panel.count"), count, weight);
            ui_layout_label(layout, text, 0.0f, false);
            if (hud_item_condition_text(text, sizeof(text), stack)) {
                ui_layout_label(layout, text, 0.0f, false);
            }
        }
        item_list_rows(layout, rows, group_count == 0);
        break;
    }
    case THING_ITEM: single_item_rows(layout, rows); break;
    case THING_ITEM_BOX: box_item_rows(layout, rows); break;
    case THING_PLANT_BOX: {
        char count[32];
        int plants = rows->app->selected_plant_count;
        tr_integer(count, sizeof(count), plants);
        TR_FORMAT(text, sizeof(text), tr_plural("tile_panel.box_plants", plants), count);
        ui_layout_label(layout, text, 0.0f, false);
        break;
    }
    case THING_TILE: hud_tile_rows(layout, rows->app, x, y, z); break;
    case THING_PLANT: {
        plant_rows(layout, rows);
        TilePos foot;
        TreeRef tree;
        bool is_tree;
        thing_plant(rows->app, x, y, z, &foot, &tree, &is_tree);
        order_row(layout, game, foot.x, foot.y, foot.z);
        break;
    }
    case THING_COUNT: break;
    }
    ForbidTarget target = thing_forbid_target(rows->thing);
    if (target != FORBID_TARGET_COUNT && game_forbidden(game, x, y, z, target)) {
        StructureType structure = map_structure(map, x, y, z);
        bool locked = target == FORBID_STRUCTURE &&
                      (structure == STRUCTURE_DOOR || structure == STRUCTURE_LADDER);
        ui_layout_label(layout, tr(locked ? "tile_panel.locked" : "tile_panel.left_alone"), 0.0f,
                        true);
    }
}

/* An order that toggles on a tile (fell a tree, take a structure apart):
 * placed, or taken back when it is there. */
static void toggle_order(App *app, DesignationType type, TilePos tile)
{
    bool ordered = designation_get(&app->game.designations, tile.x, tile.y, tile.z) == type;
    game_designate_rect(&app->game, tile.x, tile.y, tile.x, tile.y, tile.z,
                        ordered ? DESIGNATION_NONE : type);
}

/* Why an order cannot be given on a tile now (nobody knows how), NULL when
 * it can. */
static const char *unavailable(const Game *game, DesignationType type, int x, int y, int z)
{
    return game_designation_available_at(game, type, x, y, z) ? NULL : tr("hud.tool.unavailable");
}

/* An order given to all the plants selected, lit while some have it (a click
 * then takes it back from all of them). */
static void plant_box_order(App *app, int *slot, DesignationType type, RenderIcon icon,
                            const char *name, const char *description)
{
    Game *game = &app->game;
    bool ordered = false, available = false;
    for (int i = 0; i < app->selected_plant_count; i++) {
        TilePos at = app->selected_plants[i];
        ordered |= designation_get(&game->designations, at.x, at.y, at.z) == type;
        available |= game_designation_available_at(game, type, at.x, at.y, at.z);
    }
    if (inspector_order_action(app, (*slot)++, icon, name, description, ordered,
                               available ? NULL : tr("hud.tool.unavailable"), false)) {
        for (int i = 0; i < app->selected_plant_count; i++) {
            TilePos at = app->selected_plants[i];
            game_designate_rect(game, at.x, at.y, at.x, at.y, at.z,
                                ordered ? DESIGNATION_NONE : type);
        }
    }
}

/* The actions on plants selected together: forbid, and the orders their kind
 * takes (a tree is felled, a bush harvested or uprooted). */
static void plant_box_actions(App *app, int *slot)
{
    Game *game = &app->game;
    const Map *map = game_view_map(game);
    bool any = false, all_forbidden = true;
    for (int i = 0; i < app->selected_plant_count; i++) {
        TilePos at = app->selected_plants[i];
        if (game_can_forbid(game, at.x, at.y, at.z, FORBID_PLANT)) {
            any = true;
            all_forbidden &= game_forbidden(game, at.x, at.y, at.z, FORBID_PLANT);
        }
    }
    if (any && inspector_forbid_action(app, (*slot)++, all_forbidden, NULL)) {
        for (int i = 0; i < app->selected_plant_count; i++) {
            TilePos at = app->selected_plants[i];
            if (game_can_forbid(game, at.x, at.y, at.z, FORBID_PLANT)) {
                game_set_forbidden(game, at.x, at.y, at.z, FORBID_PLANT, !all_forbidden);
            }
        }
    }
    if (app->selected_plant_count == 0) {
        return;
    }
    if (plant_is_tree((uint8_t)app->selected_plant_type)) {
        plant_box_order(app, slot, DESIGNATION_CHOP, RENDER_ICON_CHOP, tr("tile_panel.chop"),
                        tr("tile_panel.chop.description"));
        return;
    }
    TilePos first = app->selected_plants[0];
    if (gather_plant_harvest_job(map, first.x, first.y, first.z) != GATHER_JOB_NONE) {
        plant_box_order(app, slot, DESIGNATION_HARVEST, RENDER_ICON_HARVEST,
                        tr("designation.harvest"), tr("tile_panel.harvest.description"));
        plant_box_order(app, slot, DESIGNATION_HARVEST_RIPE, RENDER_ICON_HARVEST_RIPE,
                        tr("designation.harvest_ripe"), tr("tile_panel.harvest_ripe.description"));
    }
    if (gather_plant_uprootable(map, first.x, first.y, first.z)) {
        plant_box_order(app, slot, DESIGNATION_UPROOT, RENDER_ICON_UPROOT, tr("designation.uproot"),
                        tr("tile_panel.uproot.description"));
    }
}

/* The action menu, right of the box: forbid, the orders the thing takes,
 * build another. */
static void thing_actions(App *app, const ThingRows *rows)
{
    Game *game = &app->game;
    int x = rows->x, y = rows->y, z = rows->z;
    int slot = 0;
    ForbidTarget target = thing_forbid_target(rows->thing);
    if (target != FORBID_TARGET_COUNT && game_can_forbid(game, x, y, z, target)) {
        bool forbidden = game_forbidden(game, x, y, z, target);
        if (inspector_forbid_action(app, slot, forbidden, NULL)) {
            game_set_forbidden(game, x, y, z, target, !forbidden);
        }
        slot++;
    }
    if (rows->thing == THING_STRUCTURE &&
        map_structure(game_view_map(game), x, y, z) == STRUCTURE_DOOR) {
        bool open = game_door_kept_open(game, x, y, z);
        const char *lines[2] = {
            tr(open ? "tile_panel.let_close" : "tile_panel.keep_open"),
            tr(open ? "tile_panel.let_close.description" : "tile_panel.keep_open.description")};
        SDL_FRect icon;
        if (inspector_action(app, slot++, open, &icon, lines, 2)) {
            game_set_door_kept_open(game, x, y, z, !open);
        }
        render_tool_icon(app->renderer, icon, RENDER_ICON_KEEP_OPEN);
    }
    switch (rows->thing) {
    case THING_STRUCTURE:
    case THING_ROOF: {
        TilePos tile = {(int16_t)x, (int16_t)y, (int16_t)z};
        bool ordered = designation_get(&game->designations, x, y, z) == DESIGNATION_DECONSTRUCT;
        if (inspector_order_action(
                app, slot++, RENDER_ICON_DECONSTRUCT, tr("tile_panel.deconstruct"),
                tr(rows->thing == THING_ROOF ? "tile_panel.deconstruct_roof.description"
                                             : "tile_panel.deconstruct.description"),
                ordered, unavailable(game, DESIGNATION_DECONSTRUCT, x, y, z), true)) {
            toggle_order(app, DESIGNATION_DECONSTRUCT, tile);
        }
        break;
    }
    case THING_PLANT: {
        TilePos foot;
        TreeRef tree;
        bool is_tree;
        const Plant *plant = thing_plant(app, x, y, z, &foot, &tree, &is_tree);
        if (plant == NULL) {
            break;
        }
        DesignationType order = designation_get(&game->designations, foot.x, foot.y, foot.z);
        if (plant_is_tree(plant->type)) {
            if (inspector_order_action(app, slot++, RENDER_ICON_CHOP, tr("tile_panel.chop"),
                                       tr("tile_panel.chop.description"), order == DESIGNATION_CHOP,
                                       unavailable(game, DESIGNATION_CHOP, x, y, z),
                                       order == DESIGNATION_CHOP)) {
                toggle_order(app, DESIGNATION_CHOP, foot);
            }
            break;
        }
        const Map *map = game_view_map(game);
        if (gather_plant_harvest_job(map, x, y, z) != GATHER_JOB_NONE) {
            const char *reason = unavailable(game, DESIGNATION_HARVEST, x, y, z);
            if (reason == NULL && !gather_plant_harvestable(map, x, y, z)) {
                reason = tr("tile_panel.nothing_to_harvest");
            }
            if (inspector_order_action(app, slot++, RENDER_ICON_HARVEST, tr("designation.harvest"),
                                       tr("tile_panel.harvest.description"),
                                       order == DESIGNATION_HARVEST, reason,
                                       order == DESIGNATION_HARVEST)) {
                toggle_order(app, DESIGNATION_HARVEST, foot);
            }
            if (inspector_order_action(
                    app, slot++, RENDER_ICON_HARVEST_RIPE, tr("designation.harvest_ripe"),
                    tr("tile_panel.harvest_ripe.description"), order == DESIGNATION_HARVEST_RIPE,
                    unavailable(game, DESIGNATION_HARVEST_RIPE, x, y, z),
                    order == DESIGNATION_HARVEST_RIPE)) {
                toggle_order(app, DESIGNATION_HARVEST_RIPE, foot);
            }
        }
        if (gather_plant_uprootable(map, x, y, z) &&
            inspector_order_action(app, slot++, RENDER_ICON_UPROOT, tr("designation.uproot"),
                                   tr("tile_panel.uproot.description"), order == DESIGNATION_UPROOT,
                                   unavailable(game, DESIGNATION_UPROOT, x, y, z),
                                   order == DESIGNATION_UPROOT)) {
            toggle_order(app, DESIGNATION_UPROOT, foot);
        }
        break;
    }
    case THING_SITE:
        if (inspector_delete_action(app, slot++, tr("tile_panel.cancel_site.description"), true)) {
            game_remove_blueprints_rect(game, x, y, x, y, z);
        }
        break;
    case THING_ITEMS: {
        if (items_get(&game->items, x, y, z).type == ITEM_NONE) {
            break; /* Nothing there any more. */
        }
        TilePos destination;
        bool ordered = game_item_move_at(game, x, y, z, &destination);
        if (inspector_order_action(app, slot++, RENDER_ICON_MOVE, tr("tile_panel.move_items"),
                                   tr("tile_panel.move_items.description"), ordered, NULL,
                                   ordered)) {
            if (ordered) {
                game_cancel_item_move(game, x, y, z);
            } else {
                app->move_item_source = (TilePos){(int16_t)x, (int16_t)y, (int16_t)z};
                app->move_item_id = ITEM_ID_NONE;
                app->move_item_box = false;
                app_toggle_tool(app, APP_TOOL_MOVE_ITEM, 0);
            }
        }
        break;
    }
    case THING_ITEM: {
        ItemPick pick = app->selected_item;
        ItemId id = game_pick_real_item(game, &pick);
        if (id.slot == UINT32_MAX || !game_tile_explored(game, x, y, z)) {
            break; /* Gone (picked up, eaten, rotted away). */
        }
        bool forbidden = game_item_forbidden(game, id);
        if (inspector_forbid_action(app, slot++, forbidden, NULL)) {
            game_set_item_forbidden(game, id, !forbidden);
        }
        TilePos destination;
        bool ordered = game_single_item_move_at(game, id, &destination);
        if (inspector_order_action(app, slot++, RENDER_ICON_MOVE, tr("tile_panel.move_items"),
                                   tr("tile_panel.move_item.description"), ordered, NULL,
                                   ordered)) {
            if (ordered) {
                game_cancel_single_item_move(game, id);
            } else {
                app->move_item_source = (TilePos){(int16_t)x, (int16_t)y, (int16_t)z};
                app->move_item_id = id;
                app->move_item_box = false;
                app_toggle_tool(app, APP_TOOL_MOVE_ITEM, 0);
            }
        }
        const char *lines[2] = {tr("tile_panel.item_info"), tr("tile_panel.item_info.description")};
        SDL_FRect icon;
        if (inspector_action(app, slot++, app->show_item_info, &icon, lines, 2)) {
            app->show_item_info = !app->show_item_info;
        }
        render_tool_icon(app->renderer, icon, RENDER_ICON_INFO);
        break;
    }
    case THING_ITEM_BOX: {
        ItemId ids[APP_BOX_ITEMS_MAX];
        int count = box_real_items(app, ids);
        if (count == 0 || !game_tile_explored(game, x, y, z)) {
            break;
        }
        bool all_forbidden = true, all_ordered = true;
        for (int i = 0; i < count; i++) {
            TilePos destination;
            all_forbidden &= game_item_forbidden(game, ids[i]);
            all_ordered &= game_single_item_move_at(game, ids[i], &destination);
        }
        if (inspector_forbid_action(app, slot++, all_forbidden, NULL)) {
            for (int i = 0; i < count; i++) {
                game_set_item_forbidden(game, ids[i], !all_forbidden);
            }
        }
        if (inspector_order_action(app, slot++, RENDER_ICON_MOVE, tr("tile_panel.move_items"),
                                   tr("tile_panel.move_box.description"), all_ordered, NULL,
                                   all_ordered)) {
            if (all_ordered) {
                for (int i = 0; i < count; i++) {
                    game_cancel_single_item_move(game, ids[i]);
                }
            } else {
                app->move_item_id = ITEM_ID_NONE;
                app->move_item_box = true;
                app_toggle_tool(app, APP_TOOL_MOVE_ITEM, 0);
            }
        }
        break;
    }
    case THING_PLANT_BOX: plant_box_actions(app, &slot); break;
    case THING_TILE:
    case THING_COUNT: break;
    }
    if ((rows->thing == THING_STRUCTURE || rows->thing == THING_ROOF ||
         rows->thing == THING_SITE) &&
        inspector_build_like_action(app, slot)) {
        slot++;
    }
    /* Looked up again: the site may have just been cancelled. */
    int site = rows->thing == THING_SITE ? construction_at(&game->constructions, x, y, z)
                                         : CONSTRUCTION_NONE;
    const ConstructionSite *found = construction_get(&game->constructions, site);
    int priority;
    if (found != NULL && inspector_priority_action(app, slot++, found->priority, &priority)) {
        construction_set_priority(&game->constructions, site, priority);
    }
    found = construction_get(&game->constructions, site);
    ItemType wooden = found != NULL && found->workstation == 0
                          ? structure_wooden_material((StructureType)found->type)
                          : ITEM_NONE;
    WoodMask woods;
    if (wooden != ITEM_NONE &&
        inspector_wood_action(app, slot, wooden, found->wood_choice, &woods)) {
        construction_set_wood_choice(&game->constructions, site, woods);
    }
}

bool tile_panel_cancel(App *app)
{
    if (!app->tile_selected) {
        return false;
    }
    Game *game = &app->game;
    int x = app->selected_tile_x, y = app->selected_tile_y, z = app->selected_tile_z;
    TilePos tile = {(int16_t)x, (int16_t)y, (int16_t)z};
    switch (app->selected_thing) {
    case THING_SITE:
        game_remove_blueprints_rect(game, x, y, x, y, z);
        app->tile_selected = false;
        return true;
    case THING_STRUCTURE:
    case THING_ROOF: {
        bool ordered = designation_get(&game->designations, x, y, z) == DESIGNATION_DECONSTRUCT;
        if (!ordered && unavailable(game, DESIGNATION_DECONSTRUCT, x, y, z) != NULL) {
            return false;
        }
        toggle_order(app, DESIGNATION_DECONSTRUCT, tile);
        return true;
    }
    case THING_PLANT: {
        TilePos foot;
        TreeRef tree;
        bool is_tree;
        if (thing_plant(app, x, y, z, &foot, &tree, &is_tree) == NULL) {
            return false;
        }
        DesignationType order = designation_get(&game->designations, foot.x, foot.y, foot.z);
        if (order != DESIGNATION_CHOP && order != DESIGNATION_HARVEST &&
            order != DESIGNATION_HARVEST_RIPE && order != DESIGNATION_UPROOT) {
            return false;
        }
        toggle_order(app, order, foot);
        return true;
    }
    case THING_ITEMS: {
        TilePos destination;
        return game_item_move_at(game, x, y, z, &destination) &&
               game_cancel_item_move(game, x, y, z);
    }
    case THING_ITEM: {
        ItemPick pick = app->selected_item;
        ItemId id = game_pick_real_item(game, &pick);
        TilePos destination;
        return game_single_item_move_at(game, id, &destination) &&
               game_cancel_single_item_move(game, id);
    }
    case THING_PLANT_BOX: {
        int cancelled = 0;
        for (int i = 0; i < app->selected_plant_count; i++) {
            TilePos at = app->selected_plants[i];
            DesignationType order = designation_get(&game->designations, at.x, at.y, at.z);
            if (order == DESIGNATION_CHOP || order == DESIGNATION_HARVEST ||
                order == DESIGNATION_HARVEST_RIPE || order == DESIGNATION_UPROOT) {
                cancelled +=
                    game_designate_rect(game, at.x, at.y, at.x, at.y, at.z, DESIGNATION_NONE);
            }
        }
        return cancelled > 0;
    }
    case THING_ITEM_BOX: {
        ItemId ids[APP_BOX_ITEMS_MAX];
        int count = box_real_items(app, ids), cancelled = 0;
        for (int i = 0; i < count; i++) {
            cancelled += game_cancel_single_item_move(game, ids[i]);
        }
        return cancelled > 0;
    }
    case THING_TILE:
    case THING_COUNT: break;
    }
    return false;
}

void tile_panel_draw(App *app)
{
    if (!app->tile_selected) {
        return;
    }
    ThingClick click = {0};
    ThingRows rows = {app,
                      app->selected_thing,
                      app->selected_tile_x,
                      app->selected_tile_y,
                      app->selected_tile_z,
                      &click};
    char title[TEXT_SIZE];
    thing_title(&rows, title, sizeof(title));
    bool open = inspector_box(app, INSPECT_TILE, title, thing_rows, &rows);
    const Corpse *corpse = thing_corpse(&rows);
    if (corpse != NULL) {
        hud_corpse_tabs(app, corpse);
    } else {
        inspector_tabs(app, NULL, 0);
    }
    thing_actions(app, &rows);
    if (!open) {
        app->tile_selected = false;
    } else if (click.item) {
        app_select_item(app, click.pick);
    } else if (click.tile) {
        app_select_tile_items(app, rows.x, rows.y, rows.z);
    } else if (click.carry) {
        game_order_carry(&app->game, click.carrier, click.carried);
    }
}
