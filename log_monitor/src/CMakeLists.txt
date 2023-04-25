
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

# Hyperscan must be installed for this to work.
link_libraries("hs")

link_libraries(pthread)
link_libraries(pcap)

add_executable(
    dpdk_log_monitor
    dpdk_log_monitor.cpp
    build_regex_database.cpp
)

add_executable(
    enso_log_monitor
    enso_log_monitor.cpp
    build_regex_database.cpp
)

add_executable(
    test_log_monitor
    test_log_monitor.cpp
    build_regex_database.cpp
)

add_executable(
    generate_pcap_from_log
    generate_pcap_from_log.cpp
)
