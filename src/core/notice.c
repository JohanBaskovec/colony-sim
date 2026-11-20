#include "core/notice.h"

#include "core/i18n.h"

#include <stdio.h>

void notices_push(Notices *notices, NoticeType type, int knowledge, const char *name, uint64_t tick)
{
    Notice *notice = &notices->list[notices->count % NOTICE_MAX];
    notice->type = (uint8_t)type;
    notice->knowledge = (uint8_t)knowledge;
    snprintf(notice->name, sizeof(notice->name), "%s", name != NULL ? name : "");
    notice->tick = tick;
    notices->count++;
}

const Notice *notices_get(const Notices *notices, uint32_t number)
{
    if (number >= notices->count || notices->count - number > NOTICE_MAX) {
        return NULL;
    }
    return &notices->list[number % NOTICE_MAX];
}

const char *notice_text_key(NoticeType type)
{
    static const char *const KEYS[NOTICE_TYPE_COUNT] = {
        [NOTICE_KNOWLEDGE_DISCOVERED] = N_("notice.knowledge_discovered"),
        [NOTICE_KNOWLEDGE_MASTERED] = N_("notice.knowledge_mastered"),
        [NOTICE_KNOWLEDGE_LAST_MASTER] = N_("notice.knowledge_last_master"),
        [NOTICE_KNOWLEDGE_LOST] = N_("notice.knowledge_lost"),
        [NOTICE_KNOWLEDGE_FORGOTTEN] = N_("notice.knowledge_forgotten"),
        [NOTICE_ROOF_COLLAPSED] = N_("notice.roof_collapsed"),
        [NOTICE_CAVE_IN] = N_("notice.cave_in"),
        [NOTICE_TREE_FELL_ON_BUILDING] = N_("notice.tree_fell_on_building"),
        [NOTICE_BREAK_SULK] = N_("notice.break_sulk"),
        [NOTICE_BREAK_WANDER_OFF] = N_("notice.break_wander_off"),
        [NOTICE_BREAK_BINGE] = N_("notice.break_binge"),
        [NOTICE_DEATH_BLOOD_LOSS] = N_("notice.death_blood_loss"),
        [NOTICE_DEATH_INJURY] = N_("notice.death_injury"),
        [NOTICE_DEATH_STARVATION] = N_("notice.death_starvation"),
        [NOTICE_DEATH_DEHYDRATION] = N_("notice.death_dehydration"),
        [NOTICE_DEATH_HYPOTHERMIA] = N_("notice.death_hypothermia"),
        [NOTICE_DEATH_HEATSTROKE] = N_("notice.death_heatstroke"),
        [NOTICE_COLONY_LOST] = N_("notice.colony_lost"),
        [NOTICE_SETTLER_ATTACKED] = N_("notice.settler_attacked"),
        [NOTICE_SETTLER_COLLAPSED] = N_("notice.settler_collapsed"),
        [NOTICE_UNDRAFTED_BREAK] = N_("notice.undrafted_break"),
        [NOTICE_UNDRAFTED_FLED] = N_("notice.undrafted_fled"),
        [NOTICE_UNDRAFTED_DOWN] = N_("notice.undrafted_down"),
    };
    return (unsigned)type < NOTICE_TYPE_COUNT ? KEYS[type] : KEYS[NOTICE_KNOWLEDGE_LOST];
}
