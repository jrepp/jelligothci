#include "network_config.h"
#include <string.h>

void jelli_network_defaults(JelliNetworkConfig *config)
{
    *config =
        (JelliNetworkConfig){.ntp = "pool.ntp.org", .timezone = "manual", .sync_seconds = 3600};
}

const char *jelli_network_timezone(const char *region)
{
    static const struct {
        const char *name, *rule;
    } zones[] = {{"manual", ""},
                 {"UTC", "UTC0"},
                 {"America/New_York", "EST5EDT,M3.2.0/2,M11.1.0/2"},
                 {"America/Chicago", "CST6CDT,M3.2.0/2,M11.1.0/2"},
                 {"America/Denver", "MST7MDT,M3.2.0/2,M11.1.0/2"},
                 {"America/Phoenix", "MST7"},
                 {"America/Los_Angeles", "PST8PDT,M3.2.0/2,M11.1.0/2"},
                 {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0/2"},
                 {"Europe/Berlin", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
                 {"Asia/Tokyo", "JST-9"},
                 {"Asia/Kolkata", "IST-5:30"},
                 {"Australia/Sydney", "AEST-10AEDT,M10.1.0/2,M4.1.0/3"}};
    for (size_t i = 0; i < sizeof(zones) / sizeof(zones[0]); ++i)
        if (!strcmp(region, zones[i].name))
            return zones[i].rule;
    return NULL;
}

static bool printable(const char *value, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i) {
        unsigned char ch = (unsigned char)value[i];
        if (!ch)
            return true;
        if (ch < 32u || ch > 126u)
            return false;
    }
    return false;
}

static bool hostname(const char *name)
{
    if (!name[0])
        return false;
    for (size_t i = 0; name[i]; ++i) {
        char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '.' || ch == '-'))
            return false;
    }
    return true;
}

bool jelli_network_config_valid(const JelliNetworkConfig *c)
{
    if (!printable(c->ssid, sizeof(c->ssid)) || !printable(c->password, sizeof(c->password)) ||
        !printable(c->ntp, sizeof(c->ntp)) || !printable(c->timezone, sizeof(c->timezone)) ||
        !printable(c->ota_url, sizeof(c->ota_url)))
        return false;
    size_t password = strlen(c->password);
    return (!c->enabled || (c->ssid[0] && password >= 8u && password <= 63u)) && hostname(c->ntp) &&
           jelli_network_timezone(c->timezone) && c->sync_seconds >= 60u &&
           c->sync_seconds <= 86400u &&
           (!c->ota_url[0] || (!strncmp(c->ota_url, "https://", 8u) && c->ota_url[8] &&
                               !strchr(c->ota_url, '@') && !strchr(c->ota_url, ' ')));
}

static bool number(const char *value, uint32_t *out)
{
    uint32_t n = 0;
    if (!value[0])
        return false;
    for (size_t i = 0; value[i]; ++i) {
        if (value[i] < '0' || value[i] > '9' || n > (UINT32_MAX - 9u) / 10u)
            return false;
        n = n * 10u + (unsigned)(value[i] - '0');
    }
    *out = n;
    return true;
}

bool jelli_network_config_set(JelliNetworkConfig *c, const char *key, const char *value)
{
    char *target = NULL;
    size_t capacity = 0;
    if (!strcmp(key, "wifi.enabled")) {
        if (strcmp(value, "0") && strcmp(value, "1"))
            return false;
        c->enabled = value[0] == '1';
        return true;
    }
    if (!strcmp(key, "time.sync_seconds")) {
        uint32_t n;
        if (!number(value, &n) || n < 60u || n > 86400u)
            return false;
        c->sync_seconds = n;
        return true;
    }
    if (!strcmp(key, "wifi.ssid")) {
        target = c->ssid;
        capacity = sizeof(c->ssid);
    } else if (!strcmp(key, "wifi.password")) {
        target = c->password;
        capacity = sizeof(c->password);
    } else if (!strcmp(key, "time.server")) {
        target = c->ntp;
        capacity = sizeof(c->ntp);
    } else if (!strcmp(key, "time.timezone")) {
        target = c->timezone;
        capacity = sizeof(c->timezone);
    } else if (!strcmp(key, "ota.url")) {
        target = c->ota_url;
        capacity = sizeof(c->ota_url);
    }
    if (!target || !printable(value, capacity))
        return false;
    if (!strcmp(key, "time.server") && !hostname(value))
        return false;
    if (!strcmp(key, "time.timezone") && !jelli_network_timezone(value))
        return false;
    memset(target, 0, capacity);
    memcpy(target, value, strlen(value));
    return true;
}

void jelli_network_config_encode(const JelliNetworkConfig *c,
                                 uint8_t bytes[JELLI_NETWORK_CONFIG_BYTES])
{
    bytes[0] = 1u;
    bytes[1] = c->enabled ? 1u : 0u;
    for (unsigned i = 0; i < 4u; ++i)
        bytes[2u + i] = (uint8_t)(c->sync_seconds >> (i * 8u));
    memcpy(bytes + 6u, c->ssid, 33u);
    memcpy(bytes + 39u, c->password, 65u);
    memcpy(bytes + 104u, c->ntp, 64u);
    memcpy(bytes + 168u, c->timezone, 96u);
    memcpy(bytes + 264u, c->ota_url, 192u);
}

bool jelli_network_config_decode(JelliNetworkConfig *c, const uint8_t *bytes, size_t size)
{
    if (size != JELLI_NETWORK_CONFIG_BYTES || bytes[0] != 1u || bytes[1] > 1u)
        return false;
    JelliNetworkConfig candidate = {.enabled = bytes[1] != 0u};
    for (unsigned i = 0; i < 4u; ++i)
        candidate.sync_seconds |= (uint32_t)bytes[2u + i] << (i * 8u);
    memcpy(candidate.ssid, bytes + 6u, 33u);
    memcpy(candidate.password, bytes + 39u, 65u);
    memcpy(candidate.ntp, bytes + 104u, 64u);
    memcpy(candidate.timezone, bytes + 168u, 96u);
    memcpy(candidate.ota_url, bytes + 264u, 192u);
    if (!jelli_network_config_valid(&candidate))
        return false;
    *c = candidate;
    return true;
}

static int digit(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return -1;
}

bool jelli_network_unhex(const char *text, char *out, size_t capacity)
{
    if (!strcmp(text, "-")) {
        if (!capacity)
            return false;
        out[0] = '\0';
        return true;
    }
    size_t length = strlen(text);
    if (length % 2u || length / 2u >= capacity)
        return false;
    for (size_t i = 0; i < length; i += 2u) {
        int high = digit(text[i]), low = digit(text[i + 1u]);
        if (high < 0 || low < 0 || high * 16 + low < 32 || high * 16 + low > 126)
            return false;
        out[i / 2u] = (char)(high * 16 + low);
    }
    out[length / 2u] = '\0';
    return true;
}

int16_t jelli_network_offset(const struct tm *local, const struct tm *utc, int16_t fallback)
{
    int days = local->tm_yday - utc->tm_yday;
    if (days > 1)
        days = -1;
    if (days < -1)
        days = 1;
    int minutes = days * 1440 + (local->tm_hour - utc->tm_hour) * 60 + local->tm_min - utc->tm_min;
    return minutes >= -720 && minutes <= 840 ? (int16_t)minutes : fallback;
}
