subdir = src/gausskernel/storage/access/hnsw
top_builddir = ../../../../..
include $(top_builddir)/src/Makefile.global

ifneq "$(MAKECMDGOALS)" "clean"
  ifneq "$(MAKECMDGOALS)" "distclean"
     ifneq "$(shell which g++ |grep hutaf_llt |wc -l)" "1"
        -include $(DEPEND)
     endif
  endif
endif
OBJS = hnsw.o \
hnswbuild.o hnswinsert.o hnswscan.o hnswvacuum.o hnswutils.o

include $(top_srcdir)/src/gausskernel/common.mk
