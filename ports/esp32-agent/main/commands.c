#include "commands.h"
#include "boot.h"
#include "probe.h"
#include <stdio.h>
#include <string.h>

/* Bumped when a command or reply shape changes incompatibly. */
enum { PROTOCOL_VERSION = 1 };

static void reply(char *out, size_t capacity, const char *body)
{
    (void)snprintf(out, capacity, "%s", body);
}

static void hello(char *out, size_t capacity)
{
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"app\":\"agent\",\"protocol\":%d,\"board\":\"%s\","
                   "\"version\":\"%s\"}",
                   PROTOCOL_VERSION, JELLI_BOARD_LABEL, JELLI_VERSION_LABEL);
}

static void probe(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *what = request->words[1];
    uint32_t ms = 0u;
    bool timed = request->count == 3u && jelli_agent_number(request->words[2], &ms);
    if (request->count == 2u && !strcmp(what, "chip"))
        jelli_agent_probe_chip(out, capacity);
    else if (request->count == 2u && !strcmp(what, "heap"))
        jelli_agent_probe_heap(out, capacity);
    else if (request->count == 2u && !strcmp(what, "pmu"))
        jelli_agent_probe_pmu(out, capacity);
    else if (timed && !strcmp(what, "pwr"))
        jelli_agent_probe_pwr(out, capacity, ms);
    else if (timed && !strcmp(what, "te"))
        jelli_agent_probe_te(out, capacity, ms);
    else
        reply(out, capacity, "{\"ok\":false,\"error\":\"syntax\"}");
}

void jelli_agent_command(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *verb = request->words[0];
    if (!strcmp(verb, "hello") && request->count == 1u)
        hello(out, capacity);
    else if (!strcmp(verb, "probe") && request->count >= 2u)
        probe(request, out, capacity);
    else if (!strcmp(verb, "boot") && request->count == 2u && !strcmp(request->words[1], "pet"))
        reply(out, capacity,
              jelli_agent_boot_pet_arm() ? "{\"ok\":true,\"app\":\"pet\",\"restart\":true}"
                                         : "{\"ok\":false,\"error\":\"pet_missing\"}");
    else
        reply(out, capacity, "{\"ok\":false,\"error\":\"syntax\"}");
}
