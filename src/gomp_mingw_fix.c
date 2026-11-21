#include <stdlib.h>

/* ============================================================================
 * Workaround cho bug GCC 12.1.0 MinGW-w64 msvcrt (GCC PR105836):
 *   Trong libgomp.a cua GCC 12.1.0, gomp_aligned_alloc() goi _aligned_malloc()
 *   nhung gomp_free_thread() / gomp_team_end() lai goi free() thuong, gay crash
 *   0xC0000374 (STATUS_HEAP_CORRUPTION) ngay khi worker thread hoac pool OpenMP
 *   giai phong bo nho.
 *
 * Khi link voi:
 *   -Wl,--wrap=gomp_aligned_alloc -Wl,--wrap=gomp_aligned_free
 * ca cap alloc/free deu dong nhat dung malloc()/free() (tren Windows x64,
 * malloc luon can le 16-byte), loai bo hoan toan loi crash nay.
 * ============================================================================ */

void *__wrap_gomp_aligned_alloc(size_t al, size_t size) {
    (void)al;
    if (size == 0) {
        size = 1;
    }
    void *ptr = malloc(size);
    if (!ptr) {
        abort();
    }
    return ptr;
}

void __wrap_gomp_aligned_free(void *ptr) {
    free(ptr);
}
