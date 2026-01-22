#include "movegen.h"
#include <iostream>

int main() {
    Position pos;
    pos.set_startpos();
    MoveList list;
    generate_legal_moves(pos, list);
    if (list.size != 20) {
        std::cerr << "movegen failed: got " << list.size << std::endl;
        return 1;
    }
    std::cout << "movegen ok" << std::endl;
    return 0;
}
