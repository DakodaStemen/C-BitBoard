#include <stdio.h>
#include <string.h>
#include "position.h"
#include "bitboard.h"

/* ── Occupancy helpers ───────────────────────────────────────────────────── */

void refresh_occupancy(Position *pos) {
    pos->by_color[WHITE] = pos->pieces[WHITE][PAWN]   | pos->pieces[WHITE][KNIGHT] |
                           pos->pieces[WHITE][BISHOP] | pos->pieces[WHITE][ROOK]   |
                           pos->pieces[WHITE][QUEEN]  | pos->pieces[WHITE][KING];
    pos->by_color[BLACK] = pos->pieces[BLACK][PAWN]   | pos->pieces[BLACK][KNIGHT] |
                           pos->pieces[BLACK][BISHOP] | pos->pieces[BLACK][ROOK]   |
                           pos->pieces[BLACK][QUEEN]  | pos->pieces[BLACK][KING];
    pos->occupied        = pos->by_color[WHITE] | pos->by_color[BLACK];
}

/* ── FEN parser ──────────────────────────────────────────────────────────── */

int parse_fen(Position *pos, const char *fen) {
    memset(pos, 0, sizeof *pos);
    pos->ep_sq    = NO_SQ;
    pos->fullmove = 1;

    /* --- piece placement --- */
    int rank = 7, file = 0;
    const char *p = fen;
    while (*p && *p != ' ') {
        if (*p == '/') {
            rank--;
            file = 0;
        } else if (*p >= '1' && *p <= '8') {
            file += *p - '0';
        } else {
            Color c;
            PieceType pt;
            switch (*p) {
                case 'P': c = WHITE; pt = PAWN;   break;
                case 'N': c = WHITE; pt = KNIGHT; break;
                case 'B': c = WHITE; pt = BISHOP; break;
                case 'R': c = WHITE; pt = ROOK;   break;
                case 'Q': c = WHITE; pt = QUEEN;  break;
                case 'K': c = WHITE; pt = KING;   break;
                case 'p': c = BLACK; pt = PAWN;   break;
                case 'n': c = BLACK; pt = KNIGHT; break;
                case 'b': c = BLACK; pt = BISHOP; break;
                case 'r': c = BLACK; pt = ROOK;   break;
                case 'q': c = BLACK; pt = QUEEN;  break;
                case 'k': c = BLACK; pt = KING;   break;
                default:  return -1;
            }
            pos->pieces[c][pt] |= bb_sq(SQ(rank, file));
            file++;
        }
        p++;
    }

    /* --- side to move --- */
    if (*p == ' ') p++;
    pos->side = (*p == 'w') ? WHITE : BLACK;
    if (*p) p++;

    /* --- castling rights --- */
    if (*p == ' ') p++;
    pos->castling = 0;
    while (*p && *p != ' ') {
        switch (*p) {
            case 'K': pos->castling |= CASTLE_WK; break;
            case 'Q': pos->castling |= CASTLE_WQ; break;
            case 'k': pos->castling |= CASTLE_BK; break;
            case 'q': pos->castling |= CASTLE_BQ; break;
        }
        p++;
    }

    /* --- en passant square --- */
    if (*p == ' ') p++;
    if (*p && *p != '-') {
        int ep_file = *p - 'a';
        p++;
        int ep_rank = *p - '1';
        pos->ep_sq = SQ(ep_rank, ep_file);
        p++;
    } else {
        if (*p) p++;  /* skip '-' */
    }

    /* --- half-move clock --- */
    if (*p == ' ') p++;
    if (*p && *p != ' ') {
        pos->halfmove = 0;
        while (*p && *p != ' ') {
            pos->halfmove = pos->halfmove * 10 + (*p - '0');
            p++;
        }
    }

    /* --- full-move number --- */
    if (*p == ' ') p++;
    if (*p) {
        pos->fullmove = 0;
        while (*p && *p != ' ') {
            pos->fullmove = pos->fullmove * 10 + (*p - '0');
            p++;
        }
    }

    refresh_occupancy(pos);
    return 0;
}

/* ── Find which piece type is on a square ────────────────────────────────── */

static int piece_on(const Position *pos, Color c, int sq) {
    Bitboard b = bb_sq(sq);
    for (int pt = PAWN; pt <= KING; pt++)
        if (pos->pieces[c][pt] & b) return pt;
    return -1;
}

/* Returns the castling-rights mask for a given square.
   Any move touching this square will AND the rights with this value. */
static inline uint8_t cr_mask(int sq) {
    switch (sq) {
        case A1: return (uint8_t)~CASTLE_WQ;
        case H1: return (uint8_t)~CASTLE_WK;
        case E1: return (uint8_t)~(CASTLE_WK | CASTLE_WQ);
        case A8: return (uint8_t)~CASTLE_BQ;
        case H8: return (uint8_t)~CASTLE_BK;
        case E8: return (uint8_t)~(CASTLE_BK | CASTLE_BQ);
        default: return 0xFF;
    }
}

/* ── Make move ───────────────────────────────────────────────────────────── */

void do_move(Position *pos, Move m, UndoInfo *undo) {
    int from  = MOVE_FROM(m);
    int to    = MOVE_TO(m);
    Color us  = pos->side;
    Color them = us ^ 1;

    /* Save undo state */
    undo->move           = m;
    undo->ep_sq          = pos->ep_sq;
    undo->castling       = pos->castling;
    undo->halfmove       = pos->halfmove;
    undo->captured_type  = -1;

    /* Identify moving piece */
    int mpt = piece_on(pos, us, from);

    /* Handle capture */
    if (MOVE_IS_EP(m)) {
        /* Captured pawn is behind the to-square */
        int cap_sq = (us == WHITE) ? to - 8 : to + 8;
        pos->pieces[them][PAWN] &= ~bb_sq(cap_sq);
        undo->captured_type = PAWN;
    } else if (MOVE_IS_CAPTURE(m)) {
        int cpt = piece_on(pos, them, to);
        if (cpt >= 0) {
            pos->pieces[them][cpt] &= ~bb_sq(to);
            undo->captured_type = cpt;
        }
    }

    /* Move the piece */
    pos->pieces[us][mpt] &= ~bb_sq(from);
    if (MOVE_IS_PROMO(m)) {
        static const int PROMO_TO_PT[4] = { KNIGHT, BISHOP, ROOK, QUEEN };
        pos->pieces[us][PROMO_TO_PT[MOVE_PROMO(m)]] |= bb_sq(to);
    } else {
        pos->pieces[us][mpt] |= bb_sq(to);
    }

    /* Handle castling: also move the rook */
    if (MOVE_IS_CASTLE(m)) {
        if (to == G1) { /* white kingside */
            pos->pieces[WHITE][ROOK] &= ~bb_sq(H1);
            pos->pieces[WHITE][ROOK] |=  bb_sq(F1);
        } else if (to == C1) { /* white queenside */
            pos->pieces[WHITE][ROOK] &= ~bb_sq(A1);
            pos->pieces[WHITE][ROOK] |=  bb_sq(D1);
        } else if (to == G8) { /* black kingside */
            pos->pieces[BLACK][ROOK] &= ~bb_sq(H8);
            pos->pieces[BLACK][ROOK] |=  bb_sq(F8);
        } else if (to == C8) { /* black queenside */
            pos->pieces[BLACK][ROOK] &= ~bb_sq(A8);
            pos->pieces[BLACK][ROOK] |=  bb_sq(D8);
        }
    }

    /* Update castling rights */
    pos->castling &= cr_mask(from) & cr_mask(to);

    /* Update en passant square */
    pos->ep_sq = MOVE_IS_DP(m) ? (us == WHITE ? from + 8 : from - 8) : NO_SQ;

    /* Half-move clock */
    if (mpt == PAWN || MOVE_IS_CAPTURE(m) || MOVE_IS_EP(m))
        pos->halfmove = 0;
    else
        pos->halfmove++;

    /* Full-move number */
    if (us == BLACK) pos->fullmove++;

    /* Flip side */
    pos->side = them;

    refresh_occupancy(pos);
}

/* ── Unmake move ─────────────────────────────────────────────────────────── */

void undo_move(Position *pos, const UndoInfo *undo) {
    Move m    = undo->move;
    int  from = MOVE_FROM(m);
    int  to   = MOVE_TO(m);

    /* Restore side (who made the move) */
    pos->side ^= 1;
    Color us   = pos->side;
    Color them = us ^ 1;

    /* Restore scalar fields */
    pos->ep_sq    = undo->ep_sq;
    pos->castling = undo->castling;
    pos->halfmove = undo->halfmove;
    if (us == BLACK) pos->fullmove--;

    /* Determine what piece is currently on `to` (after promotion it's promo piece) */
    int mpt;
    if (MOVE_IS_PROMO(m)) {
        static const int PROMO_TO_PT[4] = { KNIGHT, BISHOP, ROOK, QUEEN };
        int promo_pt = PROMO_TO_PT[MOVE_PROMO(m)];
        pos->pieces[us][promo_pt] &= ~bb_sq(to);
        mpt = PAWN;
    } else {
        mpt = piece_on(pos, us, to);
        pos->pieces[us][mpt] &= ~bb_sq(to);
    }

    /* Restore moving piece to `from` */
    pos->pieces[us][mpt] |= bb_sq(from);

    /* Restore captured piece */
    if (MOVE_IS_EP(m)) {
        int cap_sq = (us == WHITE) ? to - 8 : to + 8;
        pos->pieces[them][PAWN] |= bb_sq(cap_sq);
    } else if (undo->captured_type >= 0) {
        pos->pieces[them][undo->captured_type] |= bb_sq(to);
    }

    /* Restore rook if castling */
    if (MOVE_IS_CASTLE(m)) {
        if (to == G1) {
            pos->pieces[WHITE][ROOK] |=  bb_sq(H1);
            pos->pieces[WHITE][ROOK] &= ~bb_sq(F1);
        } else if (to == C1) {
            pos->pieces[WHITE][ROOK] |=  bb_sq(A1);
            pos->pieces[WHITE][ROOK] &= ~bb_sq(D1);
        } else if (to == G8) {
            pos->pieces[BLACK][ROOK] |=  bb_sq(H8);
            pos->pieces[BLACK][ROOK] &= ~bb_sq(F8);
        } else if (to == C8) {
            pos->pieces[BLACK][ROOK] |=  bb_sq(A8);
            pos->pieces[BLACK][ROOK] &= ~bb_sq(D8);
        }
    }

    refresh_occupancy(pos);
}

/* ── Debug board printer ─────────────────────────────────────────────────── */

void print_board(const Position *pos) {
    printf("\n  +---+---+---+---+---+---+---+---+\n");
    for (int rank = 7; rank >= 0; rank--) {
        printf("%d |", rank + 1);
        for (int file = 0; file < 8; file++) {
            int sq   = SQ(rank, file);
            char sym = '.';
            for (int c = 0; c < 2; c++)
                for (int pt = 0; pt < 6; pt++)
                    if (pos->pieces[c][pt] & bb_sq(sq))
                        sym = PIECE_CHARS[c][pt];
            printf(" %c |", sym);
        }
        printf("\n  +---+---+---+---+---+---+---+---+\n");
    }
    printf("    a   b   c   d   e   f   g   h\n");
    printf("Side: %s  EP: %s  Castle: %c%c%c%c  HM: %d  FM: %d\n\n",
           pos->side == WHITE ? "white" : "black",
           pos->ep_sq == NO_SQ ? "-" :
               (char[3]){ 'a' + SQ_FILE(pos->ep_sq), '1' + SQ_RANK(pos->ep_sq), 0 },
           (pos->castling & CASTLE_WK) ? 'K' : '-',
           (pos->castling & CASTLE_WQ) ? 'Q' : '-',
           (pos->castling & CASTLE_BK) ? 'k' : '-',
           (pos->castling & CASTLE_BQ) ? 'q' : '-',
           pos->halfmove, pos->fullmove);
}

/* ── UCI move printer ────────────────────────────────────────────────────── */

void print_move_uci(Move m) {
    int from = MOVE_FROM(m);
    int to   = MOVE_TO(m);
    putchar('a' + SQ_FILE(from));
    putchar('1' + SQ_RANK(from));
    putchar('a' + SQ_FILE(to));
    putchar('1' + SQ_RANK(to));
    if (MOVE_IS_PROMO(m))
        putchar(PROMO_CHARS[MOVE_PROMO(m)]);
}
