#pragma once
#include <netinet/in.h>
#pragma once
#include <netinet/in.h>

class Acceptor {
public:
  explicit Acceptor(int port = 6379);
  ~Acceptor();

  bool listen_on();      // 绑定并开始监听
  int  fd() const;       // 监听 socket fd
  int  accept_one();     // 接受一个连接（阻塞版，下一步会改非阻塞）

private:
  int server_fd_{-1};
  sockaddr_in addr_{};
};