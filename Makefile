# livejq - GNU make build.
#
# Runs on Linux, macOS, the BSDs, and Windows:
#   Unix-like:  make              # cc (gcc or clang), C99 flags
#   Windows:    make              # gcc (MinGW/MSYS2)
#               make CC=cl        # MSVC

NAME := livejq

SRCS := $(sort $(wildcard src/*.c))
HDRS := $(sort $(wildcard include/*.h))

# --- platform detection -------------------------------------------------

ifeq ($(OS),Windows_NT)
  WINDOWS := 1
else
  UNAME_S := $(shell uname -s 2>/dev/null)
  ifneq ($(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)),)
    WINDOWS := 1
  endif
endif

ifeq ($(WINDOWS),1)
  EXE    := .exe
  OBJEXT := obj
  MKDIR  := mkdir
else
  EXE    :=
  OBJEXT := o
  MKDIR  := mkdir -p
endif

BIN    := bin/$(NAME)$(EXE)
OBJDIR := obj
OBJS   := $(patsubst src/%.c,$(OBJDIR)/%.$(OBJEXT),$(SRCS))
TEST_CS   := $(sort $(wildcard tests/test_*.c))
TEST_BINS := $(patsubst tests/test_%.c,bin/test_%$(EXE),$(TEST_CS))

# --- compiler selection -------------------------------------------------

ifneq ($(filter undefined default,$(origin CC)),)
  ifeq ($(WINDOWS),1)
    CC := gcc
  else
    CC := cc
  endif
endif

CC_NAME := $(notdir $(subst \,/,$(CC)))
ifneq ($(filter cl cl.exe,$(CC_NAME)),)
  MSVC := 1
else ifneq ($(findstring clang-cl,$(CC_NAME)),)
  MSVC := 1
endif

ifeq ($(MSVC),1)
  CFLAGS  ?= /nologo /std:c11 /W4 /O2 /Iinclude
  LDFLAGS ?=
  COMPILE  = $(CC) $(CFLAGS) /c $< /Fo$(subst /,\,$@)
  LINK     = $(CC) $(CFLAGS) $(subst /,\,$(OBJS)) /Fe$(subst /,\,$(BIN))
  TEST_CC  = $(CC) $(CFLAGS) $(subst /,\,$<) /Fe$(subst /,\,$@)
else
  CFLAGS  ?= -Os -std=c99 -Wall -Wextra -Wpedantic -Iinclude
  LDFLAGS ?=
  COMPILE  = $(CC) $(CFLAGS) -c $< -o $@
  LINK     = $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)
  TEST_CC  = $(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(LDLIBS)
endif

# --- targets ------------------------------------------------------------

all: $(BIN)

$(BIN): $(OBJS) | bin
	$(LINK)

$(OBJDIR)/%.$(OBJEXT): src/%.c $(HDRS) | $(OBJDIR)
	$(COMPILE)

$(OBJDIR):
	$(MKDIR) $(OBJDIR)

bin:
	$(MKDIR) bin

$(TEST_BINS): bin/test_%: tests/test_%.c tests/test_helper.in | bin
	$(TEST_CC)

test: $(TEST_BINS)
ifeq ($(WINDOWS),1)
	@for %%t in ($(subst /,\,$(TEST_BINS))) do @%%t || exit /b 1
else
	@for t in $(TEST_BINS); do ./$$t || exit 1; done
endif

clean:
ifeq ($(WINDOWS),1)
	cmd /c "if exist bin rmdir /s /q bin"
	cmd /c "if exist obj rmdir /s /q obj"
else
	rm -rf bin obj
endif

.PHONY: all clean test
.SECONDARY: $(OBJS)
