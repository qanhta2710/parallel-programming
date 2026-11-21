#ifndef WORKER_H
#define WORKER_H

#include <windows.h>
#include "config.h"

/* Custom messages gui tu worker thread ve cua so chinh Win32 */
#define WM_APP_MODE_DONE   (WM_APP + 1)  /* lParam = (LPARAM)(BruteResult*) (heap-allocated, UI free) */
#define WM_APP_ALL_DONE    (WM_APP + 2)  /* wParam = RunStatus cuoi cung */

typedef struct {
    HWND hwnd_target;
    BruteConfig config;
    BruteProgress progress;
    HANDLE thread_handle;
    volatile int is_running;
} WorkerContext;

/* Khoi tao WorkerContext */
void worker_init(WorkerContext *ctx, HWND hwnd_target);

/* Bat dau chay brute force / benchmark trong thread nen (khong treo UI).
 * Tra ve 1 neu tao thread thanh cong, 0 neu dang chay hoac loi. */
int worker_start(WorkerContext *ctx, const BruteConfig *config);

/* Gui yeu cau dung som den worker */
void worker_request_stop(WorkerContext *ctx);

/* Cho worker ket thuc han va don dep handle (goi khi dong app hoac ket thuc) */
void worker_cleanup(WorkerContext *ctx);

#endif /* WORKER_H */
