/*
   Copyright (C) 2009-2011 Hans Beckerus (hans.beckerus@gmail.com)

   This is a C/C++ wrapper for the extension of the freeware Unrar C++
   library (libunrar). It is part of the extension itself. The wrapper
   can be used in source code written in C in order to access and 
   include the C++ library API.

   Unrar source may be used in any software to handle RAR archives
   without limitations free of charge, but cannot be used to re-create
   the RAR compression algorithm, which is proprietary. Distribution
   of modified Unrar source in separate form or as a part of other
   software is permitted, provided that it is clearly stated in
   the documentation and source comments that the code may not be used
   to develop a RAR (WinRAR) compatible archiver.

*/

#ifndef _UNRAR_DLLWRAPPER_
#define _UNRAR_DLLWRAPPER_

#ifndef __cplusplus
#include <wchar.h>
#endif

#include "dllext.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RARHeaderData RARHeaderData;
typedef struct RARHeaderDataEx RARHeaderDataEx;
typedef struct RAROpenArchiveData RAROpenArchiveData;
typedef struct RAROpenArchiveDataEx RAROpenArchiveDataEx;

#ifdef __cplusplus
}
#endif

#endif
