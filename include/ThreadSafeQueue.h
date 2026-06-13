#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdexcept>
#include <string>
#include <arpa/inet.h>

struct clientInfo{
    int fd = -1;
    uint32_t ip;
    uint16_t port = 0;
    clientInfo() = default;
    clientInfo(int fd_,uint32_t ip_,uint16_t port_):fd(fd_),ip(ip_),port(port_){};

    clientInfo(const clientInfo&) = delete;
    clientInfo& operator=(const clientInfo&) = delete;

    clientInfo(clientInfo&& other) noexcept
        : fd(other.fd), ip(other.ip), port(other.port) {
        other.fd = -1;
    }

    clientInfo& operator=(clientInfo&& other) noexcept {
        if (this != &other) {
            if (fd >= 0) close(fd);
            fd = other.fd;
            ip = other.ip;
            port = other.port;
            other.fd = -1;
        }
        return *this;
    }

    ~clientInfo(){
        if (fd >= 0){
            close(fd);
            fd = -1;
        } 
    }

    std::string getIP(){
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,&ip,buf,INET_ADDRSTRLEN);
        return buf;
    }

    int getPort(){
        return ntohs(port);
    }
};

template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    int eventfd_;
public:
    ThreadSafeQueue() {
        eventfd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if(eventfd_ == -1) {
            throw std::runtime_error("Failed to create eventfd");
        }
    }

    ~ThreadSafeQueue() {
        if(eventfd_ != -1) close(eventfd_);
    }

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator = (const ThreadSafeQueue&) = delete;

    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        uint64_t u = 1;
        write(eventfd_,&u,sizeof(u));
    }

    bool trypop(T& item){
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    //获取内部eventfd，用于epoll监听
    int getEventFd() const {
        return eventfd_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const{
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};