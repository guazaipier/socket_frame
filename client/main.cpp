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

#define MAX_BUFFER_SIZE 1024

class TcpClient {
public:
    /** 构造函数
     * @param server_ip 服务端IP地址
     * @param server_port 服务端端口
     * @param client_port 客户端要绑定的端口，如果为 0，则绑定的端口号随机
     */
    TcpClient(const char* ip, int server_port, int client_port = 0);
    ~TcpClient();

    // 阻塞方式发送数据
    void send(const std::string& msg);
    // 阻塞方式接收数据
    void recv();
    // 客户端运行状态， false 为结束运行，需要重新建立新的 client 进行 IO 通信
    bool isStop() const { return m_sockfd < 0; }
private:
    // 初始化客户端 socket 信息
    void init(const char* ip, int server_port, int client_port = 0);
    // 结束运行，释放资源；调用者可能是内部收发数据出现错误调用，也可能是外部主动调用，也可能是析构调用
    void stop();
    // 等待 socket 资源正常关闭读写，配合 stop 使用
    void waitForClose();
private:
    // 客户端 socket 句柄
    std::atomic_int m_sockfd;
    // 服务端地址，客户端地址
    sockaddr_in m_server_addr, m_client_addr;
};

int main(int argc, char* argv[]) {
    TcpClient client("127.0.0.1", 4000);
    int count = 1000;
    while (count && !client.isStop()) {
        client.send("hello world");
        client.recv();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        count--;
    }
    // }
    // client.send("Hello, world!1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111");
    // client.recv();
    return 0;
}

TcpClient::TcpClient(const char* ip, int server_port, int client_port) : m_sockfd(-1) {
    init(ip, server_port, client_port);
}

TcpClient::~TcpClient() {
    stop();
}

void TcpClient::send(const std::string& msg) {
    int ret = 0, sent_len = 0;
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
                break;
            }
            else {
                std::cerr << "send error: " << errno << " " << std::strerror(errno) << std::endl;
                break;
            }
        } else if (ret == 0) {
            std::cout << "client closed by client." << std::endl;
            stop();
            return;
        } else {
            sent_len += ret;
        }
    }
    std::cout << "Sent data: " << msg.substr(0, sent_len) << " len: " << sent_len << std::endl;
}

void TcpClient::recv() {
    int n = 0;
    std::string data;
    while (m_sockfd > 0) {
        char buffer[MAX_BUFFER_SIZE];
        std::cout << "recv begin: " << std::endl;
        n = ::recv(m_sockfd, buffer, MAX_BUFFER_SIZE, 0);
        if (n == 0) {
            std::cout << "client closed by server: " << m_sockfd << std::endl;
            stop();
            break;
        } else if (n < 0) {
            if (errno != EWOULDBLOCK || errno != EAGAIN)
                std::cerr << "recv error: " << errno << " " << std::strerror(errno) << std::endl;
            break;    
        } else {
            std::cout << "recved data: " << std::string(buffer, n) << " len: " << n << std::endl;
            data.append(buffer, n);
            if (n < MAX_BUFFER_SIZE)  break; // 一次性接收少于 MAX_BUFFER_SIZE 的数据，则退出循环，否则会阻塞在 ::recv；非阻塞 socket 需要判断 n < 0 && errno == EWOULDBLOCK 才退出循环
            // ++count;
        }
    }
    std::cout << "recved data end: " << data << " len: " << data.length() << std::endl;
}

void TcpClient::init(const char* ip, int server_port, int client_port) {
    //创建socket  
    m_sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd == -1) {
        std::cerr << "failed to create socket: " << errno << std::strerror(errno) << std::endl;
        exit(1);
    }
    //绑定地址  
    struct sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(client_port), .sin_addr{.s_addr = htonl(INADDR_ANY)}};
    if (::bind(m_sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "failed to bind socket: " << errno << std::strerror(errno) << std::endl;
        stop();
        exit(1);
    }
    // 连接服务器
    m_server_addr.sin_family = AF_INET;
    m_server_addr.sin_port = htons(server_port);
    m_server_addr.sin_addr.s_addr = inet_addr(ip);
    if (::connect(m_sockfd, (struct sockaddr*)&m_server_addr, sizeof(m_server_addr))) {
        std::cerr << "failed to connect to server: " << errno << std::strerror(errno) << std::endl;
        stop();
        exit(1);
    }
    // 设置 SO_LINGER 选项，使 close 发送 RST
    // struct linger linger_opt;
    // linger_opt.l_onoff = 1;    // 启用
    // linger_opt.l_linger = 0;   // 超时时间为0，发送RST
    // ::setsockopt(m_sockfd, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt)); // 压测过程结束后容易引发大量 established 状态

    socklen_t client_addr_len = sizeof(m_client_addr);
    ::getsockname(m_sockfd, (sockaddr*)&m_client_addr, &client_addr_len);
    std::cout << "client started " << m_sockfd << " port: " << ntohs(m_client_addr.sin_port) << " " << inet_ntoa(m_client_addr.sin_addr) << std::endl;
}

void TcpClient::stop() {
    int fd = m_sockfd;
    if (m_sockfd != -1) {
        ::shutdown(m_sockfd, SHUT_RDWR);
        waitForClose();
        ::close(m_sockfd);
        m_sockfd = -1;        
    }
    std::cout << "client stopped " << fd << " port: " << ntohs(m_client_addr.sin_port) << std::endl;
}

void TcpClient::waitForClose() {
    timeval tv {2, 0}; // 2 seconds
    ::setsockopt(m_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buffer[128];
    ssize_t n = 1;
    while (n > 0) {
        n = ::recv(m_sockfd, buffer, sizeof(buffer), 0);
    }
}