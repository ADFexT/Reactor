#pragma once
#include "ThreadSafeQueue.h"
#include <string>
#include <thread>
#include <atomic>

class ConnectionReceiver {
public:
    ConnectionReceiver(int port,ThreadSafeQueue<std::int>& queue);
    ~ConnectionReceiver();

    void start();
    void stop();
private:
    void run();
    void handleaccept();
    int port_;
    int listen_fd_;
    int epoll_fd_;
    int stopEvent_fd_;
    std::thread thread_;
    std::atomic<bool> running_;
    ThreadSafeQueue<std::int>& queue_;
};


