#pragma once

#include "ThreadSafeQueue.h"
#include <vector>
#include <atomic>
#include <thread>
#include <memory>
#include <unordered_map>

class ConnectionProcessor
{
private:
    void workerloop(int id);
    void handleNewConnections(int epollfd);
    void handleClientEvent(int epollfd,int fd,uint32_t events);

    struct connection
    {
        int fd;
        std::string readbuffer;
        std::string writebuffer;
        bool writePending = false;

        connection(int _fd):fd(_fd) {}
    };

    ThreadSafeQueue<int>& queue_;
    int threadNum_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_;

    std::vector<int> workerepollfds;
    std::vector<std::unordered_map<int,std::unique_ptr<connection>>> connections_;
public:
    ConnectionProcessor(ThreadSafeQueue<int>& queue,int threadNum);
    ~ConnectionProcessor();

    void start();
    void stop();
};
