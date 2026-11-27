#ifndef CONFIG_H
#define CONFIG_H

#include <stdatomic.h>
#include <stdint.h>

#define APP_TITLE              "Password Brute Force - OpenMP"
#define MAX_CHARSET_LEN        128
#define MAX_PASSWORD_LEN       12
#define DEFAULT_MAX_LENGTH     6
#define DEFAULT_CHUNK_SIZE     1000
#define APP_DEFAULT_CHARSET    "abcdefghijklmnopqrstuvwxyz"
#define DEFAULT_MD5_HASH       "900150983cd24fb0d6963f7d28e17f72"           /* MD5("abc")  */
#define DEFAULT_SHA1_HASH      "a9993e364706816aba3e25717850c26c9cd0d89d"   /* SHA1("abc") */

#define MD5_DIGEST_BYTES       16
#define SHA1_DIGEST_BYTES      20
#define MAX_DIGEST_BYTES       20
#define MAX_HASH_HEX_LEN       64
#define MAX_TRACE_SAMPLES      128

typedef enum {
    HASH_ALGO_MD5  = 0,
    HASH_ALGO_SHA1 = 1
} HashAlgo;

typedef enum {
    EXEC_MODE_SEQUENTIAL = 0,
    EXEC_MODE_STATIC     = 1,
    EXEC_MODE_DYNAMIC    = 2,
    EXEC_MODE_GUIDED     = 3,
    EXEC_MODE_BENCHMARK  = 4   /* Chay lan luot ca 4 che do de so sanh */
} ExecMode;

/* Trang thai thuc thi hien tai (hien thi len UI) */
typedef enum {
    RUN_STATUS_READY = 0,
    RUN_STATUS_RUNNING,
    RUN_STATUS_FOUND,
    RUN_STATUS_NOT_FOUND,
    RUN_STATUS_STOPPED,
    RUN_STATUS_ERROR
} RunStatus;

/* Mot diem du lieu (thoi gian, so mat khau da thu) de ve do thi tang truong */
typedef struct {
    double time_sec;
    unsigned long long attempts;
} TraceSample;

typedef struct {
    TraceSample samples[MAX_TRACE_SAMPLES];
    int count;
} ModeTrace;

/* Input cau hinh tu UI truyen xuong core brute force */
typedef struct {
    HashAlgo algo;
    ExecMode mode;
    char charset[MAX_CHARSET_LEN + 1];
    int charset_len;
    char target_hash_hex[MAX_HASH_HEX_LEN + 1];
    uint8_t target_hash_bytes[MAX_DIGEST_BYTES];
    int hash_byte_len;
    int max_length;
    int auto_threads;       /* 1 = app tu tinh so luong toi uu, 0 = nguoi dung tu chon */
    int num_threads;
    int chunk_size;
} BruteConfig;

/* Tien do chia se giua worker/OpenMP va UI (cap nhat dinh ky qua WM_TIMER) */
typedef struct {
    atomic_int stop_requested;         /* UI dat = 1 khi nguoi dung bam Stop */
    atomic_int current_length;         /* Do dai L dang duyet */
    atomic_int active_mode;            /* ExecMode dang chay (huu ich khi Benchmark) */
    atomic_ullong attempts;
    _Atomic double start_time;         /* omp_get_wtime() luc bat dau */
} BruteProgress;

/* Ket qua sau khi chay 1 che do */
typedef struct {
    ExecMode mode;
    int found;
    int stopped;
    char password[MAX_PASSWORD_LEN + 1];
    unsigned long long attempts;
    double elapsed_sec;
    int found_length;
    ModeTrace trace;
} BruteResult;

/* Mot dong trong bang Performance Benchmark (4 che do: 0..3) */
typedef struct {
    int has_data;
    double elapsed_sec;
    double speedup;
    double efficiency_pct;
    unsigned long long attempts;
    ModeTrace trace;
} BenchmarkEntry;

#endif /* CONFIG_H */
