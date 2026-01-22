#include "position.h"
#include "nnue/nnue_loader.h"
#include <sstream>
#include <cctype>

static constexpr int CASTLE_WK = 1;
static constexpr int CASTLE_WQ = 2;
static constexpr int CASTLE_BK = 4;
static constexpr int CASTLE_BQ = 8;

Position::Position() {
    Bitboards::init();
    Zobrist::init();
    clear();
}

void Position::clear() {
    board.fill(NO_PIECE);
    piece_bb.fill(0);
    occ[WHITE] = occ[BLACK] = occ_all = 0;
    side = WHITE;
    castling = 0;
    ep_sq = -1;
    rule50 = 0;
    ply_count = 0;
    zobrist = 0;
    king_sq[WHITE] = king_sq[BLACK] = -1;
    states.clear();
}

void Position::put_piece(Piece p, int sq) {
    board[sq] = p;
    if (p != NO_PIECE) {
        piece_bb[p - 1] |= (1ULL << sq);
        occ[piece_color(p)] |= (1ULL << sq);
        occ_all |= (1ULL << sq);
        if (piece_type(p) == KING) king_sq[piece_color(p)] = sq;
    }
}

void Position::remove_piece(Piece p, int sq) {
    board[sq] = NO_PIECE;
    if (p != NO_PIECE) {
        piece_bb[p - 1] &= ~(1ULL << sq);
        occ[piece_color(p)] &= ~(1ULL << sq);
        occ_all &= ~(1ULL << sq);
    }
}

void Position::move_piece(Piece p, int from, int to) {
    remove_piece(p, from);
    put_piece(p, to);
}

void Position::update_occupancy() {
    occ[WHITE] = occ[BLACK] = 0;
    for (int p = W_PAWN; p <= W_KING; ++p) occ[WHITE] |= piece_bb[p - 1];
    for (int p = B_PAWN; p <= B_KING; ++p) occ[BLACK] |= piece_bb[p - 1];
    occ_all = occ[WHITE] | occ[BLACK];
}

void Position::update_zobrist() {
    zobrist = 0;
    for (int sq = 0; sq < 64; ++sq) {
        Piece p = board[sq];
        if (p != NO_PIECE) zobrist ^= Zobrist::piece[p - 1][sq];
    }
    zobrist ^= Zobrist::castling[castling];
    if (ep_sq != -1) zobrist ^= Zobrist::ep_file[file_of(ep_sq)];
    if (side == BLACK) zobrist ^= Zobrist::side;
}

bool Position::set_fen(const std::string& fen) {
    clear();

    std::istringstream iss(fen);
    std::string board_part, side_part, castle_part, ep_part;
    int halfmove = 0, fullmove = 1;

    if (!(iss >> board_part >> side_part >> castle_part >> ep_part >> halfmove >> fullmove))
        return false;

    int sq = 56; // a8
    for (char c : board_part) {
        if (c == '/') { sq -= 16; continue; }
        if (std::isdigit(static_cast<unsigned char>(c))) { sq += (c - '0'); continue; }
        Piece p = NO_PIECE;
        switch (c) {
            case 'P': p = W_PAWN; break; case 'N': p = W_KNIGHT; break; case 'B': p = W_BISHOP; break;
            case 'R': p = W_ROOK; break; case 'Q': p = W_QUEEN; break; case 'K': p = W_KING; break;
            case 'p': p = B_PAWN; break; case 'n': p = B_KNIGHT; break; case 'b': p = B_BISHOP; break;
            case 'r': p = B_ROOK; break; case 'q': p = B_QUEEN; break; case 'k': p = B_KING; break;
            default: return false;
        }
        put_piece(p, sq);
        sq++;
    }

    side = (side_part == "w") ? WHITE : BLACK;

    castling = 0;
    if (castle_part.find('K') != std::string::npos) castling |= CASTLE_WK;
    if (castle_part.find('Q') != std::string::npos) castling |= CASTLE_WQ;
    if (castle_part.find('k') != std::string::npos) castling |= CASTLE_BK;
    if (castle_part.find('q') != std::string::npos) castling |= CASTLE_BQ;

    if (ep_part != "-") {
        int file = ep_part[0] - 'a';
        int rank = ep_part[1] - '1';
        ep_sq = make_square(file, rank);
        // only keep ep if capture is possible
        Bitboard pawns = piece_bb[(side == WHITE) ? (W_PAWN - 1) : (B_PAWN - 1)];
        Bitboard attacks = Bitboards::pawn_attacks[~side][ep_sq];
        if (!(attacks & pawns)) ep_sq = -1;
    } else {
        ep_sq = -1;
    }

    rule50 = halfmove;
    ply_count = (fullmove - 1) * 2 + (side == BLACK ? 1 : 0);

    update_zobrist();

    if (NNUE::loaded()) NNUE::accum_init(*this, nnue, NNUE::weights());

    return true;
}

void Position::set_startpos() {
    set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void Position::update_castling(int from, int to, Piece moved, Piece captured) {
    int old = castling;
    if (piece_type(moved) == KING) {
        if (piece_color(moved) == WHITE) castling &= ~(CASTLE_WK | CASTLE_WQ);
        else castling &= ~(CASTLE_BK | CASTLE_BQ);
    }
    if (piece_type(moved) == ROOK) {
        if (from == make_square(0,0)) castling &= ~CASTLE_WQ;
        if (from == make_square(7,0)) castling &= ~CASTLE_WK;
        if (from == make_square(0,7)) castling &= ~CASTLE_BQ;
        if (from == make_square(7,7)) castling &= ~CASTLE_BK;
    }
    if (captured != NO_PIECE && piece_type(captured) == ROOK) {
        if (to == make_square(0,0)) castling &= ~CASTLE_WQ;
        if (to == make_square(7,0)) castling &= ~CASTLE_WK;
        if (to == make_square(0,7)) castling &= ~CASTLE_BQ;
        if (to == make_square(7,7)) castling &= ~CASTLE_BK;
    }

    if (old != castling) {
        zobrist ^= Zobrist::castling[old];
        zobrist ^= Zobrist::castling[castling];
    }
}

void Position::do_move(Move m) {
    int from = from_sq(m);
    int to = to_sq(m);
    u32 flags = move_flags(m);
    Piece moved = board[from];
    Piece captured = board[to];
    int cap_sq = to;

    StateInfo st;
    st.key = zobrist;
    st.castling = castling;
    st.ep_square = ep_sq;
    st.rule50 = rule50;
    st.captured = captured;
    st.move = m;
    st.ply = ply_count;
    st.side = side;
    st.king_sq[WHITE] = king_sq[WHITE];
    st.king_sq[BLACK] = king_sq[BLACK];
    st.nnue = nnue;
    states.push_back(st);

    // clear ep hash
    if (ep_sq != -1) zobrist ^= Zobrist::ep_file[file_of(ep_sq)];
    ep_sq = -1;

    bool is_ep = flags & MF_ENPASSANT;
    bool is_castle = flags & MF_CASTLING;
    bool is_promo = flags & MF_PROMOTION;
    bool king_moved = (piece_type(moved) == KING);

    // Determine capture square for en passant
    if (is_ep) {
        cap_sq = to + (side == WHITE ? -8 : 8);
        captured = board[cap_sq];
    }

    // NNUE update (pre-move info)
    if (NNUE::loaded()) {
        NNUE::accum_update_move(*this, nnue, NNUE::weights(), moved, from, to, captured, cap_sq, king_moved, side);
        if (is_castle) {
            Piece rook = (side == WHITE) ? W_ROOK : B_ROOK;
            int r_from = 0, r_to = 0;
            if (side == WHITE) {
                if (to == make_square(6,0)) { r_from = make_square(7,0); r_to = make_square(5,0); }
                else { r_from = make_square(0,0); r_to = make_square(3,0); }
            } else {
                if (to == make_square(6,7)) { r_from = make_square(7,7); r_to = make_square(5,7); }
                else { r_from = make_square(0,7); r_to = make_square(3,7); }
            }
            NNUE::accum_remove_piece_both(*this, nnue, NNUE::weights(), rook, r_from);
            NNUE::accum_add_piece_both(*this, nnue, NNUE::weights(), rook, r_to);
        }
    }

    // update zobrist: remove moved and captured pieces first
    if (moved != NO_PIECE) zobrist ^= Zobrist::piece[moved - 1][from];
    if (captured != NO_PIECE) {
        int csq = is_ep ? cap_sq : to;
        zobrist ^= Zobrist::piece[captured - 1][csq];
    }

    // rule50
    if (piece_type(moved) == PAWN || captured != NO_PIECE || is_ep) rule50 = 0; else rule50++;

    update_castling(from, to, moved, captured);

    if (is_castle) {
        // Move king and rook
        if (side == WHITE) {
            if (to == make_square(6,0)) { // O-O
                move_piece(W_KING, from, to);
                move_piece(W_ROOK, make_square(7,0), make_square(5,0));
                zobrist ^= Zobrist::piece[W_KING - 1][to];
                zobrist ^= Zobrist::piece[W_ROOK - 1][make_square(7,0)];
                zobrist ^= Zobrist::piece[W_ROOK - 1][make_square(5,0)];
            } else { // O-O-O
                move_piece(W_KING, from, to);
                move_piece(W_ROOK, make_square(0,0), make_square(3,0));
                zobrist ^= Zobrist::piece[W_KING - 1][to];
                zobrist ^= Zobrist::piece[W_ROOK - 1][make_square(0,0)];
                zobrist ^= Zobrist::piece[W_ROOK - 1][make_square(3,0)];
            }
            king_sq[WHITE] = to;
        } else {
            if (to == make_square(6,7)) { // O-O
                move_piece(B_KING, from, to);
                move_piece(B_ROOK, make_square(7,7), make_square(5,7));
                zobrist ^= Zobrist::piece[B_KING - 1][to];
                zobrist ^= Zobrist::piece[B_ROOK - 1][make_square(7,7)];
                zobrist ^= Zobrist::piece[B_ROOK - 1][make_square(5,7)];
            } else {
                move_piece(B_KING, from, to);
                move_piece(B_ROOK, make_square(0,7), make_square(3,7));
                zobrist ^= Zobrist::piece[B_KING - 1][to];
                zobrist ^= Zobrist::piece[B_ROOK - 1][make_square(0,7)];
                zobrist ^= Zobrist::piece[B_ROOK - 1][make_square(3,7)];
            }
            king_sq[BLACK] = to;
        }
    } else if (is_ep) {
        move_piece(moved, from, to);
        remove_piece(captured, cap_sq);
        zobrist ^= Zobrist::piece[moved - 1][to];
    } else if (is_promo) {
        // remove pawn, add promoted piece
        remove_piece(moved, from);
        if (captured != NO_PIECE) remove_piece(captured, to);
        Piece promo_piece = NO_PIECE;
        switch (promo_type(m)) {
            case KNIGHT: promo_piece = (side == WHITE) ? W_KNIGHT : B_KNIGHT; break;
            case BISHOP: promo_piece = (side == WHITE) ? W_BISHOP : B_BISHOP; break;
            case ROOK:   promo_piece = (side == WHITE) ? W_ROOK   : B_ROOK; break;
            case QUEEN:  promo_piece = (side == WHITE) ? W_QUEEN  : B_QUEEN; break;
            default:     promo_piece = (side == WHITE) ? W_QUEEN  : B_QUEEN; break;
        }
        put_piece(promo_piece, to);
        zobrist ^= Zobrist::piece[promo_piece - 1][to];

        if (NNUE::loaded()) {
            // Replace pawn feature at 'to' with promoted piece feature
            NNUE::accum_remove_piece_both(*this, nnue, NNUE::weights(), moved, to);
            NNUE::accum_add_piece_both(*this, nnue, NNUE::weights(), promo_piece, to);
        }
    } else {
        if (captured != NO_PIECE) remove_piece(captured, to);
        move_piece(moved, from, to);
        zobrist ^= Zobrist::piece[moved - 1][to];
    }

    // set EP square if double push
    if (flags & MF_DOUBLE_PUSH) {
        int ep = (side == WHITE) ? (to - 8) : (to + 8);
        // only set if capture possible
        Bitboard pawns = piece_bb[(side == WHITE) ? (B_PAWN - 1) : (W_PAWN - 1)];
        Bitboard attacks = Bitboards::pawn_attacks[side][ep];
        if (attacks & pawns) {
            ep_sq = ep;
            zobrist ^= Zobrist::ep_file[file_of(ep_sq)];
        }
    }

    side = ~side;
    zobrist ^= Zobrist::side;
    ply_count++;

    if (NNUE::loaded() && king_moved) {
        // recompute accumulator for side that moved king
        NNUE::accum_recompute_side(*this, nnue, NNUE::weights(), ~side);
    }
}

void Position::undo_move() {
    if (states.empty()) return;
    StateInfo st = states.back();
    states.pop_back();

    Move m = st.move;
    int from = from_sq(m);
    int to = to_sq(m);
    u32 flags = move_flags(m);

    side = st.side;

    // restore simple state
    castling = st.castling;
    ep_sq = st.ep_square;
    rule50 = st.rule50;
    ply_count = st.ply;
    zobrist = st.key;
    king_sq[WHITE] = st.king_sq[WHITE];
    king_sq[BLACK] = st.king_sq[BLACK];
    nnue = st.nnue;

    Piece moved = board[to];
    Piece captured = st.captured;
    bool is_ep = flags & MF_ENPASSANT;
    bool is_castle = flags & MF_CASTLING;
    bool is_promo = flags & MF_PROMOTION;

    if (is_castle) {
        if (side == WHITE) {
            if (to == make_square(6,0)) {
                move_piece(W_KING, to, from);
                move_piece(W_ROOK, make_square(5,0), make_square(7,0));
            } else {
                move_piece(W_KING, to, from);
                move_piece(W_ROOK, make_square(3,0), make_square(0,0));
            }
        } else {
            if (to == make_square(6,7)) {
                move_piece(B_KING, to, from);
                move_piece(B_ROOK, make_square(5,7), make_square(7,7));
            } else {
                move_piece(B_KING, to, from);
                move_piece(B_ROOK, make_square(3,7), make_square(0,7));
            }
        }
        return;
    }

    if (is_ep) {
        move_piece(moved, to, from);
        int cap_sq = to + (side == WHITE ? -8 : 8);
        put_piece(captured, cap_sq);
        return;
    }

    if (is_promo) {
        // remove promoted piece, restore pawn
        remove_piece(moved, to);
        Piece pawn = (side == WHITE) ? W_PAWN : B_PAWN;
        put_piece(pawn, from);
        if (captured != NO_PIECE) put_piece(captured, to);
        return;
    }

    // normal move
    move_piece(moved, to, from);
    if (captured != NO_PIECE) put_piece(captured, to);
}

void Position::do_null_move() {
    StateInfo st;
    st.key = zobrist;
    st.castling = castling;
    st.ep_square = ep_sq;
    st.rule50 = rule50;
    st.captured = NO_PIECE;
    st.move = MOVE_NONE;
    st.ply = ply_count;
    st.side = side;
    st.king_sq[WHITE] = king_sq[WHITE];
    st.king_sq[BLACK] = king_sq[BLACK];
    st.nnue = nnue;
    states.push_back(st);

    if (ep_sq != -1) zobrist ^= Zobrist::ep_file[file_of(ep_sq)];
    ep_sq = -1;
    rule50++;
    side = ~side;
    zobrist ^= Zobrist::side;
    ply_count++;
}

void Position::undo_null_move() {
    if (states.empty()) return;
    StateInfo st = states.back();
    states.pop_back();
    zobrist = st.key;
    castling = st.castling;
    ep_sq = st.ep_square;
    rule50 = st.rule50;
    ply_count = st.ply;
    side = st.side;
    king_sq[WHITE] = st.king_sq[WHITE];
    king_sq[BLACK] = st.king_sq[BLACK];
    nnue = st.nnue;
}

bool Position::is_attacked(int sq, Color by) const {
    Bitboard occ_all_local = occ_all;
    if (Bitboards::pawn_attacks[~by][sq] & piece_bb[(by == WHITE) ? (W_PAWN - 1) : (B_PAWN - 1)]) return true;
    if (Bitboards::knight_attacks[sq] & piece_bb[(by == WHITE) ? (W_KNIGHT - 1) : (B_KNIGHT - 1)]) return true;
    if (Bitboards::king_attacks[sq] & piece_bb[(by == WHITE) ? (W_KING - 1) : (B_KING - 1)]) return true;

    Bitboard bishops = piece_bb[(by == WHITE) ? (W_BISHOP - 1) : (B_BISHOP - 1)];
    Bitboard rooks = piece_bb[(by == WHITE) ? (W_ROOK - 1) : (B_ROOK - 1)];
    Bitboard queens = piece_bb[(by == WHITE) ? (W_QUEEN - 1) : (B_QUEEN - 1)];

    if (Bitboards::bishop_attacks(sq, occ_all_local) & (bishops | queens)) return true;
    if (Bitboards::rook_attacks(sq, occ_all_local) & (rooks | queens)) return true;

    return false;
}

bool Position::in_check(Color c) const {
    return is_attacked(king_sq[c], ~c);
}

bool Position::gives_check(Move m) const {
    Position tmp = *this;
    tmp.do_move(m);
    bool check = tmp.in_check(tmp.side_to_move());
    return check;
}

bool Position::is_repetition() const {
    int reps = 0;
    for (int i = (int)states.size() - 2; i >= 0; --i) {
        if (states[i].key == zobrist) {
            reps++;
            if (reps >= 2) return true;
        }
        if (states[i].rule50 == 0) break;
    }
    return false;
}

int Position::simple_eval() const {
    static const int piece_values[PIECE_TYPE_NB] = {100, 320, 330, 500, 900, 0};
    int score = 0;
    for (int sq = 0; sq < 64; ++sq) {
        Piece p = board[sq];
        if (p == NO_PIECE) continue;
        int val = piece_values[piece_type(p)];
        score += (piece_color(p) == WHITE) ? val : -val;
    }
    return (side == WHITE) ? score : -score;
}
