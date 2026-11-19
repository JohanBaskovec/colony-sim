#include "game/tile_panel.h"

#include "core/i18n.h"
#include "core/temperature.h"
#include "core/tree.h"
#include "game/hud.h"
#include "game/inspector.h"

#define TEXT_SIZE 256
#define TILE_PANEL_GROUPS 8 /* Kinds of items listed on a heap's tile. */

typedef struct ThingRows {
    App *app;
    TileThing thing;
    int x, y, z;
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

/* What the player may forbid of the thing (FORBID_TARGET_COUNT: nothing). */
static ForbidTarget thing_forbid_target(TileThing thing)
{
    switch (thing) {
    case THING_STRUCTURE: return FORBID_STRUCTURE;
    case THING_ROOF: return FORBID_ROOF;
    case THING_ITEMS: return FORBID_ITEMS;
    case THING_PLANT: return FORBID_PLANT;
    case THING_SITE: return FORBID_SITE;
    case THING_ITEM: /* (One item: see thing_actions().) */
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
            SDL_strlcpy(out, tr(item_def((ItemType)stack.type)->name_key), size);
        } else { /* Only items lying where they fell: the newest one's name. */
            const Item *item = items_first(game_view_items(game), map_index(map, x, y, z));
            SDL_strlcpy(out, item != NULL ? tr(item_def((ItemType)item->type)->name_key) : "",
                        size);
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
            SDL_strlcpy(out, tr(item_def((ItemType)item->type)->name_key), size);
        }
        break;
    }
    case THING_COUNT: break;
    }
}

/* What there is to know of one item: its weight, quality and condition, when
 * it rots away, whether it lies where it fell, and what else lies with it. */
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
        hud_rot_text(
            text, sizeof(text),
            item_stack_rot_hours(one, game_item_rot_rate(game, rows->x, rows->y, rows->z)));
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
        ui_layout_label(layout, text, 0.0f, true);
    }
    if (item->forbidden) {
        ui_layout_label(layout, tr("tile_panel.left_alone"), 0.0f, true);
    }
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
    float rate = game_item_rot_rate(game, rows->x, rows->y, rows->z);
    int shown = count < ITEM_LIST_LINES ? count : ITEM_LIST_LINES;
    for (int i = 0; i < shown; i++) {
        ItemStack one = item_as_stack(list[i]);
        char condition[TEXT_SIZE], line[TEXT_SIZE];
        const char *name = tr(item_def((ItemType)one.type)->name_key);
        if (!hud_item_condition_text(condition, sizeof(condition), one)) {
            SDL_strlcpy(condition, "", sizeof(condition));
        }
        if (item_rots((ItemType)one.type)) {
            char rot[TEXT_SIZE];
            hud_rot_text(rot, sizeof(rot), item_stack_rot_hours(one, rate));
            TR_FORMAT(line, sizeof(line), tr("tile_panel.item_line_rot"), name, condition, rot);
        } else if (condition[0] != '\0') {
            TR_FORMAT(line, sizeof(line), tr("tile_panel.item_line"), name, condition);
        } else {
            SDL_strlcpy(line, name, sizeof(line));
        }
        ui_layout_label(layout, line, 0.0f, list[i]->forbidden); /* Forbidden: dimmed. */
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
    if (game->fog_on && !game_tile_seen(game, x, y, z)) {
        ui_layout_label(layout, tr("hud.tile.remembered"), 0.0f, true);
    }
    switch (rows->thing) {
    case THING_STRUCTURE: {
        StructureType structure = map_structure(map, x, y, z);
        if (hud_structure_condition_text(text, sizeof(text), map, x, y, z)) {
            ui_layout_label(layout, text, 0.0f, false);
        }
        if (structure_is_bed(structure)) {
            hud_bed_owner_text(rows->app, text, sizeof(text), x, y, z);
            ui_layout_label(layout, text, 0.0f, false);
        }
        order_row(layout, game, x, y, z);
        break;
    }
    case THING_ROOF: {
        char quality[16];
        hud_quality_text(quality, sizeof(quality), map_roof_quality(map, x, y, z));
        TR_FORMAT(text, sizeof(text), tr("hud.item_quality"), quality);
        ui_layout_label(layout, text, 0.0f, false);
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
                TR_FORMAT(text, sizeof(text), tr("tile_panel.group"),
                          tr(item_def((ItemType)groups[i].type)->name_key), count, weight);
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
                app_toggle_tool(app, APP_TOOL_MOVE_ITEM, 0);
            }
        }
        break;
    }
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
    if (found != NULL && inspector_priority_action(app, slot, found->priority, &priority)) {
        construction_set_priority(&game->constructions, site, priority);
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
            order != DESIGNATION_UPROOT) {
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
    case THING_COUNT: break;
    }
    return false;
}

void tile_panel_draw(App *app)
{
    if (!app->tile_selected) {
        return;
    }
    ThingRows rows = {app, app->selected_thing, app->selected_tile_x, app->selected_tile_y,
                      app->selected_tile_z};
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
    }
}
