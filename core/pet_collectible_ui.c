#include "pet_collectible_ui.h"
#include "jelli/collectible.h"
#include <string.h>

static unsigned category_for(unsigned prize)
{
    return (prize == JELLI_COLLECTIBLE_CHAIN_PRIZE || prize == JELLI_COLLECTIBLE_BUBBLE_PRIZE) ? 1u
           : prize >= JELLI_COLLECTIBLE_RECORD_PRIZE &&
                   prize < JELLI_COLLECTIBLE_RECORD_PRIZE + JELLI_COLLECTIBLE_RECORD_COUNT
               ? 2u
               : 0u;
}

unsigned jelli_pet_collectible_count(unsigned category)
{
    unsigned count = 0u;
    for (unsigned i = 0u; i < jelli_prize_count; ++i)
        if (category_for(i) == category)
            ++count;
    return count;
}

unsigned jelli_pet_collectible_prize(unsigned category, unsigned position)
{
    for (unsigned i = 0u; i < jelli_prize_count; ++i) {
        if (category_for(i) != category)
            continue;
        if (!position)
            return i;
        --position;
    }
    return JELLI_PRIZE_COUNT;
}

unsigned jelli_pet_collectible_recipe(unsigned prize)
{
    for (unsigned i = 0; i < jelli_craft_recipe_count; ++i)
        if (jelli_craft_recipes[i].output == prize)
            return i;
    return jelli_craft_recipe_count;
}

bool jelli_pet_collectible_page(unsigned page)
{
    return page == JELLI_UI_COLLECTION || page == JELLI_UI_GIFT_SHELF ||
           page == JELLI_UI_PRESENT_ACTION || page == JELLI_UI_COLLECTIBLE_PLAY;
}

static bool button(JelliPetUiButton *b, unsigned x, unsigned y, unsigned w, unsigned h,
                   const char *label)
{
    *b = (JelliPetUiButton){.bounds = {x, y, w, h}, .label = label, .scale = 2u};
    return true;
}

static bool session_finished(const JelliCollectibleSession *s)
{
    return s->mode == JELLI_COLLECTIBLE_BUBBLE   ? s->bubble.status != JELLI_BUBBLE_RUNNING
           : s->mode == JELLI_COLLECTIBLE_CHAIN  ? s->chain.status != JELLI_CHAIN_RUNNING
           : s->mode == JELLI_COLLECTIBLE_RECORD ? s->record_cursor >= JELLI_RECORD_SAMPLES
                                                 : s->elapsed_ticks >= 80u;
}

static bool session_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *b)
{
    if (!slot)
        return button(b, 185u, 414u, 96u, 52u, "BACK");
    if (jelli_pet_collectible_lost(&ui->last_view.collectible))
        return false;
    if (ui->last_view.collectible.mode == JELLI_COLLECTIBLE_BUBBLE && !ui->collectible_menu &&
        !ui->collectible_win_ms)
        return jelli_pet_bubble_button(ui, slot, b);
    if (!ui->collectible_menu || slot != 1u || ui->collectible_win_ms)
        return false;
    const char *label =
        session_finished(&ui->last_view.collectible)
            ? (ui->last_view.collectible.mode == JELLI_COLLECTIBLE_BUBBLE ? "RETRY LEVEL"
                                                                          : "REPLAY")
            : "RESUME";
    return button(b, 143u, 190u, 180u, 48u, label);
}

static bool detail_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *b)
{
    if (!ui->latched_prize || ui->latched_prize > jelli_prize_count)
        return false;
    bool owned = (ui->last_view.prize_owned & (1u << (ui->latched_prize - 1u))) != 0u;
    bool cartridge = jelli_cartridge_index(ui->latched_prize - 1u) < JELLI_CARTRIDGE_COUNT;
    if (owned && cartridge) {
        if (ui->collectible_launch_pending)
            return false;
        const JelliCollectibleSession *saved = &ui->last_view.collectible;
        const char *label =
            ui->collectible_confirm == 2u ? "REPLACE & PLAY"
            : saved->pet_id && saved->prize + 1u == ui->latched_prize && !session_finished(saved)
                ? "RESUME"
                : "PLAY";
        if (slot == 1u)
            return button(b, 137u, 104u, 192u, 192u, label);
        return slot == 2u && button(b, 113u, 348u, 240u, 52u, label);
    }
    if (slot != 1u)
        return false;
    if (!owned && jelli_pet_collectible_recipe(ui->latched_prize - 1u) == jelli_craft_recipe_count)
        return false;
    const char *label = ui->collectible_confirm == 1u   ? "CONFIRM CRAFT"
                        : ui->collectible_confirm == 2u ? "REPLACE? PLAY"
                        : owned                         ? "PLAY"
                                                        : "CRAFT";
    return button(b, 113u, 334u, 240u, 52u, label);
}

bool jelli_pet_collectible_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *b)
{
    if (!ui->menu_open || !jelli_pet_collectible_page(ui->page))
        return false;
    if (ui->page == JELLI_UI_COLLECTIBLE_PLAY)
        return session_button(ui, slot, b);
    if (!slot)
        return button(b, 185u, 414u, 96u, 52u, "BACK");
    if (ui->page == JELLI_UI_COLLECTION) {
        static const char *const labels[] = {"PRESENTS", "CARTRIDGES", "RECORDS"};
        return slot <= 3u &&
               button(b, 103u, 116u + (slot - 1u) * 86u, 260u, 68u, labels[slot - 1u]);
    }
    if (ui->page == JELLI_UI_GIFT_SHELF) {
        if (ui->gift_category == 2u && (slot == 5u || slot == 6u))
            return button(b, slot == 5u ? 13u : 357u, 185u, 96u, 96u, slot == 5u ? "PREV" : "NEXT");
        if (slot == 5u || slot == 6u)
            return button(b, slot == 5u ? 103u : 253u, 340u, 110u, 48u,
                          slot == 5u ? "PREV" : "NEXT");
        unsigned prize =
            jelli_pet_collectible_prize(ui->gift_category, ui->gift_page * 4u + slot - 1u);
        if (slot > 4u || prize >= jelli_prize_count)
            return false;
        if (ui->gift_category == 2u)
            return button(b, 115u + ((slot - 1u) % 2u) * 120u, 90u + ((slot - 1u) / 2u) * 148u,
                          116u, 144u, jelli_prizes[prize].name);
        return button(b, 93u + ((slot - 1u) % 2u) * 148u, 110u + ((slot - 1u) / 2u) * 112u, 140u,
                      100u, jelli_prizes[prize].name);
    }
    return detail_button(ui, slot, b);
}

void jelli_pet_collectible_back(JelliPetUi *ui)
{
    ui->collectible_confirm = 0u;
    ui->collectible_launch_pending = false;
    ui->collectible_launch_anchor = 0u;
    ui->collectible_launch_ms = 0u;
    ui->collectible_menu = false;
    ui->collectible_win_ms = 0u;
    if (ui->page == JELLI_UI_GIFT_SHELF)
        ui->page = JELLI_UI_COLLECTION;
    else if (ui->page == JELLI_UI_COLLECTION)
        ui->page = JELLI_UI_HOME;
    else
        ui->page = JELLI_UI_GIFT_SHELF;
}

static void command(JelliPetUi *ui, JelliGame *game, JelliCommandKind kind, unsigned value)
{
    ui->result = jelli_game_command(game, (JelliCommand){kind, game->pets[game->active].id, value});
    if (ui->result == JELLI_OK) {
        ui->save_requested = true;
        ui->save_status = JELLI_SAVE_PENDING;
        ui->rendered = false;
    }
}

static void primary(JelliPetUi *ui, JelliGame *game)
{
    if (!ui->latched_prize || ui->latched_prize > jelli_prize_count)
        return;
    unsigned prize = ui->latched_prize - 1u;
    if (!(game->prizes.owned & (1u << prize))) {
        unsigned recipe = jelli_pet_collectible_recipe(prize);
        ui->result = jelli_collectible_recipe_available(game, recipe);
        if (ui->result != JELLI_OK)
            return;
        if (ui->collectible_confirm != 1u) {
            ui->collectible_confirm = 1u;
            return;
        }
        command(ui, game, JELLI_CMD_COLLECTIBLE_CRAFT, recipe);
        ui->collectible_confirm = 0u;
        return;
    }
    if (jelli_cartridge_index(prize) < JELLI_CARTRIDGE_COUNT)
        jelli_pet_collectible_launch(ui, game);
    else
        jelli_pet_collectible_open(ui, game);
}

void jelli_pet_collectible_open(JelliPetUi *ui, JelliGame *game)
{
    if (!ui->latched_prize || ui->latched_prize > jelli_prize_count)
        return;
    unsigned prize = ui->latched_prize - 1u;
    const JelliCollectibleSession *saved = &game->collectible;
    bool replacing_lab =
        saved->pet_id &&
        (saved->mode == JELLI_COLLECTIBLE_BUBBLE || prize == JELLI_COLLECTIBLE_BUBBLE_PRIZE) &&
        (saved->prize != prize || saved->pet_id != game->pets[game->active].id);
    if (replacing_lab && ui->collectible_confirm != 2u) {
        ui->collectible_confirm = 2u;
        return;
    }
    command(ui, game, JELLI_CMD_COLLECTIBLE_PLAY, prize);
    ui->collectible_confirm = 0u;
    if (ui->result == JELLI_OK) {
        if (game->collectible.paused)
            command(ui, game, JELLI_CMD_COLLECTIBLE_CONTROL,
                    session_finished(&game->collectible) ? JELLI_COLLECTIBLE_RESTART
                                                         : JELLI_COLLECTIBLE_RESUME);
        ui->collectible_menu = false;
        ui->page = JELLI_UI_COLLECTIBLE_PLAY;
    }
}

static void session_action(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    if (!slot) {
        command(ui, game, JELLI_CMD_COLLECTIBLE_CONTROL, JELLI_COLLECTIBLE_CLOSE);
        jelli_pet_collectible_back(ui);
    } else if (slot >= 2u && game->collectible.mode == JELLI_COLLECTIBLE_BUBBLE) {
        jelli_pet_bubble_action(ui, game, slot);
    } else {
        command(ui, game, JELLI_CMD_COLLECTIBLE_CONTROL,
                session_finished(&game->collectible) ? JELLI_COLLECTIBLE_RESTART
                                                     : JELLI_COLLECTIBLE_RESUME);
        if (ui->result == JELLI_OK)
            ui->collectible_menu = false;
    }
}

static void select(JelliPetUi *ui, JelliGame *game, unsigned slot)
{
    if (ui->page == JELLI_UI_COLLECTIBLE_PLAY)
        session_action(ui, game, slot);
    else if (!slot)
        jelli_pet_collectible_back(ui);
    else if (ui->page == JELLI_UI_COLLECTION) {
        ui->gift_category = (uint8_t)(slot - 1u);
        ui->gift_page = 0u;
        ui->page = JELLI_UI_GIFT_SHELF;
    } else if (ui->page == JELLI_UI_GIFT_SHELF) {
        unsigned pages = (jelli_pet_collectible_count(ui->gift_category) + 3u) / 4u;
        if (!pages)
            return;
        if (slot >= 5u)
            ui->gift_page = (uint8_t)((ui->gift_page + (slot == 5u ? pages - 1u : 1u)) % pages);
        else {
            unsigned prize =
                jelli_pet_collectible_prize(ui->gift_category, ui->gift_page * 4u + slot - 1u);
            ui->latched_prize = (uint8_t)(prize + 1u);
            ui->page = JELLI_UI_PRESENT_ACTION;
            ui->collectible_confirm = 0u;
            if (ui->gift_category == 1u && game->collectible.pet_id &&
                (game->collectible.prize != prize ||
                 game->collectible.pet_id != game->pets[game->active].id))
                ui->collectible_confirm = 2u;
            if ((game->prizes.owned & (1u << prize)) &&
                (ui->gift_category == 2u ||
                 (ui->gift_category == 1u && ui->collectible_confirm != 2u)))
                primary(ui, game);
        }
    } else
        primary(ui, game);
}

bool jelli_pet_collectible_tap(JelliPetUi *ui, JelliGame *game, int x, int y)
{
    if (!ui->menu_open || !jelli_pet_collectible_page(ui->page))
        return false;
    /* A host may drain several inputs before rendering; hit-test the current session. */
    ui->last_view.collectible = game->collectible;
    ui->last_view.prize_owned = game->prizes.owned;
    ui->rendered = false;
    for (unsigned slot = 0u; slot <= 6u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_collectible_button(ui, slot, &b))
            continue;
        if (x >= (int)b.bounds.x && x < (int)(b.bounds.x + b.bounds.width) &&
            y >= (int)b.bounds.y && y < (int)(b.bounds.y + b.bounds.height)) {
            ui->result = JELLI_OK;
            select(ui, game, slot);
            return true;
        }
    }
    if (ui->page == JELLI_UI_COLLECTIBLE_PLAY &&
        game->collectible.mode == JELLI_COLLECTIBLE_BUBBLE && !game->collectible.paused &&
        x >= 174 && x < 366 && y >= 50 && y < 370)
        jelli_pet_bubble_action(ui, game, 3u);
    return true;
}

bool jelli_pet_collectible_swipe(JelliPetUi *ui, JelliGame *game, int dx, int dy)
{
    if (!ui->menu_open || !jelli_pet_collectible_page(ui->page))
        return false;
    unsigned ax = dx < 0 ? 0u - (unsigned)dx : (unsigned)dx;
    unsigned ay = dy < 0 ? 0u - (unsigned)dy : (unsigned)dy;
    if (ui->page == JELLI_UI_COLLECTIBLE_PLAY &&
        (game->collectible.mode == JELLI_COLLECTIBLE_CHAIN ||
         game->collectible.mode == JELLI_COLLECTIBLE_BUBBLE) &&
        !game->collectible.paused && (dx || dy)) {
        unsigned direction = ay > ax ? (dy < 0 ? JELLI_COLLECTIBLE_UP : JELLI_COLLECTIBLE_DOWN)
                                     : (dx < 0 ? JELLI_COLLECTIBLE_LEFT : JELLI_COLLECTIBLE_RIGHT);
        command(ui, game, JELLI_CMD_COLLECTIBLE_CONTROL, direction);
    } else if (ui->page == JELLI_UI_GIFT_SHELF && dx && ax >= ay)
        select(ui, game, dx < 0 ? 6u : 5u);
    return true;
}

void jelli_pet_collectible_key(const JelliPetUi *ui, const JelliGame *game, JelliPetRenderKey *key)
{
    key->collectible = game->collectible;
    const JelliCollectibleProgress *progress = &game->pets[game->active].collectible_progress;
    key->collectible_special = progress->special_unlocked;
    unsigned cartridge =
        ui->latched_prize ? jelli_cartridge_index(ui->latched_prize - 1u) : JELLI_CARTRIDGE_COUNT;
    if (cartridge < JELLI_CARTRIDGE_COUNT) {
        key->cartridge_plays = game->cartridge_stats[cartridge].plays;
        key->cartridge_wins = game->cartridge_stats[cartridge].wins;
    }
    key->collectible_record_rewarded = progress->record_rewarded;
    if (ui->latched_prize && ui->latched_prize <= JELLI_PRIZE_COUNT)
        key->collectible_play_ticks = progress->play_ticks[ui->latched_prize - 1u];
    key->keyboard_focus = ui->keyboard_focus;
    key->keyboard_slot = ui->keyboard_slot;
    key->gift_category = ui->gift_category;
    key->gift_page = ui->gift_page;
    key->collectible_confirm = ui->collectible_confirm;
    key->collectible_reduced_motion = ui->collectible_reduced_motion;
    key->collectible_menu = ui->collectible_menu;
    key->collectible_win_ms = ui->collectible_win_ms;
    key->collectible_launch_ms = ui->collectible_launch_ms;
    key->collectible_launch_pending = ui->collectible_launch_pending;
    key->collectible_cheer_phase = (uint8_t)(game->pets[game->active].reaction_ticks / 2u % 2u);
}

static bool same_session(const JelliCollectibleSession *x, const JelliCollectibleSession *y)
{
    return x->pet_id == y->pet_id && x->prize == y->prize && x->mode == y->mode &&
           x->active == y->active && x->paused == y->paused &&
           x->record_cursor == y->record_cursor && x->elapsed_ticks == y->elapsed_ticks &&
           x->chain.length == y->chain.length && x->chain.pickup == y->chain.pickup &&
           jelli_bubble_same(&x->bubble, &y->bubble) && x->chain.status == y->chain.status &&
           x->chain.relaxed == y->chain.relaxed &&
           memcmp(x->chain.body, y->chain.body, sizeof(x->chain.body)) == 0;
}

bool jelli_pet_collectible_same(const JelliPetRenderKey *a, const JelliPetRenderKey *b)
{
    return a->collectible_cheer_phase == b->collectible_cheer_phase &&
           a->cartridge_plays == b->cartridge_plays && a->cartridge_wins == b->cartridge_wins &&
           a->collectible_special == b->collectible_special &&
           a->collectible_record_rewarded == b->collectible_record_rewarded &&
           a->collectible_play_ticks == b->collectible_play_ticks &&
           a->keyboard_focus == b->keyboard_focus && a->keyboard_slot == b->keyboard_slot &&
           a->gift_category == b->gift_category && a->gift_page == b->gift_page &&
           a->collectible_confirm == b->collectible_confirm &&
           a->collectible_reduced_motion == b->collectible_reduced_motion &&
           a->collectible_menu == b->collectible_menu &&
           a->collectible_win_ms == b->collectible_win_ms &&
           a->collectible_launch_ms == b->collectible_launch_ms &&
           a->collectible_launch_pending == b->collectible_launch_pending &&
           same_session(&a->collectible, &b->collectible);
}
