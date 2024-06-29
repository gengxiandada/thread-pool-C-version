cmake_minimum_required(VERSION 3.28)
project(thread_pool C)

set(CMAKE_C_STANDARD 11)

add_executable(thread_pool main.c ThreadPool.c)
