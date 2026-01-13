# ============================================================================
# georisk - Geometric Risk Analysis Library
# ============================================================================
#
# "Statistical risk measures what appears. Geometric risk describes what is possible."
#
# Build targets:
#   make            - Build shared library
#   make static     - Build static library
#   make test       - Build and run tests
#   make clean      - Clean build artifacts
#   make install    - Install to PREFIX (default: /usr/local)
#
# Configuration:
#   DEBUG=1         - Build with debug symbols
#   PREFIX=/path    - Installation prefix
#   MCO_PATH=/path  - Path to libmcoptions.so (for tests)
#   FDP_PATH=/path  - Path to libfdpricing.so (for tests)
#
# ============================================================================

# Project
NAME        := georisk
VERSION     := 0.1.0

# Directories
SRC_DIR     := src
INC_DIR     := include
TEST_DIR    := tests
BUILD_DIR   := build
OBJ_DIR     := $(BUILD_DIR)/obj
LIB_DIR     := $(BUILD_DIR)/lib
BIN_DIR     := $(BUILD_DIR)/bin

# Installation
PREFIX      ?= /usr/local
INSTALL_INC := $(PREFIX)/include
INSTALL_LIB := $(PREFIX)/lib

# Compiler
CC          := gcc
AR          := ar

# Flags
CFLAGS      := -std=c11 -Wall -Wextra -Wpedantic -Werror
CFLAGS      += -fPIC
CFLAGS      += -I$(INC_DIR)

# Debug/Release
ifdef DEBUG
    CFLAGS  += -g -O0 -DDEBUG
else
    CFLAGS  += -O2 -DNDEBUG
endif

# Linker flags
LDFLAGS     := -shared
LDLIBS      := -lm -ldl -lpthread

# Library names
LIB_SHARED  := lib$(NAME).so
LIB_STATIC  := lib$(NAME).a
LIB_SONAME  := lib$(NAME).so.0

# Source files
SRCS        := $(wildcard $(SRC_DIR)/core/*.c) \
               $(wildcard $(SRC_DIR)/analysis/*.c) \
               $(wildcard $(SRC_DIR)/transport/*.c) \
               $(wildcard $(SRC_DIR)/bridge/*.c)

OBJS        := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Test files
TEST_SRCS   := $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS   := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/test/%.o,$(TEST_SRCS))
TEST_BIN    := $(BIN_DIR)/test_runner

# Unity test framework
UNITY_DIR   := $(TEST_DIR)/unity
UNITY_SRC   := $(UNITY_DIR)/unity.c
UNITY_OBJ   := $(OBJ_DIR)/test/unity.o

# ============================================================================
# Targets
# ============================================================================

.PHONY: all shared static test clean install uninstall dirs

all: shared

shared: dirs $(LIB_DIR)/$(LIB_SHARED)

static: dirs $(LIB_DIR)/$(LIB_STATIC)

# Create build directories
dirs:
	@mkdir -p $(OBJ_DIR)/core
	@mkdir -p $(OBJ_DIR)/analysis
	@mkdir -p $(OBJ_DIR)/transport
	@mkdir -p $(OBJ_DIR)/bridge
	@mkdir -p $(OBJ_DIR)/test
	@mkdir -p $(LIB_DIR)
	@mkdir -p $(BIN_DIR)

# ============================================================================
# Library build
# ============================================================================

$(LIB_DIR)/$(LIB_SHARED): $(OBJS)
	@echo "  LD    $@"
	@$(CC) $(LDFLAGS) -Wl,-soname,$(LIB_SONAME) -o $@ $^ $(LDLIBS)
	@ln -sf $(LIB_SHARED) $(LIB_DIR)/$(LIB_SONAME)
	@ln -sf $(LIB_SHARED) $(LIB_DIR)/lib$(NAME).so

$(LIB_DIR)/$(LIB_STATIC): $(OBJS)
	@echo "  AR    $@"
	@$(AR) rcs $@ $^

# Object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -DGR_BUILD_SHARED -c -o $@ $

# ============================================================================
# Tests
# ============================================================================

test: dirs $(TEST_BIN)
	@echo ""
	@echo "Running tests..."
	@echo "============================================"
	@LD_LIBRARY_PATH=$(LIB_DIR):$$LD_LIBRARY_PATH $(TEST_BIN)

$(TEST_BIN): $(LIB_DIR)/$(LIB_SHARED) $(TEST_OBJS) $(UNITY_OBJ)
	@echo "  LD    $@"
	@$(CC) -o $@ $(TEST_OBJS) $(UNITY_OBJ) -L$(LIB_DIR) -l$(NAME) $(LDLIBS)

$(OBJ_DIR)/test/%.o: $(TEST_DIR)/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -I$(UNITY_DIR) -c -o $@ $

$(UNITY_OBJ): $(UNITY_SRC)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c -o $@ $

# ============================================================================
# Installation
# ============================================================================

install: shared
	@echo "Installing to $(PREFIX)..."
	@mkdir -p $(INSTALL_INC)/$(NAME)
	@mkdir -p $(INSTALL_LIB)
	@cp $(INC_DIR)/$(NAME).h $(INSTALL_INC)/
	@cp -r $(INC_DIR)/internal $(INSTALL_INC)/$(NAME)/
	@cp $(LIB_DIR)/$(LIB_SHARED) $(INSTALL_LIB)/
	@ln -sf $(LIB_SHARED) $(INSTALL_LIB)/$(LIB_SONAME)
	@ln -sf $(LIB_SHARED) $(INSTALL_LIB)/lib$(NAME).so
	@ldconfig 2>/dev/null || true
	@echo "Done."

uninstall:
	@echo "Uninstalling from $(PREFIX)..."
	@rm -f $(INSTALL_INC)/$(NAME).h
	@rm -rf $(INSTALL_INC)/$(NAME)
	@rm -f $(INSTALL_LIB)/lib$(NAME).*
	@ldconfig 2>/dev/null || true
	@echo "Done."

# ============================================================================
# Cleanup
# ============================================================================

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR)

# ============================================================================
# Development helpers
# ============================================================================

.PHONY: format check

# Format source code (requires clang-format)
format:
	@find $(SRC_DIR) $(INC_DIR) $(TEST_DIR) -name '*.c' -o -name '*.h' | \
		xargs clang-format -i -style=file

# Static analysis (requires cppcheck)
check:
	@cppcheck --enable=all --inconclusive -I$(INC_DIR) $(SRC_DIR) 2>&1 | \
		grep -v "^Checking"

# Show configuration
info:
	@echo "Project:     $(NAME) v$(VERSION)"
	@echo "Compiler:    $(CC)"
	@echo "CFLAGS:      $(CFLAGS)"
	@echo "LDFLAGS:     $(LDFLAGS)"
	@echo "LDLIBS:      $(LDLIBS)"
	@echo "Sources:     $(words $(SRCS)) files"
	@echo "Tests:       $(words $(TEST_SRCS)) files"
	@echo "Build dir:   $(BUILD_DIR)"
	@echo "Install:     $(PREFIX)"
