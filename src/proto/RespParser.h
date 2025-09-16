#pragma once
#include <string>
#include <vector>

class RespParser {
public:
  // 喂入新到的字节（可多次）
  void feed(const char* data, size_t n);

  // 从内部缓冲提取“一条完整命令”的 argv（成功则消费；不完整返回 false）
  bool next(std::vector<std::string>& argv, std::string& proto_err);

  void reset();

private:
  std::string buf_;              // 累积的未解析字节
  long multibulk_len_ = 0;       // 剩余的参数个数（>0 表示正在解析 multibulk）
  long bulk_len_ = -1;           // 当前 bulk 的长度（-1 表示期待读取 "$len" 行）

  std::vector<std::string> argv_work_; // 暂存当前命令的 argv

  // 工具：读一行（inline 支持 \n 或 \r\n；RESP 头严格要求 \r\n）
  static bool readLineAny(const std::string& s, size_t& i, std::string& line);
  static bool readLineCRLF(const std::string& s, size_t& i, std::string& line);

  // 解析 inline 命令（成功推进 i，并写入 argv）
  static bool parseInline(const std::string& s, size_t& i, std::vector<std::string>& argv);

  // 解析 "*N\r\n" / "$len\r\n" 等数字行
  static bool parseLongCRLF(const std::string& s, size_t& i, long& out);

  // 从 buf_ 中推进 multibulk（可跨多次 feed）
  // 返回：true=本步有进展；false=需要更多数据或遇到致命协议错误（proto_err 非空）
  bool advanceMultibulk(std::string& proto_err);
};