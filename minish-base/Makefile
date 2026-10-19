CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic -Werror -Wno-unused-variable \
          -Wno-unused-function -Wno-unused-parameter -g
CPPFLAGS := -Iinclude
LDFLAGS :=
LDLIBS :=

TARGET := build/minish

SRC := \
	src/parser.c \
	src/shell.c

OBJ := $(SRC:%.c=build/%.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

test: $(TARGET)
	./tests/test_shell.sh ./$(TARGET)

clean:
	rm -rf build
