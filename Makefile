# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
# Builds go-link HD's engine as a library with its own API (include/golink_hd.h):
# `make` the shared library for this computer, `make static` the static one,
# `make platform=<name>` for another (unix, osx, win). `make test` runs the
# engine's tests, `make tools` builds the headless runner and the package
# tools, `make bench` measures each scene.

NAME := golinkhd
CORE_DIR := .

ifeq ($(platform),)
  platform = unix
  ifeq ($(shell uname -s),)
    platform = win
  else ifneq ($(findstring Darwin,$(shell uname -s)),)
    platform = osx
  else ifneq ($(findstring MINGW,$(shell uname -s)),)
    platform = win
  else ifneq ($(findstring MSYS,$(shell uname -s)),)
    platform = win
  endif
endif

ifeq ($(platform),unix)
  TARGET := lib$(NAME).so
  fpic := -fPIC
  SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
  DL := -ldl
else ifeq ($(platform),osx)
  TARGET := lib$(NAME).dylib
  fpic := -fPIC
  SHARED := -dynamiclib -install_name @rpath/lib$(NAME).dylib
  ifneq ($(arch),)
    CFLAGS += -arch $(arch)
    LDFLAGS += -arch $(arch)
  endif
  ifneq ($(MACOSX_DEPLOYMENT_TARGET),)
    CFLAGS += -mmacosx-version-min=$(MACOSX_DEPLOYMENT_TARGET)
    LDFLAGS += -mmacosx-version-min=$(MACOSX_DEPLOYMENT_TARGET)
  endif
else
  CC ?= gcc
  TARGET := $(NAME).dll
  SHARED := -shared -static-libgcc -s -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
  CFLAGS += -DGOLINKHD_BUILD_SHARED
endif

ifeq ($(DEBUG),1)
  CFLAGS += -O0 -g
else
  CFLAGS += -O2 -DNDEBUG
endif

WARNINGS := -Wall -Wextra -Wno-unused-parameter
# only the API (GOLINKHD_API) leaves the library
CFLAGS += -std=c99 $(WARNINGS) $(fpic) -fvisibility=hidden $(INCFLAGS)

include Makefile.common

OBJECTS := $(SOURCES_C:.c=.o)
HEADERS := include/golink_hd.h src/hd.h src/fixed.h src/pack.h src/gfx.h src/text.h src/bones.h src/road.h src/path.h

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(fpic) $(SHARED) -o $@ $(OBJECTS) $(LDFLAGS)

static: lib$(NAME).a

lib$(NAME).a: $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

# The tests build the engine straight in, with the address and undefined behaviour sanitizers.
TEST_BIN := hd_test
$(TEST_BIN): tests/test.c $(SOURCES_C) $(HEADERS)
	$(CC) -std=c99 $(WARNINGS) -O2 -fsanitize=address,undefined -fno-sanitize-recover=all $(INCFLAGS) -o $@ tests/test.c $(SOURCES_C)

test: $(TEST_BIN)
	./$(TEST_BIN)

# hdrun: a headless host that loads the built library (as go-link's device does), plays a script and saves PNG frames.
tools: tools/hdrun tools/glhd

tools/hdrun: tools/hdrun.c include/golink_hd.h
	$(CC) -std=c99 $(WARNINGS) -O2 -Iinclude -o $@ $< $(DL)

tools/glhd: tools/glhd.c $(ENGINE_C) $(HEADERS)
	$(CC) -std=c99 $(WARNINGS) -O2 $(INCFLAGS) -o $@ tools/glhd.c $(ENGINE_C)

# Milliseconds per frame of each scene, optimized and without sanitizers.
bench: tools/bench
	tools/bench

tools/bench: tools/bench.c $(ENGINE_C) $(HEADERS)
	$(CC) -std=c11 $(WARNINGS) -O2 $(INCFLAGS) -o $@ tools/bench.c $(ENGINE_C)

clean:
	rm -f $(OBJECTS) lib$(NAME).so lib$(NAME).dylib $(NAME).dll lib$(NAME).a $(TEST_BIN) tools/hdrun tools/glhd tools/bench

.PHONY: all clean static test tools bench
