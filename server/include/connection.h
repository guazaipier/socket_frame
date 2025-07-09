#pragma once

// #include "thread_pool.h"
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <netinet/in.h>
#include <shared_mutex>

class Session;
class Connection : public std::enable_shared_from_this<Connection> {
public:
    explicit Connection(int id);
    ~Connection();

    // epoll 监听 client IO 事件
    void run();
    // 结束运行
    void stop();
    // 返回连接池当前状态
    bool isStopped() const { return m_stop.load(); }
    // 返回连接数量
    int getConnectionCount();
    // 返回连接池 id
    int getId() const { return m_id.load(); }
    // manager 模块调用 <-- 当有新的 client 连接时
    void addSession(int sockfd);
    // session 模块调用 <-- 当有 client 断开连接时
    void removeSession(int sockfd); 
private:
    // 处理接收到的数据
    void handleEvents();
    void read(int sockfd);
    // 初始化 epollfd
    void init();
    // 向 m_sessions 和 epollfd 中添加 session
    void emplaceSession(int session_fd, sockaddr_in addr);
    // 从 m_sessions 和 epollfd 中删除 session
    void eraseSession(int session_fd);
    // 定时清理超时/闲置的 session
    void checkIdleSessions();
private: 
    // 保护 m_sessions 池的读写锁
    std::shared_mutex m_session_mutex;
    // sessions_ stores all the sessions connected to this connection.
    std::unordered_map<int, std::unique_ptr<Session>> m_sessions;
    
    // 监听连接池中已连接的 client event
    std::thread m_accept_thread;
    
    // 处理 IO 数据
    std::thread m_handle_thread;
    // 存放 IO 数据的队列
    std::queue<std::pair<int, std::string>> m_events;
    // 解决资源共享
    std::mutex m_events_mutex;
    std::condition_variable m_events_cv;
    
    // 运行状态标志
    std::atomic_bool m_stop{false};
    // epollfd
    std::atomic_int m_epollfd;
    // this id
    std::atomic_int m_id;

    // 处理读写事件的线程
    // std::vector<std::unique_ptr<ThreadPool>> m_threads;
    // 处理监听到的 IO 事件的线程池大小
    // int m_threadNum{0};
    // 保护即将关闭的 sessions
    std::shared_mutex m_closed_mutex;
    // 即将关闭的 sessions
    std::unordered_set<int> m_closed_sessions;
    // 处理超时闲置的 client
    std::thread m_check_thread;
};