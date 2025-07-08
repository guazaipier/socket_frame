#include "thread_pool.h"
#include "utils.h"


ThreadPool::ThreadPool(int thread_count) : m_stop(false) {
    m_threads.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i) {
        m_threads.emplace_back([this]{
            while (!m_stop) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cv.wait(lock, [this]{ return !m_tasks.empty() || m_stop; });
                    
                    if (m_stop || m_tasks.empty())
                        return;
                    
                    task = std::move(m_tasks.front());
                    m_tasks.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    if (!m_stop)
        stop();
    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.emplace(std::move(task));
    }
    m_cv.notify_one();
}

void ThreadPool::stop() {
    m_stop.store(true);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cv.notify_all();
    }
}
