#define _WIN32_IE 0x0500

#include "ui.h"
#include "brute_force.h"
#include "chart.h"
#include "config.h"
#include "hash_utils.h"
#include "worker.h"

#include <commctrl.h>
#include <ctype.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * [KHUNG CODE / FRAMEWORK] Giao dien Win32 API thuan
 * Gom cac khu vuc:
 *   1. Input Configuration (Algo, Charset, Target Hash, Max Length)
 *   2. OpenMP Configuration (Mode, Auto/Manual Threads, Chunk Size, Start/Stop/Reset)
 *   3. Execution & Result (Status, Password, Attempts, Time, Current L)
 *   4. Performance Benchmark (Bang so sanh Time / Speedup / Efficiency)
 *   5. Performance Visualization (Do thi duong tang truong & Speedup/Efficiency)
 * ============================================================================ */

#define MAIN_WND_CLASS   "BruteForceOpenMPMainWnd"
#define MAIN_WND_STYLE   (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN)

enum {
    CLIENT_WIDTH = 960,
    CLIENT_HEIGHT = 720,
    MIN_CLIENT_WIDTH = 850,
    MIN_CLIENT_HEIGHT = 600,
    PANEL_TOP = 10,
    PANEL_MARGIN = 14,
    PANEL_INSET = 16,
    LEFT_PANEL_WIDTH = 392,
    INPUT_HEIGHT = 170,
    RIGHT_PANEL_X = 420,
    RESULT_HEIGHT = 168,
    RESULT_VALUE_X = 568,
    BENCH_TOP = 188,
    BENCH_HEIGHT = 196,
    CHART_TOP = BENCH_TOP + BENCH_HEIGHT + 10,
    CHART_CANVAS_TOP = CHART_TOP + 48
};

typedef struct {
    HWND hwnd;
    HFONT font_regular;
    HFONT font_bold;
    HFONT font_mono;

    WorkerContext worker;
    BenchmarkEntry bench[4]; /* 0: Seq, 1: Static, 2: Dynamic, 3: Guided */
    ChartViewMode chart_mode;
    int last_run_threads;
    int last_run_max_len;
    int max_cpu_threads;
    int physical_cores;
} AppState;

static AppState g_app;

/* Dinh dang so nguyen lon co dau phay ngan cach hang nghin (VD: 1,234,567) */
static void format_u64_commas(unsigned long long val, char *buf, size_t buf_size) {
    char raw[32];
    snprintf(raw, sizeof(raw), "%llu", val);
    size_t len = strlen(raw);
    size_t pos = 0;

    for (size_t i = 0; i < len && pos + 2 < buf_size; i++) {
        if (i > 0 && ((len - i) % 3 == 0)) {
            buf[pos++] = ',';
        }
        buf[pos++] = raw[i];
    }
    buf[pos] = '\0';
}

/* Xoa khoang trang dau/cuoi chuoi */
static void trim_whitespace(char *s) {
    if (!s) return;
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static void set_ctrl_font(HWND parent, int ctrl_id, HFONT font) {
    HWND h = GetDlgItem(parent, ctrl_id);
    if (h && font) {
        SendMessageA(h, WM_SETFONT, (WPARAM)font, TRUE);
    }
}

static void set_ctrl_text(HWND parent, int ctrl_id, const char *text) {
    SetDlgItemTextA(parent, ctrl_id, text);
}

static void update_status_bar(HWND hwnd, const char *text) {
    HWND sb = GetDlgItem(hwnd, IDC_STATUS_BAR);
    if (sb) {
        SendMessageA(sb, SB_SETTEXTA, 0, (LPARAM)text);
    }
}

static void resize_control(HWND control, int width, int height) {
    SetWindowPos(control, NULL, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

static int chart_panel_bottom(HWND hwnd, int client_height) {
    RECT status_client = {0};
    GetClientRect(GetDlgItem(hwnd, IDC_STATUS_BAR), &status_client);
    return client_height - status_client.bottom - 10;
}

static void layout_ui(HWND hwnd) {
    HWND status = GetDlgItem(hwnd, IDC_STATUS_BAR);
    if (!status) return;

    SendMessageA(status, WM_SIZE, 0, 0);
    RECT client;
    GetClientRect(hwnd, &client);

    int right_edge = client.right - PANEL_MARGIN;
    int right_width = right_edge - RIGHT_PANEL_X;
    int chart_bottom = chart_panel_bottom(hwnd, client.bottom);
    if (right_edge <= RESULT_VALUE_X + PANEL_INSET || chart_bottom <= CHART_CANVAS_TOP + 12) return;

    const int result_ids[] = {
        IDC_VAL_STATUS, IDC_VAL_PASSWORD, IDC_VAL_ATTEMPTS, IDC_VAL_ELAPSED, IDC_VAL_CUR_LENGTH
    };
    for (size_t i = 0; i < sizeof(result_ids) / sizeof(result_ids[0]); ++i) {
        resize_control(GetDlgItem(hwnd, result_ids[i]), right_edge - PANEL_INSET - RESULT_VALUE_X, 20);
    }

    HWND list = GetDlgItem(hwnd, IDC_LIST_BENCHMARK);
    resize_control(list, right_width - 2 * PANEL_INSET, BENCH_HEIGHT - 40);
    RECT list_client = {0};
    GetClientRect(list, &list_client);
    int column_width = list_client.right / 4;
    for (int i = 0; i < 3; ++i) {
        ListView_SetColumnWidth(list, i, column_width);
    }
    ListView_SetColumnWidth(list, 3, list_client.right - 3 * column_width);

    int chart_width = right_edge - PANEL_MARGIN;
    resize_control(GetDlgItem(hwnd, IDC_CHART_CANVAS), chart_width - 2 * PANEL_INSET,
                   chart_bottom - CHART_CANVAS_TOP - 12);
}

static void paint_panel_background(HWND hwnd, HDC dc) {
    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));

    int left_edge = PANEL_MARGIN + LEFT_PANEL_WIDTH;
    int right_edge = client.right - PANEL_MARGIN;
    const RECT frames[] = {
        { PANEL_MARGIN, PANEL_TOP + 10, left_edge, PANEL_TOP + INPUT_HEIGHT },
        { PANEL_MARGIN, BENCH_TOP + 10, left_edge, BENCH_TOP + BENCH_HEIGHT },
        { RIGHT_PANEL_X, PANEL_TOP + 10, right_edge, PANEL_TOP + RESULT_HEIGHT },
        { RIGHT_PANEL_X, BENCH_TOP + 10, right_edge, BENCH_TOP + BENCH_HEIGHT },
        { PANEL_MARGIN, CHART_TOP + 10, right_edge, chart_panel_bottom(hwnd, client.bottom) }
    };
    HBRUSH border = GetSysColorBrush(COLOR_3DSHADOW);
    for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) {
        FrameRect(dc, &frames[i], border);
    }
}

static void create_panel_title(HWND hwnd, HINSTANCE instance, const char *text, int x, int y) {
    SIZE size = {230, 0};
    HDC dc = GetDC(hwnd);
    if (dc) {
        HGDIOBJ old_font = SelectObject(dc, g_app.font_bold);
        GetTextExtentPoint32A(dc, text, (int)strlen(text), &size);
        SelectObject(dc, old_font);
        ReleaseDC(hwnd, dc);
    }

    HWND title = CreateWindowExA(0, "STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
        x + 8, y, size.cx + 8, 20, hwnd, NULL, instance, NULL);
    SendMessageA(title, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
}

/* Cap nhat thanh truot & nhan Number of Threads theo trang thai Auto / Manual */
static void sync_threads_ui(HWND hwnd) {
    int is_auto = (IsDlgButtonChecked(hwnd, IDC_CHK_AUTO_THREADS) == BST_CHECKED);
    HWND sl_threads = GetDlgItem(hwnd, IDC_SLIDER_THREADS);
    if (!sl_threads) return;

    int chosen_threads = 1;
    if (is_auto) {
        char cs[MAX_CHARSET_LEN + 1];
        GetDlgItemTextA(hwnd, IDC_EDIT_CHARSET, cs, sizeof(cs));
        int cs_len  = (int)strlen(cs);
        int max_len = (int)SendDlgItemMessageA(hwnd, IDC_SLIDER_MAXLEN, TBM_GETPOS, 0, 0);
        if (cs_len < 1) cs_len = 1;
        if (max_len < 1) max_len = 1;

        chosen_threads = brute_recommend_threads(cs_len, max_len);
        SendMessageA(sl_threads, TBM_SETPOS, TRUE, chosen_threads);
    } else {
        chosen_threads = (int)SendMessageA(sl_threads, TBM_GETPOS, 0, 0);
        if (chosen_threads < 1) chosen_threads = 1;
    }

    if (!g_app.worker.is_running) {
        EnableWindow(sl_threads, !is_auto);
    }

    char buf[48];
    if (is_auto) {
        snprintf(buf, sizeof(buf), "Auto (%d / %d)", chosen_threads, g_app.max_cpu_threads);
    } else {
        snprintf(buf, sizeof(buf), "%d / %d", chosen_threads, g_app.max_cpu_threads);
    }
    set_ctrl_text(hwnd, IDC_LABEL_THREADS_VAL, buf);
}

/* Cap nhat du lieu do thi */
static void refresh_chart(HWND hwnd) {
    HWND chart = GetDlgItem(hwnd, IDC_CHART_CANVAS);
    if (!chart) return;
    int threads = (g_app.last_run_threads > 0) ? g_app.last_run_threads : 1;
    chart_set_data(chart, g_app.bench, threads, g_app.chart_mode);
}

/* Bat/tat cac control nhap lieu khi dang chay hoac dung */
static void set_controls_running_state(HWND hwnd, int running) {
    const int input_ids[] = {
        IDC_RADIO_MD5, IDC_RADIO_SHA1,
        IDC_EDIT_CHARSET, IDC_EDIT_TARGET_HASH, IDC_SLIDER_MAXLEN,
        IDC_RADIO_MODE_SEQ, IDC_RADIO_MODE_STATIC, IDC_RADIO_MODE_DYNAMIC,
        IDC_RADIO_MODE_GUIDED, IDC_RADIO_MODE_BENCH,
        IDC_CHK_AUTO_THREADS, IDC_EDIT_CHUNK_SIZE,
        IDC_BTN_START, IDC_BTN_RESET
    };
    int count = (int)(sizeof(input_ids) / sizeof(input_ids[0]));
    for (int i = 0; i < count; i++) {
        EnableWindow(GetDlgItem(hwnd, input_ids[i]), !running);
    }

    /* Slider threads chi bat lai khi khong chay VA khong bat Auto */
    int is_auto = (IsDlgButtonChecked(hwnd, IDC_CHK_AUTO_THREADS) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_SLIDER_THREADS), (!running && !is_auto));

    EnableWindow(GetDlgItem(hwnd, IDC_BTN_STOP), running);
}

/* Khoi tao 4 dong mac dinh cho bang Performance Benchmark */
static void init_benchmark_table(HWND hwnd) {
    HWND lv = GetDlgItem(hwnd, IDC_LIST_BENCHMARK);
    if (!lv) return;

    ListView_DeleteAllItems(lv);

    const char *mode_names[4] = { "Sequential", "Static", "Dynamic", "Guided" };
    for (int i = 0; i < 4; i++) {
        LVITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask    = LVIF_TEXT;
        item.iItem   = i;
        item.pszText = (LPSTR)mode_names[i];
        ListView_InsertItem(lv, &item);

        ListView_SetItemText(lv, i, 1, "-");
        ListView_SetItemText(lv, i, 2, "-");
        ListView_SetItemText(lv, i, 3, "-");
    }
}

/* Cap nhat lai bang Benchmark tu g_app.bench[] */
static void refresh_benchmark_table(HWND hwnd) {
    HWND lv = GetDlgItem(hwnd, IDC_LIST_BENCHMARK);
    if (!lv) return;

    int has_seq    = g_app.bench[EXEC_MODE_SEQUENTIAL].has_data;
    double seq_sec = g_app.bench[EXEC_MODE_SEQUENTIAL].elapsed_sec;
    int threads    = (g_app.last_run_threads > 0) ? g_app.last_run_threads : 1;

    for (int i = 0; i < 4; i++) {
        if (!g_app.bench[i].has_data) {
            ListView_SetItemText(lv, i, 1, "-");
            ListView_SetItemText(lv, i, 2, "-");
            ListView_SetItemText(lv, i, 3, "-");
            continue;
        }

        char time_buf[32];
        char spd_buf[32];
        char eff_buf[32];
        double t = g_app.bench[i].elapsed_sec;

        snprintf(time_buf, sizeof(time_buf), "%.4f", t);

        if (i == EXEC_MODE_SEQUENTIAL) {
            g_app.bench[i].speedup        = 1.0;
            g_app.bench[i].efficiency_pct = 100.0;
            snprintf(spd_buf, sizeof(spd_buf), "1.00x");
            snprintf(eff_buf, sizeof(eff_buf), "100.0%%");
        } else if (has_seq && t > 1e-7 && seq_sec > 1e-7) {
            double speedup    = seq_sec / t;
            double efficiency = (speedup / (double)threads) * 100.0;
            g_app.bench[i].speedup        = speedup;
            g_app.bench[i].efficiency_pct = efficiency;
            snprintf(spd_buf, sizeof(spd_buf), "%.2fx", speedup);
            snprintf(eff_buf, sizeof(eff_buf), "%.1f%%", efficiency);
        } else {
            snprintf(spd_buf, sizeof(spd_buf), "-");
            snprintf(eff_buf, sizeof(eff_buf), "-");
        }

        ListView_SetItemText(lv, i, 1, time_buf);
        ListView_SetItemText(lv, i, 2, spd_buf);
        ListView_SetItemText(lv, i, 3, eff_buf);
    }

    refresh_chart(hwnd);
}

/* Reset phan hien thi ket qua, bang benchmark & do thi */
static void reset_results_ui(HWND hwnd, int clear_benchmark) {
    set_ctrl_text(hwnd, IDC_VAL_STATUS,     "Ready");
    set_ctrl_text(hwnd, IDC_VAL_PASSWORD,   "-");
    set_ctrl_text(hwnd, IDC_VAL_ATTEMPTS,   "-");
    set_ctrl_text(hwnd, IDC_VAL_ELAPSED,    "-");
    set_ctrl_text(hwnd, IDC_VAL_CUR_LENGTH, "-");

    if (clear_benchmark) {
        memset(g_app.bench, 0, sizeof(g_app.bench));
        refresh_benchmark_table(hwnd);
    }
}

/* Khoi tao toan bo control Win32 */
static void create_ui_controls(HWND hwnd, HINSTANCE hInst) {
    g_app.font_regular = CreateFontA(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
    );
    g_app.font_bold = CreateFontA(
        -15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
    );
    g_app.font_mono = CreateFontA(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas"
    );

    g_app.max_cpu_threads = omp_get_max_threads();
    if (g_app.max_cpu_threads < 1) g_app.max_cpu_threads = 4;
    g_app.chart_mode = CHART_MODE_GROWTH;

    HWND h;

    /* ========================================================================
     * KHU VUC 1: Input Configuration (cot trai - tren)
     * ======================================================================== */
    create_panel_title(hwnd, hInst, "1. Input Configuration", PANEL_MARGIN, PANEL_TOP);

    h = CreateWindowExA(0, "STATIC", "Hash Algorithm:",
        WS_CHILD | WS_VISIBLE,
        28, 38, 112, 20, hwnd, NULL, hInst, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);

    CreateWindowExA(0, "BUTTON", "MD5 (32 hex)",
        WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON,
        145, 36, 112, 22, hwnd, (HMENU)IDC_RADIO_MD5, hInst, NULL);
    set_ctrl_font(hwnd, IDC_RADIO_MD5, g_app.font_regular);

    CreateWindowExA(0, "BUTTON", "SHA1 (40 hex)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        266, 36, 122, 22, hwnd, (HMENU)IDC_RADIO_SHA1, hInst, NULL);
    set_ctrl_font(hwnd, IDC_RADIO_SHA1, g_app.font_regular);
    SendDlgItemMessageA(hwnd, IDC_RADIO_MD5, BM_SETCHECK, BST_CHECKED, 0);

    h = CreateWindowExA(0, "STATIC", "Charset:",
        WS_CHILD | WS_VISIBLE | WS_GROUP,
        28, 64, 72, 18, hwnd, NULL, hInst, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);

    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", APP_DEFAULT_CHARSET,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        104, 60, 286, 26, hwnd, (HMENU)IDC_EDIT_CHARSET, hInst, NULL);
    set_ctrl_font(hwnd, IDC_EDIT_CHARSET, g_app.font_mono);

    h = CreateWindowExA(0, "STATIC", "Target Hash (Hex):",
        WS_CHILD | WS_VISIBLE,
        28, 90, 360, 18, hwnd, NULL, hInst, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);

    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", DEFAULT_MD5_HASH,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        28, 109, 362, 26, hwnd, (HMENU)IDC_EDIT_TARGET_HASH, hInst, NULL);
    set_ctrl_font(hwnd, IDC_EDIT_TARGET_HASH, g_app.font_mono);

    h = CreateWindowExA(0, "STATIC", "Maximum Password Length:",
        WS_CHILD | WS_VISIBLE,
        28, 146, 180, 20, hwnd, NULL, hInst, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);

    CreateWindowExA(0, "STATIC", "6 chars",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        334, 146, 56, 20, hwnd, (HMENU)IDC_LABEL_MAXLEN_VAL, hInst, NULL);
    set_ctrl_font(hwnd, IDC_LABEL_MAXLEN_VAL, g_app.font_bold);

    HWND sl_maxlen = CreateWindowExA(0, TRACKBAR_CLASSA, "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ,
        210, 140, 122, 30, hwnd, (HMENU)IDC_SLIDER_MAXLEN, hInst, NULL);
    SendMessageA(sl_maxlen, TBM_SETRANGE, TRUE, MAKELONG(1, 8));
    SendMessageA(sl_maxlen, TBM_SETPOS, TRUE, DEFAULT_MAX_LENGTH);

    /* ========================================================================
     * KHU VUC 2: OpenMP Configuration (cot trai - duoi)
     * ======================================================================== */
    create_panel_title(hwnd, hInst, "2. OpenMP Configuration", PANEL_MARGIN, BENCH_TOP);

    const struct { int id; const char *label; int x; int y; int width; } mode_radios[5] = {
        { IDC_RADIO_MODE_SEQ,     "Sequential (Single Thread)",    28, 212, 188 },
        { IDC_RADIO_MODE_STATIC,  "Static Scheduling",           222, 212, 168 },
        { IDC_RADIO_MODE_DYNAMIC, "Dynamic Scheduling",           28, 236, 188 },
        { IDC_RADIO_MODE_GUIDED,  "Guided Scheduling",           222, 236, 168 },
        { IDC_RADIO_MODE_BENCH,   "Run All Modes (Benchmark)",     28, 260, 250 }
    };
    for (int i = 0; i < 5; i++) {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
        if (i == 0) style |= (WS_GROUP | WS_TABSTOP);
        CreateWindowExA(0, "BUTTON", mode_radios[i].label,
            style, mode_radios[i].x, mode_radios[i].y, mode_radios[i].width, 22,
            hwnd, (HMENU)(INT_PTR)mode_radios[i].id, hInst, NULL);
        set_ctrl_font(hwnd, mode_radios[i].id, g_app.font_regular);
    }
    SendDlgItemMessageA(hwnd, IDC_RADIO_MODE_BENCH, BM_SETCHECK, BST_CHECKED, 0);

    /* Number of Threads + Auto checkbox */
    h = CreateWindowExA(0, "STATIC", "Number of Threads:",
        WS_CHILD | WS_VISIBLE | WS_GROUP,
        28, 288, 134, 20, hwnd, NULL, hInst, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);

    CreateWindowExA(0, "BUTTON", "Auto",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        166, 286, 62, 22, hwnd, (HMENU)IDC_CHK_AUTO_THREADS, hInst, NULL);
    set_ctrl_font(hwnd, IDC_CHK_AUTO_THREADS, g_app.font_regular);
    SendDlgItemMessageA(hwnd, IDC_CHK_AUTO_THREADS, BM_SETCHECK, BST_CHECKED, 0);

    CreateWindowExA(0, "STATIC", "",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        232, 288, 158, 20, hwnd, (HMENU)IDC_LABEL_THREADS_VAL, hInst, NULL);
    set_ctrl_font(hwnd, IDC_LABEL_THREADS_VAL, g_app.font_bold);

    HWND sl_threads = CreateWindowExA(0, TRACKBAR_CLASSA, "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ,
        24, 308, 222, 30, hwnd, (HMENU)IDC_SLIDER_THREADS, hInst, NULL);
    SendMessageA(sl_threads, TBM_SETRANGE, TRUE, MAKELONG(1, g_app.max_cpu_threads));
    sync_threads_ui(hwnd);

    /* Chunk Size */
    h = CreateWindowExA(0, "STATIC", "Chunk Size:",
        WS_CHILD | WS_VISIBLE,
        252, 313, 76, 22, hwnd, NULL, hInst, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);

    char chunk_buf[32];
    snprintf(chunk_buf, sizeof(chunk_buf), "%d", DEFAULT_CHUNK_SIZE);
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", chunk_buf,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
        330, 309, 60, 26, hwnd, (HMENU)IDC_EDIT_CHUNK_SIZE, hInst, NULL);
    set_ctrl_font(hwnd, IDC_EDIT_CHUNK_SIZE, g_app.font_mono);

    /* Cac nut dieu khien: Start / Stop / Reset */
    CreateWindowExA(0, "BUTTON", "Start",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        28, 345, 114, 30, hwnd, (HMENU)IDC_BTN_START, hInst, NULL);
    set_ctrl_font(hwnd, IDC_BTN_START, g_app.font_bold);

    CreateWindowExA(0, "BUTTON", "Stop",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        152, 345, 114, 30, hwnd, (HMENU)IDC_BTN_STOP, hInst, NULL);
    set_ctrl_font(hwnd, IDC_BTN_STOP, g_app.font_regular);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_STOP), FALSE);

    CreateWindowExA(0, "BUTTON", "Reset",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        276, 345, 114, 30, hwnd, (HMENU)IDC_BTN_RESET, hInst, NULL);
    set_ctrl_font(hwnd, IDC_BTN_RESET, g_app.font_regular);

    /* ========================================================================
     * KHU VUC 3: Execution & Result (cot phai - tren)
     * ======================================================================== */
    int right_width = CLIENT_WIDTH - PANEL_MARGIN - RIGHT_PANEL_X;
    create_panel_title(hwnd, hInst, "3. Execution and Result", RIGHT_PANEL_X, PANEL_TOP);

    const struct { const char *name; int val_id; const char *def_val; int is_mono; int y; } rows[] = {
        { "Status:",          IDC_VAL_STATUS,       "Ready", 0, 36  },
        { "Password Found:",  IDC_VAL_PASSWORD,     "-",     1, 62  },
        { "Attempts:",        IDC_VAL_ATTEMPTS,     "-",     1, 88  },
        { "Elapsed Time:",    IDC_VAL_ELAPSED,      "-",     1, 114 },
        { "Current Length:",  IDC_VAL_CUR_LENGTH,   "-",     0, 140 }
    };

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        h = CreateWindowExA(0, "STATIC", rows[i].name,
            WS_CHILD | WS_VISIBLE,
            RIGHT_PANEL_X + PANEL_INSET, rows[i].y, 128, 20, hwnd, NULL, hInst, NULL);
        SendMessageA(h, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);

        CreateWindowExA(0, "STATIC", rows[i].def_val,
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
            RESULT_VALUE_X, rows[i].y, CLIENT_WIDTH - PANEL_MARGIN - PANEL_INSET - RESULT_VALUE_X, 20,
            hwnd, (HMENU)(INT_PTR)rows[i].val_id, hInst, NULL);

        HFONT f = rows[i].is_mono ? g_app.font_mono :
                  (rows[i].val_id == IDC_VAL_STATUS ? g_app.font_bold : g_app.font_regular);
        set_ctrl_font(hwnd, rows[i].val_id, f);
    }

    /* ========================================================================
     * KHU VUC 4: Performance Benchmark (cot phai - giua)
     * ======================================================================== */
    create_panel_title(hwnd, hInst, "4. Performance Benchmark", RIGHT_PANEL_X, BENCH_TOP);

    HWND lv = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER,
        RIGHT_PANEL_X + PANEL_INSET, BENCH_TOP + 26, right_width - 2 * PANEL_INSET, BENCH_HEIGHT - 40,
        hwnd, (HMENU)IDC_LIST_BENCHMARK, hInst, NULL);
    SendMessageA(lv, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    const struct { const char *title; int fmt; } cols[4] = {
        { "Mode",       LVCFMT_LEFT  },
        { "Time (s)",   LVCFMT_RIGHT },
        { "Speedup",    LVCFMT_RIGHT },
        { "Efficiency", LVCFMT_RIGHT }
    };
    for (int i = 0; i < 4; i++) {
        LVCOLUMNA col;
        memset(&col, 0, sizeof(col));
        col.mask    = LVCF_TEXT | LVCF_FMT;
        col.fmt     = cols[i].fmt;
        col.pszText = (LPSTR)cols[i].title;
        ListView_InsertColumn(lv, i, &col);
    }
    init_benchmark_table(hwnd);

    /* ========================================================================
     * KHU VUC 5: Performance Chart (ben duoi ca hai cot)
     * ======================================================================== */
    int chart_width = CLIENT_WIDTH - 2 * PANEL_MARGIN;
    int chart_inner_x = PANEL_MARGIN + PANEL_INSET;
    create_panel_title(hwnd, hInst, "5. Performance Chart", PANEL_MARGIN, CHART_TOP);

    CreateWindowExA(0, "BUTTON", "Growth Curve (Attempts / Time)",
        WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON,
        chart_inner_x, CHART_TOP + 23, 225, 20, hwnd, (HMENU)IDC_RADIO_CHART_GROWTH, hInst, NULL);
    set_ctrl_font(hwnd, IDC_RADIO_CHART_GROWTH, g_app.font_regular);

    CreateWindowExA(0, "BUTTON", "Speedup and Efficiency Trend",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        chart_inner_x + 238, CHART_TOP + 23, 220, 20, hwnd, (HMENU)IDC_RADIO_CHART_SPEEDUP, hInst, NULL);
    set_ctrl_font(hwnd, IDC_RADIO_CHART_SPEEDUP, g_app.font_regular);
    SendDlgItemMessageA(hwnd, IDC_RADIO_CHART_GROWTH, BM_SETCHECK, BST_CHECKED, 0);

    CreateWindowExA(0, CHART_WND_CLASS, "",
        WS_CHILD | WS_VISIBLE | WS_GROUP,
        chart_inner_x, CHART_CANVAS_TOP, chart_width - 2 * PANEL_INSET, 200,
        hwnd, (HMENU)IDC_CHART_CANVAS, hInst, NULL);
    refresh_chart(hwnd);

    /* ========================================================================
     * Status Bar duoi cung
     * ======================================================================== */
    HWND sb = CreateWindowExA(0, STATUSCLASSNAMEA, NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS_BAR, hInst, NULL);
    SendMessageA(sb, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);

    char sb_init[192];
    snprintf(sb_init, sizeof(sb_init),
        "Ready | CPU: %d physical / %d logical cores | Default MD5/SHA1 hash is \"abc\"",
        g_app.physical_cores, g_app.max_cpu_threads);
    update_status_bar(hwnd, sb_init);
}

/* Doc & kiem tra cau hinh tu UI truoc khi Start */
static int read_and_validate_config(HWND hwnd, BruteConfig *cfg, char *err_msg, size_t err_size) {
    memset(cfg, 0, sizeof(*cfg));

    /* 1. Thuat toan */
    cfg->algo = (IsDlgButtonChecked(hwnd, IDC_RADIO_SHA1) == BST_CHECKED)
        ? HASH_ALGO_SHA1
        : HASH_ALGO_MD5;
    cfg->hash_byte_len = hash_digest_length(cfg->algo);

    /* 2. Charset */
    GetDlgItemTextA(hwnd, IDC_EDIT_CHARSET, cfg->charset, sizeof(cfg->charset));
    cfg->charset_len = (int)strlen(cfg->charset);
    if (cfg->charset_len <= 0) {
        snprintf(err_msg, err_size, "Charset cannot be empty.");
        return 0;
    }

    /* 3. Target Hash */
    GetDlgItemTextA(hwnd, IDC_EDIT_TARGET_HASH, cfg->target_hash_hex, sizeof(cfg->target_hash_hex));
    trim_whitespace(cfg->target_hash_hex);
    SetDlgItemTextA(hwnd, IDC_EDIT_TARGET_HASH, cfg->target_hash_hex);

    if (!hash_hex_to_bytes(cfg->target_hash_hex, cfg->target_hash_bytes, cfg->hash_byte_len)) {
        snprintf(err_msg, err_size,
            "Invalid %s hash.\nExpected %d hexadecimal characters (0-9, a-f).",
            (cfg->algo == HASH_ALGO_SHA1) ? "SHA1" : "MD5",
            cfg->hash_byte_len * 2);
        return 0;
    }

    /* 4. Max Length */
    cfg->max_length = (int)SendDlgItemMessageA(hwnd, IDC_SLIDER_MAXLEN, TBM_GETPOS, 0, 0);
    if (cfg->max_length < 1 || cfg->max_length > MAX_PASSWORD_LEN) {
        snprintf(err_msg, err_size, "Max password length must be between 1 and %d.", MAX_PASSWORD_LEN);
        return 0;
    }

    /* 5. Execution Mode */
    if (IsDlgButtonChecked(hwnd, IDC_RADIO_MODE_SEQ) == BST_CHECKED) {
        cfg->mode = EXEC_MODE_SEQUENTIAL;
    } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_MODE_STATIC) == BST_CHECKED) {
        cfg->mode = EXEC_MODE_STATIC;
    } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_MODE_DYNAMIC) == BST_CHECKED) {
        cfg->mode = EXEC_MODE_DYNAMIC;
    } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_MODE_GUIDED) == BST_CHECKED) {
        cfg->mode = EXEC_MODE_GUIDED;
    } else {
        cfg->mode = EXEC_MODE_BENCHMARK;
    }

    /* 6. Threads (Auto hoac Manual) & Chunk Size */
    cfg->auto_threads = (IsDlgButtonChecked(hwnd, IDC_CHK_AUTO_THREADS) == BST_CHECKED) ? 1 : 0;
    if (cfg->auto_threads) {
        cfg->num_threads = brute_recommend_threads(cfg->charset_len, cfg->max_length);
        sync_threads_ui(hwnd);
    } else {
        cfg->num_threads = (int)SendDlgItemMessageA(hwnd, IDC_SLIDER_THREADS, TBM_GETPOS, 0, 0);
    }
    if (cfg->num_threads < 1) cfg->num_threads = 1;

    char chunk_str[32];
    GetDlgItemTextA(hwnd, IDC_EDIT_CHUNK_SIZE, chunk_str, sizeof(chunk_str));
    cfg->chunk_size = atoi(chunk_str);
    if (cfg->chunk_size < 1) {
        snprintf(err_msg, err_size, "Chunk size must be a positive integer (>= 1).");
        return 0;
    }

    return 1;
}

static void handle_start_clicked(HWND hwnd) {
    BruteConfig cfg;
    char err_msg[256];

    if (!read_and_validate_config(hwnd, &cfg, err_msg, sizeof(err_msg))) {
        MessageBoxA(hwnd, err_msg, "Invalid Configuration", MB_OK | MB_ICONWARNING);
        return;
    }

    int clear_bench = (cfg.mode == EXEC_MODE_BENCHMARK);
    reset_results_ui(hwnd, clear_bench);

    g_app.last_run_threads = cfg.num_threads;
    g_app.last_run_max_len = cfg.max_length;

    if (!worker_start(&g_app.worker, &cfg)) {
        MessageBoxA(hwnd, "Failed to start worker thread.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    set_controls_running_state(hwnd, 1);
    set_ctrl_text(hwnd, IDC_VAL_STATUS, "Running...");
    SetTimer(hwnd, IDT_PROGRESS_TIMER, 100, NULL);

    char sb_msg[192];
    snprintf(sb_msg, sizeof(sb_msg),
        "Running [%s] | Threads: %d (%s) | Chunk: %d | MaxLen: %d",
        exec_mode_name(cfg.mode),
        cfg.num_threads,
        cfg.auto_threads ? "Auto" : "Manual",
        cfg.chunk_size,
        cfg.max_length);
    update_status_bar(hwnd, sb_msg);
}

static void handle_stop_clicked(HWND hwnd) {
    worker_request_stop(&g_app.worker);
    set_ctrl_text(hwnd, IDC_VAL_STATUS, "Stopping...");
    update_status_bar(hwnd, "Stop requested, waiting for OpenMP threads to finish...");
}

static void handle_reset_clicked(HWND hwnd) {
    if (g_app.worker.is_running) return;

    SendDlgItemMessageA(hwnd, IDC_RADIO_MD5, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessageA(hwnd, IDC_RADIO_SHA1, BM_SETCHECK, BST_UNCHECKED, 0);
    set_ctrl_text(hwnd, IDC_EDIT_CHARSET, APP_DEFAULT_CHARSET);
    set_ctrl_text(hwnd, IDC_EDIT_TARGET_HASH, DEFAULT_MD5_HASH);

    SendDlgItemMessageA(hwnd, IDC_SLIDER_MAXLEN, TBM_SETPOS, TRUE, DEFAULT_MAX_LENGTH);
    set_ctrl_text(hwnd, IDC_LABEL_MAXLEN_VAL, "6 chars");

    SendDlgItemMessageA(hwnd, IDC_CHK_AUTO_THREADS, BM_SETCHECK, BST_CHECKED, 0);
    sync_threads_ui(hwnd);

    char chunk_buf[32];
    snprintf(chunk_buf, sizeof(chunk_buf), "%d", DEFAULT_CHUNK_SIZE);
    set_ctrl_text(hwnd, IDC_EDIT_CHUNK_SIZE, chunk_buf);

    reset_results_ui(hwnd, 1);
    update_status_bar(hwnd, "Configuration, benchmark, and chart reset to defaults.");
}

/* Khi doi thuat toan MD5 <-> SHA1: neu dang de hash mau "abc" thi tu dong doi tuong ung */
static void handle_algo_changed(HWND hwnd, HashAlgo new_algo) {
    char cur[MAX_HASH_HEX_LEN + 1];
    GetDlgItemTextA(hwnd, IDC_EDIT_TARGET_HASH, cur, sizeof(cur));
    trim_whitespace(cur);

    if (new_algo == HASH_ALGO_SHA1 && _stricmp(cur, DEFAULT_MD5_HASH) == 0) {
        set_ctrl_text(hwnd, IDC_EDIT_TARGET_HASH, DEFAULT_SHA1_HASH);
    } else if (new_algo == HASH_ALGO_MD5 && _stricmp(cur, DEFAULT_SHA1_HASH) == 0) {
        set_ctrl_text(hwnd, IDC_EDIT_TARGET_HASH, DEFAULT_MD5_HASH);
    }
}

static void handle_progress_timer(HWND hwnd) {
    if (!g_app.worker.is_running) return;

    const BruteProgress *progress = &g_app.worker.progress;
    unsigned long long att = atomic_load_explicit(&progress->attempts, memory_order_relaxed);
    int cur_len = atomic_load_explicit(&progress->current_length, memory_order_relaxed);
    ExecMode act_mode = (ExecMode)atomic_load_explicit(&progress->active_mode, memory_order_relaxed);
    double start_time = atomic_load_explicit(&progress->start_time, memory_order_relaxed);
    double elapsed = (start_time != 0.0) ? omp_get_wtime() - start_time : 0.0;
    if (elapsed < 0.0) elapsed = 0.0;

    char buf[64];

    snprintf(buf, sizeof(buf), "Running (%s)...", exec_mode_name(act_mode));
    set_ctrl_text(hwnd, IDC_VAL_STATUS, buf);

    format_u64_commas(att, buf, sizeof(buf));
    set_ctrl_text(hwnd, IDC_VAL_ATTEMPTS, buf);

    snprintf(buf, sizeof(buf), "%.4f s", elapsed);
    set_ctrl_text(hwnd, IDC_VAL_ELAPSED, buf);

    snprintf(buf, sizeof(buf), "%d / %d", cur_len, g_app.last_run_max_len);
    set_ctrl_text(hwnd, IDC_VAL_CUR_LENGTH, buf);
}

static void handle_mode_done(HWND hwnd, BruteResult *res) {
    if (!res) return;

    char buf[64];

    if (res->found) {
        set_ctrl_text(hwnd, IDC_VAL_PASSWORD, res->password);
        snprintf(buf, sizeof(buf), "%d / %d", res->found_length, g_app.last_run_max_len);
        set_ctrl_text(hwnd, IDC_VAL_CUR_LENGTH, buf);
    }

    format_u64_commas(res->attempts, buf, sizeof(buf));
    set_ctrl_text(hwnd, IDC_VAL_ATTEMPTS, buf);

    snprintf(buf, sizeof(buf), "%.4f s", res->elapsed_sec);
    set_ctrl_text(hwnd, IDC_VAL_ELAPSED, buf);

    /* Ghi nhan vao bang Benchmark + luu trace ve do thi */
    if (!res->stopped && res->mode >= EXEC_MODE_SEQUENTIAL && res->mode <= EXEC_MODE_GUIDED) {
        g_app.bench[res->mode].has_data    = 1;
        g_app.bench[res->mode].elapsed_sec = res->elapsed_sec;
        g_app.bench[res->mode].attempts    = res->attempts;
        g_app.bench[res->mode].trace       = res->trace;
        refresh_benchmark_table(hwnd);
    }

    free(res);
}

static void handle_all_done(HWND hwnd, RunStatus final_status) {
    KillTimer(hwnd, IDT_PROGRESS_TIMER);
    worker_cleanup(&g_app.worker);
    set_controls_running_state(hwnd, 0);

    switch (final_status) {
        case RUN_STATUS_FOUND:
            set_ctrl_text(hwnd, IDC_VAL_STATUS, "Found!");
            update_status_bar(hwnd, "Completed: Password found.");
            break;
        case RUN_STATUS_STOPPED:
            set_ctrl_text(hwnd, IDC_VAL_STATUS, "Stopped");
            update_status_bar(hwnd, "Execution stopped by user.");
            break;
        case RUN_STATUS_NOT_FOUND:
        default:
            set_ctrl_text(hwnd, IDC_VAL_STATUS, "Not Found");
            set_ctrl_text(hwnd, IDC_VAL_PASSWORD, "(not found)");
            update_status_bar(hwnd, "Completed: Password not found within search space.");
            break;
    }
}

static LRESULT CALLBACK main_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hInst = ((CREATESTRUCTA *)lParam)->hInstance;
            g_app.hwnd = hwnd;
            worker_init(&g_app.worker, hwnd);
            create_ui_controls(hwnd, hInst);
            layout_ui(hwnd);
            return 0;
        }

        case WM_HSCROLL: {
            HWND src = (HWND)lParam;
            if (src == GetDlgItem(hwnd, IDC_SLIDER_MAXLEN)) {
                int pos = (int)SendMessageA(src, TBM_GETPOS, 0, 0);
                char buf[32];
                snprintf(buf, sizeof(buf), "%d %s", pos, (pos == 1) ? "char" : "chars");
                set_ctrl_text(hwnd, IDC_LABEL_MAXLEN_VAL, buf);
                if (IsDlgButtonChecked(hwnd, IDC_CHK_AUTO_THREADS) == BST_CHECKED) {
                    sync_threads_ui(hwnd);
                }
            } else if (src == GetDlgItem(hwnd, IDC_SLIDER_THREADS)) {
                sync_threads_ui(hwnd);
            }
            return 0;
        }

        case WM_COMMAND: {
            int id   = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                switch (id) {
                    case IDC_BTN_START:           handle_start_clicked(hwnd); break;
                    case IDC_BTN_STOP:            handle_stop_clicked(hwnd);  break;
                    case IDC_BTN_RESET:           handle_reset_clicked(hwnd); break;
                    case IDC_RADIO_MD5:           handle_algo_changed(hwnd, HASH_ALGO_MD5);  break;
                    case IDC_RADIO_SHA1:          handle_algo_changed(hwnd, HASH_ALGO_SHA1); break;
                    case IDC_CHK_AUTO_THREADS:    sync_threads_ui(hwnd); break;
                    case IDC_RADIO_CHART_GROWTH:
                        g_app.chart_mode = CHART_MODE_GROWTH;
                        refresh_chart(hwnd);
                        break;
                    case IDC_RADIO_CHART_SPEEDUP:
                        g_app.chart_mode = CHART_MODE_SPEEDUP;
                        refresh_chart(hwnd);
                        break;
                    default: break;
                }
            } else if (code == EN_CHANGE && id == IDC_EDIT_CHARSET) {
                if (IsDlgButtonChecked(hwnd, IDC_CHK_AUTO_THREADS) == BST_CHECKED) {
                    sync_threads_ui(hwnd);
                }
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == IDT_PROGRESS_TIMER) {
                handle_progress_timer(hwnd);
            }
            return 0;

        case WM_APP_MODE_DONE:
            handle_mode_done(hwnd, (BruteResult *)lParam);
            return 0;

        case WM_APP_ALL_DONE:
            handle_all_done(hwnd, (RunStatus)wParam);
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO *info = (MINMAXINFO *)lParam;
            RECT min_size = { 0, 0, MIN_CLIENT_WIDTH, MIN_CLIENT_HEIGHT };
            AdjustWindowRectEx(&min_size, MAIN_WND_STYLE, FALSE, 0);
            info->ptMinTrackSize.x = min_size.right - min_size.left;
            info->ptMinTrackSize.y = min_size.bottom - min_size.top;
            return 0;
        }

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                layout_ui(hwnd);
                RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            paint_panel_background(hwnd, dc);
            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_PRINTCLIENT:
            paint_panel_background(hwnd, (HDC)wParam);
            return 0;

        case WM_CTLCOLORSTATIC:
            SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

        case WM_CLOSE:
            KillTimer(hwnd, IDT_PROGRESS_TIMER);
            worker_cleanup(&g_app.worker);
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_app.font_regular) DeleteObject(g_app.font_regular);
            if (g_app.font_bold)    DeleteObject(g_app.font_bold);
            if (g_app.font_mono)    DeleteObject(g_app.font_mono);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int ui_run(HINSTANCE hInstance, int nCmdShow) {
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    if (!chart_register_class(hInstance)) {
        MessageBoxA(NULL, "Failed to register chart control class.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = main_wnd_proc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconA(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = MAIN_WND_CLASS;

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Failed to register Win32 window class.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    RECT rc = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
    AdjustWindowRectEx(&rc, MAIN_WND_STYLE, FALSE, 0);
    int wnd_w = rc.right - rc.left;
    int wnd_h = rc.bottom - rc.top;

    RECT work_area = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work_area, 0);
    int work_w = work_area.right - work_area.left;
    int work_h = work_area.bottom - work_area.top;
    if (wnd_w > work_w) wnd_w = work_w;
    if (wnd_h > work_h) wnd_h = work_h;
    int pos_x = work_area.left + (work_w - wnd_w) / 2;
    int pos_y = work_area.top + (work_h - wnd_h) / 2;

    HWND hwnd = CreateWindowExA(
        0, MAIN_WND_CLASS, APP_TITLE, MAIN_WND_STYLE,
        pos_x, pos_y, wnd_w, wnd_h,
        NULL, NULL, hInstance, NULL
    );
    if (!hwnd) {
        MessageBoxA(NULL, "Failed to create main window.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    return (int)msg.wParam;
}
