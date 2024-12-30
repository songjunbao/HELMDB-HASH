cmake_minimum_required(VERSION 3.19)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_RULE_MESSAGES OFF)
set(CMAKE_SKIP_RPATH FALSE)
# set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/fdw_adapter)
set(NVM_MACRO_OPTIONS -DNVMDB_ADAPTER)
if ("$ENV{DEBUG_TYPE}" STREQUAL "debug")
    set(NVM_MACRO_OPTIONS ${NVM_MACRO_OPTIONS} -DDEBUG)
else()
    set(NVM_MACRO_OPTIONS ${NVM_MACRO_OPTIONS} -DNDEBUG)
endif()

# ---- Add dependencies via CPM ----
# see https://github.com/TheLartians/CPM.cmake for more info

include(vendor/CPM.cmake)

### gflags support
find_package(gflags_install
        CONFIG
        REQUIRED
        PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

### glog support
find_package(glog_install
        CONFIG
        REQUIRED
        PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

### gtest support
#find_package(gtest_install
#        CONFIG
#        REQUIRED
#        PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor
#)

# 目前用centos自带的，当前不支持原子RDMA写
#find_package(rdma-core_install
#        CONFIG
#        REQUIRED
#        PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor
#)

#find_package(rpma_install
#        CONFIG
#        REQUIRED
#        PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor
#)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/common/TurboHash/src)
include_directories(SYSTEM ${CMAKE_BINARY_DIR}/include)
include_directories("${TBB_INCLUDE_PATH}")
include_directories("${PMDK_INCLUDE_PATH}")

execute_process(COMMAND cat /proc/cpuinfo COMMAND grep clwb OUTPUT_VARIABLE CLWB)
if(CLWB)
    message(STATUS "Use clwb")
    set(nvm_core_DEF_USE_CLWB)
else()
    message(STATUS "Use clflush instead of clwb")
    set(nvm_core_DEF_USE_CLWB -DNO_CLWB)
endif()

set(nvm_core_DEF_OPTIONS ${MACRO_OPTIONS} ${nvm_core_DEF_USE_CLWB} ${NVM_MACRO_OPTIONS})
set(CXX_OPTIONS -fwrapv -std=c++17 -fnon-call-exceptions ${OPTIMIZE_LEVEL})
set(nvm_core_COMPILE_OPTIONS ${OPTIMIZE_OPTIONS} ${OS_OPTIONS} ${CXX_OPTIONS} ${BIN_SECURE_OPTIONS} ${CHECK_OPTIONS})
set(CXX_LINK_OPTIONS -pthread -std=c++17 -fPIE -pie -Wl,-z,noexecstack -Wl,-z,relro,-z,now)
set(nvm_core_LINK_OPTIONS ${CXX_LINK_OPTIONS})

set(nvm_core_inc_list ${CMAKE_CURRENT_SOURCE_DIR}/include)

#   file(GLOB_RECURSE nvmdb_headers "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
file(GLOB_RECURSE nvm_core_src_list "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER nvm_core_src_list EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/src/common/TurboHash/.*\\.cpp")

#  ${CMAKE_CURRENT_SOURCE_DIR}/fdw_adapter

# message(${nvm_core_src_list})

add_static_objtarget(gausskernel_storage_nvm_core nvm_core_src_list nvm_core_inc_list
        "${nvm_core_DEF_OPTIONS}" "${nvm_core_COMPILE_OPTIONS}" "${nvm_core_LINK_OPTIONS}")
# fdw adapter
add_subdirectory(fdw_adapter)

install(DIRECTORY ${CMAKE_BINARY_DIR}/lib DESTINATION ./
        FILES_MATCHING PATTERN "libglog.so*")

install(DIRECTORY ${CMAKE_BINARY_DIR}/lib DESTINATION ./
        FILES_MATCHING PATTERN "libgflags.so*")
#
#install(DIRECTORY ${CMAKE_BINARY_DIR}/lib DESTINATION ./
#        FILES_MATCHING PATTERN "librpma.so*")

add_subdirectory(tests)
add_subdirectory(ycsb_test)
add_subdirectory(src/common)