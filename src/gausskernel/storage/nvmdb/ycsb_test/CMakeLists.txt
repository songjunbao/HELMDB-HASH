cmake_minimum_required(VERSION 3.14...3.22)

project(YCSBTests LANGUAGES CXX)


#set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fprofile-arcs -ftest-coverage")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -ffast-math -march=native")

# ---- Options ----

#option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)


# ---- Create binary ----

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(YCSBTests ${headers} ${sources} ${nvm_core_src_list})
target_include_directories(YCSBTests PUBLIC ${nvm_core_inc_list} ${PROJECT_INCLUDE_DIR})

# enable compiler warnings
#if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
#    target_compile_options(gausskernel_storage_nvm_core PUBLIC -Wall -Wpedantic -Wextra -Werror)
#elseif(MSVC)
#    target_compile_options(gausskernel_storage_nvm_core PUBLIC /W4 /WX)
#    target_compile_definitions(NVMDBTests PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)
#endif()

enable_testing()

target_compile_features(YCSBTests PUBLIC cxx_std_17)
target_link_directories(YCSBTests PRIVATE
        ${CMAKE_BINARY_DIR}/lib
        ${SECUREDYNAMICLIB_HOME}
        ${TBB_LIB_PATH}
        ${PMDK_LIB_PATH})

# The -Wl,-R part tells the resulting binary to also look for the library in
# /usr/local/lib at runtime before trying to use the one in /usr/lib/.
target_link_libraries(YCSBTests PRIVATE glog gflags numa
#        "-Wl,-R${CMAKE_BINARY_DIR}/lib64" rpma
        "-Wl,-R/usr/local/lib64" ibverbs # 目前还未自动化编译RDMA-CORE
        "-Wl,-R${SECUREDYNAMICLIB_HOME}" securec
        "-Wl,-R${TBB_LIB_PATH}" tbb
        ${CMAKE_DL_LIBS} pmemobj pmem
        stdc++fs ndctl daxctl pthread)

# "-l:libtbb.so.12.11"
target_compile_options(YCSBTests PRIVATE ${nvm_core_COMPILE_OPTIONS})

target_include_directories(
        YCSBTests PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)
# ---- code coverage ----

#if(ENABLE_TEST_COVERAGE)
#    target_compile_options(gausskernel_storage_nvm_core PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
#    target_link_options(gausskernel_storage_nvm_core PUBLIC -fprofile-arcs -ftest-coverage)
#endif()
