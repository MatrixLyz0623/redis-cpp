#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cstdlib>

#include "net/Acceptor.h"
#include "net/EventLoop.h"
#include "proto/RespParser.h"

static bool make_nonblocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags == -1) return false;
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int main() {
  //std::cout << std::unitbuf; std::cerr << std::unitbuf;

  Acceptor acceptor(6379);
  if (!acceptor.listen_on()) { std::cerr << "bind/listen failed\n"; return 1; }
  make_nonblocking(acceptor.fd());

  EventLoop loop;
  if (!loop.add(acceptor.fd(), EPOLLIN)) { std::perror("epoll ADD listen"); return 1; }

  std::vector<epoll_event> events;

  std::cout << "Listening on 6379 (epoll)…\n";
  std::unordered_map<int, RespParser> parsers; 
  std::unordered_map<std::string, std::string> kv;
  std::unordered_map<std::string, long long> expires; //{key,timeout}

  //当前时间->纪元时间
  auto now_ms = []()->long long {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  };

  for (;;) {
    int n = loop.wait(events, -1);
    if (n < 0) { if (errno == EINTR) continue; std::perror("epoll_wait"); break; }

    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      uint32_t evs = events[i].events;

      // 监听 fd
      if (fd == acceptor.fd()) {
        if (evs & EPOLLIN) {
          for (;;) {
            int cfd = acceptor.accept_one();
            if (cfd >= 0) {
              std::cout << "[accept] cfd=" << cfd << "\n";
              int one = 1; ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
              make_nonblocking(cfd);
              loop.add(cfd, EPOLLIN);
              parsers.emplace(cfd, RespParser{});
              continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            std::perror("accept"); break;
          }
        }
        continue;
      }

      // 客户端 fd
      if (evs & (EPOLLERR | EPOLLHUP)) {
        std::cout << "[close] fd=" << fd << " ERR/HUP\n";
        loop.del(fd); ::close(fd);
        parsers.erase(fd);
        continue;
      }
      //有数据进来，处理并回复
      if (evs & EPOLLIN) {
        char buf[4096];

        // 1) 先把内核缓冲的数据读出来并喂给解析器（可能会多次 recv）
        for (;;) {
          ssize_t nr = ::recv(fd, buf, sizeof(buf), 0);
          if (nr > 0) {
            std::cout << "[recv] fd=" << fd << " bytes=" << nr << "\n";
            parsers[fd].feed(buf, (size_t)nr);
            continue;              // 继续多读一点，直到 EAGAIN
          } else if (nr == 0) {    // 对端关闭（半关闭/全关闭）
            std::cout << "[close] fd=" << fd << " EOF\n";
            loop.del(fd); ::close(fd);
            parsers.erase(fd);
            break;                 // 跳出 EPOLLIN 分支
          } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // 本轮已读干净，退出读循环，去解析
              break;
            }
            std::perror("recv");
            loop.del(fd); ::close(fd);
            parsers.erase(fd);
            break;
          }
        }

        // 2) 从解析器拉取一条或多条命令，拼出本轮要回的内容
        std::vector<std::string> argv;
        std::string proto_err;
        std::string reply;

        auto append_bulk = [&](const std::string& s){
          reply.push_back('$');
          reply += std::to_string(s.size());
          reply += "\r\n";
          reply += s;
          reply += "\r\n";
        };

        while (parsers[fd].next(argv, proto_err)) {
          if (!proto_err.empty()) {
            reply += "-ERR " + proto_err + "\r\n";
            std::cout << "[proto-error] " << proto_err << "\n";
            break; // 协议错误：写完就关
          }
          if (argv.empty()) continue; // 空行/Null array 忽略

          // 命令名大写
          for (char& c : argv[0]) c = std::toupper((unsigned char)c);

          if (argv[0] == "PING") {
            if (argv.size() == 1) reply += "+PONG\r\n";
            else if (argv.size() == 2) append_bulk(argv[1]);
            else reply += "-ERR wrong number of arguments for 'PING'\r\n";
          } else if (argv[0] == "ECHO") {
            if (argv.size() == 2) append_bulk(argv[1]);
            else reply += "-ERR wrong number of arguments for 'ECHO'\r\n";
          } else if(argv[0] == "SET") {
            if(argv.size() < 3) {
              reply += "-ERR wrong number of arguments for 'SET'\r\n";
            } else {
              bool keep_ttl = false;
              long long ttl_ms = -1;

              size_t k = 3;
              bool syntax_ok = true;
              //解析expires time
              while (k < argv.size()) {
                std::string opt = argv[k];
                for(char& ch:opt) ch = std::toupper((unsigned char) ch);

                if(opt == "EX" || opt == "PX") {
                  if((k +1) >= argv.size()) {reply += "-ERR syntax error\r\n"; syntax_ok = false; break; }
                  char* endp = nullptr;
                  long long n = std::strtoll(argv[k+1].c_str(), &endp, 10);
                  if (*endp != '\0' || n <= 0) { reply += "-ERR value is not an integer or out of range\r\n"; syntax_ok = false; break; }
                  if (ttl_ms != -1) { reply += "-ERR syntax error\r\n"; syntax_ok = false; break; } // 不能同时给 EX 和 PX
                  ttl_ms = (opt == "EX") ? (n * 1000LL) : n;
                  k += 2; // 消费参数和值
                } else if (opt == "KEEPTTL") {
                  keep_ttl = true;
                  k += 1;
                } else {
                  reply += "-ERR syntax error\r\n";
                  syntax_ok = false;
                  break;
                }
              }

              if(syntax_ok) {
                kv[argv[1]] = argv[2];
                if(ttl_ms > 0) {
                  expires[argv[1]] = now_ms() + ttl_ms;
                } else if (!keep_ttl) {
                  expires.erase(argv[1]); 
                }
                reply += "+OK\r\n";
              }
            }
          } else if (argv[0] == "GET") {
              if (argv.size() == 2) {
                  auto it = kv.find(argv[1]);
                  if (it == kv.end()) {
                    reply += "$-1\r\n";  // null bulk（不存在）
                  } else {
                    //过期检查
                    auto eit = expires.find(argv[1]);
                    if(eit != expires.end() && eit->second <= now_ms()) {
                      kv.erase(it);
                      expires.erase(eit);
                      reply += "$-1\r\n";
                    } else {
                      const std::string& v = it->second;
                      append_bulk(v);
                    }
                  }
                } else {
                  reply += "-ERR wrong number of arguments for 'GET'\r\n";
                }
          }else {
            reply += "-ERR unknown command\r\n";
          }
        }

        // 3) 发送回复；若发生协议错误，按 Redis 行为写完就关
        if (!reply.empty()) {
          ssize_t nw = ::send(fd, reply.data(), reply.size(), MSG_NOSIGNAL);
          if (nw < 0) std::perror("send");

          if (!proto_err.empty()) { // 协议错误：写完就关
            loop.del(fd); ::close(fd); parsers.erase(fd);
          }
        }
      }
    }
  }
  return 0;
}
