# Password Brute Force App (C + OpenMP + Win32 API)

> **[KHUNG CODE / FRAMEWORK]**
> Đây là khung code chạy hoàn chỉnh cho ứng dụng Brute Force mật khẩu bằng **C + OpenMP + Win32 API**.
> Phần giao diện và phần logic đã được tách riêng hoàn toàn để **Quang Anh** có thể dễ dàng đọc, kiểm tra và tùy chỉnh thuật toán mà không cần đụng vào code UI.

---

## 1. Cấu trúc thư mục

```text
app/
├── Makefile                    # Biên dịch nhanh bằng MinGW-w64 (mingw32-make)
├── README.md                   # Hướng dẫn build & ghi chú phân chia module
├── include/
│   ├── config.h                # Struct cấu hình, kết quả, trạng thái, benchmark, trace đồ thị
│   ├── hash_utils.h            # Interface tính MD5 / SHA1 & đổi Hex <-> Bytes
│   ├── brute_force.h           # Hợp đồng API giữa UI/Worker và Core Logic OpenMP (+ Auto Threads)
│   ├── worker.h                # Worker thread nền (tránh treo giao diện khi chạy)
│   ├── chart.h                 # Control đồ thị đường Win32 GDI (Growth Curve & Speedup/Efficiency)
│   └── ui.h                    # Control IDs & entry point giao diện Win32
├── src/
│   ├── main.c                  # Điểm vào WinMain
│   ├── ui.c                    # Giao diện Win32 thuần (5 khu vực: Input, OpenMP, Result, Table, Chart)
│   ├── chart.c                 # Vẽ đồ thị đường GDI double-buffered cho 4 phương pháp
│   ├── worker.c                # Điều phối chạy 1 mode hoặc Benchmark cả 4 mode
│   ├── hash_utils.c            # Tính MD5 / SHA1 bằng OpenSSL và đổi Hex <-> Bytes
│   └── brute_force.c           # [QUANG ANH CHECK/SỬA] Index Mapping, 4 mode OpenMP & Auto Threads
├── resources/
│   ├── app.manifest            # Bật Windows Visual Styles (Common Controls v6)
│   └── app.rc                  # Resource nhúng manifest
└── tests/
    └── test_core.c             # Unit test + Benchmark nhanh phần core trên console
```

---

## 2. Cách build và chạy (nhanh gọn trên Windows + MinGW)

Mở terminal trong thư mục `app`:

```powershell
# 1. Build app giao diện (.exe)
mingw32-make

# 2. Chạy app
mingw32-make run

# 3. Chạy test nhanh phần core (Hash + Index Mapping + 4 mode OpenMP)
mingw32-make test

# 4. Xóa thư mục build
mingw32-make clean
```

### OpenSSL đi kèm dự án

Với MinGW-w64 x64, `mingw32-make` dùng OpenSSL đi kèm trong `libs/openssl` và tự sao chép `libcrypto-4-x64.dll` vào `build/` cạnh file `.exe`.

---

## 3. Ghi chú cho Quang Anh (phần Core Logic)

Toàn bộ logic thuật toán nằm gói gọn trong **`src/brute_force.c`** (khai báo tại `include/brute_force.h`):

1. **`index_to_password(...)`**: Ánh xạ số nguyên `index` sang mật khẩu theo hệ cơ số $K$ (trang 3–4 tài liệu).
2. **`hash_password(...)` → `check_password(...)` → `save_result(...)`**: Luồng xử lý từng ứng viên (trang 6), so sánh bằng `memcmp` trên mảng byte thay vì chuỗi hex để tối ưu tốc độ.
3. **`run_length_sequential(...)` & `run_length_parallel(...)`**: 4 chế độ `Sequential`, `Static`, `Dynamic`, `Guided` với cờ dừng sớm an toàn (`found_flag` + `stop_requested`) và bộ đếm `local_attempts` theo từng luồng để tránh nghẽn atomic/false sharing.
4. Sau khi chỉnh sửa `src/brute_force.c`, chỉ cần chạy `mingw32-make test` để kiểm chứng lại độ chính xác và xem bảng đo Speedup/Efficiency ngay trên terminal, rồi chạy `mingw32-make run` để mở giao diện.
