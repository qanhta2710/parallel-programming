#ifndef BRUTE_FORCE_H
#define BRUTE_FORCE_H

#include "config.h"

/* ============================================================================
 * [KHUNG CODE / FRAMEWORK] Hop dong giao tiep giua Giao dien (UI/Worker)
 * va Logic Brute Force + OpenMP (Quang Anh phu trach kiem tra / tinh chinh).
 *
 * Phan UI va Worker CHI goi cac ham khai bao trong file nay.
 * Quang Anh co the sua thoai mai ben trong src/brute_force.c ma khong anh huong
 * toi giao dien, mien la giu nguyen chu ky cac ham nay.
 * ============================================================================ */

/* Chuyen index -> chuoi mat khau do dai `length` tu tap `charset` co `K` ky tu.
 * (Theo dung thuat toan Index Mapping trong tai lieu trang 3-4) */
void index_to_password(
    unsigned long long index,
    int length,
    const char *charset,
    int K,
    char *password
);

/* Tinh luy thua nguyen base^exp co kiem tra tran so (tra ve 0 neu tran) */
unsigned long long safe_power_u64(int base, int exp);

/* Chay brute force voi 1 che do cu the (Sequential / Static / Dynamic / Guided).
 * - config:   cau hinh dau vao (hash target, charset, max_length, threads, chunk)
 * - mode:     che do chay cu the (0..3)
 * - progress: struct chia se de cap nhat tien do len UI va nhan tin hieu Stop
 * - result:   ket qua tra ve (tim thay hay khong, password, attempts, thoi gian)
 */
void brute_force_run_mode(
    const BruteConfig *config,
    ExecMode mode,
    BruteProgress *progress,
    BruteResult *result
);

/* Tra ve ten hien thi cua tung che do chay */
const char *exec_mode_name(ExecMode mode);

/* Tu dong de xuat so luong OpenMP toi uu dua tren so nhan CPU & khong gian tim kiem */
int brute_recommend_threads(int charset_len, int max_length);

#endif /* BRUTE_FORCE_H */
