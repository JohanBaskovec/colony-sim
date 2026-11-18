#include "game/journal_panel.h"

#include "core/animal.h"
#include "core/combat.h"
#include "core/game_clock.h"
#include "core/i18n.h"
#include "core/mood.h"

#include <string.h>

#define TEXT_SIZE 256

/* Who struck ("A wolf") and whose body it was ("the wolf's"), by species. */
static const char *const ATTACKER_KEYS[ANIMAL_SPECIES_COUNT] = {
    [ANIMAL_ROE_DEER] = N_("journal.by.roe_deer"), [ANIMAL_RED_DEER] = N_("journal.by.red_deer"),
    [ANIMAL_BOAR] = N_("journal.by.boar"),         [ANIMAL_HARE] = N_("journal.by.hare"),
    [ANIMAL_WOLF] = N_("journal.by.wolf"),         [ANIMAL_FOX] = N_("journal.by.fox"),
};

static const char *const OWNER_KEYS[ANIMAL_SPECIES_COUNT] = {
    [ANIMAL_ROE_DEER] = N_("journal.of.roe_deer"), [ANIMAL_RED_DEER] = N_("journal.of.red_deer"),
    [ANIMAL_BOAR] = N_("journal.of.boar"),         [ANIMAL_HARE] = N_("journal.of.hare"),
    [ANIMAL_WOLF] = N_("journal.of.wolf"),         [ANIMAL_FOX] = N_("journal.of.fox"),
};

/* How a blow struck, by the mark it leaves (core/combat.h). */
static const char *const VERB_KEYS[] = {
    [STRIKE_MARK_BLOW] = N_("journal.verb.blow"),
    [STRIKE_MARK_CLAWS] = N_("journal.verb.claws"),
    [STRIKE_MARK_BITE] = N_("journal.verb.bite"),
    [STRIKE_MARK_CUT] = N_("journal.verb.cut"),
};

static const char *const INJURY_KEYS[INJURY_TYPE_COUNT] = {
    [INJURY_CUT] = N_("journal.injury.cut"),     [INJURY_BRUISE] = N_("journal.injury.bruise"),
    [INJURY_BITE] = N_("journal.injury.bite"),   [INJURY_FRACTURE] = N_("journal.injury.fracture"),
    [INJURY_CRUSH] = N_("journal.injury.crush"),
};

/* A part as it reads in a sentence ("the left leg"), by its name in the body table. */
typedef struct PartPhrase {
    const char *body_key;
    const char *phrase_key;
} PartPhrase;

static const PartPhrase PART_PHRASES[] = {
    {N_("body.head"), N_("journal.part.head")},
    {N_("body.torso"), N_("journal.part.torso")},
    {N_("body.neck"), N_("journal.part.neck")},
    {N_("body.left_arm"), N_("journal.part.left_arm")},
    {N_("body.right_arm"), N_("journal.part.right_arm")},
    {N_("body.left_leg"), N_("journal.part.left_leg")},
    {N_("body.right_leg"), N_("journal.part.right_leg")},
    {N_("body.front_left_leg"), N_("journal.part.front_left_leg")},
    {N_("body.front_right_leg"), N_("journal.part.front_right_leg")},
    {N_("body.hind_left_leg"), N_("journal.part.hind_left_leg")},
    {N_("body.hind_right_leg"), N_("journal.part.hind_right_leg")},
};

static const char *part_phrase(int plan, int part)
{
    const BodyPartDef *def = body_part_def((BodyPlan)plan, part);
    if (def == NULL) {
        return "";
    }
    for (size_t i = 0; i < sizeof(PART_PHRASES) / sizeof(PART_PHRASES[0]); i++) {
        if (strcmp(PART_PHRASES[i].body_key, def->name_key) == 0) {
            return tr(PART_PHRASES[i].phrase_key);
        }
    }
    return tr(def->name_key);
}

/* A name that begins with a vowel (or an accented letter): some languages elide before it. */
static bool begins_with_vowel(const char *name)
{
    return name[0] != '\0' && (strchr("AEIOUYaeiouy", name[0]) != NULL || (name[0] & 0x80) != 0);
}

/* Whose body it was, as the sentence has it ("Felix's", "the wolf's"). */
static void owner_text(JournalOwner owner, char *out, size_t size)
{
    if (owner.name != NULL) {
        TR_FORMAT(
            out, size,
            tr(begins_with_vowel(owner.name) ? "journal.of.settler_vowel" : "journal.of.settler"),
            owner.name);
    } else if ((unsigned)owner.species < ANIMAL_SPECIES_COUNT) {
        SDL_strlcpy(out, tr(OWNER_KEYS[owner.species]), size);
    } else {
        out[0] = '\0';
    }
}

/* What hurt, when nobody did (core/health.h HurtCause). */
static const char *const HURT_KEYS[HURT_CAUSE_COUNT] = {
    [HURT_OTHER] = N_("journal.hurt.other"),
    [HURT_FALL] = N_("journal.hurt.fall"),
    [HURT_CAVE_IN] = N_("journal.hurt.cave_in"),
    [HURT_TREE_TRUNK] = N_("journal.hurt.tree_trunk"),
    [HURT_TREE_CROWN] = N_("journal.hurt.tree_crown"),
};

static void hurt_text(const JournalEntry *entry, JournalOwner owner, char *out, size_t size)
{
    char whose[TEXT_SIZE];
    owner_text(owner, whose, sizeof(whose));
    const char *pattern = tr((unsigned)entry->what < HURT_CAUSE_COUNT ? HURT_KEYS[entry->what]
                                                                      : HURT_KEYS[HURT_OTHER]);
    const char *injury =
        (unsigned)entry->injury < INJURY_TYPE_COUNT ? tr(INJURY_KEYS[entry->injury]) : "";
    TR_FORMAT(out, size, pattern, whose, part_phrase(entry->plan, entry->part), injury);
}

static void wound_text(const JournalEntry *entry, JournalOwner owner, char *out, size_t size)
{
    char attacker[TEXT_SIZE], whose[TEXT_SIZE];
    if (entry->species > 0 && entry->species <= ANIMAL_SPECIES_COUNT) {
        SDL_strlcpy(attacker, tr(ATTACKER_KEYS[entry->species - 1]), sizeof(attacker));
    } else {
        SDL_strlcpy(attacker, entry->other, sizeof(attacker));
    }
    const AttackDef *attack = attack_def((AttackType)entry->what);
    const char *verb =
        attack != NULL && (unsigned)attack->mark < sizeof(VERB_KEYS) / sizeof(*VERB_KEYS)
            ? tr(VERB_KEYS[attack->mark])
            : "";
    const char *injury =
        (unsigned)entry->injury < INJURY_TYPE_COUNT ? tr(INJURY_KEYS[entry->injury]) : "";
    owner_text(owner, whose, sizeof(whose));
    TR_FORMAT(out, size, tr("journal.wound"), attacker, verb, whose,
              part_phrase(entry->plan, entry->part), injury);
}

void journal_entry_text(const JournalEntry *entry, JournalOwner owner, char *out, size_t size)
{
    out[0] = '\0';
    switch ((JournalEvent)entry->event) {
    case JOURNAL_THOUGHT: {
        const ThoughtDef *def = thought_def((ThoughtType)entry->what);
        if (def != NULL) {
            TR_FORMAT(out, size,
                      tr(def->value > 0.0f ? "journal.thought.good" : "journal.thought.bad"),
                      tr(def->name_key));
        }
        break;
    }
    case JOURNAL_WOUND: wound_text(entry, owner, out, size); break;
    case JOURNAL_HURT: hurt_text(entry, owner, out, size); break;
    case JOURNAL_CHAT: TR_FORMAT(out, size, tr("journal.chat"), entry->other); break;
    case JOURNAL_CHAT_ABOUT: {
        const KnowledgeDef *def = knowledge_def((KnowledgeType)entry->what);
        TR_FORMAT(out, size, tr("journal.chat_about"), entry->other,
                  def != NULL ? tr(def->name_key) : "");
        break;
    }
    case JOURNAL_EVENT_COUNT: break;
    }
}

/* "Summer, day 11, 06:04". */
static void when_text(uint64_t tick, char *out, size_t size)
{
    GameDate date = game_date_from_tick(tick);
    char day[16], hours[8], minutes[8];
    tr_integer(day, sizeof(day), date.day);
    SDL_snprintf(hours, sizeof(hours), "%02d", date.hour);
    SDL_snprintf(minutes, sizeof(minutes), "%02d", date.minute);
    TR_FORMAT(out, size, tr("journal.when"), day, tr(season_name_key(date.season)), hours, minutes);
}

static SDL_Color entry_color(const Ui *ui, const JournalEntry *entry)
{
    switch ((JournalEvent)entry->event) {
    case JOURNAL_THOUGHT:
        return ui->theme
            .colors[journal_thought_is_good((ThoughtType)entry->what) ? THEME_VALUE_HIGH
                                                                      : THEME_VALUE_LOW];
    case JOURNAL_WOUND:
    case JOURNAL_HURT: return ui->theme.colors[THEME_VALUE_LOW];
    default: return ui->theme.colors[THEME_TEXT];
    }
}

/* The three check boxes on one row; a click flips the kind. */
static void filter_row(UiLayout *layout, JournalFilter *hidden)
{
    static const char *const LABELS[JOURNAL_KIND_COUNT] = {
        [JOURNAL_THOUGHTS] = N_("journal.filter.thoughts"),
        [JOURNAL_WOUNDS] = N_("journal.filter.wounds"),
        [JOURNAL_SOCIAL] = N_("journal.filter.social"),
    };
    Ui *ui = layout->ui;
    float total = 0.0f;
    for (int kind = 0; kind < JOURNAL_KIND_COUNT; kind++) {
        total += ui_checkbox_width(ui, tr(LABELS[kind])) + UI_LAYOUT_GAP * (kind > 0 ? 1.0f : 0.0f);
    }
    ui_layout_measure(layout, total);
    float x = layout->x;
    for (int kind = 0; kind < JOURNAL_KIND_COUNT; kind++) {
        const char *label = tr(LABELS[kind]);
        float width = ui_checkbox_width(ui, label);
        bool shown = (*hidden & JOURNAL_BIT(kind)) == 0;
        if (layout->draw &&
            ui_checkbox(ui, x, layout->y, width, shown ? UI_CHECKED : UI_UNCHECKED, label)) {
            *hidden ^= JOURNAL_BIT(kind);
        }
        x += width + UI_LAYOUT_GAP;
    }
    layout->y += layout->row + UI_LAYOUT_GAP;
}

void journal_panel_rows(UiLayout *layout, const Journal *journal, JournalOwner owner,
                        JournalFilter *hidden)
{
    Ui *ui = layout->ui;
    filter_row(layout, hidden);
    int shown = 0;
    for (int i = 0; i < journal->count; i++) {
        const JournalEntry *entry = journal_recent(journal, i);
        JournalKind kind = journal_event_kind((JournalEvent)entry->event);
        if (kind == JOURNAL_KIND_COUNT || (*hidden & JOURNAL_BIT(kind)) != 0) {
            continue;
        }
        char when[TEXT_SIZE], text[TEXT_SIZE];
        when_text(entry->tick, when, sizeof(when));
        journal_entry_text(entry, owner, text, sizeof(text));
        ui_layout_label(layout, when, 0.0f, true);
        ui_layout_measure(layout, ui_text_width(ui, text) + ui->theme.window_padding);
        if (layout->draw) {
            ui_label_rgba(ui, text, layout->x + ui->theme.window_padding,
                          layout->y + layout->text_offset, entry_color(ui, entry));
        }
        layout->y += layout->row;
        shown++;
    }
    if (shown == 0) {
        ui_layout_label(layout, tr(journal->count == 0 ? "journal.empty" : "journal.filtered"),
                        0.0f, true);
    }
}
