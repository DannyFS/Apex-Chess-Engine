#include "uci.h"
#include "movegen.h"
#include "util/perft.h"
#include "util/bench.h"
#include "syzygy/tbprobe.h"
#include "nnue/nnue_loader.h"
#include <iostream>
#include <sstream>
#include <vector>

EngineOptions Options;

static Search Searcher;
static Position Pos;

static void print_uci_options() {
    std::cout << "id name ApexNNUE" << std::endl;
    std::cout << "id author OpenAI Codex" << std::endl;

    std::cout << "option name Threads type spin default 1 min 1 max 128" << std::endl;
    std::cout << "option name Hash type spin default 64 min 1 max 65536" << std::endl;
    std::cout << "option name MoveOverhead type spin default 30 min 0 max 1000" << std::endl;
    std::cout << "option name SyzygyPath type string default" << std::endl;
    std::cout << "option name SyzygyProbeDepth type spin default 1 min 1 max 64" << std::endl;
    std::cout << "option name SyzygyProbeLimit type spin default 5 min 3 max 7" << std::endl;
    std::cout << "option name EvalBackend type combo default CPU_NNUE var CPU_NNUE var GPU_NNUE" << std::endl;
    std::cout << "option name GPUDevice type spin default 0 min 0 max 16" << std::endl;
    std::cout << "option name GPUBatchSize type spin default 64 min 1 max 4096" << std::endl;
    std::cout << "option name NNUEFile type string default" << std::endl;
    std::cout << "option name UsePonder type check default false" << std::endl;
    std::cout << "option name MultiPV type spin default 1 min 1 max 10" << std::endl;
    std::cout << "option name DebugLog type check default false" << std::endl;

    std::cout << "uciok" << std::endl;
}

static void set_option(const std::string& name, const std::string& value) {
    if (name == "Threads") {
        Options.Threads = std::stoi(value);
        Searcher.set_threads(Options.Threads);
    } else if (name == "Hash") {
        Options.Hash = std::stoi(value);
        Searcher.set_hash(Options.Hash);
    } else if (name == "MoveOverhead") {
        Options.MoveOverhead = std::stoi(value);
    } else if (name == "SyzygyPath") {
        Options.SyzygyPath = value;
        Syzygy::init(value);
    } else if (name == "SyzygyProbeDepth") {
        Options.SyzygyProbeDepth = std::stoi(value);
    } else if (name == "SyzygyProbeLimit") {
        Options.SyzygyProbeLimit = std::stoi(value);
    } else if (name == "EvalBackend") {
        if (value == "GPU_NNUE") Options.EvalBackend = NNUE::Backend::GPU_NNUE; else Options.EvalBackend = NNUE::Backend::CPU_NNUE;
        NNUE::set_backend(Options.EvalBackend);
    } else if (name == "GPUDevice") {
        Options.GPUDevice = std::stoi(value);
        NNUE::set_gpu_config(Options.GPUDevice, Options.GPUBatchSize, Options.EvalBackend == NNUE::Backend::GPU_NNUE);
    } else if (name == "GPUBatchSize") {
        Options.GPUBatchSize = std::stoi(value);
        NNUE::set_gpu_config(Options.GPUDevice, Options.GPUBatchSize, Options.EvalBackend == NNUE::Backend::GPU_NNUE);
    } else if (name == "NNUEFile") {
        Options.NNUEFile = value;
        NNUE::load(value);
        NNUE::set_backend(Options.EvalBackend);
        NNUE::benchmark_backends();
        NNUE::refresh(Pos);
    } else if (name == "UsePonder") {
        Options.UsePonder = (value == "true");
    } else if (name == "MultiPV") {
        Options.MultiPV = std::stoi(value);
    } else if (name == "DebugLog") {
        Options.DebugLog = (value == "true");
    }
}

static void handle_position(std::istringstream& iss) {
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    if (toks.empty()) return;

    size_t idx = 0;
    if (toks[0] == "startpos") {
        Pos.set_startpos();
        if (NNUE::loaded()) NNUE::refresh(Pos);
        idx = 1;
    } else if (toks[0] == "fen") {
        if (toks.size() < 7) return;
        std::string fen = toks[1];
        for (int i = 2; i < 7; ++i) fen += " " + toks[i];
        Pos.set_fen(fen);
        if (NNUE::loaded()) NNUE::refresh(Pos);
        idx = 7;
    }

    if (idx < toks.size() && toks[idx] == "moves") idx++;
    while (idx < toks.size()) {
        std::string mv = toks[idx++];
        MoveList moves;
        generate_legal_moves(Pos, moves);
        for (int i = 0; i < moves.size; ++i) {
            if (move_to_uci(moves.moves[i]) == mv) {
                Pos.do_move(moves.moves[i]);
                break;
            }
        }
    }
}

static SearchLimits parse_go(std::istringstream& iss) {
    SearchLimits limits;
    std::string token;
    while (iss >> token) {
        if (token == "wtime") iss >> limits.wtime;
        else if (token == "btime") iss >> limits.btime;
        else if (token == "winc") iss >> limits.winc;
        else if (token == "binc") iss >> limits.binc;
        else if (token == "movestogo") iss >> limits.movestogo;
        else if (token == "depth") iss >> limits.depth;
        else if (token == "movetime") iss >> limits.movetime;
        else if (token == "infinite") limits.infinite = true;
        else if (token == "ponder") limits.ponder = true;
    }
    return limits;
}

void uci_loop() {
    Searcher.set_threads(Options.Threads);
    Searcher.set_hash(Options.Hash);
    Pos.set_startpos();

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "uci") {
            print_uci_options();
        } else if (token == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (token == "setoption") {
            std::string name, value, tmp;
            iss >> tmp; // name
            if (tmp != "name") continue;
            std::getline(iss, name);
            // parse name and value
            auto pos = name.find(" value ");
            if (pos != std::string::npos) {
                value = name.substr(pos + 7);
                name = name.substr(0, pos);
            }
            // trim
            if (!name.empty() && name[0] == ' ') name.erase(0, 1);
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);
            set_option(name, value);
        } else if (token == "ucinewgame") {
            Searcher.new_game();
            Pos.set_startpos();
            if (NNUE::loaded()) NNUE::refresh(Pos);
        } else if (token == "position") {
            handle_position(iss);
        } else if (token == "go") {
            SearchLimits limits = parse_go(iss);
            Searcher.go(Pos, limits);
            std::cout << "bestmove " << move_to_uci(Searcher.best_move()) << std::endl;
        } else if (token == "stop") {
            Searcher.stop();
        } else if (token == "ponderhit") {
            // treat as stop ponder
        } else if (token == "quit") {
            Searcher.stop();
            break;
        } else if (token == "perft") {
            int depth = 4;
            iss >> depth;
            u64 nodes = perft(Pos, depth);
            std::cout << "perft " << depth << " nodes " << nodes << std::endl;
        } else if (token == "bench") {
            run_bench(Searcher);
        }
    }
}
