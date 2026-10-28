#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "engine.h"
#include "bitboard.h"
#include "movegen.h"
#include "position.h"

/* ── State ───────────────────────────────────────────────────────────────── */

static uint64_t nodes, node_limit;
static double   t_start, t_soft, t_hard;
static int      stopped, max_depth;
static int    (*poll_fn)(void);
static uint64_t hstack[2048 + MAX_PLY + 8];
static int      hlen;
static Move     killers[MAX_PLY][2];
static int      history[2][64][64];
static Move     pv[MAX_PLY + 1][MAX_PLY + 1];
static int      pv_len[MAX_PLY + 1];

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int move_overhead = DEFAULT_MOVE_OVERHEAD;
void search_set_move_overhead(int ms) { move_overhead = ms; }

void search_init(void) { init_zobrist(); tt_resize(64); }
void search_new_game(void) {
    tt_clear();
    memset(killers, 0, sizeof killers);
    memset(history, 0, sizeof history);
}

static void check_stop(void) {
    if (node_limit && nodes >= node_limit) stopped = 1;
    else if (now_ms() >= t_hard) stopped = 1;
    else if (poll_fn && poll_fn()) stopped = 1;
}

/* ── Move ordering ───────────────────────────────────────────────────────── */

static int piece_at(const Position *pos, Color c, int sq) {
    for (int p = 0; p < 6; p++)
        if (pos->pieces[c][p] & bb_sq(sq)) return p;
    return -1;
}

static int victim_of(const Position *pos, Move m) {
    if (MOVE_IS_EP(m)) return PAWN;
    return piece_at(pos, pos->side ^ 1, MOVE_TO(m));
}

static int score_move(const Position *pos, Move m, Move tt_move, int ply) {
    if (m == tt_move) return 10000000;
    int s = 0;
    if (MOVE_IS_CAPTURE(m)) {
        int v = victim_of(pos, m), a = piece_at(pos, pos->side, MOVE_FROM(m));
        s = 1000000 + PIECE_VALUES[v] * 10 - PIECE_VALUES[a] / 10;   /* MVV-LVA */
    } else if (m == killers[ply][0]) s = 900000;
    else if (m == killers[ply][1])   s = 800000;
    else s = history[pos->side][MOVE_FROM(m)][MOVE_TO(m)];
    if (MOVE_IS_PROMO(m)) s += (MOVE_PROMO(m) == PROMO_QUEEN) ? 1500000 : -500000;
    return s;
}

static void order(const Position *pos, MoveList *l, int *sc, Move ttm, int ply) {
    for (int i = 0; i < l->count; i++) sc[i] = score_move(pos, l->moves[i], ttm, ply);
}
static inline void pick(MoveList *l, int *sc, int i) {
    int b = i;
    for (int j = i + 1; j < l->count; j++) if (sc[j] > sc[b]) b = j;
    if (b != i) {
        Move tm = l->moves[i]; l->moves[i] = l->moves[b]; l->moves[b] = tm;
        int ts = sc[i]; sc[i] = sc[b]; sc[b] = ts;
    }
}

static int is_repetition(const Position *pos) {
    int key_i = hlen - 1;
    int lim = key_i - pos->halfmove; if (lim < 0) lim = 0;
    for (int i = key_i - 2; i >= lim; i -= 2)
        if (hstack[i] == hstack[key_i]) return 1;
    return 0;
}

static int has_non_pawn(const Position *pos, Color c) {
    return (pos->pieces[c][KNIGHT] | pos->pieces[c][BISHOP] |
            pos->pieces[c][ROOK]   | pos->pieces[c][QUEEN]) != 0;
}

/* ── Quiescence ──────────────────────────────────────────────────────────── */

static int qsearch(Position *pos, int alpha, int beta, int ply) {
    nodes++;
    if ((nodes & 2047) == 0) check_stop();
    if (stopped) return 0;
    int stand = evaluate(pos);
    if (ply >= MAX_PLY - 1) return stand;
    if (stand >= beta) return stand;
    if (stand > alpha) alpha = stand;

    MoveList l; int sc[MAX_MOVES];
    generate_moves(pos, &l);
    int n = 0;
    for (int i = 0; i < l.count; i++)
        if (MOVE_IS_CAPTURE(l.moves[i]) || MOVE_IS_PROMO(l.moves[i])) l.moves[n++] = l.moves[i];
    l.count = n;
    order(pos, &l, sc, 0, ply);

    Color us = pos->side;
    UndoInfo u;
    for (int i = 0; i < l.count; i++) {
        pick(&l, sc, i);
        Move m = l.moves[i];
        do_move(pos, m, &u);
        if (is_in_check(pos, us)) { undo_move(pos, &u); continue; }
        hstack[hlen++] = pos->hash;
        int s = -qsearch(pos, -beta, -alpha, ply + 1);
        hlen--;
        undo_move(pos, &u);
        if (stopped) return 0;
        if (s > stand) {
            stand = s;
            if (s > alpha) { alpha = s; if (s >= beta) break; }
        }
    }
    return stand;
}

/* ── Main search (PVS + TT + NMP + LMR + killers/history) ────────────────── */

static int to_tt(int s, int ply)   { return s >= MATE_BOUND ? s + ply : s <= -MATE_BOUND ? s - ply : s; }
static int from_tt(int s, int ply) { return s >= MATE_BOUND ? s - ply : s <= -MATE_BOUND ? s + ply : s; }

#define RFP_MAX_DEPTH 6
#define RFP_MARGIN    90      /* cp per ply */
#define ASP_START     5       /* first iteration that uses an aspiration window */
#define ASP_DELTA     25

static int negamax(Position *pos, int depth, int alpha, int beta, int ply, int allow_null) {
    pv_len[ply] = 0;
    if (ply > 0) {
        if (pos->halfmove >= 100 || is_repetition(pos)) return 0;
    }
    if (ply >= MAX_PLY - 1) return evaluate(pos);

    Color us = pos->side;
    int in_check = is_in_check(pos, us);
    if (in_check) depth++;
    if (depth <= 0) return qsearch(pos, alpha, beta, ply);

    nodes++;
    if ((nodes & 2047) == 0) check_stop();
    if (stopped) return 0;

    /* mate-distance pruning */
    int mate_alpha = -MATE + ply, mate_beta = MATE - ply - 1;
    if (alpha < mate_alpha) alpha = mate_alpha;
    if (beta > mate_beta)   beta = mate_beta;
    if (alpha >= beta) return alpha;

    int is_pv = beta - alpha > 1;
    uint64_t key = hstack[hlen - 1];
    Move tt_move = 0;
    TTEntry *e = tt_probe(key);
    if (e) {
        tt_move = e->move;
        if (!is_pv && e->depth >= depth) {
            int s = from_tt(e->score, ply);
            if (e->flag == TT_EXACT) return s;
            if (e->flag == TT_LOWER && s >= beta) return s;
            if (e->flag == TT_UPPER && s <= alpha) return s;
        }
    }

    int static_eval = in_check ? 0 : evaluate(pos);

    /* reverse futility pruning: static eval so far above beta that a shallow search won't change it */
    if (!is_pv && !in_check && depth <= RFP_MAX_DEPTH && beta < MATE_BOUND && beta > -MATE_BOUND &&
        static_eval - RFP_MARGIN * depth >= beta && has_non_pawn(pos, us))
        return static_eval - RFP_MARGIN * depth;

    /* null-move pruning */
    if (allow_null && !is_pv && !in_check && depth >= 3 && static_eval >= beta &&
        has_non_pawn(pos, us) && beta < MATE_BOUND) {
        int saved_ep = pos->ep_sq;
        uint64_t saved_hash = pos->hash;
        pos->hash ^= ZOB_SIDE;
        if (saved_ep != NO_SQ) pos->hash ^= ZOB_EP[SQ_FILE(saved_ep)];
        pos->side = us ^ 1; pos->ep_sq = NO_SQ;
        int saved_hm = pos->halfmove; pos->halfmove++;
        hstack[hlen++] = pos->hash;
        int s = -negamax(pos, depth - 1 - (2 + depth / 4), -beta, -beta + 1, ply + 1, 0);
        hlen--;
        pos->side = us; pos->ep_sq = saved_ep; pos->halfmove = saved_hm; pos->hash = saved_hash;
        if (stopped) return 0;
        if (s >= beta) return s >= MATE_BOUND ? beta : s;
    }

    MoveList l; int sc[MAX_MOVES];
    generate_moves(pos, &l);
    order(pos, &l, sc, tt_move, ply);

    int best = -INF_SCORE, legal = 0, orig_alpha = alpha;
    Move best_move = 0;
    UndoInfo u;
    Move quiets[MAX_MOVES]; int nq = 0;

    for (int i = 0; i < l.count; i++) {
        pick(&l, sc, i);
        Move m = l.moves[i];
        int quiet = !MOVE_IS_CAPTURE(m) && !MOVE_IS_PROMO(m);
        do_move(pos, m, &u);
        if (is_in_check(pos, us)) { undo_move(pos, &u); continue; }
        legal++;
        hstack[hlen++] = pos->hash;

        int s, r = 0;
        if (legal > 4 && depth >= 3 && quiet && !in_check) r = 1 + (legal > 10) + (depth > 6);
        if (legal == 1) {
            s = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, 1);
        } else {
            s = -negamax(pos, depth - 1 - r, -alpha - 1, -alpha, ply + 1, 1);
            if (s > alpha && r > 0)
                s = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, 1);
            if (s > alpha && s < beta)
                s = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, 1);
        }
        hlen--;
        undo_move(pos, &u);
        if (stopped) return 0;

        if (s > best) {
            best = s; best_move = m;
            if (s > alpha) {
                alpha = s;
                pv[ply][0] = m;
                memcpy(&pv[ply][1], &pv[ply + 1][0], pv_len[ply + 1] * sizeof(Move));
                pv_len[ply] = pv_len[ply + 1] + 1;
                if (s >= beta) {
                    if (quiet) {
                        if (killers[ply][0] != m) { killers[ply][1] = killers[ply][0]; killers[ply][0] = m; }
                        int bonus = depth * depth;
                        history[us][MOVE_FROM(m)][MOVE_TO(m)] += bonus;
                        for (int q = 0; q < nq; q++)
                            history[us][MOVE_FROM(quiets[q])][MOVE_TO(quiets[q])] -= bonus;
                        if (history[us][MOVE_FROM(m)][MOVE_TO(m)] > 500000)
                            memset(history, 0, sizeof history);
                    }
                    break;
                }
            }
        }
        if (quiet) quiets[nq++] = m;
    }

    if (legal == 0) return in_check ? -MATE + ply : 0;

    int flag = best >= beta ? TT_LOWER : best > orig_alpha ? TT_EXACT : TT_UPPER;
    tt_store(key, best_move, to_tt(best, ply), depth, flag);
    return best;
}

/* ── Iterative deepening driver ──────────────────────────────────────────── */

SearchResult search_go(Position *pos, const Limits *lim,
                       const uint64_t *hist, int hist_len, int (*poll)(void)) {
    SearchResult res = {0};
    nodes = 0; stopped = 0; poll_fn = poll;
    node_limit = lim->nodes;
    max_depth = lim->depth ? lim->depth : MAX_PLY - 1;
    if (max_depth > MAX_PLY - 2) max_depth = MAX_PLY - 2;
    memset(killers, 0, sizeof killers);
    for (int c = 0; c < 2; c++) for (int a = 0; a < 64; a++) for (int b = 0; b < 64; b++)
        history[c][a][b] /= 2;

    if (hist_len > 2048) { hist += hist_len - 2048; hist_len = 2048; }
    if (hist_len > 0) memcpy(hstack, hist, hist_len * sizeof(uint64_t));
    hlen = hist_len;
    hstack[hlen++] = pos->hash;

    t_start = now_ms();
    double soft = 1e18, hard = 1e18;
    if (lim->movetime_ms) { soft = hard = lim->movetime_ms - move_overhead; if (soft < 1) soft = hard = 1; }
    else if (lim->time_ms && !lim->infinite) {
        int mtg = lim->movestogo > 0 ? lim->movestogo : 30;
        double avail = lim->time_ms - move_overhead;
        if (avail < 1) avail = 1;
        double base = avail / (double)mtg + lim->inc_ms * 0.75;
        soft = base;
        hard = base * 3; if (hard > avail * 0.5) hard = avail * 0.5;
        if (hard < 2) hard = 2;
        if (soft > hard) soft = hard;
    }
    t_soft = t_start + soft; t_hard = t_start + hard;

    MoveList root; generate_legal(pos, &root);
    if (root.count == 0) return res;
    res.best = root.moves[0];      /* fallback */

    for (int d = 1; d <= max_depth; d++) {
        int score, alpha = -INF_SCORE, beta = INF_SCORE, delta = ASP_DELTA;
        if (d >= ASP_START) {
            alpha = res.score - delta; if (alpha < -INF_SCORE) alpha = -INF_SCORE;
            beta  = res.score + delta; if (beta  >  INF_SCORE) beta  =  INF_SCORE;
        }
        for (;;) {
            score = negamax(pos, d, alpha, beta, 0, 0);
            if (stopped) break;
            if (score <= alpha && alpha > -INF_SCORE) {          /* fail low: widen downwards */
                beta  = (alpha + beta) / 2;
                alpha = score - delta; if (alpha < -INF_SCORE) alpha = -INF_SCORE;
            } else if (score >= beta && beta < INF_SCORE) {      /* fail high: widen upwards */
                beta  = score + delta; if (beta > INF_SCORE) beta = INF_SCORE;
                if (pv_len[0] > 0) res.best = pv[0][0];
            } else break;
            delta += delta / 2 + 5;
            if (score >= MATE_BOUND || score <= -MATE_BOUND) { alpha = -INF_SCORE; beta = INF_SCORE; }
        }
        if (stopped && d > 1) break;
        if (pv_len[0] > 0) res.best = pv[0][0];
        res.score = score; res.depth = d;
        double el = now_ms() - t_start;
        if (!lim->quiet) {
            printf("info depth %d score ", d);
            if (score >= MATE_BOUND)       printf("mate %d", (MATE - score + 1) / 2);
            else if (score <= -MATE_BOUND) printf("mate -%d", (MATE + score + 1) / 2);
            else                           printf("cp %d", score);
            printf(" nodes %llu nps %llu time %.0f pv", (unsigned long long)nodes,
                   (unsigned long long)(el > 0 ? nodes / el * 1000 : nodes), el);
            char b[8];
            for (int i = 0; i < pv_len[0]; i++) { move_to_str(pv[0][i], b); printf(" %s", b); }
            printf("\n"); fflush(stdout);
        }
        if (stopped) break;
        if (now_ms() >= t_soft && !lim->infinite) break;
        if (score >= MATE_BOUND && d >= 2 && MATE - score < d) break;   /* mate found */
        if (root.count == 1 && (lim->time_ms || lim->movetime_ms) && d >= 4) break;
    }
    res.nodes = nodes;
    res.ms = now_ms() - t_start;
    return res;
}
