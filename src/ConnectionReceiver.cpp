#include "../include/ConnectionReceiver.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <iostream>

ConnectionReceiver::ConnectionReceiver(int port,ThreadSafeQueue<clientInfo>& queue):
    port_(port),queue_(queue),listenfd_(-1),epollfd_(-1),stopfd_(-1),running_(false){
        stopfd_ = eventfd(0,O_NONBLOCK | O_CLOEXEC);
        if (stopfd_ == -1)
        {
            throw std::runtime_error("Failed to create stopfd");
        }
    }

ConnectionReceiver::~ConnectionReceiver(){
    stop();
    if(stopfd_ != -1)   close(stopfd_);
    if(listenfd_ != -1) close(listenfd_);
    if(epollfd_ != -1)  close(epollfd_);
}

void ConnectionReceiver::start(){
    listenfd_ = socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK |SOCK_CLOEXEC,0);
    if (listenfd_ == -1)
    {
        throw std::runtime_error("Failed to create listenfd_");
    }

    int opt = 1;
    setsockopt(listenfd_,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    sockaddr_in addr{};
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    addr.sin_family = AF_INET;

    if (bind(listenfd_,(sockaddr *)&addr,sizeof(addr)) < 0)
    {
        close(listenfd_);
        throw std::runtime_error("Failed to bind");
    }

    if (listen(listenfd_,SOMAXCONN) < 0)
    {
        close(listenfd_);
        throw std::runtime_error("Failed to listen");
    }

    epollfd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd_ == -1)
    {
        throw std::runtime_error("Failed to create epoll");
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listenfd_;
    if (epoll_ctl(epollfd_,EPOLL_CTL_ADD,listenfd_,&ev) == -1)
    {
        throw std::runtime_error("Failed to add listenfd_ to epollfd_");
    }

    ev.events = EPOLLIN;
    ev.data.fd = stopfd_;
    if (epoll_ctl(epollfd_,EPOLL_CTL_ADD,stopfd_,&ev) == -1)
    {
        throw std::runtime_error("Failed to add stopfd_ to epollfd_");
    }
    
    running_ = true;
    thread_ = std::thread(&ConnectionReceiver::run,this); 
}

void ConnectionReceiver::stop(){
    if (running_ == false)  return;
    running_ = false;

    uint64_t u = 1;
    write(stopfd_,&u,sizeof(u));

    if (thread_.joinable())
    {
        thread_.join();
    }
}

void ConnectionReceiver::run(){
    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (running_)
    {
        int n = epoll_wait(epollfd_,events,MAX_EVENTS,-1);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            break;
        }

        for (size_t i = 0; i < n; i++)
        {
            int fd = events[i].data.fd;
            if (fd == listenfd_)
            {
                handleAccept();
            }else if (fd == stopfd_)
            {
                uint64_t dummy;
                read(stopfd_,&dummy,sizeof(dummy));
                running_ = false;
                break;
            }
        }
    }
}

void ConnectionReceiver::handleAccept(){
    while (true)
    {
        sockaddr_in clientaddr{};
        socklen_t len = sizeof(clientaddr);
        int clientfd = accept4(listenfd_,(sockaddr*)&clientaddr,&len,SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientfd == -1) break;

        clientInfo client(clientfd,clientaddr.sin_addr.s_addr,clientaddr.sin_port);
        
        queue_.push(std::move(client));
        /*
        char *ip;
        int clientport;
        inet_ntop(AF_INET,&clientaddr.sin_addr.s_addr,ip,INET_ADDRSTRLEN);
        clientport = ntohs(clientaddr.sin_port);
        fprintf(stdout,"client%s:%d connect\n",ip,clientport);
        */
    }
}