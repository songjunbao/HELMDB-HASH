subdir = src/gausskernel/storage/access/ivfflat
top_builddir = ../../../../..
include $(top_builddir)/src/Makefile.global

ifneq "$(MAKECMDGOALS)" "clean"
  ifneq "$(MAKECMDGOALS)" "distclean"
     ifneq "$(shell which g++ |grep hutaf_llt |wc -l)" "1"
        -include $(DEPEND)
     endif
  endif
endif
OBJS = ivfflat.o \
ivfbuild.o ivfinsert.o ivfscan.o ivfvacuum.o ivfutils.o ivfkmeans.o

include $(top_srcdir)/src/gausskernel/common.mk
