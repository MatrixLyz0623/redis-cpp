#include "RespParser.h"
#include <cctype>
#include <cstdlib>

void RespParser::feed(const char* data, size_t n) { buf_.append(data, n); }
void RespParser::reset() { buf_.clear(); multibulk_len_ = 0; bulk_len_ = -1; argv_work_.clear(); }

bool RespParser::readLineAny(const std::string& s, size_t& i, std::string& line) {
  for (size_t pos = i; pos < s.size(); ++pos) {
    if (s[pos] == '\n') {
      size_t end = (pos > i && s[pos-1] == '\r') ? pos-1 : pos;
      line.assign(s.data()+i, end - i);
      i = pos + 1; return true;
    }
  }
  return false;
}

bool RespParser::readLineCRLF(const std::string& s, size_t& i, std::string& line) {
  // 严格要求 CRLF，贴近 Redis 的 RESP 规范
  for (size_t pos = i; pos+1 < s.size(); ++pos) {
    if (s[pos] == '\r' && s[pos+1] == '\n') {
      line.assign(s.data()+i, pos - i);
      i = pos + 2; return true;
    }
  }
  return false;
}

bool RespParser::parseInline(const std::string& s, size_t& i, std::vector<std::string>& argv) {
  size_t save = i; std::string line;
  if (!readLineAny(s, i, line)) { i = save; return false; }
  if (line.empty()) { argv.clear(); return true; }

  argv.clear();
  size_t p = 0;
  while (p < line.size()) {
    while (p < line.size() && std::isspace((unsigned char)line[p])) ++p;
    size_t st = p;
    while (p < line.size() && !std::isspace((unsigned char)line[p])) ++p;
    if (st < p) argv.emplace_back(line.substr(st, p - st));
  }
  return true;
}

bool RespParser::parseLongCRLF(const std::string& s, size_t& i, long& out) {
  size_t save = i; std::string num;
  if (!readLineCRLF(s, i, num)) { i = save; return false; }
  char* endp = nullptr;
  long v = std::strtol(num.c_str(), &endp, 10);
  if (*endp != '\0') { i = save; return false; }
  out = v; return true;
}

bool RespParser::advanceMultibulk(std::string& proto_err) {
  // 期望： multibulk_len_ > 0 表示还需读取若干个参数（bulk strings）
  //        bulk_len_ == -1 表示需要解析 "$len\r\n"；否则需要读 len+2 个字节的 <arg>\r\n
  for (;;) {
    if (multibulk_len_ == 0) return false; // 本函数仅在 multibulk 过程中被调用

    if (bulk_len_ == -1) {
      // 需要一个 $len 行
      if (buf_.empty() || buf_[0] != '$') return false; // 需要更多数据
      size_t i = 1; long blen = 0;
      if (!parseLongCRLF(buf_, i, blen)) return false;  // 还不完整
      if (blen < -1) { proto_err = "Protocol error: invalid bulk length"; return false; }

      buf_.erase(0, i);
      bulk_len_ = blen;

      if (bulk_len_ == -1) {
        // Null bulk string —— Redis 会把它当作空参数传给命令，这里做同样处理
        argv_work_.emplace_back(std::string());
        bulk_len_ = -1;
        multibulk_len_--;
        if (multibulk_len_ == 0) return true; // 组装完成一条命令
        continue;
      }
    }

    // 需要 payload (bulk_len_) + CRLF(2)
    if (buf_.size() < (size_t)bulk_len_ + 2) return false;
    // 检查结尾 CRLF
    if (buf_[bulk_len_] != '\r' || buf_[(size_t)bulk_len_ + 1] != '\n') {
      proto_err = "Protocol error: invalid bulk string end CRLF"; return false;
    }
    // 取参数
    argv_work_.emplace_back(buf_.data(), (size_t)bulk_len_);
    buf_.erase(0, (size_t)bulk_len_ + 2);
    bulk_len_ = -1;
    multibulk_len_--;
    if (multibulk_len_ == 0) return true; // 组装完成一条命令，返回由 next() 提取
  }
}

bool RespParser::next(std::vector<std::string>& argv, std::string& proto_err) {
  argv.clear(); proto_err.clear();
  for (;;) {
    if (multibulk_len_ > 0) {
      // 正在解析 multibulk 过程
      bool done_or_progress = advanceMultibulk(proto_err);
      if (!proto_err.empty()) {
        // 协议错误：按 Redis 行为，应该返回错误并让上层关闭连接
        return true; // 用 proto_err 表示“产出一个错误”
      }
      if (!done_or_progress) return false; // 需要更多数据
      if (multibulk_len_ == 0) {
        argv.swap(argv_work_);
        argv_work_.clear();
        return true; // 产出一条命令
      }
      // 否则继续循环，读取下一参数
    } else {
      if (buf_.empty()) return false;

      if (buf_[0] == '*') {
        // 新的 multibulk：先读 "*N\r\n"
        size_t i = 1; long n = 0;
        if (!parseLongCRLF(buf_, i, n)) return false; // 需要更多数据
        if (n == -1) {
          // Null array：对命令处理没有意义，直接清空这帧
          buf_.erase(0, i);
          argv.clear(); return true; // 返回空 argv，让上层忽略
        }
        if (n < 0) {
          // 非法
          buf_.erase(0, i);
          proto_err = "Protocol error: invalid multibulk length";
          return true; // 让上层写错误并关闭
        }
        buf_.erase(0, i);
        multibulk_len_ = n;
        argv_work_.clear();
        // 继续进入 advanceMultibulk
      } else {
        // 尝试 inline
        size_t i = 0;
        if (!parseInline(buf_, i, argv)) return false; // 需要更多数据
        buf_.erase(0, i);
        // inline 空行：忽略，继续读
        if (argv.empty()) continue;
        return true;
      }
    }
  }
}
