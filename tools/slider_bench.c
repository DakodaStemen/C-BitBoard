/* Micro-benchmark: Hyperbola Quintessence (this engine) vs "fancy" magic bitboards.
 *   make slider-bench      (or: cc -std=c11 -O3 -march=native -Isrc tools/slider_bench.c src/bitboard.c -o slider_bench)
 * Magics are searched at startup (deterministic seed) and every table entry is cross-checked
 * against HQ on random occupancies before timing. Numbers depend entirely on the host CPU.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bitboard.h"

typedef struct { Bitboard mask, magic; Bitboard *tab; int shift; } Magic;
static Magic RM[64], BM[64];
static Bitboard rtab[102400], btab[5248];

static uint64_t rs = 88172645463325252ULL;
static uint64_t rnd(void) { rs ^= rs >> 12; rs ^= rs << 25; rs ^= rs >> 27; return rs * 2685821657736338717ULL; }
static uint64_t sparse(void) { return rnd() & rnd() & rnd(); }

static Bitboard slow_attacks(int sq, Bitboard occ, const int d[4][2]) {
    Bitboard a = 0;
    for (int i = 0; i < 4; i++) {
        int r = SQ_RANK(sq) + d[i][0], f = SQ_FILE(sq) + d[i][1];
        while (r >= 0 && r < 8 && f >= 0 && f < 8) {
            Bitboard b = bb_sq(SQ(r, f)); a |= b; if (occ & b) break;
            r += d[i][0]; f += d[i][1];
        }
    }
    return a;
}
static const int RD[4][2] = {{1,0},{-1,0},{0,1},{0,-1}}, BD[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};

static Bitboard relevant(int sq, const int d[4][2]) {
    Bitboard m = 0;
    for (int i = 0; i < 4; i++) {
        int r = SQ_RANK(sq) + d[i][0], f = SQ_FILE(sq) + d[i][1];
        while (r + d[i][0] >= 0 && r + d[i][0] < 8 && f + d[i][1] >= 0 && f + d[i][1] < 8) {
            m |= bb_sq(SQ(r, f)); r += d[i][0]; f += d[i][1];
        }
    }
    return m;
}

static void build(Magic *M, Bitboard *pool, const int d[4][2]) {
    Bitboard *p = pool;
    for (int sq = 0; sq < 64; sq++) {
        Magic *m = &M[sq];
        m->mask = relevant(sq, d); int bits = bb_popcount(m->mask); m->shift = 64 - bits;
        m->tab = p; p += 1 << bits;
        int n = 1 << bits; static Bitboard occ[4096], ref[4096], used[4096]; static int epoch[4096]; int ep = 0;
        Bitboard s = 0; for (int i = 0; i < n; i++) { occ[i] = s; ref[i] = slow_attacks(sq, s, d); s = (s - m->mask) & m->mask; }
        memset(epoch, 0, sizeof epoch);
        for (;;) {
            m->magic = sparse();
            if (bb_popcount((m->mask * m->magic) >> 56) < 6) continue;
            ep++; int ok = 1;
            for (int i = 0; i < n && ok; i++) {
                int idx = (int)((occ[i] * m->magic) >> m->shift);
                if (epoch[idx] != ep) { epoch[idx] = ep; used[idx] = ref[i]; m->tab[idx] = ref[i]; }
                else if (used[idx] != ref[i]) ok = 0;
            }
            if (ok) break;
        }
    }
}
static inline Bitboard rook_magic(Bitboard occ, int sq) { const Magic *m = &RM[sq]; return m->tab[((occ & m->mask) * m->magic) >> m->shift]; }
static inline Bitboard bishop_magic(Bitboard occ, int sq) { const Magic *m = &BM[sq]; return m->tab[((occ & m->mask) * m->magic) >> m->shift]; }

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
#define N 4096
static Bitboard occs[N]; static unsigned char sqs[N];

#define TIMEIT(name, expr) do { double best = 1e9; Bitboard sink = 0; \
    for (int rep = 0; rep < 15; rep++) { double t0 = now(); \
        for (int it = 0; it < 2000; it++) for (int i = 0; i < N; i++) { Bitboard o = occs[i]; int sq = sqs[i]; sink += (expr); o ^= sink & 1; } \
        double dt = now() - t0; if (dt < best) best = dt; } \
    printf("| %-22s | %6.2f |\n", name, best / (2000.0 * N) * 1e9); if (sink == 42) puts(""); } while (0)

int main(void) {
    init_tables();
    build(RM, rtab, RD); build(BM, btab, BD);
    for (int i = 0; i < N; i++) { occs[i] = rnd() & rnd(); sqs[i] = rnd() & 63; }
    long bad = 0;
    for (int i = 0; i < 200000; i++) {
        Bitboard o = rnd() & rnd(); int sq = rnd() & 63;
        if (rook_attacks(o, sq) != rook_magic(o, sq)) bad++;
        if (bishop_attacks(o, sq) != bishop_magic(o, sq)) bad++;
    }
    printf("cross-check HQ vs magic on 400000 lookups: %ld mismatches\n\n", bad);
    printf("| lookup                 | ns/call, throughput (best of 15) |\n|---|---|\n");
    TIMEIT("rook HQ+rank LUT", rook_attacks(o, sq));
    TIMEIT("rook magic", rook_magic(o, sq));
    TIMEIT("bishop HQ", bishop_attacks(o, sq));
    TIMEIT("bishop magic", bishop_magic(o, sq));
    TIMEIT("queen HQ", queen_attacks(o, sq));
    TIMEIT("queen magic", rook_magic(o, sq) | bishop_magic(o, sq));
    printf("\nmagic table memory: rook %zu KiB, bishop %zu KiB; HQ: %zu KiB (rank LUT) + masks\n",
           sizeof rtab / 1024, sizeof btab / 1024, sizeof rank_attacks_lut / 1024);
    return bad != 0;
}
