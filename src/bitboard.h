#ifndef BITBOARD_H
#define BITBOARD_H

#include "types.h"

/* ── Rank / file constant masks ──────────────────────────────────────────── */

static const Bitboard RANK_BB[8] = {
    0x00000000000000FFULL,   /* rank 1 */
    0x000000000000FF00ULL,   /* rank 2 */
    0x0000000000FF0000ULL,   /* rank 3 */
    0x00000000FF000000ULL,   /* rank 4 */
    0x000000FF00000000ULL,   /* rank 5 */
    0x0000FF0000000000ULL,   /* rank 6 */
    0x00FF000000000000ULL,   /* rank 7 */
    0xFF00000000000000ULL,   /* rank 8 */
};

static const Bitboard FILE_BB[8] = {
    0x0101010101010101ULL,   /* file A */
    0x0202020202020202ULL,   /* file B */
    0x0404040404040404ULL,   /* file C */
    0x0808080808080808ULL,   /* file D */
    0x1010101010101010ULL,   /* file E */
    0x2020202020202020ULL,   /* file F */
    0x4040404040404040ULL,   /* file G */
    0x8080808080808080ULL,   /* file H */
};

/* ── Precomputed attack tables (defined in bitboard.c) ───────────────────── */

extern Bitboard KNIGHT_ATTACKS[64];
extern Bitboard KING_ATTACKS[64];
extern Bitboard PAWN_ATTACKS[2][64];

/* Per-square ray masks used by Hyperbola Quintessence */
extern Bitboard DIAG_MASK[64];       /* diagonal (a1-h8 direction) */
extern Bitboard ANTI_DIAG_MASK[64];  /* anti-diagonal (a8-h1 direction) */
extern Bitboard RANK_MASK_SQ[64];    /* the rank containing this square */
extern Bitboard FILE_MASK_SQ[64];    /* the file containing this square */

/* Rank attack lookup table: rank_attacks_lut[file][8-bit occ] -> 8-bit attacks */
extern uint8_t rank_attacks_lut[8][256];

void init_tables(void);

/* ── Inline bit utilities ────────────────────────────────────────────────── */

static inline int bb_lsb(Bitboard b) {
    return __builtin_ctzll(b);
}

static inline int bb_popcount(Bitboard b) {
    return __builtin_popcountll(b);
}

static inline Bitboard bb_sq(int sq) {
    return (Bitboard)1ULL << sq;
}

/* Clear and return the LSB in one go */
static inline int bb_pop_lsb(Bitboard *b) {
    int sq = __builtin_ctzll(*b);
    *b &= *b - 1;
    return sq;
}

/* ── Hyperbola Quintessence slider attacks ───────────────────────────────
   Generates attacks along a single ray (rank, file, diagonal, anti-diagonal)
   for a slider on `sq` given occupancy `occ` and ray `mask`.

   Formula:  attacks = ((o - 2r) ^ reverse(reverse(o) - 2*reverse(r))) & mask
   where o = occ & mask, r = bit(sq).
   ─────────────────────────────────────────────────────────────────────── */
static inline Bitboard hq_attacks(Bitboard occ, int sq, Bitboard mask) {
    Bitboard o  = occ & mask;
    Bitboard r  = __builtin_bswap64(o);
    int      rs = sq ^ 56;   /* reversed square index */
    return mask & (
        (o  - (bb_sq(sq) << 1)) ^
        __builtin_bswap64(r - (bb_sq(rs) << 1))
    );
}

/* Convenience wrappers */
static inline Bitboard bishop_attacks(Bitboard occ, int sq) {
    return hq_attacks(occ, sq, DIAG_MASK[sq]) |
           hq_attacks(occ, sq, ANTI_DIAG_MASK[sq]);
}

/* Rank attacks via lookup table (HQ with bswap64 is incorrect for horizontal
   rays because bswap reverses byte order, not bit order within a byte). */
static inline Bitboard rank_attacks(Bitboard occ, int sq) {
    int rank  = SQ_RANK(sq);
    int file  = SQ_FILE(sq);
    uint8_t occ8 = (uint8_t)((occ >> (rank * 8)) & 0xFF);
    return (Bitboard)rank_attacks_lut[file][occ8] << (rank * 8);
}

static inline Bitboard rook_attacks(Bitboard occ, int sq) {
    return rank_attacks(occ, sq) |
           hq_attacks(occ, sq, FILE_MASK_SQ[sq]);
}

static inline Bitboard queen_attacks(Bitboard occ, int sq) {
    return bishop_attacks(occ, sq) | rook_attacks(occ, sq);
}

#endif /* BITBOARD_H */
