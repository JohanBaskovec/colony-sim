/*
 * The Journal tab of a settler or an animal (core/journal.h): the entries
 * newest first, each with its date and a sentence in the player's language,
 * under four check boxes (thoughts, wounds, attacks, social) that filter them
 * (an entry shows when one of its kinds does: a wound from a blow is both a
 * wound and an attack). The sentences are built here, from the facts the
 * journal keeps.
 */
#ifndef COLONY_GAME_JOURNAL_PANEL_H
#define COLONY_GAME_JOURNAL_PANEL_H

#include "core/journal.h"
#include "game/ui_layout.h"

/* Whose journal it is: a settler (named), or an animal of a species and sex. */
typedef struct JournalOwner {
    const char *name; /* A settler's name, else NULL. */
    int species;      /* An animal's AnimalSpecies, when `name` is NULL... */
    int sex;          /* ...and its AnimalSex. */
} JournalOwner;

/* The rows (a UiRowsFn's body). `hidden` is the kinds the player switched off
 * (a bit per JournalKind; 0: all shown): the check boxes change it. */
void journal_panel_rows(UiLayout *layout, const Journal *journal, JournalOwner owner,
                        JournalFilter *hidden);

/* The sentence of an entry, in the player's language. */
void journal_entry_text(const JournalEntry *entry, JournalOwner owner, char *out, size_t size);

#endif /* COLONY_GAME_JOURNAL_PANEL_H */
