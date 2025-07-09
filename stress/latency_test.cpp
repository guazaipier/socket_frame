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
#include <iomanip>

// 消息体
const char request[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr int request_len = sizeof(request) - 1;

// 延迟统计结构
struct LatencyStats {
    std::atomic<long> min{std::numeric_limits<long>::max()};
    std::atomic<long> max{0};
    std::atomic<double> total{0};
    std::atomic<long> count{0};
    std::vector<double> latencies; // 用于计算百分位数
};

class Stress {
    class Worker {
    public:
        Worker(const char* ip, int port, int connections_per_thread) : m_expect_conns(connections_per_thread) {
            m_conns.resize(connections_per_thread);
            m_epollfd = epoll_create1(0);
            if (m_epollfd == -1) {
                perror("epoll_create1");
                exit(1);
            }
            startConn(ip, port);
            m_thread = std::thread(&Worker::run, this);
            // std::cout << "worker " << std::this_thread::get_id() << " started" << std::endl;
        }

        void stop() {
            m_stop = true;

            if (m_thread.joinable())
                m_thread.join();

            for (auto& conn : m_conns)
                closeConn(conn);
            
            m_latency_stats.total = std::accumulate(m_latency_stats.latencies.begin(), m_latency_stats.latencies.end(), 0.0);
            m_latency_stats.count.fetch_add(m_latency_stats.latencies.size());
            // std::cout << "worker " << std::this_thread::get_id() << " stopped" << std::endl;
        }

        int getExpectConns() const { return m_expect_conns; }

        LatencyStats& geLatencyStats() {
            return m_latency_stats;
        }

        void run() {
            epoll_event events[10000];
            char buffer[2048];
            while (!m_stop) {
                int fds = ::epoll_wait(m_epollfd, events, 10000, 20);
                for (int i = 0; i < fds; ++i) {
                    int sockfd = events[i].data.fd;
                    if (events[i].events & EPOLLIN) {
                        if (!readOnce(sockfd, buffer, sizeof(buffer))) {
                            closeConn(sockfd);
                            continue;
                        }
                        epoll_event ev{.events = EPOLLOUT | EPOLLET | EPOLLERR, .data{.fd = sockfd}};
                        ::epoll_ctl(m_epollfd, EPOLL_CTL_MOD, sockfd, &ev);
                    } else if (events[i].events & EPOLLOUT) {
                        if (!writeOnce(sockfd, request, request_len)) {
                            closeConn(sockfd);
                            continue;
                        }
                        epoll_event ev{.events = EPOLLIN | EPOLLET | EPOLLERR, .data{.fd = sockfd}};
                        ::epoll_ctl(m_epollfd, EPOLL_CTL_MOD, sockfd, &ev);
                    } else if (events[i].events & EPOLLERR) {
                        closeConn(sockfd);
                    }
                }
            }
        }

    private:
        void startConn(const char* ip, int port) {
            sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(port), .sin_addr{.s_addr = inet_addr(ip)}};
            for (int i = 0; i < m_conns.size(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
                // std::cout << "create socket " << sockfd << std::endl;
                if (sockfd == -1) continue;
                if (::connect(sockfd, (sockaddr*)&addr, sizeof(addr)) == 0) {
                    // std::cout << "build connection " << i << std::endl;
                    addfd(m_epollfd, sockfd);
                    m_conns[i] = sockfd;
                }
            }
        }
        
        void closeConn(int sockfd) {
            ::shutdown(sockfd, SHUT_RDWR);
            ::epoll_ctl(m_epollfd, EPOLL_CTL_DEL, sockfd, nullptr);
            ::close(sockfd);
            std::remove_if(m_conns.begin(), m_conns.end(), [sockfd](int fd){ return fd == sockfd; });
        }

        void setnonblocking(int& fd) {
            int flag = ::fcntl(fd, F_GETFL);
            ::fcntl(fd, F_SETFL, flag | O_NONBLOCK);
        }

        void addfd(int& m_epollfd, int& fd) {
            epoll_event ev{.events= EPOLLOUT | EPOLLET | EPOLLERR, .data{.fd = fd}};
            ::epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &ev);
            setnonblocking(fd);
        }

        bool writeOnce(int sockfd, const char* buffer, int len) {
            int bytes_sent = 0;
            // std::cout << "write out " << len << " bytes to socket " << sockfd << std::endl;
            while (true) {
                bytes_sent = ::send(sockfd, buffer, len, 0);
                if (bytes_sent == -1) return false;
                if (bytes_sent == 0) return false;
                len -= bytes_sent;
                buffer += bytes_sent;
                if (len <= 0) {
                    m_latencies[sockfd] = std::chrono::steady_clock::now();
                    return true;
                }
            }
        }

        bool readOnce(int sockfd, char* buffer, int len) {
            int bytes_read = 0;
            bytes_read = ::recv(sockfd, buffer, len, 0);
            if (bytes_read == -1) return false;
            if (bytes_read == 0) return false;
            buffer[bytes_read] = '\0';
            // std::cout << "read " << bytes_read << " bytes from socket " << sockfd << "content: " << buffer << std::endl;
            auto recv_time = std::chrono::steady_clock::now();
            std::chrono::duration<double, std::milli> diff{recv_time - m_latencies[sockfd]};
            m_latency_stats.latencies.emplace_back(diff.count());
            m_latency_stats.min = std::min(m_latency_stats.min.load(), long(diff.count()));
            m_latency_stats.max = std::max(m_latency_stats.max.load(), long(diff.count()));
            return true;
        }

    private:
        std::vector<int> m_conns;
        int m_epollfd{-1};
        std::atomic_bool m_stop{false}; 
        std::thread m_thread;

        int m_expect_conns;
        std::unordered_map<int, std::chrono::steady_clock::time_point> m_latencies; // <sockfd, <开始时间, 延迟统计
        LatencyStats m_latency_stats; 
    };
public:
    Stress(const char* ip, int port, int worker_threads, int connections_per_thread) {
        m_workers.reserve(worker_threads);
        for (int i = 0; i < worker_threads; ++i) {
            m_workers.emplace_back(std::make_unique<Worker>(ip, port, connections_per_thread));
        }
        std::cout << "stress test started with worker count: " << m_workers.size() << std::endl;
    }

    void run(int test_duration_sec) {
        auto start_time = std::chrono::steady_clock::now();
        auto end_time = start_time + std::chrono::seconds(test_duration_sec);
        while (std::chrono::steady_clock::now() < end_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        for (auto& worker : m_workers) {
            worker->stop();
        }
        std::cout << "total worker:                     " << m_workers.size() << std::endl;
        std::cout << "per worker expect connections:    " << m_workers.size() << std::endl;
        std::cout << "total expect connections:         " << m_workers.size() * m_workers[0]->getExpectConns() << std::endl;
        double total_avg = 0, avg_p50 = 0, avg_p90 = 0, avg_p99 = 0;
        long total_count = 0, min = 1000000, max = 0;
        std::cout << "=======================stats==========================" << std::endl;
        for (auto& worker : m_workers) {
            auto& latency_stats = worker->geLatencyStats();
            auto count = latency_stats.count.load();
            double avg_latency = 0;
            if (count > 0)
                avg_latency = latency_stats.total.load() / count;
            std::cout << "=================worker latency stats=================" << std::endl;
            std::cout << "min:    " << latency_stats.min.load() << " ms" << std::endl;
            std::cout << "max:    " << latency_stats.max.load() << " ms" << std::endl;
            std::cout << "avg:    " << avg_latency << " ms" << std::endl;
            std::cout << "count:  " << count << std::endl;
            std::sort(latency_stats.latencies.begin(), latency_stats.latencies.end());
            double p50 = latency_stats.latencies[latency_stats.latencies.size() * 0.5];
            double p90 = latency_stats.latencies[latency_stats.latencies.size() * 0.9];
            double p99 = latency_stats.latencies[latency_stats.latencies.size() * 0.99];
            std::cout << "p50:    " << p50 << " ms" << std::endl;
            std::cout << "p90:    " << p90 << " ms" << std::endl;
            std::cout << "p99:    " << p99 << " ms" << std::endl;
            total_avg += avg_latency;
            total_count += count;
            min = std::min(min, latency_stats.min.load());
            max = std::max(max, latency_stats.max.load());
            avg_p50 += p50;
            avg_p90 += p90;
            avg_p99 += p99;
        }
        std::cout << "====================total stats=======================" << std::endl;
        total_avg /= m_workers.size();
        avg_p50 /= m_workers.size();
        avg_p90 /= m_workers.size();
        avg_p99 /= m_workers.size();
        std::cout << "min:         " << min << " ms" << std::endl;
        std::cout << "max:         " << max << " ms" << std::endl;
        std::cout << "p50:         " << avg_p50 << " ms" << std::endl;
        std::cout << "p90:         " << avg_p90 << " ms" << std::endl;
        std::cout << "p99:         " << avg_p99 << " ms" << std::endl;
        std::cout << "avg latency: " << total_avg << " ms" << std::endl;
        std::cout << "count:       " << total_count << std::endl;
        std::cout << "qps:         " << total_count / test_duration_sec << " /s" << std::endl;
        std::cout << "=======================stats==========================" << std::endl;
    }

private:
    std::vector<std::unique_ptr<Worker>> m_workers;
};

int main(int argc, char* argv[]) {
    assert(argc >= 3);
    char* ip = argv[1];
    int port = std::atoi(argv[2]);
    int worker_threads = (argc >= 4 ? std::atoi(argv[3]) : std::thread::hardware_concurrency());
    int connections_per_thread = (argc >= 5 ? std::atoi(argv[4]) : 100);
    int period_sec = (argc == 6 ? std::atoi(argv[5]) : 60);
    std::cout << "===============stress test with params================" << std::endl;
    std::cout << "ip:                     " << ip << std::endl;
    std::cout << "port:                   " << port << std::endl;
    std::cout << "worker_threads:         " << worker_threads << std::endl;
    std::cout << "connections_per_thread: " << connections_per_thread << std::endl;
    std::cout << "period_sec:             " << period_sec << std::endl;

    Stress stress(ip, port, worker_threads, connections_per_thread);
    
    stress.run(period_sec);

    return 0;
}