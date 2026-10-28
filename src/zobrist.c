#include "engine.h"
#include "bitboard.h"

static uint64_t Z_PIECE[2][6][64], Z_SIDE, Z_CASTLE[16], Z_EP[8];

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static uint64_t rng(void) {                 /* xorshift64* */
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1DULL;
}

void init_zobrist(void) {
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 6; p++)
            for (int s = 0; s < 64; s++) Z_PIECE[c][p][s] = rng();
    Z_SIDE = rng();
    for (int i = 0; i < 16; i++) Z_CASTLE[i] = rng();
    for (int i = 0; i < 8; i++)  Z_EP[i] = rng();
}

uint64_t pos_hash(const Position *pos) {
    uint64_t h = 0;
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 6; p++) {
            Bitboard b = pos->pieces[c][p];
            while (b) h ^= Z_PIECE[c][p][bb_pop_lsb(&b)];
        }
    if (pos->side == BLACK) h ^= Z_SIDE;
    h ^= Z_CASTLE[pos->castling & 15];
    if (pos->ep_sq != NO_SQ) h ^= Z_EP[SQ_FILE(pos->ep_sq)];
    return h;
}
