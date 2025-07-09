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

Connection::Connection(int id) : m_id(id) {
    init();
    // m_threads.reserve(MAX_SESSION_THREADS);
    // for (auto i = 0; i < MAX_SESSION_THREADS; ++i) {
    //     m_threads.emplace_back(std::make_unique<ThreadPool>(1));
    // }
    m_check_thread = std::thread(&Connection::checkIdleSessions, this);
    m_accept_thread = std::thread(&Connection::run, this);
    m_handle_thread = std::thread(&Connection::handleEvents, this);
    std::cout << m_id << " connection created threads 3" << std::endl;
}

Connection::~Connection() {
    std::cout << m_id << " connection destroyed begin..." << std::endl;
    stop();
    if (m_epollfd != -1) {
        ::close(m_epollfd);
        m_epollfd = -1;
    }
    if (m_check_thread.joinable())
        m_check_thread.join();
    if (m_accept_thread.joinable())
        m_accept_thread.join();
    if (m_handle_thread.joinable())
        m_handle_thread.join();
    std::cout << m_id << " connection destroyed end." << std::endl;
}

void Connection::run() {
    std::cout << std::this_thread::get_id() << " connection run enter..." << std::endl;
    epoll_event events[MAX_EVENTS];
    while (!m_stop) {
        // std::cout << "epoll_wait begin..." << std::endl;
        int nfds = ::epoll_wait(m_epollfd, events, MAX_EVENTS, 10);
        if (nfds == -1) {
            break;
        } else if (nfds == 0) {
            continue;
        } else {
            for (int i = 0; i < nfds; ++i) {
                int sockfd = events[i].data.fd;
                if (events[i].events & EPOLLIN) {
                    // std::cout << "EPOLLIN: " << sockfd << std::endl;
                    read(sockfd);
                } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                    // std::cout << "EPOLLHUP or EPOLLERR: " << sockfd << std::endl;
                    removeSession(sockfd);
                }
            }            
        }
    }
    std::cout << std::this_thread::get_id() << " connection run exit." << std::endl;
}

void Connection::stop() {
    m_stop = true;
    m_events_cv.notify_one();
    {
        std::unique_lock lock(m_closed_mutex);
        for (auto& [sockfd, sess] : m_sessions) {
            m_closed_sessions.emplace(sockfd);
        }
    }
    std::this_thread::sleep_for(std::chrono::seconds(1)); // 等待 threadPool 把所有的 client 端关闭
    // for (auto& thread : m_threads) {
    //     thread->stop();
    // }
}

int Connection::getConnectionCount() { 
    std::shared_lock lock(m_session_mutex); 
    return m_sessions.size(); 
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
    // {
    //     int enable = 1;
    //     ::setsockopt(session_fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
    //     // Linux特有参数
    //     int idle = 30;      // 30秒无活动开始探测
    //     int interval = 5;   // 探测间隔5秒
    //     int count = 3;      // 探测3次
    //     ::setsockopt(session_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    //     ::setsockopt(session_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    //     ::setsockopt(session_fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
    // }
    // m_threads[session_fd % MAX_SESSION_THREADS]->enqueue([this, session_fd, addr] {
    //     emplaceSession(session_fd, addr);
    // });
    emplaceSession(session_fd, addr);
}

void Connection::removeSession(int sockfd) {
    // std::cout << "100_removesession: " << sockfd << std::endl;
    // m_threads[sockfd % MAX_SESSION_THREADS]->enqueue([this, sockfd]{
    //     eraseSession(sockfd);
    // });
    std::unique_lock lock(m_closed_mutex);
    m_closed_sessions.emplace(sockfd);
}

void Connection::handleEvents() {
    std::cout << m_id << " thread_id: " << std::this_thread::get_id() << " handleEvents started..." << std::endl;         
    while (!m_stop) {
        std::queue<std::pair<int, std::string>> data;
        {
            std::unique_lock<std::mutex> lock(m_events_mutex);
            m_events_cv.wait(lock, [this]{ return !m_events.empty() || m_stop; });
            if (m_stop || m_events.empty()) break;
            std::swap(data, m_events);
        }
        // std::cout << m_id << " handleEvents: " << data.size() << std::endl;
        while (!data.empty()) {
            std::pair<int, std::string> ev = data.front();
            data.pop();
            // std::cout << "1 handleEvents: " << ev.first << " " << ev.second.size() << std::endl;
            {
                // 检查 client 是否已经关闭
                std::shared_lock lock(m_closed_mutex);
                if (m_closed_sessions.find(ev.first) != m_closed_sessions.end()) continue;
            }
            // std::cout << "2 handleEvents: " << ev.first << " " << ev.second.size() << std::endl;
            {
                std::shared_lock lock(m_session_mutex);
                auto iter = m_sessions.find(ev.first);
                if (iter == m_sessions.end()) continue;
                // 处理数据
                iter->second->recv(std::move(ev.second));
            }
            // std::cout << "3 handleEvents: " << ev.first << " " << ev.second.size() << std::endl;
        }
    }
    std::cout << m_id << " thread_id: " << std::this_thread::get_id() << " handleEvents exit." << std::endl;
}

void Connection::read(int sockfd) {
    int n = 0;
    std::string data;
    while (!m_stop) {
        char buffer[MAX_BUFFER_SIZE];
        n = ::recv(sockfd, buffer, MAX_BUFFER_SIZE, 0);
        if (n <= 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
                removeSession(sockfd);
            break; // 读取完毕
        } else {
            data.append(buffer, n);
        }
    }
    // 放入数据缓冲区队列
    if (!data.empty()) {
        std::lock_guard<std::mutex> lock(m_events_mutex);
        m_events.emplace(sockfd, std::move(data));
        m_events_cv.notify_one();        
    }
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
        std::unique_lock lock(m_session_mutex);
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
        std::unique_lock lock(m_session_mutex);
        if (m_sessions.find(session_fd) == m_sessions.end()) return;
        ::epoll_ctl(m_epollfd, EPOLL_CTL_DEL, session_fd, nullptr);
        m_sessions[session_fd]->close();
        m_sessions.erase(session_fd);
    }
    std::cout << "session removed: " << session_fd << " total sessions: " << m_sessions.size() << " this_conn_id: "<< m_id << std::endl;
}

void Connection::checkIdleSessions() {
    std::cout << m_id << " thread_id: " << std::this_thread::get_id() << " checkIdleSessions started..." << std::endl;
    while (!m_stop) {
        // 1. remove closed sessions
        std::unordered_set<int> closed_fds;
        {
            std::unique_lock lock(m_closed_mutex);
            std::swap(closed_fds, m_closed_sessions);
        }
        for (auto id : closed_fds) {
            eraseSession(id);
        }
        // 2. remove timeout sessions
        std::this_thread::sleep_for(std::chrono::seconds(IDLE_CHECK_INTERVAL));
        std::set<int> idle_fds;
        auto now = std::chrono::system_clock::now();
        {
            std::shared_lock lock(m_session_mutex);
            for (auto& [id, sess] : m_sessions) {
                std::chrono::duration<double, std::milli> diff{now-sess->lastActive()};
                if (diff.count() >= IDLE_SESSION_TIMEOUT) {
                    idle_fds.insert(id);
                }
                // std::cout << "session " << id << " diff: " << diff.count() << std::endl;
            }
        }
        for (auto id : idle_fds) {
           eraseSession(id);
        }
    }
    std::cout << m_id << " thread_id: " << std::this_thread::get_id() << " checkIdleSessions exit." << std::endl;
}
