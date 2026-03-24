#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

/* ── Basic types ─────────────────────────────────────────────────────────── */

typedef uint64_t Bitboard;
typedef uint32_t Move;

/* ── Enumerations ────────────────────────────────────────────────────────── */

typedef enum {
    WHITE = 0, BLACK = 1
} Color;

typedef enum {
    PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5
} PieceType;

/* Square indices: LSB=a1 (0), MSB=h8 (63).
   Index = rank*8 + file,  rank 0=rank1 .. rank 7=rank8  */
typedef enum {
    A1= 0, B1= 1, C1= 2, D1= 3, E1= 4, F1= 5, G1= 6, H1= 7,
    A2= 8, B2= 9, C2=10, D2=11, E2=12, F2=13, G2=14, H2=15,
    A3=16, B3=17, C3=18, D3=19, E3=20, F3=21, G3=22, H3=23,
    A4=24, B4=25, C4=26, D4=27, E4=28, F4=29, G4=30, H4=31,
    A5=32, B5=33, C5=34, D5=35, E5=36, F5=37, G5=38, H5=39,
    A6=40, B6=41, C6=42, D6=43, E6=44, F6=45, G6=46, H6=47,
    A7=48, B7=49, C7=50, D7=51, E7=52, F7=53, G7=54, H7=55,
    A8=56, B8=57, C8=58, D8=59, E8=60, F8=61, G8=62, H8=63,
    NO_SQ = 64
} Square;

/* ── Move encoding (32-bit) ──────────────────────────────────────────────
   bits  5: 0  from square  (0..63)
   bits 11: 6  to square    (0..63)
   bits 13:12  promo piece  (0=N 1=B 2=R 3=Q; valid only when FLAG_PROMO set)
   bit     14  FLAG_CAPTURE
   bit     15  FLAG_PROMO
   bit     16  FLAG_EP
   bit     17  FLAG_CASTLE
   bit     18  FLAG_DOUBLE_PUSH
   ─────────────────────────────────────────────────────────────────────── */

#define MOVE_FROM(m)         ((int)((m) & 0x3F))
#define MOVE_TO(m)           ((int)(((m) >> 6) & 0x3F))
#define MOVE_PROMO(m)        ((int)(((m) >> 12) & 0x3))
#define MOVE_IS_CAPTURE(m)   ((m) & (1u << 14))
#define MOVE_IS_PROMO(m)     ((m) & (1u << 15))
#define MOVE_IS_EP(m)        ((m) & (1u << 16))
#define MOVE_IS_CASTLE(m)    ((m) & (1u << 17))
#define MOVE_IS_DP(m)        ((m) & (1u << 18))

#define FLAG_CAPTURE     (1u << 14)
#define FLAG_PROMO       (1u << 15)
#define FLAG_EP          (1u << 16)
#define FLAG_CASTLE      (1u << 17)
#define FLAG_DOUBLE_PUSH (1u << 18)

/* Promo piece encoding (2-bit field) */
#define PROMO_KNIGHT 0
#define PROMO_BISHOP 1
#define PROMO_ROOK   2
#define PROMO_QUEEN  3

/* Build a move */
static inline Move make_move(int from, int to, int promo_piece, uint32_t flags) {
    return (Move)((uint32_t)from | ((uint32_t)to << 6) |
                  ((uint32_t)promo_piece << 12) | flags);
}

/* ── Castling rights bitmask ─────────────────────────────────────────────
   bit 0 = white kingside   (WK)
   bit 1 = white queenside  (WQ)
   bit 2 = black kingside   (BK)
   bit 3 = black queenside  (BQ)  */
#define CASTLE_WK 1u
#define CASTLE_WQ 2u
#define CASTLE_BK 4u
#define CASTLE_BQ 8u

/* ── Position ────────────────────────────────────────────────────────────── */

typedef struct {
    Bitboard pieces[2][6];   /* [color][piece_type] */
    Bitboard occupied;       /* union of all pieces */
    Bitboard by_color[2];    /* union of each side's pieces */
    Color    side;           /* side to move */
    int      ep_sq;          /* en passant target square, or NO_SQ */
    uint8_t  castling;       /* castling availability bitmask */
    int      halfmove;       /* half-move clock (50-move rule) */
    int      fullmove;       /* full-move number */
} Position;

/* ── Undo information ────────────────────────────────────────────────────── */

typedef struct {
    Move    move;
    int     ep_sq;
    uint8_t castling;
    int     halfmove;
    int     captured_type;   /* PieceType of captured piece, or -1 */
} UndoInfo;

/* ── Move list ───────────────────────────────────────────────────────────── */

#define MAX_MOVES 256

typedef struct {
    Move moves[MAX_MOVES];
    int  count;
} MoveList;

/* ── Misc helpers ────────────────────────────────────────────────────────── */

#define SQ_FILE(sq)  ((sq) & 7)
#define SQ_RANK(sq)  ((sq) >> 3)
#define SQ(r, f)     ((r)*8 + (f))

static const char PIECE_CHARS[2][6] = {
    { 'P', 'N', 'B', 'R', 'Q', 'K' },
    { 'p', 'n', 'b', 'r', 'q', 'k' }
};

static const char PROMO_CHARS[4] = { 'n', 'b', 'r', 'q' };

#endif /* TYPES_H */
