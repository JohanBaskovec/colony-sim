/*
 * Heads-up display: date and speed, the debug readout and the notices, what
 * lies under the mouse above the build menu, the windows (inspectors, work
 * priorities, controls help, game menu) in their stacking order, then the
 * build menu (game/build_menu.h), the bottom bar (game/bottom_bar.h) and the
 * tooltips. Every visible string goes through tr().
 */
#ifndef COLONY_GAME_HUD_H
#define COLONY_GAME_HUD_H

#include "core/wood.h"
#include "game/app.h"
#include "game/ui_layout.h"

void hud_draw(App *app);

/* The inspector of the selected colonist(s) (game/inspector.h). */
void hud_inspect_colonists(App *app);

/* The body of a settler (core/corpse.h), in the window of the items it lies
 * among (game/tile_panel.h): its summary rows (age, cause of death, how long
 * ago), and its tabs, like a settler's: the Character, Knowledge and Journal
 * of the dead, the others only saying it is dead. */
void hud_corpse_rows(UiLayout *layout, const Game *game, const Corpse *corpse);
void hud_corpse_tabs(App *app, const Corpse *corpse);

/* Lines shared by the tile readout and the windows of the things on a tile
 * (game/tile_panel.h); false when there is nothing to say. The grade and
 * hit points of a stack, or of the structure on a tile: */
bool hud_item_condition_text(char *out, size_t size, ItemStack stack);

/* A quality (0..100) as text, in the player's number format. */
void hud_quality_text(char *out, size_t size, ItemQuality quality);
bool hud_structure_condition_text(char *out, size_t size, const Map *map, int x, int y, int z);
/* "21 kg", "0.3 kg" (see the tile readout). */
void hud_weight_text(char *out, size_t size, float kilograms);
/* The name of the items of a stack, their wood species in it ("Oak
 * branches", core/wood.h): a buffer of its own for each of the last four
 * calls, so that a line can name a few. */
const char *hud_item_name(ItemStack stack);

/* The species of wood one may choose for a site or a bill (docs/TREES.md
 * §6.4): "Any wood", then each species with how many items of `type` lie
 * about of it ("Oak (12)"; without the counts for NULL items: no need to
 * look through them every frame while the list is closed); `selected` is
 * the option of `current` (-1 for several species). */
#define HUD_WOOD_CHOICES (WOOD_SPECIES_COUNT + 1)
typedef struct HudWoodChoices {
    int count;
    int selected;
    WoodMask values[HUD_WOOD_CHOICES];
    char texts[HUD_WOOD_CHOICES][96];
    const char *options[HUD_WOOD_CHOICES];
} HudWoodChoices;
void hud_wood_choices(const Items *items, ItemType type, WoodMask current, HudWoodChoices *out);

/* "Wood: oak and ash", "Wood: any" (woods 0). */
#define HUD_WOOD_TEXT_SIZE 256
void hud_wood_text(char *out, size_t size, WoodMask woods);

/* "Rots away in about ..." for `hours` left (item_stack_rot_hours()), or
 * that it keeps while frozen (negative hours). */
void hud_rot_text(char *out, size_t size, float hours);
/* Who owns the bed on a tile (or that it is free). */
void hud_bed_owner_text(const App *app, char *out, size_t size, int x, int y, int z);
/* A construction site's line `line`: 0 its progress, then one per material
 * (false for a slot with none, or past the last). */
bool hud_site_line(const ConstructionSite *site, int line, char *out, size_t size);

/* Takes the notices the game raised since the last frame and ages those on
 * screen by `dt` real seconds. */
void hud_update_notices(App *app, double dt);

/* Windows open now, back to front (the last one is in front). Returns how many. */
int hud_open_windows(const App *app, AppWindow out[APP_WINDOW_COUNT]);

/* Names under the colonists, drawn in the world (before the HUD panels). */
void hud_draw_world_labels(App *app);

#endif /* COLONY_GAME_HUD_H */
