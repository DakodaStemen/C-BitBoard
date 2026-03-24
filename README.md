# C-BitBoard

A pseudo-legal chess move generator written in C, using 64-bit bitboards and the Hyperbola Quintessence algorithm for sliding piece attacks. Includes perft validation to depth 6 (119,060,324 nodes from the starting position), FEN parsing, full make/unmake with undo, and a perft-divide command for debugging move generators.

[![C](https://img.shields.io/badge/C-C11-555555.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

---

## Table of Contents

- [What Are Bitboards](#what-are-bitboards)
- [Hyperbola Quintessence](#hyperbola-quintessence)
- [Rank Attack Lookup Table](#rank-attack-lookup-table)
- [Move Encoding](#move-encoding)
- [Piece Attack Tables](#piece-attack-tables)
- [Pawn Move Generation](#pawn-move-generation)
- [Pseudo-Legal vs Legal Move Generation](#pseudo-legal-vs-legal-move-generation)
- [Perft Testing](#perft-testing)
- [FEN Parsing](#fen-parsing)
- [Make/Unmake and Undo](#makeunmake-and-undo)
- [Project Layout](#project-layout)
- [Building](#building)
- [Usage](#usage)
- [Known Perft Results](#known-perft-results)
- [License](#license)

---

## What Are Bitboards

A bitboard is a 64-bit integer (`uint64_t`) where each bit represents one square on the chess board. Bit 0 = a1, bit 1 = b1, ..., bit 63 = h8 (little-endian rank-file mapping).

```
bit index layout:
56 57 58 59 60 61 62 63   ← rank 8 (a8 ... h8)
48 49 50 51 52 53 54 55
40 41 42 43 44 45 46 47
32 33 34 35 36 37 38 39
24 25 26 27 28 29 30 31
16 17 18 19 20 21 22 23
 8  9 10 11 12 13 14 15
 0  1  2  3  4  5  6  7   ← rank 1 (a1 ... h1)
```

This representation lets the CPU perform set operations on up to 64 squares simultaneously using native 64-bit instructions:

| Chess operation | Bitboard operation |
|---|---|
| Union of two square sets | `a \| b` |
| Intersection | `a & b` |
| Complement (invert) | `~a` |
| Shift all squares north one rank | `bb << 8` |
| Count pieces | `__builtin_popcountll(bb)` |
| Extract lowest set square | `__builtin_ctzll(bb)` |

The board state stores one bitboard per piece type per color: `pieces[2][6]`, where color is 0 (white) or 1 (black) and piece type is pawn/knight/bishop/rook/queen/king. Two aggregate bitboards, `by_color[2]` and `occupied`, are derived from these and kept in sync.

---

## Hyperbola Quintessence

Hyperbola Quintessence (HQ) is a branchless method for computing sliding piece attacks (bishops, rooks, queens) along a ray given the current occupancy.

### The Formula

For a slider on square `sq`, an occupancy bitboard `occ`, and a ray mask `mask`:

```
o  = occ & mask          (occupied squares along this ray)
r  = byteswap(o)         (reverse the bit order)
rs = sq ^ 56             (reversed square index)

attacks = mask & ( (o - 2*r(sq)) ^ byteswap(r - 2*r(rs)) )
```

where `r(sq)` means "the single-bit mask for square sq".

**Why it works:** The expression `o - 2*r(sq)` propagates a borrow through the occupancy bits, producing a mask of all squares the slider can reach in one direction along the ray before hitting the first blocker (inclusive). The byteswap trick applies the same calculation in the reverse direction by literally reversing the bit order of the board. XOR-ing the two halves and masking to the ray gives the complete attack set in both directions simultaneously.

**Implementation (from `bitboard.h`):**

```c
static inline Bitboard hq_attacks(Bitboard occ, int sq, Bitboard mask) {
    Bitboard o  = occ & mask;
    Bitboard r  = __builtin_bswap64(o);
    int      rs = sq ^ 56;
    return mask & (
        (o  - (bb_sq(sq) << 1)) ^
        __builtin_bswap64(r - (bb_sq(rs) << 1))
    );
}
```

This works correctly for diagonal and file rays. Rank rays require a different approach (see below).

### Applying HQ to Piece Types

```c
// Bishop: diagonal + anti-diagonal rays
Bitboard bishop_attacks(Bitboard occ, int sq) {
    return hq_attacks(occ, sq, DIAG_MASK[sq])
         | hq_attacks(occ, sq, ANTI_DIAG_MASK[sq]);
}

// Rook: file ray (HQ) + rank ray (lookup table)
Bitboard rook_attacks(Bitboard occ, int sq) {
    return hq_attacks(occ, sq, FILE_MASK_SQ[sq])
         | rank_attacks(occ, sq);
}

// Queen: combination
Bitboard queen_attacks(Bitboard occ, int sq) {
    return bishop_attacks(occ, sq) | rook_attacks(occ, sq);
}
```

---

## Rank Attack Lookup Table

HQ uses `__builtin_bswap64` (byte swap) to reverse bits. Byte swap reverses the order of bytes, not individual bits within bytes. For diagonal and file rays, where squares within a ray span multiple bytes, this works correctly. For rank rays, where all 8 squares lie within a single byte of the board, byte-swapping does not reverse bit order within that byte — so HQ gives wrong results for horizontal rays.

The solution is a precomputed 8×256 lookup table: `rank_attacks_lut[file][occ8]`.

- **`file`** (0–7): the file of the attacking piece.
- **`occ8`** (0–255): the 8-bit occupancy of the rank, one bit per file.

The lookup returns an 8-bit mask of squares attacked along that rank, which is then shifted to the correct rank of the board:

```c
static inline Bitboard rank_attacks(Bitboard occ, int sq) {
    int rank  = SQ_RANK(sq);
    int file  = SQ_FILE(sq);
    uint8_t occ8 = (uint8_t)((occ >> (rank * 8)) & 0xFF);
    return (Bitboard)rank_attacks_lut[file][occ8] << (rank * 8);
}
```

The table is populated once in `init_tables()` by iterating over all 8 files × 256 occupancy patterns.

---

## Move Encoding

Each move is a 32-bit integer packed as follows:

```
bits  5– 0:  from square (0–63)
bits 11– 6:  to square   (0–63)
bits 15–12:  promotion piece type (0 = none, 1–4 = N/B/R/Q)
bits 31–16:  flags (capture, en passant, castling, double push, promotion)
```

The `make_move(from, to, promo, flags)` macro packs these fields. `MOVE_FROM(m)` and `MOVE_TO(m)` extract them. Flags are bitfield constants defined in `types.h`.

---

## Piece Attack Tables

Knight and king attacks are fully precomputed into 64-element arrays at startup by `init_tables()` in `bitboard.c`. For each square, the set of reachable squares is computed geometrically and stored:

```c
// Knight attacks from square sq
for each of the 8 L-shaped offsets:
    target = sq + offset
    if target is on the board and not on the wrong side of the wrap:
        KNIGHT_ATTACKS[sq] |= bb_sq(target)
```

Pawn attacks are stored separately by color: `PAWN_ATTACKS[WHITE][sq]` and `PAWN_ATTACKS[BLACK][sq]`, because pawns attack differently depending on which direction they move.

---

## Pawn Move Generation

Pawn moves are generated using bitboard shift arithmetic, not loops:

```
Single push (white):  (pawns << 8) & ~occupied
Double push (white):  (single_push & RANK_3) << 8 & ~occupied
Capture left:         (pawns << 7) & ~FILE_H & enemy
Capture right:        (pawns << 9) & ~FILE_A & enemy
En passant:           same shifts applied to ep_square mask
```

Promotions are detected by checking if the destination rank is rank 8 (white) or rank 1 (black). Promoting pawns emit four moves (one per promotion piece). `FILE_A` and `FILE_H` masks prevent captures from wrapping off the edge of the board.

---

## Pseudo-Legal vs Legal Move Generation

cbitboard generates **pseudo-legal** moves: all moves that are geometrically valid but without checking whether the moving side's king is left in check. Pseudo-legal generation is faster because it avoids the check-detection step on every candidate move.

Legality is enforced in the perft loop: after `do_move`, the generator checks whether the side that just moved left its king in check (using `is_in_check`). If so, the move is discarded and `undo_move` is called. This is the standard approach for perft drivers; it does not reflect what a full engine would do in a search.

---

## Perft Testing

Perft (performance test) counts the number of leaf nodes at a given depth from a position. It is the standard correctness test for move generators: if perft(pos, depth) matches the known result for that position, the move generator and make/unmake implementation are correct for all positions reachable at that depth.

cbitboard validates to perft depth 6 from the starting position:

| Depth | Nodes |
|-------|-------|
| 1 | 20 |
| 2 | 400 |
| 3 | 8,902 |
| 4 | 197,281 |
| 5 | 4,865,609 |
| 6 | 119,060,324 |

These are the universally agreed-upon node counts from the chess programming community (chessprogramming.org). Any deviation indicates a bug.

**Perft divide** breaks the depth-1 result down by root move, showing how many nodes each first move contributes. This is the primary tool for isolating discrepancies when perft results don't match.

---

## FEN Parsing

The Forsyth–Edwards Notation (FEN) is the standard format for describing a chess position. A FEN string has six fields separated by spaces:

```
rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
│                          │         │     │ │ │  │
│                          │         │     │ │ │  └ fullmove number
│                          │         │     │ │ └ halfmove clock
│                          │         │     │ └ en passant target square (or -)
│                          │         └─────┘ castling availability
│                          └ active color (w/b)
└ piece placement (rank 8 to rank 1, / between ranks)
```

`parse_fen()` in `position.c` reads all six fields, populates the `Position` struct, and calls `refresh_occupancy()` to derive the aggregate bitboards.

---

## Make/Unmake and Undo

`do_move(pos, m, &undo)` applies a move to the position in place. Before modifying anything, it saves the current state into a `UndoInfo` struct: captured piece type, en passant square, castling rights, and halfmove clock. This makes `undo_move(pos, &undo)` an exact reversal — it restores all saved fields.

The make/unmake cycle handles:
- Quiet moves and captures
- En passant captures (remove the captured pawn from a different square than the destination)
- Castling (move both king and rook atomically)
- Double pawn pushes (set the en passant target square)
- Promotions (replace pawn with the promoted piece)

---

## Project Layout

```
C-BitBoard/
├── Makefile
├── src/
│   ├── types.h        Fundamental types: Bitboard, Move, Color, Piece, Position, UndoInfo
│   ├── bitboard.h     Rank/file masks, precomputed tables, HQ inline functions
│   ├── bitboard.c     Table initialization (knight/king/pawn/diagonal/rank lookup)
│   ├── position.h     FEN parser, do_move, undo_move, print_board, print_move_uci
│   ├── position.c     Position implementation
│   ├── movegen.h      generate_moves, is_in_check declarations
│   ├── movegen.c      Full pseudo-legal move generation for all piece types
│   └── main.c         CLI: perft, perft divide, board print, timing
└── README.md
```

---

## Building

**Release build (optimized, `-O3 -march=native`):**

```bash
make
```

**Debug build (no optimization, AddressSanitizer + UBSanitizer):**

```bash
make debug
```

The debug build links `-fsanitize=address,undefined`. Run perft at shallow depths (1–4) under the debug build to catch memory errors and undefined behavior.

**Clean:**

```bash
make clean
```

**Compiler requirements:** GCC or Clang with C11 support. The code uses `__builtin_ctzll`, `__builtin_popcountll`, and `__builtin_bswap64` — all available on any x86_64 GCC/Clang.

---

## Usage

```bash
# Run perft from startpos to depth 5
./cbitboard 5

# Run perft from a custom FEN to depth 4
./cbitboard "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -" 4

# Perft divide (root move breakdown) from startpos at depth 3
./cbitboard divide 3

# Perft divide from a custom FEN
./cbitboard "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -" divide 4

# Print the board for a FEN (for visual inspection)
./cbitboard board "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3"

# Print startpos board
./cbitboard board
```

### Sample perft output

```
FEN:   rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
Depth: 6

Nodes: 119060324
Time:  841.3 ms  (141.5 Mnodes/s)
```

### Sample perft divide output

```
Perft divide depth 3

a2a3: 380
b2b3: 420
c2c3: 420
d2d3: 539
e2e3: 599
f2f3: 380
g2g3: 420
h2h3: 380
a2a4: 420
...
Total: 8902
```

---

## Known Perft Results

The following positions are standard move generator test vectors from the [Chess Programming Wiki](https://www.chessprogramming.org/Perft_Results):

**Position 1 — Starting position**

| Depth | Nodes |
|-------|-------|
| 1 | 20 |
| 2 | 400 |
| 3 | 8,902 |
| 4 | 197,281 |
| 5 | 4,865,609 |
| 6 | 119,060,324 |

**Position 2 — Kiwipete (exercises all special moves)**
`r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -`

| Depth | Nodes |
|-------|-------|
| 1 | 48 |
| 2 | 2,039 |
| 3 | 97,862 |
| 4 | 4,085,603 |
| 5 | 193,690,690 |

---

## License

MIT License — see [LICENSE](LICENSE).

Copyright (c) 2026 Dakoda Stemen
