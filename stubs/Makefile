#

# Linux using GCC
CC=gcc
CFLAGS=-O2 
DEFINES=
STRIP=
LDFLAGS=
LIBS=
AR=ar

# Cross-compile
# Linux using mipsel-linux-
#CC=mipsel-linux-gcc
#CFLAGS=-O2
#DEFINES=
#STRIP=
#LDFLAGS=
#LIBS=
#AR=mipsel-linux-ar

##########################

C_COMPILE=$(CC) $(CFLAGS) $(DEFINES)
LINK=

OBJECTS=_stdio.o fmemopen.o fopencookie.o

.c.o:
	$(C_COMPILE) -c $<

all:	stubs

clean:
	rm -f *.o *.bak *~ *.a

stubs:	$(OBJECTS) $(UNRAR_OBJ)
	$(AR) r fmemopen.a $(OBJECTS) 
