/*
   Copyright (C) 2009-2011 Hans Beckerus (hans.beckerus@gmail.com)

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
#include "extractext.hpp"
#include "dllext.hpp"
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

struct DataSetEx
{
   DataSet* DataSetHandle;

   /* These are extensions to DataSet which are only updated
    * after RARListArchive()/Ex() has been called successfully. */
   uint64 RawFileDataEnd;

   DataSetEx(DataSet* H)
   {
      DataSetHandle = H;
      RawFileDataEnd=0;
   }
   DataSet* DataSetH() { return DataSetHandle; }
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


int PASCAL RARListArchiveEx(HANDLE* hArcData, RARArchiveListEx* N)
{
   uint FileCount=0;
   try {
      DataSet* H = *(DataSet**)hArcData;
      DataSetEx* HH = new DataSetEx(H);
      *(DataSetEx**)hArcData = HH;
      Archive& Arc = H->Arc;
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
                  wcsncpy(N->FileNameW,Arc.NewLhd.FileNameW,sizeof(N->FileNameW));
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

               HH->RawFileDataEnd = Arc.NextBlockPos;
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


void PASCAL RARFreeListEx(HANDLE* hArcData, RARArchiveListEx* L)
{
   DataSetEx* H = *(DataSetEx**)hArcData;
   *(DataSet**)hArcData = H->DataSetH();
   delete H;
   RARArchiveListEx* N = L?L->next:NULL;
   while (N)
   {
       RARArchiveListEx* tmp = N;
       N = N->next;
       delete tmp;
   }
}


void PASCAL RARExtractToStdout(const char* ArcName, const char* FileName, const char* Password, FileHandle FH)
{
   CommandData Cmd;
   Cmd.AddArcName((char*)ArcName, NULL);
   Cmd.FileArgs->AddString(FileName);

   GetWideName(Password,NULL,Cmd.Password,ASIZE(Cmd.Password));

   // P
   Cmd.Command[0]='P';

   // -inul
   InitConsoleOptions(MSG_NULL,false);
   ErrHandler.SetSilent(true);

   CmdExtractExt Extract;
   Extract.DoExtract(&Cmd, FH);
}


unsigned int PASCAL RARGetMainHeaderSize(HANDLE hArcData)
{
   DataSetEx* H = (DataSetEx*)hArcData;
   return H->DataSetH()->Arc.NewMhd.HeadSize;
}


unsigned int PASCAL RARGetMainHeaderFlags(HANDLE hArcData)
{
   DataSetEx* H = (DataSetEx*)hArcData;
   return H->DataSetH()->Arc.NewMhd.Flags;
}


off_t PASCAL RARGetRawFileDataEnd(HANDLE hArcData)
{
   DataSetEx* HH = (DataSetEx*)hArcData;
   return HH->RawFileDataEnd;
}


FileHandle PASCAL RARGetFileHandle(HANDLE hArcData)
{
   DataSetEx* H = (DataSetEx*)hArcData;
   return H->DataSetH()->Arc.GetHandle()!=BAD_HANDLE?H->DataSetH()->Arc.GetHandle():NULL;
}


static int RarErrorToDll(int ErrCode)
{
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
         return(ERAR_UNKNOWN);
   }
}

