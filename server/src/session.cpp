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

void Session::recv() {
    m_last_active_tp = std::chrono::system_clock::now();
    int n = 0;
    std::string data;
    while (true) {
        char buffer[MAX_BUFFER_SIZE];
        n = ::recv(m_sockfd, buffer, MAX_BUFFER_SIZE, 0);
        if (n == 0) {
            std::cout << "client closed by client: " << m_sockfd << " thread id: " << std::this_thread::get_id()<< std::endl;
            closedByClient();
            break;
        } else if (n < 0) {
            if (errno != EWOULDBLOCK || errno != EAGAIN) {
                std::cerr << "recv error: " << errno << " " << std::strerror(errno) << " thread id: " << std::this_thread::get_id() << std::endl;
                closedByClient();
            }
            break;    
        } else {
            data.append(buffer, n);
        }
    }
    if (n != 0) {
        std::cout << "recved data: " << data << " len: " << data.length() << " " << m_sockfd << " thread id: " << std::this_thread::get_id() << std::endl;
        send(data);
    }
}

void Session::send(const std::string& msg) {
    m_last_active_tp = std::chrono::system_clock::now();
    size_t ret = 0, sent_len = 0;
    const char* data = msg.c_str();
    while (sent_len < msg.length()) {
        ret = ::send(m_sockfd, data + sent_len, msg.length() - sent_len, 0);
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // std::cout << "send buffer full, wait for next time" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            else if (errno == EINTR) {
                // std::cout << "send interrupted, retrying..." << std::endl;
                // continue;
                break; // 这里被信号中断就不发送了，保持系统处理信号一致
            }
            else {
                char err[200];
                snprintf(err, sizeof(err), "send error: %d %s session id: %d thread id: %ul.", errno, std::strerror(errno), m_sockfd, std::this_thread::get_id());
                std::cerr << err << std::endl;
                break;
            }
        } else if (ret == 0) {
            std::cout << "client closed by client: " << m_sockfd<< " thread id: " << std::this_thread::get_id()  << std::endl;
            closedByClient();
            return;
        } else {
            sent_len += ret;
        }
    }
    std::cout << "sent data: " << msg.substr(0, sent_len) << " len: " << sent_len << " " << m_sockfd << " thread id: " << std::this_thread::get_id() << std::endl;
}

void Session::closedByClient() {
    std::shared_ptr<Connection> conn = m_conn.lock();
    if (conn)
        conn->removeSession(m_sockfd);
    std::cout << "session " << m_sockfd << " closedByClient " << " thread id: " << std::this_thread::get_id()  << std::endl;
}

void Session::close() {
    if (m_sockfd == -1) return;
    int fd = m_sockfd;
    ::shutdown(m_sockfd, SHUT_RDWR);
    ::close(m_sockfd);
    m_sockfd = -1;
    std::cout << "Session " << fd << " closed. " << " thread id: " << std::this_thread::get_id()  << std::endl;
}
