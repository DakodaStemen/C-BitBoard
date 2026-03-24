# C-BitBoard

A chess move generator and perft validator written in C11. The idea is simple:
represent the entire chessboard as a 64-bit integer and use bitwise operations
to generate moves for every piece type at once, instead of looping square by
square. The result is a generator that can enumerate all legal moves from any
position in microseconds, validated against known node counts up to depth 6
(119 million nodes).

---

## Table of Contents

- [What is a Bitboard](#what-is-a-bitboard)
- [Board Representation](#board-representation)
- [How Move Generation Works](#how-move-generation-works)
  - [Pawn Moves](#pawn-moves)
  - [Knight Moves](#knight-moves)
  - [Sliding Pieces](#sliding-pieces)
  - [Castling](#castling)
- [Move Encoding](#move-encoding)
- [Perft Validation](#perft-validation)
- [Build and Usage](#build-and-usage)
- [Project Structure](#project-structure)

---

## What is a Bitboard

A bitboard is a 64-bit integer where each bit corresponds to one square on the
board. Bit 0 is a1, bit 63 is h8. If the bit is 1, something is on that square.
If it is 0, the square is empty.

```
Square indices:

  a    b    c    d    e    f    g    h
+----+----+----+----+----+----+----+----+
| 56 | 57 | 58 | 59 | 60 | 61 | 62 | 63 |  rank 8
+----+----+----+----+----+----+----+----+
| 48 | 49 | 50 | 51 | 52 | 53 | 54 | 55 |  rank 7
+----+----+----+----+----+----+----+----+
| 40 | 41 | 42 | 43 | 44 | 45 | 46 | 47 |  rank 6
+----+----+----+----+----+----+----+----+
| 32 | 33 | 34 | 35 | 36 | 37 | 38 | 39 |  rank 5
+----+----+----+----+----+----+----+----+
| 24 | 25 | 26 | 27 | 28 | 29 | 30 | 31 |  rank 4
+----+----+----+----+----+----+----+----+
| 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 |  rank 3
+----+----+----+----+----+----+----+----+
|  8 |  9 | 10 | 11 | 12 | 13 | 14 | 15 |  rank 2
+----+----+----+----+----+----+----+----+
|  0 |  1 |  2 |  3 |  4 |  5 |  6 |  7 |  rank 1
+----+----+----+----+----+----+----+----+

sq = rank * 8 + file    (rank 0 = rank 1, file 0 = file a)
```

So all eight white pawns on rank 2 are just one integer: `0x000000000000FF00`.

```
White pawns at startup:

  a  b  c  d  e  f  g  h
+--+--+--+--+--+--+--+--+
|  |  |  |  |  |  |  |  |  rank 8
|  |  |  |  |  |  |  |  |  rank 7
|  |  |  |  |  |  |  |  |  rank 6
|  |  |  |  |  |  |  |  |  rank 5
|  |  |  |  |  |  |  |  |  rank 4
|  |  |  |  |  |  |  |  |  rank 3
| P| P| P| P| P| P| P| P|  rank 2   bits 8-15 all set
|  |  |  |  |  |  |  |  |  rank 1

0x000000000000FF00
= 00000000 00000000 00000000 00000000
  00000000 00000000 11111111 00000000
```

The engine stores 12 of these — one per piece type per color:

```
pieces[WHITE][PAWN]    pieces[BLACK][PAWN]
pieces[WHITE][KNIGHT]  pieces[BLACK][KNIGHT]
pieces[WHITE][BISHOP]  pieces[BLACK][BISHOP]
pieces[WHITE][ROOK]    pieces[BLACK][ROOK]
pieces[WHITE][QUEEN]   pieces[BLACK][QUEEN]
pieces[WHITE][KING]    pieces[BLACK][KING]
```

Two derived boards are kept in sync after every move:

```c
by_color[WHITE] = union of all white piece boards
by_color[BLACK] = union of all black piece boards
occupied        = by_color[WHITE] | by_color[BLACK]
```

---

## Board Representation

```c
typedef struct {
    Bitboard pieces[2][6];
    Bitboard occupied;
    Bitboard by_color[2];
    Color    side;
    int      ep_sq;       // en passant target square, or NO_SQ
    uint8_t  castling;    // bit 0=WK  1=WQ  2=BK  3=BQ
    int      halfmove;
    int      fullmove;
} Position;
```

Parsing `rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1` fills
the struct directly:

```
  +---+---+---+---+---+---+---+---+
8 | r | n | b | q | k | b | n | r |
  +---+---+---+---+---+---+---+---+
7 | p | p | p | p | p | p | p | p |
  +---+---+---+---+---+---+---+---+
6 | . | . | . | . | . | . | . | . |
  +---+---+---+---+---+---+---+---+
5 | . | . | . | . | . | . | . | . |
  +---+---+---+---+---+---+---+---+
4 | . | . | . | . | . | . | . | . |
  +---+---+---+---+---+---+---+---+
3 | . | . | . | . | . | . | . | . |
  +---+---+---+---+---+---+---+---+
2 | P | P | P | P | P | P | P | P |
  +---+---+---+---+---+---+---+---+
1 | R | N | B | Q | K | B | N | R |
  +---+---+---+---+---+---+---+---+
    a   b   c   d   e   f   g   h
Side: white  Castle: KQkq  EP: -
```

---

## How Move Generation Works

### Pawn Moves

Rather than iterating over each pawn, all pawns of the same color are shifted
together in a single operation. One shift moves every pawn on the board forward
at once.

```
Single push: shift the whole pawn board left by 8, mask out occupied squares.

  a  b  c  d  e  f  g  h        a  b  c  d  e  f  g  h
+--+--+--+--+--+--+--+--+      +--+--+--+--+--+--+--+--+
|  |  |  |  |  |  |  |  | 8   |  |  |  |  |  |  |  |  | 8
|  |  |  |  |  |  |  |  | 7   |  |  |  |  |  |  |  |  | 7
|  |  |  |  |  |  |  |  | 6   |  |  |  |  |  |  |  |  | 6
|  |  |  |  |  |  |  |  | 5   |  |  |  |  |  |  |  |  | 5
|  |  |  |  |  |  |  |  | 4   |  |  |  |  |  |  |  |  | 4
|  |  |  |  |  |  |  |  | 3   | *| *| *| *| *| *| *| *| 3  <-- destinations
| P| P| P| P| P| P| P| P| 2   |  |  |  |  |  |  |  |  | 2
|  |  |  |  |  |  |  |  | 1   |  |  |  |  |  |  |  |  | 1

                   pawns << 8  &  ~occupied
```

Double push sends the single-push result one more rank, but only keeps squares
on rank 4 — guaranteeing the pawn started on rank 2 and both intermediate
squares were clear:

```c
single_push = (pawns << 8) & ~occupied;
double_push = (single_push << 8) & RANK_4 & ~occupied;
```

Diagonal shifts handle captures, with file masks to stop wrapping at the edges:

```
Pawn on e4.  Left capture = << 7, right capture = << 9.

    d  e  f
  +--+--+--+
5 | *|  | *|   capture targets (only if an enemy piece is there)
  +--+--+--+
4 |  | P|  |
  +--+--+--+

pawns << 7  &  ~FILE_H  &  enemy   =  left  capture squares
pawns << 9  &  ~FILE_A  &  enemy   =  right capture squares
```

Promotions are detected when any destination lands on rank 8. Four moves are
generated per promoting pawn (N, B, R, Q).

---

### Knight Moves

The eight possible knight jumps are precomputed for all 64 squares at startup.
Each entry is the set of squares a knight can reach from that square.

```
Knight on d4 (sq 27):

  a  b  c  d  e  f  g  h
+--+--+--+--+--+--+--+--+
|  |  |  |  |  |  |  |  | 8
|  |  |  |  |  |  |  |  | 7
|  |  | *|  | *|  |  |  | 6   +15, +17
|  | *|  |  |  | *|  |  | 5    +6, +10
|  |  |  | N|  |  |  |  | 4
|  | *|  |  |  | *|  |  | 3    -6, -10
|  |  | *|  | *|  |  |  | 2   -15, -17
|  |  |  |  |  |  |  |  | 1
```

Each of the eight shifts needs a file-edge mask to prevent wrap-around. For
example, a knight on h4 shifted by +17 would land on bit 48 which is a1 on
rank 7 — impossible geometrically. Masking against FILE_A zeroes out any
result that illegally crosses the left edge:

```c
(b << 17) & ~FILE_A   // +2 ranks, +1 file: mask prevents a-file wrapping
(b << 15) & ~FILE_H   // +2 ranks, -1 file: mask prevents h-file wrapping
```

At runtime, looking up a knight's attacks is just one table access:

```c
Bitboard attacks = KNIGHT_ATTACKS[sq] & ~own_pieces;
```

---

### Sliding Pieces

Bishops, rooks, and queens cast rays out from their square until hitting a
blocker. The tricky part is that the blocking square depends on the current
board occupancy, which changes every move.

This engine uses **Hyperbola Quintessence** — a five-operation formula that
computes attacks in both directions along a ray simultaneously:

```
o  = occupied squares on this ray
r  = the slider's own bit
rs = the slider's bit in the vertically-mirrored board

attacks = ((o - 2r)  XOR  reverse(reverse(o) - 2*reverse(r)))  AND  ray_mask
```

Worked example — rook on e4, looking up and down the e-file:

```
e-file, occupied squares:

  e
+--+
| p|  sq 52  (enemy pawn)
|  |  sq 44
|  |  sq 36
| R|  sq 28  (the rook)
|  |  sq 20
| P|  sq 12  (own pawn)
| K|  sq 4   (own king)
+--+

o = bits {4, 12, 28, 52}

Forward (toward rank 8): subtract 2*bit(28) from o.
The borrow cascades through empty squares and stops at the
first occupied square above the rook.

Backward (toward rank 1): mirror the board vertically via bswap64,
apply the same subtraction, mirror back.

XOR combines both directions:

  e
+--+
| *|  sq 52  attacks enemy pawn (capture square)
| *|  sq 44
| *|  sq 36
|   |  sq 28  the rook itself (excluded later via & ~own_pieces)
| *|  sq 20
| *|  sq 12  attacks own pawn (excluded later via & ~own_pieces)
|   |  sq 4   blocked by own pawn above -- cannot reach king
+--+
```

The bswap64 trick works for files, diagonals, and anti-diagonals because
mirroring byte order correctly maps each square to its rank-mirrored
counterpart (`sq XOR 56`). Rank (horizontal) rays are different: all
squares of a rank share one byte, so bswap moves the whole byte to a
different position without reversing bit order within it. Rank attacks
use a 2 KB lookup table instead:

```
rank_attacks_lut[file][8-bit occupancy of the rank]

Example: slider on file e (index 4), rank occupancy = 10110101

  a  b  c  d  e  f  g  h
  1  0  1  1  .  1  0  1   (dot = slider's own square, not counted)

Forward scan from e toward h: f is occupied -> stop at f, attack f
Backward scan from e toward a: d is occupied -> stop at d, attack d

Result: attacks d and f only

  a  b  c  d  e  f  g  h
  0  0  0  1  0  1  0  0
```

---

### Castling

Three conditions must hold before castling is legal:

```
White kingside (e1 -> g1):

  a  b  c  d  e  f  g  h
+--+--+--+--+--+--+--+--+
|  |  |  |  | K|  |  | R|   before
+--+--+--+--+--+--+--+--+

1. Castling right flag set (neither king nor rook has moved)
2. f1 and g1 are empty
3. e1, f1, g1 are not attacked by black (king cannot pass through check)

+--+--+--+--+--+--+--+--+
|  |  |  |  |  | R| K|  |   after
+--+--+--+--+--+--+--+--+


White queenside (e1 -> c1):

+--+--+--+--+--+--+--+--+
| R|  |  |  | K|  |  |  |   before
+--+--+--+--+--+--+--+--+

1. Castling right flag set
2. b1, c1, d1 are empty
3. c1, d1, e1 are not attacked by black
   (b1 only needs to be empty, not unattacked)

+--+--+--+--+--+--+--+--+
|  |  | K| R|  |  |  |  |   after
+--+--+--+--+--+--+--+--+
```

When checking whether the king's path is attacked, the king is temporarily
removed from the occupancy so sliding pieces are not incorrectly blocked by
the king itself when evaluating f1 and g1.

---

## Move Encoding

Each move is packed into a 32-bit integer:

```
Bit layout:

  [18][17][16][15][14][13:12][11:6][5:0]
   |   |   |   |   |    |      |     |
   |   |   |   |   |    |      |     from square (0-63)
   |   |   |   |   |    |      to square   (0-63)
   |   |   |   |   |    promotion piece (0=N 1=B 2=R 3=Q)
   |   |   |   |   capture flag
   |   |   |   promotion flag
   |   |   en passant flag
   |   castling flag
   double pawn push flag
```

A few examples:

```
e2e4  (double pawn push):
  from=12  to=28  flag=double_push

e7e8q  (promotion to queen):
  from=52  to=60  promo=3  flag=promo

e1g1  (kingside castling):
  from=4   to=6   flag=castle
```

---

## Perft Validation

Perft counts every legal position reachable at a given search depth. It is the
standard correctness test for a move generator — the counts have been established
independently by hundreds of engines and any deviation points directly to a bug.

```
Starting position, first few depths:

Depth 0:     1 node   (just the root)
Depth 1:    20 nodes  (16 pawn moves + 4 knight moves)
Depth 2:   400 nodes  (20 * 20 -- each side has 20 choices)
Depth 3: 8,902 nodes  (first captures and en passant appear here)
```

The implementation is pseudo-legal: generate all candidate moves first, then
filter out any that leave the moving side's king in check.

```c
uint64_t perft(Position *pos, int depth) {
    if (depth == 0) return 1;

    MoveList list;
    generate_moves(pos, &list);

    uint64_t nodes = 0;
    UndoInfo undo;

    for (int i = 0; i < list.count; i++) {
        do_move(pos, list.moves[i], &undo);
        if (!is_in_check(pos, pos->side ^ 1))
            nodes += perft(pos, depth - 1);
        undo_move(pos, &undo);
    }
    return nodes;
}
```

### Starting position

```
  Depth    Nodes          Time
  -----    -----------    ----
  1        20             <1 ms
  2        400            <1 ms
  3        8,902          <1 ms
  4        197,281        ~4 ms
  5        4,865,609      ~96 ms
  6        119,060,324    ~2663 ms   (~45 Mnodes/s)
```

### Kiwipete

A well-known stress-test position that exercises castling, en passant captures,
and promotions all at once.

```
r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -

  a  b  c  d  e  f  g  h
+--+--+--+--+--+--+--+--+
| r|  |  |  | k|  |  | r|  8
| p|  | p| p| q| p| b|  |  7
| b| n|  |  | p| n| p|  |  6
|  |  |  | P| N|  |  |  |  5
|  | p|  |  | P|  |  |  |  4
|  |  | N|  |  | Q|  | p|  3
| P| P| P| B| B| P| P| P|  2
| R|  |  |  | K|  |  | R|  1
+--+--+--+--+--+--+--+--+

  Depth    Nodes
  -----    ---------
  1        48
  2        2,039
  3        97,862
  4        4,085,603
```

The `divide` command breaks down results by root move, which is how bugs get
isolated -- compare each line against a trusted engine until you find the
first number that disagrees:

```
$ ./cbitboard "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -" divide 1

a2a3: 1     d5d6: 1     f3f4: 1
a2a4: 1     d5e6: 1     f3g3: 1
b2b3: 1     e4e5: 1     f3h3: 1
c2c3: 1     c3b1: 1     f3g4: 1
g2g3: 1     c3a4: 1     f3h5: 1
g2g4: 1     c3b5: 1     f3f6: 1
g2h3: 1     c3d1: 1     e1d1: 1
d2c1: 1     c3e2: 1     e1f1: 1
d2e3: 1     e5d3: 1     e1g1: 1   <- kingside castle
d2f4: 1     e5g6: 1     e1c1: 1   <- queenside castle
d2g5: 1     e5c4: 1
d2h6: 1     e5g4: 1

Total: 48
```

---

## Build and Usage

```sh
# Release build
make

# Debug build (AddressSanitizer + UndefinedBehaviorSanitizer)
make debug

# Perft from starting position
./cbitboard <depth>

# Perft from a FEN string
./cbitboard "<fen>" <depth>

# Perft divide
./cbitboard "<fen>" divide <depth>

# Print the board
./cbitboard board ["<fen>"]
```

```sh
# Some examples
./cbitboard 5
./cbitboard "" 6
./cbitboard "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -" 4
./cbitboard divide 3
./cbitboard board
```

---

## Project Structure

```
cbitboard/
├── Makefile
├── README.md
└── src/
    ├── types.h       Position, Move encoding, MoveList, UndoInfo
    ├── bitboard.h    Inline bit ops, Hyperbola Quintessence, table declarations
    ├── bitboard.c    Precomputed tables: knight, king, pawn attacks, rank LUT
    ├── movegen.c     Pawn, knight, slider, and castling move generation
    ├── position.c    FEN parser, make/unmake, board printer, UCI output
    └── main.c        Perft driver with timing, perft divide
```
