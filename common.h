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

#ifndef COMMON_H
#define COMMON_H 

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#if defined ( DEBUG_ ) && DEBUG_ > 5
#undef DEBUG_
#endif

#if defined ( DEBUG_ ) 
#if DEBUG_ > 2
#define ELVL_ 2
#else
#define ELVL_ DEBUG_
#endif 
#define ENTER_(...)         ENTER__(ELVL_, __VA_ARGS__)
#define ENTER__(l, ...)     ENTERx_(l, __VA_ARGS__)
#define ENTERx_(l, ...)     ENTER##l##_(__VA_ARGS__)
#define ENTER1_(fmt, ...)   printd(1, "%s()\n", __func__)
#define ENTER2_(fmt, ...)   printd(2, "%s()   " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define ENTER_(...)
#endif

#ifdef DEBUG_
#define printd(l, fmt, ...) do{ if(l <= DEBUG_) fprintf(stderr, fmt, ##__VA_ARGS__); }while(0)
#else
#define printd(...)
#endif

/* MAC OS X version of gcc does not handle this properly!? */
#if defined ( __GNUC__ ) &&  defined ( __APPLE__ )
#define no_warn_result_ 
#else
#define no_warn_result_ void*ignore_result_;ignore_result_=(void*)
#endif

#ifdef __GNUC__
#define MB() do{ __asm__ __volatile__ ("" ::: "memory"); } while(0)
#else
#warning Check code for MB() on current platform
#define MB() 
#endif

typedef struct
{
   unsigned int magic;
   unsigned short version;
   unsigned short spare;
   off_t offset;
   size_t size;
} IdxHead;

typedef struct
{
   IdxHead head;
   char bytes[1]; /* start of data bytes */
} IdxData;

#endif
