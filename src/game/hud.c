#include "game/hud.h"

#include "core/i18n.h"
#include "core/tree.h"
#include "game/debug_panel.h"
#include "game/inspector.h"
#include "game/items_panel.h"
#include "game/layout.h"
#include "game/gather_zone_panel.h"
#include "game/bottom_bar.h"
#include "game/build_menu.h"
#include "game/colonist_bar.h"
#include "game/game_menu.h"
#include "game/station_panel.h"
#include "game/ui_layout.h"
#include "game/stockpile_panel.h"
#include "game/tile_panel.h"
#include "game/view_controls.h"
#include "game/work_panel.h"

#include <limits.h>

#define HUD_MARGIN 8.0f
#define HUD_PADDING 6.0f
#define HUD_MAX_LINES 64
#define HUD_LINE_SIZE 256

#define THEME_COLOR(app, role) ((app)->ui.theme.colors[(role)])

/* Colonist names are drawn under them from this zoom level (pixels per tile). */
#define HUD_LABEL_MIN_ZOOM 10.0f
/* Item stack sizes are drawn from this zoom level. */
#define HUD_ITEM_COUNT_MIN_ZOOM 28.0f

typedef struct Panel {
    char lines[HUD_MAX_LINES][HUD_LINE_SIZE];
    /* KnowledgeType + 1 of a line that has a knowledge's tooltip, else 0. */
    uint8_t tip[HUD_MAX_LINES];
    int count;
} Panel;

/* Returns the buffer of a new line (the last one is reused if the panel is full). */
static char *panel_add(Panel *panel)
{
    if (panel->count < HUD_MAX_LINES) {
        panel->count++;
    }
    char *line = panel->lines[panel->count - 1];
    line[0] = '\0';
    panel->tip[panel->count - 1] = 0;
    return line;
}

static void panel_add_text(Panel *panel, const char *text)
{
    SDL_strlcpy(panel_add(panel), text, HUD_LINE_SIZE);
}

static void panel_size(App *app, const Panel *panel, float *width, float *height)
{
    int max_width = 0;
    for (int i = 0; i < panel->count; i++) {
        int w, h;
        text_measure(app->text, panel->lines[i], &w, &h);
        max_width = SDL_max(max_width, w);
    }
    *width = (float)max_width + 2.0f * HUD_PADDING;
    *height = (float)(panel->count * text_line_height(app->text)) + 2.0f * HUD_PADDING;
}

/* Text panel that does not catch clicks, its lines aligned left or right. */
static void panel_draw_aligned(App *app, const Panel *panel, float x, float y, SDL_Color color,
                               bool right)
{
    if (panel->count == 0) {
        return;
    }
    float width, height;
    panel_size(app, panel, &width, &height);
    SDL_FRect background = {x, y, width, height};
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(app->renderer, &background);

    float line_height = (float)text_line_height(app->text);
    for (int i = 0; i < panel->count; i++) {
        float tx = x + HUD_PADDING;
        if (right) {
            int w, h;
            text_measure(app->text, panel->lines[i], &w, &h);
            tx = x + width - HUD_PADDING - (float)w;
        }
        text_draw(app->text, panel->lines[i], tx, y + HUD_PADDING + (float)i * line_height,
                  THEME_COLOR(app, THEME_TEXT));
    }
}

static void panel_draw_colored(App *app, const Panel *panel, float x, float y, SDL_Color color)
{
    panel_draw_aligned(app, panel, x, y, color, false);
}

static void panel_draw(App *app, const Panel *panel, float x, float y)
{
    panel_draw_colored(app, panel, x, y, THEME_COLOR(app, THEME_HUD_PANEL));
}

/* The lines of a panel as the rows of a window (game/ui_layout.h). */
static void panel_rows(UiLayout *layout, void *userdata)
{
    const Panel *panel = userdata;
    for (int i = 0; i < panel->count; i++) {
        ui_layout_label(layout, panel->lines[i], 0.0f, false);
    }
}

/* Names of the keys at the physical W/A/S/D positions ("ZQSD" on AZERTY keyboards). */
static void movement_keys_label(char *out, size_t size)
{
    static const SDL_Scancode SCANCODES[] = {APP_KEY_UP, APP_KEY_LEFT, APP_KEY_DOWN, APP_KEY_RIGHT};
    out[0] = '\0';
    for (size_t i = 0; i < SDL_arraysize(SCANCODES); i++) {
        SDL_strlcat(out, app_key_name(SCANCODES[i]), size);
    }
}

/* "21 kg", "0.3 kg": one decimal only for small, non-whole weights. */
static void format_weight(char *out, size_t size, float kilograms)
{
    char number[32];
    float rounded = SDL_roundf(kilograms);
    int decimals = (kilograms < 10.0f && SDL_fabsf(kilograms - rounded) > 0.05f) ? 1 : 0;
    tr_decimal(number, sizeof(number), decimals ? kilograms : rounded, decimals);
    TR_FORMAT(out, size, tr("unit.kg"), number);
}

/* "Quality: fine · Hit points: 45/60 (75%)" line of an item stack, with only
 * the parts its type has (none for stone blocks, no quality for grass). */
static void add_item_condition_line(Panel *panel, ItemStack stack)
{
    const ItemDef *def = item_def((ItemType)stack.type);
    if (def == NULL || stack.type == ITEM_NONE || (!def->has_quality && !def->damageable)) {
        return;
    }
    const char *quality = tr(item_quality_name_key((ItemQuality)stack.quality));
    if (!def->damageable) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.item_quality"), quality);
        return;
    }
    char hit_points[16], max_hit_points[16], number[16], percent[32];
    tr_integer(hit_points, sizeof(hit_points), stack.hit_points);
    tr_integer(max_hit_points, sizeof(max_hit_points), def->max_hit_points);
    tr_integer(number, sizeof(number), item_stack_hit_points_percent(stack));
    TR_FORMAT(percent, sizeof(percent), tr("unit.percent"), number);
    if (!def->has_quality) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.item_hit_points"), hit_points,
                  max_hit_points, percent);
        return;
    }
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.item_condition"), quality, hit_points,
              max_hit_points, percent);
}

/* Grade and hit points of the structure built on a tile. */
static void add_structure_condition_line(Panel *panel, const Map *map, int x, int y, int z)
{
    StructureType type = map_structure(map, x, y, z);
    ItemQuality grade = map_structure_quality(map, x, y, z);
    int max = structure_max_hit_points(type, grade);
    if (max <= 0) {
        return;
    }
    int current = map_structure_hit_points(map, x, y, z);
    char hit_points[16], max_hit_points[16], number[16], percent[32];
    tr_integer(hit_points, sizeof(hit_points), current);
    tr_integer(max_hit_points, sizeof(max_hit_points), max);
    tr_integer(number, sizeof(number), (long long)current * 100 / max);
    TR_FORMAT(percent, sizeof(percent), tr("unit.percent"), number);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.item_condition"),
              tr(item_quality_name_key(grade)), hit_points, max_hit_points, percent);
}

/* Owner of a bed (the settler's id is its slot + 1, core/bed.h). */
static const Colonist *bed_owner(const Game *game, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(&game->map, x, y, z, &origin_x, &origin_y)) {
        return NULL;
    }
    uint8_t owner = beds_owner(&game->beds, origin_x, origin_y, z);
    if (owner == BED_NO_OWNER) {
        return NULL;
    }
    return game_colonist_const(game, game_colonist_id_at(game, (uint32_t)owner - 1));
}

static void add_bed_owner_line(const App *app, Panel *panel, int x, int y)
{
    const int z = app->camera.level; /* The level shown. */
    const Colonist *owner = bed_owner(&app->game, x, y, z);
    if (owner != NULL) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.bed_owner"), owner->name);
    } else {
        panel_add_text(panel, tr("hud.tile.bed_free"));
    }
}

/* "12.5 °C", with `decimals` decimals. */
static void temperature_text(char *out, size_t size, float celsius, int decimals)
{
    char number[16];
    tr_decimal(number, sizeof(number), celsius, decimals);
    TR_FORMAT(out, size, tr("unit.celsius"), number);
}

static void build_status_panel(App *app, Panel *panel)
{
    const GameClock *clock = &app->game.clock;
    GameDate date = game_date_from_tick(clock->tick);

    char day[16], year[16], hours[8], minutes[8];
    tr_integer(day, sizeof(day), date.day);
    tr_integer(year, sizeof(year), date.year);
    SDL_snprintf(hours, sizeof(hours), "%02d", date.hour);
    SDL_snprintf(minutes, sizeof(minutes), "%02d", date.minute);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.date"), day,
              tr(season_name_key(date.season)), year, hours, minutes);

    if (clock->speed == GAME_SPEED_PAUSED) {
        panel_add_text(panel, tr("hud.paused"));
    }
    char outdoor[32];
    temperature_text(outdoor, sizeof(outdoor), game_outdoor_temperature(&app->game), 0);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.outdoor_temperature"), outdoor);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.weather"),
              tr(weather_name_key(&app->game.weather, clock->tick)));
    long long snow = (long long)SDL_lroundf(app->game.weather.snow * 100.0f);
    if (snow > 0) {
        char percent[16];
        tr_integer(percent, sizeof(percent), snow);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.snow_cover"), percent);
    }
    if (app->show_temperature) {
        char cold[32], hot[32];
        temperature_text(cold, sizeof(cold), -10.0f, 0);
        temperature_text(hot, sizeof(hot), 30.0f, 0);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.temperature_view"), cold, hot,
                  app_key_name(APP_KEY_TEMPERATURE));
    }
}

/* A construction site's lines in the tile inspector: progress and materials. */
static void add_site_lines(Panel *panel, const ConstructionSite *site)
{
    if (site == NULL) {
        return;
    }
    char a[32], b[32];
    tr_integer(a, sizeof(a), (long long)(construction_work_fraction(site) * 100.0f));
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE,
              tr(site->refresh ? "hud.tile.refresh" : "hud.tile.blueprint"),
              tr(construction_name_key(site)), a);
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        ItemType material = construction_material(site, slot);
        if (material == ITEM_NONE) {
            continue;
        }
        tr_integer(a, sizeof(a), site->delivered[slot].count);
        tr_integer(b, sizeof(b), construction_material_count(site, slot));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.material"),
                  tr(item_def(material)->name_key), a, b);
    }
}

static void build_tile_panel(App *app, Panel *panel)
{
    const int z = app->camera.level; /* The level shown. */
    const Map *map = &app->game.map;
    int x = app->hover_x, y = app->hover_y;
    size_t index = map_index(map, x, y, z);
    char a[32], b[32];

    tr_integer(a, sizeof(a), x);
    tr_integer(b, sizeof(b), y);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.position"), a, b);

    const TerrainDef *terrain = terrain_def((Terrain)map->terrain[index]);
    /* Up a trunk, no terrain to speak of. */
    if (map->terrain[index] != TERRAIN_OPEN || map->rock[index] != ROCK_WOOD) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.terrain"), tr(terrain->name_key));
    }
    if (map_is_slope(map, x, y, z)) {
        panel_add_text(panel, tr("hud.tile.slope"));
    }

    if (map->rock[index] == ROCK_WOOD) { /* Not a rock wall: a tree's trunk. */
        panel_add_text(panel, tr(rock_def(ROCK_WOOD)->name_key));
    } else if (map->rock[index] != ROCK_NONE) {
        const RockDef *rock = rock_def((RockType)map->rock[index]);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.rock"), tr(rock->name_key));
    }

    /* On a tree's trunk or foliage: the tree (its plant is on its foot). */
    const Plant *plant = &map->plants[index];
    TreeRef tree;
    if (tree_at(map, x, y, z, &tree)) {
        plant = tree_plant(map, &tree);
        int levels = tree_shape_levels(&tree.shape);
        tr_integer(a, sizeof(a), levels);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr_plural("hud.tile.tree_levels", levels), a);
    }
    if (plant->type != PLANT_NONE) {
        const PlantDef *def = plant_def((PlantType)plant->type);
        if (plant_is_dormant(plant)) {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.plant_dormant"),
                      tr(def->name_key));
        } else {
            tr_integer(a, sizeof(a), (long long)(plant->growth * 100.0f + 0.5f));
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.plant"), tr(def->name_key), a);
        }
        if (game_forbidden(&app->game, x, y, z, FORBID_PLANT)) {
            panel_add_text(panel, tr("tile_panel.left_alone"));
        }
        int today = game_day_of_year(app->game.clock.tick);
        for (int stock = 0; stock < PLANT_STOCK_COUNT; stock++) {
            if (plant_stock_capacity(plant, (PlantStock)stock) <= 0) {
                continue;
            }
            const char *item =
                tr(item_def(plant_stock_item((PlantType)plant->type, (PlantStock)stock))->name_key);
            if (stock == PLANT_STOCK_FOOD && !plant_days_contain(def->food_days, today)) {
                TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.plant_food_out_of_season"),
                          item);
                continue;
            }
            tr_integer(a, sizeof(a), plant_stock_available(plant, (PlantStock)stock));
            tr_integer(b, sizeof(b), plant_stock_capacity(plant, (PlantStock)stock));
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.plant_stock"), item, a, b);
        }
    }

    ItemStack stack = items_get(&app->game.items, x, y, z);
    if (stack.type != ITEM_NONE) {
        char weight[32];
        tr_integer(a, sizeof(a), stack.count);
        format_weight(weight, sizeof(weight), item_stack_weight((ItemType)stack.type, stack.count));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.items"),
                  tr(item_def((ItemType)stack.type)->name_key), a, weight);
        add_item_condition_line(panel, stack);
        if (items_forbidden(&app->game.items, x, y, z)) {
            panel_add_text(panel, tr("tile_panel.left_alone"));
        }
        const ItemDef *def = item_def((ItemType)stack.type);
        if (def->transforms_into != ITEM_NONE && def->transform_hours > stack.progress) {
            tr_integer(a, sizeof(a), def->transform_hours - stack.progress);
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.item_transform"),
                      tr(item_def(def->transforms_into)->name_key), a);
        }
        if (item_rots((ItemType)stack.type)) {
            /* At the tile's temperature now: an estimate. */
            float hours = item_stack_rot_hours(stack, game_item_rot_rate(&app->game, x, y, z));
            if (hours < 0.0f) {
                panel_add_text(panel, tr("hud.tile.item_frozen"));
            } else if (hours < 48.0f) {
                tr_integer(a, sizeof(a), (long long)hours + 1);
                TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.item_rot_hours"), a);
            } else {
                long long days = (long long)(hours / 24.0f + 0.5f);
                tr_integer(a, sizeof(a), days);
                TR_FORMAT(panel_add(panel), HUD_LINE_SIZE,
                          tr_plural("hud.tile.item_rot_days", days), a);
            }
        }
    }

    const Workstation *station = workstation_get_const(
        &app->game.workstations, workstation_at(&app->game.workstations, x, y, z));
    if (station != NULL) {
        const char *name = tr(workstation_def((WorkstationType)station->type)->name_key);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.station"), name);
        if (station->forbidden) {
            panel_add_text(panel, tr("tile_panel.left_alone"));
        }
    }
    StructureType structure = map_structure(map, x, y, z);
    if (structure != STRUCTURE_NONE) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.structure"),
                  tr(structure_def(structure)->name_key));
        add_structure_condition_line(panel, map, x, y, z);
        if (map_forbidden(map, x, y, z, MAP_FORBID_STRUCTURE)) {
            bool locked = structure == STRUCTURE_DOOR || structure == STRUCTURE_LADDER;
            panel_add_text(panel, tr(locked ? "tile_panel.locked" : "tile_panel.left_alone"));
        }
        if (structure_is_bed(structure)) {
            add_bed_owner_line(app, panel, x, y);
        }
    }
    StructureType roof = map_roof(map, x, y, z); /* The tile is a roof. */
    StructureType over = map_roof_over(map, x, y, z);
    if (roof != STRUCTURE_NONE) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.roof"),
                  tr(structure_def(roof)->name_key),
                  tr(item_quality_name_key(map_roof_quality(map, x, y, z))));
        if (map_forbidden(map, x, y, z, MAP_FORBID_ROOF)) {
            panel_add_text(panel, tr("tile_panel.left_alone"));
        }
    } else if (over != STRUCTURE_NONE) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.under_roof"),
                  tr(structure_def(over)->name_key));
    } else if (map_natural_roof(map, x, y, z)) {
        panel_add_text(panel, tr("hud.tile.natural_roof"));
    }
    char celsius[32], felt[32];
    float air = temperature_at(&app->game.temperatures, x, y, z);
    float radiant = hearths_radiant_warmth(&app->game.workstations, map, x, y, z);
    temperature_text(celsius, sizeof(celsius), air, 1);
    if (radiant >= 0.05f) {
        temperature_text(felt, sizeof(felt), air + radiant, 1);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.temperature_felt"), celsius, felt);
    } else {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.temperature"), celsius);
    }
    const ConstructionSite *site = construction_get(
        &app->game.constructions, construction_at(&app->game.constructions, x, y, z));
    add_site_lines(panel, site);

    if (site != NULL && gather_clear_work(map, x, y, z) > 0.0f) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("blueprint.clear_first"),
                  tr(plant_def((PlantType)map->plants[index].type)->name_key));
    }

    /* Stones in the ground: what gathering stones may still find. */
    if (gather_stone_ground(map, x, y, z)) {
        static const ItemType STONES[] = {ITEM_COBBLE, ITEM_FLINT};
        bool any = false;
        for (size_t i = 0; i < SDL_arraysize(STONES); i++) {
            int count = map_stones(map, x, y, z, STONES[i]);
            if (count > 0) {
                tr_integer(a, sizeof(a), count);
                TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.ground_stones"),
                          tr(item_def(STONES[i])->name_key), a);
                any = true;
            }
        }
        if (!any) {
            panel_add_text(panel, tr("hud.tile.no_stones"));
        }
    }

    /* Branches, twigs and nuts the trees shed, on the ground or on their foliage. */
    static const ItemType LITTER[] = {ITEM_BRANCHES, ITEM_TWIGS, ITEM_HAZELNUTS};
    for (size_t i = 0; i < SDL_arraysize(LITTER); i++) {
        int count = map_litter(map, x, y, z, LITTER[i]);
        if (count > 0) {
            tr_integer(a, sizeof(a), count);
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.litter"),
                      tr(item_def(LITTER[i])->name_key), a);
        }
    }

    const Stockpile *stockpile =
        stockpile_get(&app->game.stockpiles, stockpile_at(&app->game.stockpiles, x, y, z));
    if (stockpile != NULL) {
        char name[HUD_LINE_SIZE];
        tr_integer(a, sizeof(a), stockpile->number);
        TR_FORMAT(name, sizeof(name), tr("stockpile.name"), a);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.stockpile"), name);
    }
    const GatherZone *gather =
        gather_zone_get(&app->game.gather_zones, gather_zone_at(&app->game.gather_zones, x, y, z));
    if (gather != NULL) {
        char name[HUD_LINE_SIZE];
        tr_integer(a, sizeof(a), gather->number);
        TR_FORMAT(name, sizeof(name), tr("gather_zone.name"), a);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.stockpile"), name);
    }
    GatherWorkInfo work;
    if (game_gather_work_at(&app->game, x, y, z, &work)) {
        const char *activity = tr(gather_job_def(work.job)->activity_key);
        tr_integer(a, sizeof(a), (long long)(work.fraction * 100.0f));
        const Colonist *worker = game_colonist_const(&app->game, work.worker);
        if (worker != NULL) {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.gather_worker"), worker->name,
                      activity, a);
        } else {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.gather_left_off"), activity, a);
        }
    }

    DesignationType designation = designation_get(&app->game.designations, x, y, z);
    if (designation != DESIGNATION_NONE) {
        const char *name = tr(designation_def(designation)->name_key);
        float progress = designation_progress_fraction(&app->game.designations, map, x, y, z);
        if (progress > 0.0f) {
            tr_integer(a, sizeof(a), (long long)(progress * 100.0f));
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.designation_progress"), name,
                      a);
        } else {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.designation"), name);
        }
        if (designation_waits_for_support(&app->game.designations, map, x, y, z)) {
            panel_add_text(panel, tr("hud.tile.needs_support"));
        }
    }

    int dirt = dirt_at(&app->game.dirt, x, y, z);
    if (dirt > 0) {
        tr_integer(a, sizeof(a), dirt);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.dirt"), a);
    }

    float cost = map_move_cost(map, x, y, z);
    if (cost <= 0.0f) {
        panel_add_text(panel, tr("hud.tile.impassable"));
    } else {
        tr_decimal(a, sizeof(a), cost, 2);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.move_cost"), a);
    }
}

static void build_debug_panel(App *app, Panel *panel)
{
    char value[32];

    tr_integer(value, sizeof(value), (long long)(app->fps + 0.5f));
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.debug.fps"), value);

    tr_decimal(value, sizeof(value), app->render_ms, 1);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.debug.render_time"), value);

    tr_integer(value, sizeof(value), (long long)app->game.clock.tick);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.debug.tick"), value);

    /* Seeds are identifiers: shown raw, without thousands separators. */
    SDL_snprintf(value, sizeof(value), "%" SDL_PRIu64, app->game.seed);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.debug.seed"), value);

    tr_decimal(value, sizeof(value), app->camera.zoom, 1);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.debug.zoom"), value);

    size_t trees = app->debug_tree_count;
    tr_integer(value, sizeof(value), (long long)trees);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr_plural("hud.debug.trees", (long long)trees),
              value);
}

static void add_need_line(Panel *panel, const Colonist *colonist, NeedType need)
{
    float value = colonist->needs[need];
    char percent[16];
    tr_integer(percent, sizeof(percent), (long long)(value * 100.0f + 0.5f));
    const char *state_key = need_level_key(need, need_level(value));
    if (state_key == NULL) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.need"), tr(need_name_key(need)),
                  percent);
    } else {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.need_state"),
                  tr(need_name_key(need)), percent, tr(state_key));
    }
}

/* A whole number with its sign ("+6", "-8"). */
static void format_signed(char *out, size_t size, long long value)
{
    char number[16];
    tr_integer(number, sizeof(number), value);
    SDL_snprintf(out, size, "%s%s", value > 0 ? "+" : "", number);
}

/* Whole hours left of `ticks`, rounded up. */
static int hours_left(int ticks)
{
    return (ticks + TICKS_PER_HOUR - 1) / TICKS_PER_HOUR;
}

/* The mood and what it does, a break under way, then every thought with what
 * it weighs now (and, fading, the most it weighed). */
static void add_mood_lines(Panel *panel, const Colonist *colonist)
{
    const Mood *mood = &colonist->mood;
    char value[16];
    tr_integer(value, sizeof(value), SDL_lroundf(mood->value));
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.mood"), value,
              tr(mood_level_key(mood->value)));
    char work[16], learn[16], level[16];
    tr_decimal(work, sizeof(work), mood_work_factor(mood->value), 2);
    tr_decimal(learn, sizeof(learn), (float)mood_learn_factor(mood->value), 2);
    format_signed(level, sizeof(level), SDL_lround(mood_level_offset(mood->value)));
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.mood_effects"), work, learn, level);
    const char *break_key = mood_break_key((MoodBreak)mood->break_type);
    if (break_key != NULL) {
        int left = hours_left(mood->break_ticks);
        char hours[16];
        tr_integer(hours, sizeof(hours), left);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr_plural("hud.colonist.break", left),
                  tr(break_key), hours);
    }
    for (int type = 0; type < THOUGHT_COUNT; type++) {
        ThoughtType thought = (ThoughtType)type;
        if (!mood_has(mood, thought)) {
            continue;
        }
        const ThoughtDef *def = thought_def(thought);
        char amount[16];
        format_signed(amount, sizeof(amount), SDL_lroundf(mood_thought_value(mood, thought)));
        if (mood_fading(mood, thought)) {
            char peak[16];
            format_signed(peak, sizeof(peak), SDL_lroundf(mood->peak[type]));
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.thought_fading"), amount,
                      tr(def->name_key), peak);
        } else {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.thought"), amount,
                      tr(def->name_key));
        }
    }
}

/* A knowledge level as shown: one decimal, rounded down. */
static void format_level(char *out, size_t size, double level)
{
    tr_decimal(out, size, (float)(SDL_floor(level * 10.0) / 10.0), 1);
}

/* Who the colonist learns from, teaches, or chats with. */
static void add_social_lines(App *app, Panel *panel, const Colonist *colonist, EntityId id)
{
    const Colonist *mentor = game_colonist_const(&app->game, colonist->mentor);
    const KnowledgeDef *def = knowledge_def((KnowledgeType)colonist->assist_knowledge);
    if (colonist->task == TASK_ASSIST && mentor != NULL && def != NULL) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.apprentice"), mentor->name,
                  tr(def->name_key));
    }
    const KnowledgeDef *story = knowledge_def((KnowledgeType)colonist->story_topic);
    if (colonist->task == TASK_STORY && story != NULL) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.telling"), tr(story->name_key));
    }
    if (colonist->task == TASK_LISTEN && mentor != NULL) {
        const KnowledgeDef *told = knowledge_def((KnowledgeType)mentor->story_topic);
        if (mentor->task == TASK_STORY && told != NULL) {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.listening"), mentor->name,
                      tr(told->name_key));
        }
    }
    if (colonist->ember_ticks > 0) {
        SDL_strlcpy(panel_add(panel), tr("hud.colonist.embers"), HUD_LINE_SIZE);
    }
    const Colonist *apprentice =
        game_colonist_const(&app->game, game_apprentice_of(&app->game, id));
    if (apprentice != NULL) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.teaching"), apprentice->name);
    }
    const Colonist *partner = game_colonist_const(&app->game, colonist->chat_partner);
    if (colonist->chat_ticks > 0 && partner != NULL) {
        const KnowledgeDef *topic = knowledge_def((KnowledgeType)colonist->chat_topic);
        if (topic != NULL) {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.chatting_about"),
                      partner->name, tr(topic->name_key));
        } else {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.chatting"), partner->name);
        }
    }
}

/* Tooltip of a knowledge line of the colonist inspector: best level, what
 * was learned today and how, practice and fading, masters in the colony. */
static void knowledge_tooltip(App *app, const Colonist *colonist, KnowledgeType knowledge)
{
    static const char *const GAIN_KEYS[KNOWLEDGE_SOURCE_COUNT] = {
        [KNOWLEDGE_SOURCE_DOING] = N_("hud.knowledge.gain.doing"),
        [KNOWLEDGE_SOURCE_APPRENTICESHIP] = N_("hud.knowledge.gain.apprenticeship"),
        [KNOWLEDGE_SOURCE_CONVERSATION] = N_("hud.knowledge.gain.conversation"),
        [KNOWLEDGE_SOURCE_STORIES] = N_("hud.knowledge.gain.stories"),
    };
    char lines[UI_TOOLTIP_LINES][HUD_LINE_SIZE];
    const char *pointers[UI_TOOLTIP_LINES];
    int count = 0;
    uint64_t tick = app->game.clock.tick;
    char a[32], b[32];
    double peak = colonist->knowledge_peak[knowledge];
    SDL_strlcpy(lines[count++], tr(knowledge_def(knowledge)->name_key), HUD_LINE_SIZE);
    if (peak - colonist->knowledge[knowledge] >= 0.05) {
        format_level(a, sizeof(a), peak);
        TR_FORMAT(lines[count++], HUD_LINE_SIZE, tr("hud.knowledge.peak"), a);
    }
    bool gained = false;
    for (int source = 0; source < KNOWLEDGE_SOURCE_COUNT; source++) {
        double gain =
            colonist_knowledge_gain_today(colonist, knowledge, (KnowledgeSource)source, tick);
        if (gain >= 0.005) {
            tr_decimal(a, sizeof(a), (float)gain, 2);
            TR_FORMAT(lines[count++], HUD_LINE_SIZE, tr(GAIN_KEYS[source]), a);
            gained = true;
        }
    }
    if (!gained) {
        SDL_strlcpy(lines[count++], tr("hud.knowledge.no_gain"), HUD_LINE_SIZE);
    }
    int days = colonist_days_unpractised(colonist, knowledge, tick);
    tr_integer(a, sizeof(a), days);
    if (colonist_knowledge_fading(colonist, knowledge, tick)) {
        format_level(b, sizeof(b), knowledge_fade_floor(peak));
        TR_FORMAT(lines[count++], HUD_LINE_SIZE, tr_plural("hud.knowledge.fading", days), a, b);
    } else if (days >= 1) {
        tr_integer(b, sizeof(b), KNOWLEDGE_FADE_AFTER_DAYS);
        TR_FORMAT(lines[count++], HUD_LINE_SIZE, tr_plural("hud.knowledge.unpractised", days), a,
                  b);
    } else {
        SDL_strlcpy(lines[count++], tr("hud.knowledge.practised"), HUD_LINE_SIZE);
    }
    int masters = game_knowledge_masters(&app->game, knowledge);
    if (masters == 0) {
        SDL_strlcpy(lines[count++], tr("hud.knowledge.no_master"), HUD_LINE_SIZE);
    } else {
        tr_integer(a, sizeof(a), masters);
        TR_FORMAT(lines[count++], HUD_LINE_SIZE, tr_plural("hud.knowledge.masters", masters), a);
    }
    for (int i = 0; i < count; i++) {
        pointers[i] = lines[i];
    }
    ui_tooltip(&app->ui, pointers, count);
}

/* Sections of the colonist inspector: its summary and its tabs. */
typedef enum ColonistSection {
    SECTION_SUMMARY,
    SECTION_NEEDS,
    SECTION_MOOD,
    SECTION_KNOWLEDGE,
    SECTION_GEAR,
    SECTION_SOCIAL,
    SECTION_COUNT
} ColonistSection;

static const char *const SECTION_KEYS[SECTION_COUNT] = {
    [SECTION_SUMMARY] = NULL,
    [SECTION_NEEDS] = N_("inspector.tab.needs"),
    [SECTION_MOOD] = N_("inspector.tab.mood"),
    [SECTION_KNOWLEDGE] = N_("inspector.tab.knowledge"),
    [SECTION_GEAR] = N_("inspector.tab.gear"),
    [SECTION_SOCIAL] = N_("inspector.tab.social"),
};

/* Age, what it does, its needs and mood at a glance. */
static void add_summary_lines(App *app, Panel *panel, const Colonist *colonist)
{
    (void)app;
    char age[16];
    tr_integer(age, sizeof(age), colonist->age);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.age"), age);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.activity"),
              tr(colonist_activity_key(colonist)));
    for (int need = 0; need < NEED_COUNT; need++) {
        add_need_line(panel, colonist, (NeedType)need);
    }
    char value[16];
    tr_integer(value, sizeof(value), SDL_lroundf(colonist->mood.value));
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.mood"), value,
              tr(mood_level_key(colonist->mood.value)));
}

/* Needs, wetness, the warmth of its sleep, its bed. */
static void add_needs_lines(App *app, Panel *panel, const Colonist *colonist, EntityId id)
{
    for (int need = 0; need < NEED_COUNT; need++) {
        add_need_line(panel, colonist, (NeedType)need);
    }
    float chill = COLONIST_WET_CHILL * colonist->wetness;
    long long wet = (long long)SDL_lroundf(colonist->wetness * 100.0f);
    if (wet > 0) {
        char percent[16], degrees[32];
        tr_integer(percent, sizeof(percent), wet);
        temperature_text(degrees, sizeof(degrees), chill, 0);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.wet"), percent, degrees);
    }
    float sleep_celsius =
        hearths_felt_temperature(&app->game.workstations, &app->game.map, &app->game.temperatures,
                                 colonist->x, colonist->y, colonist->z) -
        chill;
    float sleep_factor = colonist_sleep_temperature_factor(sleep_celsius);
    if (colonist->task == TASK_SLEEP && !colonist_is_moving(colonist) && sleep_factor < 1.0f) {
        char celsius[32], factor[16];
        temperature_text(celsius, sizeof(celsius), sleep_celsius, 0);
        tr_decimal(factor, sizeof(factor), sleep_factor, 2);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE,
                  tr(sleep_celsius < COLONIST_COLD_SLEEP_BELOW ? "hud.colonist.cold_sleep"
                                                               : "hud.colonist.hot_sleep"),
                  celsius, factor);
    }
    TilePos bed;
    if (beds_owned(&app->game.beds, (uint8_t)(id.index + 1), &bed)) {
        StructureType type = map_structure(&app->game.map, bed.x, bed.y, bed.z);
        TR_FORMAT(
            panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.bed"),
            tr(structure_def(type)->name_key),
            tr(item_quality_name_key(map_structure_quality(&app->game.map, bed.x, bed.y, bed.z))));
    } else {
        panel_add_text(panel, tr("hud.colonist.no_bed"));
    }
}

/* Strength, what it carries, its tools. */
static void add_gear_lines(Panel *panel, const Colonist *colonist)
{
    char strength[16], carry[32];
    tr_integer(strength, sizeof(strength), colonist->strength);
    format_weight(carry, sizeof(carry), colonist->max_carry_weight);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.strength"), strength, carry);
    if (colonist->carried.count > 0) {
        char count[16], weight[32];
        tr_integer(count, sizeof(count), colonist->carried.count);
        format_weight(weight, sizeof(weight), colonist_carried_weight(colonist));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.carrying"),
                  tr(item_def((ItemType)colonist->carried.type)->name_key), count, weight);
        add_item_condition_line(panel, colonist->carried);
    }
    for (int i = 0; i < COLONIST_TOOL_MAX; i++) {
        ItemStack tool = colonist->tools[i];
        if (tool.type == ITEM_NONE) {
            continue;
        }
        char hit_points[16], percent[32];
        tr_integer(hit_points, sizeof(hit_points), item_stack_hit_points_percent(tool));
        TR_FORMAT(percent, sizeof(percent), tr("unit.percent"), hit_points);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.tool"),
                  tr(item_def((ItemType)tool.type)->name_key),
                  tr(item_quality_name_key((ItemQuality)tool.quality)), percent);
    }
    if (colonist->tools_broken > 0) {
        char count[16];
        tr_integer(count, sizeof(count), colonist->tools_broken);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE,
                  tr_plural("hud.colonist.tools_broken", colonist->tools_broken), count);
    }
}

/* Discovered knowledges the settler has started to learn: level with one
 * decimal, the work speed once it is high enough to use, and a tooltip. */
static void add_knowledge_lines(App *app, Panel *panel, const Colonist *colonist)
{
    panel_add_text(panel, tr("hud.colonist.knowledge"));
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        double value = colonist_knowledge_level(colonist, (KnowledgeType)knowledge);
        if (!(value > 0.0) || !game_knowledge_discovered(&app->game, (KnowledgeType)knowledge)) {
            continue;
        }
        const char *name = tr(knowledge_def((KnowledgeType)knowledge)->name_key);
        char level[16];
        format_level(level, sizeof(level), value);
        bool fading =
            colonist_knowledge_fading(colonist, (KnowledgeType)knowledge, app->game.clock.tick);
        char *line = panel_add(panel);
        panel->tip[panel->count - 1] = (uint8_t)(knowledge + 1);
        if (!knowledge_is_mastered(value)) {
            TR_FORMAT(line, HUD_LINE_SIZE,
                      tr(fading ? "hud.colonist.knowledge_learning_fading"
                                : "hud.colonist.knowledge_learning"),
                      name, level);
            continue;
        }
        char speed[16];
        tr_decimal(speed, sizeof(speed), knowledge_work_speed(value), 2);
        TR_FORMAT(
            line, HUD_LINE_SIZE,
            tr(fading ? "hud.colonist.knowledge_level_fading" : "hud.colonist.knowledge_level"),
            name, level, speed);
    }
}

/* The lines of one section of the colonist inspector. */
static void build_colonist_section(App *app, Panel *panel, EntityId id, ColonistSection section)
{
    const Colonist *colonist = game_colonist_const(&app->game, id);
    if (colonist == NULL) {
        return;
    }
    switch (section) {
    case SECTION_SUMMARY: add_summary_lines(app, panel, colonist); break;
    case SECTION_NEEDS: add_needs_lines(app, panel, colonist, id); break;
    case SECTION_MOOD: add_mood_lines(panel, colonist); break;
    case SECTION_KNOWLEDGE: add_knowledge_lines(app, panel, colonist); break;
    case SECTION_GEAR: add_gear_lines(panel, colonist); break;
    case SECTION_SOCIAL:
        add_social_lines(app, panel, colonist, id);
        if (panel->count == 0) {
            panel_add_text(panel, tr("inspector.social.none"));
        }
        break;
    case SECTION_COUNT: break;
    }
}

void hud_draw_item_counts(App *app)
{
    const int z = app->camera.level; /* The level shown. */
    if (app->camera.zoom < HUD_ITEM_COUNT_MIN_ZOOM) {
        return;
    }
    const Items *items = &app->game.items;
    TileRect visible = camera_visible_tiles(&app->camera, items->width, items->height);
    char count[16];
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            ItemStack stack = items_get(items, x, y, z);
            if (stack.type == ITEM_NONE) {
                continue;
            }
            float sx, sy;
            camera_world_to_screen(&app->camera, (float)(x + 1), (float)(y + 1), &sx, &sy);
            tr_integer(count, sizeof(count), stack.count);
            int width, height;
            text_measure(app->text, count, &width, &height);
            float tx = SDL_floorf(sx - (float)width - 2.0f);
            float ty = SDL_floorf(sy - (float)height);
            text_draw(app->text, count, tx + 1.0f, ty + 1.0f, THEME_COLOR(app, THEME_TEXT_SHADOW));
            text_draw(app->text, count, tx, ty, THEME_COLOR(app, THEME_TEXT));
        }
    }
}

void hud_draw_world_labels(App *app)
{
    if (app->camera.zoom < HUD_LABEL_MIN_ZOOM) {
        return;
    }
    const Game *game = &app->game;
    float offset = app->camera.zoom * 0.48f + 2.0f; /* Just under the feet. */
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (entity_id_is_none(game_colonist_id_at(game, slot)) ||
            game->colonists[slot].z != app->camera.level) {
            continue;
        }
        const Colonist *colonist = &game->colonists[slot];
        float wx, wy, sx, sy;
        colonist_position(colonist, &wx, &wy);
        camera_world_to_screen(&app->camera, wx, wy, &sx, &sy);
        int width, height;
        text_measure(app->text, colonist->name, &width, &height);
        float x = SDL_floorf(sx - (float)width * 0.5f);
        float y = SDL_floorf(sy + offset);
        text_draw(app->text, colonist->name, x + 1.0f, y + 1.0f,
                  THEME_COLOR(app, THEME_TEXT_SHADOW));
        text_draw(app->text, colonist->name, x, y, THEME_COLOR(app, THEME_TEXT));
    }
}

typedef struct ColonistRows {
    App *app;
    const Panel *panel;
} ColonistRows;

/* A section's lines; a knowledge line shows its tooltip under the mouse. */
static void colonist_rows(UiLayout *layout, void *userdata)
{
    const ColonistRows *rows = userdata;
    const Colonist *colonist = game_colonist_const(&rows->app->game, rows->app->selected[0]);
    for (int i = 0; i < rows->panel->count; i++) {
        SDL_FRect rect = {layout->x, layout->y, layout->width, layout->row};
        ui_layout_label(layout, rows->panel->lines[i], 0.0f, false);
        int tip = rows->panel->tip[i];
        if (layout->draw && tip > 0 && colonist != NULL && ui_hovered(layout->ui, rect)) {
            knowledge_tooltip(rows->app, colonist, (KnowledgeType)(tip - 1));
        }
    }
}

void hud_inspect_colonists(App *app)
{
    Panel summary = {0};
    char title[HUD_LINE_SIZE];
    const Colonist *colonist = game_colonist_const(&app->game, app->selected[0]);
    bool one = app->selected_count == 1 && colonist != NULL;
    if (one) {
        SDL_strlcpy(title, colonist->name, sizeof(title));
        build_colonist_section(app, &summary, app->selected[0], SECTION_SUMMARY);
    } else {
        char count[16];
        tr_integer(count, sizeof(count), app->selected_count);
        TR_FORMAT(title, sizeof(title), tr_plural("hud.selection.count", app->selected_count),
                  count);
        for (int i = 0; i < app->selected_count && summary.count < HUD_MAX_LINES; i++) {
            const Colonist *each = game_colonist_const(&app->game, app->selected[i]);
            if (each != NULL) {
                panel_add_text(&summary, each->name);
            }
        }
    }
    ColonistRows rows = {app, &summary};
    bool open =
        inspector_box(app, one ? INSPECT_COLONIST : INSPECT_COLONISTS, title, colonist_rows, &rows);
    if (one) {
        const char *labels[SECTION_COUNT - 1];
        for (int i = 1; i < SECTION_COUNT; i++) {
            labels[i - 1] = tr(SECTION_KEYS[i]);
        }
        int tab = inspector_tabs(app, labels, SECTION_COUNT - 1);
        if (tab >= 0) {
            Panel section = {0};
            build_colonist_section(app, &section, app->selected[0], (ColonistSection)(tab + 1));
            ColonistRows section_rows = {app, &section};
            inspector_tab_panel(app, labels[tab], colonist_rows, &section_rows);
        }
    } else {
        inspector_tabs(app, NULL, 0);
    }
    if (!open) {
        app->selected_count = 0;
    }
}

static void help_window(App *app)
{
    Panel help = {0};
    char keys[32];
    movement_keys_label(keys, sizeof(keys));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.camera"), keys);
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.game"), app_key_name(APP_KEY_LANGUAGE));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.gathering"),
              app_key_name(APP_KEY_GATHER));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.orders"), app_key_name(APP_KEY_MINE),
              app_key_name(APP_KEY_CANCEL));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.manage"),
              app_key_name(APP_KEY_STOCKPILE), app_key_name(APP_KEY_BUILD),
              app_key_name(APP_KEY_WORK));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.copy"),
              app_key_name(APP_KEY_BUILD_LIKE));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.rotate"), app_key_name(APP_KEY_ROTATE));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.temperature"),
              app_key_name(APP_KEY_TEMPERATURE));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.roofs"), app_key_name(APP_KEY_LEVEL_UP),
              tr("designation.deconstruct"));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.levels"),
              app_key_name(APP_KEY_LEVEL_UP), app_key_name(APP_KEY_LEVEL_DOWN));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.debug"),
              app_key_name(APP_KEY_DEBUG_MENU), app_key_name(APP_KEY_ITEMS_MENU));
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_HELP, tr("help.title"), UI_PLACE_CENTER, panel_rows,
                     &help, &open);
    if (!open) {
        app->show_help = false;
    }
}

static void draw_window(App *app, AppWindow window)
{
    switch (window) {
    case APP_WINDOW_INSPECTOR: inspector_draw(app); break;
    case APP_WINDOW_WORK: work_panel_draw(app); break;
    case APP_WINDOW_HELP: help_window(app); break;
    case APP_WINDOW_DEBUG: debug_panel_draw(app); break;
    case APP_WINDOW_ITEMS: items_panel_draw(app); break;
    case APP_WINDOW_GAME_MENU: game_menu_draw(app); break;
    default: break;
    }
}

/* Stacking order; a window just opened comes in front. */
static unsigned window_order(const App *app, AppWindow window)
{
    unsigned z = ui_window_z(&app->ui, (int)window);
    return z == 0 ? UINT_MAX : z;
}

int hud_open_windows(const App *app, AppWindow out[APP_WINDOW_COUNT])
{
    int count = 0;
    /* The inspector draws its open tab itself (APP_WINDOW_INSPECTOR_TAB). */
    if (inspector_kind(app) != INSPECT_NONE) {
        out[count++] = APP_WINDOW_INSPECTOR;
        if (app->inspector_tab >= 0) {
            out[count++] = APP_WINDOW_INSPECTOR_TAB;
        }
    }
    if (app->show_work_panel) {
        out[count++] = APP_WINDOW_WORK;
    }
    if (app->show_help) {
        out[count++] = APP_WINDOW_HELP;
    }
    if (app->show_debug_menu) {
        out[count++] = APP_WINDOW_DEBUG;
    }
    if (app->show_items_menu) {
        out[count++] = APP_WINDOW_ITEMS;
    }
    if (app->show_game_menu) {
        out[count++] = APP_WINDOW_GAME_MENU;
    }
    /* Insertion sort, back to front. */
    for (int i = 1; i < count; i++) {
        AppWindow window = out[i];
        int j = i;
        while (j > 0 && window_order(app, out[j - 1]) > window_order(app, window)) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = window;
    }
    return count;
}

void hud_update_notices(App *app, double dt)
{
    /* Those on screen age; the oldest go. */
    int kept = 0;
    for (int i = 0; i < app->notices_shown_count; i++) {
        app->notices_age[i] += dt;
        if (app->notices_age[i] < APP_NOTICE_SECONDS) {
            app->notices_shown[kept] = app->notices_shown[i];
            app->notices_age[kept] = app->notices_age[i];
            kept++;
        }
    }
    app->notices_shown_count = kept;
    const Notices *notices = &app->game.notices;
    if (app->next_notice > notices->count) {
        app->next_notice = 0; /* A new game. */
        app->notices_shown_count = 0;
    }
    for (; app->next_notice < notices->count; app->next_notice++) {
        if (notices_get(notices, app->next_notice) == NULL) {
            continue; /* Forgotten already. */
        }
        if (app->notices_shown_count == APP_NOTICES_SHOWN) {
            SDL_memmove(app->notices_shown, app->notices_shown + 1,
                        sizeof(app->notices_shown[0]) * (APP_NOTICES_SHOWN - 1));
            SDL_memmove(app->notices_age, app->notices_age + 1,
                        sizeof(app->notices_age[0]) * (APP_NOTICES_SHOWN - 1));
            app->notices_shown_count--;
        }
        app->notices_shown[app->notices_shown_count] = app->next_notice;
        app->notices_age[app->notices_shown_count] = 0.0;
        app->notices_shown_count++;
    }
}

/* Seconds a notice takes to fade out at the end of its time on screen. */
#define NOTICE_FADE_SECONDS 2.0

/* The notices on screen, centered under the colonist bar, oldest first; each
 * fades out at the end of its time. */
static void draw_notices(App *app)
{
    float y = colonist_bar_bottom(app) + HUD_MARGIN;
    float line = (float)text_line_height(app->text);
    for (int i = 0; i < app->notices_shown_count; i++) {
        const Notice *notice = notices_get(&app->game.notices, app->notices_shown[i]);
        if (notice == NULL) {
            continue;
        }
        /* Not every notice is about a knowledge (a roof falling in). */
        const KnowledgeDef *def = knowledge_def(notice->knowledge);
        char text[HUD_LINE_SIZE];
        TR_FORMAT(text, sizeof(text), tr(notice_text_key((NoticeType)notice->type)),
                  def != NULL ? tr(def->name_key) : "", notice->name);
        double left = APP_NOTICE_SECONDS - app->notices_age[i];
        float alpha = (float)SDL_clamp(left / NOTICE_FADE_SECONDS, 0.0, 1.0);
        int width, height;
        text_measure(app->text, text, &width, &height);
        SDL_FRect box = {SDL_floorf((app->camera.viewport_width - (float)width) * 0.5f) -
                             HUD_PADDING,
                         y, (float)width + 2.0f * HUD_PADDING, line + HUD_PADDING};
        SDL_Color background = THEME_COLOR(app, THEME_HUD_PANEL);
        SDL_Color color = THEME_COLOR(app, THEME_TEXT);
        background.a = (Uint8)((float)background.a * alpha);
        color.a = (Uint8)((float)color.a * alpha);
        SDL_SetRenderDrawColor(app->renderer, background.r, background.g, background.b,
                               background.a);
        SDL_RenderFillRect(app->renderer, &box);
        text_draw(app->text, text, box.x + HUD_PADDING, box.y + SDL_floorf(HUD_PADDING * 0.5f),
                  color);
        y += box.h + 2.0f;
    }
}

void hud_draw(App *app)
{
    const float screen_width = app->camera.viewport_width;
    float width, height;
    ScreenLayout layout;
    layout_screen(&app->ui, &layout);
    app->ui.reserved_bottom = layout.bottom_bar.h; /* Windows stay above the bar. */

    /* Bottom right, left of the level slider: date and weather. */
    Panel status = {0};
    build_status_panel(app, &status);
    float status_right, status_bottom;
    view_controls_info_corner(app, &status_right, &status_bottom);
    panel_size(app, &status, &width, &height);
    panel_draw_aligned(app, &status, status_right - width, status_bottom - height,
                       THEME_COLOR(app, THEME_HUD_PANEL), true);

    /* Top right: the debug readout (F3). */
    if (app->show_debug) {
        Panel debug = {0};
        build_debug_panel(app, &debug);
        panel_size(app, &debug, &width, &height);
        panel_draw(app, &debug, screen_width - width - HUD_MARGIN, HUD_MARGIN);
    }
    draw_notices(app);

    /* Bottom left: the tile inspector above the menus. */
    float menu_top = build_menu_top(app);
    if (app->hover_valid && inspector_kind(app) == INSPECT_NONE) {
        Panel tile = {0};
        build_tile_panel(app, &tile);
        panel_size(app, &tile, &width, &height);
        panel_draw_colored(app, &tile, HUD_MARGIN, menu_top - height - HUD_MARGIN,
                           THEME_COLOR(app, THEME_READOUT));
    }

    colonist_bar_draw(app);

    /* Windows, back to front. */
    AppWindow open[APP_WINDOW_COUNT];
    int open_count = hud_open_windows(app, open);
    for (int i = 0; i < open_count; i++) {
        draw_window(app, open[i]);
    }
    build_menu_draw(app);
    view_controls_draw(app);
    bottom_bar_draw(app);
    ui_draw_popup(&app->ui);
    ui_draw_tooltip(&app->ui);
}
