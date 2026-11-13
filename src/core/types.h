// Apex chess engine
// Copyright (C) 2026 Apex contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace apex {

using Bitboard = std::uint64_t;
using Key = std::uint64_t;

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_TYPE_NB, NO_PIECE_TYPE = 6 };

// Piece encoding: type + 6 * color. NO_PIECE = 12.
enum Piece : int {
    W_PAWN = 0, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    NO_PIECE, PIECE_NB = 12
};
constexpr Piece make_piece(Color c, PieceType pt) { return Piece(pt + 6 * c); }
constexpr PieceType type_of(Piece p) { return PieceType(p % 6); }
constexpr Color color_of(Piece p) { return Color(p / 6); }

// Squares: a1 = 0, b1 = 1, ..., h8 = 63.
enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64, SQUARE_NB = 64
};
constexpr int file_of(Square s) { return s & 7; }
constexpr int rank_of(Square s) { return s >> 3; }
constexpr Square make_square(int file, int rank) { return Square(rank * 8 + file); }
constexpr Square relative_square(Color c, Square s) { return Square(s ^ (c * 56)); }
constexpr int relative_rank(Color c, int rank) { return c == WHITE ? rank : 7 - rank; }
constexpr int pawn_push(Color c) { return c == WHITE ? 8 : -8; }

enum CastlingRight : int {
    NO_CASTLING = 0,
    WHITE_OO = 1, WHITE_OOO = 2, BLACK_OO = 4, BLACK_OOO = 8,
    ALL_CASTLING = 15
};

// Move: 16 bits. bits 0-5 from, 6-11 to, 12-15 flags.
enum MoveFlag : int {
    MF_QUIET = 0, MF_DOUBLE_PUSH = 1, MF_KING_CASTLE = 2, MF_QUEEN_CASTLE = 3,
    MF_CAPTURE = 4, MF_EP = 5,
    MF_PROMO_N = 8, MF_PROMO_B = 9, MF_PROMO_R = 10, MF_PROMO_Q = 11,
    MF_PROMO_CAP_N = 12, MF_PROMO_CAP_B = 13, MF_PROMO_CAP_R = 14, MF_PROMO_CAP_Q = 15
};

class Move {
public:
    constexpr Move() : data(0) {}
    constexpr explicit Move(std::uint16_t d) : data(d) {}
    constexpr Move(Square from, Square to, int flag)
        : data(std::uint16_t(from | (to << 6) | (flag << 12))) {}

    constexpr Square from() const { return Square(data & 63); }
    constexpr Square to() const { return Square((data >> 6) & 63); }
    constexpr int flag() const { return data >> 12; }
    constexpr bool is_capture() const { return (data >> 12) & 4; }
    constexpr bool is_promotion() const { return (data >> 12) & 8; }
    constexpr bool is_castle() const { return flag() == MF_KING_CASTLE || flag() == MF_QUEEN_CASTLE; }
    constexpr bool is_ep() const { return flag() == MF_EP; }
    constexpr bool is_tactical() const { return (data >> 12) & 12; }
    constexpr PieceType promo_type() const { return PieceType(KNIGHT + ((data >> 12) & 3)); }
    constexpr std::uint16_t raw() const { return data; }
    constexpr bool is_none() const { return data == 0; }
    constexpr explicit operator bool() const { return data != 0; }
    constexpr bool operator==(const Move& o) const { return data == o.data; }
    constexpr bool operator!=(const Move& o) const { return data != o.data; }

    static constexpr Move none() { return Move(); }
    static constexpr Move null() { return Move(std::uint16_t(65)); }  // b1->a1 quiet: never legal

private:
    std::uint16_t data;
};

using Value = int;
constexpr Value VALUE_ZERO = 0;
constexpr Value VALUE_DRAW = 0;
constexpr Value VALUE_MATE = 32000;
constexpr Value VALUE_INFINITE = 32001;
constexpr Value VALUE_NONE = 32002;
constexpr int MAX_PLY = 246;
constexpr Value VALUE_MATE_IN_MAX_PLY = VALUE_MATE - MAX_PLY;
constexpr Value VALUE_MATED_IN_MAX_PLY = -VALUE_MATE_IN_MAX_PLY;

constexpr Value mate_in(int ply) { return VALUE_MATE - ply; }
constexpr Value mated_in(int ply) { return -VALUE_MATE + ply; }

std::string square_to_string(Square s);

}  // namespace apex
