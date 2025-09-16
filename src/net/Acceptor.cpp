#include "Acceptor.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

static bool set_reuseaddr(int fd) {
  int reuse = 1;
  return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == 0;
}

Acceptor::Acceptor(int port) {
  server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) return;
  set_reuseaddr(server_fd_);

  std::memset(&addr_, 0, sizeof(addr_));
  addr_.sin_family = AF_INET;
  addr_.sin_addr.s_addr = INADDR_ANY;
  addr_.sin_port = htons(port);
}

Acceptor::~Acceptor() {
  if (server_fd_ >= 0) ::close(server_fd_);
}

bool Acceptor::listen_on() {
  if (server_fd_ < 0) return false;
  if (::bind(server_fd_, (sockaddr*)&addr_, sizeof(addr_)) != 0) return false;
  if (::listen(server_fd_, 128) != 0) return false;
  return true;
}

int Acceptor::fd() const { return server_fd_; }

int Acceptor::accept_one() {
  sockaddr_in cli{}; socklen_t len = sizeof(cli);
  return ::accept(server_fd_, (sockaddr*)&cli, &len); 
}