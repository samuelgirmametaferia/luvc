CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -std=c11 -O3
LDFLAGS = 

# Enable SEMA_DEBUG=1 when invoking make to compile in sema debug prints.
# Example: make SEMA_DEBUG=1
ifeq ($(SEMA_DEBUG),1)
CFLAGS += -DSEMA_DEBUG
endif

SRC_DIR = src
BUILD_DIR = build
BIN = $(BUILD_DIR)/luv

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
