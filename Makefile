CC      ?= gcc
# Set NATIVE=0 for a portable binary (e.g. CI artifacts); default tunes for this CPU.
NATIVE  ?= 1
CFLAGS  ?= -std=c11 -Wall -Wextra -O3
ifeq ($(NATIVE),1)
CFLAGS  += -march=native
endif
SRCS    = $(wildcard src/*.c)
HDRS    = $(wildcard src/*.h)
TARGET  = cbitboard

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

debug: CFLAGS = -std=c11 -Wall -Wextra -g -fsanitize=address,undefined
debug: clean $(TARGET)

test: $(TARGET)
	./tests/perft_suite.sh ./$(TARGET) quick
	./tests/bench.sh ./$(TARGET)
	python3 tests/uci_test.py ./$(TARGET)

slider-bench:
	$(CC) -std=c11 -O3 -march=native -Isrc tools/slider_bench.c src/bitboard.c -o slider_bench && ./slider_bench

clean:
	rm -f $(TARGET)

bench: $(TARGET)
	./tests/bench.sh ./$(TARGET)

# make sprt BASE=./old-binary [NEW=./cbitboard]   (needs fastchess on PATH or FASTCHESS=...)
sprt: $(TARGET)
	@test -n "$(BASE)" || { echo "usage: make sprt BASE=<baseline binary> [NEW=./$(TARGET)]"; exit 2; }
	./tools/sprt.sh "$(BASE)" "$(or $(NEW),./$(TARGET))"

.PHONY: debug test clean bench sprt slider-bench
