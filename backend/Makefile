CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS := -lws2_32

SRC := src/cli.c src/config.c src/document.c src/index.c src/query.c src/logging.c src/stats.c src/ai_client.c src/server.c
APP_SRC := src/main.c
TEST_SRC := tests/test_basic.c

BUILD_DIR := build
APP := $(BUILD_DIR)/paperpilot.exe
TEST := $(BUILD_DIR)/test_basic.exe

.PHONY: all clean test run-help

all: $(APP)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(APP): $(BUILD_DIR) $(SRC) $(APP_SRC)
	$(CC) $(CFLAGS) $(SRC) $(APP_SRC) -o $@ $(LDFLAGS)

$(TEST): $(BUILD_DIR) $(SRC) $(TEST_SRC)
	$(CC) $(CFLAGS) $(SRC) $(TEST_SRC) -o $@ $(LDFLAGS)

test: $(TEST)
	$(TEST)

run-help: $(APP)
	$(APP) help

clean:
	rm -rf $(BUILD_DIR)
