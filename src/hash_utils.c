#include "hash_utils.h"

#include <string.h>

#include <openssl/md5.h>
#include <openssl/sha.h>

const char *hash_backend_name(void) {
    return "OpenSSL (libcrypto)";
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

void hash_compute(HashAlgo algo, const char *data, size_t len, uint8_t *out_digest) {
    if (algo == HASH_ALGO_SHA1) {
        SHA1((const unsigned char *)data, len, out_digest);
    } else {
        MD5((const unsigned char *)data, len, out_digest);
    }
}
