#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include "engine.h"
#include "bitboard.h"
#include "position.h"
#include "movegen.h"

#define ENGINE_NAME "C-BitBoard 2.0"

/* ── Raw line reader that lets the search peek for "stop" without losing
      other buffered commands ─────────────────────────────────────────────── */

static char   ibuf[1 << 16];
static size_t ilen = 0;
static int    quit_requested = 0, eof = 0;

static int fill(int block) {
    if (eof || ilen >= sizeof ibuf - 1) return 0;
    if (!block) {
        fd_set fds; FD_ZERO(&fds); FD_SET(0, &fds);
        struct timeval tv = {0, 0};
        if (select(1, &fds, NULL, NULL, &tv) <= 0) return 0;
    }
    ssize_t n = read(0, ibuf + ilen, sizeof ibuf - 1 - ilen);
    if (n <= 0) { eof = 1; return 0; }
    ilen += n;
    return 1;
}

static int take_line(char *out, size_t cap) {
    for (;;) {
        char *nl = memchr(ibuf, '\n', ilen);
        if (nl) {
            size_t n = nl - ibuf;
            if (n >= cap) n = cap - 1;
            memcpy(out, ibuf, n); out[n] = 0;
            if (n && out[n - 1] == '\r') out[n - 1] = 0;
            memmove(ibuf, nl + 1, ilen - (nl + 1 - ibuf));
            ilen -= nl + 1 - ibuf;
            return 1;
        }
        if (!fill(1)) {
            if (ilen) { size_t n = ilen < cap - 1 ? ilen : cap - 1; memcpy(out, ibuf, n); out[n] = 0; ilen = 0; return 1; }
            return 0;
        }
    }
}

/* Called from inside the search: returns 1 on "stop"/"quit". */
static int poll_stop(void) {
    fill(0);
    char *p = ibuf, *end = ibuf + ilen, *nl;
    while ((nl = memchr(p, '\n', end - p))) {
        if (!strncmp(p, "stop", 4)) {
            memmove(p, nl + 1, end - nl - 1); ilen -= nl + 1 - p; return 1;
        }
        if (!strncmp(p, "quit", 4)) { quit_requested = 1; return 1; }
        p = nl + 1;
    }
    return 0;
}

/* ── Game state ──────────────────────────────────────────────────────────── */

static Position game;
static uint64_t ghist[4096];
static int      ghlen = 0;

static void set_position(const char *args) {
    const char *p = args;
    ghlen = 0;
    if (!strncmp(p, "startpos", 8)) { parse_fen(&game, STARTPOS_FEN); p += 8; }
    else if (!strncmp(p, "fen", 3)) {
        p += 3; while (*p == ' ') p++;
        char fen[256]; size_t n = 0;
        while (*p && strncmp(p, " moves", 6) && n < sizeof fen - 1) fen[n++] = *p++;
        fen[n] = 0;
        if (parse_fen(&game, fen) != 0) { printf("info string bad fen\n"); parse_fen(&game, STARTPOS_FEN); }
    } else return;
    const char *mv = strstr(p, "moves");
    if (!mv) return;
    mv += 5;
    char tok[16];
    while (sscanf(mv, " %15s", tok) == 1) {
        mv = strstr(mv, tok) + strlen(tok);
        Move m = parse_uci_move(&game, tok);
        if (!m) { printf("info string illegal move %s\n", tok); break; }
        UndoInfo u;
        if (ghlen < 4095) ghist[ghlen++] = game.hash;
        do_move(&game, m, &u);
    }
}

static void do_go(char *args) {
    Limits lim = {0};
    long wtime = 0, btime = 0, winc = 0, binc = 0;
    int perft_d = 0;
    char *save, *t = strtok_r(args, " ", &save);
    while (t) {
        #define NEXT() (t = strtok_r(NULL, " ", &save))
        if (!strcmp(t, "wtime") && NEXT()) wtime = atol(t);
        else if (!strcmp(t, "btime") && NEXT()) btime = atol(t);
        else if (!strcmp(t, "winc") && NEXT()) winc = atol(t);
        else if (!strcmp(t, "binc") && NEXT()) binc = atol(t);
        else if (!strcmp(t, "movestogo") && NEXT()) lim.movestogo = atoi(t);
        else if (!strcmp(t, "movetime") && NEXT()) lim.movetime_ms = atol(t);
        else if (!strcmp(t, "depth") && NEXT()) lim.depth = atoi(t);
        else if (!strcmp(t, "nodes") && NEXT()) lim.nodes = strtoull(t, NULL, 10);
        else if (!strcmp(t, "infinite")) lim.infinite = 1;
        else if (!strcmp(t, "perft") && NEXT()) perft_d = atoi(t);
        NEXT();
    }
    if (perft_d) {
        printf("nodes %llu\n", (unsigned long long)perft_legal(&game, perft_d));
        return;
    }
    lim.time_ms = game.side == WHITE ? wtime : btime;
    lim.inc_ms  = game.side == WHITE ? winc  : binc;
    SearchResult r = search_go(&game, &lim, ghist, ghlen, poll_stop);
    if (r.best) { char b[8]; move_to_str(r.best, b); printf("bestmove %s\n", b); }
    else printf("bestmove 0000\n");
    fflush(stdout);
}

void uci_loop(void) {
    search_init();
    parse_fen(&game, STARTPOS_FEN);
    char line[2048];
    setvbuf(stdout, NULL, _IOLBF, 0);
    while (!quit_requested && take_line(line, sizeof line)) {
        if (!strcmp(line, "uci")) {
            printf("id name %s\nid author Dakoda Stemen\n", ENGINE_NAME);
            printf("option name Hash type spin default 64 min 1 max 4096\n");
            printf("uciok\n");
        } else if (!strcmp(line, "isready")) printf("readyok\n");
        else if (!strcmp(line, "ucinewgame")) search_new_game();
        else if (!strncmp(line, "position ", 9)) set_position(line + 9);
        else if (!strncmp(line, "go", 2)) do_go(line + 2);
        else if (!strncmp(line, "setoption", 9)) {
            char *n = strstr(line, "name Hash"), *v = strstr(line, "value");
            if (n && v) { tt_resize((size_t)atoi(v + 5)); }
        } else if (!strcmp(line, "d")) print_board(&game);
        else if (!strcmp(line, "quit")) break;
        fflush(stdout);
    }
}

/* ── Bench ───────────────────────────────────────────────────────────────── */

static const char *BENCH_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "2kr3r/ppp2ppp/2n5/3pP3/3P4/2N2N2/PP3PPP/R1B2RK1 b - - 0 1",
    "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
};

void run_bench(int depth) {
    search_init();
    uint64_t total = 0; double ms = 0;
    for (size_t i = 0; i < sizeof BENCH_FENS / sizeof *BENCH_FENS; i++) {
        Position p; parse_fen(&p, BENCH_FENS[i]);
        search_new_game();
        Limits lim = {0}; lim.depth = depth; lim.quiet = 1;
        SearchResult r = search_go(&p, &lim, NULL, 0, NULL);
        char b[8]; move_to_str(r.best, b);
        printf("pos %zu: depth %d best %s score %d nodes %llu\n", i + 1, r.depth, b, r.score,
               (unsigned long long)r.nodes);
        total += r.nodes; ms += r.ms;
    }
    printf("\nTotal nodes: %llu\nTime: %.0f ms\nNPS: %.0f\n", (unsigned long long)total, ms,
           ms > 0 ? total / ms * 1000 : 0.0);
}
