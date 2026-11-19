#include "game/hud.h"

#include "core/i18n.h"
#include "core/temperature.h"
#include "core/tree.h"
#include "game/animals_panel.h"
#include "game/scenarios_panel.h"
#include "game/attributes_panel.h"
#include "game/debug_panel.h"
#include "game/inspector.h"
#include "game/items_panel.h"
#include "game/layout.h"
#include "game/log_window.h"
#include "game/gather_zone_panel.h"
#include "game/health_panel.h"
#include "game/journal_panel.h"
#include "game/alerts_list.h"
#include "game/bottom_bar.h"
#include "game/build_menu.h"
#include "game/colonist_bar.h"
#include "game/game_menu.h"
#include "game/station_panel.h"
#include "game/ui_layout.h"
#include "game/resources_list.h"
#include "game/settings_window.h"
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
void hud_weight_text(char *out, size_t size, float kilograms)
{
    char number[32];
    float rounded = SDL_roundf(kilograms);
    int decimals = (kilograms < 10.0f && SDL_fabsf(kilograms - rounded) > 0.05f) ? 1 : 0;
    tr_decimal(number, sizeof(number), decimals ? kilograms : rounded, decimals);
    TR_FORMAT(out, size, tr("unit.kg"), number);
}

/* A quality as text ("47"). */
void hud_quality_text(char *out, size_t size, ItemQuality quality)
{
    tr_integer(out, size, quality);
}

/* "Quality: 47 · Hit points: 45/60 (75%)" line of an item stack, with only
 * the parts its type has (none for stone blocks, no quality for grass). */
bool hud_item_condition_text(char *out, size_t size, ItemStack stack)
{
    const ItemDef *def = item_def((ItemType)stack.type);
    if (def == NULL || stack.type == ITEM_NONE || (!def->has_quality && !def->damageable)) {
        return false;
    }
    char quality[16];
    hud_quality_text(quality, sizeof(quality), stack.quality);
    if (!def->damageable) {
        TR_FORMAT(out, size, tr("hud.item_quality"), quality);
        return true;
    }
    char hit_points[16], max_hit_points[16], number[16], percent[32];
    tr_integer(hit_points, sizeof(hit_points), stack.hit_points);
    tr_integer(max_hit_points, sizeof(max_hit_points), def->max_hit_points);
    tr_integer(number, sizeof(number), item_stack_hit_points_percent(stack));
    TR_FORMAT(percent, sizeof(percent), tr("unit.percent"), number);
    if (!def->has_quality) {
        TR_FORMAT(out, size, tr("hud.item_hit_points"), hit_points, max_hit_points, percent);
        return true;
    }
    TR_FORMAT(out, size, tr("hud.item_condition"), quality, hit_points, max_hit_points, percent);
    return true;
}

static void add_item_condition_line(Panel *panel, ItemStack stack)
{
    char line[HUD_LINE_SIZE];
    if (hud_item_condition_text(line, sizeof(line), stack)) {
        panel_add_text(panel, line);
    }
}

bool hud_structure_condition_text(char *out, size_t size, const Map *map, int x, int y, int z)
{
    StructureType type = map_structure(map, x, y, z);
    ItemQuality quality = map_structure_quality(map, x, y, z);
    int max = structure_max_hit_points(type, quality);
    if (max <= 0) {
        return false;
    }
    int current = map_structure_hit_points(map, x, y, z);
    char hit_points[16], max_hit_points[16], number[16], percent[32];
    tr_integer(hit_points, sizeof(hit_points), current);
    tr_integer(max_hit_points, sizeof(max_hit_points), max);
    tr_integer(number, sizeof(number), (long long)current * 100 / max);
    TR_FORMAT(percent, sizeof(percent), tr("unit.percent"), number);
    char quality_text[16];
    hud_quality_text(quality_text, sizeof(quality_text), quality);
    TR_FORMAT(out, size, tr("hud.item_condition"), quality_text, hit_points, max_hit_points,
              percent);
    return true;
}

/* Grade and hit points of the structure built on a tile. */
static void add_structure_condition_line(Panel *panel, const Map *map, int x, int y, int z)
{
    char line[HUD_LINE_SIZE];
    if (hud_structure_condition_text(line, sizeof(line), map, x, y, z)) {
        panel_add_text(panel, line);
    }
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

void hud_bed_owner_text(const App *app, char *out, size_t size, int x, int y, int z)
{
    const Colonist *owner = bed_owner(&app->game, x, y, z);
    if (owner != NULL) {
        TR_FORMAT(out, size, tr("hud.tile.bed_owner"), owner->name);
    } else {
        SDL_strlcpy(out, tr("hud.tile.bed_free"), size);
    }
}

static void add_bed_owner_line(const App *app, Panel *panel, int x, int y)
{
    char line[HUD_LINE_SIZE];
    hud_bed_owner_text(app, line, sizeof(line), x, y, app->camera.level);
    panel_add_text(panel, line);
}

/* "12.5 °C", with `decimals` decimals. */
static void temperature_text(char *out, size_t size, float celsius, int decimals)
{
    char number[16];
    tr_decimal(number, sizeof(number), celsius, decimals);
    TR_FORMAT(out, size, tr("unit.celsius"), number);
}

/* With the temperature view on, the temperature felt on the tile under the mouse
 * (the warmth of fires included, as drawn); with the light view, its light now
 * and the best it gets today; with the moisture view, how wet its ground is.
 * Nothing for what the colony has not seen. */
static void add_hovered_tile_reading(const App *app, Panel *panel)
{
    if ((!app->show_temperature && !app->show_light && !app->show_moisture) || !app->hover_valid) {
        return;
    }
    const Game *game = &app->game;
    int x = app->hover_x, y = app->hover_y, z = app->camera.level;
    const Map *map = game_view_map(game);
    if (!map_in_bounds(map, x, y, z) || map_is_open(&game->map, x, y, z) ||
        !game_tile_seen(game, x, y, z)) {
        return;
    }
    char text[32], more[32];
    if (app->show_temperature) {
        float felt = temperature_at(&game->temperatures, x, y, z) +
                     hearths_radiant_warmth(&game->workstations, &game->map, x, y, z);
        temperature_text(text, sizeof(text), felt, 1);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.hover_temperature"), text);
    } else if (app->show_moisture) {
        const char *key = soil_moisture_key(&game->soil, &game->map, x, y, z);
        if (key != NULL) {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.hover_soil"), tr(key));
        }
    } else {
        tr_integer(text, sizeof(text),
                   (long long)SDL_lroundf(game_light_at(game, x, y, z) * 100.0f));
        tr_integer(more, sizeof(more),
                   (long long)SDL_lroundf(game_light_peak_at(game, x, y, z) * 100.0f));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.hover_light"), text, more);
    }
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
    char humidity[16];
    tr_integer(
        humidity, sizeof(humidity),
        (long long)SDL_lroundf(weather_outdoor_humidity(&app->game.weather, clock->tick) * 100.0f));
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.outdoor_humidity"), humidity);
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
    if (app->show_light) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.light_view"),
                  app_key_name(APP_KEY_LIGHT));
    }
    if (app->show_moisture) {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.moisture_view"),
                  app_key_name(APP_KEY_MOISTURE));
    }
    add_hovered_tile_reading(app, panel);
}

bool hud_site_line(const ConstructionSite *site, int line, char *out, size_t size)
{
    char a[32], b[32];
    if (line == 0) {
        tr_integer(a, sizeof(a), (long long)(construction_work_fraction(site) * 100.0f));
        TR_FORMAT(out, size, tr(site->refresh ? "hud.tile.refresh" : "hud.tile.blueprint"),
                  tr(construction_name_key(site)), a);
        return true;
    }
    int slot = line - 1;
    ItemType material =
        slot < STRUCTURE_MATERIAL_SLOTS ? construction_material(site, slot) : ITEM_NONE;
    if (material == ITEM_NONE) {
        return false;
    }
    tr_integer(a, sizeof(a), site->delivered[slot].count);
    tr_integer(b, sizeof(b), construction_material_count(site, slot));
    TR_FORMAT(out, size, tr("hud.tile.material"), tr(item_def(material)->name_key), a, b);
    return true;
}

/* A construction site's lines in the tile readout: progress and materials. */
static void add_site_lines(Panel *panel, const ConstructionSite *site)
{
    if (site == NULL) {
        return;
    }
    char line[HUD_LINE_SIZE];
    for (int i = 0; i <= STRUCTURE_MATERIAL_SLOTS; i++) {
        if (hud_site_line(site, i, line, sizeof(line))) {
            panel_add_text(panel, line);
        }
    }
}

/* "2 hours ago": how long since the colony saw something, at `tick`. */
static void seen_ago(char *out, size_t size, uint64_t now, uint64_t tick)
{
    uint64_t hours = now > tick ? (now - tick) / TICKS_PER_HOUR : 0;
    char number[32];
    if (hours < 1) {
        SDL_strlcpy(out, tr("hud.seen.recently"), size);
    } else if (hours < 2 * HOURS_PER_DAY) {
        tr_integer(number, sizeof(number), (long long)hours);
        TR_FORMAT(out, size, tr_plural("hud.seen.hours", (long long)hours), number);
    } else {
        long long days = (long long)(hours / HOURS_PER_DAY);
        tr_integer(number, sizeof(number), days);
        TR_FORMAT(out, size, tr_plural("hud.seen.days", days), number);
    }
}

/* The animals last seen on a tile out of view (their ghosts, core/known.h). */
static void add_animal_ghosts(App *app, Panel *panel, int x, int y, int z)
{
    const Game *game = &app->game;
    for (uint32_t slot = 0; slot < ANIMAL_MAX; slot++) {
        const KnownAnimal *ghost = known_animal_ghost(&game->known, slot);
        if (ghost == NULL || ghost->z != z || (int)SDL_floorf(ghost->x) != x ||
            (int)SDL_floorf(ghost->y) != y) {
            continue;
        }
        char ago[64];
        seen_ago(ago, sizeof(ago), game->clock.tick, ghost->tick);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.animal_seen"), ago,
                  tr(animal_name_key((AnimalSpecies)ghost->species, (AnimalSex)ghost->sex)));
    }
}

static void build_tile_panel(App *app, Panel *panel)
{
    const int z = app->camera.level; /* The level shown. */
    const Map *map = game_view_map(&app->game);
    int x = app->hover_x, y = app->hover_y;
    size_t index = map_index(map, x, y, z);
    char a[32], b[32];

    tr_integer(a, sizeof(a), x);
    tr_integer(b, sizeof(b), y);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.position"), a, b);

    if (app->game.fog_on) {
        /* What the colony has not seen is not told; what it has, as it saw it. */
        int seen_level = map_view_level(map, x, y, z);
        if (seen_level < 0) {
            panel_add_text(panel, tr("hud.tile.unexplored"));
            return;
        }
        if (!game_tile_seen(&app->game, x, y, seen_level)) {
            panel_add_text(panel, tr("hud.tile.remembered"));
            add_animal_ghosts(app, panel, x, y, seen_level);
        }
    }

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
    int foot_x = x, foot_y = y, foot_z = z; /* The tile the plant grows on. */
    TreeRef tree;
    if (tree_at(map, x, y, z, &tree)) {
        plant = tree_plant(map, &tree);
        foot_x = tree.x;
        foot_y = tree.y;
        foot_z = tree.z;
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
            PlantConditions conditions = game_plant_conditions(&app->game, foot_x, foot_y, foot_z);
            panel_add_text(panel, tr(plant_status_name_key(plant_status(plant, &conditions))));
        }
        if (game_forbidden(&app->game, x, y, z, FORBID_PLANT)) {
            panel_add_text(panel, tr("tile_panel.left_alone"));
        }
        for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
            const PlantProduct *product = plant_product((PlantType)plant->type, slot);
            if (product == NULL || plant_stock_capacity(plant, slot) <= 0) {
                continue;
            }
            const char *item = tr(item_def(product->item)->name_key);
            if (!plant_product_in_season(plant, slot)) {
                TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.plant_out_of_season"),
                          item);
                continue;
            }
            tr_integer(a, sizeof(a), plant_stock_available(plant, slot));
            tr_integer(b, sizeof(b), plant_stock_capacity(plant, slot));
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.plant_stock"), item, a, b);
        }
    }

    ItemStack stack = items_get(game_items_at(&app->game, x, y, z), x, y, z);
    if (stack.type != ITEM_NONE) {
        char weight[32];
        tr_integer(a, sizeof(a), stack.count);
        hud_weight_text(weight, sizeof(weight), item_stack_mass(stack));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.items"),
                  tr(item_def((ItemType)stack.type)->name_key), a, weight);
        add_item_condition_line(panel, stack);
        if (items_forbidden(game_view_items(&app->game), x, y, z)) {
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
        char quality[16];
        hud_quality_text(quality, sizeof(quality), map_roof_quality(map, x, y, z));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.roof"),
                  tr(structure_def(roof)->name_key), quality);
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
    if (!game_tile_seen(&app->game, x, y, z)) {
        panel_add_text(panel, tr("hud.tile.temperature_unknown")); /* Nobody measures it. */
    } else {
        float air = temperature_at(&app->game.temperatures, x, y, z);
        float radiant = hearths_radiant_warmth(&app->game.workstations, map, x, y, z);
        temperature_text(celsius, sizeof(celsius), air, 1);
        if (radiant >= 0.05f) {
            temperature_text(felt, sizeof(felt), air + radiant, 1);
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.temperature_felt"), celsius,
                      felt);
        } else {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.temperature"), celsius);
        }
        const char *soil = soil_moisture_key(&app->game.soil, map, x, y, z);
        if (soil != NULL) {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.soil"), tr(soil));
        }
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

    /* Branches, twigs and nuts the trees shed, on the ground or on their
     * foliage; twigs as they would be picked up (wet on wet ground, in the
     * rain). */
    static const ItemType LITTER[] = {ITEM_BRANCHES, ITEM_TWIGS, ITEM_HAZELNUTS};
    bool raining = weather_wetting(&app->game.weather, app->game.clock.tick) > 0.0f;
    for (size_t i = 0; i < SDL_arraysize(LITTER); i++) {
        int count = map_litter(map, x, y, z, LITTER[i]);
        if (count > 0) {
            ItemType shown =
                LITTER[i] == ITEM_TWIGS && soil_litter_wet(&app->game.soil, map, x, y, z, raining)
                    ? ITEM_WET_TWIGS
                    : LITTER[i];
            tr_integer(a, sizeof(a), count);
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.litter"),
                      tr(item_def(shown)->name_key), a);
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

    int dirt = dirt_at(game_view_dirt(&app->game), x, y, z);
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

/* The lines of a tooltip being put together. */
typedef struct TooltipText {
    char lines[UI_TOOLTIP_LINES][HUD_LINE_SIZE];
    int count;
} TooltipText;

static char *tooltip_add(TooltipText *text)
{
    if (text->count < UI_TOOLTIP_LINES) {
        text->count++;
    }
    char *line = text->lines[text->count - 1];
    line[0] = '\0';
    return line;
}

static void tooltip_show(App *app, const TooltipText *text)
{
    const char *pointers[UI_TOOLTIP_LINES];
    for (int i = 0; i < text->count; i++) {
        pointers[i] = text->lines[i];
    }
    ui_tooltip(&app->ui, pointers, text->count);
}

/* The thoughts a need brings on, from the lightest to the heaviest. */
static const ThoughtType NEED_THOUGHTS[NEED_COUNT][2] = {
    [NEED_FOOD] = {THOUGHT_HUNGRY, THOUGHT_STARVING},
    [NEED_WATER] = {THOUGHT_THIRSTY, THOUGHT_PARCHED},
    [NEED_REST] = {THOUGHT_TIRED, THOUGHT_EXHAUSTED},
};

/* A need: its exact value and state, what it does, and what it weighs on the
 * mood now. */
static void need_tooltip(App *app, const Colonist *colonist, NeedType need)
{
    TooltipText text = {0};
    float value = colonist->needs[need];
    char percent[16];
    tr_integer(percent, sizeof(percent), (long long)(value * 100.0f + 0.5f));
    const char *state_key = need_level_key(need, need_level(value));
    if (state_key == NULL) {
        TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("hud.colonist.need"),
                  tr(need_name_key(need)), percent);
    } else {
        TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("hud.colonist.need_state"),
                  tr(need_name_key(need)), percent, tr(state_key));
    }
    static const char *const DESCRIPTION_KEYS[NEED_COUNT] = {
        [NEED_FOOD] = N_("need.food.description"),
        [NEED_WATER] = N_("need.water.description"),
        [NEED_REST] = N_("need.rest.description"),
    };
    SDL_strlcpy(tooltip_add(&text), tr(DESCRIPTION_KEYS[need]), HUD_LINE_SIZE);
    for (int i = 0; i < 2; i++) {
        ThoughtType thought = NEED_THOUGHTS[need][i];
        if (mood_has(&colonist->mood, thought)) {
            char amount[16];
            format_signed(amount, sizeof(amount),
                          SDL_lroundf(mood_thought_value(&colonist->mood, thought)));
            TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("need.mood"), amount);
        }
    }
    tooltip_show(app, &text);
}

/* The mood: its exact value and what it does to work, learning and quality. */
static void mood_tooltip(App *app, const Mood *mood)
{
    TooltipText text = {0};
    char value[16], work[16], learn[16], level[16];
    tr_integer(value, sizeof(value), SDL_lroundf(mood->value));
    TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("hud.colonist.mood"), value,
              tr(mood_level_key(mood->value)));
    tr_decimal(work, sizeof(work), mood_work_factor(mood->value), 2);
    tr_decimal(learn, sizeof(learn), (float)mood_learn_factor(mood->value), 2);
    format_signed(level, sizeof(level), SDL_lround(mood_level_offset(mood->value)));
    TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("hud.colonist.mood_effects"), work, learn,
              level);
    tooltip_show(app, &text);
}

/* A thought: what it is about, its original value, and how it fades. */
static void thought_tooltip(App *app, const Mood *mood, ThoughtType type)
{
    TooltipText text = {0};
    const ThoughtDef *def = thought_def(type);
    char a[16], b[16];
    SDL_strlcpy(tooltip_add(&text), tr(def->name_key), HUD_LINE_SIZE);
    SDL_strlcpy(tooltip_add(&text), tr(def->description_key), HUD_LINE_SIZE);
    format_signed(a, sizeof(a), SDL_lroundf(mood_thought_peak(mood, type)));
    TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("hud.thought.original"), a);
    if (def->state) {
        tr_integer(a, sizeof(a), SDL_lroundf(def->half_life_hours));
        TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("hud.thought.state"), a);
        if (def->grow_max > 1.0f) {
            format_signed(a, sizeof(a),
                          SDL_lroundf(def->value * mood->sensitivity * def->grow_max));
            tr_integer(b, sizeof(b), SDL_lroundf(def->grow_hours));
            TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("hud.thought.grows"), a, b);
        }
    } else {
        tr_integer(a, sizeof(a), SDL_lroundf(def->plateau_hours));
        tr_integer(b, sizeof(b), SDL_lroundf(def->half_life_hours));
        TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("hud.thought.memory"), a, b);
    }
    if (def->max_stack > 1) {
        tr_integer(a, sizeof(a), def->max_stack);
        TR_FORMAT(tooltip_add(&text), HUD_LINE_SIZE, tr("hud.thought.stacks"), a);
    }
    if (mood_fading(mood, type)) {
        SDL_strlcpy(tooltip_add(&text), tr("hud.thought.fading"), HUD_LINE_SIZE);
    }
    tooltip_show(app, &text);
}

/* The mood meter, a break under way and a table of the thoughts: what each
 * weighs now. */
static void mood_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Colonist *colonist = game_colonist_const(&app->game, app->selected[0]);
    if (colonist == NULL) {
        return;
    }
    const Mood *mood = &colonist->mood;
    char state[HUD_LINE_SIZE];
    TR_FORMAT(state, sizeof(state), tr("hud.mood.state"), tr(mood_level_key(mood->value)));
    SDL_FRect meter =
        ui_layout_meter(layout, state, (mood->value - MOOD_MIN) / (MOOD_MAX - MOOD_MIN));
    if (layout->draw && ui_hovered(layout->ui, meter)) {
        mood_tooltip(app, mood);
    }
    const char *break_key = mood_break_key((MoodBreak)mood->break_type);
    if (break_key != NULL) {
        int left = hours_left(mood->break_ticks);
        char hours[16], line[HUD_LINE_SIZE];
        tr_integer(hours, sizeof(hours), left);
        TR_FORMAT(line, sizeof(line), tr_plural("hud.colonist.break", left), tr(break_key), hours);
        ui_layout_label(layout, line, 0.0f, false);
    }
    bool any = false;
    for (int type = 0; type < THOUGHT_COUNT; type++) {
        ThoughtType thought = (ThoughtType)type;
        if (!mood_has(mood, thought)) {
            continue;
        }
        any = true;
        float value = mood_thought_value(mood, thought);
        char amount[16];
        format_signed(amount, sizeof(amount), SDL_lroundf(value));
        SDL_FRect row = ui_layout_pair(layout, tr(thought_def(thought)->name_key), amount,
                                       theme_sign_color(&layout->ui->theme, value), true);
        if (layout->draw && ui_hovered(layout->ui, row)) {
            thought_tooltip(app, mood, thought);
        }
    }
    if (!any) {
        ui_layout_label(layout, tr("inspector.mood.none"), 0.0f, true);
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
    double level = colonist_knowledge_level(colonist, knowledge);
    if (knowledge_is_mastered(level)) {
        tr_decimal(a, sizeof(a), knowledge_work_speed(level), 2);
        TR_FORMAT(lines[count++], HUD_LINE_SIZE, tr("hud.knowledge.speed"), a);
    } else {
        tr_integer(a, sizeof(a), (long long)KNOWLEDGE_MASTERY_LEVEL);
        TR_FORMAT(lines[count++], HUD_LINE_SIZE, tr("hud.knowledge.not_mastered"), a);
    }
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
    SECTION_HEALTH,
    SECTION_MOOD,
    SECTION_CHARACTER,
    SECTION_KNOWLEDGE,
    SECTION_GEAR,
    SECTION_SOCIAL,
    SECTION_JOURNAL,
    SECTION_COUNT
} ColonistSection;

static const char *const SECTION_KEYS[SECTION_COUNT] = {
    [SECTION_SUMMARY] = NULL,
    [SECTION_NEEDS] = N_("inspector.tab.needs"),
    [SECTION_HEALTH] = N_("inspector.tab.health"),
    [SECTION_MOOD] = N_("inspector.tab.mood"),
    [SECTION_CHARACTER] = N_("inspector.tab.character"),
    [SECTION_KNOWLEDGE] = N_("inspector.tab.knowledge"),
    [SECTION_GEAR] = N_("inspector.tab.gear"),
    [SECTION_SOCIAL] = N_("inspector.tab.social"),
    [SECTION_JOURNAL] = N_("inspector.tab.journal"),
};

/* Wetness, the warmth of its sleep, its bed. */
static void add_needs_extra_lines(App *app, Panel *panel, const Colonist *colonist, EntityId id)
{
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
    float sleep_factor = colonist_sleep_temperature_factor(colonist, sleep_celsius);
    if (colonist->task == TASK_SLEEP && !colonist_is_moving(colonist) && sleep_factor < 1.0f) {
        char celsius[32], factor[16];
        temperature_text(celsius, sizeof(celsius), sleep_celsius, 0);
        tr_decimal(factor, sizeof(factor), sleep_factor, 2);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE,
                  tr(sleep_celsius < colonist_cold_limit(colonist) ? "hud.colonist.cold_sleep"
                                                                   : "hud.colonist.hot_sleep"),
                  celsius, factor);
    }
    TilePos bed;
    if (beds_owned(&app->game.beds, (uint8_t)(id.index + 1), &bed)) {
        StructureType type = map_structure(&app->game.map, bed.x, bed.y, bed.z);
        char quality[16];
        hud_quality_text(quality, sizeof(quality),
                         map_structure_quality(&app->game.map, bed.x, bed.y, bed.z));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.bed"),
                  tr(structure_def(type)->name_key), quality);
    } else {
        panel_add_text(panel, tr("hud.colonist.no_bed"));
    }
}

/* A need as a table row: its name, and a bar; the tooltip has the details. */
static void need_row(UiLayout *layout, App *app, const Colonist *colonist, NeedType need)
{
    SDL_FRect row = ui_layout_bar_row(layout, tr(need_name_key(need)), colonist->needs[need], true);
    if (layout->draw && ui_hovered(layout->ui, row)) {
        need_tooltip(app, colonist, need);
    }
    int step = app->character_editor ? ui_layout_stepper(layout, row) : 0;
    if (step != 0) {
        game_debug_colonist_need(&app->game, app->selected[0], need, step);
    }
}

/* The summary of the selected colonist, as a table: age and activity as text,
 * needs and mood as bars (the details are in tooltips). */
static void summary_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Colonist *colonist = game_colonist_const(&app->game, app->selected[0]);
    if (colonist == NULL) {
        return;
    }
    const SDL_Color text = layout->ui->theme.colors[THEME_TEXT];
    char age[16];
    tr_integer(age, sizeof(age), colonist->age);
    ui_layout_pair(layout, tr("hud.summary.age"), age, text, false);
    ui_layout_pair(layout, tr("hud.summary.activity"), tr(colonist_activity_key(colonist)), text,
                   false);
    ui_layout_set_stepper(layout, app->character_editor);
    for (int need = 0; need < NEED_COUNT; need++) {
        need_row(layout, app, colonist, (NeedType)need);
    }
    ui_layout_set_stepper(layout, false);
    SDL_FRect row =
        ui_layout_bar_row(layout, tr("hud.summary.mood"),
                          (colonist->mood.value - MOOD_MIN) / (MOOD_MAX - MOOD_MIN), true);
    if (layout->draw && ui_hovered(layout->ui, row)) {
        mood_tooltip(app, &colonist->mood);
    }
}

/* Every need as a table row, then wetness, the warmth of its sleep and its bed. */
static void needs_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    EntityId id = app->selected[0];
    const Colonist *colonist = game_colonist_const(&app->game, id);
    if (colonist == NULL) {
        return;
    }
    ui_layout_set_stepper(layout, app->character_editor);
    for (int need = 0; need < NEED_COUNT; need++) {
        need_row(layout, app, colonist, (NeedType)need);
    }
    ui_layout_set_stepper(layout, false);
    Panel extra = {0};
    add_needs_extra_lines(app, &extra, colonist, id);
    panel_rows(layout, &extra);
}

/* The Health tab (game/health_panel.h). */
static void health_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Colonist *colonist = game_colonist_const(&app->game, app->selected[0]);
    if (colonist != NULL) {
        health_panel_rows(layout, &colonist->health);
        TemperatureRanges ranges = colonist_temperature_ranges(colonist);
        float felt = colonist_felt_temperature(colonist, &app->game.workstations, &app->game.map,
                                               &app->game.temperatures);
        health_panel_temperature_rows(layout, felt, &ranges, true);
    }
}

/* Strength, what it carries, its tools. */
static void add_gear_lines(Panel *panel, const Colonist *colonist)
{
    char strength[16], carry[32];
    tr_integer(strength, sizeof(strength), colonist->strength);
    hud_weight_text(carry, sizeof(carry), colonist_max_carry_weight(colonist));
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.strength"), strength, carry);
    if (colonist->carried.count > 0) {
        char count[16], weight[32];
        tr_integer(count, sizeof(count), colonist->carried.count);
        hud_weight_text(weight, sizeof(weight), colonist_carried_weight(colonist));
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
        char quality[16];
        hud_quality_text(quality, sizeof(quality), tool.quality);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.tool"),
                  tr(item_def((ItemType)tool.type)->name_key), quality, percent);
    }
    if (colonist->tools_broken > 0) {
        char count[16];
        tr_integer(count, sizeof(count), colonist->tools_broken);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE,
                  tr_plural("hud.colonist.tools_broken", colonist->tools_broken), count);
    }
}

/* Discovered knowledges the settler has started to learn, as a table: the
 * level with one decimal, from red (0) through white (the mastery, 50) to
 * green (100). The work speed is in the tooltip. */
static void knowledge_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Colonist *colonist = game_colonist_const(&app->game, app->selected[0]);
    if (colonist == NULL) {
        return;
    }
    ui_layout_label(layout, tr("hud.colonist.knowledge"), 0.0f, true);
    /* The editor also lists the knowledges at level 0, to raise them. */
    ui_layout_set_stepper(layout, app->character_editor);
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        double value = colonist_knowledge_level(colonist, (KnowledgeType)knowledge);
        if ((!(value > 0.0) && !app->character_editor) ||
            !game_knowledge_discovered(&app->game, (KnowledgeType)knowledge)) {
            continue;
        }
        char level[16];
        format_level(level, sizeof(level), value);
        SDL_FRect row = ui_layout_pair(
            layout, tr(knowledge_def((KnowledgeType)knowledge)->name_key), level,
            theme_scale_color(&layout->ui->theme, (float)(value / KNOWLEDGE_LEVEL_MAX)), true);
        if (layout->draw && ui_hovered(layout->ui, row)) {
            knowledge_tooltip(app, colonist, (KnowledgeType)knowledge);
        }
        int step = app->character_editor ? ui_layout_stepper(layout, row) : 0;
        if (step != 0) {
            game_debug_colonist_knowledge(&app->game, app->selected[0], (KnowledgeType)knowledge,
                                          step);
        }
    }
    ui_layout_set_stepper(layout, false);
}

/* The Character tab (game/attributes_panel.h). */
static void character_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Colonist *colonist = game_colonist_const(&app->game, app->selected[0]);
    if (colonist != NULL) {
        AttributeType edited = ATTRIBUTE_COUNT;
        int step =
            attributes_panel_rows(layout, &colonist->attributes, app->character_editor, &edited);
        ui_layout_set_stepper(layout, false);
        if (step != 0) {
            game_debug_colonist_attribute(&app->game, app->selected[0], edited, step);
        }
    }
}

/* The Journal tab (game/journal_panel.h). */
static void journal_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Colonist *colonist = game_colonist_const(&app->game, app->selected[0]);
    if (colonist != NULL) {
        journal_panel_rows(layout, &colonist->journal, (JournalOwner){colonist->name, 0, 0},
                           &app->journal_hidden);
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
    case SECTION_SUMMARY: break; /* summary_rows() */
    case SECTION_NEEDS:
    case SECTION_HEALTH:
    case SECTION_MOOD:
    case SECTION_CHARACTER:
    case SECTION_KNOWLEDGE:
    case SECTION_JOURNAL: break; /* Drawn by their own rows (colonist_tab_rows()). */
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
    const Items *items = game_view_items(&app->game);
    TileRect visible = camera_visible_tiles(&app->camera, items->width, items->height);
    char count[16];
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            ItemStack stack = items_get(game_items_at(&app->game, x, y, z), x, y, z);
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

/* The rows of the tabs that are more than lines of text; NULL for the others. */
static UiRowsFn colonist_tab_rows(ColonistSection section)
{
    switch (section) {
    case SECTION_NEEDS: return needs_rows;
    case SECTION_HEALTH: return health_rows;
    case SECTION_MOOD: return mood_rows;
    case SECTION_CHARACTER: return character_rows;
    case SECTION_KNOWLEDGE: return knowledge_rows;
    case SECTION_JOURNAL: return journal_rows;
    default: return NULL;
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
    bool open = one ? inspector_box(app, INSPECT_COLONIST, title, summary_rows, app)
                    : inspector_box(app, INSPECT_COLONISTS, title, panel_rows, &summary);
    inspector_move_action(app, 0);
    if (one) {
        inspector_follow_action(app, 1, FOLLOW_COLONIST, app->selected[0]);
        const char *labels[SECTION_COUNT - 1];
        for (int i = 1; i < SECTION_COUNT; i++) {
            labels[i - 1] = tr(SECTION_KEYS[i]);
        }
        int tab = inspector_tabs(app, labels, SECTION_COUNT - 1);
        if (tab >= 0) {
            ColonistSection section = (ColonistSection)(tab + 1);
            UiRowsFn special = colonist_tab_rows(section);
            if (special != NULL) {
                inspector_tab_panel(app, special, app);
                if (section == SECTION_HEALTH) {
                    health_panel_body_window(app, &colonist->health);
                }
            } else {
                Panel lines = {0};
                build_colonist_section(app, &lines, app->selected[0], section);
                inspector_tab_panel(app, panel_rows, &lines);
            }
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
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.game"), app_key_name(APP_KEY_LANGUAGE),
              app_key_name(APP_KEY_SLOWER), app_key_name(APP_KEY_FASTER));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.gathering"),
              app_key_name(APP_KEY_GATHER));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.orders"), app_key_name(APP_KEY_MINE),
              app_key_name(APP_KEY_CANCEL));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.manage"),
              app_key_name(APP_KEY_STOCKPILE), app_key_name(APP_KEY_WORK),
              app_key_name(APP_KEY_LOG));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.copy"),
              app_key_name(APP_KEY_BUILD_LIKE));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.rotate"), app_key_name(APP_KEY_ROTATE));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.temperature"),
              app_key_name(APP_KEY_TEMPERATURE));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.light"), app_key_name(APP_KEY_LIGHT));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.moisture"),
              app_key_name(APP_KEY_MOISTURE));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.roofs"), app_key_name(APP_KEY_LEVEL_UP),
              tr("designation.deconstruct"));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.follow"), app_key_name(APP_KEY_FOLLOW));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.levels"),
              app_key_name(APP_KEY_LEVEL_UP), app_key_name(APP_KEY_LEVEL_DOWN));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.debug"),
              app_key_name(APP_KEY_DEBUG_MENU), app_key_name(APP_KEY_ITEMS_MENU),
              app_key_name(APP_KEY_ANIMALS_MENU), app_key_name(APP_KEY_SCENARIOS_MENU));
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
    case APP_WINDOW_LOG: log_window_draw(app); break;
    case APP_WINDOW_HELP: help_window(app); break;
    case APP_WINDOW_DEBUG: debug_panel_draw(app); break;
    case APP_WINDOW_ITEMS: items_panel_draw(app); break;
    case APP_WINDOW_ANIMALS: animals_panel_draw(app); break;
    case APP_WINDOW_SCENARIOS: scenarios_panel_draw(app); break;
    case APP_WINDOW_GAME_MENU: game_menu_draw(app); break;
    case APP_WINDOW_SETTINGS: settings_window_draw(app); break;
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
    if (app->show_log) {
        out[count++] = APP_WINDOW_LOG;
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
    if (app->show_animals_menu) {
        out[count++] = APP_WINDOW_ANIMALS;
    }
    if (app->show_scenarios_menu) {
        out[count++] = APP_WINDOW_SCENARIOS;
    }
    if (app->show_game_menu) {
        out[count++] = APP_WINDOW_GAME_MENU;
    }
    if (app->show_settings) {
        out[count++] = APP_WINDOW_SETTINGS;
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

/* A line of notice centered under the colonist bar at `y`, `alpha` opaque.
 * Returns where the next one goes. */
static float draw_notice_line(App *app, const char *text, float y, float alpha)
{
    float line = (float)text_line_height(app->text);
    int width, height;
    text_measure(app->text, text, &width, &height);
    SDL_FRect box = {SDL_floorf((app->camera.viewport_width - (float)width) * 0.5f) - HUD_PADDING,
                     y, (float)width + 2.0f * HUD_PADDING, line + HUD_PADDING};
    SDL_Color background = THEME_COLOR(app, THEME_HUD_PANEL);
    SDL_Color color = THEME_COLOR(app, THEME_TEXT);
    background.a = (Uint8)((float)background.a * alpha);
    color.a = (Uint8)((float)color.a * alpha);
    SDL_SetRenderDrawColor(app->renderer, background.r, background.g, background.b, background.a);
    SDL_RenderFillRect(app->renderer, &box);
    text_draw(app->text, text, box.x + HUD_PADDING, box.y + SDL_floorf(HUD_PADDING * 0.5f), color);
    return y + box.h + 2.0f;
}

/* The notices on screen, centered under the colonist bar, oldest first; each
 * fades out at the end of its time. Once the colony is lost, that stays on
 * top. */
static void draw_notices(App *app)
{
    float y = colonist_bar_bottom(app) + HUD_MARGIN;
    if (app->game.colony_lost) {
        y = draw_notice_line(app, tr("hud.colony_lost"), y, 1.0f);
    }
    for (int i = 0; i < app->notices_shown_count; i++) {
        const Notice *notice = notices_get(&app->game.notices, app->notices_shown[i]);
        if (notice == NULL) {
            continue;
        }
        char text[HUD_LINE_SIZE];
        log_notice_text(notice, text, sizeof(text));
        double left = APP_NOTICE_SECONDS - app->notices_age[i];
        y = draw_notice_line(app, text, y, (float)SDL_clamp(left / NOTICE_FADE_SECONDS, 0.0, 1.0));
    }
}

/* Top of the docked windows at the bottom left (inspector, work grid), as
 * drawn last frame; the bottom of the screen when none is open. */
static float left_windows_top(const App *app)
{
    AppWindow windows[4];
    int count = 0;
    if (inspector_kind(app) != INSPECT_NONE) {
        windows[count++] = APP_WINDOW_INSPECTOR;
        if (app->inspector_tab >= 0) {
            windows[count++] = APP_WINDOW_INSPECTOR_TAB;
        }
    }
    if (app->show_work_panel) {
        windows[count++] = APP_WINDOW_WORK;
    }
    if (app->show_log) {
        windows[count++] = APP_WINDOW_LOG;
    }
    float top = app->ui.screen_height;
    for (int i = 0; i < count; i++) {
        SDL_FRect rect = ui_window_rect(&app->ui, (int)windows[i]);
        if (rect.h > 0.0f) {
            top = SDL_min(top, rect.y);
        }
    }
    return top;
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
    /* Above it, the alerts. */
    alerts_list_draw(app, status_right, status_bottom - height - app->ui.theme.gap);

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
    float left_top = menu_top; /* Top of what the left side shows, under the resources. */
    /* The main windows take the place of the readout. */
    if (app->hover_valid && inspector_kind(app) == INSPECT_NONE && !app->show_work_panel &&
        !app->show_log) {
        Panel tile = {0};
        build_tile_panel(app, &tile);
        panel_size(app, &tile, &width, &height);
        float tile_y = menu_top - height - HUD_MARGIN;
        panel_draw_colored(app, &tile, HUD_MARGIN, tile_y, THEME_COLOR(app, THEME_READOUT));
        left_top = SDL_min(left_top, tile_y);
    }
    left_top = SDL_min(left_top, left_windows_top(app));

    /* Top left: the resources list. */
    resources_list_draw(app, left_top - app->ui.theme.margin);

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
