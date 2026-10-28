#include <string.h>
#include "engine.h"
#include "movegen.h"
#include "position.h"

void generate_legal(Position *pos, MoveList *out) {
    MoveList pl;
    generate_moves(pos, &pl);
    out->count = 0;
    UndoInfo u;
    Color us = pos->side;
    for (int i = 0; i < pl.count; i++) {
        do_move(pos, pl.moves[i], &u);
        if (!is_in_check(pos, us)) out->moves[out->count++] = pl.moves[i];
        undo_move(pos, &u);
    }
}

uint64_t perft_legal(Position *pos, int depth) {
    MoveList l;
    generate_legal(pos, &l);
    if (depth <= 1) return depth == 0 ? 1 : (uint64_t)l.count;
    uint64_t n = 0;
    UndoInfo u;
    for (int i = 0; i < l.count; i++) {
        do_move(pos, l.moves[i], &u);
        n += perft_legal(pos, depth - 1);
        undo_move(pos, &u);
    }
    return n;
}

void move_to_str(Move m, char *buf) {
    int f = MOVE_FROM(m), t = MOVE_TO(m), n = 0;
    buf[n++] = 'a' + SQ_FILE(f); buf[n++] = '1' + SQ_RANK(f);
    buf[n++] = 'a' + SQ_FILE(t); buf[n++] = '1' + SQ_RANK(t);
    if (MOVE_IS_PROMO(m)) buf[n++] = PROMO_CHARS[MOVE_PROMO(m)];
    buf[n] = 0;
}

Move parse_uci_move(Position *pos, const char *str) {
    MoveList l;
    generate_legal(pos, &l);
    char buf[8];
    for (int i = 0; i < l.count; i++) {
        move_to_str(l.moves[i], buf);
        if (strcmp(buf, str) == 0) return l.moves[i];
    }
    return 0;
}
