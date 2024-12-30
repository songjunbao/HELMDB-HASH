cmake_minimum_required(VERSION 3.15)
project(Meta C CXX)

option(WITH_META_CLIENT "build meta-client" ON)
option(DEBUG "Print debug logs" OFF)
option(WITH_SYSTEM_LIBS "use system installed headers and libraries instead of auto dependency" OFF)
option(WITH_DEBUG_SYMBOLS "With debug symbols" OFF)
option(WITH_TESTS "With tests" OFF)
option(WITH_GPERF "Link tcmalloc and profiler" OFF)

message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}, version: "
        "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}, version: "
        "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# check compiler version
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # require at least gcc 4.8
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC is too old, please install a newer version supporting C++11")
    endif ()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # require at least clang 3.3
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
        message(FATAL_ERROR "Clang is too old, please install a newer version supporting C++11")
    endif ()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
else ()
    message(WARNING "You are using an unsupported compiler! Compilation has only been tested with Clang and GCC.")
endif ()

SET(THIRD_PARTY_PATH ${CMAKE_CURRENT_BINARY_DIR}/third_party)
SET(THIRD_PARTY_BUILD_TYPE Release)
SET(EXTERNAL_PROJECT_LOG_ARGS
        LOG_DOWNLOAD 0
        LOG_UPDATE 1
        LOG_CONFIGURE 0
        LOG_BUILD 0
        LOG_TEST 1
        LOG_INSTALL 0)

include(ProcessorCount)
#ProcessorCount(NUM_OF_PROCESSOR)
SET(NUM_OF_PROCESSOR 4)
message(NUM_OF_PROCESSOR: ${NUM_OF_PROCESSOR})

#thread
include(FindThreads)

#openssl
find_package(OpenSSL REQUIRED)

message(STATUS "ssl:" ${OPENSSL_SSL_LIBRARY})
message(STATUS "crypto:" ${OPENSSL_CRYPTO_LIBRARY})

ADD_LIBRARY(ssl SHARED IMPORTED GLOBAL)
SET_PROPERTY(TARGET ssl PROPERTY IMPORTED_LOCATION ${OPENSSL_SSL_LIBRARY})

ADD_LIBRARY(crypto SHARED IMPORTED GLOBAL)
SET_PROPERTY(TARGET crypto PROPERTY IMPORTED_LOCATION ${OPENSSL_CRYPTO_LIBRARY})

#zlib
if (NOT WITH_SYSTEM_LIBS)
    include(zlib)
else ()
    ADD_LIBRARY(zlib SHARED IMPORTED GLOBAL)
    SET(ZLIB_LIBRARIES z)
endif ()

#gflags
if (WITH_SYSTEM_LIBS)
    find_path(GFLAGS_INCLUDE_DIR NAMES gflags/gflags.h)
    find_library(GFLAGS_LIBRARIES NAMES gflags)
    if ((NOT GFLAGS_INCLUDE_DIR) OR (NOT GFLAGS_LIBRARIES))
        message(FATAL_ERROR "Fail to find gflags")
    endif ()
    ADD_LIBRARY(gflags SHARED IMPORTED GLOBAL)
else ()
    include(gflags)
endif ()

#glog
if (WITH_SYSTEM_LIBS)
    find_path(GLOG_INCLUDE_DIR NAMES glog/logging.h)
    find_library(GLOG_LIBRARIES NAMES glog)
    if ((NOT GLOG_INCLUDE_DIR) OR (NOT GLOG_LIBRARIES))
        message(FATAL_ERROR "Fail to find glog")
    endif ()
    ADD_LIBRARY(glog SHARED IMPORTED GLOBAL)
else ()
    include(glog)
endif ()

#protobuf
include(protobuf)

#brpc
if (WITH_SYSTEM_LIBS)
    #leveldb(for brpc)
    find_library(LEVELDB_LIBRARIES NAMES leveldb)
    if (NOT LEVELDB_LIBRARIES)
        message(FATAL_ERROR "Fail to find leveldb")
    endif ()

    find_path(BRPC_INCLUDE_DIR NAMES brpc/server.h)
    find_library(BRPC_LIBRARIES NAMES libbrpc.a brpc)
    if ((NOT BRPC_INCLUDE_DIR) OR (NOT BRPC_LIBRARIES))
        message(FATAL_ERROR "Fail to find brpc")
    endif ()
else ()
    include(leveldb)
    include(brpc)
endif ()
message(BRPC:${BRPC_INCLUDE_DIR}, ${BRPC_LIBRARIES})

#tcmalloc
if (WITH_GPERF)
    include(FindGperftools)
    if (GPERFTOOLS_FOUND)
        add_library(gperf SHARED IMPORTED GLOBAL)
        set_property(TARGET gperf PROPERTY IMPORTED_LOCATION tcmalloc_and_profiler)
    else ()
        message("GPERFTOOLS NOT FOUND, downloading and compiling")
        include(gperftools)
    endif ()
endif ()

message(CUR_DIR : ${CMAKE_CURRENT_BINARY_DIR}, SRC_DIR : ${CMAKE_SOURCE_DIR})
file(GLOB PROTO_FILES ${CMAKE_SOURCE_DIR}/proto/*.proto)
message("protoc: ${PROTOBUF_PROTOC_EXECUTABLE}, proto inc: ${PROTOBUF_INCLUDE_DIRS}, lib: ${PROTOBUF_LIBRARIES}, ${PROTOBUF_PROTOC_LIBRARY}, protos: ${PROTO_FILES}")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/proto)
foreach (PROTO ${PROTO_FILES})
    message(proto : ${PROTO})
    get_filename_component(PROTO_WE ${PROTO} NAME)
    string(REPLACE ".proto" "" PROTO_WE ${PROTO_WE})
    list(APPEND PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_WE}.pb.h")
    list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_WE}.pb.cc")
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_WE}.pb.h ${CMAKE_CURRENT_BINARY_DIR}/proto/${PROTO_WE}.pb.cc
            COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
            --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/proto
            --proto_path=${PROTOBUF_INCLUDE_DIR}
            --proto_path=${CMAKE_SOURCE_DIR}/proto ${PROTO}
            DEPENDS protobuf
    )
endforeach ()
add_library(PROTO_OBJS OBJECT ${PROTO_SRCS} ${PROTO_HDRS})
message("protoc: ${PROTOBUF_PROTOC_EXECUTABLE}, proto srcs : ${PROTO_SRCS}")

SET(DEP_INC
        ${CMAKE_CURRENT_BINARY_DIR}/proto

        ${OPENSSL_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIR}

        ${PROTOBUF_INCLUDE_DIR}
        ${GFLAGS_INCLUDE_DIR}
        ${GLOG_INCLUDE_DIR}

        ${BRPC_INCLUDE_DIR}

        # ${GPERFTOOLS_INCLUDE_DIR}
        )

SET(DEP_LIB
        ${BRPC_LIBRARIES}

        ${LEVELDB_LIBRARIES}
        )

SET(DEP_LIB
        ${DEP_LIB}
        ${PROTOBUF_LIBRARIES}
        ${PROTOBUF_PROTOC_LIBRARY}
        ${GFLAGS_LIBRARIES}
        ${GLOG_LIBRARIES}
        )

if (WITH_GPERF)
        SET(DEP_LIB ${DEP_LIB} ${GPERFTOOLS_LIBRARIES})
    endif ()
    
SET(DEP_LIB
        ${DEP_LIB}
        ${ZLIB_LIBRARIES}
        ${OPENSSL_SSL_LIBRARY}
        ${OPENSSL_CRYPTO_LIBRARY}
        dl
        rt
        m
        ${CMAKE_THREAD_LIBS_INIT}
        )

message("DEP INCLUDES: ${DEP_INC}")
message("DEP LIBRARYS: ${DEP_LIB}")

if (WITH_DEBUG_SYMBOLS)
    SET(DEBUG_SYMBOL "-ggdb")
endif ()

if (NOT DEBUG)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG")
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNDEBUG")
endif ()

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 ${DEBUG_SYMBOL} -O2 -pipe -fopenmp -Wall -W -fPIC -Wno-unused-parameter -Wno-strict-aliasing -Wno-parentheses -fno-omit-frame-pointer ")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 ${DEBUG_SYMBOL} -O2 -pipe -fopenmp -Wall -W -fPIC -Wno-unused-parameter -Wno-strict-aliasing -Wno-parentheses -fno-omit-frame-pointer")

add_definitions(
        -D_GNU_SOURCE
        -D__STDC_FORMAT_MACROS
        -D__STDC_LIMIT_MACROS
        -D__STDC_CONSTANT_MACROS
        -DBRPC_WITH_GLOG=1
)

set(COMMON_INC
        ${DEP_INC}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
        include/common
        )

file(GLOB COMMON
        ${CMAKE_CURRENT_BINARY_DIR}/proto/*.pb.*
        src/common/*.cpp
        )

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/output/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/output/bin)

set(COMMON_DEPS ssl crypto zlib glog gflags protobuf brpc)
if (WITH_GPERF)
    set(COMMON_DEPS ${COMMON_DEPS} gperf)
endif ()

add_library(common STATIC ${COMMON} $<TARGET_OBJECTS:PROTO_OBJS>)
add_dependencies(common ${COMMON_DEPS})
target_include_directories(common PUBLIC ${COMMON_INC})

set(CMAKE_VERBOSE_MAKEFILEON ON)

# Meta
file(GLOB META_SERVER src/meta_server/*.cpp)
set(META_INC ${COMMON_INC} include/meta_server)
message(${META_SERVER})
add_executable(Meta ${META_SERVER})
add_dependencies(Meta common)
target_include_directories(Meta PUBLIC ${META_INC})
target_link_libraries(Meta PUBLIC common ${DEP_LIB})

if (WITH_META_CLIENT)
    message("Build meta client")
    add_subdirectory(meta_client)
endif ()