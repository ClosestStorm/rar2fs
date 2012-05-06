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
    It requires the complete unrar source in order to compile.

    Unrar source may be used in any software to handle RAR archives
    without limitations free of charge, but cannot be used to re-create
    the RAR compression algorithm, which is proprietary. Distribution
    of modified Unrar source in separate form or as a part of other
    software is permitted, provided that it is clearly stated in
    the documentation and source comments that the code may not be used
    to develop a RAR (WinRAR) compatible archiver.
*/

#include <iostream>
#include "rar.hpp"
#include "dllext.hpp"
#include "fileext.hpp"
using namespace std;

static int RarErrorToDll(int ErrCode);

struct DataSet
{
   CommandData Cmd;
   CmdExtract Extract;
   Archive Arc;
   int OpenMode;
   int HeaderSize;

   DataSet():Arc(&Cmd) {}
};

HANDLE PASCAL RARInitArchive(struct RAROpenArchiveData *r, FileHandle fh)
{
  RAROpenArchiveDataEx rx;
  memset(&rx,0,sizeof(rx));
  rx.ArcName=r->ArcName;
  rx.OpenMode=r->OpenMode;
  rx.CmtBuf=r->CmtBuf;
  rx.CmtBufSize=r->CmtBufSize;
  HANDLE hArc=RARInitArchiveEx(&rx, fh);
  r->OpenResult=rx.OpenResult;
  r->CmtSize=rx.CmtSize;
  r->CmtState=rx.CmtState;
  return(hArc);
}

HANDLE PASCAL RARInitArchiveEx(struct RAROpenArchiveDataEx *r, FileHandle fh)
{
  try
  {
    r->OpenResult=0;
    DataSet *Data=new DataSet;
    Data->Cmd.DllError=0;
    Data->OpenMode=r->OpenMode;
    Data->Cmd.FileArgs->AddString("*");

    char an[NM];
    if (r->ArcName==NULL && r->ArcNameW!=NULL)
    {
      WideToChar(r->ArcNameW,an,NM);
      r->ArcName=an;
    }

    Data->Cmd.AddArcName(r->ArcName,r->ArcNameW);
    Data->Cmd.Overwrite=OVERWRITE_ALL;
    Data->Cmd.VersionControl=1;
    ((FileExt*)&Data->Arc)->SetHandle(fh);
    ((FileExt*)&Data->Arc)->SkipHandle();
    if (!Data->Arc.IsArchive(false))
    {
      r->OpenResult=Data->Cmd.DllError!=0 ? Data->Cmd.DllError:ERAR_BAD_ARCHIVE;
      delete Data;
      return(NULL);
    }
    r->Flags=Data->Arc.NewMhd.Flags;
    Array<byte> CmtData;
    if (r->CmtBufSize!=0 && Data->Arc.GetComment(&CmtData,NULL))
    {
      r->Flags|=2;
      size_t Size=CmtData.Size()+1;
      r->CmtState=Size>r->CmtBufSize ? ERAR_SMALL_BUF:1;
      r->CmtSize=(uint)Min(Size,r->CmtBufSize);
      memcpy(r->CmtBuf,&CmtData[0],r->CmtSize-1);
      if (Size<=r->CmtBufSize)
        r->CmtBuf[r->CmtSize-1]=0;
    }
    else
      r->CmtState=r->CmtSize=0;
    if (Data->Arc.Signed)
      r->Flags|=0x20;
    Data->Extract.ExtractArchiveInit(&Data->Cmd,Data->Arc);
    return((HANDLE)Data);
  }
  catch (int ErrCode)
  {
    r->OpenResult=RarErrorToDll(ErrCode);
    return(NULL);
  }
}


int PASCAL RARFreeArchive(HANDLE hArcData)
{
  DataSet *Data=(DataSet *)hArcData;
  bool Success=Data==NULL ? false:true;
  delete Data;
  return(Success ? 0:ERAR_ECLOSE);
}


int PASCAL RARListArchiveEx(HANDLE* hArcData, RARArchiveListEx* N, off_t* FileDataEnd)
{
   uint FileCount=0;
   try {
      DataSet *Data=*(DataSet**)hArcData;
      Archive& Arc = Data->Arc;
      while(Arc.ReadHeader()>0)
      {
         int HeaderType=Arc.GetHeaderType();
         if (HeaderType==ENDARC_HEAD)
         {
            break;
         }
         switch(HeaderType)
         {
            case FILE_HEAD:
               if (FileCount)
               {
                  N->next = new RARArchiveListEx;
                  N = N->next;
               }
               FileCount++;

               IntToExt(Arc.NewLhd.FileName,Arc.NewLhd.FileName);
               strncpyz(N->FileName,Arc.NewLhd.FileName,ASIZE(N->FileName));
               if (*Arc.NewLhd.FileNameW)
                  wcsncpy(N->FileNameW,Arc.NewLhd.FileNameW,ASIZE(N->FileNameW));
               else
               {
#ifdef _WIN_ALL
                  char AnsiName[NM];
                  OemToChar(Arc.NewLhd.FileName,AnsiName);
                  CharToWide(AnsiName,N->FileNameW);
#else
                  CharToWide(Arc.NewLhd.FileName,N->FileNameW);
#endif
               }

               N->Flags = Arc.NewLhd.Flags;
               N->PackSize = Arc.NewLhd.PackSize;
               N->PackSizeHigh = Arc.NewLhd.HighPackSize;
               N->UnpSize = Arc.NewLhd.UnpSize;
               N->UnpSizeHigh = Arc.NewLhd.HighUnpSize;
               N->HostOS = Arc.NewLhd.HostOS;
               N->FileCRC = Arc.NewLhd.FileCRC;
               N->FileTime = Arc.NewLhd.FileTime;
               N->UnpVer = Arc.NewLhd.UnpVer;
               N->Method = Arc.NewLhd.Method;
               N->FileAttr = Arc.NewLhd.FileAttr;
               N->HeadSize = Arc.NewLhd.HeadSize;
               N->NameSize = Arc.NewLhd.NameSize;
               N->Offset = Arc.CurBlockPos;

               if (FileDataEnd) 
                  *FileDataEnd = Arc.NextBlockPos;
               break;

            default:
               break;
         }
         Arc.SeekToNext();
      }
      N->next = NULL;
   }
   catch (int ErrCode)
   {
      N->next = NULL;
      cerr << "RarListArchiveEx() caught error "
           << RarErrorToDll(ErrCode)
           << endl;
      return 0;
   }
   return FileCount;
}


void PASCAL RARFreeListEx(RARArchiveListEx* L)
{
   RARArchiveListEx* N = L?L->next:NULL;
   while (N)
   {
       RARArchiveListEx* tmp = N;
       N = N->next;
       delete tmp;
   }
}


unsigned int PASCAL RARGetMainHeaderSize(HANDLE hArcData)
{
   DataSet *Data=(DataSet*)hArcData;
   return Data->Arc.NewMhd.HeadSize;
}


unsigned int PASCAL RARGetMainHeaderFlags(HANDLE hArcData)
{
   DataSet *Data=(DataSet*)hArcData;
   return Data->Arc.NewMhd.Flags;
}


FileHandle PASCAL RARGetFileHandle(HANDLE hArcData)
{
   DataSet *Data=(DataSet*)hArcData;
   return Data->Arc.GetHandle()!=BAD_HANDLE?Data->Arc.GetHandle():NULL;
}


void PASCAL RARNextVolumeName(char* arch, bool oldstylevolume)
{
   NextVolumeName(arch, NULL, 0, oldstylevolume);
}


static int RarErrorToDll(int ErrCode)
{
#if RARVER_MAJOR > 4 || ( RARVER_MAJOR == 4 && RARVER_MINOR >= 20 )
   switch(ErrCode)
   {
      case RARX_FATAL:
         return(ERAR_EREAD);
      case RARX_CRC:
         return(ERAR_BAD_DATA);
      case RARX_WRITE:
         return(ERAR_EWRITE);
      case RARX_OPEN:
         return(ERAR_EOPEN);
      case RARX_CREATE:
         return(ERAR_ECREATE);
      case RARX_MEMORY:
         return(ERAR_NO_MEMORY);
      case RARX_SUCCESS:
         return(0);
      default:
         ;
   }
#else
    switch(ErrCode)
    {
       case FATAL_ERROR:
          return(ERAR_EREAD);
       case CRC_ERROR:
          return(ERAR_BAD_DATA);
       case WRITE_ERROR:
          return(ERAR_EWRITE);
       case OPEN_ERROR:
          return(ERAR_EOPEN);
       case CREATE_ERROR:
          return(ERAR_ECREATE);
       case MEMORY_ERROR:
          return(ERAR_NO_MEMORY);
       case SUCCESS:
          return(0);
       default:
          ;
    }
#endif
    return(ERAR_UNKNOWN);
}

