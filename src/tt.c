#include <stdlib.h>
#include <string.h>
#include "engine.h"

static TTEntry *table = NULL;
static size_t   mask  = 0;

void tt_resize(size_t mb) {
    size_t n = 1;
    while (n * 2 * sizeof(TTEntry) <= mb * 1024 * 1024) n *= 2;
    free(table);
    table = calloc(n, sizeof(TTEntry));
    mask = n - 1;
}
void tt_clear(void) { memset(table, 0, (mask + 1) * sizeof(TTEntry)); }

TTEntry *tt_probe(uint64_t key) {
    TTEntry *e = &table[key & mask];
    return e->key == key && e->flag != TT_NONE ? e : NULL;
}

void tt_store(uint64_t key, Move m, int score, int depth, int flag) {
    TTEntry *e = &table[key & mask];
    /* replace if different position or deeper-or-equal search */
    if (e->key != key || depth >= e->depth || flag == TT_EXACT) {
        if (m || e->key != key) e->move = m;
        e->key = key; e->score = (int16_t)score;
        e->depth = (int8_t)depth; e->flag = (uint8_t)flag;
    }
}
