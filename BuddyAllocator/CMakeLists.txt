cmake_minimum_required(VERSION 3.17)
project(BuddyAllocator)

set(CMAKE_CXX_STANDARD 20)

add_library(BuddyAllocator buddy_allocator.cpp)
