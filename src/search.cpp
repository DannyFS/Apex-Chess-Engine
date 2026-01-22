#include "search.h"
#include "movegen.h"
#include "nnue/nnue_loader.h"
#include "syzygy/tbprobe.h"
#include "options.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>

static constexpr int INF = 32000;

static inline int piece_value(Piece p) {
    switch (piece_type(p)) {
        case PAWN: return 100;
        case KNIGHT: return 320;
        case BISHOP: return 330;
        case ROOK: return 500;
        case QUEEN: return 900;
        default: return 0;
    }
}

Searcher::Searcher(Search& parent, int id) : searcher_parent(parent), id(id) {}

bool Searcher::time_up() const {
    return searcher_parent.stop_flag.load(std::memory_order_relaxed) || searcher_parent.timeman().time_up();
}

int Searcher::static_eval(Position& pos) {
    return NNUE::evaluate(pos);
}

void Searcher::update_history(Color c, Move m, int depth) {
    if (!is_capture(m)) {
        int f = from_sq(m), t = to_sq(m);
        history[c][f][t] += depth * depth;
        if (history[c][f][t] > 1000000) history[c][f][t] /= 2;
    }
}

int Searcher::qsearch(Position& pos, int alpha, int beta, int ply) {
    if (time_up()) return 0;
    nodes++;

    int stand_pat = static_eval(pos);
    if (stand_pat >= beta) return stand_pat;
    if (stand_pat > alpha) alpha = stand_pat;
    MoveList moves;
    generate_pseudo_moves(pos, moves, false);

    for (int i = 0; i < moves.size; ++i) {
        Move m = moves.moves[i];
        if (!(is_capture(m) || is_promotion(m))) continue;
        pos.do_move(m);
        if (pos.in_check(~pos.side_to_move())) { pos.undo_move(); continue; }
        int score = -qsearch(pos, -beta, -alpha, ply + 1);
        pos.undo_move();
        if (score >= beta) return score;
        if (score > alpha) alpha = score;
        if (time_up()) break;
    }
    return alpha;
}

int Searcher::search_pvs(Position& pos, int depth, int alpha, int beta, int ply, bool pv) {
    if (time_up()) return 0;
    nodes++;

    bool in_check = pos.in_check(pos.side_to_move());
    int orig_alpha = alpha;

    if (pos.rule50_count() >= 100 || pos.is_repetition()) return 0;

    if (depth <= 0) return qsearch(pos, alpha, beta, ply);

    // Syzygy
    if (!in_check && Options.SyzygyProbeLimit > 0) {
        int pieces = std::popcount(pos.occupancy_all());
        if (pieces <= Options.SyzygyProbeLimit && depth <= Options.SyzygyProbeDepth) {
            bool ok = false;
            int wdl = Syzygy::probe_wdl(pos, ok);
            if (ok) {
                switch (wdl) {
                    case Syzygy::TB_WIN: return MATE_IN_MAX_PLY - ply;
                    case Syzygy::TB_LOSS: return -MATE_IN_MAX_PLY + ply;
                    default: return 0;
                }
            }
        }
    }

    TTEntry* tte = searcher_parent.tt().probe(pos.key());
    Move tt_move = MOVE_NONE;
    if (tte && tte->key == pos.key()) {
        tt_move = tte->move;
        int tt_score = score_from_tt(tte->score, ply);
        if (tte->depth >= depth) {
            if (tte->bound == BOUND_EXACT) return tt_score;
            if (tte->bound == BOUND_LOWER && tt_score >= beta) return tt_score;
            if (tte->bound == BOUND_UPPER && tt_score <= alpha) return tt_score;
        }
    }

    int eval = static_eval(pos);

    // Reverse futility
    if (!pv && !in_check && depth <= 3) {
        int margin = 150 * depth;
        if (eval - margin >= beta) return eval;
    }

    // Null move pruning
    if (!pv && !in_check && depth >= 3 && eval >= beta) {
        pos.do_null_move();
        int r = 2 + depth / 4;
        int score = -search_pvs(pos, depth - 1 - r, -beta, -beta + 1, ply + 1, false);
        pos.undo_null_move();
        if (score >= beta) return score;
    }

    MoveList moves;
    generate_pseudo_moves(pos, moves, false);

    // Order moves
    struct ScoredMove { Move m; int score; };
    std::vector<ScoredMove> ordered;
    ordered.reserve(moves.size);

    for (int i = 0; i < moves.size; ++i) {
        Move m = moves.moves[i];
        int score = 0;
        if (m == tt_move) score = 1000000;
        else if (is_capture(m)) {
            Piece victim = pos.piece_at(to_sq(m));
            Piece attacker = pos.piece_at(from_sq(m));
            score = 500000 + piece_value(victim) - piece_value(attacker);
        } else if (m == killers[0][ply]) score = 400000;
        else if (m == killers[1][ply]) score = 399000;
        else score = history[pos.side_to_move()][from_sq(m)][to_sq(m)];
        ordered.push_back({m, score});
    }
    std::sort(ordered.begin(), ordered.end(), [](const ScoredMove& a, const ScoredMove& b){ return a.score > b.score; });

    int best = -INF;
    Move best_move_local = MOVE_NONE;
    int moves_searched = 0;

    for (auto& sm : ordered) {
        Move m = sm.m;
        pos.do_move(m);
        if (pos.in_check(~pos.side_to_move())) { pos.undo_move(); continue; }

        int ext = 0;
        if (pos.in_check(pos.side_to_move())) ext = 1; // check extension

        int new_depth = depth - 1 + ext;
        int score = 0;

        if (moves_searched == 0) {
            score = -search_pvs(pos, new_depth, -beta, -alpha, ply + 1, pv);
        } else {
            int r = 0;
            if (!is_capture(m) && new_depth > 2 && moves_searched >= 3) r = 1 + (moves_searched >= 6) + (new_depth >= 6);
            score = -search_pvs(pos, new_depth - r, -alpha - 1, -alpha, ply + 1, false);
            if (score > alpha && r > 0) {
                score = -search_pvs(pos, new_depth, -alpha - 1, -alpha, ply + 1, false);
            }
            if (score > alpha && score < beta) {
                score = -search_pvs(pos, new_depth, -beta, -alpha, ply + 1, true);
            }
        }

        pos.undo_move();
        moves_searched++;

        if (score > best) {
            best = score;
            best_move_local = m;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) {
                    if (!is_capture(m)) {
                        killers[1][ply] = killers[0][ply];
                        killers[0][ply] = m;
                        update_history(pos.side_to_move(), m, depth);
                    }
                    break;
                }
            }
        }

        if (time_up()) break;
    }

    if (moves_searched == 0) {
        return in_check ? mated_in(ply) : 0;
    }

    Bound bound = (best <= orig_alpha) ? BOUND_UPPER : (best >= beta ? BOUND_LOWER : BOUND_EXACT);
    searcher_parent.tt().store(pos.key(), best_move_local, depth, score_to_tt(best, ply), eval, bound);

    return best;
}

void Searcher::search(Position root, const SearchLimits& limits, bool is_main) {
    nodes = 0;
    bestmove = MOVE_NONE;
    bestscore = 0;
    auto start = std::chrono::steady_clock::now();

    int max_depth = limits.depth > 0 ? limits.depth : MAX_PLY - 1;
    int score = 0;

    if (is_main) {
        searcher_parent.timeman().init(limits, root.side_to_move(), Options.MoveOverhead);
    }

    for (int depth = 1; depth <= max_depth; ++depth) {
        if (time_up()) break;

        int window = 50;
        int alpha = score - window;
        int beta = score + window;
        int best = 0;

        while (true) {
            int alpha0 = alpha;
            int beta0 = beta;
            MoveList root_moves;
            generate_pseudo_moves(root, root_moves, false);
            std::vector<std::pair<Move,int>> scored;
            scored.reserve(root_moves.size);

            Move tt_move = MOVE_NONE;
            TTEntry* tte = searcher_parent.tt().probe(root.key());
            if (tte && tte->key == root.key()) tt_move = tte->move;

            for (int i = 0; i < root_moves.size; ++i) {
                Move m = root_moves.moves[i];
                int s = (m == tt_move) ? 1000000 : 0;
                if (is_capture(m)) {
                    Piece victim = root.piece_at(to_sq(m));
                    Piece attacker = root.piece_at(from_sq(m));
                    s += 500000 + piece_value(victim) - piece_value(attacker);
                }
                scored.push_back({m, s});
            }
            std::sort(scored.begin(), scored.end(), [](auto& a, auto& b){ return a.second > b.second; });

            best = -INF;
            Move best_local = MOVE_NONE;
            int legal = 0;

            for (size_t i = 0; i < scored.size(); ++i) {
                Move m = scored[i].first;
                root.do_move(m);
                if (root.in_check(~root.side_to_move())) { root.undo_move(); continue; }
                legal++;
                int s = -search_pvs(root, depth - 1, -beta, -alpha, 1, true);
                root.undo_move();
                if (time_up()) break;
                if (s > best) {
                    best = s;
                    best_local = m;
                    if (s > alpha) alpha = s;
                }
            }

            if (legal == 0) {
                best = root.in_check(root.side_to_move()) ? mated_in(1) : 0;
                best_local = MOVE_NONE;
            }

            if (best <= alpha0) {
                alpha = alpha0 - window;
                beta = alpha0 + window;
                window *= 2;
                continue;
            }
            if (best >= beta0) {
                alpha = beta0 - window;
                beta = beta0 + window;
                window *= 2;
                continue;
            }

            bestmove = best_local;
            bestscore = best;
            break;
        }

        if (is_main) {
            auto now = std::chrono::steady_clock::now();
            int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            u64 nps = 0;
            if (elapsed > 0) nps = (u64)(nodes * 1000 / elapsed);
            std::cout << "info depth " << depth << " ";
            if (std::abs(bestscore) >= MATE_IN_MAX_PLY - MAX_PLY) {
                int mate = (bestscore > 0) ? (MATE_SCORE - bestscore + 1) / 2 : -(MATE_SCORE + bestscore) / 2;
                std::cout << "score mate " << mate;
            } else {
                std::cout << "score cp " << bestscore;
            }
            std::cout << " nodes " << nodes << " nps " << nps
                      << " pv " << move_to_uci(bestmove) << std::endl;
        }

        if (time_up()) break;
        score = bestscore;
        searcher_parent.update_best(bestmove, bestscore);
    }

    searcher_parent.update_best(bestmove, bestscore);
}

Search::Search() {
    set_threads(1);
    set_hash(64);
}

void Search::set_threads(int n) {
    if (n < 1) n = 1;
    thread_count = n;
    searchers.clear();
    for (int i = 0; i < thread_count; ++i) {
        searchers.emplace_back(std::make_unique<Searcher>(*this, i));
    }
}

void Search::set_hash(int mb) {
    table.resize_mb(mb);
}

void Search::new_game() {
    table.clear();
}

void Search::go(Position& pos, const SearchLimits& limits) {
    stop_flag.store(false, std::memory_order_relaxed);
    table.new_generation();

    if (thread_count > 1) {
        pool.start(thread_count - 1, [this, pos, limits](int id) mutable {
            searchers[id + 1]->search(pos, limits, false);
        });
    }

    Position root = pos;
    searchers[0]->search(root, limits, true);

    stop_flag.store(true, std::memory_order_relaxed);
    pool.stop();
}

void Search::stop() {
    stop_flag.store(true, std::memory_order_relaxed);
}
