#include "jelli/pet_rewards.h"
#include <string.h>

uint16_t jelli_pet_reward_stat(const JelliPet *pet, unsigned stat)
{
    if (stat >= 1u && stat <= JELLI_NEED_COUNT)
        return pet->needs[stat - 1u];
    if (stat == 8u)
        return pet->hydration;
    if (stat == 6u)
        return pet->bond;
    if (stat == 7u)
        return jelli_habits_sleep_score(&pet->habits);
    return (uint16_t)(jelli_pet_mood(pet) * 10u);
}

void jelli_pet_rewards_cancel(JelliPetRewards *r)
{
    r->count = r->index = 0u;
    r->elapsed_ms = 0u;
    r->visible = r->burst = false;
}

static void clear(JelliPetRewards *r)
{
    uint32_t cursor = r->cursor;
    uint32_t completed = r->completed;
    *r = (JelliPetRewards){.cursor = cursor, .completed = completed};
}

static void add_gains(JelliPetRewards *r, const JelliEvent *event)
{
    for (unsigned i = 0u; i < 7u; ++i) {
        int before = i < 5u    ? event->before.needs[i]
                     : i == 5u ? event->before.bond
                               : event->before.hydration;
        int after = i < 5u    ? event->after.needs[i]
                    : i == 5u ? event->after.bond
                              : event->after.hydration;
        int gain = r->gains[i] + after - before;
        r->gains[i] = (int16_t)(gain > 1000 ? 1000 : gain < -1000 ? -1000 : gain);
    }
}

static void append(JelliPetRewards *r, unsigned stat, uint16_t from, uint16_t to)
{
    if (to <= from || r->count >= JELLI_PET_REWARD_CAPACITY)
        return;
    r->items[r->count++] = (JelliPetReward){from, to, (uint8_t)stat};
}

static void finish(JelliPetRewards *r, const JelliPet *pet)
{
    r->count = r->index = 0u;
    r->elapsed_ms = 0u;
    r->visible = false;
    r->burst = false;
    ++r->completed;
    for (unsigned i = 0u; i < 7u; ++i) {
        unsigned stat = i == 6u ? 8u : i + 1u;
        uint16_t to = jelli_pet_reward_stat(pet, stat);
        if (r->gains[i] > 0) {
            unsigned gain = (unsigned)r->gains[i];
            append(r, stat, gain > to ? 0u : (uint16_t)(to - gain), to);
        }
    }
    r->pending = r->health = false;
    memset(r->gains, 0, sizeof(r->gains));
}

static void prize(const JelliPetRewards *r, JelliGame *game)
{
    if (r->health) {
        if (r->health_kind != 0u && r->health_kind != 3u && r->health_kind != 4u)
            return;
        unsigned trigger = r->health_kind == 0u   ? JELLI_PRIZE_BRUSH
                           : r->health_kind == 3u ? JELLI_PRIZE_WASH
                                                  : JELLI_PRIZE_CARE;
        jelli_prize_complete(game, trigger);
    } else if (r->command == JELLI_CMD_MOMENT) {
        static const unsigned triggers[] = {JELLI_PRIZE_BREAKFAST, JELLI_PRIZE_TEA,
                                            JELLI_PRIZE_OUTING, JELLI_PRIZE_MOVIE};
        if (r->command_value < 4u)
            jelli_prize_complete(game, triggers[r->command_value]);
    } else if (r->command == JELLI_CMD_TRAVEL)
        jelli_prize_complete(game, JELLI_PRIZE_TRAVEL);
    else if (r->command == JELLI_CMD_CARE)
        jelli_prize_complete(game, JELLI_PRIZE_CARE);
}

static void begin(JelliPetRewards *r, const JelliEvent *event, bool health)
{
    memset(r->gains, 0, sizeof(r->gains));
    r->pet_id = event->pet_id;
    r->command = event->code;
    r->command_value = event->value;
    r->activity = event->after.activity;
    r->health_kind = (uint8_t)event->value;
    r->pending = true;
    r->health = health;
}

static void wake_rewards(JelliPetRewards *r, JelliGame *game, const JelliEvent *event)
{
    const JelliSleepLog *log = &game->sleep_log;
    if (!log->count || log->active || !(event->before.flags & 32u))
        return;
    unsigned last =
        ((unsigned)log->head + JELLI_SLEEP_SESSION_CAPACITY - 1u) % JELLI_SLEEP_SESSION_CAPACITY;
    const JelliSleepSession *session = &log->sessions[last];
    const JelliPet *pet = &game->pets[game->active];
    if (log->pet_id != pet->id || !(session->flags & JELLI_SLEEP_STATS_KNOWN))
        return;
    r->count = r->index = 0u;
    r->elapsed_ms = 0u;
    r->visible = false;
    r->burst = false;
    ++r->completed;
    append(r, 2u, session->bed_energy, pet->needs[JELLI_ENERGY]);
    append(r, 7u, session->bed_sleep_score, jelli_habits_sleep_score(&pet->habits));
    if (session->duration_seconds >= 21600u)
        jelli_prize_complete(game, JELLI_PRIZE_SLEEP);
}

static bool activity_command(unsigned code)
{
    return code == JELLI_CMD_EXERCISE || code == JELLI_CMD_WATER || code == JELLI_CMD_FEED ||
           code == JELLI_CMD_PLAY || code == JELLI_CMD_CLEAN || code == JELLI_CMD_CARE ||
           code == JELLI_CMD_GIFT || code == JELLI_CMD_MOMENT || code == JELLI_CMD_TRAVEL;
}

static void consume(JelliPetRewards *r, JelliGame *game, const JelliEvent *event,
                    bool health_active)
{
    const JelliPet *pet = &game->pets[game->active];
    if (event->pet_id != pet->id || event->result != JELLI_OK)
        return;
    if (event->kind == JELLI_EVENT_EFFECT) {
        if (r->pending && !r->health && r->activity == event->code) {
            add_gains(r, event);
            prize(r, game);
            finish(r, pet);
        }
        return;
    }
    if (event->kind != JELLI_EVENT_COMMAND)
        return;
    if (event->code == JELLI_CMD_WAKE) {
        wake_rewards(r, game, event);
        return;
    }
    if (event->code == JELLI_CMD_HEALTH) {
        if (!health_active)
            return;
        if (!r->pending || !r->health)
            begin(r, event, true);
        add_gains(r, event);
        return;
    }
    if (!activity_command(event->code))
        return;
    begin(r, event, false);
    add_gains(r, event);
    if (event->after.activity == JELLI_IDLE) {
        prize(r, game);
        finish(r, pet);
    }
}

static bool history_lost(const JelliPetRewards *r, const JelliEventLog *log)
{
    const JelliEvent *oldest = jelli_events_at(log, 0u);
    return r->cursor > log->sequence || (oldest && r->cursor && oldest->sequence > r->cursor &&
                                         oldest->sequence - r->cursor > 1u);
}

bool jelli_pet_rewards_process(JelliPetRewards *r, JelliGame *game, bool health_active,
                               bool health_done, uint32_t health_pet)
{
    const JelliEventLog *log = game->events;
    if (!log)
        return false;
    const JelliPet *pet = &game->pets[game->active];
    if (game->resuming || history_lost(r, log)) {
        clear(r);
        r->cursor = log->sequence;
        return false;
    }
    if (r->pet_id && r->pet_id != pet->id)
        clear(r);
    if (r->health && (!health_active || health_pet != pet->id)) {
        r->pending = r->health = false;
        memset(r->gains, 0, sizeof(r->gains));
    }
    unsigned count = log->count; /* Prize completion may append an event; never chase it. */
    for (unsigned i = 0u; i < count; ++i) {
        const JelliEvent *event = jelli_events_at(log, i);
        if (event && event->sequence > r->cursor) {
            JelliEvent copy = *event;
            r->cursor = copy.sequence;
            consume(r, game, &copy, health_active && health_pet == pet->id);
        }
    }
    if (r->health && health_active && health_done && r->pet_id == health_pet) {
        prize(r, game);
        finish(r, pet);
        return true;
    }
    return false;
}

bool jelli_pet_rewards_animate(JelliPetRewards *r, JelliParticles *particles, uint64_t now,
                               unsigned duration, bool visible, uint8_t *selected, uint16_t *value)
{
    if (!r->count || !visible || !duration) {
        r->visible = false;
        r->last_ms = now;
        return false;
    }
    if (!r->visible) {
        r->last_ms = now;
        r->visible = true;
        if (!r->burst) {
            jelli_particles_burst_tuned(particles, 190, 310, true, 3u, 25u);
            r->burst = true;
        }
    } else if (now >= r->last_ms) {
        uint64_t delta = now - r->last_ms;
        r->elapsed_ms += delta > duration ? duration : delta;
        r->last_ms = now;
    } else
        r->last_ms = now;
    if (r->elapsed_ms >= duration) {
        if (++r->index >= r->count) {
            r->count = 0u;
            return false;
        }
        r->elapsed_ms = 0u;
        jelli_particles_burst_tuned(particles, 190, 310, true, 3u, 25u);
    }
    const JelliPetReward *item = &r->items[r->index];
    *selected = item->stat;
    uint64_t counting = (uint64_t)duration * 2u / 3u;
    uint64_t elapsed = r->elapsed_ms < counting ? r->elapsed_ms : counting;
    *value = item->to;
    if (counting)
        *value = (uint16_t)(item->from + (item->to - item->from) * elapsed / counting);
    return true;
}
