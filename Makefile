# MIT License
# Safer builds by default. Release (-O2 -DNDEBUG), Debug (-O0 -g3 -DDEBUG).

BASH_INC    ?= /usr/include/bash
INCLUDE_DIR ?= include
EXTRA_INC   ?=
BUILD_DIR   ?= build
SRC_DIR     := src

OBJDIR       := $(BUILD_DIR)/obj
DEBUG_OBJDIR := $(BUILD_DIR)/obj.debug
ASAN_OBJDIR  := $(BUILD_DIR)/obj.asan

SOURCES := $(SRC_DIR)/socket_builtin.c
HEADERS := $(INCLUDE_DIR)/argparse.h $(INCLUDE_DIR)/util.h

TARGET       := $(BUILD_DIR)/socket.so
DEBUG_TARGET := $(BUILD_DIR)/socket.debug.so
ASAN_TARGET  := $(BUILD_DIR)/socket.asan.so

CC ?= cc

# Warnings & hardening (works on modern GCC/glibc)
WARN := -Wall -Wextra -Werror -Wformat=2 -Wformat-security -Wvla \
        -Wshadow -Wpointer-arith -Wcast-align -Wstrict-prototypes \
        -Wmissing-prototypes -Wmissing-declarations -Wduplicated-cond \
        -Wlogical-op -Wnull-dereference -Wwrite-strings -Wcast-qual

HARDEN_CFLAGS  := -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fno-plt \
                  -fstack-clash-protection -fcf-protection -MMD -MP
HARDEN_LDFLAGS := -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack

INC  := -I$(BASH_INC) -I$(INCLUDE_DIR) $(addprefix -I,$(EXTRA_INC)) -I/home/opsman/bash/include -I/home/opsman/bash

DEFS := -D_GNU_SOURCE -DBUILTIN

BASE_CFLAGS  := $(DEFS) -fPIC $(WARN) $(HARDEN_CFLAGS) $(INC) $(EXTRA_CFLAGS)
BASE_LDFLAGS := -shared $(HARDEN_LDFLAGS) $(EXTRA_LDFLAGS)

# Profiles
CFLAGS_RELEASE := -O2 -DNDEBUG
CFLAGS_DEBUG   := -O0 -g3 -DDEBUG
CFLAGS_ASAN    := -O1 -g3 -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS_ASAN   := -fsanitize=address,undefined

# Derived object lists
OBJ_RELEASE := $(SOURCES:$(SRC_DIR)/%.c=$(OBJDIR)/%.o)
OBJ_DEBUG   := $(SOURCES:$(SRC_DIR)/%.c=$(DEBUG_OBJDIR)/%.o)
OBJ_ASAN    := $(SOURCES:$(SRC_DIR)/%.c=$(ASAN_OBJDIR)/%.o)

.PHONY: all debug asan clean test print-flags

all: $(TARGET)
debug: $(DEBUG_TARGET)
asan: $(ASAN_TARGET)

$(BUILD_DIR) $(OBJDIR) $(DEBUG_OBJDIR) $(ASAN_OBJDIR):
	@mkdir -p $@

# Release ----------------------------------------------------------------------
$(TARGET): $(OBJ_RELEASE) | $(BUILD_DIR)
	$(CC) $(BASE_LDFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(OBJDIR)
	$(CC) $(BASE_CFLAGS) $(CFLAGS_RELEASE) -c $< -o $@

# Debug ------------------------------------------------------------------------
$(DEBUG_TARGET): $(OBJ_DEBUG) | $(BUILD_DIR)
	$(CC) $(BASE_LDFLAGS) -o $@ $^

$(DEBUG_OBJDIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(DEBUG_OBJDIR)
	$(CC) $(BASE_CFLAGS) $(CFLAGS_DEBUG) -c $< -o $@

# ASAN/UBSAN -------------------------------------------------------------------
$(ASAN_TARGET): $(OBJ_ASAN) | $(BUILD_DIR)
	$(CC) $(BASE_LDFLAGS) $(LDFLAGS_ASAN) -o $@ $^

$(ASAN_OBJDIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(ASAN_OBJDIR)
	$(CC) $(BASE_CFLAGS) $(CFLAGS_ASAN) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

# Tests use release .so
test: all
	@bash -lc 'enable -f ./build/socket.so socket; \
		if command -v bats >/dev/null 2>&1; then bats tests/mvp.bats; else bash tests/mvp.bats; fi'

print-flags:
	@echo "CC=$(CC)"
	@echo "BASE_CFLAGS=$(BASE_CFLAGS)"
	@echo "BASE_LDFLAGS=$(BASE_LDFLAGS)"
