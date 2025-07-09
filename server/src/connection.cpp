#include "connection.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <cstring>
#include <iostream>
#include <set>
#include "session.h"
#include "utils.h"

Connection::Connection() {
    init();
    m_threads.reserve(MAX_SESSION_THREADS);
    for (auto i = 0; i < MAX_SESSION_THREADS; ++i) {
        m_threads.emplace_back(std::make_unique<ThreadPool>(1));
    }
    m_check_thread = std::thread(&Connection::checkIdleSessions, this);
    m_accept_thread = std::thread(&Connection::run, this);

    std::cout << "connection created threadpool " << MAX_SESSION_THREADS << std::endl;
}

Connection::~Connection() {
    std::cout << "connection destroyed begin..." << std::endl;
    stop();
    if (m_epollfd != -1) {
        ::close(m_epollfd);
        m_epollfd = -1;
    }
    if (m_check_thread.joinable())
        m_check_thread.join();
    if (m_accept_thread.joinable())
        m_accept_thread.join();
    std::cout << "connection destroyed" << std::endl;
}

void Connection::run() {
    std::cout << "connection run enter..." << std::endl;
    epoll_event events[MAX_EVENTS];
    while (!m_stop) {
        int nfds = ::epoll_wait(m_epollfd, events, MAX_EVENTS, 10);
        if (nfds == -1) {
            if (errno == EINTR) {
                std::cout << "epoll_wait interrupted by signal" << std::endl;
            } else {
                std::cerr << "epoll_wait error: " << errno << std::strerror(errno) << std::endl;
            }
            break;
        } else if (nfds == 0) {
            continue;
        } else {
            for (int i = 0; i < nfds; i++) {
                int sockfd = events[i].data.fd;
                if (events[i].events & EPOLLIN) {
                    m_threads[sockfd % MAX_SESSION_THREADS]->enqueue([this, &sockfd]{
                        if (m_sessions.find(sockfd) != m_sessions.end())
                            m_sessions[sockfd]->recv();
                    });
                    // std::cout << "EPOLLIN on socket: " << sockfd << std::endl;
                } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                    removeSession(sockfd);
                    // std::cout << "EPOLLERR or EPOLLHUP on socket: " << sockfd << std::endl;
                }
            }            
        }

    }
    std::cout << "connection run exit." << std::endl;
}

void Connection::stop() {
    m_stop = true;
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        for (auto& sockfd : m_sessions) {
            removeSession(sockfd.first);
        }
    }
    std::this_thread::sleep_for(std::chrono::seconds(1)); // 等待 threadPool 把所有的 client 端关闭
    for (auto& thread : m_threads) {
        thread->stop();
    }
}

void Connection::addSession(int sockfd) {
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int session_fd = ::accept4(sockfd, (sockaddr*)(&addr), &len, SOCK_NONBLOCK);
    if (session_fd == -1) {
        std::cerr << "error accepting client connection: " << errno << " " << std::strerror(errno) << std::endl;
        return;
    }
    // 设置 keepalive
    {
        int enable = 1;
        ::setsockopt(session_fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
        // Linux特有参数
        int idle = 30;      // 30秒无活动开始探测
        int interval = 5;   // 探测间隔5秒
        int count = 3;      // 探测3次
        ::setsockopt(session_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        ::setsockopt(session_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
        ::setsockopt(session_fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
    }
    m_threads[session_fd % MAX_SESSION_THREADS]->enqueue([this, session_fd, addr] {
        emplaceSession(session_fd, addr);
    });
}

void Connection::removeSession(int sockfd) {
    // std::cout << "100_removesession: " << sockfd << std::endl;
    m_threads[sockfd % MAX_SESSION_THREADS]->enqueue([this, sockfd]{
        eraseSession(sockfd);
    });
    
}

void Connection::init() {
    m_epollfd = ::epoll_create1(0);
    if (m_epollfd == -1) {
        std::cerr << "error creating epoll instance: " << errno << " " << std::strerror(errno) << std::endl;
        m_stop = true;
        return;
    }
}

void Connection::emplaceSession(int session_fd, sockaddr_in addr) {
    epoll_event ev{.events = EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR, .data{.fd = session_fd}};
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (::epoll_ctl(m_epollfd, EPOLL_CTL_ADD, session_fd, &ev) == -1) {
            std::cerr << "error adding session to epoll: " << errno << " " << std::strerror(errno) << std::endl;
            return;
        }
        m_sessions.emplace(session_fd, std::make_unique<Session>(session_fd, weak_from_this(), &addr));
    }
    std::cout << "new session added: " << session_fd << " total sessions: " << m_sessions.size() << std::endl;
}

void Connection::eraseSession(int session_fd) {
    std::cout << std::this_thread::get_id() << " erasing session " << session_fd << std::endl;
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (m_sessions.find(session_fd) == m_sessions.end()) return;
        if (::epoll_ctl(m_epollfd, EPOLL_CTL_DEL, session_fd, nullptr) == -1 && errno != EBADF) {
            std::cerr << "error removing session from epoll: " << errno << " " << std::strerror(errno) << std::endl;
        }
        m_sessions[session_fd]->close();
        m_sessions.erase(session_fd);
    }
    std::cout << "session removed: " << session_fd << " total sessions: " << m_sessions.size() << std::endl;
}

void Connection::checkIdleSessions() {
    std::cout << "checkIdleSessions started..." << " thread id: " << std::this_thread::get_id() << std::endl;
    while (!m_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(IDLE_CHECK_INTERVAL));
        std::set<int> idle_fds;
        auto now = std::chrono::system_clock::now();
        {
            std::lock_guard<std::mutex> lock(m_session_mutex);
            for (auto& [id, sess] : m_sessions) {
                std::chrono::duration<double, std::milli> diff{now-sess->lastActive()};
                std::cout << "session " << id << " diff: " << diff.count() << std::endl;
                if (diff.count() >= IDLE_SESSION_TIMEOUT) {
                    idle_fds.insert(id);
                }
            }
        }
        for (auto id : idle_fds) {
            m_threads[id % MAX_SESSION_THREADS]->enqueue([this, id]{
                eraseSession(id);
            });
        }
    }
    std::cout << "checkIdleSessions exit." << " thread id: " << std::this_thread::get_id() << std::endl;
}
