#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "types.h"

/* Generate all pseudo-legal moves for pos->side into list.
   Illegal moves (leaving own king in check) must be filtered by the caller. */
void generate_moves(const Position *pos, MoveList *list);

/* Return a bitboard of all squares attacked by `by` in pos.
   Uses the current pos->occupied for slider blocking. */
Bitboard attacks_by(const Position *pos, Color by);

/* Return 1 if `color`'s king is in check. */
int is_in_check(const Position *pos, Color color);

#endif /* MOVEGEN_H */
