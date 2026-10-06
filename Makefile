CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude -Itools $(EXTRA_CFLAGS)

SRC_DIR = src
INC_DIR = include
EXAMPLES_DIR = examples
TOOLS_DIR = tools
TESTS_DIR = tests
BENCH_DIR = bench
BIN_DIR = bin
OBJ_DIR = obj
LIB_DIR = lib

LIB = $(LIB_DIR)/libhudp.a
NATEMU_OBJ = $(OBJ_DIR)/natemu.o
HEADERS = $(INC_DIR)/hudp.h $(SRC_DIR)/protocol.h $(TOOLS_DIR)/natemu.h $(TESTS_DIR)/harness.h

TARGET_CLIENT = $(BIN_DIR)/client
TARGET_SERVER = $(BIN_DIR)/server
TARGET_NATEMU = $(BIN_DIR)/natemu
TARGET_TEST = $(BIN_DIR)/test_hudp
TARGET_TEST_NAT = $(BIN_DIR)/test_nat
TARGET_BENCH = $(BIN_DIR)/bench

all: create_dirs $(LIB) $(TARGET_CLIENT) $(TARGET_SERVER) $(TARGET_NATEMU)

create_dirs:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(LIB_DIR)

# Building the library
$(LIB): $(OBJ_DIR)/hudp.o
	$(AR) rcs $@ $^
	@echo "[SUCCESS] Library built: $@"

# Building client example
$(TARGET_CLIENT): $(OBJ_DIR)/client.o $(LIB)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "[SUCCESS] Client binaries built: $@"

# Building server example
$(TARGET_SERVER): $(OBJ_DIR)/server.o $(LIB)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "[SUCCESS] Server binaries built: $@"

# Building the CGNAT emulator
$(TARGET_NATEMU): $(OBJ_DIR)/natemu_cli.o $(NATEMU_OBJ)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "[SUCCESS] NAT emulator built: $@"

# Building tests and the benchmark
$(TARGET_TEST): $(OBJ_DIR)/test_hudp.o $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

$(TARGET_TEST_NAT): $(OBJ_DIR)/test_nat.o $(NATEMU_OBJ) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

$(TARGET_BENCH): $(OBJ_DIR)/bench.o $(NATEMU_OBJ) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

# Object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(EXAMPLES_DIR)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(TOOLS_DIR)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(TESTS_DIR)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(BENCH_DIR)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

test: all $(TARGET_TEST) $(TARGET_TEST_NAT)
	./$(TARGET_TEST)
	./$(TARGET_TEST_NAT)
	sh $(TESTS_DIR)/smoke.sh
	sh $(TESTS_DIR)/smoke_nat.sh
	CC="$(CC)" CFLAGS="$(CFLAGS)" sh $(TESTS_DIR)/doc_examples.sh

bench: all $(TARGET_BENCH)
	./$(TARGET_BENCH)

docs-check:
	python3 $(TESTS_DIR)/check_links.py

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB_DIR)
	@echo "[CLEAN] obj/, bin/ and lib/ directories removed"

.PHONY: all bench clean create_dirs docs-check test
