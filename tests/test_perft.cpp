#include "util/perft.h"
#include <iostream>

int main() {
    Position pos;
    pos.set_startpos();
    u64 nodes = perft(pos, 4);
    if (nodes != 197281) {
        std::cerr << "perft failed: got " << nodes << std::endl;
        return 1;
    }
    std::cout << "perft ok" << std::endl;
    return 0;
}
