#include "commands.h"
#include "suite.h"
#include "../boards/inventory.h"
#include <stdio.h>
#include <string.h>

/* Main-task owned; never put the response scratch on the small task stack. */
static char body[1536];

static void send(JelliDebugProtocol *protocol, uint32_t id, int size)
{
    jelli_debug_protocol_response(
        protocol, id,
        size > 0 && (size_t)size < sizeof(body) ? body : "{\"ok\":false,\"error\":\"report\"}");
}

static void status(JelliDebugProtocol *protocol, uint32_t id)
{
    const JelliFactoryState *s = jelli_factory_state();
    unsigned passed = 0, errors = 0, skipped = 0, pending = 0;
    for (unsigned i = 0; i < JELLI_FACTORY_TEST_COUNT; ++i) {
        const char *value = jelli_factory_test(i)->status;
        passed += !strcmp(value, "pass");
        errors += !strcmp(value, "error");
        skipped += !strcmp(value, "skip");
        pending += !strcmp(value, "pending");
    }
    send(
        protocol, id,
        snprintf(
            body, sizeof(body),
            "{\"ok\":true,\"schema\":3,\"board\":\"waveshare-31261\",\"profile\":\"factory-smoke\","
            "\"boot\":%lu,\"run\":%lu,\"device\":\"%s\",\"elf_sha256\":\"%s\","
            "\"status\":\"%s\",\"scope\":\"%s\",\"passed\":%u,\"errors\":%u,\"skipped\":%u,"
            "\"pending\":%u,\"acceptance\":false}",
            (unsigned long)s->boot, (unsigned long)s->run, s->device, s->elf_sha256, s->phase,
            s->scope, passed, errors, skipped, pending));
}

static void catalog(JelliDebugProtocol *protocol, uint32_t id)
{
    size_t used = (size_t)snprintf(body, sizeof(body), "{\"ok\":true,\"tests\":[");
    for (unsigned i = 0; i < JELLI_FACTORY_TEST_COUNT; ++i) {
        const JelliFactoryTest *test = jelli_factory_test(i);
        int size = snprintf(body + used, sizeof(body) - used,
                            "%s{\"id\":\"%s\",\"supported\":%s,\"required\":true}", i ? "," : "",
                            test->id, test->supported ? "true" : "false");
        if (size < 0 || (size_t)size >= sizeof(body) - used) {
            send(protocol, id, -1);
            return;
        }
        used += (size_t)size;
    }
    int size = snprintf(body + used, sizeof(body) - used, "]}");
    send(protocol, id, size >= 0 ? (int)used + size : -1);
}

static int inventory_tail(char *output, size_t capacity, unsigned index)
{
    const JelliInventory *s = jelli_inventory_get(index);
    int length = snprintf(output, capacity, ",\"address\":%u,\"valid\":%u,\"samples\":[",
                          s->address, s->valid);
    if (length < 0 || (size_t)length >= capacity)
        return -1;
    size_t used = (size_t)length;
    for (unsigned i = 0; i < s->count; ++i) {
        length = snprintf(output + used, capacity - used, "%s[%u,%u]", i ? "," : "",
                          s->registers[i], s->values[i]);
        if (length < 0 || (size_t)length >= capacity - used)
            return -1;
        used += (size_t)length;
    }
    length = snprintf(output + used, capacity - used, "]}");
    return length < 0 || (size_t)length >= capacity - used ? -1 : (int)used + length;
}

static void result(JelliDebugProtocol *protocol, uint32_t id, uint32_t run, const char *name)
{
    const JelliFactoryState *s = jelli_factory_state();
    if (!run || run != s->run) {
        jelli_debug_protocol_response(protocol, id, "{\"ok\":false,\"error\":\"stale_run\"}");
        return;
    }
    for (unsigned i = 0; i < JELLI_FACTORY_TEST_COUNT; ++i) {
        const JelliFactoryTest *t = jelli_factory_test(i);
        if (strcmp(t->id, name))
            continue;
        int size = snprintf(body, sizeof(body),
                            "{\"ok\":true,\"boot\":%lu,\"run\":%lu,\"id\":\"%s\",\"status\":\"%s\","
                            "\"error\":%d,\"elapsed_us\":%lld",
                            (unsigned long)s->boot, (unsigned long)s->run, t->id, t->status,
                            (int)t->error, (long long)t->elapsed_us);
        if (size < 0 || (size_t)size >= sizeof(body)) {
            send(protocol, id, -1);
            return;
        }
        int tail = !strcmp(name, "pmic.read")
                       ? snprintf(body + size, sizeof(body) - (size_t)size,
                                  ",\"registers\":[3,38,128,144],\"values\":[%u,%u,%u,%u]}",
                                  s->pmic_values[0], s->pmic_values[1], s->pmic_values[2],
                                  s->pmic_values[3])
                       : snprintf(body + size, sizeof(body) - (size_t)size, "}");
        if (i >= 7)
            tail = inventory_tail(body + size, sizeof(body) - (size_t)size, i - 7);
        send(protocol, id, tail < 0 ? -1 : size + tail);
        return;
    }
    jelli_debug_protocol_response(protocol, id, "{\"ok\":false,\"error\":\"test\"}");
}

void jelli_factory_command(void *ctx, uint32_t id, char **words, unsigned count)
{
    JelliDebugProtocol *protocol = ctx;
    if (count == 3u && !strcmp(words[2], "capabilities")) {
        jelli_debug_protocol_response(protocol, id,
                                      "{\"ok\":true,\"protocol\":1,\"profile\":\"factory-smoke\","
                                      "\"board\":\"waveshare-31261\",\"factory_schema\":3,"
                                      "\"commands\":[\"capabilities\",\"factory\"]}");
        return;
    }
    if (count >= 4u && !strcmp(words[2], "factory")) {
        uint32_t run;
        if (count == 4u && !strcmp(words[3], "status")) {
            status(protocol, id);
            return;
        }
        if (count == 4u && !strcmp(words[3], "tests")) {
            catalog(protocol, id);
            return;
        }
        if ((count == 4u || count == 5u) && !strcmp(words[3], "run")) {
            esp_err_t error = jelli_factory_begin(count == 5u ? words[4] : "all");
            if (error == ESP_OK)
                status(protocol, id);
            else
                jelli_debug_protocol_response(protocol, id,
                                              error == ESP_ERR_INVALID_STATE
                                                  ? "{\"ok\":false,\"error\":\"busy\"}"
                                                  : "{\"ok\":false,\"error\":\"test\"}");
            return;
        }
        if (count == 6u && !strcmp(words[3], "result") && jelli_debug_number(words[4], &run)) {
            result(protocol, id, run, words[5]);
            return;
        }
    }
    jelli_debug_protocol_response(protocol, id, "{\"ok\":false,\"error\":\"unsupported_command\"}");
}
