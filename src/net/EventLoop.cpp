#include "EventLoop.h"
#include <unistd.h>
#include <cstdio>   
#include <cerrno>   
EventLoop::EventLoop(){
    epfd_ = ::epoll_create1(0);
}

EventLoop::~EventLoop() {
  if (epfd_ >= 0) ::close(epfd_);
}

bool EventLoop::add(int fd, uint32_t events) {
  epoll_event ev{}; ev.data.fd = fd; ev.events = events;
  if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) != 0) {
    perror("[epoll_ctl ADD]");   // ← 打印失败原因
    return false;
  }
  return true;
}

int EventLoop::wait(std::vector<epoll_event>& events, int timeout_ms) {
    if (events.empty())               // 如果调用方传进来的数组是空的
        events.resize(64);              // 先给它分配一个默认容量（64 个事件槽位），避免传空指针
    return ::epoll_wait(epfd_,        // 等待 epoll 实例上有事件发生
                      events.data(),// 把结果写到这块连续内存中（vector 的底层数组）
                      (int)events.size(), // 能接收的最大事件个数（epoll_wait 需要 int）
                      timeout_ms);  // 超时：-1 永久阻塞；0 立即返回；>0 最长等待毫秒数
}

bool EventLoop::mod(int fd, uint32_t events) {
  epoll_event ev{}; ev.data.fd = fd; ev.events = events;
  if (::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) != 0) {
    perror("[epoll_ctl MOD]");   // ← 打印失败原因
    return false;
  }
  return true;
}

bool EventLoop::del(int fd) {
  if (::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) != 0) {
    perror("[epoll_ctl DEL]");   // ← 打印失败原因
    return false;
  }
  return true;
}