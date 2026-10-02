# Dotkeeper build. Works with GNU make on Linux and macOS.

CC      ?= cc
PREFIX  ?= /usr/local
BIN      = dotkeeper

# C17 is pinned on purpose: GCC 15 defaults to C23 and Apple clang to C17,
# and the code must not depend on which default the compiler picked.
CSTD     = -std=c17
WARN     = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS  ?= -O2 -g

# `make clean && make SANITIZE=1 test` builds and tests with AddressSanitizer
# and UndefinedBehaviorSanitizer. Clean first: objects are not rebuilt when
# only flags change.
ifdef SANITIZE
CFLAGS  = -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif
# inih settings: long lines for paths, no inline ';' comments (a path may
# contain ';'), and a callback at each new section for repeated [link]s.
INIFLAGS = -DINI_MAX_LINE=8192 -DINI_ALLOW_INLINE_COMMENTS=0 \
           -DINI_CALL_HANDLER_ON_NEW_SECTION=1 -DINI_STOP_ON_FIRST_ERROR=1 \
           -DINI_ALLOW_MULTILINE=0
CPPFLAGS += -Ivendor/inih $(INIFLAGS)

# POSIX.1-2008 with the XSI extension (realpath). On macOS that request
# would hide BSD functions, so ask for everything there instead.
ifeq ($(shell uname -s),Darwin)
CPPFLAGS += -D_DARWIN_C_SOURCE
else
CPPFLAGS += -D_XOPEN_SOURCE=700
endif

SRC = $(wildcard src/util/*.c src/platform/*.c src/core/*.c src/cli/*.c)
LIB_OBJ = $(SRC:%.c=build/%.o) build/vendor/inih/ini.o
MAIN_OBJ = build/src/main.o
TEST_SRC = $(wildcard tests/unit/*.c)
TEST_OBJ = $(TEST_SRC:%.c=build/%.o)

.PHONY: all clean test unit-test integration-test install uninstall

all: $(BIN)

$(BIN): $(MAIN_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(WARN) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

build/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(WARN) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

# Vendored code is compiled without our extra warnings; it is not ours to fix.
build/vendor/%.o: vendor/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

build/unit-tests: $(TEST_OBJ) $(LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: unit-test integration-test

unit-test: build/unit-tests
	./build/unit-tests

integration-test: $(BIN)
	sh tests/integration/run.sh ./$(BIN)

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

clean:
	rm -rf build $(BIN)

-include $(shell find build -name '*.d' 2>/dev/null)
