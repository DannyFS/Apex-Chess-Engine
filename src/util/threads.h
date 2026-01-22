#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

class ThreadPool {
public:
    ThreadPool() = default;
    ~ThreadPool() { stop(); }

    void start(int n, const std::function<void(int)>& fn);
    void stop();

    bool stop_requested() const { return stop_flag.load(std::memory_order_relaxed); }
    int size() const { return static_cast<int>(threads.size()); }

private:
    std::atomic<bool> stop_flag{false};
    std::vector<std::thread> threads;
};

int hardware_threads();
