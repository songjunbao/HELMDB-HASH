MODULE_big = nvmdb_fdw

EXTENSION = nvmdb_fdw

REGRESS = nvmdb_fdw

subdir=src/gausskernel/storage/nvmdb/fdw_adapter
top_builddir ?= ../../../../../

ENGINE_INC = $(top_builddir)/src/gausskernel/storage/nvmdb/include

include $(top_builddir)/src/Makefile.global

OBJ_DIR = ./obj

SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(SRCS))
DEPS := $(patsubst %.cpp, $(OBJ_DIR)/%.d, $(SRCS))

DATA = nvm_fdw.control nvm_fdw--1.0.sql

include $(top_srcdir)/src/gausskernel/common.mk
override CXXFLAGS += -I$(top_builddir)/$(subdir) -I$(ENGINE_INC)
override CXXFLAGS += -isystem $(GFLAG_INCLUDE_PATH)/include
override CXXFLAGS += -isystem $(GLOG_INCLUDE_PATH)/include
override CXXFLAGS += -isystem $(TBB_INCLUDE_PATH)/include
override CXXFLAGS += -isystem $(PMDK_INCLUDE_PATH)/include

$(OBJS): | buildrepo

install: install-data

.PHONY: install-data
install-data: installdirs
	$(INSTALL_DATA) $(addprefix $(srcdir)/, $(DATA)) '$(DESTDIR)$(datadir)/extension/'

installdirs:
	$(MKDIR_P) '$(DESTDIR)$(datadir)/extension'

uninstall: uninstall-data

.PHONY: uninstall-data
uninstall-data:
	rm -f $(addprefix '$(DESTDIR)$(datadir)/extension'/, $(notdir $(DATA)))

clean: 
	rm -rf $(OBJ_DIR)

buildrepo:
	@$(call make-repo)

$(OBJ_DIR)/%.o: %.cpp
	$(COMPILE.cpp) -std=c++17 -MMD -MP -MF"$(patsubst %.o,%.d,$@)" -MT"$@" -o $@ $<

show:
	@echo "CC_VERSION=${CC_VERSION}"
	@echo "enable_cassert=${enable_cassert}"
	@echo "CC=${CC}"
	@echo
	@echo "DEBUG=${DEBUG}"
	@echo
	@echo "BUILD=${BUILD}"
	@echo
	@echo "SRC_DIRS=${SRC_DIRS}"
	@echo
	@echo "CPPFLAGS=${CPPFLAGS}"
	@echo
	@echo "CFLAGS=${CFLAGS}"
	@echo
	@echo "LDFLAGS=${LDFLAGS}"
	@echo
	@echo "CXXFLAGS=${CXXFLAGS}"
	@echo
	@echo "CPPS=${CPPS}"
	@echo
	@echo "OBJS=${OBJS}"

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

define make-repo
   mkdir -p $(OBJ_DIR); 
endef
