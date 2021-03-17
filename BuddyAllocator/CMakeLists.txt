cmake_minimum_required(VERSION 3.17)
project(contest)
project(BuddyAllocator)

set(CMAKE_CXX_STANDARD 20)

add_library(BuddyAllocator BuddyAllocator.cpp)
add_executable(contest main.cpp BuddyAllocator.cpp)