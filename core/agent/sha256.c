#include "jelli/agent_sha256.h"
#include <string.h>

static const uint32_t ROUND[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

static uint32_t rotate(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void schedule(const uint8_t block[64], uint32_t w[64])
{
    for (size_t i = 0u; i < 16u; ++i)
        w[i] = (uint32_t)block[4u * i] << 24 | (uint32_t)block[4u * i + 1u] << 16 |
               (uint32_t)block[4u * i + 2u] << 8 | (uint32_t)block[4u * i + 3u];
    for (unsigned i = 16u; i < 64u; ++i) {
        uint32_t s0 = rotate(w[i - 15u], 7u) ^ rotate(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
        uint32_t s1 = rotate(w[i - 2u], 17u) ^ rotate(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
}

static void compress(JelliSha256 *sha, const uint8_t block[64])
{
    uint32_t w[64], v[8];
    schedule(block, w);
    memcpy(v, sha->h, sizeof(v));
    for (unsigned i = 0u; i < 64u; ++i) {
        uint32_t s1 = rotate(v[4], 6u) ^ rotate(v[4], 11u) ^ rotate(v[4], 25u);
        uint32_t choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t t1 = v[7] + s1 + choose + ROUND[i] + w[i];
        uint32_t s0 = rotate(v[0], 2u) ^ rotate(v[0], 13u) ^ rotate(v[0], 22u);
        uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        memmove(v + 1, v, 7u * sizeof(v[0]));
        v[4] += t1;
        v[0] = t1 + s0 + majority;
    }
    for (unsigned i = 0u; i < 8u; ++i)
        sha->h[i] += v[i];
}

void jelli_sha256_init(JelliSha256 *sha)
{
    static const uint32_t start[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(sha->h, start, sizeof(start));
    sha->length = 0u;
    sha->used = 0u;
}

void jelli_sha256_update(JelliSha256 *sha, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    sha->length += size;
    while (size > 0u) {
        size_t take = sizeof(sha->block) - sha->used;
        take = take < size ? take : size;
        memcpy(sha->block + sha->used, bytes, take);
        sha->used += take;
        bytes += take;
        size -= take;
        if (sha->used == sizeof(sha->block)) {
            compress(sha, sha->block);
            sha->used = 0u;
        }
    }
}

void jelli_sha256_final(JelliSha256 *sha, uint8_t digest[JELLI_SHA256_SIZE])
{
    uint64_t bits = sha->length * 8u;
    uint8_t pad[72] = {0x80u};
    /* Pad to 56 bytes past a block boundary, then append the length in bits. */
    size_t padding = sha->used < 56u ? 56u - sha->used : 120u - sha->used;
    for (unsigned i = 0u; i < 8u; ++i)
        pad[padding + i] = (uint8_t)(bits >> (56u - 8u * i));
    uint64_t length = sha->length;
    jelli_sha256_update(sha, pad, padding + 8u);
    sha->length = length;
    for (size_t i = 0u; i < 8u; ++i) {
        digest[4u * i] = (uint8_t)(sha->h[i] >> 24);
        digest[4u * i + 1u] = (uint8_t)(sha->h[i] >> 16);
        digest[4u * i + 2u] = (uint8_t)(sha->h[i] >> 8);
        digest[4u * i + 3u] = (uint8_t)sha->h[i];
    }
}
