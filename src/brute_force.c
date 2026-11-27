#include "brute_force.h"
#include "hash_utils.h"

#include <limits.h>
#include <omp.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* ============================================================================
 * [KHUNG CODE / FRAMEWORK - CHAY DUOC NGAY]
 * File nay la khung logic mau cho phan Brute Force + OpenMP.
 *
 * NOTE CHO QUANG ANH:
 *   - Hay kiem tra lai cac ham ben duoi theo dung tai lieu thiet ke:
 *       1. index_to_password()       (Index Mapping - trang 3-4)
 *       2. hash_password()           (Tinh hash MD5/SHA1 - trang 6)
 *       3. check_password()          (So sanh hash - trang 6)
 *       4. save_result()             (Luu ket qua an toan trong critical - trang 6)
 *       5. 4 che do chay: Sequential / Static / Dynamic / Guided (trang 2, 5)
 *       6. brute_recommend_threads() (Heuristic tu dong chon so luong toi uu)
 *   - Quang Anh co the toi uu them trong file nay ma KHONG can sua code UI.
 * ============================================================================ */

#define ATTEMPTS_FLUSH_MASK 0xFFFULL 
#define TRACE_SAMPLE_DT     0.015    

const char *exec_mode_name(ExecMode mode) {
    switch (mode) {
        case EXEC_MODE_SEQUENTIAL: return "Sequential";
        case EXEC_MODE_STATIC:     return "Static";
        case EXEC_MODE_DYNAMIC:    return "Dynamic";
        case EXEC_MODE_GUIDED:     return "Guided";
        case EXEC_MODE_BENCHMARK:  return "Run All Modes";
        default:                   return "Unknown";
    }
}

// Auto Threads

int brute_recommend_threads(int charset_len, int max_length) {
    int logical  = omp_get_max_threads();

    unsigned long long total_space = 0;
    int overflowed = 0;
    if (charset_len > 0 && max_length > 0) {
        for (int L = 1; L <= max_length; L++) {
            unsigned long long p = safe_power_u64(charset_len, L);
            if (p == 0 || total_space > ULLONG_MAX - p) {
                overflowed = 1;
                break;
            }
            total_space += p;
        }
    }

    // Partition via searching space
    int rec = logical - 2; // MAX = thread_nums - 2
    if (!overflowed) {
        if (total_space < 2000ULL) { // Searching space < 2000
            rec = 1;
        } else if (total_space < 20000ULL) { // Searching space < 20000
            rec = 2;
        } else if (total_space < 100000ULL) { // Searching space < 100000
            rec = 4;
        }
    }

    if (rec < 1) rec = 1;
    if (rec > logical) rec = logical;
    return rec;
}

// Index Mapping

void index_to_password(
    unsigned long long index,
    int length,
    const char *charset,
    int K,
    char *password
) {
    for (int i = length - 1; i >= 0; i--) {
        password[i] = charset[index % (unsigned long long)K];
        index /= (unsigned long long)K;
    }
    password[length] = '\0';
}

unsigned long long safe_power_u64(int base, int exp) {
    if (base <= 0 || exp < 0) return 0;
    unsigned long long result = 1ULL;
    unsigned long long b = (unsigned long long)base;
    for (int i = 0; i < exp; i++) {
        if (result > (unsigned long long)LLONG_MAX / b) {
            return 0;
        }
        result *= b;
    }
    return result;
}

static inline void hash_password(
    HashAlgo algo,
    const char *password,
    int length,
    uint8_t *out_digest
) {
    hash_compute(algo, password, (size_t)length, out_digest);
}

static inline int check_password(
    const uint8_t *digest,
    const uint8_t *target_bytes,
    int byte_len
) {
    return (memcmp(digest, target_bytes, (size_t)byte_len) == 0);
}

static void save_result(
    BruteResult *result,
    atomic_int *found_flag,
    const char *password,
    int length
) {
    #pragma omp critical(brute_save_result) // ensure only 1 thread can write result
    {
        if (!atomic_load_explicit(found_flag, memory_order_relaxed)) {
            result->found = 1;
            result->found_length = length;
            memcpy(result->password, password, (size_t)length + 1);
            atomic_store_explicit(found_flag, 1, memory_order_relaxed);
        }
    }
}

static inline int should_stop_now(const atomic_int *found_flag, const BruteProgress *progress) {
    return atomic_load_explicit(found_flag, memory_order_relaxed) ||
           (progress && atomic_load_explicit(&progress->stop_requested, memory_order_relaxed));
}

// Logging & Debugging
static inline void flush_attempts(
    unsigned long long *local_attempts,
    atomic_ullong *total_attempts,
    BruteProgress *progress
) {
    if (*local_attempts == 0) return;

    unsigned long long delta = *local_attempts;
    *local_attempts = 0;

    atomic_fetch_add_explicit(total_attempts, delta, memory_order_relaxed);

    if (progress) {
        atomic_fetch_add_explicit(&progress->attempts, delta, memory_order_relaxed);
    }
}

static inline void trace_append(ModeTrace *trace, double time_sec, unsigned long long attempts, int force_final) {
    if (!trace) return;
    if (time_sec < 0.0) time_sec = 0.0;

    int limit = force_final ? MAX_TRACE_SAMPLES : (MAX_TRACE_SAMPLES - 1);
    if (trace->count >= limit) {
        if (force_final && trace->count > 0) {
            trace->samples[trace->count - 1].time_sec = time_sec;
            trace->samples[trace->count - 1].attempts = attempts;
        }
        return;
    }

    if (trace->count > 0) {
        TraceSample *prev = &trace->samples[trace->count - 1];
        if (!force_final && prev->attempts == attempts && (time_sec - prev->time_sec) < 1e-6) {
            return;
        }
        if (force_final && prev->attempts == attempts && (time_sec - prev->time_sec) < 1e-7) {
            prev->time_sec = time_sec;
            return;
        }
    }

    trace->samples[trace->count].time_sec = time_sec;
    trace->samples[trace->count].attempts = attempts;
    trace->count++;
}

static inline void trace_maybe_sample(
    ModeTrace *trace,
    double t_start,
    double *last_sample_t,
    const atomic_ullong *total_attempts
) {
    double now = omp_get_wtime();
    if (now - *last_sample_t >= TRACE_SAMPLE_DT) {
        unsigned long long snap = atomic_load_explicit(total_attempts, memory_order_relaxed);
        trace_append(trace, now - t_start, snap, 0);
        *last_sample_t = now;
    }
}

// Sequential
static void run_length_sequential(
    const BruteConfig *config,
    int L,
    long long total,
    double t_start,
    double *last_sample_t,
    BruteProgress *progress,
    BruteResult *result,
    atomic_int *found_flag,
    atomic_ullong *total_attempts
) {
    char password[MAX_PASSWORD_LEN + 1];
    uint8_t digest[MAX_DIGEST_BYTES];
    unsigned long long local_attempts = 0;

    for (long long idx = 0; idx < total; idx++) {
        if ((local_attempts & ATTEMPTS_FLUSH_MASK) == 0) {
            flush_attempts(&local_attempts, total_attempts, progress);
            trace_maybe_sample(&result->trace, t_start, last_sample_t, total_attempts);
            if (should_stop_now(found_flag, progress)) {
                break;
            }
        }

        index_to_password((unsigned long long)idx, L, config->charset, config->charset_len, password);
        hash_password(config->algo, password, L, digest);
        local_attempts++;

        if (check_password(digest, config->target_hash_bytes, config->hash_byte_len)) {
            save_result(result, found_flag, password, L);
            break;
        }
    }

    flush_attempts(&local_attempts, total_attempts, progress);
}

static void run_length_parallel(
    const BruteConfig *config,
    ExecMode mode,
    int L,
    long long total,
    double t_start,
    double *last_sample_t,
    BruteProgress *progress,
    BruteResult *result,
    atomic_int *found_flag,
    atomic_ullong *total_attempts
) {
    int threads = (config->num_threads > 0) ? config->num_threads : 1;
    int chunk   = (config->chunk_size > 0)  ? config->chunk_size  : DEFAULT_CHUNK_SIZE;

    #pragma omp parallel num_threads(threads)
    {
        char password[MAX_PASSWORD_LEN + 1];
        uint8_t digest[MAX_DIGEST_BYTES];
        unsigned long long local_attempts = 0;
        int stop_local = 0;
        int tid = omp_get_thread_num();

        if (mode == EXEC_MODE_STATIC) {
            // Static
            #pragma omp for schedule(static)
            for (long long idx = 0; idx < total; idx++) {
                if (stop_local) continue;
                if ((local_attempts & ATTEMPTS_FLUSH_MASK) == 0) {
                    flush_attempts(&local_attempts, total_attempts, progress);
                    if (tid == 0) {
                        trace_maybe_sample(&result->trace, t_start, last_sample_t, total_attempts);
                    }
                    if (should_stop_now(found_flag, progress)) {
                        stop_local = 1;
                        continue;
                    }
                }

                index_to_password((unsigned long long)idx, L, config->charset, config->charset_len, password);
                hash_password(config->algo, password, L, digest);
                local_attempts++;

                if (check_password(digest, config->target_hash_bytes, config->hash_byte_len)) {
                    save_result(result, found_flag, password, L);
                    stop_local = 1;
                }
            }
        } else if (mode == EXEC_MODE_DYNAMIC) {
            // Dynamic
            #pragma omp for schedule(dynamic, chunk)
            for (long long idx = 0; idx < total; idx++) {
                if (stop_local) continue;
                if ((local_attempts & ATTEMPTS_FLUSH_MASK) == 0) {
                    flush_attempts(&local_attempts, total_attempts, progress);
                    if (tid == 0) {
                        trace_maybe_sample(&result->trace, t_start, last_sample_t, total_attempts);
                    }
                    if (should_stop_now(found_flag, progress)) {
                        stop_local = 1;
                        continue;
                    }
                }

                index_to_password((unsigned long long)idx, L, config->charset, config->charset_len, password);
                hash_password(config->algo, password, L, digest);
                local_attempts++;

                if (check_password(digest, config->target_hash_bytes, config->hash_byte_len)) {
                    save_result(result, found_flag, password, L);
                    stop_local = 1;
                }
            }
        } else { 
            // Guided
            #pragma omp for schedule(guided, chunk)
            for (long long idx = 0; idx < total; idx++) {
                if (stop_local) continue;
                if ((local_attempts & ATTEMPTS_FLUSH_MASK) == 0) {
                    flush_attempts(&local_attempts, total_attempts, progress);
                    if (tid == 0) {
                        trace_maybe_sample(&result->trace, t_start, last_sample_t, total_attempts);
                    }
                    if (should_stop_now(found_flag, progress)) {
                        stop_local = 1;
                        continue;
                    }
                }

                index_to_password((unsigned long long)idx, L, config->charset, config->charset_len, password);
                hash_password(config->algo, password, L, digest);
                local_attempts++;

                if (check_password(digest, config->target_hash_bytes, config->hash_byte_len)) {
                    save_result(result, found_flag, password, L);
                    stop_local = 1;
                }
            }
        }

        flush_attempts(&local_attempts, total_attempts, progress);
    }
}
// Main function
void brute_force_run_mode(
    const BruteConfig *config,
    ExecMode mode,
    BruteProgress *progress,
    BruteResult *result
) {
    memset(result, 0, sizeof(*result));
    result->mode = mode;

    if (!config || config->charset_len <= 0 || config->max_length <= 0) {
        return;
    }

    atomic_int found_flag = 0;
    atomic_ullong total_attempts = 0;
    double t_start = omp_get_wtime();
    double last_sample_t = t_start;

    /* Diem khoi dau (0s, 0 attempts) cho do thi */
    trace_append(&result->trace, 0.0, 0, 0);

    if (progress) {
        atomic_store_explicit(&progress->start_time, t_start, memory_order_relaxed);
        atomic_store_explicit(&progress->attempts, 0, memory_order_relaxed);
        atomic_store_explicit(&progress->current_length, 1, memory_order_relaxed);
        atomic_store_explicit(&progress->active_mode, (int)mode, memory_order_relaxed);
    }

    // Length loop for
    for (int L = 1; L <= config->max_length; L++) {
        if (should_stop_now(&found_flag, progress)) {
            break;
        }

        if (progress) {
            atomic_store_explicit(&progress->current_length, L, memory_order_relaxed);
        }

        unsigned long long total_u64 = safe_power_u64(config->charset_len, L);
        if (total_u64 == 0) {
            break;
        }

        long long total = (long long)total_u64;

        if (mode == EXEC_MODE_SEQUENTIAL) {
            run_length_sequential(config, L, total, t_start, &last_sample_t,
                                  progress, result, &found_flag, &total_attempts);
        } else {
            run_length_parallel(config, mode, L, total, t_start, &last_sample_t,
                                progress, result, &found_flag, &total_attempts);
        }

        double now = omp_get_wtime();
        trace_append(&result->trace, now - t_start,
                     atomic_load_explicit(&total_attempts, memory_order_relaxed), 0);
        last_sample_t = now;
    }

    double t_end = omp_get_wtime();
    result->elapsed_sec = t_end - t_start;
    result->attempts    = atomic_load_explicit(&total_attempts, memory_order_relaxed);
    result->stopped     = !result->found && progress &&
                          atomic_load_explicit(&progress->stop_requested, memory_order_relaxed);

    trace_append(&result->trace, result->elapsed_sec, result->attempts, 1);
}
