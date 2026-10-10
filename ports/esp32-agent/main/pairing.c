#include "pairing.h"
#include "identity.h"
#include "settings.h"
#include "jelli/agent_args.h"
#include "jelli/agent_sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/x509_crt.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum { CHUNK_BYTES = 96 };

static SemaphoreHandle_t lock;
static StaticSemaphore_t lock_storage;
/* Guarded by lock: the link task reads it while the console task may change it. */
static JelliAgentBridge stored;
static bool paired;
static atomic_uint generation;
/* Console task only: a bridge certificate arriving in parts. */
static uint8_t staging[sizeof(stored.cert)];
static size_t staged;
static bool staging_open;

static void hex_of(const uint8_t *data, size_t size, char *out)
{
    JelliSha256 sha;
    uint8_t digest[JELLI_SHA256_SIZE];
    jelli_sha256_init(&sha);
    jelli_sha256_update(&sha, data, size);
    jelli_sha256_final(&sha, digest);
    for (size_t i = 0u; i < sizeof(digest); ++i)
        (void)snprintf(out + 2u * i, 3u, "%02x", (unsigned)digest[i]);
}

bool jelli_agent_pairing_start(void)
{
    lock = xSemaphoreCreateMutexStatic(&lock_storage);
    JelliAgentBridge bridge = {0};
    size_t host_size = sizeof(bridge.host) - 1u, port_size = sizeof(bridge.port);
    bridge.cert_size = sizeof(bridge.cert);
    paired = jelli_agent_settings_get("bridge_cert", bridge.cert, &bridge.cert_size) &&
             jelli_agent_settings_get("bridge_host", bridge.host, &host_size) &&
             jelli_agent_settings_get("bridge_port", &bridge.port, &port_size) &&
             port_size == sizeof(bridge.port) && bridge.port && bridge.cert_size;
    bridge.host[host_size < sizeof(bridge.host) ? host_size : 0u] = '\0';
    paired = paired && jelli_agent_host_valid(bridge.host);
    size_t address_size = sizeof(bridge.address) - 1u;
    uint8_t ipv4[4];
    if (!jelli_agent_settings_get("bridge_ip", bridge.address, &address_size))
        address_size = 0u;
    bridge.address[address_size] = '\0';
    if (!jelli_agent_ipv4_parse(bridge.address, ipv4))
        bridge.address[0] = '\0';
    if (paired)
        stored = bridge;
    return lock != NULL;
}

bool jelli_agent_pairing_get(JelliAgentBridge *bridge)
{
    bool result = false;
    if (lock && xSemaphoreTake(lock, portMAX_DELAY) == pdTRUE) {
        result = paired;
        if (paired)
            *bridge = stored;
        xSemaphoreGive(lock);
    }
    return result;
}

uint32_t jelli_agent_pairing_generation(void) { return atomic_load(&generation); }

static void replace(const JelliAgentBridge *bridge)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    paired = bridge != NULL;
    if (bridge)
        stored = *bridge;
    else
        memset(&stored, 0, sizeof(stored));
    xSemaphoreGive(lock);
    atomic_fetch_add(&generation, 1u);
}

static const char *receive_part(const JelliAgentRequest *request)
{
    uint32_t offset = 0u;
    uint8_t part[CHUNK_BYTES];
    size_t size = 0u;
    if (request->count != 4u || !jelli_agent_number(request->words[2], &offset) ||
        !jelli_agent_hex_decode(request->words[3], part, sizeof(part), &size) || !size)
        return "{\"ok\":false,\"error\":\"syntax\"}";
    if (!staging_open || offset != staged || size > sizeof(staging) - staged)
        return "{\"ok\":false,\"error\":\"sequence\"}";
    memcpy(staging + staged, part, size);
    staged += size;
    return NULL;
}

/* "pair commit HOST PORT SHA256 [IPV4]" with every word well formed. */
static bool commit_syntax(const JelliAgentRequest *request, uint32_t *port)
{
    uint8_t ipv4[4];
    return (request->count == 5u || request->count == 6u) &&
           jelli_agent_host_valid(request->words[2]) &&
           jelli_agent_number(request->words[3], port) && *port && *port <= 65535u &&
           strlen(request->words[4]) == 64u &&
           (request->count == 5u || jelli_agent_ipv4_parse(request->words[5], ipv4));
}

static bool store(const JelliAgentBridge *bridge)
{
    return jelli_agent_settings_set("bridge_cert", bridge->cert, bridge->cert_size) &&
           jelli_agent_settings_set("bridge_host", bridge->host, strlen(bridge->host)) &&
           jelli_agent_settings_set("bridge_port", &bridge->port, sizeof(bridge->port)) &&
           (bridge->address[0]
                ? jelli_agent_settings_set("bridge_ip", bridge->address, strlen(bridge->address))
                : jelli_agent_settings_erase("bridge_ip"));
}

/* Checks the staged certificate against its fingerprint and stores everything. */
static const char *commit(const JelliAgentRequest *request)
{
    JelliAgentBridge bridge = {0};
    uint32_t port = 0u;
    char actual[65];
    mbedtls_x509_crt crt;
    if (!commit_syntax(request, &port))
        return "{\"ok\":false,\"error\":\"syntax\"}";
    if (!staging_open || !staged)
        return "{\"ok\":false,\"error\":\"sequence\"}";
    hex_of(staging, staged, actual);
    mbedtls_x509_crt_init(&crt);
    bool parsed = mbedtls_x509_crt_parse_der(&crt, staging, staged) == 0;
    mbedtls_x509_crt_free(&crt);
    if (strcmp(actual, request->words[4]) != 0 || !parsed)
        return "{\"ok\":false,\"error\":\"certificate\"}";
    memcpy(bridge.cert, staging, staged);
    bridge.cert_size = staged;
    /* Both lengths were checked: a host is at most 63 characters, an IPv4 15. */
    memcpy(bridge.host, request->words[2], strlen(request->words[2]) + 1u);
    if (request->count == 6u)
        memcpy(bridge.address, request->words[5], strlen(request->words[5]) + 1u);
    bridge.port = (uint16_t)port;
    if (!store(&bridge))
        return "{\"ok\":false,\"error\":\"storage\"}";
    replace(&bridge);
    staging_open = false;
    staged = 0u;
    return NULL;
}

static const char *clear(void)
{
    if (!jelli_agent_settings_erase("bridge_cert") || !jelli_agent_settings_erase("bridge_host") ||
        !jelli_agent_settings_erase("bridge_port") || !jelli_agent_settings_erase("bridge_ip"))
        return "{\"ok\":false,\"error\":\"storage\"}";
    replace(NULL);
    return NULL;
}

static const char *show(char *out, size_t capacity)
{
    const char *fingerprint = jelli_agent_identity_fingerprint();
    if (!fingerprint[0])
        return "{\"ok\":false,\"error\":\"no_identity\"}";
    (void)snprintf(out, capacity, "{\"ok\":true,\"name\":\"%s\",\"fingerprint\":\"%s\"}",
                   jelli_agent_identity_name(), fingerprint);
    return NULL;
}

static void status(char *out, size_t capacity)
{
    JelliAgentBridge bridge;
    char fingerprint[65] = "";
    bool is_paired = jelli_agent_pairing_get(&bridge);
    if (is_paired)
        hex_of(bridge.cert, bridge.cert_size, fingerprint);
    (void)snprintf(out, capacity,
                   "{\"ok\":true,\"paired\":%s,\"host\":\"%s\",\"address\":\"%s\",\"port\":%u,"
                   "\"bridge\":\"%s\"}",
                   is_paired ? "true" : "false", is_paired ? bridge.host : "",
                   is_paired ? bridge.address : "", is_paired ? (unsigned)bridge.port : 0u,
                   fingerprint);
}

bool jelli_agent_command_pair(const JelliAgentRequest *request, char *out, size_t capacity)
{
    const char *verb = request->count >= 2u ? request->words[1] : "";
    const char *error = NULL;
    if (strcmp(request->words[0], "pair") != 0)
        return false;
    if (!strcmp(verb, "show") && request->count == 2u) {
        error = show(out, capacity);
    } else if (!strcmp(verb, "begin") && request->count == 2u) {
        staging_open = true;
        staged = 0u;
    } else if (!strcmp(verb, "cert")) {
        error = receive_part(request);
    } else if (!strcmp(verb, "commit")) {
        error = commit(request);
    } else if (!strcmp(verb, "status") && request->count == 2u) {
        status(out, capacity);
        return true;
    } else if (!strcmp(verb, "clear") && request->count == 2u) {
        error = clear();
    } else {
        error = "{\"ok\":false,\"error\":\"syntax\"}";
    }
    if (error)
        (void)snprintf(out, capacity, "%s", error);
    else if (strcmp(verb, "show") != 0)
        (void)snprintf(out, capacity, "{\"ok\":true,\"received\":%u}", (unsigned)staged);
    return true;
}
