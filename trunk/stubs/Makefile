#

# Linux using GCC
CC=gcc
CCFLAGS=-O2 
DEFINES=
STRIP=
LDFLAGS=
LIBS=
AR=ar

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
LINK=

OBJECTS=_stdio.o fmemopen.o fopencookie.o

.c.o:
	$(C_COMPILE) -c $<

all:	stubs

clean:
	rm -f *.o *.bak *~

stubs:	$(OBJECTS) $(UNRAR_OBJ)
	$(AR) r fmemopen.a $(OBJECTS) 
