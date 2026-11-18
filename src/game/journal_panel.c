#include "game/journal_panel.h"

#include "core/animal.h"
#include "core/combat.h"
#include "core/game_clock.h"
#include "core/i18n.h"
#include "core/mood.h"

#include <string.h>

#define TEXT_SIZE 256

/* An animal of a species and sex as a sentence names it: who struck ("A
 * she-wolf"), whose body it was ("the she-wolf's"), itself in its own journal
 * ("The she-wolf"), and the one a blow was aimed at ("the she-wolf"). */
static const char *const ATTACKER_KEYS[ANIMAL_SPECIES_COUNT][ANIMAL_SEX_COUNT] = {
    [ANIMAL_ROE_DEER] = {N_("journal.by.roe_deer.female"), N_("journal.by.roe_deer.male")},
    [ANIMAL_RED_DEER] = {N_("journal.by.red_deer.female"), N_("journal.by.red_deer.male")},
    [ANIMAL_BOAR] = {N_("journal.by.boar.female"), N_("journal.by.boar.male")},
    [ANIMAL_HARE] = {N_("journal.by.hare.female"), N_("journal.by.hare.male")},
    [ANIMAL_WOLF] = {N_("journal.by.wolf.female"), N_("journal.by.wolf.male")},
    [ANIMAL_FOX] = {N_("journal.by.fox.female"), N_("journal.by.fox.male")},
};

static const char *const OWNER_KEYS[ANIMAL_SPECIES_COUNT][ANIMAL_SEX_COUNT] = {
    [ANIMAL_ROE_DEER] = {N_("journal.of.roe_deer.female"), N_("journal.of.roe_deer.male")},
    [ANIMAL_RED_DEER] = {N_("journal.of.red_deer.female"), N_("journal.of.red_deer.male")},
    [ANIMAL_BOAR] = {N_("journal.of.boar.female"), N_("journal.of.boar.male")},
    [ANIMAL_HARE] = {N_("journal.of.hare.female"), N_("journal.of.hare.male")},
    [ANIMAL_WOLF] = {N_("journal.of.wolf.female"), N_("journal.of.wolf.male")},
    [ANIMAL_FOX] = {N_("journal.of.fox.female"), N_("journal.of.fox.male")},
};

static const char *const SELF_KEYS[ANIMAL_SPECIES_COUNT][ANIMAL_SEX_COUNT] = {
    [ANIMAL_ROE_DEER] = {N_("journal.self.roe_deer.female"), N_("journal.self.roe_deer.male")},
    [ANIMAL_RED_DEER] = {N_("journal.self.red_deer.female"), N_("journal.self.red_deer.male")},
    [ANIMAL_BOAR] = {N_("journal.self.boar.female"), N_("journal.self.boar.male")},
    [ANIMAL_HARE] = {N_("journal.self.hare.female"), N_("journal.self.hare.male")},
    [ANIMAL_WOLF] = {N_("journal.self.wolf.female"), N_("journal.self.wolf.male")},
    [ANIMAL_FOX] = {N_("journal.self.fox.female"), N_("journal.self.fox.male")},
};

static const char *const AIMED_KEYS[ANIMAL_SPECIES_COUNT][ANIMAL_SEX_COUNT] = {
    [ANIMAL_ROE_DEER] = {N_("journal.the.roe_deer.female"), N_("journal.the.roe_deer.male")},
    [ANIMAL_RED_DEER] = {N_("journal.the.red_deer.female"), N_("journal.the.red_deer.male")},
    [ANIMAL_BOAR] = {N_("journal.the.boar.female"), N_("journal.the.boar.male")},
    [ANIMAL_HARE] = {N_("journal.the.hare.female"), N_("journal.the.hare.male")},
    [ANIMAL_WOLF] = {N_("journal.the.wolf.female"), N_("journal.the.wolf.male")},
    [ANIMAL_FOX] = {N_("journal.the.fox.female"), N_("journal.the.fox.male")},
};

/* How a blow struck, by the mark it leaves (core/combat.h). */
static const char *const VERB_KEYS[] = {
    [STRIKE_MARK_BLOW] = N_("journal.verb.blow"),
    [STRIKE_MARK_CLAWS] = N_("journal.verb.claws"),
    [STRIKE_MARK_BITE] = N_("journal.verb.bite"),
    [STRIKE_MARK_CUT] = N_("journal.verb.cut"),
};

/* The same, after "tried" ("to bite"). */
static const char *const TRY_KEYS[] = {
    [STRIKE_MARK_BLOW] = N_("journal.try.blow"),
    [STRIKE_MARK_CLAWS] = N_("journal.try.claws"),
    [STRIKE_MARK_BITE] = N_("journal.try.bite"),
    [STRIKE_MARK_CUT] = N_("journal.try.cut"),
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
        SDL_strlcpy(out, tr(OWNER_KEYS[owner.species][owner.sex % ANIMAL_SEX_COUNT]), size);
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

/* Who a sentence is about: a settler (named), or an animal of a species. */
static JournalOwner entry_party(const JournalEntry *entry)
{
    if (entry->species > 0 && entry->species <= ANIMAL_SPECIES_COUNT) {
        return (JournalOwner){NULL, entry->species - 1, entry->sex};
    }
    return (JournalOwner){entry->other, 0, 0};
}

/* A party by its name, else by its species' phrase from `keys`. */
/* One of the tables above (indexed by species, then sex). */
typedef const char *const (*SpeciesKeys)[ANIMAL_SEX_COUNT];

static const char *party_text(JournalOwner party, SpeciesKeys keys)
{
    if (party.name != NULL) {
        return party.name;
    }
    return (unsigned)party.species < ANIMAL_SPECIES_COUNT
               ? tr(keys[party.species][party.sex % ANIMAL_SEX_COUNT])
               : "";
}

static const char *mark_text(int attack, const char *const *keys)
{
    const AttackDef *def = attack_def((AttackType)attack);
    return def != NULL && (unsigned)def->mark < sizeof(VERB_KEYS) / sizeof(*VERB_KEYS)
               ? tr(keys[def->mark])
               : "";
}

/* A blow: `striker` (as the subject: `striker_keys` for an animal) struck
 * `struck` (the owner of the body hit), and how it went. */
static void blow_text(const JournalEntry *entry, JournalBlow outcome, JournalOwner striker,
                      SpeciesKeys striker_keys, JournalOwner struck, char *out, size_t size)
{
    const char *who = party_text(striker, striker_keys);
    char whose[TEXT_SIZE];
    owner_text(struck, whose, sizeof(whose));
    const char *part = part_phrase(entry->plan, entry->part);
    const char *injury =
        (unsigned)entry->injury < INJURY_TYPE_COUNT ? tr(INJURY_KEYS[entry->injury]) : "";
    switch (outcome) {
    case JOURNAL_BLOW_MISSED:
    case JOURNAL_BLOW_DODGED:
        TR_FORMAT(
            out, size,
            tr(outcome == JOURNAL_BLOW_MISSED ? "journal.blow.missed" : "journal.blow.dodged"), who,
            mark_text(entry->what, TRY_KEYS), party_text(struck, AIMED_KEYS));
        break;
    case JOURNAL_BLOW_HARMLESS:
        TR_FORMAT(out, size, tr("journal.blow.harmless"), who, mark_text(entry->what, VERB_KEYS),
                  whose, part);
        break;
    case JOURNAL_BLOW_KILLED:
        TR_FORMAT(out, size, tr("journal.blow.fatal"), who, mark_text(entry->what, VERB_KEYS),
                  whose, part, injury);
        break;
    default:
        TR_FORMAT(out, size, tr("journal.wound"), who, mark_text(entry->what, VERB_KEYS), whose,
                  part, injury);
        break;
    }
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
    case JOURNAL_WOUND:
        blow_text(entry, JOURNAL_BLOW_WOUNDED, entry_party(entry), ATTACKER_KEYS, owner, out, size);
        break;
    case JOURNAL_STRIKE:
        /* Given: the owner struck the one the entry names. */
        blow_text(entry, (JournalBlow)entry->outcome, owner, SELF_KEYS, entry_party(entry), out,
                  size);
        break;
    case JOURNAL_STRUCK:
        blow_text(entry, (JournalBlow)entry->outcome, entry_party(entry), ATTACKER_KEYS, owner, out,
                  size);
        break;
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
    if (entry->repeats > 0 && out[0] != '\0') {
        char once[TEXT_SIZE], times[16];
        SDL_strlcpy(once, out, sizeof(once));
        tr_integer(times, sizeof(times), entry->repeats + 1);
        TR_FORMAT(out, size, tr("journal.repeat"), once, times);
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
    case JOURNAL_STRIKE: /* A blow that landed. */
        return ui->theme
            .colors[entry->outcome == JOURNAL_BLOW_WOUNDED || entry->outcome == JOURNAL_BLOW_KILLED
                        ? THEME_VALUE_HIGH
                        : THEME_TEXT];
    default: return ui->theme.colors[THEME_TEXT];
    }
}

/* The check boxes on one row; a click flips the kind. */
static void filter_row(UiLayout *layout, JournalFilter *hidden)
{
    static const char *const LABELS[JOURNAL_KIND_COUNT] = {
        [JOURNAL_THOUGHTS] = N_("journal.filter.thoughts"),
        [JOURNAL_WOUNDS] = N_("journal.filter.wounds"),
        [JOURNAL_SOCIAL] = N_("journal.filter.social"),
        [JOURNAL_ATTACKS] = N_("journal.filter.attacks"),
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
        JournalFilter kinds = journal_event_kinds((JournalEvent)entry->event);
        if ((kinds & ~*hidden) == 0) {
            continue; /* None of its kinds is shown. */
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
