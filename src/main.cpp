#include "../include/ConnectionReceiver.h"
#include "../include/ConnectionProcessor.h"
#include "../include/ThreadSafeQueue.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

// 全局变量（用于信号处理）
std::atomic<bool> g_running(true);

// 信号处理函数（处理Ctrl+C退出）
void handleSignal(int sig) {
    if (sig == SIGINT) {
        g_running = false;
    }
}

int main(int argc, char* argv[]) {
    // 配置参数
    const int SERVER_PORT = 8888;          // 监听端口
    const int WORKER_THREAD_NUM = 4;       // 工作线程数

    // 注册信号处理（处理Ctrl+C）
    signal(SIGINT, handleSignal);

    try {
        // 1. 创建线程安全队列（连接接收器和处理器共享）
        ThreadSafeQueue<clientInfo> connQueue;

        // 2. 创建并启动连接接收器（负责监听和接受新连接）
        ConnectionReceiver receiver(SERVER_PORT, connQueue);
        receiver.start();
        std::cout << "ConnectionReceiver started on port " << SERVER_PORT << std::endl;

        // 3. 创建并启动连接处理器（负责处理客户端读写事件）
        ConnectionProcessor processor(connQueue, WORKER_THREAD_NUM);
        processor.start();
        std::cout << "ConnectionProcessor started with " << WORKER_THREAD_NUM << " worker threads" << std::endl;

        // 4. 主线程等待退出信号
        std::cout << "Server running, press Ctrl+C to stop..." << std::endl;
        while (g_running) {
            sleep(1); // 主线程休眠，等待信号
        }

        // 5. 资源清理
        std::cout << "\nReceived SIGINT, stopping server..." << std::endl;
        receiver.stop();
        processor.stop();
        std::cout << "Server stopped successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Server startup failed: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}