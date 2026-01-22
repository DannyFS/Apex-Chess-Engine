#pragma once

#include "util/types.h"
#include "util/bitboard.h"
#include "util/zobrist.h"
#include "nnue/nnue_features.h"
#include <array>
#include <string>
#include <vector>

struct StateInfo {
    u64 key = 0;
    int castling = 0;
    int ep_square = -1;
    int rule50 = 0;
    Piece captured = NO_PIECE;
    Move move = MOVE_NONE;
    int ply = 0;
    Color side = WHITE;
    int king_sq[COLOR_NB] = { -1, -1 };
    NNUE::NNUEState nnue;
};

class Position {
public:
    Position();

    bool set_fen(const std::string& fen);
    void set_startpos();

    Color side_to_move() const { return side; }
    Piece piece_at(int sq) const { return board[sq]; }
    Bitboard pieces_bb(Piece p) const { return piece_bb[p - 1]; }
    Bitboard occupancy(Color c) const { return occ[c]; }
    Bitboard occupancy_all() const { return occ_all; }
    int king_square(Color c) const { return king_sq[c]; }
    int castling_rights() const { return castling; }
    int ep_square() const { return ep_sq; }
    int rule50_count() const { return rule50; }
    int ply() const { return ply_count; }
    u64 key() const { return zobrist; }

    const NNUE::NNUEState& nnue_state() const { return nnue; }
    NNUE::NNUEState& nnue_state() { return nnue; }

    void do_move(Move m);
    void undo_move();
    void do_null_move();
    void undo_null_move();

    bool is_attacked(int sq, Color by) const;
    bool in_check(Color c) const;
    bool gives_check(Move m) const;

    bool is_repetition() const;

    int simple_eval() const;

private:
    std::array<Piece, 64> board{};
    std::array<Bitboard, 12> piece_bb{}; // W pawn..W king, B pawn..B king
    Bitboard occ[COLOR_NB]{};
    Bitboard occ_all = 0;
    Color side = WHITE;
    int castling = 0;
    int ep_sq = -1;
    int rule50 = 0;
    int ply_count = 0;
    u64 zobrist = 0;
    int king_sq[COLOR_NB] = { -1, -1 };

    NNUE::NNUEState nnue;

    std::vector<StateInfo> states;

    void clear();
    void put_piece(Piece p, int sq);
    void remove_piece(Piece p, int sq);
    void move_piece(Piece p, int from, int to);
    void update_occupancy();
    void update_zobrist();
    void update_castling(int from, int to, Piece moved, Piece captured);
};
