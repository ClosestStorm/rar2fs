#ifndef _RAR_FILEEXT_
#define _RAR_FILEEXT__

#ifndef __cplusplus
#ifdef _WIN_32
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
#ifdef _WIN_32
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
