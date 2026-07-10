CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude

SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
OBJ_DIR = obj

TARGET_DRONE = $(BIN_DIR)/drone
TARGET_GROUND = $(BIN_DIR)/ground

COMMON_SRC = $(SRC_DIR)/crsf.c
DRONE_SRC = $(SRC_DIR)/drone.c
GROUND_SRC = $(SRC_DIR)/ground.c

COMMON_OBJ = $(OBJ_DIR)/crsf.o
DRONE_OBJ = $(OBJ_DIR)/drone.o
GROUND_OBJ = $(OBJ_DIR)/ground.o

all: create_dirs $(TARGET_DRONE) $(TARGET_GROUND)

create_dirs:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(OBJ_DIR)

# Building drone part
$(TARGET_DRONE): $(DRONE_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "[SUCCESS] Drone binaries built: $@"

# Building ground part
$(TARGET_GROUND): $(GROUND_OBJ) $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "[SUCCESS] Ground binaries built: $@"

# Universal rule for object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "[CLEAN] obj/ and bin/ directories removed"

.PHONY: all clean create_dirs

