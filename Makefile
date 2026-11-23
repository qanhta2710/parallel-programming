CC        := gcc
WINDRES   := windres
CFLAGS    := -Wall -Wextra -O2 -fopenmp -Iinclude
GUI_LIBS  := -mwindows -lcomctl32 -lgdi32 -luser32

# Link tinh libgomp va libwinpthread de tranh xung dot DLL trong PATH.
CORE_LIBS := -static-libgcc -Wl,-Bstatic -lgomp -ldl -lpthread -Wl,-Bdynamic

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/bruteforce_app.exe
TEST_BIN  := $(BUILD_DIR)/test_core.exe

APP_SRCS  := src/main.c src/ui.c src/chart.c src/worker.c src/brute_force.c src/hash_utils.c
APP_OBJS  := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
TEST_OBJS := $(addprefix $(BUILD_DIR)/,brute_force.o hash_utils.o)
RES_OBJ   := $(BUILD_DIR)/app.res
GUI_RES   := $(RES_OBJ)

# GCC co manifest mac dinh se link manifest cua du an qua duong dan -B.
ifneq ($(shell $(CC) -print-file-name=default-manifest.o),default-manifest.o)
    RES_OBJ := $(BUILD_DIR)/default-manifest.o
    GUI_RES := -B"$(abspath $(BUILD_DIR))/"
endif

OPENSSL_DIR ?= libs/openssl
OPENSSL_CONFIG := $(BUILD_DIR)/openssl-dir.txt

empty :=
space := $(empty) $(empty)
openssl_prereq_dir := $(subst $(space),\$(space),$(OPENSSL_DIR))
OPENSSL_HEADERS := $(addprefix $(openssl_prereq_dir)/include/openssl/,md5.h sha.h)
OPENSSL_IMPORT := $(openssl_prereq_dir)/lib/libcrypto.dll.a
OPENSSL_DLL := libcrypto-4-x64.dll
OPENSSL_RUNTIME := $(BUILD_DIR)/$(OPENSSL_DLL)
OPENSSL_CFLAGS := -Wno-deprecated-declarations -I"$(OPENSSL_DIR)/include"
CORE_LIBS += "$(OPENSSL_DIR)/lib/libcrypto.dll.a"

.PHONY: all clean run test FORCE

all: $(TARGET)

$(BUILD_DIR):
	if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(OPENSSL_RUNTIME): $(openssl_prereq_dir)/bin/$(OPENSSL_DLL) | $(BUILD_DIR)
	copy /y /b "$(subst /,\,$<)" "$(subst /,\,$@)" >nul

ifneq ($(file <$(OPENSSL_CONFIG)),"$(OPENSSL_DIR)")
$(OPENSSL_CONFIG) $(OPENSSL_RUNTIME): FORCE
endif

$(OPENSSL_CONFIG): | $(BUILD_DIR) $(OPENSSL_RUNTIME)
	@> "$@" echo "$(OPENSSL_DIR)"

$(BUILD_DIR)/hash_utils.o: CFLAGS += $(OPENSSL_CFLAGS)
$(BUILD_DIR)/hash_utils.o: $(OPENSSL_CONFIG) $(OPENSSL_HEADERS) Makefile

$(BUILD_DIR)/%.o: src/%.c include/config.h include/hash_utils.h include/brute_force.h include/worker.h include/chart.h include/ui.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(RES_OBJ): resources/app.rc resources/app.manifest | $(BUILD_DIR)
	$(WINDRES) -Iresources resources/app.rc -O coff -o $@

$(TARGET): $(APP_OBJS) $(RES_OBJ) $(OPENSSL_IMPORT) Makefile | $(OPENSSL_RUNTIME)
	$(CC) $(APP_OBJS) $(GUI_RES) -o $@ $(GUI_LIBS) $(CORE_LIBS)

test: $(TEST_OBJS) $(OPENSSL_IMPORT) | $(OPENSSL_RUNTIME)
	$(CC) $(CFLAGS) tests/test_core.c $(TEST_OBJS) -o $(TEST_BIN) $(CORE_LIBS)
	$(TEST_BIN)

run: $(TARGET)
	$(TARGET)

clean:
	if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
