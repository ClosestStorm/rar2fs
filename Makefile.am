ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = -Wall
AM_CXXFLAGS = $(UNRAR_CXXFLAGS) -Wall 
AM_CPPFLAGS = $(UNRAR_CPPFLAGS) $(FUSE_CPPFLAGS)
bin_PROGRAMS = rar2fs mkr2i
rar2fs_SOURCES = dllext.cpp configdb.c filecache.c iobuffer.c sighandler.c rar2fs.c
rar2fs_LDADD = $(PTHREAD_CFLAGS) $(LDFLAGS) $(UNRAR_LDFLAGS) $(FUSE_LDFLAGS) $(LIBS) $(PTHREAD_LIBS) $(FUSE_LIBS) $(UNRAR_LIBS) -lstdc++
rar2fs_LINK = $(CC) -o $@
mkr2i_SOURCES = mkr2i.c
mkr2i_LDADD = $(LDFLAGS) $(UNRAR_LDFLAGS) $(FUSE_LDFLAGS) $(LIBS) 
mkr2i_LINK = $(CC) -o $@

