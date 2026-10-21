#include "jelli/pet_celebration.h"
#include "jelli/pet_effect.h"

_Static_assert(sizeof(JelliPetCelebration) <= 32u, "Celebration queue exceeds budget");

static unsigned binding(const JelliEvent *event, const uint8_t *bindings)
{
    if (event->result != JELLI_OK)
        return 0u;
    if (event->kind == JELLI_EVENT_STATUS && event->code == 1u)
        return bindings[JELLI_EFFECT_EVOLUTION];
    if (event->kind == JELLI_EVENT_INPUT && event->code == JELLI_INPUT_CATCH_PRESENT)
        return bindings[JELLI_EFFECT_PRESENT_CAUGHT];
    if (event->kind == JELLI_EVENT_COMMAND && event->code == JELLI_CMD_GIFT && event->value > 0u &&
        event->after.activity == JELLI_IDLE)
        return bindings[JELLI_EFFECT_PRESENT_ACCEPTED];
    if (event->kind == JELLI_EVENT_EFFECT && event->code == JELLI_GIVING &&
        event->before.activity == JELLI_GIVING && event->after.activity == JELLI_IDLE)
        return bindings[JELLI_EFFECT_PRESENT_ACCEPTED];
    return 0u;
}

void jelli_pet_celebrations(JelliPetCelebration *state, JelliPetUi *ui, const JelliGame *game,
                            const JelliEventLog *events,
                            const uint8_t bindings[JELLI_EFFECT_TRIGGER_COUNT],
                            uint64_t animation_ms, bool suppress, bool paused)
{
    const JelliPet *pet = &game->pets[game->active];
    if (suppress || animation_ms < state->last_ms) {
        state->pending = 0u;
        state->cursor = events->sequence;
    }
    state->last_ms = animation_ms;
    if (state->pending && (state->pet_id != pet->id || state->form != pet->form ||
                           animation_ms - state->queued_ms >= 30000u))
        state->pending = 0u;
    if (state->cursor > events->sequence)
        state->cursor = 0u; /* Event log rollover starts a fresh history. */
    for (unsigned i = 0u; i < events->count; ++i) {
        const JelliEvent *event = jelli_events_at(events, i);
        if (event->sequence <= state->cursor || event->pet_id != pet->id)
            continue;
        unsigned effect = binding(event, bindings);
        if (!effect || effect > jelli_effect_definition_count)
            continue;
        state->pending = (uint8_t)effect;
        state->pet_id = pet->id;
        state->form = pet->form;
        state->queued_ms = animation_ms;
    }
    state->cursor = events->sequence;
    if (!state->pending || ui->menu_open || ui->effect || paused || suppress)
        return;
    (void)jelli_pet_effect_start(ui, jelli_effect_definitions[state->pending - 1u].key,
                                 animation_ms);
    state->pending = 0u;
}
