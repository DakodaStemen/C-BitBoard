#ifndef ENGINE_H
#define ENGINE_H

#include <stddef.h>
#include "types.h"

/* ── Legal move generation (legal.c) ─────────────────────────────────────── */
void     generate_legal(Position *pos, MoveList *list);
uint64_t perft_legal(Position *pos, int depth);
Move     parse_uci_move(Position *pos, const char *str);   /* 0 if illegal */
void     move_to_str(Move m, char *buf);                   /* buf >= 6 bytes */

/* ── Zobrist hashing (zobrist.c) ─────────────────────────────────────────── */
void     init_zobrist(void);                 /* idempotent: same keys every call */
uint64_t pos_hash(const Position *pos);      /* full recompute (reference / FEN setup) */
extern uint64_t ZOB_PIECE[2][6][64], ZOB_SIDE, ZOB_CASTLE[16], ZOB_EP[8];

/* ── Evaluation (eval.c): centipawns from side-to-move's point of view ───── */
#define PIECE_VALUE_MG(pt) (PIECE_VALUES[pt])
extern const int PIECE_VALUES[6];
int evaluate(const Position *pos);

/* ── Transposition table (tt.c) ──────────────────────────────────────────── */
enum { TT_NONE = 0, TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3 };
typedef struct {
    uint64_t key;
    Move     move;
    int16_t  score;
    int8_t   depth;
    uint8_t  flag;
} TTEntry;
void     tt_resize(size_t mb);
void     tt_clear(void);
TTEntry *tt_probe(uint64_t key);
void     tt_store(uint64_t key, Move m, int score, int depth, int flag);

/* ── Search (search.c) ───────────────────────────────────────────────────── */
#define MAX_PLY   96
#define MATE      30000
#define MATE_BOUND (MATE - MAX_PLY)
#define INF_SCORE 32000

typedef struct {
    int      depth;        /* 0 = unlimited */
    long     movetime_ms;  /* 0 = unset */
    long     time_ms;      /* our clock, 0 = unset */
    long     inc_ms;
    int      movestogo;
    uint64_t nodes;        /* 0 = unlimited */
    int      infinite;
    int      quiet;        /* suppress "info" lines */
} Limits;

typedef struct {
    Move     best;
    int      score;
    int      depth;
    uint64_t nodes;
    double   ms;
} SearchResult;

/* poll() is called periodically; returns nonzero if search should stop. */
void search_init(void);
void search_new_game(void);
/* hist[0..hist_len-1] = hashes of earlier game positions (excl. current). */
SearchResult search_go(Position *pos, const Limits *lim,
                       const uint64_t *hist, int hist_len,
                       int (*poll)(void));

/* ── UCI (uci.c) ─────────────────────────────────────────────────────────── */
void uci_loop(void);
void run_bench(int depth);

#endif
