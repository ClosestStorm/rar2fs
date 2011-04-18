
include config.mk

LIBS=-lfuse -lunrar -pthread
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
	rm -rf *.o *.bak *~ $(DEPS)

ifneq ("$(UCLIBC_STUBS)", "")
rar2fs:	$(OBJECTS) 
	(cd stubs;make CROSS=$(CROSS))
else
rar2fs:	$(OBJECTS) 
endif
	$(LINK) -o rar2fs $(LDFLAGS) $(OBJECTS) $(LIB_DIR) $(LIBS)	
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
