#pragma once

#include "util/types.h"
#include <array>
#include <vector>
#include <bit>
#include <immintrin.h>

struct Bitboards {
    static inline std::array<Bitboard, 64> knight_attacks{};
    static inline std::array<Bitboard, 64> king_attacks{};
    static inline Bitboard pawn_attacks[COLOR_NB][64]{};

    static inline std::array<Bitboard, 64> rook_masks{};
    static inline std::array<Bitboard, 64> bishop_masks{};

    static inline std::array<std::vector<Bitboard>, 64> rook_table{};
    static inline std::array<std::vector<Bitboard>, 64> bishop_table{};

    static inline std::array<std::array<Bitboard, 64>, 64> between{};
    static inline std::array<std::array<Bitboard, 64>, 64> line{};

    static inline bool use_bmi2 = false;
    static inline bool initialized = false;

    static void init();
    static Bitboard rook_attacks(int sq, Bitboard occ);
    static Bitboard bishop_attacks(int sq, Bitboard occ);
    static Bitboard queen_attacks(int sq, Bitboard occ) {
        return rook_attacks(sq, occ) | bishop_attacks(sq, occ);
    }
};

inline bool cpu_has_bmi2() {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_cpu_supports("bmi2");
#elif defined(_MSC_VER) && defined(_M_X64)
    int info[4];
    __cpuid(info, 7);
    return (info[1] & (1 << 8)) != 0;
#else
    return false;
#endif
}

inline Bitboard rook_attacks_slow(int sq, Bitboard occ) {
    Bitboard attacks = 0;
    int f = file_of(sq), r = rank_of(sq);
    for (int rr = r + 1; rr <= 7; ++rr) { int s = make_square(f, rr); attacks |= (1ULL << s); if (occ & (1ULL << s)) break; }
    for (int rr = r - 1; rr >= 0; --rr) { int s = make_square(f, rr); attacks |= (1ULL << s); if (occ & (1ULL << s)) break; }
    for (int ff = f + 1; ff <= 7; ++ff) { int s = make_square(ff, r); attacks |= (1ULL << s); if (occ & (1ULL << s)) break; }
    for (int ff = f - 1; ff >= 0; --ff) { int s = make_square(ff, r); attacks |= (1ULL << s); if (occ & (1ULL << s)) break; }
    return attacks;
}

inline Bitboard bishop_attacks_slow(int sq, Bitboard occ) {
    Bitboard attacks = 0;
    int f = file_of(sq), r = rank_of(sq);
    for (int ff = f + 1, rr = r + 1; ff <= 7 && rr <= 7; ++ff, ++rr) { int s = make_square(ff, rr); attacks |= (1ULL << s); if (occ & (1ULL << s)) break; }
    for (int ff = f - 1, rr = r + 1; ff >= 0 && rr <= 7; --ff, ++rr) { int s = make_square(ff, rr); attacks |= (1ULL << s); if (occ & (1ULL << s)) break; }
    for (int ff = f + 1, rr = r - 1; ff <= 7 && rr >= 0; ++ff, --rr) { int s = make_square(ff, rr); attacks |= (1ULL << s); if (occ & (1ULL << s)) break; }
    for (int ff = f - 1, rr = r - 1; ff >= 0 && rr >= 0; --ff, --rr) { int s = make_square(ff, rr); attacks |= (1ULL << s); if (occ & (1ULL << s)) break; }
    return attacks;
}

inline Bitboard Bitboards::rook_attacks(int sq, Bitboard occ) {
    if (use_bmi2) {
#if defined(__BMI2__) || defined(_MSC_VER)
        Bitboard mask = rook_masks[sq];
        Bitboard occ_masked = occ & mask;
        u64 index = _pext_u64(occ_masked, mask);
        return rook_table[sq][index];
#else
        return rook_attacks_slow(sq, occ);
#endif
    }
    return rook_attacks_slow(sq, occ);
}

inline Bitboard Bitboards::bishop_attacks(int sq, Bitboard occ) {
    if (use_bmi2) {
#if defined(__BMI2__) || defined(_MSC_VER)
        Bitboard mask = bishop_masks[sq];
        Bitboard occ_masked = occ & mask;
        u64 index = _pext_u64(occ_masked, mask);
        return bishop_table[sq][index];
#else
        return bishop_attacks_slow(sq, occ);
#endif
    }
    return bishop_attacks_slow(sq, occ);
}

inline Bitboard mask_rook(int sq) {
    Bitboard mask = 0;
    int f = file_of(sq), r = rank_of(sq);
    for (int rr = r + 1; rr <= 6; ++rr) mask |= (1ULL << make_square(f, rr));
    for (int rr = r - 1; rr >= 1; --rr) mask |= (1ULL << make_square(f, rr));
    for (int ff = f + 1; ff <= 6; ++ff) mask |= (1ULL << make_square(ff, r));
    for (int ff = f - 1; ff >= 1; --ff) mask |= (1ULL << make_square(ff, r));
    return mask;
}

inline Bitboard mask_bishop(int sq) {
    Bitboard mask = 0;
    int f = file_of(sq), r = rank_of(sq);
    for (int ff = f + 1, rr = r + 1; ff <= 6 && rr <= 6; ++ff, ++rr) mask |= (1ULL << make_square(ff, rr));
    for (int ff = f - 1, rr = r + 1; ff >= 1 && rr <= 6; --ff, ++rr) mask |= (1ULL << make_square(ff, rr));
    for (int ff = f + 1, rr = r - 1; ff <= 6 && rr >= 1; ++ff, --rr) mask |= (1ULL << make_square(ff, rr));
    for (int ff = f - 1, rr = r - 1; ff >= 1 && rr >= 1; --ff, --rr) mask |= (1ULL << make_square(ff, rr));
    return mask;
}

inline Bitboard subset_from_index(Bitboard mask, int index) {
    Bitboard occ = 0;
    int bit = 0;
    while (mask) {
        Bitboard lsb = mask & -mask;
        if (index & (1 << bit)) occ |= lsb;
        mask ^= lsb;
        bit++;
    }
    return occ;
}

inline void Bitboards::init() {
    if (initialized) return;
    initialized = true;

    use_bmi2 = cpu_has_bmi2();

    for (int sq = 0; sq < 64; ++sq) {
        int f = file_of(sq), r = rank_of(sq);
        Bitboard b = 0;
        // Knight
        const int knight_dirs[8][2] = {{1,2},{2,1},{-1,2},{-2,1},{1,-2},{2,-1},{-1,-2},{-2,-1}};
        for (auto &d : knight_dirs) {
            int ff = f + d[0], rr = r + d[1];
            if (ff >= 0 && ff <= 7 && rr >= 0 && rr <= 7)
                b |= (1ULL << make_square(ff, rr));
        }
        knight_attacks[sq] = b;

        b = 0;
        const int king_dirs[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
        for (auto &d : king_dirs) {
            int ff = f + d[0], rr = r + d[1];
            if (ff >= 0 && ff <= 7 && rr >= 0 && rr <= 7)
                b |= (1ULL << make_square(ff, rr));
        }
        king_attacks[sq] = b;

        // Pawn attacks
        Bitboard w = 0, bl = 0;
        if (f > 0 && r < 7) w |= (1ULL << make_square(f - 1, r + 1));
        if (f < 7 && r < 7) w |= (1ULL << make_square(f + 1, r + 1));
        if (f > 0 && r > 0) bl |= (1ULL << make_square(f - 1, r - 1));
        if (f < 7 && r > 0) bl |= (1ULL << make_square(f + 1, r - 1));
        pawn_attacks[WHITE][sq] = w;
        pawn_attacks[BLACK][sq] = bl;

        rook_masks[sq] = mask_rook(sq);
        bishop_masks[sq] = mask_bishop(sq);
    }

    // Precompute pext attack tables if BMI2
    if (use_bmi2) {
        for (int sq = 0; sq < 64; ++sq) {
            Bitboard rmask = rook_masks[sq];
            int rbits = std::popcount(rmask);
            int rsize = 1 << rbits;
            rook_table[sq].resize(rsize);
            for (int index = 0; index < rsize; ++index) {
                Bitboard occ = subset_from_index(rmask, index);
                rook_table[sq][index] = rook_attacks_slow(sq, occ);
            }

            Bitboard bmask = bishop_masks[sq];
            int bbits = std::popcount(bmask);
            int bsize = 1 << bbits;
            bishop_table[sq].resize(bsize);
            for (int index = 0; index < bsize; ++index) {
                Bitboard occ = subset_from_index(bmask, index);
                bishop_table[sq][index] = bishop_attacks_slow(sq, occ);
            }
        }
    }

    // line and between
    for (int a = 0; a < 64; ++a) {
        for (int b = 0; b < 64; ++b) {
            line[a][b] = 0;
            between[a][b] = 0;
            if (a == b) continue;
            Bitboard occ = (1ULL << a) | (1ULL << b);
            Bitboard rook_line = rook_attacks_slow(a, 0ULL) & rook_attacks_slow(b, 0ULL);
            Bitboard bishop_line = bishop_attacks_slow(a, 0ULL) & bishop_attacks_slow(b, 0ULL);
            Bitboard l = rook_line | bishop_line;
            if (l & (1ULL << b)) {
                line[a][b] = l | (1ULL << a) | (1ULL << b);
                Bitboard between_bb = (rook_attacks_slow(a, occ) | bishop_attacks_slow(a, occ)) &
                                      (rook_attacks_slow(b, occ) | bishop_attacks_slow(b, occ));
                between[a][b] = between_bb;
            }
        }
    }
}
