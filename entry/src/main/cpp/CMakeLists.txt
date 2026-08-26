cmake_minimum_required(VERSION 3.4.1)
project(Nodist)

# NAPI 共享库:被 ArkTS 通过 import testNapi from 'libentry.so' 调用
add_library(entry SHARED napi_init.cpp)
target_link_libraries(entry PUBLIC libace_napi.z.so)

# Nodist 原生可执行文件:单独编译,可经 hdc push 到设备端使用
add_executable(nodist main.cpp)
