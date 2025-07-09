#include "session.h"
#include "utils.h"
#include "connection.h"
#include <sys/socket.h>
#include <string>
#include <cstring>
#include <iostream>
#include <errno.h>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

Session::Session(int id, std::weak_ptr<Connection> conn, sockaddr_in* addr) : m_sockfd(id), m_conn(conn) {
    m_sockaddr.sin_addr = addr->sin_addr;
    m_sockaddr.sin_port = addr->sin_port;
    m_last_active_tp = std::chrono::system_clock::now();
}

Session::~Session() {
    close();
}

bool Session::recv(std::string&& data) {
    m_last_active_tp = std::chrono::system_clock::now();
    std::cout << "recved data: " << data << " len: " << data.length() << " " << m_sockfd << " thread id: " << std::this_thread::get_id() << std::endl;
    return send(data);
}

bool Session::send(const std::string& msg) {
    m_last_active_tp = std::chrono::system_clock::now();
    size_t bytes_sent = 0, len = msg.length();
    const char* data = msg.c_str();
    // std::cout << "send data: " << msg.size() << " begin..." << std::endl;
    while (len > 0) {
        bytes_sent = ::send(m_sockfd, data, len, 0);
        if (bytes_sent <= 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
                return false;
            continue;
        }
        len -= bytes_sent;
        data += bytes_sent;
    }
    std::cout << "sent data: " << msg.substr(0, len) << " len: " << len << " " << m_sockfd << " thread id: " << std::this_thread::get_id() << std::endl;
    return true;
}

void Session::close() {
    if (m_sockfd == -1) return;
    int fd = m_sockfd;
    ::shutdown(m_sockfd, SHUT_RDWR);
    ::close(m_sockfd);
    m_sockfd = -1;
    std::cout << "session " << fd << " closed. " << " thread id: " << std::this_thread::get_id()  << std::endl;
}
