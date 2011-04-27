# Change below to match current configuration
##########################

UNRAR_SRC=./unrar
UNRAR_LIB=./unrar
FUSE_SRC=/opt/include/fuse
FUSE_LIB=

# Does the host support glibc custom streams?
# If unsure try 'y' here. If linker fails to find e.g. fmemopen()
# your answer was most likely incorrect.
HAS_GLIBC_CUSTOM_STREAMS=y

ifdef DEBUG
CFLAGS=-O0 -DDEBUG_
CXXFLAGS=-O0 -DDEBUG_
else
CFLAGS=-O2
CXXFLAGS=-O2
endif

ifndef CROSS
# Linux using GCC
CC=gcc
CXX=g++
CFLAGS+=-g -rdynamic -fno-omit-frame-pointer
CXXFLAGS+=-g -rdynamic -fno-omit-frame-pointer
DEFINES=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
STRIP=strip
LDFLAGS=
else
# Cross-compile
# Linux using mipsel-linux-?
CC=mipsel-linux-gcc
CXX=mipsel-linux-g++
CFLAGS+=-g
CXXFLAGS+=-g
DEFINES=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
STRIP=mipsel-linux-strip
LDFLAGS=
endif
