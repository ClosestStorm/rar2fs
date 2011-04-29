/*
   Copyright (C) 2009-2011 Hans Beckerus (hans.beckerus@gmail.com)

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

#ifndef _RAR_EXTRACTEXT_
#define _RAR_EXTRACTEXT_

class CmdExtractExt
{
  private:
    EXTRACT_ARC_CODE ExtractArchive(CommandData *Cmd, FileHandle = NULL);
    RarTime StartTime; // time when extraction started

    ComprDataIO DataIO;
    Unpack *Unp;
    unsigned long TotalFileCount;

    unsigned long FileCount;
    unsigned long MatchedArgs;
    bool FirstFile;
    bool AllMatchesExact;
    bool ReconstructDone;

    // If any non-zero solid file was successfully unpacked before current.
    // If true and if current encrypted file is broken, obviously
    // the password is correct and we can report broken CRC without
    // any wrong password hints.
    bool AnySolidDataUnpackedWell;

    char ArcName[NM];
    wchar ArcNameW[NM];

    wchar Password[MAXPASSWORD];
    bool PasswordAll;
    bool PrevExtracted;
    char DestFileName[NM];
    wchar DestFileNameW[NM];
    bool PasswordCancelled;
  public:
    CmdExtractExt();
    ~CmdExtractExt();
    void DoExtract(CommandData *Cmd, FileHandle = NULL);
    void ExtractArchiveInit(CommandData *Cmd,Archive &Arc);
    bool ExtractCurrentFile(CommandData *Cmd,Archive &Arc,size_t HeaderSize,
                            bool &Repeat);
    static void UnstoreFile(ComprDataIO &DataIO,int64 DestUnpSize);

    bool SignatureFound;
};

#endif
