#ifndef CHART_H
#define CHART_H

#include <windows.h>
#include "config.h"

#define CHART_WND_CLASS "BruteForceChartCanvas"

typedef enum {
    CHART_MODE_GROWTH  = 0,  /* Do thi duong toc do tang truong: Attempts vs Time (4 phuong phap) */
    CHART_MODE_SPEEDUP = 1   /* Do thi duong so sanh Speedup (x) & Efficiency (%) */
} ChartViewMode;

/* Dang ky window class cho control do thi GDI */
int chart_register_class(HINSTANCE hInstance);

/* Cap nhat du lieu benchmark + che do xem va ve lai do thi */
void chart_set_data(HWND hwnd_chart, const BenchmarkEntry bench[4], int threads, ChartViewMode view_mode);

#endif /* CHART_H */
