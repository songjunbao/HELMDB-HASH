#This is the main CMAKE for build bin.
AUX_SOURCE_DIRECTORY(${CMAKE_CURRENT_SOURCE_DIR} TGT_hnsw_SRC)

set(TGT_hnsw_INC 
    ${PROJECT_SRC_DIR}/gausskernel/cbb/communication
    ${PROJECT_SRC_DIR}/include/iprange
    ${PROJECT_SRC_DIR}/include/libcomm
    ${PROJECT_SRC_DIR}/include
    ${PROJECT_SRC_DIR}/lib/gstrace
    ${LIBCGROUP_INCLUDE_PATH}
    ${ZLIB_INCLUDE_PATH}
    ${LIBCURL_INCLUDE_PATH} 
)

set(hnsw_DEF_OPTIONS ${MACRO_OPTIONS})
set(hnsw_COMPILE_OPTIONS ${OPTIMIZE_OPTIONS} ${OS_OPTIONS} ${PROTECT_OPTIONS} ${WARNING_OPTIONS} ${BIN_SECURE_OPTIONS} ${CHECK_OPTIONS})
set(hnsw_LINK_OPTIONS ${BIN_LINK_OPTIONS})
add_static_objtarget(gausskernel_storage_access_hnsw TGT_hnsw_SRC TGT_hnsw_INC "${hnsw_DEF_OPTIONS}" "${hnsw_COMPILE_OPTIONS}" "${hnsw_LINK_OPTIONS}")
