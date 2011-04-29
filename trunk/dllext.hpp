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

    This is an extension of the freeware Unrar C++ library (libunrar).
    It requires the complete unrar source package in order to compile.

    Unrar source may be used in any software to handle RAR archives
    without limitations free of charge, but cannot be used to re-create
    the RAR compression algorithm, which is proprietary. Distribution
    of modified Unrar source in separate form or as a part of other
    software is permitted, provided that it is clearly stated in
    the documentation and source comments that the code may not be used
    to develop a RAR (WinRAR) compatible archiver.
*/

#ifndef _UNRAR_DLLEXT_
#define _UNRAR_DLLEXT_

#include "raros.hpp"
#include "rardefs.hpp"
#include "version.hpp"
#include "fileext.hpp"
#include "dll.hpp"

#ifndef __cplusplus
/* These are defined here since headers.hpp can not be included from non C++ code */

#define  SIZEOF_MARKHEAD         7
#define  SIZEOF_OLDMHD           7
#define  SIZEOF_NEWMHD          13
#define  SIZEOF_OLDLHD          21
#define  SIZEOF_NEWLHD          32
#define  SIZEOF_SHORTBLOCKHEAD   7
#define  SIZEOF_LONGBLOCKHEAD   11
#define  SIZEOF_SUBBLOCKHEAD    14
#define  SIZEOF_COMMHEAD        13
#define  SIZEOF_PROTECTHEAD     26
#define  SIZEOF_AVHEAD          14
#define  SIZEOF_SIGNHEAD        15
#define  SIZEOF_UOHEAD          18
#define  SIZEOF_MACHEAD         22
#define  SIZEOF_EAHEAD          24
#define  SIZEOF_BEEAHEAD        24
#define  SIZEOF_STREAMHEAD      26

#define  PACK_VER               29
#define  PACK_CRYPT_VER         29
#define  UNP_VER                36
#define  CRYPT_VER              29
#define  AV_VER                 20
#define  PROTECT_VER            20

#define  MHD_VOLUME         0x0001U
#define  MHD_COMMENT        0x0002U
#define  MHD_LOCK           0x0004U
#define  MHD_SOLID          0x0008U
#define  MHD_PACK_COMMENT   0x0010U
#define  MHD_NEWNUMBERING   0x0010U
#define  MHD_AV             0x0020U
#define  MHD_PROTECT        0x0040U
#define  MHD_PASSWORD       0x0080U
#define  MHD_FIRSTVOLUME    0x0100U
#define  MHD_ENCRYPTVER     0x0200U

#define  LHD_SPLIT_BEFORE   0x0001U
#define  LHD_SPLIT_AFTER    0x0002U
#define  LHD_PASSWORD       0x0004U
#define  LHD_COMMENT        0x0008U
#define  LHD_SOLID          0x0010U

#define  LHD_WINDOWMASK     0x00e0U
#define  LHD_WINDOW64       0x0000U
#define  LHD_WINDOW128      0x0020U
#define  LHD_WINDOW256      0x0040U
#define  LHD_WINDOW512      0x0060U
#define  LHD_WINDOW1024     0x0080U
#define  LHD_WINDOW2048     0x00a0U
#define  LHD_WINDOW4096     0x00c0U
#define  LHD_DIRECTORY      0x00e0U

#define  LHD_LARGE          0x0100U
#define  LHD_UNICODE        0x0200U
#define  LHD_SALT           0x0400U
#define  LHD_VERSION        0x0800U
#define  LHD_EXTTIME        0x1000U
#define  LHD_EXTFLAGS       0x2000U

#define  SKIP_IF_UNKNOWN    0x4000U
#define  LONG_BLOCK         0x8000U

#define  EARC_NEXT_VOLUME   0x0001U /* not last volume */
#define  EARC_DATACRC       0x0002U /* store CRC32 of RAR archive (now used only in volumes) */
#define  EARC_REVSPACE      0x0004U /* reserve space for end of REV file 7 byte record */
#define  EARC_VOLNUMBER     0x0008U /* store a number of current volume */

enum HOST_SYSTEM {
   HOST_MSDOS=0,HOST_OS2=1,HOST_WIN32=2,HOST_UNIX=3,HOST_MACOS=4,
   HOST_BEOS=5,HOST_MAX
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RARArchiveList RARArchiveList;
typedef struct RARArchiveListEx RARArchiveListEx;

#ifdef __cplusplus
}
#endif

struct RARArchiveList 
{
   char         FileName[260];
   unsigned int Flags;
   unsigned int PackSize;
   unsigned int UnpSize;
   unsigned int HostOS;
   unsigned int FileCRC;
   unsigned int FileTime;
   unsigned int UnpVer;
   unsigned int Method;
   unsigned int FileAttr;
   unsigned int HeadSize;
   unsigned int NameSize;
   off_t        Offset;
   RARArchiveList* next;
};

struct RARArchiveListEx
{
   char         FileName[1024];
   wchar_t      FileNameW[1024];
   unsigned int Flags;
   unsigned int PackSize;
   unsigned int PackSizeHigh;
   unsigned int UnpSize;
   unsigned int UnpSizeHigh;
   unsigned int HostOS;
   unsigned int FileCRC;
   unsigned int FileTime;
   unsigned int UnpVer;
   unsigned int Method;
   unsigned int FileAttr;
   unsigned int HeadSize;
   unsigned int NameSize;
   off_t        Offset;
   RARArchiveListEx* next;
};

#ifdef __cplusplus
extern "C" {
#endif

HANDLE       PASCAL RARInitArchive(struct RAROpenArchiveData *ArchiveData, FileHandle);
HANDLE       PASCAL RARInitArchiveEx(struct RAROpenArchiveDataEx *ArchiveData, FileHandle);
int          PASCAL RARFreeArchive(HANDLE hArcData);
int          PASCAL RARListArchive(HANDLE hArcData, RARArchiveList* fList);
int          PASCAL RARListArchiveEx(HANDLE* hArcData, RARArchiveListEx* fList);
void         PASCAL RARExtractToStdout(const char* ArcName, const char* FileName, const char* Password, FileHandle);
void         PASCAL RARFreeList(RARArchiveList* fList);
void         PASCAL RARFreeListEx(HANDLE* hArcData, RARArchiveListEx* fList);
unsigned int PASCAL RARGetMainHeaderSize(HANDLE hArcData);
unsigned int PASCAL RARGetMainHeaderFlags(HANDLE hArcData);
off_t        PASCAL RARGetRawFileDataEnd(HANDLE hArcData);
FileHandle   PASCAL RARGetFileHandle(HANDLE hArcData);

#ifdef __cplusplus
}
#endif

#endif
