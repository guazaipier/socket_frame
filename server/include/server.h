#pragma once

#include <atomic>
#include <vector>
#include <thread>
#include <memory>

class Connection;
class Server {
public:
    // 创建套接字
    Server(const int port);
    // 关闭套接字；结束所有线程
    ~Server();

    // 执行主程序，接收客户端连接
    void run();

private:
    // 初始化套接字
    void initSocket(const int port);
    // 停止服务
    void stop();

private:
    // 停止服务
    std::atomic_bool m_stop{false};

    // 服务端套接字
    int m_sockfd{-1};
    int m_epoll_fd{-1};

    // 连接池 -> accept 新的连接
    std::vector<std::shared_ptr<Connection>> m_connections;
    // 当前连接池 -> 轮询方式接受新连接
    int m_cur_thread{0};
};