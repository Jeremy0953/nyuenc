cmake_minimum_required(VERSION 3.10) # Set the minimum version of CMake

project(nyuenc) # Name of your project

# Set the C standard to C17 and enable GNU extensions
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_EXTENSIONS ON)

# Compiler options for debugging (disable optimizations)
set(CMAKE_C_FLAGS_DEBUG "-g")

# Compiler options for release (enable optimizations)
set(CMAKE_C_FLAGS_RELEASE "-O3")

# Find threads package for pthread
find_package(Threads REQUIRED)

# 使用file命令和GLOB选项来收集所有.c和.h文件
file(GLOB SOURCES "*.c" "*.h")

# 将收集到的源文件用于创建可执行文件nyuenc
add_executable(nyuenc ${SOURCES})

# Link pthread to your project
target_link_libraries(nyuenc PRIVATE Threads::Threads)
