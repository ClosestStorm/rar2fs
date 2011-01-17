#

UNRAR_SRC=./unrar
UNRAR_LIB=./unrar
FUSE_SRC=/opt/include/fuse
FUSE_LIB=

# Linux using GCC
CC=gcc
CXX=g++
CCFLAGS=-O2 -g -rdynamic -fno-omit-frame-pointer  
CXXFLAGS=-O2 -g -rdynamic -fno-omit-frame-pointer 
DEFINES=-D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DRARDLL
STRIP=strip
LDFLAGS=

# Cross-compile
# Linux using mipsel-linux-?
#CC=mipsel-linux-gcc
#CXX=mipsel-linux-g++
#CFLAGS=-O2
#CXXFLAGS=-O2 -I
#DEFINES=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
#STRIP=mipsel-linux-strip
#LDFLAGS=

##########################

ifneq ("$(UCLIBC_STUBS)", "")
LIBS=-lfuse -lunrar -lfmemopen -pthread
else
LIBS=-lfuse -lunrar -pthread
endif
C_COMPILE=$(CC) $(CCFLAGS) $(DEFINES)
CXX_COMPILE=$(CXX) $(CXXFLAGS) $(DEFINES)
LINK=$(CC)
ifneq ("$(FUSE_LIB)", "")
LIB_DIR=-L$(UNRAR_LIB) -L$(FUSE_LIB)
else
LIB_DIR=-L$(UNRAR_LIB)
endif

OBJECTS=dllext.o extractext.o rar2fs.o

.c.o:
	$(C_COMPILE) -I$(UNRAR_SRC) -I$(FUSE_SRC) -c $<
.cpp.o:
	$(CXX_COMPILE) -I$(UNRAR_SRC) -I$(FUSE_SRC) -c $<

all:	rar2fs

clean:
	(cd stubs;make clean)
	rm -f *.o *.bak *~

ifneq ("$(UCLIBC_STUBS)", "")
rar2fs:	$(OBJECTS) $(UNRAR_OBJ) 
	(cd stubs;make)
else
rar2fs:	$(OBJECTS) $(UNRAR_OBJ) 
endif
	$(LINK) -o rar2fs $(LDFLAGS) $(OBJECTS) $(LIB_DIR) $(LIBS)	
	$(STRIP) rar2fs
