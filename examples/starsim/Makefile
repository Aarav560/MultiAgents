# starsim - pure C gravity simulator. `make`, `make run`, `make test`, `make headless`, `make clean`.
CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
LDLIBS = -lm

B := build
LIB := world physics quadtree scenario render font camera trails
LIB_OBJS := $(LIB:%=$(B)/%.o)

UNAME := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME),Darwin)
  PLATFORM := platform_cocoa
  PLATFORM_LIBS := -framework Cocoa -framework CoreGraphics
else
  PLATFORM := platform_x11
  PLATFORM_LIBS := -lX11
endif

TESTS := $(patsubst tests/%.c,%,$(wildcard tests/test_*.c))
HL := $(B)/headless
HEADERS := $(wildcard include/*.h)

.PHONY: all run test headless clean

all: $(B)/starsim

$(B):
	mkdir -p $(B)

$(HL):
	mkdir -p $(HL)

$(B)/%.o: src/%.c $(HEADERS) | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(HL)/%.o: src/%.c $(HEADERS) | $(HL)
	$(CC) $(CFLAGS) -DSTARSIM_HEADLESS -c $< -o $@

$(B)/starsim: $(LIB_OBJS) $(B)/app.o $(B)/main.o $(B)/$(PLATFORM).o
	$(CC) $(CFLAGS) $^ -o $@ $(PLATFORM_LIBS) $(LDLIBS)

$(B)/starsim-headless: $(LIB:%=$(HL)/%.o) $(HL)/app.o $(HL)/main.o $(HL)/platform_null.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

headless: $(B)/starsim-headless

run: $(B)/starsim
	./$(B)/starsim

# Tests link the library as an archive, so a test's own helper (e.g. a stub gravity_bh in
# test_physics) takes precedence over the archive member.
$(B)/libstarsim.a: $(LIB_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(B)/test_app: tests/test_app.c tests/test.h $(B)/app.o $(HL)/platform_null.o $(B)/libstarsim.a
	$(CC) $(CFLAGS) $< $(B)/app.o $(HL)/platform_null.o $(B)/libstarsim.a -o $@ $(LDLIBS)

$(B)/test_%: tests/test_%.c tests/test.h $(B)/libstarsim.a
	$(CC) $(CFLAGS) $< $(B)/libstarsim.a -o $@ $(LDLIBS)

test: $(TESTS:%=$(B)/%)
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$(B)/$$t; done

clean:
	rm -rf $(B)
