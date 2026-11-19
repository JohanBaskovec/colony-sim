#include "game/tile_panel.h"

#include "core/i18n.h"
#include "core/temperature.h"
#include "core/tree.h"
#include "game/hud.h"
#include "game/inspector.h"

#define TEXT_SIZE 256

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

/* The record of the settler whose body the window of some items is about
 * (core/corpse.h), NULL for anything else. */
static const Corpse *thing_corpse(const ThingRows *rows)
{
    if (rows->thing != THING_ITEMS) {
        return NULL;
    }
    const Game *game = &rows->app->game;
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
        if (corpse != NULL && corpse->kind == CORPSE_SETTLER) {
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
    case THING_COUNT: break;
    }
}

/* Items of a tile listed in the inspector at most. */
#define ITEM_LIST_LINES 40

/* The items lying on the tile one by one, in the order chosen with the
 * buttons above them (quality, condition, rot). */
static void item_list_rows(UiLayout *layout, const ThingRows *rows)
{
    App *app = rows->app;
    const Game *game = &app->game;
    const Items *items = game_items_at(game, rows->x, rows->y, rows->z);
    size_t index = map_index(game_view_map(game), rows->x, rows->y, rows->z);
    const Item *list[ITEM_LIST_LINES];
    int count = items_list(items, index, app->items_order, list, ITEM_LIST_LINES);
    if (count < 2) {
        return;
    }
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
        /* Dim: lying where it fell (a wild item, left to gathering). */
        ui_layout_label(layout, line, 0.0f, (list[i]->flags & ITEM_FLAG_WILD) != 0);
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
        if (stack.type != ITEM_NONE) {
            tr_integer(count, sizeof(count), stack.count);
            hud_weight_text(weight, sizeof(weight), item_stack_mass(stack));
            TR_FORMAT(text, sizeof(text), tr("tile_panel.count"), count, weight);
            ui_layout_label(layout, text, 0.0f, false);
            if (hud_item_condition_text(text, sizeof(text), stack)) {
                ui_layout_label(layout, text, 0.0f, false);
            }
        }
        item_list_rows(layout, rows);
        break;
    }
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
            break; /* Only items lying where they fell: gathering takes them. */
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
