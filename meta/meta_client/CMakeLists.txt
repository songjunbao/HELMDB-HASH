cmake_minimum_required(VERSION 3.15)
project(meta_client C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 设置安装路径
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)

# 查找源文件
file(GLOB META_CLIENT
    ${CMAKE_SOURCE_DIR}/meta_client/src/*.h
    ${CMAKE_SOURCE_DIR}/meta_client/src/*.hpp
    ${CMAKE_SOURCE_DIR}/meta_client/src/*.cpp
)

# 添加库
add_library(meta_client ${META_CLIENT})

# 添加依赖，这里根据需要添加 WITH_SYSTEM_LIBS 的判断
if (NOT WITH_SYSTEM_LIBS)
    add_dependencies(meta_client ssl crypto zlib glog gflags protobuf brpc)
endif()

# 设置 meta_client 的公共包含目录
target_include_directories(meta_client PUBLIC
    ${DEP_INC}
    ${CMAKE_SOURCE_DIR}/meta_client/include
)

# 设置 meta_client 的链接库
target_link_libraries(meta_client PUBLIC ${DEP_LIB})

# 安装目标到指定目录
install(TARGETS meta_client
    ARCHIVE DESTINATION src
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# 安装头文件到指定目录
install(DIRECTORY ${CMAKE_SOURCE_DIR}/meta_client/include/
    DESTINATION include/meta_client
    FILES_MATCHING PATTERN "*.h"
)