cmake_minimum_required(VERSION 3.10)
project(SetIntersection)

# Output targets
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# C++ STD version
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# C STD version
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_EXTENSIONS True)

# DPDK
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(dpdk REQUIRED)
find_package(PCAP REQUIRED)

# CXX flags
add_compile_options(-m64 -O3 -march=native -g
                    -pedantic-errors -Wall -Wextra -Werror)
# Headers
include_directories(.)
include_directories(${dpdk_INCLUDE_DIRS})
set(RTE_TARGET x86_64-native-linuxapp-gcc)

# Sources
add_executable(dpdk_nitrosketch
    dpdk_nitrosketch.cpp
    minheap.cpp
    xxhash.cpp
)

add_executable(enso_nitrosketch
    enso_nitrosketch.cpp
    minheap.cpp
    xxhash.cpp
)

# Link libraries
target_link_libraries(dpdk_nitrosketch ${dpdk_LIBRARIES})
target_link_libraries(enso_nitrosketch pthread "enso")
