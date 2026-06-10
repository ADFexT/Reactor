#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdexcept>

template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue(){
        event_fd = eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC);
        if(event_fd == -1){
            throw std::runtime_error("Failed to create eventfd");
        }
    }

    ~ThreadSafeQueue() {
        if (event_fd != -1){
            close(event_fd);
        }
    }

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    void push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        uint64_t one = 1;
        if (write(event_fd,&one,sizeof(one)) == -1){
            throw std::runtime_error("Failed to write to eventfd");
        }
    }

    bool trypop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    int getEventFd() const {
        return event_fd;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    int event_fd;
};