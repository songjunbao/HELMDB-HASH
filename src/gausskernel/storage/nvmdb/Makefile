CC=g++
CFLAGS ?=

subdir = src/gausskernel/storage/nvmdb
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

ifeq ($(enable_cassert), yes)
override CXXFLAGS += -DDEBUG
else
override CXXFLAGS += -DNDEBUG
endif

.SUFFIXES:
.SUFFIXES: .o .cpp .h .hpp

# Common flags definition
CFLAGS += -std=c++17 -w -D_GLIBCXX_USE_CXX11_ABI=0

# 设置clwb
HAVE_CLWB = $(shell cat /proc/cpuinfo | grep clwb)
ifndef HAVE_CLWB
CFLAGS += -DNO_CLWB
endif

# 找到src下所有cpp文件
SRC_DIRS = $(shell find src -type d 2> /dev/null)
# 添加项目的 include 文件夹
INCLUDE_DIR = include
CFLAGS += -I$(INCLUDE_DIR)
# OBJ_DIR
OBJ_DIR = ./obj

# turn on some warnings
CFLAGS += -Wwrite-strings -Wcast-align -Wreturn-type
CFLAGS += -Wpointer-arith -Wlogical-op -Waddress -Wsizeof-pointer-memaccess -Winit-self
CFLAGS += -DNVMDB_ADAPTER

LDFLAGS += $(CFLAGS)

CPPS := $(foreach dir, $(SRC_DIRS), $(wildcard $(dir)/*.cpp))
OBJS := $(CPPS)
OBJS := $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(OBJS))
DEPS := $(OBJS:.o=.d)

.PHONY: FORCE all

all: objfiles

FORCE:

$(OBJS): | buildrepo

objfiles: objfiles.txt
objfiles.txt: Makefile $(OBJS)

$(OBJ_DIR)/%.o: %.cpp
	@$(CC) -c $(CFLAGS) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF"$(patsubst %.o,%.d,$@)" -MT"$@" -o $@ $<

.PHONY: clean

clean:
	@rm -rf $(OBJ_DIR) objfiles.txt

buildrepo:
	@$(call make-repo)

.PHONY: show
show:
	@echo "enable_cassert=${enable_cassert}"
	@echo
	@echo "CC_VERSION=${CC_VERSION}"
	@echo
	@echo "CC=${CC}"
	@echo
	@echo "SRC_DIRS=${SRC_DIRS}"
	@echo
	@echo "CPPFLAGS=${CPPFLAGS}"
	@echo
	@echo "CXXFLAGS=${CXXFLAGS}"
	@echo
	@echo "CFLAGS=${CFLAGS}"
	@echo
	@echo "LDFLAGS=${LDFLAGS}"
	@echo
	@echo "CPPS=${CPPS}"
	@echo
	@echo "OBJS=${OBJS}"

# Optionally include dependencies files
ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

define make-repo
   mkdir -p $(OBJ_DIR); \
   mkdir -p $(BIN_DIR); \
   for dir in $(SRC_DIRS); \
   do \
	mkdir -p $(OBJ_DIR)/$$dir; \
   done
endef

# 查找 fdw_adapter 中的makefile
SUBDIRS = fdw_adapter
include $(top_builddir)/src/gausskernel/common.mk
