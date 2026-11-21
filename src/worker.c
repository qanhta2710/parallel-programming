#include "worker.h"
#include "brute_force.h"

#include <stdlib.h>
#include <string.h>

static void post_mode_result(HWND hwnd, const BruteResult *res) {
    BruteResult *copy = (BruteResult *)malloc(sizeof(BruteResult));
    if (!copy) return;
    *copy = *res;
    if (!PostMessageA(hwnd, WM_APP_MODE_DONE, 0, (LPARAM)copy)) {
        free(copy);
    }
}

static DWORD WINAPI worker_thread_proc(LPVOID param) {
    WorkerContext *ctx = (WorkerContext *)param;
    RunStatus final_status = RUN_STATUS_NOT_FOUND;

    if (ctx->config.mode == EXEC_MODE_BENCHMARK) {
        const ExecMode modes[4] = {
            EXEC_MODE_SEQUENTIAL,
            EXEC_MODE_STATIC,
            EXEC_MODE_DYNAMIC,
            EXEC_MODE_GUIDED
        };

        for (int i = 0; i < 4; i++) {
            if (ctx->progress.stop_requested) {
                final_status = RUN_STATUS_STOPPED;
                break;
            }

            BruteResult res;
            brute_force_run_mode(&ctx->config, modes[i], &ctx->progress, &res);
            post_mode_result(ctx->hwnd_target, &res);

            if (res.stopped) {
                final_status = RUN_STATUS_STOPPED;
                break;
            }
            if (res.found) {
                final_status = RUN_STATUS_FOUND;
            }
        }
    } else {
        BruteResult res;
        brute_force_run_mode(&ctx->config, ctx->config.mode, &ctx->progress, &res);
        post_mode_result(ctx->hwnd_target, &res);

        if (res.found) {
            final_status = RUN_STATUS_FOUND;
        } else if (res.stopped) {
            final_status = RUN_STATUS_STOPPED;
        } else {
            final_status = RUN_STATUS_NOT_FOUND;
        }
    }

    ctx->is_running = 0;
    PostMessageA(ctx->hwnd_target, WM_APP_ALL_DONE, (WPARAM)final_status, 0);
    return 0;
}

void worker_init(WorkerContext *ctx, HWND hwnd_target) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->hwnd_target = hwnd_target;
}

int worker_start(WorkerContext *ctx, const BruteConfig *config) {
    if (!ctx || !config || ctx->is_running) {
        return 0;
    }

    /* Don dep handle cu neu con */
    if (ctx->thread_handle) {
        CloseHandle(ctx->thread_handle);
        ctx->thread_handle = NULL;
    }

    ctx->config = *config;
    memset(&ctx->progress, 0, sizeof(ctx->progress));
    ctx->progress.stop_requested = 0;
    ctx->progress.active_mode    = (int)config->mode;
    ctx->is_running              = 1;

    ctx->thread_handle = CreateThread(NULL, 0, worker_thread_proc, ctx, 0, NULL);
    if (!ctx->thread_handle) {
        ctx->is_running = 0;
        return 0;
    }
    return 1;
}

void worker_request_stop(WorkerContext *ctx) {
    if (!ctx || !ctx->is_running) return;
    ctx->progress.stop_requested = 1;
}

void worker_cleanup(WorkerContext *ctx) {
    if (!ctx) return;
    if (ctx->is_running) {
        ctx->progress.stop_requested = 1;
    }
    if (ctx->thread_handle) {
        WaitForSingleObject(ctx->thread_handle, INFINITE);
        CloseHandle(ctx->thread_handle);
        ctx->thread_handle = NULL;
    }
    ctx->is_running = 0;
}
