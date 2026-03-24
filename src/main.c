#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "types.h"
#include "bitboard.h"
#include "position.h"
#include "movegen.h"

/* ── Perft ───────────────────────────────────────────────────────────────── */

static uint64_t perft(Position *pos, int depth) {
    if (depth == 0) return 1;

    MoveList list;
    generate_moves(pos, &list);

    uint64_t nodes = 0;
    UndoInfo undo;

    for (int i = 0; i < list.count; i++) {
        do_move(pos, list.moves[i], &undo);
        /* Filter pseudo-legal: skip if we left our king in check */
        if (!is_in_check(pos, pos->side ^ 1))
            nodes += perft(pos, depth - 1);
        undo_move(pos, &undo);
    }
    return nodes;
}

/* ── Perft divide (root move breakdown, great for debugging) ─────────────── */

static void perft_divide(Position *pos, int depth) {
    MoveList list;
    generate_moves(pos, &list);

    uint64_t total = 0;
    UndoInfo undo;

    for (int i = 0; i < list.count; i++) {
        do_move(pos, list.moves[i], &undo);
        if (!is_in_check(pos, pos->side ^ 1)) {
            uint64_t n = (depth <= 1) ? 1 : perft(pos, depth - 1);
            print_move_uci(list.moves[i]);
            printf(": %llu\n", (unsigned long long)n);
            total += n;
        }
        undo_move(pos, &undo);
    }
    printf("\nTotal: %llu\n", (unsigned long long)total);
}

/* ── Timing helper ───────────────────────────────────────────────────────── */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ── Usage ───────────────────────────────────────────────────────────────── */

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s [fen] <depth>          -- run perft\n"
        "  %s [fen] divide <depth>   -- run perft divide\n"
        "  %s board [fen]            -- print board\n"
        "\n"
        "  fen defaults to startpos when omitted or empty string.\n"
        "\nExamples:\n"
        "  %s 5\n"
        "  %s \"\" 6\n"
        "  %s \"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -\" 4\n"
        "  %s board\n",
        prog, prog, prog, prog, prog, prog, prog);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    init_tables();

    if (argc < 2) { usage(argv[0]); return 1; }

    /* Determine FEN and remaining args */
    const char *fen = STARTPOS_FEN;
    int arg = 1;

    /* If first arg is "board", just print and exit */
    if (strcmp(argv[arg], "board") == 0) {
        if (argc > 2 && strlen(argv[2]) > 4) fen = argv[2];
        Position pos;
        if (parse_fen(&pos, fen) != 0) {
            fprintf(stderr, "Invalid FEN: %s\n", fen);
            return 1;
        }
        print_board(&pos);
        return 0;
    }

    /* First arg might be a FEN string (contains '/' or is empty) */
    if (strchr(argv[arg], '/') || strlen(argv[arg]) == 0) {
        if (strlen(argv[arg]) > 0) fen = argv[arg];
        arg++;
    }

    if (arg >= argc) { usage(argv[0]); return 1; }

    Position pos;
    if (parse_fen(&pos, fen) != 0) {
        fprintf(stderr, "Invalid FEN: %s\n", fen);
        return 1;
    }

    /* "divide" command */
    if (strcmp(argv[arg], "divide") == 0) {
        arg++;
        if (arg >= argc) { usage(argv[0]); return 1; }
        int depth = atoi(argv[arg]);
        if (depth < 1) depth = 1;
        printf("Perft divide depth %d\n\n", depth);
        perft_divide(&pos, depth);
        return 0;
    }

    /* Plain perft */
    int depth = atoi(argv[arg]);
    if (depth < 0) depth = 0;

    printf("FEN:   %s\n", fen);
    printf("Depth: %d\n\n", depth);

    double t0    = now_ms();
    uint64_t n   = perft(&pos, depth);
    double   ms  = now_ms() - t0;

    printf("Nodes: %llu\n", (unsigned long long)n);
    if (ms > 0.5)
        printf("Time:  %.1f ms  (%.1f Mnodes/s)\n", ms, n / ms / 1000.0);

    return 0;
}
