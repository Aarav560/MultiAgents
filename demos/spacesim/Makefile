CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
DEPFLAGS = -MMD -MP
LDLIBS = -lm
SCENARIO ?= solar

SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))
LIB_OBJS := $(filter-out build/main.o,$(OBJS))
TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,build/%,$(TEST_SRCS))

.PHONY: all test bench run clean

all: build/orbit

build:
	@mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

build/orbit: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

build/test_%: tests/test_%.c $(LIB_OBJS) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) $< $(LIB_OBJS) -o $@ $(LDLIBS)

test: $(TEST_BINS)
	@pass=0; for t in $(TEST_BINS); do \
	    if ./$$t > build/$$(basename $$t).log 2>&1; then \
	        pass=$$((pass + 1)); echo "PASS $$t"; \
	    else \
	        cat build/$$(basename $$t).log; echo "FAIL $$t"; \
	        echo "tests: $$pass passed, 1 failed (stopped)"; exit 1; \
	    fi; \
	done; echo "tests: $$pass passed, 0 failed"

build/bench_gravity: bench/bench_gravity.c $(LIB_OBJS) | build
	$(CC) $(CFLAGS) $(DEPFLAGS) $< $(LIB_OBJS) -o $@ $(LDLIBS)

bench: build/bench_gravity
	./build/bench_gravity

run: build/orbit
	./build/orbit --scenario $(SCENARIO)

clean:
	rm -rf build

-include $(OBJS:.o=.d) $(TEST_BINS:=.d) build/bench_gravity.d
