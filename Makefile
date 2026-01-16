# ============================================================================
# georisk - Geometric Risk Analysis Library
# ============================================================================
# Usage:
#   make help
#   make clean && make CC=clang WERROR=1
#   make clean && make DEBUG=1 SAN=1 CC=clang test
# ============================================================================

NAME        := georisk
VERSION     := 0.1.0

SRC_DIR     := src
INC_DIR     := include
TEST_DIR    := tests
BUILD_DIR   := build
OBJ_DIR     := $(BUILD_DIR)/obj
LIB_DIR     := $(BUILD_DIR)/lib
BIN_DIR     := $(BUILD_DIR)/bin

PREFIX      ?= /usr/local
USER_LIB    := $(HOME)/libraries

# Toolchain (overrideable)
CC          ?= gcc
AR          ?= ar

# Build knobs
STRICT      ?= 1   # 1 = extra warnings on (recommended)
WERROR      ?= 0   # 1 = treat warnings as errors
SAN         ?= 0   # 1 = enable ASAN/UBSAN (best with DEBUG=1)

# ----------------------------------------------------------------------------
# Flags
# ----------------------------------------------------------------------------

CFLAGS      := -std=c11 -fPIC -I$(INC_DIR)

# Baseline warnings
WARNFLAGS   := -Wall -Wextra -Wpedantic

# Stricter warnings (portable subset across gcc/clang)
ifeq ($(STRICT),1)
WARNFLAGS   += \
	-Wshadow \
	-Wconversion -Wsign-conversion \
	-Wformat=2 -Wundef \
	-Wstrict-prototypes -Wmissing-prototypes \
	-Wold-style-definition \
	-Wcast-align -Wcast-qual \
	-Wpointer-arith \
	-Wvla \
	-Wnull-dereference \
	-Wdouble-promotion \
	-Wswitch-enum
endif

ifeq ($(WERROR),1)
WARNFLAGS   += -Werror
endif

# Security hardening (compile-time)
HARDEN_CFLAGS := -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fno-common

ifdef DEBUG
    CFLAGS  += -g -O0 -DDEBUG
else
    CFLAGS  += -O2 -DNDEBUG
endif

# Link libs
LDLIBS      := -lm -ldl -lpthread

# Sanitizers (best with DEBUG=1)
ifeq ($(SAN),1)
    CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
    LDLIBS  += -fsanitize=address,undefined
endif

# Final flags
CFLAGS      += $(WARNFLAGS) $(HARDEN_CFLAGS)

# Hardened shared-library link flags (ELF platforms)
LDFLAGS_SHARED := -Wl,-z,relro -Wl,-z,now

LIB_SHARED  := lib$(NAME).so
LIB_STATIC  := lib$(NAME).a

# ----------------------------------------------------------------------------
# Sources / Objects
# ----------------------------------------------------------------------------

CORE_SRCS       := $(wildcard $(SRC_DIR)/core/*.c)
ANALYSIS_SRCS   := $(wildcard $(SRC_DIR)/analysis/*.c)
TRANSPORT_SRCS  := $(wildcard $(SRC_DIR)/transport/*.c)
BRIDGE_SRCS     := $(wildcard $(SRC_DIR)/bridge/*.c)

SRCS            := $(CORE_SRCS) $(ANALYSIS_SRCS) $(TRANSPORT_SRCS) $(BRIDGE_SRCS)

CORE_OBJS       := $(patsubst $(SRC_DIR)/core/%.c,$(OBJ_DIR)/core/%.o,$(CORE_SRCS))
ANALYSIS_OBJS   := $(patsubst $(SRC_DIR)/analysis/%.c,$(OBJ_DIR)/analysis/%.o,$(ANALYSIS_SRCS))
TRANSPORT_OBJS  := $(patsubst $(SRC_DIR)/transport/%.c,$(OBJ_DIR)/transport/%.o,$(TRANSPORT_SRCS))
BRIDGE_OBJS     := $(patsubst $(SRC_DIR)/bridge/%.c,$(OBJ_DIR)/bridge/%.o,$(BRIDGE_SRCS))

OBJS            := $(CORE_OBJS) $(ANALYSIS_OBJS) $(TRANSPORT_OBJS) $(BRIDGE_OBJS)

# Tests
TEST_SRCS       := $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS       := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/test/%.o,$(TEST_SRCS))
TEST_BIN        := $(BIN_DIR)/test_runner

UNITY_SRC       := $(TEST_DIR)/unity/unity.c
UNITY_OBJ       := $(OBJ_DIR)/test/unity.o

# ----------------------------------------------------------------------------
# Targets
# ----------------------------------------------------------------------------

.PHONY: all build shared static test clean dirs info install help gcc clang

all: shared

# Alias
build: shared

# Convenience toolchain targets
gcc:
	@$(MAKE) CC=gcc

clang:
	@$(MAKE) CC=clang

help:
	@echo ""
	@echo "georisk Makefile targets"
	@echo "-----------------------"
	@echo "  make (or make all)           Build shared library (default)"
	@echo "  make build                   Alias for 'make shared'"
	@echo "  make shared                  Build $(LIB_SHARED) -> $(LIB_DIR) and copy to $(USER_LIB)"
	@echo "  make static                  Build $(LIB_STATIC) -> $(LIB_DIR) and copy to $(USER_LIB)"
	@echo "  make test                    Build and run tests"
	@echo "  make clean                   Remove build artifacts (rm -rf $(BUILD_DIR))"
	@echo "  make install                 Install to PREFIX (default: $(PREFIX))"
	@echo "  make info                    Print build configuration"
	@echo ""
	@echo "Toolchain selection:"
	@echo "  make CC=clang                Build with clang"
	@echo "  make CC=gcc                  Build with gcc"
	@echo "  make clang                   Convenience target (CC=clang)"
	@echo "  make gcc                     Convenience target (CC=gcc)"
	@echo ""
	@echo "Strictness / safety knobs:"
	@echo "  make STRICT=1                Extra warnings (default: 1)"
	@echo "  make WERROR=1                Treat warnings as errors"
	@echo "  make DEBUG=1                 Debug build (-g -O0 -DDEBUG)"
	@echo "  make SAN=1                   ASAN+UBSAN (adds sanitizer link flags)"
	@echo ""
	@echo "Examples:"
	@echo "  make clean && make CC=clang WERROR=1"
	@echo "  make clean && make DEBUG=1 SAN=1 CC=clang test"
	@echo ""

dirs:
	@mkdir -p $(OBJ_DIR)/core
	@mkdir -p $(OBJ_DIR)/analysis
	@mkdir -p $(OBJ_DIR)/transport
	@mkdir -p $(OBJ_DIR)/bridge
	@mkdir -p $(OBJ_DIR)/test
	@mkdir -p $(LIB_DIR)
	@mkdir -p $(BIN_DIR)

shared: dirs $(LIB_DIR)/$(LIB_SHARED)
	@mkdir -p $(USER_LIB)
	@cp $(LIB_DIR)/$(LIB_SHARED) $(USER_LIB)/
	@echo "  COPY  $(USER_LIB)/$(LIB_SHARED)"

static: dirs $(LIB_DIR)/$(LIB_STATIC)
	@mkdir -p $(USER_LIB)
	@cp $(LIB_DIR)/$(LIB_STATIC) $(USER_LIB)/
	@echo "  COPY  $(USER_LIB)/$(LIB_STATIC)"

# ----------------------------------------------------------------------------
# Library
# ----------------------------------------------------------------------------

$(LIB_DIR)/$(LIB_SHARED): $(OBJS)
	@echo "  LD    $@"
	$(CC) -shared -o $@ $(OBJS) $(LDFLAGS_SHARED) $(LDLIBS)

$(LIB_DIR)/$(LIB_STATIC): $(OBJS)
	@echo "  AR    $@"
	$(AR) rcs $@ $(OBJS)

# ----------------------------------------------------------------------------
# Object build rules
# ----------------------------------------------------------------------------

$(OBJ_DIR)/core/%.o: $(SRC_DIR)/core/%.c
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -DGR_BUILD_SHARED -c -o $@ $<

$(OBJ_DIR)/analysis/%.o: $(SRC_DIR)/analysis/%.c
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -DGR_BUILD_SHARED -c -o $@ $<

$(OBJ_DIR)/transport/%.o: $(SRC_DIR)/transport/%.c
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -DGR_BUILD_SHARED -c -o $@ $<

$(OBJ_DIR)/bridge/%.o: $(SRC_DIR)/bridge/%.c
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -DGR_BUILD_SHARED -c -o $@ $<

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------

test: shared $(TEST_BIN)
	@echo ""
	@echo "Running tests..."
	@echo "============================================"
	LD_LIBRARY_PATH=$(LIB_DIR) $(TEST_BIN)

$(TEST_BIN): $(TEST_OBJS) $(UNITY_OBJ)
	@echo "  LD    $@"
	$(CC) -o $@ $(TEST_OBJS) $(UNITY_OBJ) -L$(LIB_DIR) -l$(NAME) $(LDLIBS)

$(OBJ_DIR)/test/%.o: $(TEST_DIR)/%.c
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -I$(TEST_DIR)/unity -c -o $@ $<

$(UNITY_OBJ): $(UNITY_SRC)
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# ----------------------------------------------------------------------------
# Install (system-wide)
# ----------------------------------------------------------------------------

install: shared static
	@echo "Installing to $(PREFIX)..."
	@mkdir -p $(PREFIX)/lib
	@mkdir -p $(PREFIX)/include
	@cp $(LIB_DIR)/$(LIB_SHARED) $(PREFIX)/lib/
	@cp $(LIB_DIR)/$(LIB_STATIC) $(PREFIX)/lib/
	@cp $(INC_DIR)/georisk.h $(PREFIX)/include/
	@echo "  Done."

# ----------------------------------------------------------------------------
# Utility
# ----------------------------------------------------------------------------

clean:
	@echo "Cleaning..."
	rm -rf $(BUILD_DIR)

info:
	@echo "NAME:        $(NAME)"
	@echo "VERSION:     $(VERSION)"
	@echo "CC:          $(CC)"
	@echo "AR:          $(AR)"
	@echo "DEBUG:       $(DEBUG)"
	@echo "STRICT:      $(STRICT)"
	@echo "WERROR:      $(WERROR)"
	@echo "SAN:         $(SAN)"
	@echo "CFLAGS:      $(CFLAGS)"
	@echo "LDFLAGS_SHARED: $(LDFLAGS_SHARED)"
	@echo "LDLIBS:      $(LDLIBS)"
	@echo "PREFIX:      $(PREFIX)"
	@echo "USER_LIB:    $(USER_LIB)"
	@echo "SRCS:        $(words $(SRCS)) files"
	@echo "OBJS:        $(words $(OBJS)) files"

