CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O3 -march=native
SRCS    = src/bitboard.c src/position.c src/movegen.c src/main.c
TARGET  = cbitboard

$(TARGET): $(SRCS) src/types.h src/bitboard.h src/position.h src/movegen.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

debug: CFLAGS = -std=c11 -Wall -Wextra -g -fsanitize=address,undefined
debug: $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: debug clean
