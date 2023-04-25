# CXX flags
add_compile_options(-m64 -O3 -march=native -g -pedantic-errors
                    -Wall -Wextra -Werror -Wno-pedantic)

# dpdk
include_directories(${dpdk_INCLUDE_DIRS})
set(RTE_TARGET x86_64-native-linuxapp-gcc)
link_libraries(${dpdk_LIBRARIES})

# Includes
include_directories(.)

add_executable(
    dpdk_echo
    dpdk_echo.cpp
)
