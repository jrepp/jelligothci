#include "pet_collectible_ui.h"
#include "pet_tune_library.h"
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
           page == JELLI_UI_PRESENT_ACTION || page == JELLI_UI_COLLECTIBLE_PLAY ||
           page == JELLI_UI_TUNE_PLAY;
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
    if (ui->page == JELLI_UI_TUNE_PLAY)
        return jelli_pet_tune_button(ui, slot, b);
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
        if (ui->gift_category == 2u)
            return jelli_pet_library_button(ui, slot, b);
        if (slot == 5u || slot == 6u)
            return button(b, slot == 5u ? 103u : 253u, 340u, 110u, 48u,
                          slot == 5u ? "PREV" : "NEXT");
        unsigned prize =
            jelli_pet_collectible_prize(ui->gift_category, ui->gift_page * 4u + slot - 1u);
        if (slot > 4u || prize >= jelli_prize_count)
            return false;

        return button(b, 93u + ((slot - 1u) % 2u) * 148u, 110u + ((slot - 1u) / 2u) * 112u, 140u,
                      100u, jelli_prizes[prize].name);
    }
    return detail_button(ui, slot, b);
}

void jelli_pet_collectible_back(JelliPetUi *ui)
{
    ui->tune.song = 0u;
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
    if (ui->page == JELLI_UI_TUNE_PLAY)
        jelli_pet_tune_action(ui, slot);
    else if (ui->page == JELLI_UI_COLLECTIBLE_PLAY)
        session_action(ui, game, slot);
    else if (!slot)
        jelli_pet_collectible_back(ui);
    else if (ui->page == JELLI_UI_COLLECTION) {
        ui->gift_category = (uint8_t)(slot - 1u);
        ui->gift_page = 0u;
        ui->page = JELLI_UI_GIFT_SHELF;
    } else if (ui->page == JELLI_UI_GIFT_SHELF) {
        unsigned count = ui->gift_category == 2u ? jelli_pet_library_count(game->tunes_seen)
                                                 : jelli_pet_collectible_count(ui->gift_category);
        unsigned pages = (count + 3u) / 4u;
        if (!pages)
            return;
        if (slot >= 5u)
            ui->gift_page = (uint8_t)((ui->gift_page + (slot == 5u ? pages - 1u : 1u)) % pages);
        else {
            if (ui->gift_category == 2u) {
                unsigned song =
                    jelli_pet_library_song(game->tunes_seen, ui->gift_page * 4u + slot - 1u);
                if (song >= JELLI_MUSIC_LAUNCH) {
                    jelli_pet_tune_start(ui, game, song);
                    return;
                }
            }
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
    ui->last_view.tunes_seen = game->tunes_seen;
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
