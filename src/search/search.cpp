// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// M3 baseline search: iterative deepening, PVS negamax, transposition table,
// quiescence search, TT-move + MVV-LVA ordering, mate-distance pruning.
// Every further technique (NMP, LMR, RFP, histories, ...) is added in M5,
// one SPRT-tested change at a time; see EXPERIMENTS.md.
#include "search.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/movegen.h"
#include "eval/eval.h"

namespace apex {

namespace {

constexpr int OrderValue[PIECE_TYPE_NB + 1] = {1, 3, 3, 5, 9, 20, 0};

// Selection sort step: brings the best remaining move to index i.
inline Move pick(MoveList& list, int i) {
    int best = i;
    for (int j = i + 1; j < list.size(); ++j)
        if (list[j].score > list[best].score) best = j;
    std::swap(list[i], list[best]);
    return list[i].move;
}

bool has_legal_move(const Position& pos) {
    MoveList list;
    generate<GEN_ALL>(pos, list);
    for (Move m : list)
        if (pos.legal(m)) return true;
    return false;
}

}  // namespace

std::string score_to_uci(Value v) {
    if (v >= VALUE_MATE_IN_MAX_PLY) return "mate " + std::to_string((VALUE_MATE - v + 1) / 2);
    if (v <= VALUE_MATED_IN_MAX_PLY) return "mate " + std::to_string(-(VALUE_MATE + v) / 2);
    return "cp " + std::to_string(v);
}

namespace {
constexpr int HistoryMax = 16384;

// EXP-003: late move reduction table, R = 0.75 + ln(depth) * ln(moveNumber) / 2.25
struct ReductionTable {
    int r[64][64];
    ReductionTable() {
        for (int d = 0; d < 64; ++d)
            for (int m = 0; m < 64; ++m)
                r[d][m] = (d == 0 || m == 0) ? 0 : int(0.75 + std::log(d) * std::log(m) / 2.25);
    }
};
const ReductionTable Reductions;
inline int lmr(int depth, int move_number) { return Reductions.r[std::min(depth, 63)][std::min(move_number, 63)]; }
inline void apply_gravity(int& h, int bonus) {
    bonus = std::clamp(bonus, -HistoryMax, HistoryMax);
    h += bonus - h * std::abs(bonus) / HistoryMax;
}
}  // namespace

void Search::clear() {
    for (auto& k : killers) k[0] = k[1] = Move::none();
    std::fill(&history[0][0][0], &history[0][0][0] + sizeof(history) / sizeof(int), 0);
}

// Scores moves in-place: TT move, captures/promotions by MVV-LVA, killers, then quiets by history.
void Search::score_moves(const Position& pos, MoveList& list, Move tt_move, int ply) const {
    const Color us = pos.side_to_move();
    for (int i = 0; i < list.size(); ++i) {
        Move m = list[i].move;
        int s;
        if (m == tt_move) {
            s = 1 << 30;
        } else if (m.is_tactical()) {
            PieceType victim = m.is_ep() ? PAWN : (m.is_capture() ? type_of(pos.piece_on(m.to())) : NO_PIECE_TYPE);
            PieceType attacker = type_of(pos.piece_on(m.from()));
            s = (1 << 20) + 64 * OrderValue[victim] - OrderValue[attacker];
            if (m.is_promotion()) s += m.promo_type() == QUEEN ? 512 : -(1 << 19);
        } else if (m == killers[ply][0]) {
            s = (1 << 19) + 2;
        } else if (m == killers[ply][1]) {
            s = (1 << 19) + 1;
        } else {
            s = history[us][m.from()][m.to()];
        }
        list[i].score = s;
    }
}

void Search::update_quiet_stats(const Position& pos, Move best, const Move* quiets, int n_quiets, int depth,
                                int ply) {
    const Color us = pos.side_to_move();
    if (killers[ply][0] != best) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = best;
    }
    int bonus = std::min(16 * depth * depth + 32 * depth, 1200);
    apply_gravity(history[us][best.from()][best.to()], bonus);
    for (int i = 0; i < n_quiets; ++i)
        if (quiets[i] != best) apply_gravity(history[us][quiets[i].from()][quiets[i].to()], -bonus);
}

std::int64_t Search::elapsed_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void Search::setup_time(const Position& pos, const SearchLimits& limits) {
    Color us = pos.side_to_move();
    time_managed = false;
    soft_ms = hard_ms = 0;
    if (limits.movetime >= 0) {
        time_managed = true;
        soft_ms = hard_ms = std::max<std::int64_t>(1, limits.movetime - config.move_overhead_ms);
        return;
    }
    if (limits.time[us] < 0 || limits.infinite) return;

    time_managed = true;
    std::int64_t remaining = std::max<std::int64_t>(1, limits.time[us] - config.move_overhead_ms);
    int mtg = limits.movestogo > 0 ? std::min(limits.movestogo, 40) : 30;
    std::int64_t base = remaining / mtg + limits.inc[us] * 3 / 4;
    soft_ms = std::min(base, remaining / 2);
    hard_ms = std::min(remaining * 3 / 4, std::max(soft_ms, base * 4));
    soft_ms = std::max<std::int64_t>(1, soft_ms);
    hard_ms = std::max<std::int64_t>(1, hard_ms);
}

void Search::check_time() {
    if (lim.nodes && nodes >= lim.nodes) stop_flag.store(true, std::memory_order_relaxed);
    if ((nodes & 1023) != 0) return;
    if (time_managed && elapsed_ms() >= hard_ms) stop_flag.store(true, std::memory_order_relaxed);
}

void Search::print_info(int depth, Value score, const PV& pv) const {
    std::int64_t ms = elapsed_ms();
    std::uint64_t nps = ms > 0 ? nodes * 1000 / std::uint64_t(ms) : nodes * 1000;
    std::string s = "info depth " + std::to_string(depth) + " seldepth " + std::to_string(seldepth) + " score " +
                    score_to_uci(score) + " nodes " + std::to_string(nodes) + " nps " + std::to_string(nps) +
                    " hashfull " + std::to_string(tt.hashfull()) + " time " + std::to_string(ms) + " pv";
    for (int i = 0; i < pv.length; ++i) s += " " + Position::move_to_uci(pv.moves[i]);
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);
}

SearchResult Search::run(Position& pos, const SearchLimits& limits, bool silent) {
    start = Clock::now();
    lim = limits;
    stop_flag.store(false);
    nodes = 0;
    root_best = Move::none();
    setup_time(pos, limits);
    tt.new_search();

    SearchResult result;
    MoveList root_moves;
    generate_legal(pos, root_moves);
    if (root_moves.empty()) {
        result.score = pos.in_check() ? -VALUE_MATE : VALUE_DRAW;
        return result;
    }
    result.best = root_moves[0].move;  // always have a legal fallback

    int max_depth = limits.depth > 0 ? std::min(limits.depth, MAX_PLY - 1) : MAX_PLY - 1;
    for (int depth = 1; depth <= max_depth; ++depth) {
        seldepth = 0;
        root_best = Move::none();
        // EXP-005: aspiration windows around the previous iteration's score.
        Value score;
        Value delta = 20;
        Value alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
        if (depth >= 4 && result.score != VALUE_NONE && std::abs(result.score) < VALUE_MATE_IN_MAX_PLY) {
            alpha = std::max(result.score - delta, -VALUE_INFINITE);
            beta = std::min(result.score + delta, VALUE_INFINITE);
        }
        while (true) {
            score = negamax(pos, alpha, beta, depth, 0, true);
            if (stop_flag.load(std::memory_order_relaxed)) break;
            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(score - delta, -VALUE_INFINITE);
            } else if (score >= beta) {
                beta = std::min(score + delta, VALUE_INFINITE);
            } else {
                break;
            }
            delta += delta / 2;
            if (delta > 1000) {
                alpha = -VALUE_INFINITE;
                beta = VALUE_INFINITE;
            }
        }

        // Aborted iteration: root_best is only set by fully searched root moves, and the
        // previous best move is searched first, so a set root_best is safe to play.
        if (stop_flag.load(std::memory_order_relaxed) && depth > 1) {
            if (root_best && root_best != result.best) {
                result.best = root_best;
                result.ponder = Move::none();
            }
            break;
        }

        Move iter_best = pv_table[0].length > 0 ? pv_table[0].moves[0] : root_best;
        if (iter_best) result.best = iter_best;
        result.ponder = pv_table[0].length > 1 ? pv_table[0].moves[1] : Move::none();
        result.score = score;
        result.depth = depth;
        if (!silent) print_info(depth, score, pv_table[0]);

        if (stop_flag.load(std::memory_order_relaxed)) break;
        if (time_managed && limits.movetime < 0 && elapsed_ms() >= soft_ms) break;
        // A forced mate found well within the depth searched cannot be improved upon.
        if (!limits.infinite && limits.depth == 0 && std::abs(score) >= VALUE_MATE_IN_MAX_PLY &&
            VALUE_MATE - std::abs(score) <= depth)
            break;
    }
    result.nodes = nodes;
    return result;
}

Value Search::negamax(Position& pos, Value alpha, Value beta, int depth, int ply, bool pv_node) {
    pv_table[ply].length = 0;
    if (depth <= 0) return qsearch(pos, alpha, beta, ply, pv_node);

    ++nodes;
    check_time();
    if (stop_flag.load(std::memory_order_relaxed)) return 0;
    seldepth = std::max(seldepth, ply);

    const bool root = ply == 0;
    const bool in_check = pos.in_check();

    if (!root) {
        if (pos.is_repetition(ply) || pos.insufficient_material()) return VALUE_DRAW;
        if (pos.is_fifty_move_draw()) return in_check && !has_legal_move(pos) ? mated_in(ply) : VALUE_DRAW;
        if (ply >= MAX_PLY - 1) return in_check ? VALUE_DRAW : evaluate(pos);

        // Mate distance pruning
        alpha = std::max(alpha, mated_in(ply));
        beta = std::min(beta, mate_in(ply + 1));
        if (alpha >= beta) return alpha;
    }

    TTData tte{};
    bool tt_hit = tt.probe(pos.key(), tte);
    Move tt_move = tt_hit ? tte.move : Move::none();
    if (tt_hit) {
        Value tt_score = value_from_tt(tte.score, ply);
        if (!pv_node && tte.depth >= depth &&
            (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && tt_score >= beta) ||
             (tte.bound == BOUND_UPPER && tt_score <= alpha)))
            return tt_score;
    }
    if (tt_move && !pos.pseudo_legal(tt_move)) tt_move = Move::none();

    Value static_eval = in_check ? VALUE_NONE : evaluate(pos);

    // EXP-004: reverse futility pruning (static null move) at shallow depth.
    if (!pv_node && !in_check && !root && depth <= 8 && std::abs(beta) < VALUE_MATE_IN_MAX_PLY &&
        static_eval - 80 * depth >= beta)
        return static_eval;

    // EXP-001: null-move pruning. Skipped in PV nodes, in check, right after another
    // null move, and when the side to move has only pawns (zugzwang risk).
    if (!pv_node && !in_check && !root && depth >= 3 && static_eval >= beta && pos.plies_from_null() > 0 &&
        pos.non_pawn_material_count(pos.side_to_move()) > 0) {
        int R = 3 + depth / 3 + std::min((static_eval - beta) / 200, 3);
        pos.make_null();
        Value null_score = -negamax(pos, -beta, -beta + 1, depth - R, ply + 1, false);
        pos.unmake_null();
        if (stop_flag.load(std::memory_order_relaxed)) return 0;
        if (null_score >= beta) return null_score >= VALUE_MATE_IN_MAX_PLY ? beta : null_score;
    }

    MoveList list;
    generate<GEN_ALL>(pos, list);
    score_moves(pos, list, tt_move, ply);
    killers[ply + 1][0] = killers[ply + 1][1] = Move::none();

    Value best = -VALUE_INFINITE;
    Move quiets_tried[64];
    int n_quiets = 0;
    Move best_move = Move::none();
    const Value old_alpha = alpha;
    int legal = 0;

    for (int i = 0; i < list.size(); ++i) {
        Move m = pick(list, i);
        if (!pos.legal(m)) continue;
        ++legal;

        const bool quiet = !m.is_tactical();
        const bool is_killer = m == killers[ply][0] || m == killers[ply][1];
        const bool gives_check = pos.gives_check(m);
        const int new_depth = depth - 1;

        pos.make_move(m);
        Value score;
        if (legal == 1) {
            score = -negamax(pos, -beta, -alpha, new_depth, ply + 1, pv_node);
        } else {
            // EXP-003: reduce late quiet moves, verify at full depth if they beat alpha.
            int r = 0;
            if (depth >= 3 && quiet && !in_check && !gives_check && legal > 1 + int(pv_node)) {
                r = lmr(depth, legal);
                r -= int(pv_node);
                r -= int(is_killer);
                r = std::clamp(r, 0, new_depth - 1);
            }
            score = -negamax(pos, -alpha - 1, -alpha, new_depth - r, ply + 1, false);
            if (r > 0 && score > alpha)
                score = -negamax(pos, -alpha - 1, -alpha, new_depth, ply + 1, false);
            if (score > alpha && score < beta)
                score = -negamax(pos, -beta, -alpha, new_depth, ply + 1, true);
        }
        pos.unmake_move(m);

        if (stop_flag.load(std::memory_order_relaxed)) return 0;
        if (!m.is_tactical() && n_quiets < 64) quiets_tried[n_quiets++] = m;

        if (score > best) {
            best = score;
            if (score > alpha) {
                best_move = m;
                alpha = score;
                // Update the triangular PV
                PV& pv = pv_table[ply];
                const PV& child = pv_table[ply + 1];
                pv.moves[0] = m;
                pv.length = 1 + child.length;
                std::copy(child.moves, child.moves + child.length, pv.moves + 1);
                if (root) root_best = m;
                if (alpha >= beta) {
                    if (!m.is_tactical()) update_quiet_stats(pos, m, quiets_tried, n_quiets, depth, ply);
                    break;
                }
            }
        }
    }

    if (legal == 0) return in_check ? mated_in(ply) : VALUE_DRAW;

    Bound bound = best >= beta ? BOUND_LOWER : (alpha > old_alpha ? BOUND_EXACT : BOUND_UPPER);
    tt.store(pos.key(), best_move, value_to_tt(best, ply), static_eval == VALUE_NONE ? 0 : static_eval, depth, bound);
    return best;
}

Value Search::qsearch(Position& pos, Value alpha, Value beta, int ply, bool pv_node) {
    pv_table[ply].length = 0;
    ++nodes;
    check_time();
    if (stop_flag.load(std::memory_order_relaxed)) return 0;
    seldepth = std::max(seldepth, ply);

    if (pos.is_repetition(ply) || pos.insufficient_material()) return VALUE_DRAW;
    const bool in_check = pos.in_check();
    if (ply >= MAX_PLY - 1) return in_check ? VALUE_DRAW : evaluate(pos);

    TTData tte{};
    if (!pv_node && tt.probe(pos.key(), tte)) {
        Value tt_score = value_from_tt(tte.score, ply);
        if (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && tt_score >= beta) ||
            (tte.bound == BOUND_UPPER && tt_score <= alpha))
            return tt_score;
    }

    Value best = -VALUE_INFINITE;
    if (!in_check) {
        best = evaluate(pos);  // stand pat
        if (best >= beta) return best;
        alpha = std::max(alpha, best);
    }

    MoveList list;
    if (in_check) generate<GEN_ALL>(pos, list);  // evasions: all moves
    else generate<GEN_TACTICAL>(pos, list);
    score_moves(pos, list, Move::none(), ply);

    int legal = 0;
    for (int i = 0; i < list.size(); ++i) {
        Move m = pick(list, i);
        if (!pos.legal(m)) continue;
        ++legal;
        // Underpromotions are almost never needed in quiescence.
        if (!in_check && m.is_promotion() && m.promo_type() != QUEEN) continue;

        pos.make_move(m);
        Value score = -qsearch(pos, -beta, -alpha, ply + 1, pv_node);
        pos.unmake_move(m);
        if (stop_flag.load(std::memory_order_relaxed)) return 0;

        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                if (pv_node) {
                    PV& pv = pv_table[ply];
                    const PV& child = pv_table[ply + 1];
                    pv.moves[0] = m;
                    pv.length = 1 + child.length;
                    std::copy(child.moves, child.moves + child.length, pv.moves + 1);
                }
                if (alpha >= beta) break;
            }
        }
    }

    if (in_check && legal == 0) return mated_in(ply);
    return best;
}

}  // namespace apex
