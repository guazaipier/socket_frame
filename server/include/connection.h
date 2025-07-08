#pragma once

#include "thread_pool.h"
#include <unordered_map>
#include <memory>
#include <netinet/in.h>

class Session;
class Connection : public std::enable_shared_from_this<Connection> {
public:
    explicit Connection();
    ~Connection();

    // epoll 监听 client IO 事件
    void run();
    // 结束运行
    void stop();
    // 返回连接池当前状态
    bool isStopped() const { return m_stop.load(); }

    // manager 模块调用 <-- 当有新的 client 连接时
    void addSession(int sockfd);
    // session 模块调用 <-- 当有 client 断开连接时
    void removeSession(int sockfd); 
private:
    // 初始化 epollfd
    void init();
    // 向 m_sessions 和 epollfd 中添加 session
    void emplaceSession(int session_fd, sockaddr_in addr);
    // 从 m_sessions 和 epollfd 中删除 session
    void eraseSession(int session_fd);
    // 定时清理超时/闲置的 session
    void checkIdleSessions();
private: 
    // sessions_ stores all the sessions connected to this connection.
    std::unordered_map<int, std::unique_ptr<Session>> m_sessions;
    
    // 运行状态标志
    std::atomic_bool m_stop{false};
    // epollfd
    std::atomic_int m_epollfd;

    // 处理读写事件的线程
    std::vector<std::unique_ptr<ThreadPool>> m_threads;
    // 处理监听到的 IO 事件的线程池大小
    int m_threadNum{0};
    // 保护 m_sessions 池的互斥锁
    std::mutex m_session_mutex;
    // 处理超时闲置的 client
    std::thread m_check_thread;
};