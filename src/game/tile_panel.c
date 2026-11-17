#include "game/tile_panel.h"

#include "core/i18n.h"
#include "core/tree.h"
#include "game/inspector.h"

#define TEXT_SIZE 128

/* The things a tile window lists, top to bottom (stations have their own
 * window, game/station_panel.h). */
static const ForbidTarget LISTED[] = {FORBID_STRUCTURE, FORBID_ROOF, FORBID_ITEMS, FORBID_PLANT};

typedef struct TileRows {
    App *app;
    int x, y, z;
    /* What the player clicked, applied once the window is drawn. */
    int clicked; /* Index in LISTED, or -1. */
} TileRows;

/* The name of a thing on the tile ("Branches (4)" for items). */
static void thing_name(const App *app, int x, int y, int z, ForbidTarget target, char *out,
                       size_t size)
{
    const Game *game = &app->game;
    const Map *map = game_view_map(game);
    out[0] = '\0';
    switch (target) {
    case FORBID_STRUCTURE:
        SDL_strlcpy(out, tr(structure_def(map_structure(map, x, y, z))->name_key), size);
        break;
    case FORBID_ROOF:
        SDL_strlcpy(out, tr(structure_def(map_roof(map, x, y, z))->name_key), size);
        break;
    case FORBID_ITEMS: {
        ItemStack stack = items_get(game_view_items(game), x, y, z);
        char count[16];
        tr_integer(count, sizeof(count), stack.count);
        TR_FORMAT(out, size, tr("tile_panel.items"), tr(item_def((ItemType)stack.type)->name_key),
                  count);
        break;
    }
    case FORBID_PLANT: {
        const Plant *plant = &map->plants[map_index(map, x, y, z)];
        TreeRef tree;
        if (plant->type == PLANT_NONE && tree_at(map, x, y, z, &tree)) {
            plant = tree_plant(map, &tree);
        }
        SDL_strlcpy(out, tr(plant_def((PlantType)plant->type)->name_key), size);
        break;
    }
    case FORBID_STATION:
    case FORBID_TARGET_COUNT: break;
    }
}

static void tile_rows(UiLayout *layout, void *userdata)
{
    TileRows *rows = userdata;
    const Game *game = &rows->app->game;
    int x = rows->x, y = rows->y, z = rows->z;
    bool any = false;
    for (int i = 0; i < (int)SDL_arraysize(LISTED); i++) {
        ForbidTarget target = LISTED[i];
        if (!game_can_forbid(game, x, y, z, target)) {
            continue;
        }
        any = true;
        char name[TEXT_SIZE];
        thing_name(rows->app, x, y, z, target, name, sizeof(name));
        bool forbidden = game_forbidden(game, x, y, z, target);
        if (ui_layout_label_with_button(layout, name,
                                        tr(forbidden ? "forbid.allow" : "forbid.forbid"))) {
            rows->clicked = i;
        }
        if (forbidden) {
            StructureType structure = map_structure(game_view_map(game), x, y, z);
            bool locked = target == FORBID_STRUCTURE &&
                          (structure == STRUCTURE_DOOR || structure == STRUCTURE_LADDER);
            ui_layout_label(layout, tr(locked ? "tile_panel.locked" : "tile_panel.left_alone"),
                            UI_LAYOUT_INDENT, true);
        }
    }
    const ConstructionSite *site =
        construction_get(&game->constructions, construction_at(&game->constructions, x, y, z));
    if (site != NULL) {
        char text[TEXT_SIZE];
        TR_FORMAT(text, sizeof(text), tr("tile_panel.site"), tr(construction_name_key(site)));
        ui_layout_label(layout, text, 0.0f, false);
        any = true;
    }
    if (!any) {
        ui_layout_label(layout, tr("tile_panel.nothing"), 0.0f, true);
    }
}

void tile_panel_draw(App *app)
{
    if (!app->tile_selected) {
        return;
    }
    TileRows rows = {app, app->selected_tile_x, app->selected_tile_y, app->selected_tile_z, -1};
    char a[16], b[16], title[TEXT_SIZE];
    tr_integer(a, sizeof(a), rows.x);
    tr_integer(b, sizeof(b), rows.y);
    TR_FORMAT(title, sizeof(title), tr("tile_panel.title"), a, b);
    bool open = inspector_box(app, INSPECT_TILE, title, tile_rows, &rows);
    inspector_tabs(app, NULL, 0);
    inspector_build_like_action(app, 0);
    if (rows.clicked >= 0) {
        ForbidTarget target = LISTED[rows.clicked];
        game_set_forbidden(&app->game, rows.x, rows.y, rows.z, target,
                           !game_forbidden(&app->game, rows.x, rows.y, rows.z, target));
    }
    if (!open) {
        app->tile_selected = false;
    }
}
