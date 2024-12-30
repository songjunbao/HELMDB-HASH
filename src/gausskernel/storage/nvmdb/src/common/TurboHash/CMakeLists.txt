cmake_minimum_required(VERSION 3.11)

# THIS HAS TO COME BEFORE THE PROJECT LINE
# set(CMAKE_C_COMPILER "clang")
# set(CMAKE_CXX_COMPILER "clang++")
# THIS HAS TO COME BEFORE THE PROJECT LINE

# set the project name
project (turbohash VERSION 1.0.0 LANGUAGES C CXX)
# 设置 CMP0081 策略为 OLD，允许相对路径
cmake_policy(SET CMP0081 OLD)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# default using release mode
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE release) # RelWithDebInfo
  message(STATUS "build type set to release")
endif()
string(TOLOWER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE)
message(STATUS "build type: ${CMAKE_BUILD_TYPE}")

option(AVX512 "Enable use of the Advanced Vector Extensions 512 (AVX512) instruction set" ON)

# 添加 TurboHash 的源文件
file(GLOB TurboHash_SOURCES "${PROJECT_SOURCE_DIR}/src/turbo/*.h" "${PROJECT_SOURCE_DIR}/src/util/*.h")
# 创建 TurboHash 库
add_library(turbohash STATIC ${TurboHash_SOURCES})
set_target_properties(turbohash PROPERTIES LINKER_LANGUAGE CXX)

# add liballoc.a library: https://github.com/urcs-sync/ralloc
execute_process(  COMMAND make libralloc.a -j8
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/lib/ralloc/test
)
link_directories(${PROJECT_SOURCE_DIR}/lib/ralloc/test)
list(APPEND THIRDPARTY_LIBS ralloc)
add_definitions(-DRALLOC)


include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -Wall -Wextra")
endif()

# tbb
find_library(TbbLib tbb)
list(APPEND THIRDPARTY_LIBS  ${TbbLib})
list(APPEND THIRDPARTY_LIBS pmem vmem pmemobj)
# add pthread support
find_package(Threads REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wno-unused-parameter -Wno-ignored-qualifiers -msse -msse2")

if(AVX512)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__AVX512__ -mavx -mavx512f")
endif(AVX512)

# check existence of jemalloc
include(CheckIncludeFile)
check_include_file("jemalloc/jemalloc.h" HAVE_JEMALLOC)
if(HAVE_JEMALLOC)
  add_definitions(-DJEMALLOC)
  list(APPEND THIRDPARTY_LIBS jemalloc)
endif()

# In GCC, this will invoke the "-I" command
target_include_directories(
        turbohash PUBLIC
        "/usr/include"
        "/usr/local/include"
        "${PROJECT_SOURCE_DIR}/lib/pcm/src"
        "${PROJECT_SOURCE_DIR}/lib/ralloc/src"
        "${CMAKE_BINARY_DIR}/lib/gflags/include"
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/lib"
)

# 添加库搜索路径
target_link_directories(
        turbohash PUBLIC
        "${THIRDPARTY_LIBS}"
        "${PROJECT_SOURCE_DIR}/lib/gflags/build/lib"
        "${PROJECT_SOURCE_DIR}/lib/ralloc/test"
        "/usr/local/lib"
        "/usr/lib"
        "/usr/lib64"
        "/usr/local/sbin"
)

# 所有二进制文件都将放在构建文件夹中
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})


# 打印所有的 include 路径和 lib 路径
get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "include dir='${dir}'")
endforeach()
get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY LINK_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "lib dir='${dir}'")
endforeach()

foreach(tlib ${THIRDPARTY_LIBS})
  message(STATUS "lib name='${tlib}'")
endforeach()


