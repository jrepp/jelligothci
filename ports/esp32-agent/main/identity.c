#include "identity.h"
#include "settings.h"
#include "jelli/agent_sha256.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include <stdio.h>
#include <string.h>

enum { KEY_BYTES = 200, CERT_BYTES = 640 };

static const char *TAG = "agent_identity";
static uint8_t key_der[KEY_BYTES], cert_der[CERT_BYTES];
static size_t key_size, cert_size;
static char fingerprint[65], name[32];

static void set_name(void)
{
    uint8_t mac[6] = {0};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    (void)snprintf(name, sizeof(name), "Loki Agent %02X%02X%02X", mac[3], mac[4], mac[5]);
}

static void set_fingerprint(void)
{
    JelliSha256 sha;
    uint8_t digest[JELLI_SHA256_SIZE];
    jelli_sha256_init(&sha);
    jelli_sha256_update(&sha, cert_der, cert_size);
    jelli_sha256_final(&sha, digest);
    for (size_t i = 0u; i < sizeof(digest); ++i)
        (void)snprintf(fingerprint + 2u * i, 3u, "%02x", (unsigned)digest[i]);
}

/* Blinding for key checks; the hardware generator is enough for that. */
static int hardware_random(void *unused, unsigned char *out, size_t size)
{
    (void)unused;
    esp_fill_random(out, size);
    return 0;
}

/* A stored identity counts only if both halves parse and the key matches the
 * certificate. */
static bool parses(void)
{
    mbedtls_pk_context pk;
    mbedtls_x509_crt crt;
    mbedtls_pk_init(&pk);
    mbedtls_x509_crt_init(&crt);
    bool ok = mbedtls_pk_parse_key(&pk, key_der, key_size, NULL, 0u, hardware_random, NULL) == 0 &&
              mbedtls_x509_crt_parse_der(&crt, cert_der, cert_size) == 0 &&
              mbedtls_pk_check_pair(&crt.pk, &pk, hardware_random, NULL) == 0;
    mbedtls_x509_crt_free(&crt);
    mbedtls_pk_free(&pk);
    return ok;
}

static bool load(void)
{
    key_size = sizeof(key_der);
    cert_size = sizeof(cert_der);
    return jelli_agent_settings_get("tls_key", key_der, &key_size) &&
           jelli_agent_settings_get("tls_cert", cert_der, &cert_size) && key_size && cert_size &&
           parses();
}

/* mbedTLS writes DER at the end of the buffer it is given; this keeps the start. */
static bool keep_tail(const uint8_t *buffer, size_t capacity, int written, uint8_t *out,
                      size_t out_capacity, size_t *size)
{
    if (written <= 0 || (size_t)written > out_capacity)
        return false;
    memcpy(out, buffer + capacity - (size_t)written, (size_t)written);
    *size = (size_t)written;
    return true;
}

static bool write_certificate(mbedtls_pk_context *pk, mbedtls_ctr_drbg_context *drbg)
{
    static uint8_t buffer[1024];
    char subject[48];
    uint8_t serial[16];
    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);
    (void)snprintf(subject, sizeof(subject), "CN=%s", name);
    bool ok = mbedtls_ctr_drbg_random(drbg, serial, sizeof(serial)) == 0;
    serial[0] = (uint8_t)((serial[0] & 0x7Fu) | 0x40u); /* Positive, full length. */
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, pk);
    mbedtls_x509write_crt_set_issuer_key(&crt, pk);
    ok = ok && mbedtls_x509write_crt_set_subject_name(&crt, subject) == 0 &&
         mbedtls_x509write_crt_set_issuer_name(&crt, subject) == 0 &&
         mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial)) == 0 &&
         /* The bridge pins this certificate, so its dates are never checked. */
         mbedtls_x509write_crt_set_validity(&crt, "20260101000000", "21260101000000") == 0 &&
         mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1) == 0;
    int written =
        ok ? mbedtls_x509write_crt_der(&crt, buffer, sizeof(buffer), mbedtls_ctr_drbg_random, drbg)
           : -1;
    mbedtls_x509write_crt_free(&crt);
    return keep_tail(buffer, sizeof(buffer), written, cert_der, sizeof(cert_der), &cert_size);
}

static bool generate(void)
{
    static const char personal[] = "loki-agent-identity";
    static uint8_t buffer[KEY_BYTES];
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_context pk;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_pk_init(&pk);
    bool ok =
        mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)personal, sizeof(personal) - 1u) == 0 &&
        mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) == 0 &&
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), mbedtls_ctr_drbg_random,
                            &drbg) == 0 &&
        write_certificate(&pk, &drbg) &&
        keep_tail(buffer, sizeof(buffer), mbedtls_pk_write_key_der(&pk, buffer, sizeof(buffer)),
                  key_der, sizeof(key_der), &key_size);
    memset(buffer, 0, sizeof(buffer)); /* The key's only other copy is key_der. */
    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ok && jelli_agent_settings_set("tls_key", key_der, key_size) &&
           jelli_agent_settings_set("tls_cert", cert_der, cert_size);
}

bool jelli_agent_identity_start(bool radio_on)
{
    set_name();
    if (!load()) {
        if (!radio_on) {
            ESP_LOGW(TAG, "No TLS identity yet; one is made when Wi-Fi starts");
            key_size = cert_size = 0u;
            return false;
        }
        ESP_LOGI(TAG, "Making this board's TLS identity");
        if (!generate()) {
            ESP_LOGE(TAG, "Could not make a TLS identity");
            key_size = cert_size = 0u;
            return false;
        }
    }
    set_fingerprint();
    if (!parses()) {
        ESP_LOGE(TAG, "The new TLS identity does not check out");
        key_size = cert_size = 0u;
        return false;
    }
    ESP_LOGI(TAG, "%s, certificate %.16s...", name, fingerprint);
    return true;
}

const uint8_t *jelli_agent_identity_cert(size_t *size)
{
    *size = cert_size;
    return cert_size ? cert_der : NULL;
}

const uint8_t *jelli_agent_identity_key(size_t *size)
{
    *size = key_size;
    return key_size ? key_der : NULL;
}

const char *jelli_agent_identity_fingerprint(void) { return fingerprint; }

const char *jelli_agent_identity_name(void) { return name; }
