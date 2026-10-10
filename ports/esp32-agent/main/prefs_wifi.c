#include "prefs_wifi.h"
#include "wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>

/* One errand at a time, handed over through state: the UI task moves IDLE to an ask
 * after filling the slot, the errand task moves the ask to its DONE after writing the
 * result, and the UI task moves DONE back to IDLE once it has read it. Each side
 * touches the slot and results only in its own states, so no lock is needed. */
typedef enum { IDLE, SCAN_ASKED, JOIN_ASKED, SCAN_DONE, JOIN_DONE } Errand;

static const char *TAG = "agent_prefs_wifi";
static atomic_int errand = IDLE;
static TaskHandle_t worker;
static JelliPrefsJoin slot; /* The join asked for; wiped as soon as it is done. */
static JelliPrefsNetwork found[JELLI_PREFS_MAX_NETWORKS + 8];
static int found_count;
static JelliPrefsOutcome outcome;

static void wipe_slot(void)
{
    volatile uint8_t *bytes = (volatile uint8_t *)&slot;
    for (size_t i = 0u; i < sizeof(slot); ++i)
        bytes[i] = 0u;
}

static void errand_task(void *unused)
{
    (void)unused;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int asked = atomic_load(&errand);
        if (asked == SCAN_ASKED) {
            found_count = jelli_agent_wifi_scan(found, sizeof(found) / sizeof(found[0]));
            atomic_store(&errand, SCAN_DONE);
        } else if (asked == JOIN_ASKED) {
            outcome =
                jelli_agent_wifi_join(slot.ssid, slot.ssid_size, slot.password, slot.password_size);
            wipe_slot();
            atomic_store(&errand, JOIN_DONE);
        }
    }
}

bool jelli_agent_prefs_wifi_start(void)
{
    static StaticTask_t task_control;
    /* Room for the Wi-Fi driver's calls and a log line; the records are static. */
    static StackType_t task_stack[4096u / sizeof(StackType_t)];
    worker = xTaskCreateStatic(errand_task, "agent_wifi_ui", sizeof(task_stack), NULL, 2u,
                               task_stack, &task_control);
    if (!worker)
        ESP_LOGE(TAG, "No task for Settings' Wi-Fi errands");
    return worker != NULL;
}

static bool ask(int which)
{
    int idle = IDLE;
    if (!worker || !atomic_compare_exchange_strong(&errand, &idle, which))
        return false;
    xTaskNotifyGive(worker);
    return true;
}

bool jelli_agent_prefs_wifi_scan(void) { return ask(SCAN_ASKED); }

bool jelli_agent_prefs_wifi_join(const JelliPrefsJoin *join)
{
    if (!worker || !join || atomic_load(&errand) != IDLE)
        return false;
    slot = *join; /* Only the UI task asks, so the errand stays IDLE meanwhile. */
    if (ask(JOIN_ASKED))
        return true;
    wipe_slot();
    return false;
}

void jelli_agent_prefs_wifi_deliver(JelliPrefs *prefs, uint64_t now_ms)
{
    int done = atomic_load(&errand);
    if (done == SCAN_DONE) {
        size_t count = found_count > 0 ? (size_t)found_count : 0u;
        jelli_prefs_scanned(prefs, found, count, found_count >= 0, now_ms);
        memset(found, 0, sizeof(found));
        atomic_store(&errand, IDLE);
    } else if (done == JOIN_DONE) {
        jelli_prefs_joined(prefs, outcome, now_ms);
        atomic_store(&errand, IDLE);
    }
}
