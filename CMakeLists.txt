cmake_minimum_required(VERSION 3.15)
project(codecrafters_redis_cpp LANGUAGES CXX)

# 使用 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 开启常用警告
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)
elseif (MSVC)
  add_compile_options(/W4)
endif()

# 可执行文件
add_executable(redis_server
  src/Server.cpp
  src/net/Acceptor.cpp
  src/net/EventLoop.cpp
)
