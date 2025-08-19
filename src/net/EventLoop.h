#pragma once
#include <sys/epoll.h>
#include <vector>
#include <cstdint>

class EventLoop {
    public:
        EventLoop();
        ~EventLoop();
        
        bool add(int fd, uint32_t events);
        int wait(std::vector<epoll_event> & event, int timeout_ms = -1);
    private:
        int epfd_{-1};
};