#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "brute_force.h"
#include "config.h"
#include "hash_utils.h"

/* ============================================================================
 * Test nhanh phan core (Hash + Index Mapping + 4 che do OpenMP)
 * Chay bang: mingw32-make test
 * ============================================================================ */

static void test_hashes(void) {
    uint8_t digest[MAX_DIGEST_BYTES];
    char hex[MAX_HASH_HEX_LEN];

    hash_compute(HASH_ALGO_MD5, "abc", 3, digest);
    hash_bytes_to_hex(digest, MD5_DIGEST_BYTES, hex);
    assert(strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0);

    hash_compute(HASH_ALGO_MD5, "", 0, digest);
    hash_bytes_to_hex(digest, MD5_DIGEST_BYTES, hex);
    assert(strcmp(hex, "d41d8cd98f00b204e9800998ecf8427e") == 0);

    hash_compute(HASH_ALGO_SHA1, "abc", 3, digest);
    hash_bytes_to_hex(digest, SHA1_DIGEST_BYTES, hex);
    assert(strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);

    hash_compute(HASH_ALGO_SHA1, "", 0, digest);
    hash_bytes_to_hex(digest, SHA1_DIGEST_BYTES, hex);
    assert(strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);

    const char *long_str = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    hash_compute(HASH_ALGO_SHA1, long_str, strlen(long_str), digest);
    hash_bytes_to_hex(digest, SHA1_DIGEST_BYTES, hex);
    assert(strcmp(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1") == 0);

    printf("[PASS] Hash MD5/SHA1 (%s)\n", hash_backend_name());
}

static void test_index_mapping(void) {
    char pw[MAX_PASSWORD_LEN + 1];

    /* Vi du trang 3 trong tai lieu: C = {a,b,c}, K = 3, L = 3, index = 19 -> "cab" */
    index_to_password(19, 3, "abc", 3, pw);
    assert(strcmp(pw, "cab") == 0);

    index_to_password(0, 1, APP_DEFAULT_CHARSET, 26, pw);
    assert(strcmp(pw, "a") == 0);

    index_to_password(25, 1, APP_DEFAULT_CHARSET, 26, pw);
    assert(strcmp(pw, "z") == 0);

    index_to_password(0, 2, APP_DEFAULT_CHARSET, 26, pw);
    assert(strcmp(pw, "aa") == 0);

    printf("[PASS] Index Mapping\n");
}

static void test_all_modes(void) {
    BruteConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.algo          = HASH_ALGO_MD5;
    cfg.hash_byte_len = MD5_DIGEST_BYTES;
    strcpy(cfg.charset, APP_DEFAULT_CHARSET);
    cfg.charset_len   = (int)strlen(cfg.charset);
    cfg.max_length    = 4;
    cfg.num_threads   = 4;
    cfg.chunk_size    = 1000;

    /* Test voi mat khau "zzza" (4 ky tu) de thay ro toc do song song */
    uint8_t target[MD5_DIGEST_BYTES];
    hash_compute(HASH_ALGO_MD5, "zzza", 4, target);
    memcpy(cfg.target_hash_bytes, target, MD5_DIGEST_BYTES);
    hash_bytes_to_hex(target, MD5_DIGEST_BYTES, cfg.target_hash_hex);

    const ExecMode modes[4] = {
        EXEC_MODE_SEQUENTIAL,
        EXEC_MODE_STATIC,
        EXEC_MODE_DYNAMIC,
        EXEC_MODE_GUIDED
    };

    double t_seq = 0.0;
    printf("Physical cores = %d | Recommended threads (26, len=6) = %d | (26, len=2) = %d\n",
           brute_get_physical_cores(),
           brute_recommend_threads(26, 6),
           brute_recommend_threads(26, 2));
    assert(brute_recommend_threads(26, 2) == 1);
    assert(brute_recommend_threads(26, 6) >= 1);

    printf("\nBenchmark test target = \"zzza\" (MD5 = %s, threads = %d):\n",
           cfg.target_hash_hex, cfg.num_threads);
    printf("%-12s | %-10s | %-10s | %-10s | %-10s\n",
           "Mode", "Found", "Time (s)", "Speedup", "Efficiency");
    printf("-------------+------------+------------+------------+-----------\n");

    for (int i = 0; i < 4; i++) {
        BruteProgress prog;
        BruteResult res;
        memset(&prog, 0, sizeof(prog));

        brute_force_run_mode(&cfg, modes[i], &prog, &res);
        assert(res.found == 1);
        assert(strcmp(res.password, "zzza") == 0);
        assert(res.trace.count >= 2);

        if (i == 0) {
            t_seq = res.elapsed_sec;
            printf("%-12s | %-10s | %10.4f | %9.2fx | %9.1f%%\n",
                   exec_mode_name(modes[i]), res.password, res.elapsed_sec, 1.0, 100.0);
        } else {
            double spd = (res.elapsed_sec > 1e-9) ? (t_seq / res.elapsed_sec) : 0.0;
            double eff = (spd / (double)cfg.num_threads) * 100.0;
            printf("%-12s | %-10s | %10.4f | %9.2fx | %9.1f%%\n",
                   exec_mode_name(modes[i]), res.password, res.elapsed_sec, spd, eff);
        }
    }

    printf("\n[PASS] All 4 OpenMP modes verified!\n");
}

int main(void) {
    test_hashes();
    test_index_mapping();
    test_all_modes();
    return 0;
}
