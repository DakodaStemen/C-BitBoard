#include "movegen.h"
#include "bitboard.h"
#include "position.h"

/* ── Internal add-move helper ────────────────────────────────────────────── */

static inline void add(MoveList *list, int from, int to,
                       int promo, uint32_t flags) {
    list->moves[list->count++] = make_move(from, to, promo, flags);
}

/* Emit moves for every set bit in `targets` from a fixed `from` square */
static inline void add_targets(MoveList *list, int from,
                                Bitboard targets, uint32_t flags) {
    while (targets) {
        int to = bb_pop_lsb(&targets);
        add(list, from, to, 0, flags);
    }
}

/* ── Pawn move generation ────────────────────────────────────────────────── */

static void gen_pawn_moves(const Position *pos, MoveList *list) {
    Color us   = pos->side;
    Color them = us ^ 1;
    Bitboard pawns  = pos->pieces[us][PAWN];
    Bitboard enemy  = pos->by_color[them];
    Bitboard empty  = ~pos->occupied;

    Bitboard promo_rank;
    int push1, push2, cap_l, cap_r;
    Bitboard not_file_a = ~FILE_BB[0];
    Bitboard not_file_h = ~FILE_BB[7];

    if (us == WHITE) {
        promo_rank = RANK_BB[7];
        push1  =  8; push2  = 16;
        cap_l  =  7; cap_r  =  9;
    } else {
        promo_rank = RANK_BB[0];
        push1  = -8; push2  = -16;
        cap_l  = -9; cap_r  = -7;
    }

    /* Single push */
    Bitboard sp = (us == WHITE) ? (pawns << 8 & empty) : (pawns >> 8 & empty);

    /* Double push (from start rank, through empty square) */
    Bitboard dp;
    if (us == WHITE)
        dp = (sp & RANK_BB[2]) << 8 & empty;
    else
        dp = (sp & RANK_BB[5]) >> 8 & empty;

    /* Captures */
    Bitboard cl, cr;
    if (us == WHITE) {
        cl = (pawns << 7) & not_file_h & enemy;
        cr = (pawns << 9) & not_file_a & enemy;
    } else {
        cl = (pawns >> 9) & not_file_h & enemy;
        cr = (pawns >> 7) & not_file_a & enemy;
    }

    /* En passant */
    Bitboard ep_bb = (pos->ep_sq != NO_SQ) ? bb_sq(pos->ep_sq) : 0ULL;
    Bitboard ep_cl = 0, ep_cr = 0;
    if (ep_bb) {
        if (us == WHITE) {
            ep_cl = (pawns << 7) & not_file_h & ep_bb;
            ep_cr = (pawns << 9) & not_file_a & ep_bb;
        } else {
            ep_cl = (pawns >> 9) & not_file_h & ep_bb;
            ep_cr = (pawns >> 7) & not_file_a & ep_bb;
        }
    }

    /* Emit single pushes (split promos and non-promos) */
    {
        Bitboard promo = sp & promo_rank;
        Bitboard quiet = sp & ~promo_rank;
        while (quiet) {
            int to = bb_pop_lsb(&quiet);
            add(list, to - push1, to, 0, 0);
        }
        while (promo) {
            int to = bb_pop_lsb(&promo);
            for (int p = PROMO_KNIGHT; p <= PROMO_QUEEN; p++)
                add(list, to - push1, to, p, FLAG_PROMO);
        }
    }

    /* Emit double pushes */
    while (dp) {
        int to = bb_pop_lsb(&dp);
        add(list, to - push2, to, 0, FLAG_DOUBLE_PUSH);
    }

    /* Emit captures left */
    {
        Bitboard promo = cl & promo_rank;
        Bitboard nopr  = cl & ~promo_rank;
        while (nopr) {
            int to = bb_pop_lsb(&nopr);
            add(list, to - cap_l, to, 0, FLAG_CAPTURE);
        }
        while (promo) {
            int to = bb_pop_lsb(&promo);
            for (int p = PROMO_KNIGHT; p <= PROMO_QUEEN; p++)
                add(list, to - cap_l, to, p, FLAG_CAPTURE | FLAG_PROMO);
        }
    }

    /* Emit captures right */
    {
        Bitboard promo = cr & promo_rank;
        Bitboard nopr  = cr & ~promo_rank;
        while (nopr) {
            int to = bb_pop_lsb(&nopr);
            add(list, to - cap_r, to, 0, FLAG_CAPTURE);
        }
        while (promo) {
            int to = bb_pop_lsb(&promo);
            for (int p = PROMO_KNIGHT; p <= PROMO_QUEEN; p++)
                add(list, to - cap_r, to, p, FLAG_CAPTURE | FLAG_PROMO);
        }
    }

    /* Emit en passant */
    while (ep_cl) {
        int to = bb_pop_lsb(&ep_cl);
        add(list, to - cap_l, to, 0, FLAG_CAPTURE | FLAG_EP);
    }
    while (ep_cr) {
        int to = bb_pop_lsb(&ep_cr);
        add(list, to - cap_r, to, 0, FLAG_CAPTURE | FLAG_EP);
    }
}

/* ── Knight move generation ──────────────────────────────────────────────── */

static void gen_knight_moves(const Position *pos, MoveList *list) {
    Color us     = pos->side;
    Bitboard own = pos->by_color[us];
    Bitboard bb  = pos->pieces[us][KNIGHT];
    while (bb) {
        int from    = bb_pop_lsb(&bb);
        Bitboard atk = KNIGHT_ATTACKS[from] & ~own;
        Bitboard cap = atk & pos->by_color[us ^ 1];
        Bitboard qui = atk & ~pos->occupied;
        add_targets(list, from, cap, FLAG_CAPTURE);
        add_targets(list, from, qui, 0);
    }
}

/* ── Bishop move generation ──────────────────────────────────────────────── */

static void gen_bishop_moves(const Position *pos, MoveList *list) {
    Color us     = pos->side;
    Bitboard bb  = pos->pieces[us][BISHOP];
    Bitboard own = pos->by_color[us];
    while (bb) {
        int from     = bb_pop_lsb(&bb);
        Bitboard atk = bishop_attacks(pos->occupied, from) & ~own;
        Bitboard cap = atk & pos->by_color[us ^ 1];
        Bitboard qui = atk & ~pos->occupied;
        add_targets(list, from, cap, FLAG_CAPTURE);
        add_targets(list, from, qui, 0);
    }
}

/* ── Rook move generation ────────────────────────────────────────────────── */

static void gen_rook_moves(const Position *pos, MoveList *list) {
    Color us     = pos->side;
    Bitboard bb  = pos->pieces[us][ROOK];
    Bitboard own = pos->by_color[us];
    while (bb) {
        int from     = bb_pop_lsb(&bb);
        Bitboard atk = rook_attacks(pos->occupied, from) & ~own;
        Bitboard cap = atk & pos->by_color[us ^ 1];
        Bitboard qui = atk & ~pos->occupied;
        add_targets(list, from, cap, FLAG_CAPTURE);
        add_targets(list, from, qui, 0);
    }
}

/* ── Queen move generation ───────────────────────────────────────────────── */

static void gen_queen_moves(const Position *pos, MoveList *list) {
    Color us     = pos->side;
    Bitboard bb  = pos->pieces[us][QUEEN];
    Bitboard own = pos->by_color[us];
    while (bb) {
        int from     = bb_pop_lsb(&bb);
        Bitboard atk = queen_attacks(pos->occupied, from) & ~own;
        Bitboard cap = atk & pos->by_color[us ^ 1];
        Bitboard qui = atk & ~pos->occupied;
        add_targets(list, from, cap, FLAG_CAPTURE);
        add_targets(list, from, qui, 0);
    }
}

/* ── King move generation ────────────────────────────────────────────────── */

static void gen_king_moves(const Position *pos, MoveList *list) {
    Color us     = pos->side;
    Color them   = us ^ 1;
    Bitboard own = pos->by_color[us];
    int from     = bb_lsb(pos->pieces[us][KING]);

    /* Normal king moves — we'll filter check after make */
    Bitboard atk = KING_ATTACKS[from] & ~own;
    Bitboard cap = atk & pos->by_color[them];
    Bitboard qui = atk & ~pos->occupied;
    add_targets(list, from, cap, FLAG_CAPTURE);
    add_targets(list, from, qui, 0);
}

/* ── Attack computation ──────────────────────────────────────────────────── */

Bitboard attacks_by(const Position *pos, Color by) {
    Bitboard atk = 0;
    Bitboard occ = pos->occupied;

    /* Pawns */
    Bitboard p = pos->pieces[by][PAWN];
    while (p) {
        int sq = bb_pop_lsb(&p);
        atk |= PAWN_ATTACKS[by][sq];
    }

    /* Knights */
    Bitboard n = pos->pieces[by][KNIGHT];
    while (n) {
        int sq = bb_pop_lsb(&n);
        atk |= KNIGHT_ATTACKS[sq];
    }

    /* Bishops */
    Bitboard b = pos->pieces[by][BISHOP];
    while (b) {
        int sq = bb_pop_lsb(&b);
        atk |= bishop_attacks(occ, sq);
    }

    /* Rooks */
    Bitboard r = pos->pieces[by][ROOK];
    while (r) {
        int sq = bb_pop_lsb(&r);
        atk |= rook_attacks(occ, sq);
    }

    /* Queens */
    Bitboard q = pos->pieces[by][QUEEN];
    while (q) {
        int sq = bb_pop_lsb(&q);
        atk |= queen_attacks(occ, sq);
    }

    /* King */
    atk |= KING_ATTACKS[bb_lsb(pos->pieces[by][KING])];

    return atk;
}

int is_in_check(const Position *pos, Color color) {
    int king_sq = bb_lsb(pos->pieces[color][KING]);
    Color them  = color ^ 1;

    /* Quick checks before full attack scan */
    if (KNIGHT_ATTACKS[king_sq]    & pos->pieces[them][KNIGHT])  return 1;
    if (PAWN_ATTACKS[color][king_sq] & pos->pieces[them][PAWN])  return 1;
    if (KING_ATTACKS[king_sq]      & pos->pieces[them][KING])    return 1;

    Bitboard occ = pos->occupied;
    if (bishop_attacks(occ, king_sq) & (pos->pieces[them][BISHOP] | pos->pieces[them][QUEEN])) return 1;
    if (rook_attacks(occ, king_sq)   & (pos->pieces[them][ROOK]   | pos->pieces[them][QUEEN])) return 1;

    return 0;
}

/* ── Castling ────────────────────────────────────────────────────────────── */

static void gen_castling(const Position *pos, MoveList *list) {
    Color us  = pos->side;
    Bitboard occ = pos->occupied;

    /* We need to know squares attacked by opponent to verify king path.
       Temporarily remove our king from occupancy so sliders can't be
       "blocked" by the king itself when checking king-path squares. */
    int king_sq = bb_lsb(pos->pieces[us][KING]);
    Bitboard occ_no_king = occ & ~bb_sq(king_sq);

    /* Build a position with the king removed to test attacks */
    Position tmp = *pos;
    tmp.occupied = occ_no_king;

    if (us == WHITE) {
        /* Kingside: e1 f1 g1 must not be attacked, f1 g1 must be empty */
        if ((pos->castling & CASTLE_WK) &&
            !(occ & (bb_sq(F1) | bb_sq(G1))) &&
            !is_in_check(pos, WHITE) &&
            !(attacks_by(&tmp, BLACK) & (bb_sq(F1) | bb_sq(G1)))) {
            add(list, E1, G1, 0, FLAG_CASTLE);
        }
        /* Queenside: e1 d1 c1 must not be attacked, b1 c1 d1 must be empty */
        if ((pos->castling & CASTLE_WQ) &&
            !(occ & (bb_sq(B1) | bb_sq(C1) | bb_sq(D1))) &&
            !is_in_check(pos, WHITE) &&
            !(attacks_by(&tmp, BLACK) & (bb_sq(D1) | bb_sq(C1)))) {
            add(list, E1, C1, 0, FLAG_CASTLE);
        }
    } else {
        /* Kingside */
        if ((pos->castling & CASTLE_BK) &&
            !(occ & (bb_sq(F8) | bb_sq(G8))) &&
            !is_in_check(pos, BLACK) &&
            !(attacks_by(&tmp, WHITE) & (bb_sq(F8) | bb_sq(G8)))) {
            add(list, E8, G8, 0, FLAG_CASTLE);
        }
        /* Queenside */
        if ((pos->castling & CASTLE_BQ) &&
            !(occ & (bb_sq(B8) | bb_sq(C8) | bb_sq(D8))) &&
            !is_in_check(pos, BLACK) &&
            !(attacks_by(&tmp, WHITE) & (bb_sq(D8) | bb_sq(C8)))) {
            add(list, E8, C8, 0, FLAG_CASTLE);
        }
    }
}

/* ── Top-level generate_moves ────────────────────────────────────────────── */

void generate_moves(const Position *pos, MoveList *list) {
    list->count = 0;
    gen_pawn_moves(pos, list);
    gen_knight_moves(pos, list);
    gen_bishop_moves(pos, list);
    gen_rook_moves(pos, list);
    gen_queen_moves(pos, list);
    gen_king_moves(pos, list);
    gen_castling(pos, list);
}
