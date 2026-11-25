#ifndef HASH_UTILS_H
#define HASH_UTILS_H

#include <stdint.h>
#include "config.h"

int hash_digest_length(HashAlgo algo);

void hash_compute(HashAlgo algo, const char *data, size_t len, uint8_t *out_digest);

int hash_hex_to_bytes(const char *hex_str, uint8_t *out_bytes, int expected_bytes);

void hash_bytes_to_hex(const uint8_t *bytes, int byte_len, char *out_hex);

#endif 