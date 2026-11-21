#include "chart.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Control do thi duong (Line Chart) thuan Win32 GDI + Double Buffering
 * Ho tro 2 che do xem:
 *   1. Growth Curve (Attempts vs Time): 4 duong bieu dien toc do tang truong
 *      cua Sequential, Static, Dynamic, Guided theo thoi gian.
 *   2. Speedup & Efficiency: do thi duong 2 truc Y so sanh Speedup (x) va
 *      Efficiency (%) giua 4 phuong phap.
 * ============================================================================ */

typedef struct {
    BenchmarkEntry bench[4];
    int threads;
    ChartViewMode view_mode;
    HFONT font_small;
    HFONT font_bold;
} ChartState;

static const COLORREF kModeColors[4] = {
    RGB(220, 53, 69),   /* Sequential - Do */
    RGB(13, 110, 253),  /* Static     - Xanh duong */
    RGB(25, 135, 84),   /* Dynamic    - Xanh la */
    RGB(245, 130, 32)   /* Guided     - Cam */
};

static const char *kModeLabels[4] = {
    "Sequential", "Static", "Dynamic", "Guided"
};

static void format_attempts_short(double val, char *buf, size_t size) {
    if (val >= 1e9) {
        snprintf(buf, size, "%.1fB", val / 1e9);
    } else if (val >= 1e6) {
        snprintf(buf, size, "%.1fM", val / 1e6);
    } else if (val >= 1e3) {
        snprintf(buf, size, "%.1fK", val / 1e3);
    } else {
        snprintf(buf, size, "%.0f", val);
    }
}

static void format_time_short(double sec, double max_sec, char *buf, size_t size) {
    if (max_sec < 0.01) {
        snprintf(buf, size, "%.2fms", sec * 1000.0);
    } else if (max_sec < 1.0) {
        snprintf(buf, size, "%.3fs", sec);
    } else {
        snprintf(buf, size, "%.2fs", sec);
    }
}

static void draw_circle_marker(HDC hdc, int cx, int cy, int r, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HBRUSH br = CreateSolidBrush(color);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    HGDIOBJ old_br  = SelectObject(hdc, br);
    Ellipse(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(br);
    DeleteObject(pen);
}

static void draw_square_marker(HDC hdc, int cx, int cy, int r, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HBRUSH br = CreateSolidBrush(color);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    HGDIOBJ old_br  = SelectObject(hdc, br);
    Rectangle(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(br);
    DeleteObject(pen);
}

static void draw_empty_state(HDC hdc, const RECT *rc, HFONT font_small) {
    HGDIOBJ old_font = SelectObject(hdc, font_small);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(125, 135, 148));
    RECT text_rc = *rc;
    DrawTextA(hdc,
        "No benchmark data yet.\nSelect \"Run All Modes (Benchmark)\" and click Start to visualize all 4 methods.",
        -1, &text_rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    SelectObject(hdc, old_font);
}

/* ----------------------------------------------------------------------------
 * Che do 1: Do thi duong toc do tang truong (Attempts vs Time) cho 4 phuong phap
 * ---------------------------------------------------------------------------- */
static void paint_growth_chart(HDC hdc, const RECT *rc, const ChartState *st) {
    int has_any = 0;
    double max_t = 0.0;
    double max_a = 0.0;

    for (int m = 0; m < 4; m++) {
        if (!st->bench[m].has_data) continue;
        has_any = 1;
        if (st->bench[m].elapsed_sec > max_t) max_t = st->bench[m].elapsed_sec;
        if ((double)st->bench[m].attempts > max_a) max_a = (double)st->bench[m].attempts;
        for (int k = 0; k < st->bench[m].trace.count; k++) {
            double t = st->bench[m].trace.samples[k].time_sec;
            double a = (double)st->bench[m].trace.samples[k].attempts;
            if (t > max_t) max_t = t;
            if (a > max_a) max_a = a;
        }
    }

    if (!has_any) {
        draw_empty_state(hdc, rc, st->font_small);
        return;
    }

    if (max_t < 1e-6) max_t = 1e-4;
    if (max_a < 10.0) max_a = 10.0;
    max_t *= 1.06;
    max_a *= 1.12;

    const int pad_l = 56;
    const int pad_r = 16;
    const int pad_t = 30;
    const int pad_b = 34;

    int x0 = rc->left + pad_l;
    int y0 = rc->bottom - pad_b;
    int x1 = rc->right - pad_r;
    int y1 = rc->top + pad_t;
    int plot_w = x1 - x0;
    int plot_h = y0 - y1;
    if (plot_w < 40 || plot_h < 40) return;

    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ old_font = SelectObject(hdc, st->font_small);

    /* Legend ngang o phia tren */
    int lx = x0;
    int ly = rc->top + 8;
    for (int m = 0; m < 4; m++) {
        HPEN lp = CreatePen(PS_SOLID, 2, kModeColors[m]);
        HGDIOBJ op = SelectObject(hdc, lp);
        MoveToEx(hdc, lx, ly + 6, NULL);
        LineTo(hdc, lx + 16, ly + 6);
        SelectObject(hdc, op);
        DeleteObject(lp);
        draw_circle_marker(hdc, lx + 8, ly + 6, 3, kModeColors[m]);

        SetTextColor(hdc, st->bench[m].has_data ? RGB(40, 48, 60) : RGB(165, 172, 182));
        TextOutA(hdc, lx + 21, ly, kModeLabels[m], (int)strlen(kModeLabels[m]));
        lx += 108;
    }

    /* Luoi ngang & truc Y (Attempts) */
    HPEN grid_pen = CreatePen(PS_SOLID, 1, RGB(234, 238, 242));
    HPEN axis_pen = CreatePen(PS_SOLID, 1, RGB(130, 140, 152));
    HGDIOBJ old_pen = SelectObject(hdc, grid_pen);

    const int y_ticks = 4;
    for (int i = 0; i <= y_ticks; i++) {
        int py = y0 - (plot_h * i) / y_ticks;
        if (i > 0) {
            MoveToEx(hdc, x0, py, NULL);
            LineTo(hdc, x1, py);
        }
        char lbl[32];
        format_attempts_short((max_a * i) / y_ticks, lbl, sizeof(lbl));
        RECT tr = { rc->left + 2, py - 8, x0 - 6, py + 8 };
        SetTextColor(hdc, RGB(95, 105, 118));
        DrawTextA(hdc, lbl, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    /* Luoi doc & truc X (Time) */
    const int x_ticks = 4;
    for (int i = 0; i <= x_ticks; i++) {
        int px = x0 + (plot_w * i) / x_ticks;
        if (i > 0) {
            MoveToEx(hdc, px, y1, NULL);
            LineTo(hdc, px, y0);
        }
        char lbl[32];
        format_time_short((max_t * i) / x_ticks, max_t, lbl, sizeof(lbl));
        RECT tr = { px - 34, y0 + 3, px + 34, y0 + 18 };
        SetTextColor(hdc, RGB(95, 105, 118));
        DrawTextA(hdc, lbl, -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    /* Tieu de truc X */
    RECT xtitle = { x0, y0 + 17, x1, rc->bottom - 1 };
    SetTextColor(hdc, RGB(80, 90, 105));
    DrawTextA(hdc, "Elapsed Time (steep slope = faster cracking speed)", -1, &xtitle, DT_CENTER | DT_SINGLELINE);

    /* Ve 2 truc toa do */
    SelectObject(hdc, axis_pen);
    MoveToEx(hdc, x0, y1, NULL);
    LineTo(hdc, x0, y0);
    LineTo(hdc, x1, y0);
    SelectObject(hdc, old_pen);
    DeleteObject(grid_pen);
    DeleteObject(axis_pen);

    /* Ve duong tang truong cua tung phuong phap */
    for (int m = 0; m < 4; m++) {
        if (!st->bench[m].has_data) continue;
        const ModeTrace *tr = &st->bench[m].trace;

        POINT pts[MAX_TRACE_SAMPLES + 2];
        int npts = 0;

        if (tr->count >= 2) {
            for (int k = 0; k < tr->count && npts < MAX_TRACE_SAMPLES; k++) {
                double fx = tr->samples[k].time_sec / max_t;
                double fy = (double)tr->samples[k].attempts / max_a;
                if (fx < 0.0) fx = 0.0;
                if (fx > 1.0) fx = 1.0;
                if (fy < 0.0) fy = 0.0;
                if (fy > 1.0) fy = 1.0;
                pts[npts].x = x0 + (int)(fx * plot_w + 0.5);
                pts[npts].y = y0 - (int)(fy * plot_h + 0.5);
                npts++;
            }
        } else {
            pts[0].x = x0;
            pts[0].y = y0;
            double fx = st->bench[m].elapsed_sec / max_t;
            double fy = (double)st->bench[m].attempts / max_a;
            pts[1].x = x0 + (int)(fx * plot_w + 0.5);
            pts[1].y = y0 - (int)(fy * plot_h + 0.5);
            npts = 2;
        }

        if (npts >= 2) {
            HPEN line_pen = CreatePen(PS_SOLID, 2, kModeColors[m]);
            HGDIOBJ op = SelectObject(hdc, line_pen);
            Polyline(hdc, pts, npts);
            SelectObject(hdc, op);
            DeleteObject(line_pen);

            /* Cham diem o diem ket thuc (va cac diem giua neu it diem) */
            if (npts <= 12) {
                for (int k = 1; k < npts; k++) {
                    draw_circle_marker(hdc, pts[k].x, pts[k].y, (k == npts - 1) ? 4 : 2, kModeColors[m]);
                }
            } else {
                draw_circle_marker(hdc, pts[npts - 1].x, pts[npts - 1].y, 4, kModeColors[m]);
            }
        }
    }

    SelectObject(hdc, old_font);
}

/* ----------------------------------------------------------------------------
 * Che do 2: Do thi duong so sanh Speedup (x) & Efficiency (%) cua 4 phuong phap
 * ---------------------------------------------------------------------------- */
static void paint_speedup_chart(HDC hdc, const RECT *rc, const ChartState *st) {
    int has_any = 0;
    for (int m = 0; m < 4; m++) {
        if (st->bench[m].has_data) has_any = 1;
    }
    if (!has_any) {
        draw_empty_state(hdc, rc, st->font_small);
        return;
    }

    int has_seq    = st->bench[EXEC_MODE_SEQUENTIAL].has_data;
    double seq_sec = st->bench[EXEC_MODE_SEQUENTIAL].elapsed_sec;
    int threads    = (st->threads > 0) ? st->threads : 1;

    double spd[4] = { 0 };
    double eff[4] = { 0 };
    int valid[4]  = { 0 };
    double max_spd = (double)threads;
    if (max_spd < 2.0) max_spd = 2.0;

    for (int m = 0; m < 4; m++) {
        if (!st->bench[m].has_data) continue;
        if (m == EXEC_MODE_SEQUENTIAL) {
            spd[m] = 1.0;
            eff[m] = 100.0;
            valid[m] = 1;
        } else if (has_seq && st->bench[m].elapsed_sec > 1e-7 && seq_sec > 1e-7) {
            spd[m] = seq_sec / st->bench[m].elapsed_sec;
            eff[m] = (spd[m] / (double)threads) * 100.0;
            valid[m] = 1;
            if (spd[m] > max_spd) max_spd = spd[m];
        }
    }

    double max_eff = 120.0;
    for (int m = 0; m < 4; m++) {
        if (valid[m] && eff[m] > max_eff - 10.0) {
            max_eff = ceil((eff[m] + 15.0) / 20.0) * 20.0;
        }
    }
    max_spd *= 1.15;

    const int pad_l = 48;
    const int pad_r = 48;
    const int pad_t = 30;
    const int pad_b = 28;

    int x0 = rc->left + pad_l;
    int y0 = rc->bottom - pad_b;
    int x1 = rc->right - pad_r;
    int y1 = rc->top + pad_t;
    int plot_w = x1 - x0;
    int plot_h = y0 - y1;
    if (plot_w < 60 || plot_h < 40) return;

    const COLORREF col_spd = RGB(13, 110, 253);  /* Xanh duong: Speedup */
    const COLORREF col_eff = RGB(245, 130, 32);  /* Cam: Efficiency */

    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ old_font = SelectObject(hdc, st->font_small);

    /* Legend tren cung */
    int ly = rc->top + 8;
    HPEN p1 = CreatePen(PS_SOLID, 2, col_spd);
    HGDIOBJ op = SelectObject(hdc, p1);
    MoveToEx(hdc, x0, ly + 6, NULL);
    LineTo(hdc, x0 + 18, ly + 6);
    SelectObject(hdc, op);
    DeleteObject(p1);
    draw_circle_marker(hdc, x0 + 9, ly + 6, 3, col_spd);
    SetTextColor(hdc, col_spd);
    TextOutA(hdc, x0 + 24, ly, "Speedup (x) [Left]", 18);

    int lx2 = x0 + 165;
    HPEN p2 = CreatePen(PS_SOLID, 2, col_eff);
    op = SelectObject(hdc, p2);
    MoveToEx(hdc, lx2, ly + 6, NULL);
    LineTo(hdc, lx2 + 18, ly + 6);
    SelectObject(hdc, op);
    DeleteObject(p2);
    draw_square_marker(hdc, lx2 + 9, ly + 6, 3, col_eff);
    SetTextColor(hdc, col_eff);
    TextOutA(hdc, lx2 + 24, ly, "Efficiency (%) [Right]", 22);

    /* Grid & 2 truc Y */
    HPEN grid_pen = CreatePen(PS_SOLID, 1, RGB(234, 238, 242));
    HPEN axis_pen = CreatePen(PS_SOLID, 1, RGB(130, 140, 152));
    op = SelectObject(hdc, grid_pen);

    const int y_ticks = 4;
    for (int i = 0; i <= y_ticks; i++) {
        int py = y0 - (plot_h * i) / y_ticks;
        if (i > 0) {
            MoveToEx(hdc, x0, py, NULL);
            LineTo(hdc, x1, py);
        }

        char lbl_l[24], lbl_r[24];
        snprintf(lbl_l, sizeof(lbl_l), "%.1fx", (max_spd * i) / y_ticks);
        snprintf(lbl_r, sizeof(lbl_r), "%.0f%%", (max_eff * i) / y_ticks);

        RECT rl = { rc->left + 2, py - 8, x0 - 5, py + 8 };
        SetTextColor(hdc, col_spd);
        DrawTextA(hdc, lbl_l, -1, &rl, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        RECT rr = { x1 + 5, py - 8, rc->right - 2, py + 8 };
        SetTextColor(hdc, col_eff);
        DrawTextA(hdc, lbl_r, -1, &rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* Truc toa do */
    SelectObject(hdc, axis_pen);
    MoveToEx(hdc, x0, y1, NULL);
    LineTo(hdc, x0, y0);
    LineTo(hdc, x1, y0);
    LineTo(hdc, x1, y1);
    SelectObject(hdc, op);
    DeleteObject(grid_pen);
    DeleteObject(axis_pen);

    /* Toa do X cho 4 phuong phap */
    int xs[4];
    for (int m = 0; m < 4; m++) {
        xs[m] = x0 + (plot_w * (2 * m + 1)) / 8;
        RECT tr = { xs[m] - 42, y0 + 5, xs[m] + 42, rc->bottom - 2 };
        SetTextColor(hdc, RGB(50, 58, 70));
        DrawTextA(hdc, kModeLabels[m], -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    POINT spd_pts[4], eff_pts[4];
    int n_valid = 0;
    int valid_idx[4];

    for (int m = 0; m < 4; m++) {
        if (!valid[m]) continue;
        double fy_s = spd[m] / max_spd;
        double fy_e = eff[m] / max_eff;
        if (fy_s > 1.0) fy_s = 1.0;
        if (fy_e > 1.0) fy_e = 1.0;

        spd_pts[n_valid].x = xs[m];
        spd_pts[n_valid].y = y0 - (int)(fy_s * plot_h + 0.5);
        eff_pts[n_valid].x = xs[m];
        eff_pts[n_valid].y = y0 - (int)(fy_e * plot_h + 0.5);
        valid_idx[n_valid] = m;
        n_valid++;
    }

    if (n_valid >= 2) {
        HPEN ps = CreatePen(PS_SOLID, 2, col_spd);
        op = SelectObject(hdc, ps);
        Polyline(hdc, spd_pts, n_valid);
        SelectObject(hdc, op);
        DeleteObject(ps);

        HPEN pe = CreatePen(PS_SOLID, 2, col_eff);
        op = SelectObject(hdc, pe);
        Polyline(hdc, eff_pts, n_valid);
        SelectObject(hdc, op);
        DeleteObject(pe);
    }

    for (int i = 0; i < n_valid; i++) {
        int m = valid_idx[i];
        draw_circle_marker(hdc, spd_pts[i].x, spd_pts[i].y, 4, col_spd);
        draw_square_marker(hdc, eff_pts[i].x, eff_pts[i].y, 4, col_eff);

        char val_s[24], val_e[24];
        snprintf(val_s, sizeof(val_s), "%.2fx", spd[m]);
        snprintf(val_e, sizeof(val_e), "%.0f%%", eff[m]);

        /* Dat nhan gia tri tranh de len nhau */
        int dy_s = (spd_pts[i].y <= eff_pts[i].y) ? -17 : 7;
        int dy_e = (spd_pts[i].y <= eff_pts[i].y) ? 7 : -17;

        RECT rs = { spd_pts[i].x - 28, spd_pts[i].y + dy_s, spd_pts[i].x + 28, spd_pts[i].y + dy_s + 14 };
        SetTextColor(hdc, col_spd);
        DrawTextA(hdc, val_s, -1, &rs, DT_CENTER | DT_SINGLELINE);

        RECT re = { eff_pts[i].x - 28, eff_pts[i].y + dy_e, eff_pts[i].x + 28, eff_pts[i].y + dy_e + 14 };
        SetTextColor(hdc, col_eff);
        DrawTextA(hdc, val_e, -1, &re, DT_CENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, old_font);
}

static LRESULT CALLBACK chart_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ChartState *st = (ChartState *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            st = (ChartState *)calloc(1, sizeof(ChartState));
            if (st) {
                st->threads = 1;
                st->view_mode = CHART_MODE_GROWTH;
                st->font_small = CreateFontA(
                    -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
                );
                st->font_bold = CreateFontA(
                    -12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
                );
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)st);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1; /* Double-buffer trong WM_PAINT, khong nhap nhay */

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            if (w > 0 && h > 0) {
                HDC mem_dc = CreateCompatibleDC(hdc);
                HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
                HGDIOBJ old_bmp = SelectObject(mem_dc, bmp);

                /* Nen trang + vien nhat */
                HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(mem_dc, &rc, bg);
                DeleteObject(bg);

                HPEN border = CreatePen(PS_SOLID, 1, RGB(205, 212, 220));
                HGDIOBJ op = SelectObject(mem_dc, border);
                HGDIOBJ ob = SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
                Rectangle(mem_dc, 0, 0, w, h);
                SelectObject(mem_dc, ob);
                SelectObject(mem_dc, op);
                DeleteObject(border);

                if (st) {
                    if (st->view_mode == CHART_MODE_SPEEDUP) {
                        paint_speedup_chart(mem_dc, &rc, st);
                    } else {
                        paint_growth_chart(mem_dc, &rc, st);
                    }
                }

                BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);
                SelectObject(mem_dc, old_bmp);
                DeleteObject(bmp);
                DeleteDC(mem_dc);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            if (st) {
                if (st->font_small) DeleteObject(st->font_small);
                if (st->font_bold)  DeleteObject(st->font_bold);
                free(st);
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            }
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int chart_register_class(HINSTANCE hInstance) {
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = chart_wnd_proc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = CHART_WND_CLASS;
    return (RegisterClassExA(&wc) != 0);
}

void chart_set_data(HWND hwnd_chart, const BenchmarkEntry bench[4], int threads, ChartViewMode view_mode) {
    if (!hwnd_chart) return;
    ChartState *st = (ChartState *)GetWindowLongPtrA(hwnd_chart, GWLP_USERDATA);
    if (!st) return;

    memcpy(st->bench, bench, sizeof(st->bench));
    st->threads   = (threads > 0) ? threads : 1;
    st->view_mode = view_mode;

    InvalidateRect(hwnd_chart, NULL, FALSE);
}
