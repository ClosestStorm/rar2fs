# Change below to match current configuration
##########################

ifeq ("$(CROSS)", "")
# Linux using GCC
CC=gcc
CFLAGS=-O2 
DEFINES=
STRIP=
LDFLAGS=
LIBS=
AR=ar
else
# Cross-compile
# Linux using mipsel-linux-gcc
CC=mipsel-linux-gcc
CFLAGS=-O2
DEFINES=
STRIP=
LDFLAGS=
LIBS=
AR=mipsel-linux-ar
endif

# Do not change anything below this line
##########################

C_COMPILE=$(CC) $(CFLAGS) $(DEFINES)
LINK=

OBJECTS=_stdio.o fmemopen.o fopencookie.o
DEPS=.deps

all:	stubs

clean:
	rm -rf *.o *.bak *~ *.a .deps

stubs:	$(OBJECTS) $(UNRAR_OBJ)
	$(AR) r fmemopen.a $(OBJECTS) 

%.o : %.c
	@mkdir -p .deps
	$(C_COMPILE) -MD -c $<
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	-e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	mv $*.P $(DEPS); \
	rm -f $*.d

-include $(OBJECTS:%.o=$(DEPS)/%.P)

