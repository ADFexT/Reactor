#include "../include/ConnectionProcessor.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <arpa/inet.h>

ConnectionProcessor::ConnectionProcessor(ThreadSafeQueue<clientInfo>& queue,int threadNum):
    queue_(queue),threadNum_(threadNum),running_(false){
        if (threadNum_ <= 0) threadNum_ = 1;
        workerepollfds.resize(threadNum_,-1);
        connections_.resize(threadNum_);
    }

ConnectionProcessor::~ConnectionProcessor(){
    stop();
}

void ConnectionProcessor::start(){
    running_ = true;
    for (size_t i = 0; i < threadNum_; i++)
    {
        threads_.emplace_back(&ConnectionProcessor::workerloop,this,i);
    }
}

void ConnectionProcessor::stop(){
    if(!running_) return;
    running_ = false;

    for (size_t i = 0; i < threadNum_; i++)
    {
        queue_.push(clientInfo());
    }
    
    for (auto& t : threads_)
    {
        if (t.joinable())   t.join();
    }
    

    for (int fd : workerepollfds)
    {
        if (fd != -1) close(fd);
    }
}

void ConnectionProcessor::workerloop(int id){
    int epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd == -1)
    {   close(epollfd);
        fprintf(stderr,"Failed to create epoll %d",id);
        return;
    }

    workerepollfds[id] = epollfd;

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = queue_.getEventFd();

    if (epoll_ctl(epollfd,EPOLL_CTL_ADD,queue_.getEventFd(),&ev) == -1)
    {
        close(epollfd);
        fprintf(stderr,"workerloop%d failed to add queue event",id);
        return;
    }
    
    const int MAX_EVENTS = 256;
    epoll_event events[MAX_EVENTS];

    while (running_)
    {
        int n = epoll_wait(epollfd,events,MAX_EVENTS,-1);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            break;  
        }

        for (size_t i = 0; i < n; i++)
        {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;
            if (fd == queue_.getEventFd())
            {
                handleNewConnections(epollfd);
            }else{
                handleClientEvent(epollfd,fd,revents);
            }
        }
    }
    
    for (auto& pair : connections_[id]) {
        close(pair.first);
    } 
    connections_[id].clear();
}

void ConnectionProcessor::handleNewConnections(int epollfd){
    uint64_t dummy;
    read(queue_.getEventFd(),&dummy,sizeof(dummy));

    clientInfo client;
    while (queue_.trypop(client))
    {
        if (client.fd == -1) return;
        
        epoll_event ev{};
        ev.data.fd = client.fd;
        ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;

        if (epoll_ctl(epollfd,EPOLL_CTL_ADD,client.fd,&ev) == -1)
        {
            fprintf(stderr,"handleNewConnections failed to add new fd to epoll"); 
            close(client.fd);
            client.fd = -1;
            continue;
        }

        fprintf(stdout,"%s:%d connect to server\n",client.getIP().c_str(),client.getPort());
        fflush(stdout);
        
        int threadid = -1;
        for (size_t i = 0; i < threadNum_; i++)
        {
            if (workerepollfds[i] == epollfd)
            {
                threadid = i;
                break;
            }
        }

        if (threadid != -1)
        {
            connections_[threadid][client.fd] = std::make_unique<connection>(client.fd,client.getIP(),client.getPort());
        }

        client.fd = -1;
    }
}

void ConnectionProcessor::handleClientEvent(int epollfd,int fd,uint32_t events){
    int threadid = -1;
    for (size_t i = 0; i < threadNum_; i++)
    {
        if (workerepollfds[i] == epollfd)
        {
            threadid = i;
            break;
        }
    }
    if (threadid == -1) return;
    
    auto it = connections_[threadid].find(fd);
    if (it == connections_[threadid].end()) return;
    
    auto& conn = it->second;

    if ((events & EPOLLERR) || (events & EPOLLHUP) || (events & EPOLLRDHUP)) {
        close(fd);
        connections_[threadid].erase(it);
        return;
    }

    if (events&EPOLLIN)
    {
        char buf[4096];
        while (true)
        {
            size_t n = read(fd,buf,sizeof(buf));
            if (n > 0)
            {
                conn->readbuffer.append(buf,n);
                fprintf(stdout,"%s:%d:\n%s\n",conn->ip.c_str(),conn->port,conn->readbuffer.c_str());
                fflush(stdout);
            }else if (n == 0)
            {
                close(fd);
                connections_[threadid].erase(it);
                return;
            }else{
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // 读完
                } else {
                    close(fd);
                    connections_[threadid].erase(it);
                    return;
                }
            }
            
            if (!conn->writebuffer.empty())
            {
                conn->writebuffer  += conn->readbuffer;
                conn->readbuffer.clear();
                
                while (!conn->writebuffer.empty())
                {   
                    size_t n = write(fd,conn->writebuffer.data(),conn->writebuffer.length());
                    if (n > 0) {
                        conn->writebuffer.erase(0, n);
                    } else{
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            conn->writePending = true;
                            epoll_event ev{};
                            ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
                            ev.data.fd = fd;
                            epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
                            return;
                        }else{
                            close(fd);
                            connections_[threadid].erase(it);
                            return;
                        }
                    }
                    
                }
                
            }
            
            
        }
        
    }
    
    if (events&EPOLLOUT)
    {
        if (!conn->writebuffer.empty())     
        {
            while (!conn->writebuffer.empty())
            {
                ssize_t n = write(fd,conn->writebuffer.data(),conn->writebuffer.length());
                if (n > 0)
                {
                    conn->writebuffer.erase(0,n);
                }else{
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        break;
                    }else{
                        close(fd);
                        connections_[threadid].erase(it);
                        return;
                    }
                    
                }
                
            }
            
        }

        if (conn->writebuffer.empty())
        {
            conn->writePending = false;
            epoll_event ev{};
            ev.data.fd = fd;
            ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
            epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&ev);
        }   
    }
}