#include "util/perft.h"
#include "movegen.h"

u64 perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generate_legal_moves(pos, list);
    if (depth == 1) return list.size;

    u64 nodes = 0;
    for (int i = 0; i < list.size; ++i) {
        Move m = list.moves[i];
        pos.do_move(m);
        nodes += perft(pos, depth - 1);
        pos.undo_move();
    }
    return nodes;
}
