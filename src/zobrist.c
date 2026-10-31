#include "engine.h"
#include "bitboard.h"

uint64_t ZOB_PIECE[2][6][64], ZOB_SIDE, ZOB_CASTLE[16], ZOB_EP[8];

#define ZOB_SEED 0x9E3779B97F4A7C15ULL
static uint64_t rng_state = ZOB_SEED;
static uint64_t rng(void) {                 /* xorshift64* */
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1DULL;
}

void init_zobrist(void) {
    rng_state = ZOB_SEED;
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 6; p++)
            for (int s = 0; s < 64; s++) ZOB_PIECE[c][p][s] = rng();
    ZOB_SIDE = rng();
    for (int i = 0; i < 16; i++) ZOB_CASTLE[i] = rng();
    for (int i = 0; i < 8; i++)  ZOB_EP[i] = rng();
}

uint64_t pos_hash(const Position *pos) {
    uint64_t h = 0;
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 6; p++) {
            Bitboard b = pos->pieces[c][p];
            while (b) h ^= ZOB_PIECE[c][p][bb_pop_lsb(&b)];
        }
    if (pos->side == BLACK) h ^= ZOB_SIDE;
    h ^= ZOB_CASTLE[pos->castling & 15];
    if (pos->ep_sq != NO_SQ) h ^= ZOB_EP[SQ_FILE(pos->ep_sq)];
    return h;
}
