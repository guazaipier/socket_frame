#include "server.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <cstring>
#include <signal.h>

#include "utils.h"
#include "connection.h"

// 忽略信号
void signalDisable() {
    signal(SIGPIPE, SIG_IGN);
}

Server::Server(const int port) {
    initSocket(port);
    
    // 创建管理 client IO 的对象 和 监听已连接的 client 线程
    m_connections.reserve(MAX_CONNECT_THREADS);
    for (int i = 0; i < MAX_CONNECT_THREADS; ++i) {
        auto conn(std::make_shared<Connection>(i));
        if (conn->isStopped()) {
            std::cerr << "exit for not create connection" << std::endl;
            exit(1);
        }
        m_connections.emplace_back(conn);
    }

    signalDisable();
    std::cout << "server construct connection " << MAX_CONNECT_THREADS << " threads." << std::endl;
}

Server::~Server() {
    std::cout << "server ~destruct begin..." << std::endl;
    for (auto& conn : m_connections) {
        conn->stop();
    }
    stop();
    std::cout << "server ~destruct exit." << std::endl;
}

void Server::run() {
    std::cout << "server running..." << std::endl;
    std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
    while (!m_stop) {
        std::chrono::steady_clock::time_point now_time = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now_time - last_time).count() >= CHECK_STATE_INTERVAL) {
            int total_conns = 0;
            for (auto& connections : m_connections) {
                int conn = connections->getConnectionCount();
                total_conns += conn;
                std::cout << "connection " << connections->getId() << " count: " << conn << std::endl;
            }
            std::cout << "total connection count: " << total_conns << std::endl;
            last_time = now_time;
        }
        epoll_event events[1024];
        int nfds = ::epoll_wait(m_epoll_fd, events, 1024, 10);
        if (nfds == -1) {
            if (errno == EINTR) {
                std::cout << "epoll_wait interrupted by signal EINTR" << std::endl;
            } else {
                std::cerr << "epoll_wait error: " << errno << std::strerror(errno) << std::endl;
            }
            break;
        } else if (nfds == 0) {
            continue;
        } else {
            for (int i = 0; i < nfds; ++i) {
                if (events[i].data.fd == m_sockfd) {
                    // 接受新连接
                    m_connections[(++m_cur_thread) % MAX_CONNECT_THREADS]->addSession(m_sockfd);
                }
            }
        }
    }
    std::cout << "server running exit." << std::endl;
}

void Server::stop() {
    std::cout << "server stop begin..." << std::endl;
    m_stop = true;
    if (m_epoll_fd != -1) {
        ::close(m_epoll_fd);
        m_epoll_fd = -1;
    }
    if (m_sockfd != -1) {
        ::shutdown(m_sockfd, SHUT_RDWR);
        ::close(m_sockfd);
        m_sockfd = -1;
    }
    std::cout << "server stop exit." << std::endl;
}

void Server::initSocket(const int port) {
    //创建侦听socket  
    m_sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd == -1) {
        std::cerr << "failed to create socket: " << errno << " " << std::strerror(errno) << std::endl;
        exit(1);
    }
    //绑定侦听地址  
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(m_sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "failed to bind socket: " << errno << " " << std::strerror(errno) << std::endl;
        stop();
        exit(1);
    }
    // listen
    if (::listen(m_sockfd, 1024) == -1) {
        std::cerr << "failed to listen: " << errno << " " << std::strerror(errno) << std::endl; 
        stop();
        exit(1);
    }
    // 设置套接字为非阻塞  
    {
        int flags = ::fcntl(m_sockfd, F_GETFL, 0);
        if (flags == -1) {
            std::cerr << "failed to get socket flags: " << errno << " " << std::strerror(errno) << std::endl;
            stop();
            exit(1);
        }
        if (::fcntl(m_sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
            std::cerr << "failed to set socket nonblocking: " << errno << " " << std::strerror(errno) << std::endl;
            stop();
            exit(1);
        }
    }
    // 设置地址重用
    {
        int reuse = 1;
        if (::setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
            std::cerr << "failed to set socket reuseaddr: " << errno << " " << std::strerror(errno) << std::endl;
            stop();
            exit(1);
        }
        if (::setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1) {
            std::cerr << "failed to set socket reuseport: " << errno << " " << std::strerror(errno) << std::endl;
            stop();
            exit(1);
        }
    }
    // 设置 epoll
    m_epoll_fd = epoll_create1(0);
    if (m_epoll_fd == -1) {
        std::cerr << "failed to create epoll: " << errno << " " << std::strerror(errno) << std::endl;
        stop();
        exit(1);
    }
    epoll_event ev{.events = EPOLLIN | EPOLLET, .data{.fd = m_sockfd}};
    if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_sockfd, &ev) == -1) {
        std::cerr << "failed to add socket to epoll: " << errno << " " << std::strerror(errno) << std::endl;
        stop();
        exit(1);
    } 
}