#pragma once

#include <string>
#include <memory>
#include <arpa/inet.h>
#include <chrono>

class Connection;
class Session {
public:
    Session(int id, std::weak_ptr<Connection> conn, sockaddr_in* addr);
    ~Session();
    
    void recv();
    void send(const std::string& msg);
    void close();

    auto lastActive() const { return m_last_active_tp; }
private:
    void closedByClient();
private:
    int m_sockfd;
    sockaddr_in m_sockaddr;
    std::weak_ptr<Connection> m_conn;
    std::chrono::system_clock::time_point m_last_active_tp;
};