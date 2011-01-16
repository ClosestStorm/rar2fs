#

# Linux using GCC
CC=gcc
CXX=g++
CCFLAGS=-O2 -g -rdynamic -fno-omit-frame-pointer -I/opt/include/fuse -L./unrar -I./unrar
CXXFLAGS=-O2 -g -rdynamic -fno-omit-frame-pointer -I/opt/include/fuse -L./unrar -I./unrar
DEFINES=-D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DRARDLL
STRIP=strip
LDFLAGS=
LIBS=-lfuse -lunrar

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

C_COMPILE=$(CC) $(CCFLAGS) $(DEFINES)
CXX_COMPILE=$(CXX) $(CXXFLAGS) $(DEFINES)
LINK=$(CC)

OBJECTS=dllext.o extractext.o rar2fs.o

.c.o:
	$(C_COMPILE) -c $<
.cpp.o:
	$(CXX_COMPILE) -c $<

all:	rar2fs

clean:
	@rm -f *.o *.bak *~

rar2fs:	$(OBJECTS) $(UNRAR_OBJ)
	$(LINK) -o rar2fs $(LDFLAGS) $(OBJECTS) $(LIBS)	
	$(STRIP) rar2fs
