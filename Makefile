# ============================================================================
# georisk - Geometric Risk Analysis Library
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

CC          := gcc
AR          := ar

CFLAGS      := -std=c11 -Wall -Wextra -Wpedantic -fPIC -I$(INC_DIR)

ifdef DEBUG
    CFLAGS  += -g -O0 -DDEBUG
else
    CFLAGS  += -O2 -DNDEBUG
endif

LDLIBS      := -lm -ldl -lpthread

LIB_SHARED  := lib$(NAME).so
LIB_STATIC  := lib$(NAME).a

# Source files
CORE_SRCS   := $(wildcard $(SRC_DIR)/core/*.c)
ANALYSIS_SRCS := $(wildcard $(SRC_DIR)/analysis/*.c)
TRANSPORT_SRCS := $(wildcard $(SRC_DIR)/transport/*.c)
BRIDGE_SRCS := $(wildcard $(SRC_DIR)/bridge/*.c)

SRCS        := $(CORE_SRCS) $(ANALYSIS_SRCS) $(TRANSPORT_SRCS) $(BRIDGE_SRCS)

# Object files
CORE_OBJS   := $(patsubst $(SRC_DIR)/core/%.c,$(OBJ_DIR)/core/%.o,$(CORE_SRCS))
ANALYSIS_OBJS := $(patsubst $(SRC_DIR)/analysis/%.c,$(OBJ_DIR)/analysis/%.o,$(ANALYSIS_SRCS))
TRANSPORT_OBJS := $(patsubst $(SRC_DIR)/transport/%.c,$(OBJ_DIR)/transport/%.o,$(TRANSPORT_SRCS))
BRIDGE_OBJS := $(patsubst $(SRC_DIR)/bridge/%.c,$(OBJ_DIR)/bridge/%.o,$(BRIDGE_SRCS))

OBJS        := $(CORE_OBJS) $(ANALYSIS_OBJS) $(TRANSPORT_OBJS) $(BRIDGE_OBJS)

# Test files
TEST_SRCS   := $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS   := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/test/%.o,$(TEST_SRCS))
TEST_BIN    := $(BIN_DIR)/test_runner

UNITY_SRC   := $(TEST_DIR)/unity/unity.c
UNITY_OBJ   := $(OBJ_DIR)/test/unity.o

# ============================================================================
# Targets
# ============================================================================

.PHONY: all shared static test clean dirs info install

all: shared

shared: dirs $(LIB_DIR)/$(LIB_SHARED)
	@mkdir -p $(USER_LIB)
	@cp $(LIB_DIR)/$(LIB_SHARED) $(USER_LIB)/
	@echo "  COPY  $(USER_LIB)/$(LIB_SHARED)"

static: dirs $(LIB_DIR)/$(LIB_STATIC)
	@mkdir -p $(USER_LIB)
	@cp $(LIB_DIR)/$(LIB_STATIC) $(USER_LIB)/
	@echo "  COPY  $(USER_LIB)/$(LIB_STATIC)"

dirs:
	@mkdir -p $(OBJ_DIR)/core
	@mkdir -p $(OBJ_DIR)/analysis
	@mkdir -p $(OBJ_DIR)/transport
	@mkdir -p $(OBJ_DIR)/bridge
	@mkdir -p $(OBJ_DIR)/test
	@mkdir -p $(LIB_DIR)
	@mkdir -p $(BIN_DIR)

# ============================================================================
# Library
# ============================================================================

$(LIB_DIR)/$(LIB_SHARED): $(OBJS)
	@echo "  LD    $@"
	$(CC) -shared -o $@ $(OBJS) $(LDLIBS)

$(LIB_DIR)/$(LIB_STATIC): $(OBJS)
	@echo "  AR    $@"
	$(AR) rcs $@ $(OBJS)

# ============================================================================
# Object files
# ============================================================================

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

# ============================================================================
# Tests
# ============================================================================

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

# ============================================================================
# Install (system-wide)
# ============================================================================

install: shared static
	@echo "Installing to $(PREFIX)..."
	@mkdir -p $(PREFIX)/lib
	@mkdir -p $(PREFIX)/include
	@cp $(LIB_DIR)/$(LIB_SHARED) $(PREFIX)/lib/
	@cp $(LIB_DIR)/$(LIB_STATIC) $(PREFIX)/lib/
	@cp $(INC_DIR)/georisk.h $(PREFIX)/include/
	@echo "  Done."

# ============================================================================
# Utility
# ============================================================================

clean:
	@echo "Cleaning..."
	rm -rf $(BUILD_DIR)

info:
	@echo "Sources: $(SRCS)"
	@echo "Objects: $(OBJS)"
	@echo "User library dir: $(USER_LIB)"
