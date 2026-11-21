#include "hash_utils.h"

#include <ctype.h>
#include <string.h>

#ifdef USE_OPENSSL
#include <openssl/md5.h>
#include <openssl/sha.h>
#endif

/* ============================================================================
 * Backend thong tin & Tien ich chuyen doi Hex <-> Bytes
 * ============================================================================ */

const char *hash_backend_name(void) {
#ifdef USE_OPENSSL
    return "OpenSSL (libcrypto)";
#else
    return "Built-in Fallback (Build with USE_OPENSSL=1 for OpenSSL)";
#endif
}

int hash_digest_length(HashAlgo algo) {
    return (algo == HASH_ALGO_SHA1) ? SHA1_DIGEST_BYTES : MD5_DIGEST_BYTES;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int hash_hex_to_bytes(const char *hex_str, uint8_t *out_bytes, int expected_bytes) {
    if (!hex_str || !out_bytes || expected_bytes <= 0) {
        return 0;
    }

    size_t len = strlen(hex_str);
    if ((int)len != expected_bytes * 2) {
        return 0;
    }

    for (int i = 0; i < expected_bytes; i++) {
        int hi = hex_nibble(hex_str[i * 2]);
        int lo = hex_nibble(hex_str[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        out_bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

void hash_bytes_to_hex(const uint8_t *bytes, int byte_len, char *out_hex) {
    static const char hex_digits[] = "0123456789abcdef";
    for (int i = 0; i < byte_len; i++) {
        out_hex[i * 2]     = hex_digits[(bytes[i] >> 4) & 0x0F];
        out_hex[i * 2 + 1] = hex_digits[bytes[i] & 0x0F];
    }
    out_hex[byte_len * 2] = '\0';
}

/* ============================================================================
 * Nhanh 1: Su dung OpenSSL (dung yeu cau de bai khi may da cai OpenSSL)
 * ============================================================================ */
#ifdef USE_OPENSSL

void hash_compute(HashAlgo algo, const char *data, size_t len, uint8_t *out_digest) {
    if (algo == HASH_ALGO_SHA1) {
        SHA1((const unsigned char *)data, len, out_digest);
    } else {
        MD5((const unsigned char *)data, len, out_digest);
    }
}

/* ============================================================================
 * Nhanh 2: [FALLBACK] Cai dat MD5 (RFC 1321) & SHA1 (FIPS 180-4) thuan C
 * - Khong dung bien tinh/toan cuc -> an toan tuyet doi khi chay da luong OpenMP.
 * - Chi dung de chay test nhanh khi may chua cai OpenSSL; khi nop bai/build voi
 *   OpenSSL chi can chay: mingw32-make USE_OPENSSL=1
 * ============================================================================ */
#else

static inline uint32_t rotl32(uint32_t x, unsigned int n) {
    return (x << n) | (x >> (32U - n));
}

/* ---------- MD5 (RFC 1321) ---------- */
static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    static const uint32_t k[64] = {
        0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
        0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
        0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
        0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
        0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
        0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
        0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
        0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
        0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
        0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
        0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
        0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
        0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
        0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
        0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
        0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U
    };
    static const unsigned int s[64] = {
        7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
        5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
        4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
        6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
    };

    uint32_t m[16];
    for (int i = 0; i < 16; i++) {
        m[i] = ((uint32_t)block[i * 4]) |
               ((uint32_t)block[i * 4 + 1] << 8) |
               ((uint32_t)block[i * 4 + 2] << 16) |
               ((uint32_t)block[i * 4 + 3] << 24);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];

    for (unsigned int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | ((~d) & c);
            g = (5U * i + 1U) & 15U;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3U * i + 5U) & 15U;
        } else {
            f = c ^ (b | (~d));
            g = (7U * i) & 15U;
        }
        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rotl32(a + f + k[i] + m[g], s[i]);
        a = tmp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_compute_fallback(const uint8_t *data, size_t len, uint8_t out[16]) {
    uint32_t state[4] = {
        0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U
    };
    uint8_t block[64];
    size_t offset = 0;

    while (len - offset >= 64) {
        md5_transform(state, data + offset);
        offset += 64;
    }

    size_t rem = len - offset;
    memset(block, 0, sizeof(block));
    if (rem > 0) {
        memcpy(block, data + offset, rem);
    }
    block[rem] = 0x80U;

    if (rem >= 56) {
        md5_transform(state, block);
        memset(block, 0, sizeof(block));
    }

    uint64_t bit_len = (uint64_t)len * 8ULL;
    for (int i = 0; i < 8; i++) {
        block[56 + i] = (uint8_t)((bit_len >> (i * 8)) & 0xFFU);
    }
    md5_transform(state, block);

    for (int i = 0; i < 4; i++) {
        out[i * 4]     = (uint8_t)(state[i] & 0xFFU);
        out[i * 4 + 1] = (uint8_t)((state[i] >> 8) & 0xFFU);
        out[i * 4 + 2] = (uint8_t)((state[i] >> 16) & 0xFFU);
        out[i * 4 + 3] = (uint8_t)((state[i] >> 24) & 0xFFU);
    }
}

/* ---------- SHA-1 (FIPS 180-4) ---------- */
static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void sha1_compute_fallback(const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t state[5] = {
        0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U
    };
    uint8_t block[64];
    size_t offset = 0;

    while (len - offset >= 64) {
        sha1_transform(state, data + offset);
        offset += 64;
    }

    size_t rem = len - offset;
    memset(block, 0, sizeof(block));
    if (rem > 0) {
        memcpy(block, data + offset, rem);
    }
    block[rem] = 0x80U;

    if (rem >= 56) {
        sha1_transform(state, block);
        memset(block, 0, sizeof(block));
    }

    uint64_t bit_len = (uint64_t)len * 8ULL;
    for (int i = 0; i < 8; i++) {
        block[63 - i] = (uint8_t)((bit_len >> (i * 8)) & 0xFFU);
    }
    sha1_transform(state, block);

    for (int i = 0; i < 5; i++) {
        out[i * 4]     = (uint8_t)((state[i] >> 24) & 0xFFU);
        out[i * 4 + 1] = (uint8_t)((state[i] >> 16) & 0xFFU);
        out[i * 4 + 2] = (uint8_t)((state[i] >> 8) & 0xFFU);
        out[i * 4 + 3] = (uint8_t)(state[i] & 0xFFU);
    }
}

void hash_compute(HashAlgo algo, const char *data, size_t len, uint8_t *out_digest) {
    if (algo == HASH_ALGO_SHA1) {
        sha1_compute_fallback((const uint8_t *)data, len, out_digest);
    } else {
        md5_compute_fallback((const uint8_t *)data, len, out_digest);
    }
}

#endif /* USE_OPENSSL */
