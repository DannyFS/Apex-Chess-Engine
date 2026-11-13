// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include "position.h"

#include <cstring>
#include <sstream>

#include "movegen.h"
#include "prng.h"

namespace apex {

namespace zobrist {
Key PieceSquare[PIECE_NB][SQUARE_NB];
Key Castling[16];
Key EnPassant[8];
Key Side;

void init() {
    PRNG rng(0x41504558ULL);  // "APEX"
    for (auto& p : PieceSquare)
        for (auto& k : p) k = rng.rand();
    for (auto& k : Castling) k = rng.rand();
    for (auto& k : EnPassant) k = rng.rand();
    Side = rng.rand();
    // Make castling keys composable: key(rights) = XOR of the keys of the individual rights.
    for (int cr = 0; cr < 16; ++cr) {
        Key k = 0;
        for (int bit = 0; bit < 4; ++bit)
            if (cr & (1 << bit)) k ^= Castling[1 << bit];
        if (cr != 1 && cr != 2 && cr != 4 && cr != 8) Castling[cr] = k;
    }
    Castling[0] = 0;
}
}  // namespace zobrist

void init_core() {
    bb::init();
    zobrist::init();
}

namespace {
constexpr const char* PieceChars = "PNBRQKpnbrqk";
}

// ---------------------------------------------------------------- setup

void Position::put_piece(Piece p, Square s) {
    board[s] = p;
    by_type[type_of(p)] |= square_bb(s);
    by_color[color_of(p)] |= square_bb(s);
}

void Position::remove_piece(Square s) {
    Piece p = board[s];
    by_type[type_of(p)] ^= square_bb(s);
    by_color[color_of(p)] ^= square_bb(s);
    board[s] = NO_PIECE;
}

void Position::move_piece(Square from, Square to) {
    Piece p = board[from];
    Bitboard ft = square_bb(from) | square_bb(to);
    by_type[type_of(p)] ^= ft;
    by_color[color_of(p)] ^= ft;
    board[from] = NO_PIECE;
    board[to] = p;
}

bool Position::set(const std::string& fen) {
    std::memset(board, 0, sizeof(board));
    for (auto& b : board) b = NO_PIECE;
    std::memset(by_type, 0, sizeof(by_type));
    std::memset(by_color, 0, sizeof(by_color));
    states.clear();
    states.reserve(1024);
    states.emplace_back();
    StateInfo& s = st();
    std::memset(&s, 0, sizeof(StateInfo));
    s.ep = SQ_NONE;
    s.captured = NO_PIECE;

    std::istringstream is(fen);
    std::string placement, side, castle, ep;
    int r50 = 0, fullmove = 1;
    is >> placement >> side >> castle >> ep;
    if (!(is >> r50)) r50 = 0;
    if (!(is >> fullmove)) fullmove = 1;

    auto fail = [&]() {
        if (fen != StartFEN) set(StartFEN);
        return false;
    };

    int file = 0, rank = 7;
    for (char c : placement) {
        if (c == '/') {
            if (file != 8) return fail();
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            const char* p = std::strchr(PieceChars, c);
            if (!p || file > 7 || rank < 0) return fail();
            put_piece(Piece(p - PieceChars), make_square(file, rank));
            ++file;
        }
        if (file > 8) return fail();
    }
    if (rank != 0 || file != 8) return fail();
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1) return fail();
    if (pieces(PAWN) & (RANK_1_BB | RANK_8_BB)) return fail();

    if (side == "w") stm = WHITE;
    else if (side == "b") stm = BLACK;
    else return fail();

    // Castling rights, sanitized against actual king/rook placement.
    s.castling = 0;
    if (castle != "-") {
        for (char c : castle) {
            switch (c) {
            case 'K': s.castling |= WHITE_OO; break;
            case 'Q': s.castling |= WHITE_OOO; break;
            case 'k': s.castling |= BLACK_OO; break;
            case 'q': s.castling |= BLACK_OOO; break;
            default: return fail();
            }
        }
    }
    if (board[SQ_E1] != W_KING) s.castling &= ~(WHITE_OO | WHITE_OOO);
    if (board[SQ_H1] != W_ROOK) s.castling &= ~WHITE_OO;
    if (board[SQ_A1] != W_ROOK) s.castling &= ~WHITE_OOO;
    if (board[SQ_E8] != B_KING) s.castling &= ~(BLACK_OO | BLACK_OOO);
    if (board[SQ_H8] != B_ROOK) s.castling &= ~BLACK_OO;
    if (board[SQ_A8] != B_ROOK) s.castling &= ~BLACK_OOO;

    for (int i = 0; i < 64; ++i) castling_mask[i] = ALL_CASTLING;
    castling_mask[SQ_E1] &= ~(WHITE_OO | WHITE_OOO);
    castling_mask[SQ_H1] &= ~WHITE_OO;
    castling_mask[SQ_A1] &= ~WHITE_OOO;
    castling_mask[SQ_E8] &= ~(BLACK_OO | BLACK_OOO);
    castling_mask[SQ_H8] &= ~BLACK_OO;
    castling_mask[SQ_A8] &= ~BLACK_OOO;

    // En passant: only kept if a capture is pseudo-legally possible (keeps hashes canonical).
    s.ep = SQ_NONE;
    if (ep != "-" && ep.size() == 2 && ep[0] >= 'a' && ep[0] <= 'h' && (ep[1] == '3' || ep[1] == '6')) {
        Square e = make_square(ep[0] - 'a', ep[1] - '1');
        if (relative_rank(stm, rank_of(e)) == 5 && ep_capturable(e, stm)) s.ep = e;
    }

    s.rule50 = r50;
    s.plies_from_null = 0;
    ply_count = std::max(2 * (fullmove - 1), 0) + (stm == BLACK);
    s.key = compute_key();
    s.pawn_key = compute_pawn_key();

    // The side that just moved must not be in check.
    if (attacked_by(stm, king_square(~stm), pieces())) return fail();
    update_check_info();
    return true;
}

bool Position::ep_capturable(Square ep, Color by) const {
    Color them = ~by;
    Square pushed = Square(ep + pawn_push(them));  // square the double-pushed pawn now stands on
    if (board[pushed] != make_piece(them, PAWN)) return false;
    if (!empty(ep)) return false;
    return pawn_attacks(them, ep) & pieces(by, PAWN);
}

Key Position::compute_key() const {
    Key k = 0;
    for (int s = 0; s < 64; ++s)
        if (board[s] != NO_PIECE) k ^= zobrist::PieceSquare[board[s]][s];
    k ^= zobrist::Castling[st().castling];
    if (st().ep != SQ_NONE) k ^= zobrist::EnPassant[file_of(st().ep)];
    if (stm == BLACK) k ^= zobrist::Side;
    return k;
}

Key Position::compute_pawn_key() const {
    Key k = 0;
    Bitboard b = pieces(PAWN);
    while (b) {
        Square s = pop_lsb(b);
        k ^= zobrist::PieceSquare[board[s]][s];
    }
    return k;
}

std::string Position::fen() const {
    std::string f;
    for (int r = 7; r >= 0; --r) {
        int empty_count = 0;
        for (int fl = 0; fl < 8; ++fl) {
            Piece p = board[make_square(fl, r)];
            if (p == NO_PIECE) {
                ++empty_count;
            } else {
                if (empty_count) f += char('0' + empty_count);
                empty_count = 0;
                f += PieceChars[p];
            }
        }
        if (empty_count) f += char('0' + empty_count);
        if (r) f += '/';
    }
    f += stm == WHITE ? " w " : " b ";
    std::string c;
    if (can_castle(WHITE_OO)) c += 'K';
    if (can_castle(WHITE_OOO)) c += 'Q';
    if (can_castle(BLACK_OO)) c += 'k';
    if (can_castle(BLACK_OOO)) c += 'q';
    f += c.empty() ? "-" : c;
    f += " " + square_to_string(st().ep);
    f += " " + std::to_string(st().rule50) + " " + std::to_string(1 + ply_count / 2);
    return f;
}

std::string Position::pretty() const {
    std::string s = "+---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) {
            Piece p = board[make_square(f, r)];
            s += "| ";
            s += p == NO_PIECE ? ' ' : PieceChars[p];
            s += ' ';
        }
        s += "| " + std::to_string(r + 1) + "\n+---+---+---+---+---+---+---+---+\n";
    }
    s += "  a   b   c   d   e   f   g   h\n\nFen: " + fen() + "\nKey: ";
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llX", (unsigned long long)key());
    s += buf;
    s += "\n";
    return s;
}

// ---------------------------------------------------------------- attacks

Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    return (pawn_attacks(BLACK, s) & pieces(WHITE, PAWN)) | (pawn_attacks(WHITE, s) & pieces(BLACK, PAWN)) |
           (knight_attacks(s) & pieces(KNIGHT)) | (rook_attacks(s, occ) & pieces(ROOK, QUEEN)) |
           (bishop_attacks(s, occ) & pieces(BISHOP, QUEEN)) | (king_attacks(s) & pieces(KING));
}

bool Position::attacked_by(Color c, Square s, Bitboard occ) const {
    return (pawn_attacks(~c, s) & pieces(c, PAWN)) || (knight_attacks(s) & pieces(c, KNIGHT)) ||
           (king_attacks(s) & pieces(c, KING)) || (rook_attacks(s, occ) & pieces(c, ROOK, QUEEN)) ||
           (bishop_attacks(s, occ) & pieces(c, BISHOP, QUEEN));
}

void Position::compute_blockers(Color c) {
    StateInfo& s = st();
    s.blockers[c] = 0;
    s.pinners[c] = 0;
    Square ksq = king_square(c);
    Bitboard snipers = ((rook_attacks(ksq, 0) & pieces(~c, ROOK, QUEEN)) |
                        (bishop_attacks(ksq, 0) & pieces(~c, BISHOP, QUEEN)));
    Bitboard occ = pieces() ^ snipers;
    while (snipers) {
        Square sn = pop_lsb(snipers);
        Bitboard b = between_bb(ksq, sn) & occ;
        if (b && !more_than_one(b)) {
            s.blockers[c] |= b;
            if (b & pieces(c)) s.pinners[c] |= square_bb(sn);
        }
    }
}

void Position::update_check_info() {
    StateInfo& s = st();
    s.checkers = attackers_to(king_square(stm)) & pieces(~stm);
    compute_blockers(WHITE);
    compute_blockers(BLACK);
    Square ksq = king_square(~stm);
    Bitboard occ = pieces();
    s.check_squares[PAWN] = pawn_attacks(~stm, ksq);
    s.check_squares[KNIGHT] = knight_attacks(ksq);
    s.check_squares[BISHOP] = bishop_attacks(ksq, occ);
    s.check_squares[ROOK] = rook_attacks(ksq, occ);
    s.check_squares[QUEEN] = s.check_squares[BISHOP] | s.check_squares[ROOK];
    s.check_squares[KING] = 0;
}

// ---------------------------------------------------------------- legality

bool Position::legal(Move m) const {
    Color us = stm, them = ~stm;
    Square from = m.from(), to = m.to();
    Square ksq = king_square(us);

    if (m.is_ep()) {
        Square cap = Square(to - pawn_push(us));
        Bitboard occ = (pieces() ^ square_bb(from) ^ square_bb(cap)) | square_bb(to);
        return !(attackers_to(ksq, occ) & pieces(them) & ~square_bb(cap));
    }

    if (m.is_castle()) {
        if (in_check()) return false;
        // King passes over f/g (OO) or d/c (OOO); b-file only needs to be empty.
        Square step1 = m.flag() == MF_KING_CASTLE ? relative_square(us, SQ_F1) : relative_square(us, SQ_D1);
        Square step2 = m.flag() == MF_KING_CASTLE ? relative_square(us, SQ_G1) : relative_square(us, SQ_C1);
        return !attacked_by(them, step1, pieces()) && !attacked_by(them, step2, pieces());
    }

    if (from == ksq) return !(attackers_to(to, pieces() ^ square_bb(from)) & pieces(them));

    Bitboard chk = checkers();
    if (chk) {
        if (more_than_one(chk)) return false;
        if (!(between_bb(ksq, lsb(chk)) & square_bb(to))) return false;
    }
    return !(pinned(us) & square_bb(from)) || aligned(from, to, ksq);
}

bool Position::pseudo_legal(Move m) const {
    if (m.is_none() || m == Move::null()) return false;
    Color us = stm, them = ~stm;
    Square from = m.from(), to = m.to();
    Piece p = board[from];
    if (p == NO_PIECE || color_of(p) != us) return false;
    if (pieces(us) & square_bb(to)) return false;
    PieceType pt = type_of(p);
    int flag = m.flag();
    if (flag == 6 || flag == 7) return false;  // unused encodings

    if (m.is_castle()) {
        if (pt != KING || from != relative_square(us, SQ_E1) || in_check()) return false;
        if (flag == MF_KING_CASTLE) {
            return to == relative_square(us, SQ_G1) && can_castle(us == WHITE ? WHITE_OO : BLACK_OO) &&
                   !(pieces() & (square_bb(relative_square(us, SQ_F1)) | square_bb(relative_square(us, SQ_G1))));
        }
        return to == relative_square(us, SQ_C1) && can_castle(us == WHITE ? WHITE_OOO : BLACK_OOO) &&
               !(pieces() & (square_bb(relative_square(us, SQ_D1)) | square_bb(relative_square(us, SQ_C1)) |
                             square_bb(relative_square(us, SQ_B1))));
    }

    if (pt == PAWN) {
        bool last_rank = relative_rank(us, rank_of(to)) == 7;
        if (m.is_promotion() != last_rank) return false;
        if (flag == MF_EP) return to == ep_square() && (pawn_attacks(us, from) & square_bb(to));
        if (m.is_capture())
            return (pawn_attacks(us, from) & pieces(them) & square_bb(to)) && type_of(board[to]) != KING;
        if (flag == MF_DOUBLE_PUSH)
            return relative_rank(us, rank_of(from)) == 1 && to == from + 2 * pawn_push(us) &&
                   empty(Square(from + pawn_push(us))) && empty(to);
        if (flag == MF_QUIET || m.is_promotion())  // quiet push or quiet promotion
            return to == from + pawn_push(us) && empty(to);
        return false;
    }

    if (flag == MF_CAPTURE) {
        if (!(pieces(them) & square_bb(to)) || type_of(board[to]) == KING) return false;
    } else if (flag == MF_QUIET) {
        if (!empty(to)) return false;
    } else {
        return false;
    }
    return attacks_bb(pt, from, pieces()) & square_bb(to);
}

bool Position::gives_check(Move m) const {
    Color us = stm, them = ~stm;
    Square from = m.from(), to = m.to();
    Square eksq = king_square(them);
    PieceType pt = type_of(board[from]);

    if (m.is_promotion() || m.is_ep() || m.is_castle()) {
        // Special moves: compute directly on the resulting occupancy.
        Bitboard occ = (pieces() ^ square_bb(from)) | square_bb(to);
        if (m.is_ep()) occ ^= square_bb(Square(to - pawn_push(us)));
        Square piece_sq = to;
        PieceType moved = m.is_promotion() ? m.promo_type() : pt;
        if (m.is_castle()) {
            Square rfrom = m.flag() == MF_KING_CASTLE ? relative_square(us, SQ_H1) : relative_square(us, SQ_A1);
            Square rto = m.flag() == MF_KING_CASTLE ? relative_square(us, SQ_F1) : relative_square(us, SQ_D1);
            occ = (pieces() ^ square_bb(from) ^ square_bb(rfrom)) | square_bb(to) | square_bb(rto);
            return rook_attacks(rto, occ) & square_bb(eksq);
        }
        Bitboard own = (pieces(us) ^ square_bb(from)) | square_bb(to);
        if (moved == PAWN) {
            if (pawn_attacks(us, piece_sq) & square_bb(eksq)) return true;
        } else if (attacks_bb(moved, piece_sq, occ) & square_bb(eksq)) {
            return true;
        }
        // Discovered checks through vacated squares
        own &= ~square_bb(to);
        return (rook_attacks(eksq, occ) & pieces(us, ROOK, QUEEN) & own) ||
               (bishop_attacks(eksq, occ) & pieces(us, BISHOP, QUEEN) & own);
    }

    if (st().check_squares[pt] & square_bb(to)) return true;
    if ((blockers_for_king(them) & square_bb(from)) && !aligned(from, to, eksq)) return true;
    return false;
}

// Static exchange evaluation: is the material balance of the exchange on m.to() >= threshold?
bool Position::see_ge(Move m, int threshold) const {
    static constexpr int SeeValue[PIECE_TYPE_NB] = {100, 300, 300, 500, 900, 0};
    if (m.is_castle() || m.is_ep() || m.is_promotion()) return 0 >= threshold;

    Square from = m.from(), to = m.to();
    int swap = (board[to] == NO_PIECE ? 0 : SeeValue[type_of(board[to])]) - threshold;
    if (swap < 0) return false;
    swap = SeeValue[type_of(board[from])] - swap;
    if (swap <= 0) return true;

    Bitboard occ = pieces() ^ square_bb(from) ^ square_bb(to);
    Color side = stm;
    Bitboard attackers = attackers_to(to, occ);
    int res = 1;

    while (true) {
        side = ~side;
        attackers &= occ;
        Bitboard side_att = attackers & pieces(side);
        if (!side_att) break;
        // Pinned pieces may not take part while their pinner is still on the board.
        if (st().pinners[side] & occ) side_att &= ~st().blockers[side];
        if (!side_att) break;
        res ^= 1;

        Bitboard bb;
        if ((bb = side_att & pieces(PAWN))) {
            if ((swap = SeeValue[PAWN] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & pieces(BISHOP, QUEEN);
        } else if ((bb = side_att & pieces(KNIGHT))) {
            if ((swap = SeeValue[KNIGHT] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
        } else if ((bb = side_att & pieces(BISHOP))) {
            if ((swap = SeeValue[BISHOP] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & pieces(BISHOP, QUEEN);
        } else if ((bb = side_att & pieces(ROOK))) {
            if ((swap = SeeValue[ROOK] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= rook_attacks(to, occ) & pieces(ROOK, QUEEN);
        } else if ((bb = side_att & pieces(QUEEN))) {
            if ((swap = SeeValue[QUEEN] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= (bishop_attacks(to, occ) & pieces(BISHOP, QUEEN)) |
                         (rook_attacks(to, occ) & pieces(ROOK, QUEEN));
        } else {
            // King: can only capture if the opponent has no attackers left.
            return (attackers & ~pieces(side)) ? res ^ 1 : res;
        }
    }
    return bool(res);
}

// ---------------------------------------------------------------- make / unmake

void Position::make_move(Move m) {
    states.push_back(states.back());
    StateInfo& s = st();
    Color us = stm, them = ~stm;
    Square from = m.from(), to = m.to();
    Piece p = board[from];
    Key k = s.key ^ zobrist::Side;

    s.rule50++;
    s.plies_from_null++;
    s.captured = NO_PIECE;
    if (s.ep != SQ_NONE) {
        k ^= zobrist::EnPassant[file_of(s.ep)];
        s.ep = SQ_NONE;
    }

    if (m.is_castle()) {
        bool oo = m.flag() == MF_KING_CASTLE;
        Square rfrom = relative_square(us, oo ? SQ_H1 : SQ_A1);
        Square rto = relative_square(us, oo ? SQ_F1 : SQ_D1);
        Piece rook = board[rfrom];
        move_piece(from, to);
        move_piece(rfrom, rto);
        k ^= zobrist::PieceSquare[p][from] ^ zobrist::PieceSquare[p][to] ^ zobrist::PieceSquare[rook][rfrom] ^
             zobrist::PieceSquare[rook][rto];
    } else {
        if (m.is_capture()) {
            Square cap = m.is_ep() ? Square(to - pawn_push(us)) : to;
            Piece captured = board[cap];
            s.captured = captured;
            remove_piece(cap);
            k ^= zobrist::PieceSquare[captured][cap];
            if (type_of(captured) == PAWN) s.pawn_key ^= zobrist::PieceSquare[captured][cap];
            s.rule50 = 0;
        }
        move_piece(from, to);
        k ^= zobrist::PieceSquare[p][from] ^ zobrist::PieceSquare[p][to];

        if (type_of(p) == PAWN) {
            s.rule50 = 0;
            s.pawn_key ^= zobrist::PieceSquare[p][from] ^ zobrist::PieceSquare[p][to];
            if (m.flag() == MF_DOUBLE_PUSH) {
                Square ep = Square(from + pawn_push(us));
                if (pawn_attacks(us, ep) & pieces(them, PAWN)) {
                    s.ep = ep;
                    k ^= zobrist::EnPassant[file_of(ep)];
                }
            } else if (m.is_promotion()) {
                Piece promo = make_piece(us, m.promo_type());
                remove_piece(to);
                put_piece(promo, to);
                k ^= zobrist::PieceSquare[p][to] ^ zobrist::PieceSquare[promo][to];
                s.pawn_key ^= zobrist::PieceSquare[p][to];
            }
        }
    }

    int new_castling = s.castling & castling_mask[from] & castling_mask[to];
    if (new_castling != s.castling) {
        k ^= zobrist::Castling[s.castling] ^ zobrist::Castling[new_castling];
        s.castling = new_castling;
    }

    s.key = k;
    stm = them;
    ++ply_count;
    update_check_info();
}

void Position::unmake_move(Move m) {
    stm = ~stm;
    Color us = stm;
    Square from = m.from(), to = m.to();
    Piece captured = st().captured;

    if (m.is_castle()) {
        bool oo = m.flag() == MF_KING_CASTLE;
        move_piece(to, from);
        move_piece(relative_square(us, oo ? SQ_F1 : SQ_D1), relative_square(us, oo ? SQ_H1 : SQ_A1));
    } else {
        if (m.is_promotion()) {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }
        move_piece(to, from);
        if (captured != NO_PIECE) put_piece(captured, m.is_ep() ? Square(to - pawn_push(us)) : to);
    }
    states.pop_back();
    --ply_count;
}

void Position::make_null() {
    states.push_back(states.back());
    StateInfo& s = st();
    Key k = s.key ^ zobrist::Side;
    if (s.ep != SQ_NONE) {
        k ^= zobrist::EnPassant[file_of(s.ep)];
        s.ep = SQ_NONE;
    }
    s.key = k;
    s.rule50++;
    s.plies_from_null = 0;
    s.captured = NO_PIECE;
    stm = ~stm;
    ++ply_count;
    update_check_info();
}

void Position::unmake_null() {
    stm = ~stm;
    states.pop_back();
    --ply_count;
}

// ---------------------------------------------------------------- draws

bool Position::is_repetition(int search_ply) const {
    const StateInfo& s = st();
    int end = std::min(s.rule50, s.plies_from_null);
    int n = int(states.size()) - 1;
    int reps = 0;
    for (int i = 4; i <= end && n - i >= 0; i += 2) {
        if (states[n - i].key == s.key) {
            if (i < search_ply) return true;  // repetition inside the search tree: treat as draw
            if (++reps >= 2) return true;     // threefold including positions before the root
        }
    }
    return false;
}

bool Position::is_fifty_move_draw() const { return st().rule50 >= 100; }

bool Position::insufficient_material() const {
    if (pieces(PAWN) | pieces(ROOK) | pieces(QUEEN)) return false;
    Bitboard minors = pieces(KNIGHT) | pieces(BISHOP);
    if (!more_than_one(minors)) return true;
    // Only bishops, all on the same square color.
    constexpr Bitboard DarkSquares = 0xAA55AA55AA55AA55ULL;
    if (!pieces(KNIGHT)) {
        Bitboard b = pieces(BISHOP);
        return !(b & DarkSquares) || !(b & ~DarkSquares);
    }
    return false;
}

// ---------------------------------------------------------------- UCI move strings

std::string Position::move_to_uci(Move m) {
    if (m.is_none()) return "0000";
    std::string s = square_to_string(m.from()) + square_to_string(m.to());
    if (m.is_promotion()) s += "nbrq"[m.promo_type() - KNIGHT];
    return s;
}

Move Position::parse_uci(const std::string& str) const {
    MoveList list;
    generate_legal(*this, list);
    for (Move m : list)
        if (move_to_uci(m) == str) return m;
    return Move::none();
}

// ---------------------------------------------------------------- debug

bool Position::is_consistent(std::string* why) const {
    auto bad = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    Bitboard all = 0;
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        if (all & by_type[pt]) return bad("type bitboards overlap");
        all |= by_type[pt];
    }
    if (all != pieces()) return bad("type/color union mismatch");
    if (by_color[WHITE] & by_color[BLACK]) return bad("color bitboards overlap");
    for (int s = 0; s < 64; ++s) {
        Piece p = board[s];
        if (p == NO_PIECE) {
            if (all & square_bb(Square(s))) return bad("bitboard has piece on empty mailbox square");
        } else if (!(pieces(color_of(p), type_of(p)) & square_bb(Square(s)))) {
            return bad("mailbox/bitboard mismatch");
        }
    }
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1) return bad("king count");
    if (compute_key() != key()) return bad("zobrist key mismatch");
    if (compute_pawn_key() != pawn_key()) return bad("pawn key mismatch");
    if (attacked_by(stm, king_square(~stm), pieces())) return bad("side not to move is in check");
    if ((attackers_to(king_square(stm)) & pieces(~stm)) != checkers()) return bad("checkers mismatch");
    return true;
}

}  // namespace apex
