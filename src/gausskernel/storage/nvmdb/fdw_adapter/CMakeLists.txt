    set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_RULE_MESSAGES OFF)
set(CMAKE_SKIP_RPATH TRUE)

set(nvm_adapter_DEF_OPTIONS ${MACRO_OPTIONS} ${NVM_MACRO_OPTIONS})

set(nvm_adapter_COMPILE_OPTIONS ${OPTIMIZE_OPTIONS} ${OS_OPTIONS} ${PROTECT_OPTIONS} ${BIN_SECURE_OPTIONS} ${CHECK_OPTIONS})

set(nvm_adapter_LINK_OPTIONS ${BIN_LINK_OPTIONS})

file(GLOB_RECURSE nvm_adapter_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

set(nvm_adapter_src_list ${nvmdb_headers} ${nvm_adapter_sources})

set(nvm_adapter_headers
		${PROJECT_SRC_DIR}/include
		${nvm_core_inc_list}
		${CMAKE_CURRENT_SOURCE_DIR}
)

message(${nvm_adapter_headers})

#  ${PROJECT_SRC_DIR}/include
add_static_objtarget(gausskernel_storage_nvm_fdw_adapter nvm_adapter_src_list nvm_adapter_headers
		"${nvm_adapter_DEF_OPTIONS}" "${nvm_adapter_COMPILE_OPTIONS}" "${nvm_adapter_LINK_OPTIONS}")

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/nvm_fdw.control
		DESTINATION share/postgresql/extension
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/nvm_fdw--1.0.sql
		DESTINATION share/postgresql/extension
)
