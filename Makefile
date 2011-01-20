# Change below to match current configuration
##########################

UNRAR_SRC=./unrar
UNRAR_LIB=./unrar
FUSE_SRC=/usr/include/fuse
FUSE_LIB=

ifeq ("$(CROSS)", "")
# Linux using GCC
CC=gcc
CXX=g++
CFLAGS=-O2 -g -rdynamic -fno-omit-frame-pointer  
CXXFLAGS=-O2 -g -rdynamic -fno-omit-frame-pointer 
DEFINES=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
STRIP=strip
LDFLAGS=
else
# Cross-compile
# Linux using mipsel-linux-?
CC=mipsel-linux-gcc
CXX=mipsel-linux-g++
CFLAGS=-O2 
CXXFLAGS=-O2 
DEFINES=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
STRIP=mipsel-linux-strip
LDFLAGS=
endif

# Do not change anything below this line
##########################

ifneq ("$(UCLIBC_STUBS)", "")
LIBS=-lfuse -lunrar -lfmemopen -pthread
else
LIBS=-lfuse -lunrar -pthread
endif
C_COMPILE=$(CC) $(CFLAGS) $(DEFINES) -DRARDLL -DFUSE_USE_VERSION=27
CXX_COMPILE=$(CXX) $(CXXFLAGS) $(DEFINES) -DRARDLL
LINK=$(CC)
ifneq ("$(FUSE_LIB)", "")
LIB_DIR=-L$(UNRAR_LIB) -L$(FUSE_LIB)
else
LIB_DIR=-L$(UNRAR_LIB)
endif

OBJECTS=dllext.o extractext.o  filecache.o iobuffer.o rar2fs.o
DEPS=.deps

all:	rar2fs

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
	$(STRIP) rar2fs

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
