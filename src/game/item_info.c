#include "game/item_info.h"

#include "core/combat.h"
#include "core/game_clock.h"
#include "core/i18n.h"
#include "core/tool.h"
#include "core/weapon.h"
#include "core/wood.h"
#include "game/hud.h"
#include "game/render.h"

#define GAP UI_LAYOUT_GAP
#define TEXT_SIZE 192

SDL_FRect item_info_row(UiLayout *layout, const char *label_key, const char *value, bool hoverable)
{
    return ui_layout_pair(layout, tr(label_key), value, layout->ui->theme.colors[THEME_TEXT],
                          hoverable);
}

void item_info_row_tip(UiLayout *layout, const char *label_key, const char *value,
                       const char *const *lines, int count)
{
    SDL_FRect row = item_info_row(layout, label_key, value, count > 0);
    if (count > 0 && layout->draw && ui_hovered(layout->ui, row)) {
        ui_tooltip(layout->ui, lines, count);
    }
}

void item_info_heading(UiLayout *layout, const char *key)
{
    ui_layout_label(layout, tr(key), 0.0f, true);
}

void item_info_quantity_text(char *out, size_t size, float value, const char *unit_key)
{
    int decimals = value < 0.1f ? 2 : (SDL_fabsf(value - SDL_roundf(value)) > 0.05f ? 1 : 0);
    char number[32];
    tr_decimal(number, sizeof(number), value, decimals);
    if (unit_key == NULL) {
        SDL_strlcpy(out, number, size);
    } else {
        TR_FORMAT(out, size, tr(unit_key), number);
    }
}

void item_info_percent_text(char *out, size_t size, float fraction)
{
    char number[16];
    tr_integer(number, sizeof(number), (long long)(SDL_clamp(fraction, 0.0f, 1.0f) * 100.0f));
    TR_FORMAT(out, size, tr("unit.percent"), number);
}

void item_info_picture(App *app, SDL_FRect rect, ItemType type)
{
    ui_fill(&app->ui, rect, app->ui.theme.colors[THEME_ICON_WELL]);
    render_item_icon(app->renderer, app->sprites, rect, type);
}

/* The species an item is of, and what they make of it in the tooltip: weight,
 * how long it lasts, the quality of a shaft. */
static void wood_row(UiLayout *layout, WoodMask woods)
{
    char text[TEXT_SIZE], number[16];
    if (woods == 0) {
        SDL_strlcpy(text, tr("item.info.wood_any"), sizeof(text));
        item_info_row(layout, "item.info.wood", text, false);
        return;
    }
    wood_list_name(woods, text, sizeof(text));
    char lines[3][TEXT_SIZE];
    const char *shown[3];
    tr_decimal(number, sizeof(number), wood_weight(woods), 2);
    TR_FORMAT(lines[0], TEXT_SIZE, tr("item.info.wood.weight"), number);
    tr_decimal(number, sizeof(number), wood_toughness(woods), 2);
    TR_FORMAT(lines[1], TEXT_SIZE, tr("item.info.wood.toughness"), number);
    char signed_number[16];
    tr_integer(number, sizeof(number), wood_shafts(woods));
    SDL_snprintf(signed_number, sizeof(signed_number), "%s%s", wood_shafts(woods) > 0 ? "+" : "",
                 number);
    TR_FORMAT(lines[2], TEXT_SIZE, tr("item.info.wood.shafts"), signed_number);
    for (int i = 0; i < 3; i++) {
        shown[i] = lines[i];
    }
    item_info_row_tip(layout, "item.info.wood", text, shown, 3);
}

void item_info_traits(UiLayout *layout, ItemStack one, bool hit_points)
{
    const ItemDef *def = item_def((ItemType)one.type);
    if (def == NULL) {
        return;
    }
    char text[TEXT_SIZE], number[16];
    item_info_quantity_text(text, sizeof(text), item_stack_mass(one), "unit.kg");
    item_info_row(layout, "item.info.weight", text, false);
    item_info_quantity_text(text, sizeof(text), def->bulk, NULL);
    item_info_row(layout, "item.info.bulk", text, false);
    if (def->length_cm > 0) {
        tr_integer(number, sizeof(number), def->length_cm);
        TR_FORMAT(text, sizeof(text), tr("item.info.size_cm"), number);
    } else {
        SDL_strlcpy(text, tr("item.info.size_loose"), sizeof(text));
    }
    item_info_row(layout, "item.info.size", text, false);
    const char *form = item_form_name_key(def->form);
    if (form != NULL) {
        item_info_row(layout, "item.info.form", tr(form), false);
    }
    if (hit_points && def->damageable) {
        tr_integer(number, sizeof(number), def->max_hit_points);
        item_info_row(layout, "item.info.hit_points", number, false);
    }
    if (def->wooden) {
        wood_row(layout, one.woods);
    }
    if (def->nutrition > 0.0f) {
        item_info_percent_text(text, sizeof(text), def->nutrition);
        item_info_row(layout, "item.info.nutrition", text, false);
    }
    if (def->rot_days > 0.0f) {
        char celsius[32], temperature[32];
        tr_decimal(number, sizeof(number), def->rot_days, def->rot_days < 10.0f ? 1 : 0);
        tr_integer(celsius, sizeof(celsius), (long long)ITEM_ROT_REFERENCE_CELSIUS);
        TR_FORMAT(temperature, sizeof(temperature), tr("unit.celsius"), celsius);
        TR_FORMAT(text, sizeof(text), tr("item.info.keeps"), number, temperature);
        item_info_row(layout, "item.info.rot", text, false);
    }
}

void item_info_tool_rows(UiLayout *layout, ItemType type)
{
    bool heading = false;
    for (int use = 0; use < TOOL_USE_COUNT; use++) {
        float factor = tool_type_factor((ToolUse)use, type);
        if (factor <= 0.0f) {
            continue;
        }
        if (!heading) {
            layout->y += GAP;
            item_info_heading(layout, "item.info.heading.tool");
            heading = true;
        }
        char number[16], text[TEXT_SIZE];
        tr_decimal(number, sizeof(number), factor, 1);
        TR_FORMAT(text, sizeof(text), tr("item.info.tool_speed"), number);
        ui_layout_pair(layout, tr(tool_use_def((ToolUse)use)->name_key), text,
                       layout->ui->theme.colors[THEME_TEXT], false);
    }
}

void item_info_weapon_rows(UiLayout *layout, ItemType type)
{
    const WeaponDef *weapon = weapon_def(type);
    const AttackDef *attack = weapon != NULL ? attack_def((AttackType)weapon->attack) : NULL;
    if (attack == NULL) {
        return;
    }
    layout->y += GAP;
    item_info_heading(layout, "item.info.heading.weapon");
    char text[TEXT_SIZE], number[16];
    item_info_row(layout, "item.info.attack", tr(attack->name_key), false);
    item_info_row(layout, "item.info.hands",
                  tr(weapon->hands >= 2 ? "item.info.two_hands" : "item.info.one_hand"), false);
    tr_decimal(number, sizeof(number), attack->damage, 1);
    const char *damage_tip = tr("item.info.damage.description");
    item_info_row_tip(layout, "item.info.damage", number, &damage_tip, 1);
    tr_decimal(number, sizeof(number), attack->interval, 1);
    TR_FORMAT(text, sizeof(text), tr("item.info.blow_every"), number);
    item_info_row(layout, "item.info.pace", text, false);
    item_info_percent_text(text, sizeof(text), attack->accuracy);
    item_info_row(layout, "item.info.accuracy", text, false);
    tr_decimal(number, sizeof(number), attack->damage / attack->interval, 1);
    char line[TEXT_SIZE], hits[16];
    tr_decimal(hits, sizeof(hits), attack_worth((AttackType)weapon->attack), 1);
    TR_FORMAT(line, sizeof(line), tr("item.info.dps.description"), hits);
    const char *dps_tip = line;
    item_info_row_tip(layout, "item.info.dps", number, &dps_tip, 1);
}

/* ---- The window of an item ---------------------------------------------- */

typedef struct InfoRows {
    App *app;
    const Item *item;
    ItemStack one;
    int x, y, z; /* The tile it lies on. */
    /* What the player clicked, applied after the window. */
    bool go_to_tile;
    EntityId go_to_maker;
} InfoRows;

/* The picture of the item, its name and its category beside it. */
static void header_rows(UiLayout *layout, const InfoRows *rows)
{
    const ItemDef *def = item_def((ItemType)rows->one.type);
    const char *name = hud_item_name(rows->one);
    const char *category = tr(item_category_name_key(def->category));
    Ui *ui = layout->ui;
    float size = SDL_roundf(layout->row * 2.6f);
    float text_x = size + GAP;
    ui_layout_measure(layout,
                      text_x + SDL_max(ui_text_width(ui, name), ui_text_width(ui, category)));
    if (layout->draw) {
        item_info_picture(rows->app, (SDL_FRect){layout->x, layout->y, size, size},
                          (ItemType)rows->one.type);
        float line = (float)text_line_height(ui->text);
        float top = layout->y + SDL_roundf((size - 2.0f * line) * 0.5f);
        ui_label(ui, name, layout->x + text_x, top);
        ui_label_dim(ui, category, layout->x + text_x, top + line);
    }
    layout->y += size + GAP;
}

/* The ground the item was told to be dry or wet by: "Dry", "Wet, dries in 5 h". */
static bool humidity_text(char *out, size_t size, const Item *item)
{
    const ItemDef *def = item_def((ItemType)item->type);
    if (def->transforms_into != ITEM_NONE) { /* Fresh grass, wet twigs: they dry. */
        int left = SDL_max(1, def->transform_hours - (int)item->progress);
        char hours[16], time[32];
        tr_integer(hours, sizeof(hours), left);
        TR_FORMAT(time, sizeof(time), tr("unit.hours"), hours);
        TR_FORMAT(out, size,
                  tr(item->type == ITEM_WET_TWIGS ? "item.info.humidity.wet"
                                                  : "item.info.humidity.fresh"),
                  time);
        return true;
    }
    bool dried = def->wets_into != ITEM_NONE;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT && !dried; type++) {
        dried = item_def((ItemType)type)->transforms_into == (ItemType)item->type;
    }
    if (dried) {
        SDL_strlcpy(out, tr("item.info.humidity.dry"), size);
    }
    return dried;
}

/* How it is: quality, hit points, rot, humidity, what is eaten of it. */
static void state_rows(UiLayout *layout, const InfoRows *rows)
{
    const Game *game = &rows->app->game;
    const Item *item = rows->item;
    const ItemDef *def = item_def((ItemType)item->type);
    char text[TEXT_SIZE], a[32], b[32], c[32];
    if (def->has_quality) {
        hud_quality_text(a, sizeof(a), item->quality);
        const char *tip = tr("item.info.quality.description");
        item_info_row_tip(layout, "item.info.quality", a, &tip, 1);
    }
    if (def->damageable) {
        tr_integer(a, sizeof(a), item->hit_points);
        tr_integer(b, sizeof(b), def->max_hit_points);
        item_info_percent_text(c, sizeof(c),
                               (float)item_stack_hit_points_percent(rows->one) / 100.0f);
        TR_FORMAT(text, sizeof(text), tr("item.info.hit_points_of"), a, b, c);
        item_info_row(layout, "item.info.hit_points", text, false);
    }
    if (item_rots((ItemType)item->type)) {
        hud_rot_text(
            text, sizeof(text),
            item_stack_rot_hours(rows->one, game_item_rot_rate(game, rows->x, rows->y, rows->z)));
        item_info_row(layout, "item.info.rotting", text, false);
    }
    if (humidity_text(text, sizeof(text), item)) {
        item_info_row(layout, "item.info.humidity", text, false);
    }
    if (item->eaten > 0) {
        item_info_percent_text(text, sizeof(text), (float)item->eaten / (float)ITEM_EATEN_FULL);
        item_info_row(layout, "item.info.eaten", text, false);
    }
}

/* "Summer, day 11, year 1 · 06:25" for a tick. */
static void date_text(char *out, size_t size, uint64_t tick)
{
    GameDate date = game_date_from_tick(tick);
    char day[16], year[16], hours[8], minutes[8];
    tr_integer(day, sizeof(day), date.day);
    tr_integer(year, sizeof(year), date.year);
    SDL_snprintf(hours, sizeof(hours), "%02d", date.hour);
    SDL_snprintf(minutes, sizeof(minutes), "%02d", date.minute);
    TR_FORMAT(out, size, tr("hud.date"), day, tr(season_name_key(date.season)), year, hours,
              minutes);
}

/* A row that is clicked to go there: its tooltip says what the click does. True when
 * left-clicked this frame (apply it after the rows). */
static bool link_row(UiLayout *layout, const char *label_key, const char *value,
                     const char *tip_key)
{
    SDL_FRect row = item_info_row(layout, label_key, value, true);
    if (!layout->draw) {
        return false;
    }
    if (ui_hovered(layout->ui, row)) {
        const char *tip = tr(tip_key);
        ui_tooltip(layout->ui, &tip, 1);
    }
    return ui_take_click(layout->ui, row) == SDL_BUTTON_LEFT;
}

/* Where it lies and where it comes from: its tile (a click brings the view
 * there), when it came to be and who made it (a click selects them, if they are
 * still among us). */
static void origin_rows(UiLayout *layout, InfoRows *rows)
{
    const Game *game = &rows->app->game;
    char text[TEXT_SIZE], a[32], b[32], c[32];
    tr_integer(a, sizeof(a), rows->x);
    tr_integer(b, sizeof(b), rows->y);
    tr_integer(c, sizeof(c), rows->z);
    TR_FORMAT(text, sizeof(text), tr("item.info.tile_value"), a, b, c);
    if (link_row(layout, "item.info.tile", text, "item.info.tile.description")) {
        rows->go_to_tile = true;
    }
    if (rows->item->made > 0) {
        uint64_t tick = rows->item->made - 1;
        if (tick == 0) {
            item_info_row(layout, "item.info.made", tr("item.info.made.start"), false);
        } else {
            date_text(text, sizeof(text), tick);
            char ago[TEXT_SIZE];
            hud_ago_text(ago, sizeof(ago), game->clock.tick, tick);
            const char *tip = ago;
            item_info_row_tip(layout, "item.info.made", text, &tip, 1);
        }
    }
    const char *maker = items_maker_name(&game->items, rows->item->maker);
    EntityId maker_id = items_maker_id(&game->items, rows->item->maker);
    const Colonist *colonist = game_colonist_const(game, maker_id);
    if (colonist == NULL || health_is_dead(&colonist->health)) {
        item_info_row(layout, "item.info.maker", maker != NULL ? maker : tr("item.info.maker.none"),
                      false);
    } else if (link_row(layout, "item.info.maker", colonist->name, "item.info.maker.description")) {
        rows->go_to_maker = maker_id;
    }
}

static void info_rows(UiLayout *layout, void *userdata)
{
    InfoRows *rows = userdata;
    const Game *game = &rows->app->game;
    header_rows(layout, rows);
    const Corpse *corpse = corpses_get(&game->corpses, rows->item->corpse);
    if (corpse != NULL) {
        hud_corpse_rows(layout, game, corpse);
    }
    layout->y += GAP;
    item_info_heading(layout, "item.info.heading.state");
    state_rows(layout, rows);
    layout->y += GAP;
    item_info_heading(layout, "item.info.heading.item");
    item_info_traits(layout, rows->one, false);
    item_info_tool_rows(layout, (ItemType)rows->one.type);
    item_info_weapon_rows(layout, (ItemType)rows->one.type);
    layout->y += GAP;
    item_info_heading(layout, "item.info.heading.origin");
    origin_rows(layout, rows);
}

void item_info_draw(App *app)
{
    ItemPick pick = app->selected_item;
    const Item *item = app->tile_selected && app->selected_thing == THING_ITEM
                           ? game_pick_item(&app->game, &pick)
                           : NULL;
    if (item == NULL) {
        app->show_item_info = false;
        return;
    }
    InfoRows rows = {.app = app,
                     .item = item,
                     .one = item_as_stack(item),
                     .x = app->selected_tile_x,
                     .y = app->selected_tile_y,
                     .z = app->selected_tile_z};
    bool open = true;
    char title[TEXT_SIZE];
    SDL_strlcpy(title, hud_item_name(rows.one), sizeof(title));
    ui_layout_window(&app->ui, APP_WINDOW_ITEM_INFO, title, UI_PLACE_CENTER, info_rows, &rows,
                     &open);
    if (!open) {
        app->show_item_info = false;
    } else if (!entity_id_is_none(rows.go_to_maker)) {
        app_select_colonist(app, rows.go_to_maker, false);
        app_focus_colonist(app, rows.go_to_maker);
    } else if (rows.go_to_tile) {
        app_focus_tile(app, rows.x, rows.y, rows.z);
    }
}
