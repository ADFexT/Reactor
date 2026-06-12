// File: main.cpp
#include "../include/ConnectionReceiver.h"
#include "../include/ConnectionProcessor.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>

// 全局对象指针，用于信号处理
std::unique_ptr<ConnectionReceiver> g_receiver;
std::unique_ptr<ConnectionProcessor> g_processor;
std::unique_ptr<ThreadSafeQueue<int>> g_queue;

// 信号处理函数，用于优雅退出
void signalHandler(int sig) {
    std::cout << "\nReceived signal " << sig << ", stopping server..." << std::endl;
    
    if (g_receiver) g_receiver->stop();
    if (g_processor) g_processor->stop();
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    exit(0);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    int port = 8888;
    int threadNum = std::thread::hardware_concurrency();
    if (threadNum == 0) threadNum = 4;

    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) threadNum = atoi(argv[2]);

    std::cout << "Starting Reactor Server on port " << port 
              << " with " << threadNum << " worker threads." << std::endl;

    try {
        g_queue = std::make_unique<ThreadSafeQueue<int>>();
        g_processor = std::make_unique<ConnectionProcessor>(*g_queue, threadNum);
        g_receiver = std::make_unique<ConnectionReceiver>(port, *g_queue);
        
        g_processor->start();
        g_receiver->start();

        std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;

        // 主线程保持运行
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}