#include "bitboard.h"
#include "types.h"

/* ── Attack table storage ────────────────────────────────────────────────── */

Bitboard KNIGHT_ATTACKS[64];
Bitboard KING_ATTACKS[64];
Bitboard PAWN_ATTACKS[2][64];

Bitboard DIAG_MASK[64];
Bitboard ANTI_DIAG_MASK[64];
Bitboard RANK_MASK_SQ[64];
Bitboard FILE_MASK_SQ[64];

/* rank_attacks_lut[file][occ8]: attacks for a rook on `file` when the
   8-bit occupancy of its rank is `occ8` (the rook's own bit is included
   in occ8 but that square is excluded from the result, as usual). */
uint8_t rank_attacks_lut[8][256];

/* ── Initialization ──────────────────────────────────────────────────────── */

static void init_knight_attacks(void) {
    for (int sq = 0; sq < 64; sq++) {
        Bitboard b = bb_sq(sq);
        Bitboard a = 0;
        /* +2 rank, +1 file  */  a |= (b << 17) & ~FILE_BB[0];
        /* +2 rank, -1 file  */  a |= (b << 15) & ~FILE_BB[7];
        /* +1 rank, +2 file  */  a |= (b << 10) & ~(FILE_BB[0] | FILE_BB[1]);
        /* +1 rank, -2 file  */  a |= (b <<  6) & ~(FILE_BB[6] | FILE_BB[7]);
        /* -1 rank, +2 file  */  a |= (b >>  6) & ~(FILE_BB[0] | FILE_BB[1]);
        /* -1 rank, -2 file  */  a |= (b >> 10) & ~(FILE_BB[6] | FILE_BB[7]);
        /* -2 rank, +1 file  */  a |= (b >> 15) & ~FILE_BB[0];
        /* -2 rank, -1 file  */  a |= (b >> 17) & ~FILE_BB[7];
        KNIGHT_ATTACKS[sq] = a;
    }
}

static void init_king_attacks(void) {
    for (int sq = 0; sq < 64; sq++) {
        Bitboard b = bb_sq(sq);
        Bitboard a = 0;
        a |= (b << 8);
        a |= (b >> 8);
        a |= (b << 1) & ~FILE_BB[0];
        a |= (b >> 1) & ~FILE_BB[7];
        a |= (b << 9) & ~FILE_BB[0];
        a |= (b >> 9) & ~FILE_BB[7];
        a |= (b << 7) & ~FILE_BB[7];
        a |= (b >> 7) & ~FILE_BB[0];
        KING_ATTACKS[sq] = a;
    }
}

static void init_pawn_attacks(void) {
    for (int sq = 0; sq < 64; sq++) {
        Bitboard b = bb_sq(sq);
        PAWN_ATTACKS[WHITE][sq] = ((b << 9) & ~FILE_BB[0]) |
                                   ((b << 7) & ~FILE_BB[7]);
        PAWN_ATTACKS[BLACK][sq] = ((b >> 7) & ~FILE_BB[0]) |
                                   ((b >> 9) & ~FILE_BB[7]);
    }
}

static void init_ray_masks(void) {
    for (int sq = 0; sq < 64; sq++) {
        int r = SQ_RANK(sq);
        int f = SQ_FILE(sq);

        /* Rank mask: all squares on the same rank */
        RANK_MASK_SQ[sq] = RANK_BB[r];

        /* File mask: all squares on the same file */
        FILE_MASK_SQ[sq] = FILE_BB[f];

        /* Diagonal (bottom-left to top-right, a1-h8 direction) */
        Bitboard diag = 0;
        for (int rr = 0; rr < 8; rr++) {
            int ff = f + (rr - r);
            if (ff >= 0 && ff < 8)
                diag |= bb_sq(SQ(rr, ff));
        }
        DIAG_MASK[sq] = diag;

        /* Anti-diagonal (bottom-right to top-left, a8-h1 direction) */
        Bitboard anti = 0;
        for (int rr = 0; rr < 8; rr++) {
            int ff = f - (rr - r);
            if (ff >= 0 && ff < 8)
                anti |= bb_sq(SQ(rr, ff));
        }
        ANTI_DIAG_MASK[sq] = anti;
    }
}

static void init_rank_attacks(void) {
    for (int file = 0; file < 8; file++) {
        for (int occ = 0; occ < 256; occ++) {
            uint8_t atk = 0;
            for (int f = file + 1; f < 8; f++) {
                atk |= (uint8_t)(1 << f);
                if (occ & (1 << f)) break;   /* blocked */
            }
            for (int f = file - 1; f >= 0; f--) {
                atk |= (uint8_t)(1 << f);
                if (occ & (1 << f)) break;   /* blocked */
            }
            rank_attacks_lut[file][occ] = atk;
        }
    }
}

void init_tables(void) {
    init_knight_attacks();
    init_king_attacks();
    init_pawn_attacks();
    init_ray_masks();
    init_rank_attacks();
}
