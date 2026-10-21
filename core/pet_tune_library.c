#include "pet_tune_library.h"
#include "pet_collectible_ui.h"
#include "jelli/collectible.h"
#include "jelli_asset_ids.h"
#include <stdio.h>
#include <string.h>

_Static_assert(JELLI_MUSIC_LAUNCH == JELLI_COLLECTIBLE_RECORD_COUNT,
               "Rare record tracks must precede encountered tunes");

unsigned jelli_pet_library_count(uint16_t seen)
{
    unsigned count = JELLI_COLLECTIBLE_RECORD_COUNT;
    for (unsigned song = JELLI_MUSIC_LAUNCH; song < JELLI_MUSIC_TRACK_COUNT; ++song)
        count += (seen & (UINT32_C(1) << song)) ? 1u : 0u;
    return count;
}

unsigned jelli_pet_library_song(uint16_t seen, unsigned position)
{
    if (position < JELLI_COLLECTIBLE_RECORD_COUNT)
        return position;
    position -= JELLI_COLLECTIBLE_RECORD_COUNT;
    for (unsigned song = JELLI_MUSIC_LAUNCH; song < JELLI_MUSIC_TRACK_COUNT; ++song) {
        if (!(seen & (UINT32_C(1) << song)))
            continue;
        if (!position)
            return song;
        --position;
    }
    return JELLI_MUSIC_TRACK_COUNT;
}

bool jelli_pet_library_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *b)
{
    if (slot == 5u || slot == 6u) {
        *b = (JelliPetUiButton){.bounds = {slot == 5u ? 13u : 357u, 185u, 96u, 96u},
                                .label = slot == 5u ? "PREV" : "NEXT",
                                .scale = 2u};
        return true;
    }
    if (slot < 1u || slot > 4u)
        return false;
    unsigned song =
        jelli_pet_library_song(ui->last_view.tunes_seen, ui->gift_page * 4u + slot - 1u);
    if (song >= JELLI_MUSIC_TRACK_COUNT)
        return false;
    *b = (JelliPetUiButton){
        .bounds = {115u + ((slot - 1u) % 2u) * 120u, 90u + ((slot - 1u) / 2u) * 148u, 116u, 144u},
        .label = jelli_music_name(song),
        .scale = 2u};
    return true;
}

static void card_name(Canvas *c, const char *name, int x, int y)
{
    const char *space = strchr(name, ' ');
    size_t first = space ? (size_t)(space - name) : strlen(name);
    char word[25];
    if (first >= sizeof(word))
        return;
    memcpy(word, name, first);
    word[first] = '\0';
    jelli_canvas_text(c, word, x + 58 - (int)first * 8, y + 82, 2u, PALE);
    if (space)
        jelli_canvas_text(c, space + 1, x + 58 - (int)strlen(space + 1) * 8, y + 104, 2u, PALE);
}

void jelli_pet_library_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    unsigned count = jelli_pet_library_count(game->tunes_seen);
    unsigned available = count - JELLI_COLLECTIBLE_RECORD_COUNT;
    for (unsigned song = 0u; song < JELLI_COLLECTIBLE_RECORD_COUNT; ++song)
        available += (game->prizes.owned & (UINT32_C(1) << (JELLI_COLLECTIBLE_RECORD_PRIZE + song)))
                         ? 1u
                         : 0u;
    jelli_canvas_heading(c, "RECORDS", 40, 2u);
    char line[32];
    int n = snprintf(line, sizeof(line), "%u/%u READY - %u/%u", available, count,
                     ui->gift_page + 1u, (count + 3u) / 4u);
    if (n > 0 && (size_t)n < sizeof(line))
        jelli_canvas_centered(c, line, 78, 1u, PALE);
    for (unsigned slot = 1u; slot <= 4u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_library_button(ui, slot, &b))
            continue;
        unsigned song = jelli_pet_library_song(game->tunes_seen, ui->gift_page * 4u + slot - 1u);
        int x = (int)b.bounds.x, y = (int)b.bounds.y;
        bool record = song < JELLI_COLLECTIBLE_RECORD_COUNT;
        bool owned = !record || (game->prizes.owned &
                                 (UINT32_C(1) << (JELLI_COLLECTIBLE_RECORD_PRIZE + song)));
        if (record) {
            jelli_canvas_rect(c, x, y, 116, 2, GOLD);
            jelli_canvas_rect(c, x, y + 142, 116, 2, GOLD);
            jelli_canvas_rect(c, x, y, 2, 144, GOLD);
            jelli_canvas_rect(c, x + 114, y, 2, 144, GOLD);
        }
        c->dim = !owned;
        if (record)
            jelli_canvas_centered_sprite(
                c, jelli_prizes[JELLI_COLLECTIBLE_RECORD_PRIZE + song].asset, x + 58, y + 41, 3u);
        else {
            jelli_canvas_disk(c, x + 58, y + 41, 43, INK);
            jelli_canvas_disk(c, x + 58, y + 41, 34, TEAL);
            jelli_canvas_centered_sprite(c, JELLI_ASSET_EFFECTS_MUSIC, x + 58, y + 41, 1u);
        }
        c->dim = false;
        card_name(c, b.label, x, y);
        const char *kind = record ? (owned ? "RECORD" : "LOCKED") : "TUNE";
        jelli_canvas_text(c, kind, x + 58 - (int)strlen(kind) * 4, y + 129, 1u,
                          record ? GOLD : MINT);
    }
}

void jelli_pet_tune_start(JelliPetUi *ui, const JelliGame *game, unsigned song)
{
    if (song < JELLI_MUSIC_LAUNCH || song >= JELLI_MUSIC_TRACK_COUNT ||
        !(game->tunes_seen & (UINT32_C(1) << song)))
        return;
    uint32_t generation = ui->tune.generation + 1u; /* Playback identity only; wrapping is safe. */
    ui->tune = (JelliTunePlayback){.song = (uint8_t)(song + 1u),
                                   .pet_id = game->pets[game->active].id,
                                   .generation = generation};
    ui->page = JELLI_UI_TUNE_PLAY;
    ui->collectible_menu = false;
}

bool jelli_pet_tune_button(const JelliPetUi *ui, unsigned slot, JelliPetUiButton *b)
{
    if (!slot) {
        *b = (JelliPetUiButton){.bounds = {185u, 414u, 96u, 52u}, .label = "BACK", .scale = 2u};
        return true;
    }
    if (slot != 1u || !ui->tune.paused)
        return false;
    *b = (JelliPetUiButton){.bounds = {143u, 190u, 180u, 48u},
                            .label =
                                ui->tune.elapsed_ms >= JELLI_TUNE_DURATION_MS ? "REPLAY" : "RESUME",
                            .scale = 2u};
    return true;
}

void jelli_pet_tune_action(JelliPetUi *ui, unsigned slot)
{
    if (!slot) {
        ui->tune.song = 0u;
        jelli_pet_collectible_back(ui);
    } else if (slot == 1u && ui->tune.paused) {
        if (ui->tune.elapsed_ms >= JELLI_TUNE_DURATION_MS) {
            ui->tune.elapsed_ms = 0u;
            ++ui->tune.generation;
        }
        ui->tune.paused = false;
        ui->tune.anchor_ms = 0u;
    }
}

void jelli_pet_tune_update(JelliPetUi *ui, const JelliGame *game, uint64_t now)
{
    if (!ui->tune.song)
        return;
    const JelliPet *pet = &game->pets[game->active];
    if (!ui->menu_open || ui->page != JELLI_UI_TUNE_PLAY || ui->tune.pet_id != pet->id ||
        pet->asleep || pet->activity != JELLI_IDLE) {
        ui->tune.song = 0u;
        if (ui->page == JELLI_UI_TUNE_PLAY)
            jelli_pet_collectible_back(ui);
        return;
    }
    if (ui->tune.paused)
        return;
    if (!ui->tune.anchor_ms || now < ui->tune.anchor_ms - 1u)
        ui->tune.anchor_ms = now == UINT64_MAX ? UINT64_MAX : now + 1u;
    uint64_t elapsed = now - (ui->tune.anchor_ms - 1u);
    unsigned remaining = JELLI_TUNE_DURATION_MS - ui->tune.elapsed_ms;
    ui->tune.elapsed_ms += (uint16_t)(elapsed > remaining ? remaining : elapsed);
    ui->tune.anchor_ms = now == UINT64_MAX ? UINT64_MAX : now + 1u;
    if (ui->tune.elapsed_ms >= JELLI_TUNE_DURATION_MS)
        ui->tune.paused = true;
}
