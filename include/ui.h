#ifndef UI_H
#define UI_H

#include <windows.h>

/* ============================================================================
 * Giao dien Win32 API thuan (4 khu vuc theo mockup tai lieu)
 * - Khu vuc 1: Input Configuration (Algo, Charset, Target Hash, Max Length)
 * - Khu vuc 2: OpenMP Configuration (Mode, Threads, Chunk Size, Start/Stop/Reset)
 * - Khu vuc 3: Execution & Result (Status, Password, Attempts, Time, Current L)
 * - Khu vuc 4: Performance Benchmark (Bang so sanh Time / Speedup / Efficiency)
 * ============================================================================ */

/* Timer ID cap nhat tien do real-time len UI */
#define IDT_PROGRESS_TIMER      1

/* Control IDs - Khu vuc 1: Input Configuration */
#define IDC_RADIO_MD5           101
#define IDC_RADIO_SHA1          102
#define IDC_EDIT_CHARSET        103
#define IDC_EDIT_TARGET_HASH    104
#define IDC_SLIDER_MAXLEN       105
#define IDC_LABEL_MAXLEN_VAL    106

/* Control IDs - Khu vuc 2: OpenMP Configuration */
#define IDC_RADIO_MODE_SEQ      201
#define IDC_RADIO_MODE_STATIC   202
#define IDC_RADIO_MODE_DYNAMIC  203
#define IDC_RADIO_MODE_GUIDED   204
#define IDC_RADIO_MODE_BENCH    205
#define IDC_SLIDER_THREADS      206
#define IDC_LABEL_THREADS_VAL   207
#define IDC_CHK_AUTO_THREADS    212
#define IDC_EDIT_CHUNK_SIZE     208
#define IDC_BTN_START           209
#define IDC_BTN_STOP            210
#define IDC_BTN_RESET           211

/* Control IDs - Khu vuc 3: Execution & Result */
#define IDC_VAL_STATUS          301
#define IDC_VAL_PASSWORD        302
#define IDC_VAL_ATTEMPTS        303
#define IDC_VAL_ELAPSED         304
#define IDC_VAL_CUR_LENGTH      305
#define IDC_VAL_HASH_BACKEND    306

/* Control IDs - Khu vuc 4: Performance Benchmark & Chart */
#define IDC_LIST_BENCHMARK      401
#define IDC_RADIO_CHART_GROWTH  402
#define IDC_RADIO_CHART_SPEEDUP 403
#define IDC_CHART_CANVAS        404

/* Status bar duoi cung */
#define IDC_STATUS_BAR          501

/* Khoi tao va chay vong lap thong diep Win32 cua cua so chinh */
int ui_run(HINSTANCE hInstance, int nCmdShow);

#endif /* UI_H */
