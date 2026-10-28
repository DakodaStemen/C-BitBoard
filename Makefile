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

clean:
	rm -f $(TARGET)

bench: $(TARGET)
	./tests/bench.sh ./$(TARGET)

.PHONY: debug test clean bench
