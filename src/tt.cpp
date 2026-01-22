#include "tt.h"

void TranspositionTable::resize_mb(int mb) {
    if (mb < 1) mb = 1;
    size_t bytes = static_cast<size_t>(mb) * 1024ULL * 1024ULL;
    size_t clusters = bytes / sizeof(Cluster);
    if (clusters == 0) clusters = 1;
    table.clear();
    table.resize(clusters);
    clear();
}

void TranspositionTable::clear() {
    for (auto& c : table) {
        for (auto& e : c.entry) {
            e = TTEntry{};
        }
    }
}

TTEntry* TranspositionTable::probe(u64 key) {
    if (table.empty()) return nullptr;
    Cluster& c = table[key % table.size()];
    for (auto& e : c.entry) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

void TranspositionTable::store(u64 key, Move move, int depth, int score, int eval, Bound bound) {
    if (table.empty()) return;
    Cluster& c = table[key % table.size()];
    TTEntry* replace = &c.entry[0];
    for (auto& e : c.entry) {
        if (e.key == key) { replace = &e; break; }
        // choose entry with lowest depth or old generation
        if (e.gen != generation || e.depth < replace->depth) replace = &e;
    }
    replace->key = key;
    replace->move = move;
    replace->depth = static_cast<i16>(depth);
    replace->score = static_cast<i16>(score);
    replace->eval = static_cast<i16>(eval);
    replace->bound = bound;
    replace->gen = generation;
}

void TranspositionTable::new_generation() {
    generation++;
    if (generation == 0) generation = 1;
}

u64 TranspositionTable::hashfull() const {
    if (table.empty()) return 0;
    u64 cnt = 0;
    for (size_t i = 0; i < table.size() && i < 1000; ++i) {
        for (auto& e : table[i].entry) if (e.gen == generation) cnt++;
    }
    return (cnt * 1000) / (1000 * 4);
}
