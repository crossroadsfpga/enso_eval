CMAKE_MINIMUM_REQUIRED(VERSION 3.5)
PROJECT(MICA)



SET(CMAKE_CXX_COMPILER g++)
SET(CMAKE_C_LINK_EXECUTABLE g++)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_EXTENSIONS True)



# OPTION(AVX2 "Enable AVX2" ON)
OPTION(DPDK "Enable Intel DPDK" ON)
OPTION(DEBUG "Enable debugging" OFF)
OPTION(LTO "Use link time optimization" ON)



INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/src)
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)



ADD_DEFINITIONS(-std=c++17)

ADD_DEFINITIONS(-Wall -Wextra)
ADD_DEFINITIONS(-Wzero-as-null-pointer-constant)
ADD_DEFINITIONS(-Wsign-conversion)
ADD_DEFINITIONS(-Wconversion)
ADD_DEFINITIONS(-Winline)

ADD_DEFINITIONS(-Wno-unused-function)

ADD_DEFINITIONS(-march=native)
ADD_DEFINITIONS(-pthread)
ADD_DEFINITIONS(-mavx)

ADD_DEFINITIONS(-g)



IF(DEBUG)

  MESSAGE(WARNING "Debugging is ENABLED (to disable, run `cmake .. -DDEBUG=OFF`). Performance will be low.")
  ADD_DEFINITIONS(-Og)
  # ADD_DEFINITIONS(-O0)

ELSE(DEBUG)

  MESSAGE(STATUS "Debugging is disabled (to enable, run `cmake .. -DDEBUG=ON`)")
  ADD_DEFINITIONS(-DNDEBUG)
  ADD_DEFINITIONS(-O3)
  # ADD_DEFINITIONS(--param inline-unit-growth=200 --param large-function-growth=500)

ENDIF(DEBUG)


IF(LTO)

  MESSAGE(STATUS "Link time optimization is enabled (to disable, run `cmake .. -DLTO=OFF`)")
  ADD_DEFINITIONS(-flto)
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")

ELSE(LTO)

  MESSAGE(WARNING "Link time optimization is DISABLED (to enable, run `cmake .. -DLTO=ON`). Performance will be low.")

ENDIF(LTO)


IF(DPDK)

  set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
  find_package(dpdk REQUIRED)

  MESSAGE(STATUS "Intel DPDK support is enabled (to disable, run `cmake .. -DDPDK=OFF`)")
  set(RTE_SDK ${CMAKE_SOURCE_DIR}/build/dpdk)
  add_definitions(-DUSE_DPDK)
  add_definitions(-isystem ${RTE_SDK}/build/include -include rte_config.h)
  link_directories(${LINK_DIRECTORIES} ${RTE_SDK}/build/lib)
  set(LIBRARIES ${LIBRARIES} -Wl,--whole-archive)
  set(LIBRARIES ${LIBRARIES} rte_kvargs rte_mbuf rte_ethdev rte_mempool rte_ring rte_eal)
  set(LIBRARIES ${LIBRARIES} -Wl,--no-whole-archive)
  set(LIBRARIES ${LIBRARIES} rt dl)

ELSE(DPDK)

  MESSAGE(STATUS "Intel DPDK support is disabled (to enable, run `cmake .. -DDPDK=ON`)")

ENDIF(DPDK)


IF(SQLITE)

  MESSAGE(STATUS "Sqlite backend support is enabled (to disable, run `cmake .. -DSQLITE=OFF`)")
  add_definitions(-DUSE_SQLITE)
  set(LIBRARIES ${LIBRARIES} sqlite3)

ELSE(SQLITE)

  MESSAGE(STATUS "Sqlite backend support is disabled (to enable, run `cmake .. -DSQLITE=ON`)")

ENDIF(SQLITE)



SET(SOURCES ${SOURCES} src/mica/alloc/hugetlbfs_shm.cc)
SET(SOURCES ${SOURCES} src/mica/directory/directory_client.cc)
SET(SOURCES ${SOURCES} src/mica/directory/etcdcpp/etcd_mod.cc)
SET(SOURCES ${SOURCES} src/mica/util/config.cc)
SET(SOURCES ${SOURCES} src/mica/util/cityhash/city_mod.cc)
SET(SOURCES ${SOURCES} src/mica/util/siphash/siphash24.c)
SET_SOURCE_FILES_PROPERTIES(src/mica/util/siphash/siphash24.c PROPERTIES LANGUAGE CXX)
SET(SOURCES ${SOURCES} src/mica/util/lcore.cc)
SET(SOURCES ${SOURCES} src/mica/util/stopwatch.cc)
SET(SOURCES ${SOURCES} src/mica/util/zipf.cc)

SET(LIBRARIES ${LIBRARIES} rt numa pthread)
SET(LIBRARIES ${LIBRARIES} curl)
# SET(LIBRARIES ${LIBRARIES} boost_coroutine boost_system)

IF(LTO)

  IF(DPDK)

    ADD_EXECUTABLE(server_enso src/mica/test/server.cc ${SOURCES})
    TARGET_COMPILE_DEFINITIONS(server_enso PRIVATE USE_ENSO)
    TARGET_LINK_LIBRARIES(server_enso ${LIBRARIES})
    TARGET_LINK_LIBRARIES(server_enso "enso")

    ADD_EXECUTABLE(server_dpdk src/mica/test/server.cc ${SOURCES})
    TARGET_LINK_LIBRARIES(server_dpdk ${LIBRARIES})

    ADD_EXECUTABLE(netbench_enso src/mica/test/netbench.cc ${SOURCES})
    TARGET_COMPILE_DEFINITIONS(netbench_enso PRIVATE USE_ENSO)
    TARGET_LINK_LIBRARIES(netbench_enso ${LIBRARIES})
    TARGET_LINK_LIBRARIES(netbench_enso "enso")

    ADD_EXECUTABLE(netbench_dpdk src/mica/test/netbench.cc ${SOURCES})
    TARGET_LINK_LIBRARIES(netbench_dpdk ${LIBRARIES})

  ENDIF(DPDK)

ELSE(LTO)

  ADD_LIBRARY(common ${SOURCES})

  IF(DPDK)

    ADD_EXECUTABLE(server src/mica/test/server.cc)
    TARGET_LINK_LIBRARIES(server common ${LIBRARIES})

    ADD_EXECUTABLE(netbench src/mica/test/netbench.cc)
    TARGET_LINK_LIBRARIES(netbench common ${LIBRARIES})

  ENDIF(DPDK)

ENDIF(LTO)
