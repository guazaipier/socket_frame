#include <iostream>
#include <signal.h>
#include <atomic>
#include <thread>

#include "server.h"

// 退出信号标志
std::atomic_bool g_stop_server(false);

// 处理终止信号
void on_signal(int signum) {
    std::cout << "process recved signal: " << signum << std::endl;
    g_stop_server = true;
}

int main() {
    // 启动管理器
    Server server(4000);
    // 获取终止信号
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    // 启动线程用于监听终止信号
    std::thread stop_thread([]{
        while (!g_stop_server) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    std::cout << "server started." << std::endl;
    server.run();
    // 等待线程结束
    g_stop_server = true;
    stop_thread.join();
    std::cout << "server stopped." << std::endl;
    return 0;
}
