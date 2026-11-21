#include "game/saves.h"

#include "core/game_clock.h"
#include "core/i18n.h"
#include "core/log.h"
#include "game/app.h"

#include <stdlib.h>

#define SAVES_SUBFOLDER "saves"
#define PATH_SIZE 1200
#define TEXT_SIZE 192

void saves_init(App *app)
{
    app->saves_folder[0] = '\0';
    char *base = SDL_GetPrefPath("", APP_DATA_FOLDER);
    if (base == NULL) {
        LOG_WARN("No folder for the saves: %s", SDL_GetError());
        return;
    }
    char folder[sizeof(app->saves_folder)];
    int length = SDL_snprintf(folder, sizeof(folder), "%s%s/", base, SAVES_SUBFOLDER);
    SDL_free(base);
    if (length <= 0 || (size_t)length >= sizeof(folder)) {
        LOG_WARN("The saves folder's path is too long");
        return;
    }
    if (!SDL_CreateDirectory(folder)) {
        LOG_WARN("Cannot make the saves folder '%s': %s", folder, SDL_GetError());
        return;
    }
    SDL_strlcpy(app->saves_folder, folder, sizeof(app->saves_folder));
}

/* The path of a file of the saves folder; false when there is no folder or
 * the path does not fit. */
static bool save_path(const App *app, const char *file, const char *suffix, char *out, size_t size)
{
    if (app->saves_folder[0] == '\0') {
        return false;
    }
    int length = SDL_snprintf(out, size, "%s%s%s", app->saves_folder, file, suffix);
    return length > 0 && (size_t)length < size;
}

/* What a save file tells of itself, from its first bytes. */
static void read_entry(const App *app, SaveEntry *entry)
{
    entry->version = -1;
    entry->has_info = false;
    char path[PATH_SIZE];
    if (!save_path(app, entry->file, "", path, sizeof(path))) {
        return;
    }
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info)) {
        entry->modified = info.modify_time;
    }
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (io == NULL) {
        return;
    }
    uint8_t start[SAVE_INFO_BYTES];
    size_t read = SDL_ReadIO(io, start, sizeof(start));
    SDL_CloseIO(io);
    entry->version = save_version_of(start, read);
    entry->has_info = save_read_info(start, read, &entry->info);
}

static int compare_entries(const void *a, const void *b)
{
    const SaveEntry *x = a, *y = b;
    if (x->modified != y->modified) {
        return x->modified > y->modified ? -1 : 1; /* Newest first. */
    }
    return SDL_strcmp(x->file, y->file);
}

void saves_list(const App *app, SaveList *list)
{
    list->count = 0;
    if (app->saves_folder[0] == '\0') {
        return;
    }
    int count = 0;
    char **names = SDL_GlobDirectory(app->saves_folder, "*.sav", 0, &count);
    if (names == NULL) {
        LOG_WARN("Cannot list the saves: %s", SDL_GetError());
        return;
    }
    for (int i = 0; i < count; i++) {
        SaveEntry entry = {0};
        entry.kind = save_file_kind(names[i], &entry.number);
        if (entry.kind == SAVE_FILE_NONE) {
            continue;
        }
        SDL_strlcpy(entry.file, names[i], sizeof(entry.file));
        read_entry(app, &entry);
        if (list->count < SAVES_LISTED) {
            list->entries[list->count++] = entry;
            continue;
        }
        /* Too many: the oldest ones are left out. */
        int oldest = 0;
        for (int j = 1; j < list->count; j++) {
            if (compare_entries(&list->entries[j], &list->entries[oldest]) > 0) {
                oldest = j;
            }
        }
        if (compare_entries(&entry, &list->entries[oldest]) < 0) {
            list->entries[oldest] = entry;
        }
    }
    SDL_free(names);
    SDL_qsort(list->entries, (size_t)list->count, sizeof(list->entries[0]), compare_entries);
}

/* Writes the game to a temporary file, which then takes the save's name: a
 * save cut short (the game stopped, a disk full) never takes the place of a
 * whole one. */
static bool write_file(App *app, const char *file)
{
    char path[PATH_SIZE], temp[PATH_SIZE];
    if (!save_path(app, file, "", path, sizeof(path)) ||
        !save_path(app, file, SAVE_FILE_TEMP_SUFFIX, temp, sizeof(temp))) {
        app_show_message(app, tr("saves.error.folder"));
        return false;
    }
    size_t size = 0;
    uint8_t *data = game_save(&app->game, &size);
    if (data == NULL) {
        app_show_message(app, tr("saves.error.memory"));
        return false;
    }
    bool written = SDL_SaveFile(temp, data, size);
    free(data);
    if (!written || !SDL_RenamePath(temp, path)) {
        LOG_WARN("Cannot write the save '%s': %s", path, SDL_GetError());
        SDL_RemovePath(temp);
        app_show_message(app, tr("saves.error.write"));
        return false;
    }
    LOG_INFO("Game saved to '%s' (%zu bytes)", path, size);
    app->saves_stale = true;
    return true;
}

bool saves_write(App *app, const char *file, bool told)
{
    if (!write_file(app, file)) {
        return false;
    }
    if (told) {
        app_show_message(app, tr("saves.saved"));
    }
    return true;
}

bool saves_write_new(App *app)
{
    if (app->saves_folder[0] == '\0') {
        app_show_message(app, tr("saves.error.folder"));
        return false;
    }
    /* All the files, not only those listed: none is overwritten. */
    int count = 0;
    char **names = SDL_GlobDirectory(app->saves_folder, "*.sav", 0, &count);
    if (names == NULL) {
        app_show_message(app, tr("saves.error.write"));
        return false;
    }
    int number = save_file_next_number((const char *const *)names, count);
    SDL_free(names);
    char file[SAVE_FILE_NAME_SIZE];
    if (number == 0 || !save_file_name(SAVE_FILE_MANUAL, number, file, sizeof(file))) {
        app_show_message(app, tr("saves.error.full"));
        return false;
    }
    return saves_write(app, file, true);
}

bool saves_delete(App *app, const char *file)
{
    char path[PATH_SIZE];
    if (!save_path(app, file, "", path, sizeof(path))) {
        return false;
    }
    app->saves_stale = true;
    if (!SDL_RemovePath(path)) {
        LOG_WARN("Cannot delete the save '%s': %s", path, SDL_GetError());
        return false;
    }
    return true;
}

void *saves_read(App *app, const char *file, size_t *size)
{
    char path[PATH_SIZE];
    if (!save_path(app, file, "", path, sizeof(path))) {
        app_show_message(app, tr("saves.error.folder"));
        return NULL;
    }
    void *data = SDL_LoadFile(path, size);
    if (data == NULL) {
        LOG_WARN("Cannot read the save '%s': %s", path, SDL_GetError());
        app_show_message(app, tr("saves.error.read"));
    }
    return data;
}

void saves_tell_error(App *app, SaveError error, int version)
{
    char text[APP_MESSAGE_SIZE];
    switch (error) {
    case SAVE_ERROR_MEMORY: app_show_message(app, tr("saves.error.memory")); break;
    case SAVE_ERROR_NOT_A_SAVE: app_show_message(app, tr("saves.error.not_a_save")); break;
    case SAVE_ERROR_VERSION: {
        char number[16];
        tr_integer(number, sizeof(number), version);
        TR_FORMAT(text, sizeof(text), tr("saves.error.version"), number);
        app_show_message(app, text);
        break;
    }
    case SAVE_ERROR_DAMAGED: app_show_message(app, tr("saves.error.damaged")); break;
    default: break;
    }
}

static bool save_exists(const App *app, const char *file)
{
    char path[PATH_SIZE];
    SDL_PathInfo info;
    return save_path(app, file, "", path, sizeof(path)) && SDL_GetPathInfo(path, &info) &&
           info.type == SDL_PATHTYPE_FILE;
}

void saves_quick_save(App *app)
{
    char file[SAVE_FILE_NAME_SIZE];
    if (save_file_name(SAVE_FILE_QUICK, 0, file, sizeof(file))) {
        saves_write(app, file, true);
    }
}

void saves_quick_load(App *app)
{
    char file[SAVE_FILE_NAME_SIZE];
    if (!save_file_name(SAVE_FILE_QUICK, 0, file, sizeof(file))) {
        return;
    }
    if (!save_exists(app, file)) {
        app_show_message(app, tr("saves.no_quick_save"));
        return;
    }
    app_request_load(app, file);
}

void saves_autosave_update(App *app)
{
    uint64_t day = app->game.clock.tick / TICKS_PER_DAY;
    if (app->autosave_day == UINT64_MAX) {
        app->autosave_day = day; /* A game begins: the next day is the first saved. */
        return;
    }
    if (day == app->autosave_day) {
        return;
    }
    app->autosave_day = day;
    if (app->saves_folder[0] == '\0') {
        return;
    }
    char file[SAVE_FILE_NAME_SIZE], previous[SAVE_FILE_NAME_SIZE];
    char path[PATH_SIZE], previous_path[PATH_SIZE];
    if (!save_file_name(SAVE_FILE_AUTO, 0, file, sizeof(file)) ||
        !save_file_name(SAVE_FILE_AUTO_PREVIOUS, 0, previous, sizeof(previous)) ||
        !save_path(app, file, "", path, sizeof(path)) ||
        !save_path(app, previous, "", previous_path, sizeof(previous_path))) {
        return;
    }
    /* The last autosave is kept, in case this one is of a game gone wrong. */
    if (save_exists(app, file) && !SDL_RenamePath(path, previous_path)) {
        LOG_WARN("Cannot keep the previous autosave: %s", SDL_GetError());
    }
    if (write_file(app, file)) {
        app_show_message(app, tr("saves.autosaved"));
    }
}

void saves_entry_name(const SaveEntry *entry, char *out, size_t size)
{
    switch (entry->kind) {
    case SAVE_FILE_MANUAL: {
        char number[16];
        tr_integer(number, sizeof(number), entry->number);
        TR_FORMAT(out, size, tr("saves.name.manual"), number);
        break;
    }
    case SAVE_FILE_QUICK: SDL_strlcpy(out, tr("saves.name.quick"), size); break;
    case SAVE_FILE_AUTO: SDL_strlcpy(out, tr("saves.name.auto"), size); break;
    case SAVE_FILE_AUTO_PREVIOUS: SDL_strlcpy(out, tr("saves.name.auto_previous"), size); break;
    default: SDL_strlcpy(out, entry->file, size); break;
    }
}

/* When the file was written, in the player's time zone. */
static void saved_at_text(SDL_Time when, char *out, size_t size)
{
    SDL_DateTime date;
    if (!SDL_TimeToDateTime(when, &date, true)) {
        out[0] = '\0';
        return;
    }
    char year[8], month[4], day[4], hours[4], minutes[4];
    SDL_snprintf(year, sizeof(year), "%04d", date.year);
    SDL_snprintf(month, sizeof(month), "%02d", date.month);
    SDL_snprintf(day, sizeof(day), "%02d", date.day);
    SDL_snprintf(hours, sizeof(hours), "%02d", date.hour);
    SDL_snprintf(minutes, sizeof(minutes), "%02d", date.minute);
    TR_FORMAT(out, size, tr("saves.saved_at"), year, month, day, hours, minutes);
}

void saves_entry_details(const SaveEntry *entry, char *out, size_t size)
{
    if (!entry->has_info) {
        if (entry->version >= 0 && entry->version != SAVE_VERSION) {
            char number[16];
            tr_integer(number, sizeof(number), entry->version);
            TR_FORMAT(out, size, tr("saves.other_version"), number);
        } else {
            SDL_strlcpy(out, tr("saves.unreadable"), size);
        }
        return;
    }
    GameDate date = game_date_from_tick(entry->info.tick);
    char day[16], year[16], hours[8], minutes[8], when[TEXT_SIZE];
    tr_integer(day, sizeof(day), date.day);
    tr_integer(year, sizeof(year), date.year);
    SDL_snprintf(hours, sizeof(hours), "%02d", date.hour);
    SDL_snprintf(minutes, sizeof(minutes), "%02d", date.minute);
    TR_FORMAT(when, sizeof(when), tr("hud.date"), day, tr(season_name_key(date.season)), year,
              hours, minutes);
    char settlers[TEXT_SIZE], count[16];
    if (entry->info.colony_lost) {
        SDL_strlcpy(settlers, tr("saves.lost"), sizeof(settlers));
    } else {
        tr_integer(count, sizeof(count), entry->info.settlers);
        TR_FORMAT(settlers, sizeof(settlers), tr_plural("saves.settlers", entry->info.settlers),
                  count);
    }
    char saved[TEXT_SIZE];
    saved_at_text(entry->modified, saved, sizeof(saved));
    TR_FORMAT(out, size, tr("saves.details"), when, settlers, saved);
}
