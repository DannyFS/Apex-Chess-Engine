#pragma once

#include <cstdint>
#include <string>
#include <bit>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using Bitboard = u64;

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
inline Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5, PIECE_TYPE_NB = 6 };

enum Piece : int {
    NO_PIECE = 0,
    W_PAWN = 1, W_KNIGHT = 2, W_BISHOP = 3, W_ROOK = 4, W_QUEEN = 5, W_KING = 6,
    B_PAWN = 7, B_KNIGHT = 8, B_BISHOP = 9, B_ROOK = 10, B_QUEEN = 11, B_KING = 12,
    PIECE_NB = 13
};

constexpr int MAX_PLY = 128;

inline Color piece_color(Piece p) {
    if (p >= B_PAWN) return BLACK;
    if (p >= W_PAWN && p <= W_KING) return WHITE;
    return WHITE;
}

inline PieceType piece_type(Piece p) {
    switch (p) {
        case W_PAWN: case B_PAWN: return PAWN;
        case W_KNIGHT: case B_KNIGHT: return KNIGHT;
        case W_BISHOP: case B_BISHOP: return BISHOP;
        case W_ROOK: case B_ROOK: return ROOK;
        case W_QUEEN: case B_QUEEN: return QUEEN;
        case W_KING: case B_KING: return KING;
        default: return PAWN;
    }
}

inline bool is_slider(PieceType pt) { return pt == BISHOP || pt == ROOK || pt == QUEEN; }

inline int file_of(int sq) { return sq & 7; }
inline int rank_of(int sq) { return sq >> 3; }
inline int make_square(int file, int rank) { return (rank << 3) | file; }

using Move = u32;
constexpr Move MOVE_NONE = 0;

enum MoveFlags : u32 {
    MF_NONE = 0,
    MF_CAPTURE = 1 << 0,
    MF_ENPASSANT = 1 << 1,
    MF_CASTLING = 1 << 2,
    MF_PROMOTION = 1 << 3,
    MF_DOUBLE_PUSH = 1 << 4
};

inline Move make_move(int from, int to, PieceType promo = PIECE_TYPE_NB, u32 flags = MF_NONE) {
    return (Move)(from | (to << 6) | (promo << 12) | (flags << 16));
}

inline int from_sq(Move m) { return int(m & 0x3F); }
inline int to_sq(Move m) { return int((m >> 6) & 0x3F); }
inline PieceType promo_type(Move m) { return PieceType((m >> 12) & 0xF); }
inline u32 move_flags(Move m) { return (m >> 16) & 0x1F; }

inline bool is_capture(Move m) { return move_flags(m) & MF_CAPTURE; }
inline bool is_enpassant(Move m) { return move_flags(m) & MF_ENPASSANT; }
inline bool is_castling(Move m) { return move_flags(m) & MF_CASTLING; }
inline bool is_promotion(Move m) { return move_flags(m) & MF_PROMOTION; }

enum Bound : u8 { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

constexpr int MATE_SCORE = 32000;
constexpr int MATE_IN_MAX_PLY = MATE_SCORE - MAX_PLY;

inline int mate_in(int ply) { return MATE_SCORE - ply; }
inline int mated_in(int ply) { return -MATE_SCORE + ply; }

constexpr int MAX_MOVES = 256;

struct MoveList {
    Move moves[MAX_MOVES];
    int size = 0;
    void push(Move m) { moves[size++] = m; }
};

inline std::string square_to_string(int sq) {
    char file = char('a' + file_of(sq));
    char rank = char('1' + rank_of(sq));
    return std::string() + file + rank;
}

inline std::string move_to_uci(Move m) {
    if (m == MOVE_NONE) return "0000";
    std::string s = square_to_string(from_sq(m)) + square_to_string(to_sq(m));
    if (is_promotion(m)) {
        char promo = 'q';
        switch (promo_type(m)) {
            case KNIGHT: promo = 'n'; break;
            case BISHOP: promo = 'b'; break;
            case ROOK: promo = 'r'; break;
            case QUEEN: promo = 'q'; break;
            default: break;
        }
        s.push_back(promo);
    }
    return s;
}

inline int pop_lsb(Bitboard& b) {
    int sq = std::countr_zero(b);
    b &= b - 1;
    return sq;
}

inline int clamp_score(int score) {
    if (score > MATE_IN_MAX_PLY) return MATE_IN_MAX_PLY;
    if (score < -MATE_IN_MAX_PLY) return -MATE_IN_MAX_PLY;
    return score;
}
