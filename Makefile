CC        := gcc
WINDRES   := windres
CFLAGS    := -Wall -Wextra -O2 -fopenmp -Iinclude
GUI_LIBS  := -mwindows -lcomctl32 -lgdi32 -luser32

# Link tinh libgomp + libwinpthread + wrap gomp_aligned_alloc/free de tranh:
# 1) Bug GCC 12.1.0 MinGW-w64 (PR105836: _aligned_malloc / free mismatch -> 0xC0000374)
# 2) Xung dot libwinpthread-1.dll tu Anaconda trong PATH
CORE_LIBS := -static-libgcc -Wl,--wrap=gomp_aligned_alloc -Wl,--wrap=gomp_aligned_free \
             -Wl,-Bstatic -lgomp -ldl -lpthread -Wl,-Bdynamic

# Bat OpenSSL khi may da cai libcrypto:
#   mingw32-make USE_OPENSSL=1
# Hoac chi dinh duong dan OpenSSL tuy chon:
#   mingw32-make USE_OPENSSL=1 OPENSSL_DIR="C:/OpenSSL-Win64"
ifeq ($(USE_OPENSSL),1)
    CFLAGS += -DUSE_OPENSSL -Wno-deprecated-declarations
    ifdef OPENSSL_DIR
        CFLAGS    += -I"$(OPENSSL_DIR)/include"
        CORE_LIBS += -L"$(OPENSSL_DIR)/lib"
    endif
    CORE_LIBS += -lcrypto
endif

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/bruteforce_app.exe
TEST_BIN  := $(BUILD_DIR)/test_core.exe

APP_SRCS  := src/main.c src/ui.c src/chart.c src/worker.c src/brute_force.c src/hash_utils.c src/gomp_mingw_fix.c
APP_OBJS  := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
RES_OBJ   := $(BUILD_DIR)/app.res

.PHONY: all clean run test

all: $(TARGET)

$(BUILD_DIR):
	if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(BUILD_DIR)/%.o: src/%.c include/config.h include/hash_utils.h include/brute_force.h include/worker.h include/chart.h include/ui.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(RES_OBJ): resources/app.rc resources/app.manifest | $(BUILD_DIR)
	$(WINDRES) -Iresources resources/app.rc -O coff -o $@

$(TARGET): $(APP_OBJS) $(RES_OBJ)
	$(CC) $(APP_OBJS) $(RES_OBJ) -o $@ $(GUI_LIBS) $(CORE_LIBS)

test: $(BUILD_DIR)/brute_force.o $(BUILD_DIR)/hash_utils.o $(BUILD_DIR)/gomp_mingw_fix.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) tests/test_core.c $(BUILD_DIR)/brute_force.o $(BUILD_DIR)/hash_utils.o $(BUILD_DIR)/gomp_mingw_fix.o -o $(TEST_BIN) $(CORE_LIBS)
	$(TEST_BIN)

run: $(TARGET)
	$(TARGET)

clean:
	if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
