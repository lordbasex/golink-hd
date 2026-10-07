# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
# Builds the go-link HD libretro core the libretro way: `make` for this
# computer, `make platform=<name>` for another (unix, osx, win, emscripten).
# `make test` runs the engine's tests, `make tools` builds the headless runner.

TARGET_NAME := golink_hd
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

GIT_VERSION := $(shell git rev-parse --short HEAD 2>/dev/null)
ifneq ($(GIT_VERSION),)
  CFLAGS += -DGIT_VERSION=\"" $(GIT_VERSION)"\"
endif

ifeq ($(platform),unix)
  TARGET := $(TARGET_NAME)_libretro.so
  fpic := -fPIC
  SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
else ifeq ($(platform),osx)
  TARGET := $(TARGET_NAME)_libretro.dylib
  fpic := -fPIC
  SHARED := -dynamiclib
  ifneq ($(arch),)
    CFLAGS += -arch $(arch)
    LDFLAGS += -arch $(arch)
  endif
  ifneq ($(MACOSX_DEPLOYMENT_TARGET),)
    CFLAGS += -mmacosx-version-min=$(MACOSX_DEPLOYMENT_TARGET)
    LDFLAGS += -mmacosx-version-min=$(MACOSX_DEPLOYMENT_TARGET)
  endif
else ifeq ($(platform),emscripten)
  TARGET := $(TARGET_NAME)_libretro_$(platform).bc
  STATIC_LINKING = 1
else
  CC ?= gcc
  TARGET := $(TARGET_NAME)_libretro.dll
  SHARED := -shared -static-libgcc -s -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
endif

ifeq ($(DEBUG),1)
  CFLAGS += -O0 -g
else
  CFLAGS += -O2 -DNDEBUG
endif

WARNINGS := -Wall -Wextra -Wno-unused-parameter
CFLAGS += -std=c99 $(WARNINGS) $(fpic) $(INCFLAGS)

include Makefile.common

OBJECTS := $(SOURCES_C:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING),1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CC) $(fpic) $(SHARED) -o $@ $(OBJECTS) $(LDFLAGS)
endif

%.o: %.c src/hd.h src/fixed.h src/pack.h
	$(CC) $(CFLAGS) -c -o $@ $<

# The tests build the engine straight in, without the libretro layer's frontend.
TEST_BIN := hd_test
$(TEST_BIN): tests/test.c $(SOURCES_C) src/hd.h src/fixed.h src/pack.h
	$(CC) -std=c99 $(WARNINGS) -O2 -fsanitize=address,undefined -fno-sanitize-recover=all $(INCFLAGS) -o $@ tests/test.c $(SOURCES_C)

test: $(TEST_BIN)
	./$(TEST_BIN)

# A headless libretro frontend: loads the built core, plays a script, saves PNG frames.
tools: tools/hdrun tools/glhd

tools/glhd: tools/glhd.c $(ENGINE_C) src/hd.h src/fixed.h src/pack.h
	$(CC) -std=c99 $(WARNINGS) -O2 $(INCFLAGS) -o $@ tools/glhd.c $(ENGINE_C)

tools/hdrun: tools/hdrun.c
	$(CC) -std=c99 $(WARNINGS) -O2 -Ilibretro -o $@ $< $(if $(filter unix,$(platform)),-ldl,)

clean:
	rm -f $(OBJECTS) $(TARGET) $(TEST_BIN) tools/hdrun tools/glhd

.PHONY: all clean test tools
