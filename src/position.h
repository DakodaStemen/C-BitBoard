#ifndef POSITION_H
#define POSITION_H

#include "types.h"

#define STARTPOS_FEN \
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

/* Parse a FEN string into pos. Returns 0 on success, -1 on error. */
int  parse_fen(Position *pos, const char *fen);

/* Apply / reverse a move. undo must be provided for unmake. */
void do_move(Position *pos, Move m, UndoInfo *undo);
void undo_move(Position *pos, const UndoInfo *undo);

/* Recompute the derived fields (occupied, by_color) from pieces[][]. */
void refresh_occupancy(Position *pos);

/* Print an ASCII board to stdout (for debugging). */
void print_board(const Position *pos);

/* Print a move in UCI notation (e.g. "e2e4", "e7e8q"). */
void print_move_uci(Move m);

#endif /* POSITION_H */
