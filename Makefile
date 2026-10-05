CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude

SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
OBJ_DIR = obj

TARGET_CLIENT = $(BIN_DIR)/client
TARGET_SERVER = $(BIN_DIR)/server

CLIENT_SRC = $(SRC_DIR)/client.c
SERVER_SRC = $(SRC_DIR)/server.c

CLIENT_OBJ = $(OBJ_DIR)/client.o
SERVER_OBJ = $(OBJ_DIR)/server.o

all: create_dirs $(TARGET_CLIENT) $(TARGET_SERVER)

create_dirs:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(OBJ_DIR)

# Building client part
$(TARGET_CLIENT): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "[SUCCESS] Client binaries built: $@"

# Building server part
$(TARGET_SERVER): $(SERVER_OBJ)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "[SUCCESS] Server binaries built: $@"

# Universal rule for object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "[CLEAN] obj/ and bin/ directories removed"

.PHONY: all clean create_dirs

