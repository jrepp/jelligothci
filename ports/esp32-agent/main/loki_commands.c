#include "loki_commands.h"
#include "identity.h"
#include "loki_link.h"
#include "loki_models.h"
#include "pairing.h"
#include "player.h"
#include "voice.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "loki";
/* Console task only: the message being shown and the last question seen. */
static JelliLokiMessage shown, question;
static bool has_question;

void jelli_agent_loki_start(bool audio, bool radio)
{
    bool identity = jelli_agent_identity_start(radio);
    bool pairing = jelli_agent_pairing_start();
    bool player = audio && jelli_agent_player_start() && jelli_agent_voice_start();
    bool link =
        identity && pairing && jelli_agent_link_start(player ? jelli_agent_player_hooks() : NULL);
    ESP_LOGI(TAG, "identity=%d pairing=%d audio=%d link=%d", identity, pairing, player, link);
}

static void inbox(char *out, size_t capacity)
{
    static char canonical[4096];
    if (!jelli_agent_link_receive(&shown, 0u)) {
        (void)snprintf(out, capacity, "{\"ok\":true,\"empty\":true}");
        return;
    }
    if (shown.type == JELLI_LOKI_ASK) {
        question = shown;
        has_question = true;
    } else if (shown.type == JELLI_LOKI_ASK_CANCEL && has_question &&
               !strcmp(shown.id, question.id)) {
        has_question = false;
    }
    size_t size = jelli_loki_canonical(&shown, canonical, sizeof(canonical));
    /* The canonical form is printable ASCII; quotes and backslashes are escaped
     * once more for the JSON reply. Long messages go to the log instead. */
    static char escaped[2048];
    size_t used = 0u;
    bool fits = size > 0u;
    for (size_t i = 0u; fits && i < size; ++i) {
        bool special = canonical[i] == '"' || canonical[i] == '\\';
        fits = used + (special ? 2u : 1u) < sizeof(escaped);
        if (fits && special)
            escaped[used++] = '\\';
        if (fits)
            escaped[used++] = canonical[i];
    }
    escaped[fits ? used : 0u] = '\0';
    fits = fits && used + 40u < capacity;
    (void)snprintf(out, capacity, "{\"ok\":true,\"seq\":%u,\"message\":\"%s\"}",
                   (unsigned)shown.seq, fits ? escaped : "(too long; see the log)");
    if (!fits)
        ESP_LOGI(TAG, "Inbox: %s", size ? canonical : "(no canonical form)");
}

static void answer(const char *word, char *out, size_t capacity)
{
    uint32_t choice = 0u;
    char digest[JELLI_LOKI_DIGEST];
    if (!jelli_agent_number(word, &choice) || !has_question || choice >= question.option_count) {
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"no_such_choice\"}");
        return;
    }
    jelli_loki_card_digest(&question, digest);
    JelliLokiOut reply = {.type = JELLI_LOKI_ANSWER,
                          .id = question.id,
                          .nonce = question.nonce,
                          .digest = digest,
                          .choice = choice};
    bool sent = jelli_agent_link_send(&reply);
    if (sent)
        has_question = false;
    (void)snprintf(out, capacity, sent ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"offline\"}");
}

/* "loki models" and "loki model NAME" (ADR-018); false for other verbs. */
static bool model_command(const JelliAgentRequest *request, const char *verb, char *out,
                          size_t capacity)
{
    if (!strcmp(verb, "models") && request->count == 2u)
        jelli_agent_models_show(out, capacity);
    else if (!strcmp(verb, "model") && request->count == 3u)
        jelli_agent_models_pick(request->words[2], out, capacity);
    else
        return false;
    return true;
}

bool jelli_agent_command_loki(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *verb = request->count >= 2u ? request->words[1] : "";
    if (strcmp(request->words[0], "loki") != 0)
        return false;
    if (!strcmp(verb, "status") && request->count == 2u) {
        jelli_agent_link_status(out, capacity);
    } else if (!strcmp(verb, "inbox") && request->count == 2u) {
        inbox(out, capacity);
    } else if (!strcmp(verb, "answer") && request->count == 3u) {
        answer(request->words[2], out, capacity);
    } else if (!strcmp(verb, "ptt") && request->count == 3u &&
               !strcmp(request->words[2], "start")) {
        (void)snprintf(out, capacity,
                       jelli_agent_voice_begin() ? "{\"ok\":true}"
                                                 : "{\"ok\":false,\"error\":\"offline_or_busy\"}");
    } else if (!strcmp(verb, "ptt") && request->count == 3u && !strcmp(request->words[2], "stop")) {
        jelli_agent_voice_end();
        (void)snprintf(out, capacity, "{\"ok\":true}");
    } else if (model_command(request, verb, out, capacity)) {
        return true;
    } else if (!strcmp(verb, "cancel") && request->count == 2u) {
        JelliLokiOut cancel = {.type = JELLI_LOKI_CANCEL};
        (void)snprintf(out, capacity,
                       jelli_agent_link_send(&cancel) ? "{\"ok\":true}"
                                                      : "{\"ok\":false,\"error\":\"offline\"}");
    } else {
        (void)snprintf(out, capacity, "{\"ok\":false,\"error\":\"syntax\"}");
    }
    return true;
}
