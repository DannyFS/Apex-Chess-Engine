// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "bitboard.h"
#include "types.h"

namespace apex {

namespace zobrist {
extern Key PieceSquare[PIECE_NB][SQUARE_NB];
extern Key Castling[16];
extern Key EnPassant[8];
extern Key Side;
void init();
}  // namespace zobrist

// Initializes all static tables (bitboards, magics, zobrist). Call once at startup.
void init_core();

// Irreversible state, saved on every make_move.
struct StateInfo {
    Key key;
    Key pawn_key;
    int castling;
    Square ep;
    int rule50;
    int plies_from_null;
    Piece captured;
    Bitboard checkers;
    Bitboard blockers[COLOR_NB];  // pieces (either color) shielding king of color c from sliders
    Bitboard pinners[COLOR_NB];   // sliders of color ~c pinning pieces to king of color c
    Bitboard check_squares[PIECE_TYPE_NB];  // squares from which each piece type gives check to the side NOT to move
};

class Position {
public:
    static constexpr const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Position() { set(StartFEN); }
    explicit Position(const std::string& fen) { set(fen); }

    // Returns false on a malformed FEN (position is then left in the start position).
    bool set(const std::string& fen);
    std::string fen() const;
    std::string pretty() const;

    // Accessors
    Color side_to_move() const { return stm; }
    Piece piece_on(Square s) const { return board[s]; }
    bool empty(Square s) const { return board[s] == NO_PIECE; }
    Bitboard pieces() const { return by_color[WHITE] | by_color[BLACK]; }
    Bitboard pieces(Color c) const { return by_color[c]; }
    Bitboard pieces(PieceType pt) const { return by_type[pt]; }
    Bitboard pieces(PieceType a, PieceType b) const { return by_type[a] | by_type[b]; }
    Bitboard pieces(Color c, PieceType pt) const { return by_color[c] & by_type[pt]; }
    Bitboard pieces(Color c, PieceType a, PieceType b) const { return by_color[c] & (by_type[a] | by_type[b]); }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }
    int castling_rights() const { return st().castling; }
    bool can_castle(int cr) const { return st().castling & cr; }
    Square ep_square() const { return st().ep; }
    int rule50() const { return st().rule50; }
    int plies_from_null() const { return st().plies_from_null; }
    int game_ply() const { return ply_count; }
    Key key() const { return st().key; }
    Key pawn_key() const { return st().pawn_key; }
    Bitboard checkers() const { return st().checkers; }
    bool in_check() const { return st().checkers != 0; }
    Bitboard blockers_for_king(Color c) const { return st().blockers[c]; }
    Bitboard pinned(Color c) const { return st().blockers[c] & pieces(c); }
    Piece captured_piece() const { return st().captured; }
    int non_pawn_material_count(Color c) const {
        return popcount(pieces(c) & ~pieces(PAWN) & ~pieces(KING));
    }

    // Attacks
    Bitboard attackers_to(Square s, Bitboard occ) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, pieces()); }
    bool attacked_by(Color c, Square s, Bitboard occ) const;

    // Move properties
    bool legal(Move m) const;         // m must be pseudo-legal
    bool pseudo_legal(Move m) const;  // validates arbitrary 16-bit moves (TT, killers)
    bool gives_check(Move m) const;
    PieceType moved_piece_type(Move m) const { return type_of(board[m.from()]); }
    bool see_ge(Move m, int threshold) const;

    // Make / unmake
    void make_move(Move m);
    void unmake_move(Move m);
    void make_null();
    void unmake_null();

    // Draws (excluding stalemate, which needs move generation)
    bool is_repetition(int search_ply) const;
    bool is_fifty_move_draw() const;
    bool insufficient_material() const;

    // Parse a UCI move string against the legal moves of this position; returns Move::none() if illegal.
    Move parse_uci(const std::string& s) const;
    static std::string move_to_uci(Move m);

    // Full recomputation of the key from scratch (for verification).
    Key compute_key() const;
    Key compute_pawn_key() const;
    bool is_consistent(std::string* why = nullptr) const;

private:
    const StateInfo& st() const { return states.back(); }
    StateInfo& st() { return states.back(); }

    void put_piece(Piece p, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    void update_check_info();
    void compute_blockers(Color c);
    bool ep_capturable(Square ep, Color by) const;

    Piece board[SQUARE_NB];
    Bitboard by_type[PIECE_TYPE_NB];
    Bitboard by_color[COLOR_NB];
    Color stm;
    int ply_count;  // half-moves since game start
    int castling_mask[SQUARE_NB];
    std::vector<StateInfo> states;
};

}  // namespace apex
