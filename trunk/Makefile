
include config.mk

UNAME := $(shell uname)

LIBS=-lunrar -pthread
ifeq ($(UNAME), Darwin)
LIBS+=-lstdc++
OSX_VER := $(shell sw_vers | grep ProductV | cut -d"." -f2)
NEED_FUSE_INO64 := $(shell `test $(OSX_VER) -gt 5` && echo true)
ifeq ($(NEED_FUSE_INO64), true)
LIBS+=-lfuse_ino64
else
LIBS+=-lfuse
endif
else
LIBS+=-lfuse 
endif
ifeq ("$(HAS_GLIBC_CUSTOM_STREAMS)", "y")
CONF+=-DHAS_GLIBC_CUSTOM_STREAMS_
endif
ifneq ("$(UCLIBC_STUBS)", "")
LIBS+=-lfmemopen
endif

C_COMPILE=$(CC) $(CFLAGS) $(DEFINES) $(CONF) -DRARDLL -DFUSE_USE_VERSION=27
CXX_COMPILE=$(CXX) $(CXXFLAGS) $(DEFINES) -DRARDLL
LINK=$(CC)
ifneq ("$(FUSE_LIB)", "")
LIB_DIR=-L$(UNRAR_LIB) -L$(FUSE_LIB)
else
LIB_DIR=-L$(UNRAR_LIB)
endif

OBJECTS=dllext.o extractext.o configdb.o filecache.o iobuffer.o rar2fs.o
DEPS=.deps

all:	rar2fs mkr2i

clean:
	(cd stubs;make clean)
	rm -rf *.o *~ $(DEPS)

clobber:
	(cd stubs;make clobber)
	rm -rf *.o *.P *.d rar2fs mkr2i *~ $(DEPS)

ifneq ("$(UCLIBC_STUBS)", "")
rar2fs:	$(OBJECTS) 
	(cd stubs;make CROSS=$(CROSS))
else
rar2fs:	$(OBJECTS) 
endif
	$(LINK) -o rar2fs.x $(LDFLAGS) $(OBJECTS) $(LIB_DIR) $(LIBS)	
	echo $X
ifneq ("$(STRIP)", "")
	$(STRIP) rar2fs
endif

mkr2i:	mkr2i.o 
	$(LINK) -o mkr2i $(LDFLAGS) mkr2i.o
ifneq ("$(STRIP)", "")
	$(STRIP) mkr2i
endif

%.o : %.c
	@mkdir -p .deps
	$(C_COMPILE) -MD -I$(UNRAR_SRC) -I$(FUSE_SRC) -c $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	mv $*.P $(DEPS); \
	rm -f $*.d


%.o : %.cpp
	@mkdir -p .deps
	$(CXX_COMPILE) -MD -I$(UNRAR_SRC) -c $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	mv $*.P $(DEPS); \
	rm -f $*.d

-include $(OBJECTS:%.o=$(DEPS)/%.P)
