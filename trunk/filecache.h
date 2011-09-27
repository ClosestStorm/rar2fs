/*
    Copyright (C) 2009-2011 Hans Beckerus (hans.beckerus@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    This program take use of the freeware "Unrar C++ Library" (libunrar)
    by Alexander Roshal and some extensions to it.

    Unrar source may be used in any software to handle RAR archives
    without limitations free of charge, but cannot be used to re-create
    the RAR compression algorithm, which is proprietary. Distribution
    of modified Unrar source in separate form or as a part of other
    software is permitted, provided that it is clearly stated in
    the documentation and source comments that the code may not be used
    to develop a RAR (WinRAR) compatible archiver.
*/

#ifndef FILECACHE_H
#define FILECACHE_H

#include <stdlib.h>
#include <sys/stat.h>
#include <pthread.h>

typedef struct dir_elem dir_elem;
__extension__
struct dir_elem {
        char* name_p;
        char* rar_p;
        char* file_p;
        char* password_p;
        struct stat stat;
        off_t offset;     /* >0: offset in rar file (raw read) */
        union {
                off_t vsize;      /* >0: volume file size (raw read) */
                off_t msize;      /* >0: mmap size */
        };
        off_t vsize_real;
        off_t vsize_next;
        short vno_base;
        short vno_max;
        short vlen;
        short vpos;
        short vtype;
        struct Flags
        {
                unsigned int image:1;
                unsigned int fake_iso:1;
                unsigned int mmap:2;
                unsigned int :28;
        } flags;
        struct dir_elem* next_p;
};
typedef struct dir_elem dir_elem_t;

#define LOCAL_FS_ENTRY ((void*)-1)

#define ABS_ROOT(s, path) \
        (s) = alloca(strlen(path) + strlen(OBJ_STR2(OBJ_SRC,0)) + 1); \
        strcpy((s), OBJ_STR2(OBJ_SRC,0)); \
        strcat((s), path)

#define ABS_MP(s, path, file)         ABS_MP1_((s), (path), (file), __LINE__)
#define ABS_MP1_(s, path, file, line) ABS_MP2_((s), (path), (file), line)
#define ABS_MP2_(s, path, file, line) \
        int l##line##_ = strlen(path);\
        (s) = alloca(l##line##_ + strlen(file) + 3 + 2); /* add +2 in case of fake .iso */ \
        strcpy((s), path); \
        if (l##line##_ && path[l##line##_ - 1] != '/') strcat((s), "/"); \
        strcat((s), file)

extern pthread_mutex_t file_access_mutex;

dir_elem_t*
cache_path_alloc(const char* path);

dir_elem_t*
cache_path_get(const char* path);

dir_elem_t*
cache_path(const char* path, struct stat *stbuf);

void
inval_cache_path(const char* path);

void
filecache_init();

void
filecache_destroy();

#endif
