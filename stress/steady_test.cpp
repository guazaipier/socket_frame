#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <cstring>
#include <signal.h>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <cassert>

const char request[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

void setnonblocking(int& fd) {
    int flag = ::fcntl(fd, F_GETFL);
    ::fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

void addfd(int& epoll_fd, int& fd) {
    epoll_event ev{.events= EPOLLOUT | EPOLLET | EPOLLERR, .data{.fd = fd}};
    ::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    setnonblocking(fd);
}

bool writeOnce(int sockfd, const char* buffer, int len) {
    int bytes_sent = 0;
    std::cout << "write out " << len << " bytes to socket " << sockfd << std::endl;
    while (true) {
        bytes_sent = ::send(sockfd, buffer, len, 0);
        if (bytes_sent == -1) return false;
        if (bytes_sent == 0) return false;
        len -= bytes_sent;
        buffer += bytes_sent;
        if (len <= 0) return true;
    }
}

bool readOnce(int sockfd, char* buffer, int len) {
    int bytes_read = 0;
    // ::memset(buffer, '\0', len);
    bytes_read = ::recv(sockfd, buffer, len, 0);
    if (bytes_read == -1) return false;
    if (bytes_read == 0) return false;
    buffer[bytes_read] = '\0';
    std::cout << "read " << bytes_read << " bytes from socket " << sockfd << "content: " << buffer << std::endl;
    return true;
}

void startConn(int& epoll_fd, int num, const char* ip, int port) {
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(port), .sin_addr{.s_addr = inet_addr(ip)}};
    for (int i = 0; i < num; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
        // std::cout << "create socket " << sockfd << std::endl;
        if (sockfd == -1) continue;
        if (::connect(sockfd, (sockaddr*)&addr, sizeof(addr)) == 0) {
            // std::cout << "build connection " << i << std::endl;
            addfd(epoll_fd, sockfd);
        }
    }
}

void closeConn(int& epoll_fd, int sockfd) {
    ::shutdown(sockfd, SHUT_RDWR);
    ::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, nullptr);
    ::close(sockfd);
}

int main(int argc, char* argv[]) {
    assert(argc >= 4);
    int epoll_fd = ::epoll_create1(0);
    startConn(epoll_fd, std::atoi(argv[1]), argv[2], std::atoi(argv[3]));
    epoll_event events[10000];
    char buffer[2048];
    int period = (argc == 5 ? std::atoi(argv[4]) : 1000);
    std::atomic_bool stop(false);
    std::thread timer([&stop, period]{
        auto start_time = std::chrono::steady_clock::now();
        auto end_time = start_time + std::chrono::seconds(period);
        while (std::chrono::steady_clock::now() < end_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        stop = true;
    });
    while (!stop) {
        int fds = ::epoll_wait(epoll_fd, events, 10000, 20);
        for (int i = 0; i < fds; ++i) {
            int sockfd = events[i].data.fd;
            if (events[i].events & EPOLLIN) {
                if (!readOnce(sockfd, buffer, sizeof(buffer))) {
                    closeConn(epoll_fd, sockfd);
                    continue;
                }
                epoll_event ev{.events = EPOLLOUT | EPOLLET | EPOLLERR, .data{.fd = sockfd}};
                ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sockfd, &ev);
            } else if (events[i].events & EPOLLOUT) {
                if (!writeOnce(sockfd, request, strlen(request))) {
                    closeConn(epoll_fd, sockfd);
                    continue;
                }
                epoll_event ev{.events = EPOLLIN | EPOLLET | EPOLLERR, .data{.fd = sockfd}};
                ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sockfd, &ev);
            } else if (events[i].events & EPOLLERR) {
                closeConn(epoll_fd, sockfd);
            }
        }
    }
}

