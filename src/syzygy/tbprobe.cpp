#include "syzygy/tbprobe.h"
#include "syzygy/tbprobe_fathom.h"
#include "position.h"

namespace Syzygy {

static bool initialized = false;

bool init(const std::string& path) {
    if (path.empty()) return false;
    initialized = tb_init(path.c_str());
    return initialized;
}

void free() {
    if (initialized) tb_free();
    initialized = false;
}

static void build_bitboards(const Position& pos,
                            u64& white, u64& black,
                            u64& kings, u64& queens, u64& rooks,
                            u64& bishops, u64& knights, u64& pawns) {
    white = pos.occupancy(WHITE);
    black = pos.occupancy(BLACK);
    kings = pos.pieces_bb(W_KING) | pos.pieces_bb(B_KING);
    queens = pos.pieces_bb(W_QUEEN) | pos.pieces_bb(B_QUEEN);
    rooks = pos.pieces_bb(W_ROOK) | pos.pieces_bb(B_ROOK);
    bishops = pos.pieces_bb(W_BISHOP) | pos.pieces_bb(B_BISHOP);
    knights = pos.pieces_bb(W_KNIGHT) | pos.pieces_bb(B_KNIGHT);
    pawns = pos.pieces_bb(W_PAWN) | pos.pieces_bb(B_PAWN);
}

int probe_wdl(const Position& pos, bool& success) {
    success = false;
    if (!initialized) return 0;
    if (pos.castling_rights() != 0) return 0; // Syzygy doesn't handle castling

    u64 white, black, kings, queens, rooks, bishops, knights, pawns;
    build_bitboards(pos, white, black, kings, queens, rooks, bishops, knights, pawns);

    unsigned ep = pos.ep_square() == -1 ? 0 : (unsigned)pos.ep_square();
    unsigned res = tb_probe_wdl(white, black, kings, queens, rooks, bishops, knights, pawns,
                                0, 0, ep, pos.side_to_move() == WHITE);
    if (res == TB_RESULT_FAILED) return 0;
    success = true;
    return TB_GET_WDL(res);
}

int probe_root(const Position& pos, Move& best_move, int& wdl, int& dtz) {
    best_move = MOVE_NONE;
    wdl = 0; dtz = 0;
    if (!initialized) return 0;
    if (pos.castling_rights() != 0) return 0;

    u64 white, black, kings, queens, rooks, bishops, knights, pawns;
    build_bitboards(pos, white, black, kings, queens, rooks, bishops, knights, pawns);

    unsigned ep = pos.ep_square() == -1 ? 0 : (unsigned)pos.ep_square();
    TbRootMoves results{};
    int ok = tb_probe_root_dtz(white, black, kings, queens, rooks, bishops, knights, pawns,
                               pos.rule50_count(), 0, ep, pos.side_to_move() == WHITE,
                               pos.is_repetition(), true, &results);
    if (!ok) {
        ok = tb_probe_root_wdl(white, black, kings, queens, rooks, bishops, knights, pawns,
                               pos.rule50_count(), 0, ep, pos.side_to_move() == WHITE, true, &results);
    }
    if (!ok || results.size == 0) return 0;

    int best = 0;
    for (unsigned i = 1; i < results.size; ++i) {
        if (results.moves[i].tbRank > results.moves[best].tbRank) best = i;
    }

    TbMove tbm = results.moves[best].move;
    int from = TB_MOVE_FROM(tbm);
    int to = TB_MOVE_TO(tbm);
    int prom = TB_MOVE_PROMOTES(tbm);

    u32 flags = 0;
    Piece p = pos.piece_at(from);
    if (pos.piece_at(to) != NO_PIECE) flags |= MF_CAPTURE;
    if (piece_type(p) == KING && (to == from + 2 || to == from - 2)) flags |= MF_CASTLING;
    if (piece_type(p) == PAWN && pos.ep_square() == to && pos.piece_at(to) == NO_PIECE) flags |= MF_ENPASSANT | MF_CAPTURE;

    PieceType promo = PIECE_TYPE_NB;
    if (prom == TB_PROMOTES_QUEEN) promo = QUEEN;
    else if (prom == TB_PROMOTES_ROOK) promo = ROOK;
    else if (prom == TB_PROMOTES_BISHOP) promo = BISHOP;
    else if (prom == TB_PROMOTES_KNIGHT) promo = KNIGHT;
    if (promo != PIECE_TYPE_NB) flags |= MF_PROMOTION;

    best_move = make_move(from, to, promo, flags);
    wdl = results.moves[best].tbRank;
    dtz = results.moves[best].tbScore;
    return 1;
}

} // namespace Syzygy
