#pragma once 

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

class ThreadPool {
public:
    ThreadPool(int thread_count);
    ~ThreadPool();

    void enqueue(std::function<void()> task);
    void stop();

private:
    std::vector<std::thread> m_threads;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic_bool m_stop;
};