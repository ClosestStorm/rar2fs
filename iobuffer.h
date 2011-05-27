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

#ifndef IOBUFFER_H
#define IOBUFFER_H

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include "common.h"

#define IOB_SZ_DEFAULT           (4*1024*1024)
#ifdef USE_STATIC_IOB_
#define IOB_SZ                   IOB_SZ_DEFAULT
#define IOB_HIST_SZ              (IOB_SZ/2)
#else
#define IOB_SZ                   (iob_sz)
#define IOB_HIST_SZ              (iob_hist_sz)
#endif

#define IOB_NO_HIST 0
#define IOB_SAVE_HIST 1

#define IOB_RST(b)  (memset((b), 0, sizeof(IoBuf)+IOB_SZ))

typedef struct
{
  int fd;
  int mmap;
  IdxData* data_p;
} IdxInfo;

typedef struct
{
   IdxInfo idx;
   off_t offset;
   volatile size_t ri;
   volatile size_t wi;
   size_t used;
   char data_p[];
} IoBuf;


size_t
readTo(IoBuf* dest, FILE* fp, int hist);

size_t
readFrom(char* dest, IoBuf* src, size_t size, int off);

size_t
copyFrom(char* dest, IoBuf* src, size_t size, size_t pos);

extern size_t iob_hist_sz;
extern size_t iob_sz;

void
iobuffer_init();

void
iobuffer_destroy();

#endif
