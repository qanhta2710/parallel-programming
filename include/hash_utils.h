#ifndef HASH_UTILS_H
#define HASH_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include "config.h"

/* ============================================================================
 * Module Hash (MD5 / SHA1)
 * - Khi bien dich voi -DUSE_OPENSSL: goi truc tiep MD5()/SHA1() cua OpenSSL.
 * - Khi bien dich mac dinh (chua cai OpenSSL): dung fallback thuan C tuong
 *   thich 100% ket qua (RFC 1321 / FIPS 180-4) de build va test ngay lap tuc.
 * ============================================================================ */

/* Tra ve ten backend hash dang su dung ("OpenSSL" hoac "Built-in Fallback") */
const char *hash_backend_name(void);

/* Do dai digest theo byte: MD5 = 16, SHA1 = 20 */
int hash_digest_length(HashAlgo algo);

/* Tinh hash cua chuoi data (do dai len) va ghi mang byte ra out_digest */
void hash_compute(HashAlgo algo, const char *data, size_t len, uint8_t *out_digest);

/* Chuyen chuoi hex (VD: "90015098...") sang mang byte de so sanh nhanh bang memcmp.
 * Tra ve 1 neu hop le, 0 neu sai dinh dang hoac sai do dai. */
int hash_hex_to_bytes(const char *hex_str, uint8_t *out_bytes, int expected_bytes);

/* Chuyen mang byte sang chuoi hex viet thuong (huu ich khi debug/test) */
void hash_bytes_to_hex(const uint8_t *bytes, int byte_len, char *out_hex);

#endif /* HASH_UTILS_H */
