CC      	?= cc
CFLAGS  	?= -std=c99 -Wall -Wextra -Werror -Iinclude -MMD -MP
AR      	?= ar
ARFLAGS 	?= rcs
ASAN_FLAGS := -fsanitize=address,undefined -g -O0

SRCS    	:= $(wildcard src/*.c)
TEST_SRC	:= tests/test_int_vector.c src/int_vector.c
BIN_DIR 	:= build
TARGET  	:= libintvector.a
OBJS    	:= $(patsubst src/%.c,$(BIN_DIR)/%.o,$(SRCS))
DEPS    	:= $(OBJS:.o=.d)

.PHONY: all test test-asan clean re

all: $(BIN_DIR)/$(TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/%.o: src/%.c include/int_vector.h | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/$(TARGET): $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)

test: $(TEST_SRC) include/int_vector.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(BIN_DIR)/test_int_vector
	$(BIN_DIR)/test_int_vector

test-asan: $(TEST_SRC) include/int_vector.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(TEST_SRC) -o $(BIN_DIR)/test_int_vector_asan
	$(BIN_DIR)/test_int_vector_asan

clean:
	rm -rf $(BIN_DIR)

re: clean all

-include $(DEPS)