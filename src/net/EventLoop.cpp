#include "EventLoop.h"
#include <unistd.h>

EventLoop::EventLoop(){
    epfd_ = ::epoll_create1(0);
}

EventLoop::~EventLoop() {
  if (epfd_ >= 0) ::close(epfd_);
}

bool EventLoop::add(int fd, uint32_t events) {
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = events;
    return ::epoll_ctl(epfd_,         // 调用内核的 epoll_ctl，操作哪个 epoll 实例（epfd_）
                     EPOLL_CTL_ADD, // 操作类型：添加（也有 MOD/DEL）
                     fd,            // 要被关注的那个 fd
                     &ev) == 0;     // 指向事件配置的指针；返回 0 表示成功，这里直接比较返回布尔 
}

int EventLoop::wait(std::vector<epoll_event>& events, int timeout_ms) {
    if (events.empty())               // 如果调用方传进来的数组是空的
        events.resize(64);              // 先给它分配一个默认容量（64 个事件槽位），避免传空指针
    return ::epoll_wait(epfd_,        // 等待 epoll 实例上有事件发生
                      events.data(),// 把结果写到这块连续内存中（vector 的底层数组）
                      (int)events.size(), // 能接收的最大事件个数（epoll_wait 需要 int）
                      timeout_ms);  // 超时：-1 永久阻塞；0 立即返回；>0 最长等待毫秒数
}