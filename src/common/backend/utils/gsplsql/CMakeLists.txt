#This is the main CMAKE for build all components.
AUX_SOURCE_DIRECTORY(${CMAKE_CURRENT_SOURCE_DIR} TGT_gsplsql_SRC)

set(TGT_gsplsql_INC
        ${PROJECT_OPENGS_DIR}/contrib/log_fdw
        ${PROJECT_TRUNK_DIR}/distribute/bin/gds
        ${PROJECT_SRC_DIR}/include/libcomm
        ${PROJECT_SRC_DIR}/include
        ${PROJECT_SRC_DIR}/lib/gstrace
        ${LZ4_INCLUDE_PATH}
        ${LIBCGROUP_INCLUDE_PATH}
        ${EVENT_INCLUDE_PATH}
        ${ZLIB_INCLUDE_PATH}
        )

set(gsplsql_DEF_OPTIONS ${MACRO_OPTIONS})
set(gsplsql_COMPILE_OPTIONS ${OPTIMIZE_OPTIONS} ${OS_OPTIONS} ${PROTECT_OPTIONS} ${WARNING_OPTIONS} ${BIN_SECURE_OPTIONS} ${CHECK_OPTIONS})
set(gsplsql_LINK_OPTIONS ${BIN_LINK_OPTIONS})
add_static_objtarget(common_backend_utils_gsplsql TGT_gsplsql_SRC TGT_gsplsql_INC "${gsplsql_DEF_OPTIONS}" "${gsplsql_COMPILE_OPTIONS}" "${gsplsql_LINK_OPTIONS}")
