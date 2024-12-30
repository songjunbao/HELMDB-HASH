cmake_minimum_required(VERSION 3.15)
project(ExampleProject)

# set dirs of meta, should be changed to your relative path
set(META_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../meta)
set(META_CLIENT_DIR ${META_DIR}/meta_client)
set(THIRD_PARTY_DIR ${META_DIR}/third_party/install)
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../meta/proto)

# use system openssl library
find_package(OpenSSL REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)



message(STATUS "ssl:" ${OPENSSL_SSL_LIBRARY})
message(STATUS "crypto:" ${OPENSSL_CRYPTO_LIBRARY})


ADD_LIBRARY(ssl SHARED IMPORTED GLOBAL)
SET_PROPERTY(TARGET ssl PROPERTY IMPORTED_LOCATION ${OPENSSL_SSL_LIBRARY})

ADD_LIBRARY(crypto SHARED IMPORTED GLOBAL)
SET_PROPERTY(TARGET crypto PROPERTY IMPORTED_LOCATION ${OPENSSL_CRYPTO_LIBRARY})

# linking errors may occure if you change the order of DEP_INC DEP_LIB
set(DEP_INC "")
set(DEP_LIB "")
list(APPEND DEP_LIB ${OPENSSL_SSL_LIBRARY})
list(APPEND DEP_LIB ${OPENSSL_CRYPTO_LIBRARY})
list(APPEND DEP_LIB ${CMAKE_DL_LIBS})
list(APPEND DEP_LIB Threads::Threads)

list(APPEND DEP_INC ${THIRD_PARTY_DIR}/zlib/include)
list(APPEND DEP_LIB ${THIRD_PARTY_DIR}/zlib/lib/libz.a)
list(APPEND DEP_INC ${THIRD_PARTY_DIR}/gflags/include)
list(APPEND DEP_LIB ${THIRD_PARTY_DIR}/gflags/lib/libgflags.a)
list(APPEND DEP_INC ${THIRD_PARTY_DIR}/glog/include)
list(APPEND DEP_LIB ${THIRD_PARTY_DIR}/glog/lib/libglog.a)
list(APPEND DEP_INC ${THIRD_PARTY_DIR}/brpc/include)
list(APPEND DEP_LIB ${THIRD_PARTY_DIR}/brpc/lib/libbrpc.a)
list(APPEND DEP_INC ${THIRD_PARTY_DIR}/leveldb/include)
list(APPEND DEP_LIB ${THIRD_PARTY_DIR}/leveldb/lib/libleveldb.a)

list(APPEND DEP_INC ${THIRD_PARTY_DIR}/protobuf/include)
list(APPEND DEP_LIB ${THIRD_PARTY_DIR}/protobuf/lib/libprotobuf.a)
list(APPEND DEP_LIB ${THIRD_PARTY_DIR}/protobuf/lib/libprotobuf-lite.a)
list(APPEND DEP_LIB ${THIRD_PARTY_DIR}/protobuf/lib/libprotoc.a)

list(APPEND DEP_INC ${META_CLIENT_DIR}/include)
list(APPEND DEP_LIB ${META_CLIENT_DIR}/libmeta_client.a)
list(APPEND DEP_INC ${META_DIR}/proto)

include(FindThreads)
SET(DEP_LIB
        ${DEP_LIB}
        dl
        rt
        m
        ${CMAKE_THREAD_LIBS_INIT}
        )
        
message(STATUS "DEP_INC: " ${DEP_INC})
message(STATUS "DEP_LIB: " ${DEP_LIB})

# include all .pb.cc files
file(GLOB PROTO_SRCS "${PROTO_DIR}/*.pb.cc")

# add your source files and meta proto source
add_executable(example_project src/main.cpp ${PROTO_SRCS})

# add include headers for dependencies
target_include_directories(example_project PUBLIC ${DEP_INC})

# link libraries
target_link_libraries(example_project PUBLIC ${DEP_LIB})
