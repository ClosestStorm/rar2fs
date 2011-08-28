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
#include <stdint.h>   /* C99 uintptr_t */

#ifdef __sun__
#include <arpa/nameser_compat.h>
#define __BYTE_ORDER BYTE_ORDER
#define CONST_DIRENT_ const
#endif
#ifdef __APPLE__
#include <sys/param.h>
#include <sys/mount.h>
#include <architecture/byte_order.h>
#define CONST_DIRENT_
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#ifdef __LITTLE_ENDIAN__
#define __BYTE_ORDER __LITTLE_ENDIAN
#else
#ifdef __BIG_ENDIAN__
#define __BYTE_ORDER __BIG_ENDIAN
#endif
#endif
#endif
#ifdef __FreeBSD__
#if __FreeBSD__ >= 2
#include <osreldate.h>
/* 800501 8.0-STABLE after change of the scandir(3) and alphasort(3)
   prototypes to conform to SUSv4. */
#if __FreeBSD_version >= 800501
#define CONST_DIRENT_ const
#else
#define CONST_DIRENT_ 
#endif
#else
#define CONST_DIRENT_
#endif
#define __BYTE_ORDER _BYTE_ORDER
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#define __BIG_ENDIAN _BIG_ENDIAN
#endif
#ifdef __linux
#define CONST_DIRENT_ const
#endif
#ifndef __BYTE_ORDER
#error __BYTE_ORDER not defined
#endif

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
#define no_warn_result_ void*ignore_result_;ignore_result_=(void*)(uintptr_t)
#endif

#ifdef __GNUC__
#define MB() do{ __asm__ __volatile__ ("" ::: "memory"); } while(0)
#else
#warning Check code for MB() on current platform
#define MB() 
#endif

extern long page_size;
#define P_ALIGN_(a) (((a)+page_size)&~(page_size-1))

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
