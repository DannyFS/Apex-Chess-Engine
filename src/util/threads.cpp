#include "util/threads.h"

int hardware_threads() {
    unsigned int n = std::thread::hardware_concurrency();
    return n ? static_cast<int>(n) : 1;
}

void ThreadPool::start(int n, const std::function<void(int)>& fn) {
    stop();
    stop_flag.store(false, std::memory_order_relaxed);
    threads.reserve(n);
    for (int i = 0; i < n; ++i) {
        threads.emplace_back([this, fn, i]() {
            fn(i);
        });
    }
}

void ThreadPool::stop() {
    stop_flag.store(true, std::memory_order_relaxed);
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    threads.clear();
}
