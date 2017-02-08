cmake_minimum_required(VERSION 3.6)
project(OS2)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS -O0)

set(SOURCE_FILES test.cpp test.h buddy.cpp buddy.h slab.cpp slab.h errors.cpp errors.h)
add_executable(OS2 ${SOURCE_FILES})