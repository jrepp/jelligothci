#include "loki_peer.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "jelli/agent_args.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

static const char *TAG = "loki_link";
/* Link task only: the last address that resolved, and the pairing it belongs to. */
static char last_resolved[16];
static uint32_t resolved_generation;
/* The session's peer; zero between sessions. */
static atomic_uint peer_ip, peer_port;

bool jelli_agent_peer_resolve(const JelliAgentBridge *bridge, uint32_t generation, char dial[16])
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *found = NULL;
    uint8_t ipv4[4];
    if (resolved_generation != generation)
        last_resolved[0] = '\0';
    resolved_generation = generation;
    if (jelli_agent_ipv4_parse(bridge->host, ipv4)) { /* At most 15 characters. */
        memcpy(dial, bridge->host, strlen(bridge->host) + 1u);
        return true;
    }
    if (getaddrinfo(bridge->host, NULL, &hints, &found) == 0 && found) {
        const struct sockaddr_in *address = (const struct sockaddr_in *)found->ai_addr;
        (void)inet_ntoa_r(address->sin_addr, last_resolved, sizeof(last_resolved));
        freeaddrinfo(found);
    } else {
        ESP_LOGW(TAG, "%s did not resolve; trying the last known address", bridge->host);
    }
    const char *choice = last_resolved[0] ? last_resolved : bridge->address;
    (void)snprintf(dial, 16u, "%s", choice);
    return dial[0] != '\0';
}

bool jelli_agent_peer_set(const char *dial, uint16_t port)
{
    struct in_addr address;
    if (inet_pton(AF_INET, dial, &address) != 1 || !address.s_addr || !port)
        return false;
    atomic_store(&peer_ip, address.s_addr);
    atomic_store(&peer_port, port);
    return true;
}

void jelli_agent_peer_clear(void)
{
    atomic_store(&peer_ip, 0u);
    atomic_store(&peer_port, 0u);
}

/* esp_websocket_client does not reveal its socket until the TLS handshake is done, so
 * the socket is found by its peer instead (memo-027). Only the link connects to the
 * bridge's address and port, and the link starts a session only after the last one
 * has finished, so the one match is the current session's socket. */
void jelli_agent_peer_shutdown(void)
{
    uint32_t ip = atomic_load(&peer_ip);
    unsigned port = atomic_load(&peer_port);
    if (!ip || !port)
        return;
    for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; ++fd) {
        struct sockaddr_in peer = {0};
        socklen_t size = sizeof(peer);
        if (getpeername(fd, (struct sockaddr *)&peer, &size) == 0 && peer.sin_family == AF_INET &&
            peer.sin_addr.s_addr == ip && ntohs(peer.sin_port) == port)
            (void)shutdown(fd, SHUT_RDWR);
    }
}
