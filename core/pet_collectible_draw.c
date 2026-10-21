#include "pet_collectible_ui.h"
#include "jelli/collectible.h"
#include <stdio.h>
#include <string.h>

static const char *const categories[] = {"PRESENTS", "CARTRIDGES", "RECORDS"};

static void control(Canvas *c, const JelliPetUi *ui, unsigned slot, bool dim)
{
    JelliPetUiButton b;
    if (!jelli_pet_collectible_button(ui, slot, &b))
        return;
    if (!slot) {
        jelli_canvas_disk(c, 233, 462, 48, TEAL);
        jelli_canvas_centered(c, "BACK", 432, 2u, PALE);
        return;
    }
    if (ui->page == JELLI_UI_GIFT_SHELF && ui->gift_category == 2u && slot >= 5u) {
        int x = slot == 5u ? 61 : 405;
        jelli_canvas_disk(c, x, 233, 48, TEAL);
        jelli_canvas_text(c, b.label, x - 32, 221, 2u, PALE);
        return;
    }
    jelli_canvas_rect(c, (int)b.bounds.x, (int)b.bounds.y, (int)b.bounds.width,
                      (int)b.bounds.height, dim ? INK : TEAL);
    unsigned scale = b.bounds.width < 140u ? 1u : 2u;
    int width = 0;
    for (const char *p = b.label; *p; ++p)
        width += (int)(8u * scale);
    jelli_canvas_text(c, b.label, (int)(b.bounds.x + b.bounds.width / 2u) - width / 2,
                      (int)b.bounds.y + 16, scale, PALE);
}

static unsigned collected(const JelliGame *game, unsigned category)
{
    unsigned count = 0u;
    for (unsigned i = 0u; i < jelli_pet_collectible_count(category); ++i) {
        unsigned prize = jelli_pet_collectible_prize(category, i);
        count += (game->prizes.owned & (1u << prize)) ? 1u : 0u;
    }
    return count;
}

static void categories_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    jelli_canvas_heading(c, "GIFTS", 42, 3u);
    jelli_canvas_centered(c, "YOUR LITTLE COLLECTION", 84, 1u, PALE);
    for (unsigned i = 0; i < 3u; ++i) {
        control(c, ui, i + 1u, false);
        char count[24];
        int n = snprintf(count, sizeof(count), "COLLECTED %u/%u", collected(game, i),
                         jelli_pet_collectible_count(i));
        if (n > 0 && (size_t)n < sizeof(count))
            jelli_canvas_centered(c, count, 162 + (int)i * 86, 1u, PALE);
    }
}

static void record_card(Canvas *c, const JelliPetUiButton *b, unsigned prize, bool owned)
{
    int x = (int)b->bounds.x, y = (int)b->bounds.y;
    c->dim = !owned;
    jelli_canvas_centered_sprite(c, jelli_prizes[prize].asset, x + 58, y + 45, 3u);
    c->dim = false;
    const char *name = jelli_prizes[prize].name;
    const char *space = strchr(name, ' ');
    size_t first = space ? (size_t)(space - name) : strlen(name);
    char word[25];
    if (first >= sizeof(word))
        return;
    memcpy(word, name, first);
    word[first] = '\0';
    jelli_canvas_text(c, word, x + 58 - (int)first * 8, y + 90, 2u, PALE);
    if (space)
        jelli_canvas_text(c, space + 1, x + 58 - (int)strlen(space + 1) * 8, y + 114, 2u, PALE);
}

static void shelf_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    unsigned category = ui->gift_category < 3u ? ui->gift_category : 0u;
    jelli_canvas_heading(c, categories[category], 40, 2u);
    char count[24];
    int n = snprintf(count, sizeof(count), "%u/%u COLLECTED - %u/%u", collected(game, category),
                     jelli_pet_collectible_count(category), ui->gift_page + 1u,
                     (jelli_pet_collectible_count(category) + 3u) / 4u);
    if (n > 0 && (size_t)n < sizeof(count))
        jelli_canvas_centered(c, count, 78, 1u, PALE);
    for (unsigned slot = 1u; slot <= 4u; ++slot) {
        JelliPetUiButton b;
        if (!jelli_pet_collectible_button(ui, slot, &b))
            continue;
        unsigned prize = jelli_pet_collectible_prize(category, ui->gift_page * 4u + slot - 1u);
        bool owned = (game->prizes.owned & (1u << prize)) != 0u;
        if (category == 2u) {
            record_card(c, &b, prize, owned);
            continue;
        }
        jelli_canvas_rect(c, (int)b.bounds.x, (int)b.bounds.y, 140, 100, INK);
        jelli_canvas_rect(c, (int)b.bounds.x + 2, (int)b.bounds.y + 2, 136, 96, BG);
        c->dim = !owned;
        jelli_canvas_centered_sprite(c, jelli_prizes[prize].asset, (int)b.bounds.x + 70,
                                     (int)b.bounds.y + (category == 1u ? 39 : 33),
                                     category == 1u ? 3u : 1u);
        c->dim = false;
        const char *name = b.label;
        int width = 0;
        for (const char *p = name; *p; ++p)
            width += 8;
        jelli_canvas_text(c, name, (int)b.bounds.x + 70 - width / 2,
                          (int)b.bounds.y + (category == 1u ? 83 : 62), 1u, PALE);
        if (category == 1u)
            continue;
        unsigned recipe = jelli_pet_collectible_recipe(prize);
        const char *action = owned ? "PLAY"
                             : jelli_collectible_recipe_available(game, recipe) == JELLI_OK
                                 ? "CRAFT"
                                 : "SEE RECIPE";
        jelli_canvas_text(c, action, (int)b.bounds.x + 20, (int)b.bounds.y + 82, 1u,
                          owned ? MINT : GOLD);
    }
    control(c, ui, 5u, false);
    control(c, ui, 6u, false);
}

static const char *acquisition(unsigned prize)
{
    static const char *const hints[] = {"GARDEN OUTINGS", "KEEP BRUSHING",    "MORNING BREAKFASTS",
                                        "SHARE TEA",      "WATCH A MOVIE",    "WASH TOGETHER",
                                        "LINKED SLEEP",   "CARE THEN GARDEN", "PLAY WITH A PRESENT",
                                        "REWARD SNUGGLE"};
    return prize < sizeof(hints) / sizeof(hints[0]) ? hints[prize] : "COMBINE TWO GIFTS";
}

static bool owned_cartridge(const JelliGame *game, unsigned prize)
{
    return (game->prizes.owned & (1u << prize)) &&
           jelli_cartridge_index(prize) < JELLI_CARTRIDGE_COUNT;
}

static void detail_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    unsigned prize = ui->latched_prize ? ui->latched_prize - 1u : JELLI_PRIZE_COUNT;
    if (prize >= jelli_prize_count || prize >= JELLI_PRIZE_COUNT)
        return;
    bool owned = (game->prizes.owned & (1u << prize)) != 0u;
    if (owned_cartridge(game, prize)) {
        jelli_pet_cartridge_draw(c, game, prize);
        control(c, ui, 2u, false);
        return;
    }
    jelli_canvas_heading(c, "COLLECTIBLE", 40, 2u);
    jelli_canvas_centered(c, jelli_prizes[prize].name, 82, 1u, MINT);
    jelli_canvas_centered_sprite(c, jelli_prizes[prize].asset, 233, 145, 2u);
    unsigned recipe = jelli_pet_collectible_recipe(prize);
    if (recipe < jelli_craft_recipe_count) {
        const JelliCraftRecipe *r = &jelli_craft_recipes[recipe];
        jelli_canvas_centered(c, "RECIPE - KEEP BOTH GIFTS", 200, 1u, PALE);
        const unsigned inputs[2] = {r->first, r->second};
        for (unsigned i = 0; i < 2u; ++i) {
            char line[40];
            unsigned input = inputs[i];
            int n = snprintf(line, sizeof(line), "%s: %s", jelli_prizes[input].name,
                             game->prizes.owned & (1u << input) ? "OWNED" : "MISSING");
            if (n > 0 && (size_t)n < sizeof(line))
                jelli_canvas_centered(c, line, 222 + (int)i * 22, 1u, PALE);
        }
    } else {
        jelli_canvas_centered(c, "FIND THROUGH", 206, 1u, PALE);
        jelli_canvas_centered(c, acquisition(prize), 232, 1u, MINT);
    }
    const char *message = ui->collectible_confirm == 2u   ? "REPLACES OTHER SAVED PLAY"
                          : ui->collectible_confirm == 1u ? "MAKE THIS COLLECTIBLE?"
                          : owned                         ? "EVERY PET CAN PLAY"
                                                          : "FIND INGREDIENTS IN PRESENTS";
    if (ui->result == JELLI_ASLEEP)
        message = "WAKE YOUR PET TO PLAY";
    else if (ui->result == JELLI_BUSY)
        message = "FINISH CARE THEN PLAY";
    jelli_canvas_centered(c, message, 284, 1u, GOLD);
    bool craftable = recipe < jelli_craft_recipe_count &&
                     jelli_collectible_recipe_available(game, recipe) == JELLI_OK;
    const JelliPet *pet = &game->pets[game->active];
    if (jelli_collectible_special_pair(pet, prize)) {
        char progress[40];
        int n = snprintf(progress, sizeof(progress), "SPECIAL CHEER %u/%uS",
                         pet->collectible_progress.play_ticks[prize] / 10u,
                         JELLI_COLLECTIBLE_PROGRESS_TICKS / 10u);
        if (n > 0 && (size_t)n < sizeof(progress))
            jelli_canvas_centered(c, progress, 308, 1u, MINT);
    }
    control(c, ui, 1u, !owned && !craftable);
}

static void focus(Canvas *c, const JelliPetUi *ui)
{
    JelliPetUiButton b;
    if (!ui->keyboard_focus || !jelli_pet_collectible_button(ui, ui->keyboard_slot, &b))
        return;
    int x = (int)b.bounds.x - 3, y = (int)b.bounds.y - 3;
    int w = (int)b.bounds.width + 6, h = (int)b.bounds.height + 6;
    jelli_canvas_rect(c, x, y, w, 3, GOLD);
    jelli_canvas_rect(c, x, y + h - 3, w, 3, GOLD);
    jelli_canvas_rect(c, x, y, 3, h, GOLD);
    jelli_canvas_rect(c, x + w - 3, y, 3, h, GOLD);
}

void jelli_pet_collectible_draw(Canvas *c, const JelliPetUi *ui, const JelliGame *game)
{
    jelli_canvas_rect(c, 0, 0, JELLI_WIDTH, JELLI_HEIGHT, BG);
    if (ui->page == JELLI_UI_COLLECTIBLE_PLAY) {
        jelli_pet_collectible_session_draw(c, ui, game);
        focus(c, ui);
        return;
    }
    if (ui->page == JELLI_UI_COLLECTION)
        categories_draw(c, ui, game);
    else if (ui->page == JELLI_UI_GIFT_SHELF)
        shelf_draw(c, ui, game);
    else
        detail_draw(c, ui, game);
    control(c, ui, 0u, false);
    focus(c, ui);
}
