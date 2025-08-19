#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

#include "net/Acceptor.h"
#include "net/EventLoop.h"
static bool make_nonblocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags == -1) return false;
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  Acceptor acceptor=Acceptor(6379);
  if (!make_nonblocking(acceptor.fd())) {
    std::cerr << "Failed to set nonblocking on listen fd\n";
    return 1;
  }
  if (!acceptor.listen_on()) {
    std::cerr << "Failed to bind/listen on 6379\n";
    return 1;
  }

  EventLoop loop;
  if (!loop.add(acceptor.fd(), EPOLLIN)) {
    std::cerr << "epoll add listen fd failed\n";
    return 1;
  }

  std::cout << "Listening on 6379 (epoll)…\n";
  std::vector<epoll_event> events;
  const std::string pong = "+PONG\r\n";

  for (;;) {
    int n = loop.wait(events, -1);
    if (n < 0) {
      if (errno == EINTR) continue;
      std::cerr << "epoll_wait error: " << std::strerror(errno) << "\n";
      break;
    }

    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      uint32_t evs = events[i].events;

      if (fd != acceptor.fd()) continue;        // 本步只关心 listen fd
      if (!(evs & EPOLLIN)) continue;

      // 非阻塞 accept：把就绪队列里所有连接都取出来
      for (;;) {
        int cfd = acceptor.accept_one();
        if (cfd < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 没有更多了
          std::cerr << "accept error: " << std::strerror(errno) << "\n";
          break;
        }
        // 直接回应并关闭
        ::send(cfd, pong.data(), pong.size(), MSG_NOSIGNAL);
        ::close(cfd);
      }
    }
  }
  return 0;
}
