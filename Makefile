TARGET_EXEC := amex

BUILD_DIR := ./build
SRC_DIRS := ./src

SRCS := $(shell find $(SRC_DIRS) -name '*.c')

OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

DEPS := $(OBJS:.o=.d)

# generate .d file so if .h changed, will re-compile
CFLAGS := -MMD -std=c99 -Wall

ifdef DEBUG
	CFLAGS += -g -O0 -DDEBUG
	TARGET_EXEC = amex-debug
else ifdef DEBUG_GC
	CFLAGS += -g -O0 -DDEBUG_GC
	TARGET_EXEC = amex-debug-gc
else ifdef RELEASE
	CFLAGS += -O2
else
	CFLAGS += -g -O0
	TARGET_EXEC = amex-no-optimization
endif

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

.PHONY: clean memcheck format test
clean:
	rm -rf $(BUILD_DIR)

memcheck: $(BUILD_DIR)/$(TARGET_EXEC)
	valgrind --leak-check=yes $(BUILD_DIR)/$(TARGET_EXEC)

format:
	@sed -i -E 's/[ \t]+$$//' $(SRC_DIRS)/*.h $(SRC_DIRS)/*.c

test: $(BUILD_DIR)/$(TARGET_EXEC)
	@./test.sh $(BUILD_DIR)/$(TARGET_EXEC)

-include $(DEPS)

