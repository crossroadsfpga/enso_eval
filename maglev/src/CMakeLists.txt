
# CXX flags
add_compile_options(-m64 -O3 -march=native -g -pedantic-errors
                    -Wall -Wextra -Werror -Wno-pedantic)

# dpdk
include_directories(${dpdk_INCLUDE_DIRS})
set(RTE_TARGET x86_64-native-linuxapp-gcc)
link_libraries(${dpdk_LIBRARIES})

# Includes
include_directories(.)

# Enso must be installed for this to work.
link_libraries("enso")

link_libraries(pthread)

add_executable(
    dpdk_maglev
    dpdk_maglev.cpp
)

add_executable(
    enso_maglev
    enso_maglev.cpp
)

add_executable(
    test_maglev
    test_maglev.cpp
)
