/*

   fileext.hpp

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

#ifndef _RAR_FILEEXT_
#define _RAR_FILEEXT__

#ifndef __cplusplus
#ifdef _WIN_ALL
typedef HANDLE FileHandle;
#define BAD_HANDLE INVALID_HANDLE_VALUE
#else
typedef FILE* FileHandle;
#define BAD_HANDLE NULL
#endif
#endif

#ifdef __cplusplus
class FileExt
{
  private:
    FileHandle hFile;
    bool LastWrite;
    FILE_HANDLETYPE HandleType;
    bool SkipClose;
    bool IgnoreReadErrors;
    bool NewFile;
    bool AllowDelete;
    bool AllowExceptions;
#ifdef _WIN_ALL
    bool NoSequentialRead;
#endif
  protected:
    bool OpenShared;
  public:
    char FileName[NM];
    wchar FileNameW[NM];
    FILE_ERRORTYPE ErrorType;
    uint CloseCount;
  public:
    FileExt();
    virtual ~FileExt();
    FileHandle GetHandle() {return(hFile);};
    void SetHandle(FileHandle FH) {hFile=FH;};
    void SkipHandle() {SkipClose=true;};
};
#endif

#endif
