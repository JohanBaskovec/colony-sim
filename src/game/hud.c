#include "game/hud.h"

#include "core/i18n.h"
#include "game/station_panel.h"
#include "game/stockpile_panel.h"
#include "game/work_panel.h"

#define HUD_MARGIN 8.0f
#define HUD_PADDING 6.0f
#define HUD_MAX_LINES 24
#define HUD_LINE_SIZE 256

static const SDL_Color HUD_TEXT_COLOR = {236, 236, 228, 255};
static const SDL_Color HUD_PANEL_COLOR = {12, 14, 18, 170};
static const SDL_Color LABEL_SHADOW_COLOR = {0, 0, 0, 200};

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

static void panel_draw(App *app, const Panel *panel, float x, float y)
{
    if (panel->count == 0) {
        return;
    }
    float width, height;
    panel_size(app, panel, &width, &height);
    SDL_FRect background = {x, y, width, height};
    SDL_SetRenderDrawColor(app->renderer, HUD_PANEL_COLOR.r, HUD_PANEL_COLOR.g,
                           HUD_PANEL_COLOR.b, HUD_PANEL_COLOR.a);
    SDL_RenderFillRect(app->renderer, &background);

    float line_height = (float)text_line_height(app->text);
    for (int i = 0; i < panel->count; i++) {
        text_draw(app->text, panel->lines[i], x + HUD_PADDING,
                  y + HUD_PADDING + (float)i * line_height, HUD_TEXT_COLOR);
    }
}

/* Names of the keys at the physical W/A/S/D positions ("ZQSD" on AZERTY keyboards). */
static void movement_keys_label(char *out, size_t size)
{
    static const SDL_Scancode SCANCODES[] = {SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S,
                                             SDL_SCANCODE_D};
    out[0] = '\0';
    for (size_t i = 0; i < SDL_arraysize(SCANCODES); i++) {
        SDL_Keycode key = SDL_GetKeyFromScancode(SCANCODES[i], SDL_KMOD_NONE, false);
        SDL_strlcat(out, SDL_GetKeyName(key), size);
    }
}

/* Name of the key with this label on the current layout (mnemonic keys). */
static const char *key_name(SDL_Keycode key)
{
    return SDL_GetKeyName(key);
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

static const char *tool_name_key(DesignationType type)
{
    const DesignationDef *def = designation_def(type);
    return type == DESIGNATION_NONE || def == NULL ? N_("tool.cancel") : def->name_key;
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
    } else {
        char multiplier[8];
        tr_integer(multiplier, sizeof(multiplier), game_speed_multiplier(clock->speed));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.speed"), multiplier);
    }
}

static void build_tile_panel(App *app, Panel *panel)
{
    const Map *map = &app->game.map;
    int x = app->hover_x, y = app->hover_y;
    size_t index = map_index(map, x, y);
    char a[32], b[32];

    tr_integer(a, sizeof(a), x);
    tr_integer(b, sizeof(b), y);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.position"), a, b);

    const TerrainDef *terrain = terrain_def((Terrain)map->terrain[index]);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.terrain"), tr(terrain->name_key));

    if (map->rock[index] != ROCK_NONE) {
        const RockDef *rock = rock_def((RockType)map->rock[index]);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.rock"), tr(rock->name_key));
    }

    const Plant *plant = &map->plants[index];
    if (plant->type != PLANT_NONE) {
        tr_integer(a, sizeof(a), (long long)(plant->growth * 100.0f + 0.5f));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.plant"),
                  tr(plant_def((PlantType)plant->type)->name_key), a);
        for (int stock = 0; stock < PLANT_STOCK_COUNT; stock++) {
            if (plant_stock_capacity(plant, (PlantStock)stock) <= 0) {
                continue;
            }
            tr_integer(a, sizeof(a), plant_stock_available(plant, (PlantStock)stock));
            tr_integer(b, sizeof(b), plant_stock_capacity(plant, (PlantStock)stock));
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.plant_stock"),
                      tr(item_def(plant_stock_item((PlantStock)stock))->name_key), a, b);
        }
    }

    ItemStack stack = items_get(&app->game.items, x, y);
    if (stack.type != ITEM_NONE) {
        char weight[32];
        tr_integer(a, sizeof(a), stack.count);
        format_weight(weight, sizeof(weight), item_stack_weight((ItemType)stack.type, stack.count));
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.items"),
                  tr(item_def((ItemType)stack.type)->name_key), a, weight);
        add_item_condition_line(panel, stack);
        const ItemDef *def = item_def((ItemType)stack.type);
        if (def->transforms_into != ITEM_NONE && def->transform_hours > stack.progress) {
            tr_integer(a, sizeof(a), def->transform_hours - stack.progress);
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.item_transform"),
                      tr(item_def(def->transforms_into)->name_key), a);
        }
    }

    const Workstation *station = workstation_get_const(
        &app->game.workstations, workstation_at(&app->game.workstations, x, y));
    if (station != NULL) {
        const char *name = tr(workstation_def((WorkstationType)station->type)->name_key);
        if (station->built) {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.station"), name);
        } else {
            tr_integer(a, sizeof(a), (long long)(workstation_build_fraction(station) * 100.0f));
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.blueprint"), name, a);
        }
    }

    /* Flint in the ground: what gathering stones may turn up. */
    int flint = map_flint_chance(map, x, y);
    if (flint > 0) {
        panel_add_text(panel, tr(flint >= MAP_FLINT_RICH_CHANCE ? "hud.tile.flint_plenty"
                                                                 : "hud.tile.flint_some"));
    }

    const Stockpile *stockpile =
        stockpile_get(&app->game.stockpiles, stockpile_at(&app->game.stockpiles, x, y));
    if (stockpile != NULL) {
        char name[HUD_LINE_SIZE];
        tr_integer(a, sizeof(a), stockpile->number);
        TR_FORMAT(name, sizeof(name), tr("stockpile.name"), a);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.stockpile"), name);
    }

    DesignationType designation = designation_get(&app->game.designations, x, y);
    if (designation != DESIGNATION_NONE) {
        const char *name = tr(designation_def(designation)->name_key);
        float progress = designation_progress_fraction(&app->game.designations, map, x, y);
        if (progress > 0.0f) {
            tr_integer(a, sizeof(a), (long long)(progress * 100.0f));
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.designation_progress"),
                      name, a);
        } else {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.tile.designation"), name);
        }
    }

    float cost = map_move_cost(map, x, y);
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
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.need"),
                  tr(need_name_key(need)), percent);
    } else {
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.need_state"),
                  tr(need_name_key(need)), percent, tr(state_key));
    }
}

static void build_selection_panel(App *app, Panel *panel)
{
    if (app->selected_count != 1) {
        char count[16];
        tr_integer(count, sizeof(count), app->selected_count);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE,
                  tr_plural("hud.selection.count", app->selected_count), count);
        return;
    }
    const Colonist *colonist = game_colonist_const(&app->game, app->selected[0]);
    if (colonist == NULL) {
        return;
    }
    char age[16];
    tr_integer(age, sizeof(age), colonist->age);
    panel_add_text(panel, colonist->name);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.age"), age);
    char strength[16], carry[32];
    tr_integer(strength, sizeof(strength), colonist->strength);
    format_weight(carry, sizeof(carry), colonist->max_carry_weight);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.strength"), strength, carry);
    TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.activity"),
              tr(colonist_activity_key(colonist)));
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
    for (int need = 0; need < NEED_COUNT; need++) {
        add_need_line(panel, colonist, (NeedType)need);
    }
    /* Knowledges the settler has started to learn: level with one decimal,
     * and the work speed once it is high enough to use. */
    panel_add_text(panel, tr("hud.colonist.knowledge"));
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        double value = colonist_knowledge_level(colonist, (KnowledgeType)knowledge);
        if (!(value > 0.0)) {
            continue;
        }
        const char *name = tr(knowledge_def((KnowledgeType)knowledge)->name_key);
        char level[16];
        tr_decimal(level, sizeof(level), (float)(SDL_floor(value * 10.0) / 10.0), 1);
        if (!knowledge_is_mastered(value)) {
            TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.knowledge_learning"),
                      name, level);
            continue;
        }
        char speed[16];
        tr_decimal(speed, sizeof(speed), knowledge_work_speed(value), 2);
        TR_FORMAT(panel_add(panel), HUD_LINE_SIZE, tr("hud.colonist.knowledge_level"), name,
                  level, speed);
    }
}

void hud_draw_item_counts(App *app)
{
    if (app->camera.zoom < HUD_ITEM_COUNT_MIN_ZOOM) {
        return;
    }
    const Items *items = &app->game.items;
    TileRect visible = camera_visible_tiles(&app->camera, items->width, items->height);
    char count[16];
    for (int y = visible.y0; y < visible.y1; y++) {
        for (int x = visible.x0; x < visible.x1; x++) {
            ItemStack stack = items_get(items, x, y);
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
            text_draw(app->text, count, tx + 1.0f, ty + 1.0f, LABEL_SHADOW_COLOR);
            text_draw(app->text, count, tx, ty, HUD_TEXT_COLOR);
        }
    }
}

void hud_draw_world_labels(App *app)
{
    if (app->camera.zoom < HUD_LABEL_MIN_ZOOM) {
        return;
    }
    const Game *game = &app->game;
    float offset = app->camera.zoom * 0.45f + 2.0f;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (entity_id_is_none(game_colonist_id_at(game, slot))) {
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
        text_draw(app->text, colonist->name, x + 1.0f, y + 1.0f, LABEL_SHADOW_COLOR);
        text_draw(app->text, colonist->name, x, y, HUD_TEXT_COLOR);
    }
}

void hud_draw(App *app)
{
    const float screen_width = app->camera.viewport_width;
    const float screen_height = app->camera.viewport_height;
    float width, height;

    /* Top left: date and speed, then the debug overlay below. */
    Panel status = {0};
    build_status_panel(app, &status);
    panel_draw(app, &status, HUD_MARGIN, HUD_MARGIN);

    if (app->show_debug) {
        Panel debug = {0};
        build_debug_panel(app, &debug);
        panel_size(app, &status, &width, &height);
        panel_draw(app, &debug, HUD_MARGIN, HUD_MARGIN * 2.0f + height);
    }

    /* Top center: active tool. */
    if (app->tool != APP_TOOL_NONE) {
        Panel tool = {0};
        if (app->tool == APP_TOOL_STOCKPILE) {
            TR_FORMAT(panel_add(&tool), HUD_LINE_SIZE, tr("hud.tool.name"),
                      tr("tool.stockpile"));
            panel_add_text(&tool, tr("hud.tool.stockpile_help"));
        } else if (app->tool == APP_TOOL_BUILD) {
            const WorkstationDef *def = workstation_def(app->build_type);
            TR_FORMAT(panel_add(&tool), HUD_LINE_SIZE, tr("hud.tool.name"), tr(def->name_key));
            if (def->material != ITEM_NONE) {
                char count[16];
                tr_integer(count, sizeof(count), def->material_count);
                TR_FORMAT(panel_add(&tool), HUD_LINE_SIZE, tr("hud.tool.build_cost"),
                          tr(item_def(def->material)->name_key), count);
            }
            TR_FORMAT(panel_add(&tool), HUD_LINE_SIZE, tr("hud.tool.build_help"),
                      key_name(SDLK_E));
            if (!game_workstation_buildable(&app->game, app->build_type)) {
                panel_add_text(&tool, tr("hud.tool.unavailable"));
            }
        } else {
            TR_FORMAT(panel_add(&tool), HUD_LINE_SIZE, tr("hud.tool"),
                      tr(tool_name_key(app->tool_type)));
            if (app->tool_type != DESIGNATION_NONE &&
                !game_designation_available(&app->game, app->tool_type)) {
                panel_add_text(&tool, tr("hud.tool.unavailable"));
            }
        }
        panel_size(app, &tool, &width, &height);
        panel_draw(app, &tool, SDL_floorf((screen_width - width) * 0.5f), HUD_MARGIN);
    }

    /* Top right: current language. */
    Panel language = {0};
    const LanguageInfo *info = localization_active_language(&app->localization);
    TR_FORMAT(panel_add(&language), HUD_LINE_SIZE, tr("hud.language"),
              info != NULL ? info->name : "?");
    panel_size(app, &language, &width, &height);
    panel_draw(app, &language, screen_width - width - HUD_MARGIN, HUD_MARGIN);

    /* Bottom: controls help, with the tile inspector above it. */
    Panel help = {0};
    char keys[32];
    movement_keys_label(keys, sizeof(keys));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.camera"), keys);
    panel_add_text(&help, tr("hud.help.game"));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.gathering"), key_name(SDLK_H),
              key_name(SDLK_R), key_name(SDLK_B), key_name(SDLK_K), key_name(SDLK_G));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.orders"), key_name(SDLK_C),
              key_name(SDLK_M), key_name(SDLK_X));
    TR_FORMAT(panel_add(&help), HUD_LINE_SIZE, tr("hud.help.manage"), key_name(SDLK_P),
              key_name(SDLK_E), key_name(SDLK_T));
    float help_width, help_height;
    panel_size(app, &help, &help_width, &help_height);
    float help_y = screen_height - help_height - HUD_MARGIN;
    panel_draw(app, &help, HUD_MARGIN, help_y);

    /* Bottom right: inspector of the selected colonist(s), station or stockpile. */
    if (app->selected_station != WORKSTATION_NONE) {
        station_panel_draw(app, screen_width - HUD_MARGIN, help_y - HUD_MARGIN);
    } else if (app->selected_stockpile != STOCKPILE_NONE) {
        stockpile_panel_draw(app, screen_width - HUD_MARGIN, help_y - HUD_MARGIN);
    } else if (app->selected_count > 0) {
        Panel selection = {0};
        build_selection_panel(app, &selection);
        panel_size(app, &selection, &width, &height);
        panel_draw(app, &selection, screen_width - width - HUD_MARGIN,
                   help_y - height - HUD_MARGIN);
    }

    if (app->hover_valid) {
        Panel tile = {0};
        build_tile_panel(app, &tile);
        panel_size(app, &tile, &width, &height);
        panel_draw(app, &tile, HUD_MARGIN, help_y - height - HUD_MARGIN);
    }

    /* Last, so that it is drawn over the other panels. */
    work_panel_draw(app);
}
