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
    int findThreadId(int epollfd);
    void closeConnection(int epollfd,int threadid,int fd);

    struct connection
    {
        int fd;
        int port;
        std::string ip;
        std::string readbuffer;
        std::string writebuffer;
        bool writePending = false;

        connection(int _fd,std::string _ip,int _port):fd(_fd),ip(_ip),port(_port) {}
    };

    ThreadSafeQueue<clientInfo>& queue_;
    int threadNum_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_;

    std::vector<int> workerepollfds;
    std::vector<std::unordered_map<int,std::unique_ptr<connection>>> connections_;
public:
    ConnectionProcessor(ThreadSafeQueue<clientInfo>& queue,int threadNum);
    ~ConnectionProcessor();

    void start();
    void stop();
};
