CC := clang
CFLAGS := -O3 -Wall -Wextra -pthread -flto
LDFLAGS := -Wl,-stack_size,0x4000000

BUILD := build
SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/main

$(BUILD)/main: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD):
	mkdir -p $@

run: $(BUILD)/main
	./$< $(ARGS)

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)

.PHONY: all run clean
