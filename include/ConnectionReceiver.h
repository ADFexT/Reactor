#pragma once

#include "ThreadSafeQueue.h"
#include <string>
#include <atomic>
#include <thread>

class ConnectionReceiver{
private:
    void run();
    void handleAccept();

    int port_;
    int listenfd_;
    int epollfd_;
    int stopfd_;
    ThreadSafeQueue<clientInfo>& queue_;
    std::atomic<bool> running_;
    std::thread thread_;

public:
    ConnectionReceiver(int port,ThreadSafeQueue<clientInfo>& queue);
    ~ConnectionReceiver();

    void start();
    void stop();
};
