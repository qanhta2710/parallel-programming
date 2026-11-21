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

#define ATTEMPTS_FLUSH_MASK 0xFFFULL /* Flush attempts moi 4096 lan thu de giam atomic overhead */
#define TRACE_SAMPLE_DT     0.015    /* Ghi diem do thi moi ~15ms (chi thread 0 thuc hien) */

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

/* ----------------------------------------------------------------------------
 * Tu dong nhan dien nhan vat ly CPU va de xuat so luong toi uu (Auto Threads)
 * ---------------------------------------------------------------------------- */
int brute_get_physical_cores(void) {
    int logical = omp_get_max_threads();
    if (logical < 1) logical = 1;

#ifdef _WIN32
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    if (len > 0) {
        BYTE *buf = (BYTE *)malloc(len);
        if (buf) {
            if (GetLogicalProcessorInformationEx(
                    RelationProcessorCore,
                    (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf,
                    &len)) {
                int cores = 0;
                DWORD offset = 0;
                while (offset < len) {
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
                        (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf + offset);
                    if (info->Relationship == RelationProcessorCore) {
                        cores++;
                    }
                    if (info->Size == 0) break;
                    offset += info->Size;
                }
                free(buf);
                if (cores >= 1) {
                    return cores;
                }
            } else {
                free(buf);
            }
        }
    }
#endif

    return logical;
}

int brute_recommend_threads(int charset_len, int max_length) {
    int physical = brute_get_physical_cores();
    int logical  = omp_get_max_threads();
    if (physical < 1) physical = 1;
    if (logical < physical) logical = physical;

    /* Tinh tong khong gian tim kiem sum_{L=1..max_length} K^L */
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

    /* Heuristic toi uu cho tac vu CPU-bound (MD5/SHA1):
     * - Khong gian nho (< 2,000): 1 luong (tranh ton chi phi tao luong)
     * - Khong gian trung binh nho (< 20,000): toi da 2 luong
     * - Khong gian vua (< 100,000): toi da 4 luong (hoac physical cores)
     * - Khong gian lon: dung dung so nhan vat ly (physical cores) de dat
     *   Speedup cao nhat ma van giu Efficiency tot va giao dien UI muot. */
    int rec = physical;
    if (!overflowed) {
        if (total_space < 2000ULL) {
            rec = 1;
        } else if (total_space < 20000ULL) {
            rec = (physical < 2) ? physical : 2;
        } else if (total_space < 100000ULL) {
            rec = (physical < 4) ? physical : 4;
        }
    }

    if (rec < 1) rec = 1;
    if (rec > logical) rec = logical;
    return rec;
}

/* ----------------------------------------------------------------------------
 * 1. Index Mapping: chuyen so nguyen index -> chuoi mat khau do dai `length`
 * ---------------------------------------------------------------------------- */
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

/* Tinh K^L co kiem tra tran so so voi LLONG_MAX (de dung an toan trong OpenMP for) */
unsigned long long safe_power_u64(int base, int exp) {
    if (base <= 0 || exp < 0) return 0;
    unsigned long long result = 1ULL;
    unsigned long long b = (unsigned long long)base;
    for (int i = 0; i < exp; i++) {
        if (result > (unsigned long long)LLONG_MAX / b) {
            return 0; /* Tran so */
        }
        result *= b;
    }
    return result;
}

/* ----------------------------------------------------------------------------
 * 2. Luong xu ly tung index theo so do trang 6:
 *    index_to_password() -> hash_password() -> check_password() -> save_result()
 * ---------------------------------------------------------------------------- */
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
    volatile int *found_flag,
    const char *password,
    int length
) {
    #pragma omp critical(brute_save_result)
    {
        if (!(*found_flag)) {
            *found_flag = 1;
            result->found = 1;
            result->found_length = length;
            memcpy(result->password, password, (size_t)length + 1);
        }
    }
}

/* Doc co dung (tim thay hoac nguoi dung bam Stop) */
static inline int should_stop_now(volatile int *found_flag, const BruteProgress *progress) {
    int f = 0;
    #pragma omp atomic read
    f = *found_flag;
    if (f) return 1;

    if (progress) {
        int s = 0;
        #pragma omp atomic read
        s = progress->stop_requested;
        if (s) return 1;
    }
    return 0;
}

/* Cong don so lan thu tu bien local cua tung luong vao bo dem chung */
static inline void flush_attempts(
    unsigned long long *local_attempts,
    unsigned long long *total_attempts,
    BruteProgress *progress
) {
    if (*local_attempts == 0) return;

    unsigned long long delta = *local_attempts;
    *local_attempts = 0;

    #pragma omp atomic
    *total_attempts += delta;

    if (progress) {
        #pragma omp atomic
        progress->attempts += delta;
    }
}

/* Ghi 1 diem du lieu (time_sec, attempts) cho do thi tang truong (chi goi tu 1 luong) */
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

    /* Tranh ghi trung lap y het diem ngay truoc do */
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
    unsigned long long *total_attempts
) {
    double now = omp_get_wtime();
    if (now - *last_sample_t >= TRACE_SAMPLE_DT) {
        unsigned long long snap = 0;
        #pragma omp atomic read
        snap = *total_attempts;
        trace_append(trace, now - t_start, snap, 0);
        *last_sample_t = now;
    }
}

/* ----------------------------------------------------------------------------
 * 3. Chay 1 muc do dai L theo tung che do lap lich OpenMP
 * ---------------------------------------------------------------------------- */
static void run_length_sequential(
    const BruteConfig *config,
    int L,
    long long total,
    double t_start,
    double *last_sample_t,
    BruteProgress *progress,
    BruteResult *result,
    volatile int *found_flag,
    unsigned long long *total_attempts
) {
    char password[MAX_PASSWORD_LEN + 1];
    uint8_t digest[MAX_DIGEST_BYTES];
    unsigned long long local_attempts = 0;

    for (long long idx = 0; idx < total; idx++) {
        if ((local_attempts & ATTEMPTS_FLUSH_MASK) == 0) {
            flush_attempts(&local_attempts, total_attempts, progress);
            trace_maybe_sample(&result->trace, t_start, last_sample_t, total_attempts);
            if (progress && progress->stop_requested) {
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
    volatile int *found_flag,
    unsigned long long *total_attempts
) {
    int threads = (config->num_threads > 0) ? config->num_threads : 1;
    int chunk   = (config->chunk_size > 0)  ? config->chunk_size  : DEFAULT_CHUNK_SIZE;

    /* NOTE CHO QUANG ANH:
     * Khung code tach ro 3 block schedule(static/dynamic/guided) de de doc va
     * de so sanh khi bao cao. Ket thuc vung `#pragma omp parallel` co san
     * barrier dong bo giua cac muc do dai L.
     *
     * Chi thread 0 ghi mau thoi gian (trace_maybe_sample) de khong gay tranh
     * chap lock va khong lam cham toc do song song. */
    #pragma omp parallel num_threads(threads)
    {
        char password[MAX_PASSWORD_LEN + 1];
        uint8_t digest[MAX_DIGEST_BYTES];
        unsigned long long local_attempts = 0;
        int stop_local = 0;
        int tid = omp_get_thread_num();

        if (mode == EXEC_MODE_STATIC) {
            #pragma omp for schedule(static, chunk)
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
        } else { /* EXEC_MODE_GUIDED */
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
    } /* Implicit OpenMP barrier tai day truoc khi sang do dai L + 1 */
}

/* ----------------------------------------------------------------------------
 * 4. Ham chinh chay 1 che do brute force (duoc goi tu worker thread)
 * ---------------------------------------------------------------------------- */
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

    volatile int found_flag = 0;
    unsigned long long total_attempts = 0;
    double t_start = omp_get_wtime();
    double last_sample_t = t_start;

    /* Diem khoi dau (0s, 0 attempts) cho do thi */
    trace_append(&result->trace, 0.0, 0, 0);

    if (progress) {
        progress->active_mode    = (int)mode;
        progress->current_length = 1;
        progress->attempts       = 0;
        progress->start_time     = t_start;
    }

    /* Vong lap ngoai cung duyet tuan tu do dai L = 1 .. max_length (trang 5) */
    for (int L = 1; L <= config->max_length; L++) {
        if (found_flag || (progress && progress->stop_requested)) {
            break;
        }

        if (progress) {
            progress->current_length = L;
        }

        unsigned long long total_u64 = safe_power_u64(config->charset_len, L);
        if (total_u64 == 0) {
            /* Khong gian tim kiem vuot gioi han 64-bit */
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

        /* Ghi 1 diem moc sau moi muc do dai L */
        double now = omp_get_wtime();
        trace_append(&result->trace, now - t_start, total_attempts, 0);
        last_sample_t = now;
    }

    double t_end = omp_get_wtime();
    result->elapsed_sec = t_end - t_start;
    result->attempts    = total_attempts;
    result->stopped     = (progress && progress->stop_requested && !result->found) ? 1 : 0;

    /* Diem ket thuc chinh xac cho do thi */
    trace_append(&result->trace, result->elapsed_sec, result->attempts, 1);
}
