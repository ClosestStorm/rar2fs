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

#include <platform.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <fuse.h>
#include <fcntl.h>
#include <getopt.h>
#include <syslog.h>
#ifdef HAVE_MMAP
# include <sys/mman.h>
#endif
#include <limits.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>
#ifdef HAVE_SCHED_H
# include <sched.h>
#endif
#include <assert.h>
#include "version.h"
#include "debug.h"
#include "index.h"
#include "dllwrapper.h"
#include "filecache.h"
#include "iobuffer.h"
#include "configdb.h"
#include "sighandler.h"

typedef struct dir_entry_list dir_entry_list_t;
struct dir_entry_list {
        struct dir_entry {
                char* name;
                struct stat* st;
                int valid;
        } entry;
        dir_entry_list_t* next;
};

#define DIR_LIST_RST(l) \
{  (l)->next = NULL;\
   (l)->entry.name = NULL;\
   (l)->entry.st = NULL;}
#define DIR_ENTRY_ADD(l, n, s) \
{  (l)->next = malloc(sizeof(dir_entry_list_t));\
   if ((l)->next){\
      (l)=(l)->next;\
      (l)->entry.name=strdup(n);\
      (l)->entry.st=(s);\
      (l)->entry.valid=1; /* assume entry is valid */ \
      (l)->next = NULL;}}
#define DIR_LIST_EMPTY(l) (!(l)->next)
#define DIR_LIST_FREE(l)\
{  dir_entry_list_t* next = (l)->next;\
   while(next){\
      dir_entry_list_t* tmp = next;\
      next = next->next;\
      free(tmp->entry.name);\
      free(tmp);}}

#define E_TO_MEM 0
#define E_TO_TMP 1

#define MOUNT_FOLDER  0
#define MOUNT_ARCHIVE 1

typedef struct
{
   FILE* fp;
   off_t pos;
} VolHandle;

typedef struct IOContext IOContext;
typedef union
{
  int fd;
  FILE* fp;
  IOContext* context;
  uint64_t bits;
} IOFileHandle;

#define FH_ZERO(fh)            (((IOFileHandle*)(fh))->bits=0)
#define FH_ISSET(fh)           (fh)
#define FH_ISADDR(fh)          (FH_ISSET(fh) && !FH_ISFP(fh))
#define FH_ISFP(fh)            (FH_ISSET(fh) && ((fh)&0x1))
#define FH_SETFP(fh, v)        {((IOFileHandle*)(fh))->bits = ((uint64_t)(size_t)(v))|0x1ULL;}
#define FH_SETFD(fh, v)        FH_SETFP(fh, ((v)<<1))
#define FH_SETCONTEXT(fh, v)   {((IOFileHandle*)(fh))->bits = ((uint64_t)(size_t)(v))&~0x1ULL;}
#define FH_TOFP(fh)            ((FILE*)(size_t)(((uint64_t)(size_t)(((IOFileHandle)(fh)).fp))&~0x1ULL))
#define FH_TOFD(fh)            (((IOFileHandle)(fh)).fd>>1)
#define FH_TOCONTEXT(fh)       (((IOFileHandle)(fh)).context)

struct IOContext
{
   IOFileHandle fh;
   off_t pos;
   IoBuf* buf;
   pid_t pid;
   unsigned int seq;
   short vno;
   dir_elem_t* entry_p;
   VolHandle* volHdl;
   int pfd1[2];
   int pfd2[2];
   volatile int terminate;
   pthread_t thread;
   /* mmap() data */
   void* mmap_addr;
   FILE* mmap_fp;
   int mmap_fd;
};

#define WAIT_THREAD(pfd) \
{\
    int fd = pfd[0];\
    int nfsd = fd+1;\
    fd_set rd;\
    FD_ZERO(&rd);\
    FD_SET(fd, &rd);\
    int retval = select(nfsd, &rd, NULL, NULL, NULL); \
    if (retval == -1) {\
       if (errno!=EINTR) perror("select()");\
    } else if (retval) {\
       /* FD_ISSET(0, &rfds) will be true. */\
       char buf[2];\
       NO_UNUSED_RESULT read(fd, buf, 1); /* consume byte */\
       printd(4, "%lu thread wakeup (%d, %u)\n", (unsigned long)pthread_self(), retval, (int)buf[0]);\
   }\
   else perror("select()");\
}

#define WAKE_THREAD(pfd,op) \
{\
   /* Wakeup the reader thread */ \
   char buf[2]; \
   buf[0] = (op); \
   if (write((pfd)[1], buf, 1) != 1) perror("write"); \
}

long page_size = 0;
static int mount_type;
dir_entry_list_t arch_list_root;    /* internal list root */
dir_entry_list_t* arch_list = &arch_list_root;
pthread_attr_t thread_attr;
unsigned int rar2_ticks;

static void sync_dir(const char *dir);

static int
extract_rar(char* arch, const char* file, char* passwd, FILE* fp, void* arg);

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void*
extract_to(const char* file, off_t sz, FILE* fp, const dir_elem_t* entry_p, int oper)
{
  ENTER_("%s", file);

  int out_pipe[2] = {-1,-1};
  if(pipe(out_pipe) != 0 ) {          /* make a pipe */
    return MAP_FAILED;
  }

  printd(3, "Extracting %llu bytes resident in %s\n", sz, entry_p->rar_p);
  pid_t pid = fork();
  if (pid == 0)
  {
     close(out_pipe[0]);
     (void)extract_rar(entry_p->rar_p, file, entry_p->password_p, fp, (void*)(uintptr_t)out_pipe[1]);
     close(out_pipe[1]);
     _exit(EXIT_SUCCESS);
  }
  else if (pid < 0)
  {
     close(out_pipe[0]);
     close(out_pipe[1]);
     /* The fork failed. Report failure. */
     return MAP_FAILED;
  }

  close(out_pipe[1]);

  FILE* tmp = NULL;
  char* buffer = malloc(sz);
  if (!buffer)
     return MAP_FAILED;
  if (oper == E_TO_TMP)
  {
     tmp = tmpfile();
  }
  off_t off = 0;
  do
  {
     ssize_t n = read(out_pipe[0], buffer+off, sz-off); /* read from pipe into buffer */
     if (n == -1)
     {
        if (errno == EINTR)
           continue;
        perror("read");
        free(buffer);
        buffer = MAP_FAILED;
        break;
     }
     off += n;
  } while (off != sz);

  printd(4, "Read %llu bytes from PIPE %d\n", off, out_pipe[0]);
  close(out_pipe[0]);

  if (tmp && (buffer != MAP_FAILED))
  {
      if (!fwrite(buffer, sz, 1, tmp))
      {
        fclose(tmp);
        tmp = MAP_FAILED;
      }
      else fseeko(tmp, 0, SEEK_SET);
      free(buffer);
      return tmp;
  }
  return buffer;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/

#define UNRAR_ unrar_path, unrar_path

static FILE*
popen_(const dir_elem_t* entry_p, pid_t* cpid, void** mmap_addr, FILE** mmap_fp, int* mmap_fd)
{
   char* maddr = MAP_FAILED;
   FILE* fp = NULL;
   char* unrar_path = OBJ_STR(OBJ_UNRAR_PATH,0);
   if (entry_p->flags.mmap)
   {
       int fd = open(entry_p->rar_p, O_RDONLY);
       if (fd != -1)
       {
          if (entry_p->flags.mmap==2)
          {
#ifdef HAVE_FMEMOPEN
             maddr = extract_to(entry_p->file_p, entry_p->msize, NULL, entry_p, E_TO_MEM);
             if (maddr != MAP_FAILED)
                fp = fmemopen(maddr, entry_p->msize, "r");
#else
             fp = extract_to(entry_p->file_p, entry_p->msize, NULL, entry_p, E_TO_TMP);
             if (fp == MAP_FAILED)
             {
                fp = NULL;
                printd(1, "Extract to tmpfile failed\n");
             }
#endif
          }
          else
          {
#ifdef HAVE_FMEMOPEN
             maddr = mmap(0, P_ALIGN_(entry_p->msize), PROT_READ, MAP_SHARED, fd, 0);
             if (maddr != MAP_FAILED)
                fp = fmemopen(maddr+entry_p->offset, entry_p->msize-entry_p->offset, "r");
#else
             fp = fopen(entry_p->rar_p, "r");
             if (fp) fseeko(fp, entry_p->offset, SEEK_SET);
#endif
          }
       }
       if (fp)
       {
          *mmap_addr = maddr;
          *mmap_fp = fp;
          *mmap_fd = fd;
       }
       else
       {
          if (maddr != MAP_FAILED)
          {
             if(entry_p->flags.mmap==1) munmap(maddr, P_ALIGN_(entry_p->msize));
             else free(maddr);
          }
          if (fd != -1) close(fd);
          return NULL;
       }
   }
   int pfd[2];
   pid_t pid;
   if (pipe(pfd) == -1) { perror("pipe"); return NULL; }

   pid = fork();
   if (pid == 0)
   {
      int ret;
      setpgid(getpid(), 0);
      close(pfd[0]);          /* Close unused read end */

      /* This is the child process.  Execute the shell command. */
      if (unrar_path && !entry_p->flags.mmap)
      {
         fflush(stdout);
         dup2(pfd[1], STDOUT_FILENO);
         /* anything sent to stdout should now go down the pipe */
         if (entry_p->password_p)
         {
            char parg[MAXPASSWORD+2];
            printd(3, parg, MAXPASSWORD+2, "%s%s", "-p", entry_p->password_p);
            execl(UNRAR_, parg, "P", "-inul", entry_p->rar_p, entry_p->file_p, NULL);
         }
         else
         {
            execl(UNRAR_, "P", "-inul", entry_p->rar_p, entry_p->file_p, NULL);
         }
         /* Should never come here! */
         _exit (EXIT_FAILURE);
      }
      ret = extract_rar(entry_p->rar_p, entry_p->flags.mmap ? basename(entry_p->name_p) : entry_p->file_p, entry_p->password_p, fp, (void*)(uintptr_t)pfd[1]);
      close(pfd[1]);
      _exit(ret);
   }
   else if (pid < 0)
      /* The fork failed.  Report failure.  */
      return NULL;
   /* This is the parent process. */
   close(pfd[1]);          /* Close unused write end */
   *cpid = pid;
   return fdopen(pfd[0], "r");
}

#undef UNRAR_

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
pclose_(FILE* fp, pid_t pid)
{
   int status;

   fclose(fp);
   killpg(pid, SIGKILL);
   /* Sync */
   while (waitpid(pid, &status, WNOHANG|WUNTRACED) != -1);
   if (WIFEXITED(status))
   {
       return WEXITSTATUS(status);
   }
   return 0;
}

/* Size of file in first volume in which it exists */
#define VOL_FIRST_SZ op->entry_p->vsize

/* Size of file in the following volume(s) (if situated in more than one) */
#define VOL_NEXT_SZ op->entry_p->vsize_next

/* Size of file data in first volume file */
#define VOL_REAL_SZ op->entry_p->vsize_real

/* Calculate volume number base offset using archived file offset */
#define VOL_NO(off) (off < VOL_FIRST_SZ ? 0 : ((offset-VOL_FIRST_SZ) / VOL_NEXT_SZ)+1)

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static char*
get_vname(int t, const char* str, int vol, int len, int pos)
{
   ENTER_("%s   vol=%d, len=%d, pos=%d", str, vol, len, pos);
   char* s = strdup(str);
   if (!vol) return s;
   if (t)
   {
      char f[16];
      char f1[16];
      sprintf(f1, "%%0%dd", len);
      sprintf(f, f1, vol);
      strncpy(&s[pos], f, len);
   }
   else
   {
      char f[16];
      if (vol==1)
      {
         sprintf(f, "%s", "ar");
      }
      else if (vol <= 101)
      {
         sprintf(f, "%02d", (vol-2));
      }
      /* Possible, but unlikely */
      else
      {
         sprintf(f, "%c%02d", 'r'+(vol-2)/100, (vol-2)%100);
         --pos;
         ++len;
      }
      strncpy(&s[pos], f, len);
   }
   return s;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
lread_raw(char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
   int n = 0;
   IOContext* op = FH_TOCONTEXT(fi->fh);
   if (!op) return -EACCES;

   ++op->seq;
   printd(3, "PID %05d calling %s(), seq = %d, offset=%llu\n", getpid(), __func__, op->seq, offset);

   off_t chunk;
   int tot = 0;
   int force_seek = 0;

   /* Handle the case when a user tries to read outside file size.
    * This is especially important to handle here since last file in a
    * volume usually is of much less size than the others and conseqently
    * the chunk based calculation will not detect this. */
   if ((offset + size) > op->entry_p->stat.st_size)
   {
      if (offset > op->entry_p->stat.st_size) return 0; /* come on!? */
      size = op->entry_p->stat.st_size - offset;
   }

   while (size)
   {
      FILE* fp;
      off_t src_off = 0;
      VolHandle* vol_p = NULL;
      if (VOL_FIRST_SZ)
      {
         chunk = offset < VOL_FIRST_SZ ? VOL_FIRST_SZ-offset : (VOL_NEXT_SZ) - ((offset-VOL_FIRST_SZ)%(VOL_NEXT_SZ));
         {
            /* keep current open file */
            int vol = VOL_NO(offset);
            if (vol!=op->vno)
            {
               /* close/open */
               op->vno = vol;
               if (op->volHdl && op->volHdl[vol].fp)
               {
                  vol_p = &op->volHdl[vol];
                  fp = vol_p->fp;
                  src_off = VOL_REAL_SZ - chunk;
                  if (src_off != vol_p->pos) force_seek = 1;
               }
               else
               {
                  /* It is advisable to return 0 (read fail) here rather
                   * than -errno at failure.
                   * Some media players tend to react "better" on that and
                   * terminate playback as expected. */
                  char* tmp = get_vname(
                     op->entry_p->vtype, op->entry_p->rar_p, op->vno+op->entry_p->vno_base, op->entry_p->vlen, op->entry_p->vpos);
                  if (tmp)
                  {
                     printd(3, "Opening %s\n", tmp);
                     fp = fopen(tmp, "r");
                     free(tmp);
                     if (fp == NULL)
                     {
                        perror("open");
                        return 0;
                     }
                     fclose(FH_TOFP(op->fh));
                     FH_SETFP(&op->fh, fp);
                     force_seek = 1;
                  }
                  else return 0;
               }
            }
            else {
               if (op->volHdl && op->volHdl[vol].fp) fp = op->volHdl[vol].fp;
               else fp = FH_TOFP(op->fh);
            }
         }
         if (force_seek || offset != op->pos)
         {
            src_off = VOL_REAL_SZ - chunk;
            printd(3, "SEEK src_off = %llu, VOL_REAL_SZ = %llu\n", src_off, VOL_REAL_SZ);
            fseeko(fp, src_off, SEEK_SET);
            force_seek = 0;
         }
         printd(3, "size = %zu, chunk = %llu\n", size, chunk);
         chunk = size < chunk ? size : chunk;
      }
      else
      {
         fp = FH_TOFP(op->fh);
         chunk = size;
         if (!offset || offset != op->pos)
         {
            src_off = offset + op->entry_p->offset;
            printd(3, "SEEK src_off = %llu\n", src_off);
            fseeko(fp, src_off, SEEK_SET);
         }
      }
      n = fread(buf, 1, (size_t)chunk, fp);
      printd(3, "Read %d bytes from vol=%d, base=%d\n", n, op->vno, op->entry_p->vno_base);
      if (n!=chunk)
      {
         size = n;
      }

      size-=n;
      offset+=n;
      buf+=n;
      tot+=n;
      op->pos=offset;
      if (vol_p) vol_p->pos+=n;
   }
   return tot;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
lread_rar(char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
   IOContext* op = FH_TOCONTEXT(fi->fh);
   if (!op) return -EIO;

   ++op->seq;
   printd(3, "PID %05d calling %s(), seq = %d, size=%zu, offset=%llu/%llu\n", getpid(), __func__, op->seq, size, offset, op->pos);

   int n = 0;
   errno = 0;

   /* Check for exception case */
   if (offset != op->pos)
   {
      if (op->buf->idx.data_p != MAP_FAILED &&
         offset >= op->buf->idx.data_p->head.offset)
      {
         off_t o = op->buf->idx.data_p->head.offset;
         size_t s = op->buf->idx.data_p->head.size;
         off_t off = (offset - o);
         size = (off + size) > s ? size - ((off + size) - s) : size;
         printd(3, "Copying data from preloaded index information @ %llu\n", offset);
         if (op->buf->idx.mmap)
         {
            memcpy(buf, op->buf->idx.data_p->bytes+off, size);
            return size;
         }
         else return pread(op->buf->idx.fd, buf, size, off+sizeof(IdxHead));
      }
      /* If the initial read is not according to expected offset, return best effort.
       * That is, return all zeros according to size. If this approach is causing
       * problems for some media players turn this feature off. */
      if (((offset - op->pos)/(op->entry_p->stat.st_size*1.0)*100)>60 ||
         (offset + size) > op->entry_p->stat.st_size)
      {
         if ((offset + size) > op->entry_p->stat.st_size)
         {
            size = offset < op->entry_p->stat.st_size ? op->entry_p->stat.st_size - offset : 0;
         }
         if (size) memset(buf,0,size);
         return size;
      }
      /* Check for backward read */
      if (offset < op->pos)
      {
         off_t delta = op->pos - offset;
         if (delta <= IOB_HIST_SZ)
         {
            size_t pos = (op->buf->ri-delta)&(IOB_SZ-1);
            size_t chunk = op->pos > IOB_HIST_SZ ? IOB_HIST_SZ : op->pos;
            chunk = chunk > size ? size : size - chunk;
            n += copyFrom(buf, op->buf, chunk, pos);
            size -= n;
            buf+=n;
            offset+=n;
         }
         else
         {
            printd(1, "read: I/O error\n");
            return -EIO;
         }
      }
   }
   /* Check if we need to wait for data to arrive.
    * This should not be happening frequently. If it does it is an
    * indication that the I/O buffer is set too small. */
   size = (offset+size) > op->entry_p->stat.st_size ? op->entry_p->stat.st_size - offset : size;
   if ((offset+size)>op->buf->offset)
   {
      /* This is another hack! Some media players, especially VLC,
       * seems to be buggy and many times requests a second read
       * far beyond the current offset. This is rendering the
       * stream completely useless for continued playback.
       * By checking the distance of the jump this effect can in
       * most cases be worked around. For VLC this will result in
       * an error message being displayed. But, playback can
       * usually be started at the second attempt. */
#if 1
      if (((offset+size)-op->buf->offset) > 100000000 &&
          op->buf->idx.data_p == MAP_FAILED) return -EIO;
#endif
      if (!op->terminate) /* make sure thread is running */
      {
         /* Take control of reader thread */
         do {
             errno = 0;
             WAKE_THREAD(op->pfd1, 2);
             WAIT_THREAD(op->pfd2);
         } while (errno==EINTR);
         while(!feof(FH_TOFP(op->fh)) && (offset+size)>op->buf->offset)
         {
            /* consume buffer */
            op->pos += op->buf->used;
            op->buf->ri = op->buf->wi;
            (void)readTo(op->buf, FH_TOFP(op->fh), IOB_NO_HIST);
         }
            op->buf->ri += (offset - op->pos);
            op->buf->ri &= (IOB_SZ-1);
            op->buf->used -= (offset - op->pos);
            op->pos = offset;
      }
   }

   if (size)
   {
      int off = offset - op->pos;
      n += readFrom(buf, op->buf, size, off);
      op->pos += (off + n);
      if (!op->terminate) WAKE_THREAD(op->pfd1, 0);
   }
   printd(3, "read: RETURN %d\n", errno ? -errno : n);

   if (errno) perror("read");
   return errno ? -errno : n;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int lflush(const char *path,
                  struct fuse_file_info *fi)
{
   ENTER_("%s", path);
   (void)fi; /* touch */
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int lrelease(const char *path,
                    struct fuse_file_info *fi)
{
   ENTER_("%s", path);
   close(FH_TOFD(fi->fh));
   FH_ZERO(&fi->fh);
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
lread(const char *path,
      char *buffer, size_t size, off_t offset,
      struct fuse_file_info *fi)
{
   ENTER_("%s   size = %zu, offset = %llu", path, size, offset);
   return pread(FH_TOFD(fi->fh), buffer, size, offset);
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
lopen(const char *path,
      struct fuse_file_info *fi)
{
   ENTER_("%s", path);
   int fd = open(path, fi->flags);
   if (fd == -1)
      return -errno;
   FH_SETFD(&fi->fh, fd);
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
#if !defined ( DEBUG_ ) || DEBUG_ < 5
#define dump_stat(s)
#else
#define DUMP_STATO_(m) fprintf(stderr, "%10s = %o (octal)\n", #m , (unsigned int)stbuf->m)
#define DUMP_STAT4_(m) fprintf(stderr, "%10s = %u\n", #m , (unsigned int)stbuf->m)
#define DUMP_STAT8_(m) fprintf(stderr, "%10s = %llu\n", #m , (unsigned long long)stbuf->m)
static void
dump_stat(struct stat* stbuf)
{
   fprintf(stderr, "struct stat {\n");
   DUMP_STAT4_(st_dev);
   DUMP_STATO_(st_mode);
   DUMP_STAT4_(st_nlink);
   if (sizeof(stbuf->st_ino) > 4)
      DUMP_STAT8_(st_ino);
   else
      DUMP_STAT4_(st_ino);
   DUMP_STAT4_(st_uid);
   DUMP_STAT4_(st_gid);
   DUMP_STAT4_(st_rdev);
   if (sizeof(stbuf->st_size) > 4)
      DUMP_STAT8_(st_size);
   else
      DUMP_STAT4_(st_size);
#ifdef HAVE_STRUCT_STAT_ST_BLOCKS
   DUMP_STAT4_(st_blocks);
#endif
   DUMP_STAT4_(st_blksize);
#ifdef __APPLE__
   DUMP_STAT4_(st_gen);
#endif
   fprintf(stderr, "}\n");
}
#undef DUMP_STAT4_
#undef DUMP_STAT8_
#endif

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
collect_files(const char* arch, dir_entry_list_t* list)
{
   RAROpenArchiveDataEx d;
   int files = 0;

   memset(&d, 0, sizeof(RAROpenArchiveDataEx));
   d.ArcName=strdup(arch);
   d.OpenMode=RAR_OM_LIST;

   while (1)
   {
      HANDLE hdl = RAROpenArchiveEx(&d);
      if (d.OpenResult)
      {
         break;
      }
      if (!(d.Flags & MHD_VOLUME))
      {
         files = 1;
         DIR_ENTRY_ADD(list, d.ArcName, NULL);
         break;
      }
      if (!files && !(d.Flags & MHD_FIRSTVOLUME))
      {
         break;
      }
      ++files;
      DIR_ENTRY_ADD(list, d.ArcName, NULL);
      RARCloseArchive(hdl);
      RARNextVolumeName(d.ArcName, !(d.Flags&MHD_NEWNUMBERING));
   }
   free(d.ArcName);
   return files;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_getattr(const char *path, struct stat *stbuf)
{
   ENTER_("%s", path);

   pthread_mutex_lock(&file_access_mutex);
   if (cache_path(path, stbuf))
   {
      pthread_mutex_unlock(&file_access_mutex);
      dump_stat(stbuf);
      return 0;
   }
   pthread_mutex_unlock(&file_access_mutex);

   /* There was a cache miss and the file could not be found locally!
    * This is bad! To make sure the files does not really exist all
    * rar archives need to be scanned for a matching file = slow! */
   CHK_FILTER;
   char* dir = alloca(strlen(path)+1);
   strcpy(dir, path);
   sync_dir(dirname(dir));
   pthread_mutex_lock(&file_access_mutex);
   dir_elem_t* e_p = cache_path_get(path);
   pthread_mutex_unlock(&file_access_mutex);
   if (e_p)
   {
      memcpy(stbuf, &e_p->stat, sizeof(struct stat));
      dump_stat(stbuf);
      return 0;
   }
   return -ENOENT;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_getattr2(const char *path, struct stat *stbuf)
{
   ENTER_("%s", path);

   pthread_mutex_lock(&file_access_mutex);
   if (cache_path(path, stbuf))
   {
      pthread_mutex_unlock(&file_access_mutex);
      dump_stat(stbuf);
      return 0;
   }
   pthread_mutex_unlock(&file_access_mutex);
   return -ENOENT;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
int
is_rxx_vol(const char* name)
{
   size_t len = strlen(name);
   {
      if (name[len-4] == '.' &&
         name[len-3] >= 'r' &&
         isdigit(name[len-2]) &&
         isdigit(name[len-1]))
      {
         /* This seems to be a classic .rNN rar volume file.
          * Let the rar header be the final judge. */
         return 1;
      }
   }
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
get_vformat(const char* s, int t, int* l, int* p)
{
   int len = 0;
   int pos = 0;
   int vol = 0;
   if (t)
   {
      int dot = 0;
      len = strlen(s) - 1;
      while (dot < 2 && len>=0)
      {
         if (s[len--]=='.') ++dot;
      }
      if (len>=0)
      {
         pos = len+1;
         len = strlen(&s[pos]);
         if (len >= 10)
         {
            pos += 5;      /* - ".part" */
            len -= 9;      /* - ".ext" */
            vol = strtoul(&s[pos], NULL, 10);
         }
      }
   }
   else
   {
      int dot = 0;
      len = strlen(s) - 1;
      while (dot < 1 && len>=0)
      {
         if (s[len--]=='.') ++dot;
      }
      if (len>=0)
      {
         pos = len+1;
         len = strlen(&s[pos]);
         if (len == 4)
         {
            pos+=2;
            len-=2;
            if (!strncmp(&s[pos-1], "rar", 3))
            {
               vol = 1;
            }
            else
            {
               errno = 0;
               vol = strtoul(&s[pos], NULL, 10) + 2 +
                  (100*(s[pos-1]-'r'));   /* Possible, but unlikely */
               vol = errno ? 0 : vol;
            }
         }
      }
   }
   if (l) *l = vol ? len : 0;
   if (p) *p = vol ? pos : 0;
   return vol ? vol : 1;
}

#define IS_RAR(s)  (!strcmp(&(s)[strlen((s))-4], ".rar"))
#define IS_RXX(s)  (is_rxx_vol(s))
#if 0
#define IS_RAR_DIR(l) ((l)->HostOS != HOST_UNIX && (l)->HostOS != HOST_BEOS \
   ? (l)->FileAttr & 0x10 : (l)->FileAttr & S_IFDIR)
#else
/* Unless we need to support RAR version < 2.0 this is good enough */
#define IS_RAR_DIR(l) (((l)->Flags&LHD_DIRECTORY)==LHD_DIRECTORY)
#endif
#define GET_RAR_MODE(l) ((l)->HostOS != HOST_UNIX && (l)->HostOS != HOST_BEOS \
   ? IS_RAR_DIR(l) ? (S_IFDIR|0777) : (S_IFREG|0666) : (l)->FileAttr)
#define GET_RAR_SZ(l) (IS_RAR_DIR(l) ? 4096 : (((l)->UnpSizeHigh * 0x100000000ULL) | (l)->UnpSize))
#define GET_RAR_PACK_SZ(l) (IS_RAR_DIR(l) ? 4096 : (((l)->PackSizeHigh * 0x100000000ULL) | (l)->PackSize))

/*!
 ****************************************************************************
 *
 ****************************************************************************/
static int CALLBACK
extract_callback(UINT msg, LPARAM UserData, LPARAM P1, LPARAM P2)
{
   if (msg == UCM_PROCESSDATA)
   {
      /* We do not need to handle the case that not all data is written
       * after return from write() since the pipe is not opened using the
       * O_NONBLOCK flag. */
      int fd = UserData ? UserData : STDOUT_FILENO;
      if (write(fd, (void*)P1, P2) == -1)
      {
         /* Do not treat EPIPE as an error. It is the normal case
          * when the process is terminted, ie. the pipe is closed
          * since SIGPIPE is not handled. */
         if (errno != EPIPE)
            perror("write");
         return -1;
      }
   }
   if (msg == UCM_CHANGEVOLUME)
   {
       return access((char*)P1, F_OK);
   }
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
extract_rar(char* arch, const char* file, char* passwd, FILE* fp, void* arg)
{
    int ret = 0;
    struct RAROpenArchiveData d = {arch, RAR_OM_EXTRACT, ERAR_EOPEN, NULL, 0, 0, 0};
    struct RARHeaderData header;
    HANDLE hdl = fp?RARInitArchive(&d, fp):RAROpenArchive(&d);
    if (!hdl || d.OpenResult)
        goto extract_error;

    if (passwd && strlen(passwd))
        RARSetPassword(hdl, passwd);

    header.CmtBufSize = 0;

    RARSetCallback(hdl, extract_callback, (LPARAM)(arg));
    while (1)
    {
        if (RARReadHeader(hdl, &header))
           break;

        /* We won't extract subdirs */
        if (IS_RAR_DIR(&header) ||
            strcmp(header.FileName, file))
        {
            if (RARProcessFile(hdl, RAR_SKIP, NULL, NULL))
                 break;
        }
        else
        {
            ret = RARProcessFile(hdl, RAR_TEST, NULL, NULL);
            break;
        }
    }
extract_error:
    if (!fp) RARCloseArchive(hdl);
    return ret;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static char*
getArcPassword(const char* file, char* buf)
{
  if (file && !OBJ_SET(OBJ_NO_PASSWD))
  {
     size_t l = strlen(file);
     char* F = alloca(l+1);
     strcpy(F, file);
     strcpy(F+(l-4), ".pwd");
     FILE* fp = fopen(F, "r");
     if (fp)
     {
        char FMT[8];
        sprintf(FMT, "%%%ds", MAXPASSWORD);
        NO_UNUSED_RESULT fscanf(fp, FMT, buf);
        fclose(fp);
        return buf;
     }
  }
  /* If no .pwd file is found return some default
   * dummy password string */
  buf[0] = '?';
  buf[1] = '\0';
  return buf;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void
set_rarstats(dir_elem_t* entry_p,  RARArchiveListEx* alist_p, int force_dir)
{
   if (!force_dir)
   {
      mode_t mode = GET_RAR_MODE(alist_p);
      if (!S_ISDIR(mode))
      {
          /* Force file to be treated as a 'regular file' */
          mode = (mode & ~S_IFMT) | S_IFREG;
      }
      entry_p->stat.st_mode = mode;
      entry_p->stat.st_nlink = S_ISDIR(mode) ? 2 : alist_p->Method - (0x30 - 1);
      entry_p->stat.st_size = GET_RAR_SZ(alist_p);
   }
   else
   {
      entry_p->stat.st_mode = (S_IFDIR|0777);
      entry_p->stat.st_nlink = 2;
      entry_p->stat.st_size = 4096;
   }
   entry_p->stat.st_uid = getuid();
   entry_p->stat.st_gid = getgid();
   entry_p->stat.st_ino = 0;

#ifdef HAVE_STRUCT_STAT_ST_BLOCKS
   /* This is far from perfect but does the job pretty well!
    * If there is some obvious way to calculate the number of blocks
    * used by a file, please tell me! Most Linux systems seems to
    * apply some sort of multiple of 8 blocks scheme? */
   entry_p->stat.st_blocks = (((entry_p->stat.st_size + (8*512)) & ~((8*512)-1)) / 512);
#endif
   struct tm t;
   memset(&t, 0, sizeof(struct tm));

   union dos_time_t
   {
      __extension__
      struct
      {
#ifndef WORDS_BIGENDIAN
      unsigned int second : 5;
      unsigned int minute : 6;
      unsigned int hour : 5;
      unsigned int day : 5;
      unsigned int month : 4;
      unsigned int year : 7;
#else
      unsigned int year : 7;
      unsigned int month : 4;
      unsigned int day : 5;
      unsigned int hour : 5;
      unsigned int minute : 6;
      unsigned int second : 5;
#endif
      };

      /* Avoid type-punned pointer warning when strict aliasing is used
       * with some versions of gcc */
      unsigned int as_uint_;
   };

   union dos_time_t* dos_time = (union dos_time_t*)&alist_p->FileTime;

   t.tm_sec = dos_time->second;
   t.tm_min = dos_time->minute;
   t.tm_hour = dos_time->hour;
   t.tm_mday = dos_time->day;
   t.tm_mon = dos_time->month-1;
   t.tm_year = (1980 + dos_time->year) - 1900;
   entry_p->stat.st_atime = mktime(&t);
   entry_p->stat.st_mtime = entry_p->stat.st_atime;
   entry_p->stat.st_ctime = entry_p->stat.st_atime;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/

#define NEED_PASSWORD() ((MainHeaderFlags&MHD_PASSWORD)||(next->Flags&LHD_PASSWORD))
#define BS_TO_UNIX(p) do{char*s=(p);while(*s++)if(*s==92)*s='/';}while(0)

static int
listrar(const char* path, dir_entry_list_t** buffer, const char* arch, const char* Password)
{
   ENTER_("%s   arch=%s", path, arch);
   pthread_mutex_lock(&file_access_mutex);

   RAROpenArchiveData d;
   d.ArcName=(char*)arch; /* Horrible cast! But hey... it is the API! */
   d.OpenMode=RAR_OM_LIST;
   d.CmtBuf = NULL;
   d.CmtBufSize = 0;
   d.CmtSize = 0;
   d.CmtState = 0;
   HANDLE hdl = RAROpenArchive(&d);

   /* Check for fault */
   if (d.OpenResult)
   {
      pthread_mutex_unlock(&file_access_mutex);
      return d.OpenResult;
   }

   if (Password) RARSetPassword(hdl, (char*)Password);

   off_t FileDataEnd;
   RARArchiveListEx L;
   RARArchiveListEx* next = &L;
   if (RARListArchiveEx(&hdl, next, &FileDataEnd))
   {
      char* last = strdup("");
      const unsigned int MainHeaderSize = RARGetMainHeaderSize(hdl);
      const unsigned int MainHeaderFlags = RARGetMainHeaderFlags(hdl);

      while (next)
      {
         BS_TO_UNIX(next->FileName);

         /* Skip compressed image files */
         if (!OBJ_SET(OBJ_SHOW_COMP_IMG) &&
            next->Method != 0x30 &&   /* Store */
            IS_IMG(next->FileName))
         {
            next = next->next;
            continue;
         }
         int display = 0;
         char* rar_name = strdup(next->FileName);
         char* tmp1 = rar_name;
         rar_name = strdup(dirname(rar_name));
         free(tmp1);
         tmp1 = rar_name;
         char* rar_root = strdup(arch);
         char* tmp2 = rar_root;
         rar_root = strdup(dirname(rar_root));
         free(tmp2);
         tmp2 = rar_root;
         rar_root += strlen(OBJ_STR2(OBJ_SRC,0));
         if (!strcmp(rar_root, path) || !strcmp("/", path))
         {
            if (!strcmp(".", rar_name)) display = 1;

            /* Handle the rare case when the root folder does not have
             * its own entry in the file header. The entry needs to be
             * faked by adding it to the cache. */
            if (!display)
            {
               if  (!strcmp(basename(rar_name), rar_name))
               {
                  char* mp;
                  ABS_MP(mp, "/", rar_name);
                  dir_elem_t* entry_p = cache_path_get(mp);
                  if (entry_p == NULL)
                  {
                     printd(3, "Adding %s to cache\n", mp);
                     entry_p = cache_path_alloc(mp);
                     entry_p->name_p = strdup(mp);
                     entry_p->file_p = strdup(rar_name);
                     entry_p->rar_p = strdup(arch);
                     set_rarstats(entry_p, next, 1);
                  }
                  if (buffer && strcmp(last,rar_name))
                  {
                     DIR_ENTRY_ADD(*buffer, basename(entry_p->name_p), &entry_p->stat);
                     free(last);
                     last = strdup(rar_name);
                  }
               }
            }
         }
         else if (!strcmp(path + strlen(rar_root)+1, rar_name)) display = 1;
         free(tmp1);
         free(tmp2);

         char* mp;
         if (!display)
         {
            ABS_MP(mp, (*rar_root ? rar_root : "/"), next->FileName);
         }
         else
         {
            char* rar_dir = strdup(next->FileName);
            char* tmp1 = rar_dir;
            ABS_MP(mp, path, basename(rar_dir));
            free(tmp1);
         }

         if (!IS_RAR_DIR(next) && OBJ_SET(OBJ_FAKE_ISO))
         {
            int l = OBJ_CNT(OBJ_FAKE_ISO)
               ? chk_obj(OBJ_FAKE_ISO, mp) : chk_obj(OBJ_IMG_TYPE, mp);
            if (l) strcpy(mp+(strlen(mp)-l), "iso");
         }

         printd(3, "Looking up %s in cache\n", mp);
         dir_elem_t* entry_p = cache_path_get(mp);
         if (entry_p == NULL)
         {
            printd(3, "Adding %s to cache\n", mp);
            entry_p = cache_path_alloc(mp);
            entry_p->name_p = strdup(mp);
            entry_p->rar_p = strdup(arch);
            assert(!entry_p->password_p && "Unexpected handle");
            entry_p->password_p = (NEED_PASSWORD()
               ? strdup(Password)
               : entry_p->password_p);

            /* Check for .rar inside archive */
            if (!(MainHeaderFlags & MHD_VOLUME) &&
                OBJ_INT(OBJ_SEEK_DEPTH, 0))
            {
               /* Check for .rar file */
               if (IS_RAR(entry_p->name_p))
               {
                  printd(3, "%llu byte RAR file %s found in archive %s\n", GET_RAR_PACK_SZ(next), entry_p->name_p, arch);
                  RAROpenArchiveData d2;
                  d2.ArcName=entry_p->name_p;
                  d2.OpenMode=RAR_OM_LIST;
                  d2.CmtBuf = NULL;
                  d2.CmtBufSize = 0;
                  d2.CmtSize = 0;
                  d2.CmtState = 0;
                  HANDLE hdl2 = NULL;
                  FILE* fp = NULL;
                  char* maddr = MAP_FAILED;
                  off_t msize = 0;
                  int mflags = 0;
                  int fd = fileno(RARGetFileHandle(hdl));
                  if (fd!=-1)
                  {
                     if (next->Method == 0x30 &&  /* Store */
                        !NEED_PASSWORD())
                     {
                        struct stat st;
                        (void)fstat(fd, &st);
#ifdef HAVE_FMEMOPEN
                        maddr = mmap(0, P_ALIGN_(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
                        if (maddr != MAP_FAILED)
                           fp = fmemopen(maddr+(next->Offset + next->HeadSize), GET_RAR_PACK_SZ(next), "r");
#else
                        fp = fopen(entry_p->rar_p, "r");
                        if (fp) fseeko(fp, next->Offset + next->HeadSize, SEEK_SET);
#endif
                        msize = st.st_size;
                        mflags = 1;
                     }
                     else
                     {
                         FILE* fp_ = RARGetFileHandle(hdl);
                         off_t curr_pos = ftello(fp_);
                         fseeko(fp_, 0, SEEK_SET);
#ifdef HAVE_FMEMOPEN
                         maddr = extract_to(basename(entry_p->name_p), GET_RAR_SZ(next), fp_, entry_p, E_TO_MEM);
                         if (maddr != MAP_FAILED)
                            fp = fmemopen(maddr, GET_RAR_SZ(next), "r");
#else
                         fp = extract_to(basename(entry_p->name_p), GET_RAR_SZ(next), fp_, entry_p, E_TO_TMP);
                         if (fp == MAP_FAILED)
                         {
                            fp = NULL;
                            printd(1, "Extract to tmpfile failed\n");
                         }
#endif
                         fseeko(fp_, curr_pos, SEEK_SET);
                         msize = GET_RAR_SZ(next);
                         mflags = 2;
                     }
                  }
                  if (fp) hdl2 = RARInitArchive(&d2, fp);
                  if (hdl2)
                  {
                     RARArchiveListEx LL;
                     RARArchiveListEx* next2 = &LL;
                     if (RARListArchiveEx(&hdl2, next2, NULL))
                     {
                        const unsigned int MHF = RARGetMainHeaderFlags(hdl2);
                        while (next2)
                        {
                           if (!(MHF & MHD_VOLUME))
                           {
                              dir_elem_t* entry2_p;
                              printd(3, "File inside archive is %s\n", next2->FileName);
                              /* Allocate a cache entry for this file */
                              char* mp2;
                              char* rar_dir = strdup(next2->FileName);
                              char* tmp1 = rar_dir;
                              ABS_MP(mp2, path, basename(rar_dir));
                              free(tmp1);

                              entry2_p = cache_path_get(mp2);
                              if (!entry2_p)
                              {
                                 entry2_p = cache_path_alloc(mp2);
                                 entry2_p->name_p = strdup(mp2);
                                 entry2_p->rar_p = strdup(arch);
                                 entry2_p->file_p = strdup(next->FileName);
                                 entry2_p->offset = (next->Offset + next->HeadSize);
                                 entry2_p->flags.mmap = mflags;
                                 entry2_p->msize = msize;
                                 entry2_p->flags.multipart = 0;
                                 entry2_p->flags.raw = 0;
                                 set_rarstats(entry2_p, next2, 0);
                              }
                              if (buffer)
                                 DIR_ENTRY_ADD(*buffer, next2->FileName, &entry2_p->stat);
                          }
                          next2 = next2->next;
                        }
                     }
                     RARFreeListEx(&LL);
                     RARFreeArchive(hdl2);
                  }
                  if (fp) fclose(fp);
                  if (maddr != MAP_FAILED)
                  {
                     if (mflags==1) munmap(maddr, P_ALIGN_(msize));
                     else free(maddr);
                  }

                  /* We are done with this rar file (.rar will never display!)*/
                  if (hdl2)
                  {
                     inval_cache_path(mp);
                     next = next->next;
                     continue;
                  }
               }
            }

            if (next->Method == 0x30 &&  /* Store */
               !NEED_PASSWORD())
            {
               entry_p->flags.raw = 1;
               if ((MainHeaderFlags & MHD_VOLUME) &&  /* volume ? */
                  ((next->Flags & (LHD_SPLIT_BEFORE|LHD_SPLIT_AFTER)) ||
                     (IS_RAR_DIR(next))))
               {
                  int len, pos;

                  entry_p->flags.multipart = 1;
                  entry_p->flags.image = IS_IMG(next->FileName);
                  entry_p->vtype = MainHeaderFlags & MHD_NEWNUMBERING?1:0;
                  entry_p->vno_base = get_vformat(
                     entry_p->rar_p,
                     entry_p->vtype,
                     &len,
                     &pos);

                  if (len > 0)
                  {
                     entry_p->vlen = len;
                     entry_p->vpos = pos;
                     if (!IS_RAR_DIR(next))
                     {
                        entry_p->vsize_real = FileDataEnd;
                        entry_p->vsize_next =
                           FileDataEnd - (SIZEOF_MARKHEAD + MainHeaderSize+next->HeadSize);
                        entry_p->vsize = GET_RAR_PACK_SZ(next);
                     }
                  }
                  else
                     entry_p->flags.raw = 0;
               }
               else
               {
                  entry_p->flags.multipart = 0;
                  entry_p->offset = (next->Offset + next->HeadSize);
               }
            }
            else /* Compressed and/or Encrypted */
            {
               entry_p->flags.raw = 0;
               /* Check if part of a volume */
               if (MainHeaderFlags & MHD_VOLUME)
               {
                  entry_p->flags.multipart = 1;
                  entry_p->vtype = MainHeaderFlags & MHD_NEWNUMBERING?1:0;
               }
               else
                  entry_p->flags.multipart = 0;
            }
            entry_p->file_p = strdup(next->FileName);
            set_rarstats(entry_p, next, 0);
         }
         /* To protect from display of the same file name multiple times
          * the cache entry is compared with current archive name.
          * A true cache hit must also be located inside the same
          * archive. */
         else if (!entry_p->rar_p || strcmp(entry_p->rar_p, arch))
            display = 0;

         /* Check if this is a continued volume or not --
          * in that case we might need to skip presentation of the file */
         if ((next->Flags & MHD_VOLUME) && !IS_RAR_DIR(next))
         {
            if(get_vformat(arch, MainHeaderFlags & MHD_NEWNUMBERING?1:0, NULL, NULL)
               != entry_p->vno_base)
            {
               display = 0;
            }
         }

         /* Check if there is already a ordinary/rared file with the same name.
          * In that case this one will loose :( */
         char* tmp = basename(entry_p->name_p);
         if (display && buffer && strcmp(tmp, last))
         {
            DIR_ENTRY_ADD(*buffer, tmp, &entry_p->stat);
            free(last);
            last = strdup(tmp);
         }

         next = next->next;
      }

      RARFreeListEx(&L);
      RARCloseArchive(hdl);
      free(last);
      pthread_mutex_unlock(&file_access_mutex);
      return 0;
   }
   pthread_mutex_unlock(&file_access_mutex);
   return 1;
}

#undef NEED_PASSWORD
#undef BS_TO_UNIX

/*!
 *****************************************************************************
 *
 ****************************************************************************/

static int f0(SCANDIR_ARG3 e) { return (!IS_RAR(e->d_name) && !IS_RXX(e->d_name)); }
static int f1(SCANDIR_ARG3 e) { return IS_RAR(e->d_name); }
static int f2(SCANDIR_ARG3 e) { return IS_RXX(e->d_name); }

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void
sync_dir(const char *dir)
{
   ENTER_("%s", dir);

   char tmpbuf[MAXPASSWORD];
   const char* password = NULL;
   DIR *dp;
   char* root;
   ABS_ROOT(root, dir);

   dp = opendir (root);
   if (dp != NULL)
   {
      struct dirent **namelist;
      int n, f;
      int(*filter[])(SCANDIR_ARG3) = {f0, f1, f2};
      for (f = 1; f < (sizeof(filter) / sizeof(filter[0])); f++)   /* skip first filter; not needed */
      {
         n = scandir(root, &namelist, filter[f], alphasort);
         if (n < 0)
            perror("scandir");
         else
         {
            int i = 0;
            while (i < n)
            {
               int vno =  get_vformat(namelist[i]->d_name, !(f-1), NULL, NULL);
               if (!OBJ_INT(OBJ_SEEK_LENGTH,0) ||
                   vno <= OBJ_INT(OBJ_SEEK_LENGTH,0))
               {
                  char* arch;
                  ABS_MP(arch, root, namelist[i]->d_name);
                  if (vno == 1) /* first file */
                  {
                     password = getArcPassword(arch, tmpbuf);
                  }
                  (void)listrar(dir, NULL, arch, password);
               }
               free(namelist[i]);
               ++i;
            }
            free(namelist);
         }
      }
      (void)closedir(dp);
   }
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static inline int
swap(struct dir_entry_list* A, struct dir_entry_list* B)
{
    if (strcmp(A->entry.name,B->entry.name)>0)
    {
       const struct dir_entry TMP = B->entry;
       B->entry = A->entry;
       A->entry = TMP;
       return 1;
    }
    return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void
sort_dir(dir_entry_list_t* root, const char* path)
{
   /* Simple bubble sort of directory entries in
    * alphabetical order */
   if (root && root->next)
   {
      int n;
      dir_entry_list_t* next;
      do
      {
         n = 0;
         next = root->next;
         while (next->next)
         {
           n += swap(next, next->next);
           next = next->next;
         }
      } while (n != 0); /* while swaps performed */
      /* Make sure entries are unique. Duplicates will be removed.
       * The winner will be the last entry added to the cache or
         entries in the back-end fs. */
      next = root->next;
      while(next->next)
      {
          if (!strcmp(next->entry.name, next->next->entry.name))
          {
              /* A duplicate. Rare but possible. */
              char* tmp;
              ABS_MP(tmp, path, next->entry.name);
              inval_cache_path(tmp);
              next->entry.valid=0;
          }
          next = next->next;
      }
   }
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info *fi)
{
   ENTER_("%s", path);

   char tmpbuf[MAXPASSWORD];
   const char* password = NULL;
   dir_entry_list_t dir_list;    /* internal list root */
   dir_entry_list_t* next = &dir_list;
   DIR_LIST_RST(next);

   DIR *dp;
   char* root;
   ABS_ROOT(root, path);

   dp = opendir (root);
   if (dp != NULL)
   {
      struct dirent **namelist;
      int n, f;
      int(*filter[])(SCANDIR_ARG3) = {f0, f1, f2};
      for (f = 0; f < (sizeof(filter) / sizeof(filter[0])); f++)
      {
         n = scandir(root, &namelist, filter[f], alphasort);
         if (n < 0)
            perror("scandir");
         else
         {
            int i = 0;
            while (i < n)
            {
               if (!f)
               {
                  char* tmp = strdup(namelist[i]->d_name);
                  if (OBJ_SET(OBJ_FAKE_ISO))
                  {
                     char* tmp2;
                     ABS_ROOT(tmp2, tmp);
                     if (!access(tmp2, F_OK)) /* must not already exist */
                     {
                         int l = OBJ_CNT(OBJ_FAKE_ISO)
                            ? chk_obj(OBJ_FAKE_ISO, tmp) : chk_obj(OBJ_IMG_TYPE, tmp);
                         if (l)
                         {
                            if (l < 3) tmp = realloc(tmp, strlen(tmp)+1+(3-l));
                            strcpy(tmp+(strlen(tmp)-l), "iso");
                         }
                     }
                  }
                  DIR_ENTRY_ADD(next, tmp, NULL);
                  free(tmp);
               }
               else
               {
                  int vno =  get_vformat(namelist[i]->d_name, !(f-1), NULL, NULL);
                  if (!OBJ_INT(OBJ_SEEK_LENGTH,0) ||
                      vno <= OBJ_INT(OBJ_SEEK_LENGTH,0))
                  {
                     char* arch;
                     ABS_MP(arch, root, namelist[i]->d_name);
                     if (vno == 1) /* first file */
                     {
                        password = getArcPassword(arch, tmpbuf);
                     }
                     if (listrar(path, &next, arch, password))
                     {
                        DIR_ENTRY_ADD(next, namelist[i]->d_name, NULL);
                     }
                  }
               }
               free(namelist[i]);
               ++i;
            }
            free(namelist);
         }
      }
      (void)closedir(dp);
   }
   else
   {
      pthread_mutex_lock(&file_access_mutex);
      dir_elem_t* entry_p = cache_path_get(path);
      pthread_mutex_unlock(&file_access_mutex);
      int vol = 1;
      if (entry_p && entry_p->flags.multipart)
      {
         char* tmp = strdup(entry_p->rar_p);
         do
         {
            printd(3, "Search for local directory in %s\n", tmp);
            if (vol == 1) /* first file */
            {
               password = getArcPassword(tmp, tmpbuf);
            }
            else RARNextVolumeName(tmp, !entry_p->vtype);
            ++vol;
         } while (!listrar(path, &next, tmp, password));
         free(tmp);
      }
      else if (entry_p && entry_p->rar_p)
      {
         printd(3, "Search for local directory in %s\n", entry_p->rar_p);
         password = getArcPassword(entry_p->rar_p, tmpbuf);
         if (!listrar(path, &next, entry_p->rar_p, password))
         {
            /* Avoid fault detection */
            vol = 3;
         }
      }

      if (vol<3) /* First attempt failed! */
      {
         DIR_LIST_FREE(&dir_list);
         /* Fuse bug!? Returning -ENOENT here seems to be silently ignored.
          * Typically errno is here set to ESPIPE (aka "Illegal seek"). */
         return -errno;
      }

      filler(buffer, ".", NULL, 0);
      filler(buffer, "..", NULL, 0);
   }

   if(!DIR_LIST_EMPTY(&dir_list))
   {
      sort_dir(&dir_list, path);
      dir_entry_list_t* next = dir_list.next;
      while(next)
      {
        if (next->entry.valid)
            filler(buffer, next->entry.name, next->entry.st, 0);
        next = next->next;
      }
      DIR_LIST_FREE(&dir_list);
   }

   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_readdir2(const char *path, void *buffer, fuse_fill_dir_t filler,
              off_t offset, struct fuse_file_info *fi)
{
   ENTER_("%s", path);

   char tmpbuf[MAXPASSWORD];
   const char* password = NULL;
   dir_entry_list_t dir_list;    /* internal list root */
   dir_entry_list_t* next = &dir_list;
   DIR_LIST_RST(next);

   int c = 0;
   dir_entry_list_t* arch_next = arch_list_root.next;
   while(arch_next)
   {
     if (!c++) password = getArcPassword(arch_next->entry.name, tmpbuf);
     (void)listrar(path, &next, arch_next->entry.name, password);
     arch_next = arch_next->next;
   }

   filler(buffer, ".", NULL, 0);
   filler(buffer, "..", NULL, 0);

   if(!DIR_LIST_EMPTY(&dir_list))
   {
      sort_dir(&dir_list, path);
      dir_entry_list_t* next = dir_list.next;
      while(next)
      {
        if (next->entry.valid)
            filler(buffer, next->entry.name, next->entry.st, 0);
        next = next->next;
      }
      DIR_LIST_FREE(&dir_list);
   }

   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void*
reader_task(void* arg)
{
   IOContext* op = (IOContext*)arg;
   op->terminate = 0;

   printd(4, "Reader thread started, fp=%p\n", FH_TOFP(op->fh));

   int fd = op->pfd1[0];
   int nfsd = fd+1;
   while(!op->terminate)
   {
      fd_set rd;

      FD_ZERO(&rd);
      FD_SET(fd, &rd);
      int retval = select(nfsd, &rd, NULL, NULL, NULL);
      if (retval == -1)
         perror("select()");
      else if (retval)
      {
         /* FD_ISSET(0, &rfds) will be true. */
         printd(4, "Reader thread wakeup, select()=%d\n", retval);
         char buf[2];
         NO_UNUSED_RESULT read(fd, buf, 1); /* consume byte */
         {
            if (buf[0]<2 && !feof(FH_TOFP(op->fh)))
               (void)readTo(op->buf, FH_TOFP(op->fh), IOB_SAVE_HIST);
            if (buf[0])
            {
               printd(4, "Reader thread acknowledge\n");
               int fd = op->pfd2[1];
               if (write(fd, buf, 1) != 1) perror("write");
            }
            /* Early termination */
            /*if (feof(FH_TOFP(op->fh))) break;*/ /* XXX check this! */
         }
      }
      else
         perror("select()");
   }
   printd(4, "Reader thread stopped\n");
   pthread_exit(NULL);
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void
preload_index(IoBuf* buf, const char* path)
{
   ENTER_("%s", path);

   /* check for .avi or .mkv */
   if (!IS_AVI(path) && !IS_MKV(path))
      return;

   char* r2i;
   ABS_ROOT(r2i, path);
   strcpy(&r2i[strlen(r2i)-3], "r2i");
   printd(3, "Preloading index for %s\n", r2i);

   buf->idx.data_p = MAP_FAILED;
   int fd = open(r2i, O_RDONLY);
   if (fd==-1)
   {
      return;
   }
#ifdef HAVE_MMAP
   if (!OBJ_SET(OBJ_NO_IDX_MMAP))
   {
      /* Map the file into address space (1st pass) */
      IdxHead* h = (IdxHead*)mmap(NULL, sizeof(IdxHead), PROT_READ, MAP_SHARED, fd, 0);
      if (h == MAP_FAILED)
      {
         close(fd);
         return;
      }
      /* Map the file into address space (2st pass) */
      buf->idx.data_p = (void*)mmap(NULL, P_ALIGN_(h->size), PROT_READ, MAP_SHARED, fd, 0);
      munmap((void*)h, sizeof(IdxHead));
      if (buf->idx.data_p == MAP_FAILED)
      {
         close(fd);
         return;
      }
      buf->idx.mmap = 1;
   } else
#endif
   {
      buf->idx.data_p = malloc(sizeof(IdxData));
      if (!buf->idx.data_p)
      {
         buf->idx.data_p = MAP_FAILED;
         return;
      }
      NO_UNUSED_RESULT read(fd, buf->idx.data_p, sizeof(IdxHead));
      buf->idx.mmap = 0;
   }
   buf->idx.fd = fd;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static inline int
pow_(int b, int n)
{
    int p = 1;
    while (n--) p *= b;
    return p;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_open(const char *path, struct fuse_file_info *fi)
{
   ENTER_("%s", path);
   printd(3, "(%05d) %-8s%s [0x%llu][called from %05d]\n", getpid(), "OPEN", path, fi->fh, fuse_get_context()->pid);
   dir_elem_t* entry_p;
   char* root;

   errno = 0;
   pthread_mutex_lock(&file_access_mutex);
   entry_p = cache_path(path, NULL);
   pthread_mutex_unlock(&file_access_mutex);
   if (entry_p == NULL)
   {
      if (mount_type == MOUNT_FOLDER)
      {
         /* There was a cache miss and the file could not be found locally!
          * This is bad! To make sure the files does not really exist all
          * rar archives need to be scanned for a matching file = slow! */
         CHK_FILTER;
         char* dir = alloca(strlen(path)+1);
         strcpy(dir, path);
         sync_dir(dirname(dir));
         pthread_mutex_lock(&file_access_mutex);
         entry_p = cache_path_get(path);
         pthread_mutex_unlock(&file_access_mutex);
      }
      if (entry_p == NULL)
      {
         /* This should not really be needed. According to API the O_CREAT
            flag is never set in open() calls. */
         if (fi->flags & O_CREAT)
            entry_p = LOCAL_FS_ENTRY;
         else
            return -ENOENT;
      }
   }
   if (entry_p == LOCAL_FS_ENTRY)
   {
      /* XXX Do we need to handle O_TRUNC specifically here? */
      ABS_ROOT(root, path);
      return lopen(root, fi);
   }
   if (entry_p->flags.fake_iso)
   {
      ABS_ROOT(root, entry_p->file_p);
      return lopen(root, fi);
   }
   /* For files inside RAR archives create() type of calls are not permitted */
   if (fi->flags & (O_CREAT|O_WRONLY|O_RDWR|O_TRUNC))
      return -EACCES;

   FILE* fp = NULL;
   IoBuf* buf = NULL;
   IOContext* op = NULL;
   pid_t pid = 0;

   if (entry_p->flags.raw)
   {
      if (!FH_ISSET(fi->fh))
      {
         FILE* fp = fopen(entry_p->rar_p, "r");
         if (fp != NULL)
         {
            op = malloc(sizeof(IOContext));
            if (!op)
               goto open_error;

            printd(3, "Opened %s\n", entry_p->rar_p);
            FH_SETFP(&op->fh, fp);
            FH_SETCONTEXT(&fi->fh, op);
            printd(3, "(%05d) %-8s%s [%-16p]\n", getpid(), "ALLOC", path, FH_TOCONTEXT(fi->fh));
            op->pid = 0;
            op->entry_p = entry_p;
            op->seq = 0;
            op->buf = NULL;
            op->pos = 0;
            op->vno = -1; /* force a miss 1st time */
            op->terminate = 1;
            if (entry_p->flags.multipart &&
                OBJ_SET(OBJ_PREOPEN_IMG) &&
                entry_p->flags.image)
            {
               entry_p->vno_max = pow_(10, op->entry_p->vlen) + 1;
               op->volHdl = malloc(entry_p->vno_max * sizeof(VolHandle));
               if (op->volHdl)
               {
                  memset(op->volHdl, 0, entry_p->vno_max * sizeof(VolHandle));
                  char* tmp = strdup(entry_p->rar_p);
                  int j = entry_p->vno_max;
                  while (j--)
                  {
                     FILE* fp_ = fopen(tmp, "r");
                     if (fp_ == NULL)
                     {
                        break;
                     }
                     printd(3, "pre-open %s\n", tmp);
                     op->volHdl[j].fp = fp_;
                     op->volHdl[j].pos = VOL_REAL_SZ - VOL_FIRST_SZ;
                     printd(3, "SEEK src_off = %llu\n", op->volHdl[j].pos);
                     fseeko(fp_, op->volHdl[j].pos, SEEK_SET);
                     RARNextVolumeName(tmp, !entry_p->vtype);
                  }
                  free(tmp);
               } else printd(1, "Failed to allocate resource (%u)\n", __LINE__);
            }
            else op->volHdl = NULL;
            goto open_end;
         }
         goto open_error;
      }
   }
   if (!FH_ISSET(fi->fh))
   {
      void* mmap_addr = NULL;
      FILE* mmap_fp = NULL;
      int mmap_fd = 0;

      buf = malloc(P_ALIGN_(sizeof(IoBuf)+IOB_SZ));
      if (!buf)
         goto open_error;
      IOB_RST(buf);

      op = malloc(sizeof(IOContext));
      if (!op)
         goto open_error;
      op->buf = buf;

      /* Open PIPE(s) and create child process */
      fp = popen_(entry_p, &pid, &mmap_addr, &mmap_fp, &mmap_fd);
      if (fp!=NULL)
      {
         buf->idx.data_p = MAP_FAILED;
         buf->idx.fd = -1;
         preload_index(buf, path);
         op->mmap_addr = mmap_addr;
         op->mmap_fp = mmap_fp;
         op->mmap_fd = mmap_fd;
         op->entry_p = entry_p;
         op->seq = 0;
         op->pos = 0;
         FH_SETCONTEXT(&fi->fh, op);
         printd(3, "(%05d) %-8s%s [%-16p]\n", getpid(), "ALLOC", path, FH_TOCONTEXT(fi->fh));
         FH_SETFP(&op->fh, fp);
         op->pid = pid;
         printd(4, "PIPE %p created towards child %d\n", FH_TOFP(op->fh), pid);

         /* Disable flushing the cache of the file contents on every open().
          * This is important to make sure FUSE does not force read from
          * offset 0 if a RAR file is opened multiple times. It will break
          * the logic for compressed/encrypted archives since the I/O context
          * will become out-of-sync.
          * This should only be enabled on files, where the file data is never
          * changed externally (not through the mounted FUSE filesystem). */
         fi->keep_cache = 1;

         /* Create pipes to be used between threads.
          * Both these pipes are used for communication between
          * parent (this thread) and reader thread. One pipe is for
          * requests (w->r) and the other is for responses (r<-w). */
         op->pfd1[0] = -1;
         op->pfd1[1] = -1;
         op->pfd2[0] = -1;
         op->pfd2[1] = -1;
         if (pipe(op->pfd1) == -1) { perror("pipe"); goto open_error; }
         if (pipe(op->pfd2) == -1) { perror("pipe"); goto open_error; }

         /* Create reader thread */
         op->terminate = 1;
         pthread_create(&op->thread, &thread_attr, reader_task, (void*)op);
         while (op->terminate);
         WAKE_THREAD(op->pfd1, 0);
         goto open_end;
      }
      goto open_error;
   }
   goto open_end;

open_error:
   if (fp)                pclose_(fp, pid);
   if (op)
   {
      if (op->pfd1[0]>=0) close(op->pfd1[0]);
      if (op->pfd1[1]>=0) close(op->pfd1[1]);
      if (op->pfd2[0]>=0) close(op->pfd2[0]);
      if (op->pfd2[1]>=0) close(op->pfd2[1]);
      free(op);
   }
   if (buf) free(buf);

   /* This is the best we can return here. So many different things
    * might go wrong and errno can actually be set to something that
    * FUSE is accepting and thus proceeds with next operation! */
   printd(1, "open: I/O error\n");
   return -EIO;

open_end:
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static inline int
access_chk(const char* path, int new_file)
{
   void* e;

   /* To return a more correct fault code if an attempt is
    * made to create/remove a file in a RAR folder, a cache lookup
    * will tell if operation should be permitted or not.
    * Simply, if the file/folder is in the cache, forget it!
    *   This works fine in most cases but due to a FUSE bug(!?)
    * it does not work for 'touch'. A touch seems to result in
    * a getattr() callback even if -EPERM is returned which
    * will eventually render a "No such file or directory"
    * type of error/message. */
    if (new_file)
    {
       char* p = strdup(path);
       char* tmp = p;   /* In case p is destroyed by dirname() */
       e = (void*)cache_path_get(dirname(p));
       free(tmp);
    }
    else e = (void*)cache_path_get(path);
    return e?1:0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/

static void*
rar2_init(struct fuse_conn_info *conn)
{
   ENTER_();

   filecache_init();
   iobuffer_init();
   sighandler_init();

#ifdef HAVE_FMEMOPEN
   /* Check fmemopen() support */
   {
       char tmp[64];
       glibc_test = 1;
       fclose(fmemopen(tmp, 64, "r"));
       glibc_test = 0;
   }
#endif

   return NULL;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void
rar2_destroy(void *data)
{
   ENTER_();

   iobuffer_destroy();
   filecache_destroy();
   sighandler_destroy();
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_flush(const char *path, struct fuse_file_info *fi)
{
   ENTER_("%s", path);
   printd(3, "(%05d) %-8s%s [%-16p][called from %05d]\n", getpid(), "FLUSH", path, FH_TOCONTEXT(fi->fh), fuse_get_context()->pid);
   return lflush(path, fi);
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_readlink(const char *path, char* buf, size_t buflen)
{
   ENTER_("%s", path);
   char* tmp;
   ABS_ROOT(tmp, path);
   buflen = readlink(tmp, buf, buflen);
   if (buflen>=0)
   {
       buf[buflen] = 0;
       return 0;
   }
   return -errno;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_statfs(const char *path, struct statvfs* vfs)
{
   ENTER_("%s", path);
   (void)path;   /* touch */
   if (!statvfs(OBJ_STR2(OBJ_SRC,0), vfs))
      return 0;
   return -errno;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_release(const char *path, struct fuse_file_info *fi)
{
   ENTER_("%s", path);
   printd(3, "(%05d) %-8s%s [%-16p]\n", getpid(), "RELEASE", path, FH_TOCONTEXT(fi->fh));
   if (!FH_ISSET(fi->fh))
   {
      pthread_mutex_lock(&file_access_mutex);
      inval_cache_path(path);
      pthread_mutex_unlock(&file_access_mutex);
      return 0;
   }
   if (FH_ISADDR(fi->fh))
   {
      IOContext* op = FH_TOCONTEXT(fi->fh);
      if (!op->terminate)
      {
         op->terminate = 1;
         WAKE_THREAD(op->pfd1, 2);
         pthread_join(op->thread, NULL);
      }
      if (FH_TOFP(op->fh))
      {
         if (op->entry_p->flags.raw)
         {
            if (op->volHdl)
            {
               int j;
               for (j=0;j<op->entry_p->vno_max;j++)
               {
                  if (op->volHdl[j].fp) fclose(op->volHdl[j].fp);
               }
               free(op->volHdl);
            }
            fclose(FH_TOFP(op->fh));
            printd(3, "Closing file handle %p\n", FH_TOFP(op->fh));
         }
         else
         {
            close(op->pfd1[0]);
            close(op->pfd1[1]);
            close(op->pfd2[0]);
            close(op->pfd2[1]);
            if (pclose_(FH_TOFP(op->fh), op->pid))
            {
                printd(4, "child closed abnormaly");
            }
            printd(4, "PIPE %p closed towards child %05d\n", FH_TOFP(op->fh), op->pid);
            if (op->entry_p->flags.mmap)
            {
               fclose(op->mmap_fp);
               if (op->mmap_addr != MAP_FAILED)
               {
                  if (op->entry_p->flags.mmap==1) munmap(op->mmap_addr, P_ALIGN_(op->entry_p->msize));
                  else free(op->mmap_addr);
               }
               close(op->mmap_fd);
            }
         }
      }
      printd(3, "(%05d) %-8s%s [%-16p]\n", getpid(), "FREE", path, op);
      if (op->buf)
      {
         /* XXX clean up */
         if (op->buf->idx.data_p != MAP_FAILED && op->buf->idx.mmap)
            munmap((void*)op->buf->idx.data_p, P_ALIGN_(op->buf->idx.data_p->head.size));
         if (op->buf->idx.data_p != MAP_FAILED && !op->buf->idx.mmap)
            free(op->buf->idx.data_p);
         if (op->buf->idx.fd != -1) close(op->buf->idx.fd);
         free(op->buf);
      }
      free(op);
      FH_ZERO(&fi->fh);
   }
   else
   {
      char* root;
      ABS_ROOT(root, path);
      return lrelease(root, fi);
   }

   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_read(const char *path, char *buffer, size_t size, off_t offset,
          struct fuse_file_info *fi)
{
   ENTER_("%s   size=%zu, offset=%llu, fh=%llu", path, size, offset, fi->fh);
   dir_elem_t* entry_p;
   pthread_mutex_lock(&file_access_mutex);
   entry_p = cache_path_get(path);
   pthread_mutex_unlock(&file_access_mutex);
   if (!entry_p)
   {
      char* root;
      ABS_ROOT(root, path);
      return lread(root, buffer, size, offset, fi);
   }
   if (entry_p->flags.fake_iso)
   {
      char* root;
      ABS_ROOT(root, entry_p->file_p);
      return lread(root, buffer, size, offset, fi);
   }
   if (entry_p->flags.raw)
   {
      return lread_raw(buffer, size, offset, fi);
   }
   return lread_rar(buffer, size, offset, fi);
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_truncate(const char* path, off_t offset)
{
   ENTER_("%s", path);
   char* root;
   ABS_ROOT(root, path);
   return -truncate(root, offset);
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_write(const char* path, const char *buffer, size_t size, off_t offset,
          struct fuse_file_info *fi)
{
   ENTER_("%s", path);
   char* root;
   ABS_ROOT(root, path);
   size_t n = pwrite(FH_TOFD(fi->fh), buffer, size, offset);
   return n>=0?n:-errno;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_chmod(const char* path, mode_t mode)
{
   ENTER_("%s", path);
   if (!access_chk(path, 0))
   {
      char* root;
      ABS_ROOT(root, path);
      if (!chmod(root, mode))
         return 0;
      return -errno;
   }
   return -EPERM;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_chown(const char* path, uid_t uid, gid_t gid)
{
   ENTER_("%s", path);
   if (!access_chk(path, 0))
   {
      char* root;
      ABS_ROOT(root, path);
      if (!chown(root, uid, gid))
         return 0;
      return -errno;
   }
   return -EPERM;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_create(const char* path, mode_t mode, struct fuse_file_info* fi)
{
   ENTER_("%s", path);
   /* Only allow creation of "regular" files this way */
   if (S_ISREG(mode))
   {
      if (!access_chk(path, 1))
      {
         char* root;
         ABS_ROOT(root, path);
         int fd = creat(root, mode);
         if (fd == -1)
            return -errno;
         FH_SETFD(&fi->fh, fd);
         return 0;
      }
   }
   return -EPERM;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_eperm_stub()
{
   return -EPERM;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_rename(const char* oldpath, const char* newpath)
{
   ENTER_("%s", oldpath);
   /* We can not move things out of- or from RAR archives */
   if (!access_chk(newpath, 1) &&
       !access_chk(oldpath, 0))
   {
      char* oldroot;
      char* newroot;
      ABS_ROOT(oldroot, oldpath);
      ABS_ROOT(newroot, newpath);
      if (!rename(oldroot, newroot))
         return 0;
      return -errno;
   }
   return -EPERM;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_mknod(const char* path, mode_t mode, dev_t dev)
{
   ENTER_("%s", path);
   if (!access_chk(path, 1))
   {
      char* root;
      ABS_ROOT(root, path);
      if (!mknod(root, mode, dev))
         return 0;
      return -errno;
   }
   return -EPERM;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_unlink(const char* path)
{
   ENTER_("%s", path);
   if (!access_chk(path, 0))
   {
      char* root;
      ABS_ROOT(root, path);
      if (!unlink(root))
         return 0;
      return -errno;
   }
   return -EPERM;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_mkdir(const char* path, mode_t mode)
{
   ENTER_("%s", path);
   if (!access_chk(path, 1))
   {
      char* root;
      ABS_ROOT(root, path);
      if (!mkdir(root, mode))
        return 0;
      return -errno;
   }
   return -EPERM;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_rmdir(const char* path)
{
   ENTER_("%s", path);
   if (!access_chk(path, 0))
   {
      char* root;
      ABS_ROOT(root, path);
      if (!rmdir(root))
         return 0;
      return -errno;
   }
   return -EPERM;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_utime_deprecated(const char* path, struct utimbuf* ut)
{
   ENTER_("%s", path);
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
rar2_utime(const char* path, const struct timespec tv[2])
{
   ENTER_("%s", path);
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void
usage(char* prog)
{
   const char* P_ = basename(prog);
   printf("Usage: %s [options] source target\n", P_);
   printf("Try `%s -h' or `%s --help' for more information.\n", P_, P_);
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
int
check_paths(char* src_path_in, char* dst_path_in,
            char** src_path_out, char** dst_path_out,
            int verbose)
{
   char p1[PATH_MAX];
   char p2[PATH_MAX];
   char* a1 = realpath(src_path_in, p1);
   char* a2 = realpath(dst_path_in, p2);
   if (!a1||!a2||!strcmp(a1,a2))
   {
      if (verbose) printf("invalid source and/or mount point\n");
      return -1;
   }
   DIR_LIST_RST(arch_list);
   struct stat st;
   (void)stat(a1, &st);
   mount_type = S_ISDIR(st.st_mode) ? MOUNT_FOLDER : MOUNT_ARCHIVE;
   /* Check path type(s), destination path *must* be a folder */
   (void)stat(a2, &st);
   if (!S_ISDIR(st.st_mode) || (mount_type == MOUNT_ARCHIVE && !collect_files(a1, arch_list)))
   {
      if (verbose) printf("invalid source and/or mount point\n");
      return -1;
   }
   /* Do not try to use 'a1' after this call since dirname() will destroy it! */
   *src_path_out = mount_type == MOUNT_FOLDER ? strdup(a1) : strdup(dirname(a1));
   *dst_path_out = strdup(a2);

   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
int
check_iob(char* bname, int verbose)
{
   unsigned int bsz = OBJ_INT(OBJ_BUFF_SIZE, 0);
   unsigned int hsz = OBJ_INT(OBJ_HIST_SIZE, 0);
   if ((OBJ_SET(OBJ_BUFF_SIZE) && !bsz) ||
       (bsz & (bsz -1)) ||
       (OBJ_SET(OBJ_HIST_SIZE) && (hsz > 75)))
   {
      if (verbose) usage(bname);
      return -1;
   }
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
int
check_libunrar(int verbose)
{
   if (RARGetDllVersion() != RAR_DLL_VERSION)
   {
      if (verbose)
      {
          if (RARVER_BETA)
          {
             printf("libunrar.so (v%d.%d beta %d) or compatible library not found\n",
                 RARVER_MAJOR, RARVER_MINOR, RARVER_BETA);
          }
          else
          {
             printf("libunrar.so (v%d.%d) or compatible library not found\n",
                 RARVER_MAJOR, RARVER_MINOR);
          }
      }
      return -1;
   }
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
int
check_libfuse(int verbose)
{
   if (fuse_version() < FUSE_VERSION)
   {
      if (verbose) printf("libfuse.so.%d.%d or compatible library not found\n",
         FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);
      return -1;
   }
   return 0;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/

/* Mapping of static/non-configurable FUSE file system operations. */
static struct fuse_operations rar2_operations = {
   .init    = rar2_init,
   .statfs  = rar2_statfs,
   .utime   = rar2_utime_deprecated,
   .utimens = rar2_utime,
   .destroy = rar2_destroy,
   .open    = rar2_open,
   .release = rar2_release,
   .read    = rar2_read,
   .flush   = rar2_flush,
   .readlink = rar2_readlink
};

struct work_task_data
{
        struct fuse* fuse;
        int mt;
        volatile int work_task_exited;
        int status;
};

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void*
work_task(void* data)
{
        struct work_task_data* wdt = (struct work_task_data*)data;
        wdt->status = wdt->mt ? fuse_loop_mt(wdt->fuse) : fuse_loop(wdt->fuse);
        wdt->work_task_exited = 1;
        pthread_exit(NULL);
        return NULL;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static int
work(struct fuse_args* args)
{
        struct work_task_data wdt;

        /* For portability, explicitly create threads in a joinable state */
        pthread_attr_init(&thread_attr);
        pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);

#if defined ( HAVE_SCHED_SETAFFINITY ) && defined ( HAVE_CPU_SET_T )
        cpu_set_t cpu_mask_saved;
        if (OBJ_SET(OBJ_NO_SMP)) {
                cpu_set_t cpu_mask;
                CPU_ZERO(&cpu_mask);
                CPU_SET(1, &cpu_mask);
                if (sched_getaffinity(0, sizeof(cpu_set_t), &cpu_mask_saved)) {
                        perror("sched_getaffinity");
                } else {
                        if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask)) {
                                perror("sched_setaffinity");
                        }
                }
        }
#endif

        /* The below callbacks depend on mount type */
        rar2_operations.getattr  = mount_type == MOUNT_FOLDER ? rar2_getattr  : rar2_getattr2;
        rar2_operations.readdir  = mount_type == MOUNT_FOLDER ? rar2_readdir  : rar2_readdir2;
        rar2_operations.create   = mount_type == MOUNT_FOLDER ? rar2_create   : (void*)rar2_eperm_stub;
        rar2_operations.rename   = mount_type == MOUNT_FOLDER ? rar2_rename   : (void*)rar2_eperm_stub;
        rar2_operations.mknod    = mount_type == MOUNT_FOLDER ? rar2_mknod    : (void*)rar2_eperm_stub;
        rar2_operations.unlink   = mount_type == MOUNT_FOLDER ? rar2_unlink   : (void*)rar2_eperm_stub;
        rar2_operations.mkdir    = mount_type == MOUNT_FOLDER ? rar2_mkdir    : (void*)rar2_eperm_stub;
        rar2_operations.rmdir    = mount_type == MOUNT_FOLDER ? rar2_rmdir    : (void*)rar2_eperm_stub;
        rar2_operations.write    = mount_type == MOUNT_FOLDER ? rar2_write    : (void*)rar2_eperm_stub;
        rar2_operations.truncate = mount_type == MOUNT_FOLDER ? rar2_truncate : (void*)rar2_eperm_stub;
        rar2_operations.chmod    = mount_type == MOUNT_FOLDER ? rar2_chmod    : (void*)rar2_eperm_stub;
        rar2_operations.chown    = mount_type == MOUNT_FOLDER ? rar2_chown    : (void*)rar2_eperm_stub;

        struct fuse* f;
        pthread_t t;
        char* mp;
        int mt;

        f = fuse_setup(args->argc, args->argv, &rar2_operations, sizeof(rar2_operations), &mp, &mt, NULL);
        wdt.fuse = f;
        wdt.mt = mt;
        wdt.work_task_exited = 0;
        wdt.status = 0;
        pthread_create(&t, &thread_attr, work_task, (void*)&wdt);

        /* This is a workaround for an issue with fuse_loop() that does
         * not always release properly at reception of SIGINT. 
         * But this is really what we want since this thread should not 
         * block blindly without some user control.
         */
        while(!fuse_exited(f) && !wdt.work_task_exited) {
                sleep(1);
                ++rar2_ticks;
        }
        if (!wdt.work_task_exited)
                pthread_kill(t, SIGINT);   /* terminate nicely */

        pthread_join(t, NULL);
        fuse_teardown(f, mp);

#if defined ( HAVE_SCHED_SETAFFINITY ) && defined ( HAVE_CPU_SET_T )
        if (OBJ_SET(OBJ_NO_SMP)) {
                if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask_saved)) {
                        perror("sched_setaffinity");
                }
        }
#endif

        pthread_attr_destroy(&thread_attr);

        return wdt.status;
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/

#define CONSUME_LONG_ARG() { \
   int i;\
   --argc;\
   --optind;\
   for(i=optind;i<argc;i++){\
      argv[i] = argv[i+1];}\
}

int
main(int argc, char* argv[])
{
   int res = 0;

   /*openlog("rarfs2",LOG_NOWAIT|LOG_PID, 0);*/
   configdb_init();

   long ps = -1;
#if defined ( _SC_PAGE_SIZE )
   ps = sysconf(_SC_PAGE_SIZE);
#elif defined ( _SC_PAGESIZE )
   ps = sysconf(_SC_PAGESIZE);
#endif
   if (ps != -1) page_size = ps;
   else          page_size = 4096;

   if (1)
   {
      int opt;
      char *helpargv[2]={NULL,"-h"};

      struct option longopts[] = {
         {"show-comp-img", no_argument,       NULL, OBJ_ADDR(OBJ_SHOW_COMP_IMG)},
         {"preopen-img",   no_argument,       NULL, OBJ_ADDR(OBJ_PREOPEN_IMG)},
         {"no-idx-mmap",   no_argument,       NULL, OBJ_ADDR(OBJ_NO_IDX_MMAP)},
         {"fake-iso",      optional_argument, NULL, OBJ_ADDR(OBJ_FAKE_ISO)},
         {"exclude",       required_argument, NULL, OBJ_ADDR(OBJ_EXCLUDE)},
         {"seek-length",   required_argument, NULL, OBJ_ADDR(OBJ_SEEK_LENGTH)},
         {"unrar-path",    required_argument, NULL, OBJ_ADDR(OBJ_UNRAR_PATH)},
         {"no-password",   no_argument,       NULL, OBJ_ADDR(OBJ_NO_PASSWD)},
         {"seek-depth",    required_argument, NULL, OBJ_ADDR(OBJ_SEEK_DEPTH)},
#if defined ( HAVE_SCHED_SETAFFINITY ) && defined ( HAVE_CPU_SET_T )
         {"no-smp",        no_argument,       NULL, OBJ_ADDR(OBJ_NO_SMP)},
#endif
         {"img-type",      required_argument, NULL, OBJ_ADDR(OBJ_IMG_TYPE)},
         {"no-lib-check",  no_argument,       NULL, OBJ_ADDR(OBJ_NO_LIB_CHECK)},
#ifndef USE_STATIC_IOB_
         {"hist-size",     required_argument, NULL, OBJ_ADDR(OBJ_HIST_SIZE)},
         {"iob-size",      required_argument, NULL, OBJ_ADDR(OBJ_BUFF_SIZE)},
#endif
         {"version",       no_argument,       NULL, 'V'},
         {"help",          no_argument,       NULL, 'h'},
         {NULL,0,NULL,0}
      };

      opterr=0;
      while((opt=getopt_long(argc,argv,"Vhfo:",longopts,NULL))!=-1)
      {
         if(opt=='V'){
#ifdef SVNREV
            printf("rar2fs v%u.%u.%u build %d (DLL version %d, FUSE version %d)    Copyright (C) 2009-2011 Hans Beckerus\n",
#else
            printf("rar2fs v%u.%u.%u (DLL version %d, FUSE version %d)    Copyright (C) 2009-2011 Hans Beckerus\n",
#endif
               RAR2FS_MAJOR_VER,
               RAR2FS_MINOR_VER,
               RAR2FS_PATCH_LVL,
#ifdef SVNREV
               SVNREV,
#endif
               RARGetDllVersion(),
               FUSE_VERSION);
            printf("This program comes with ABSOLUTELY NO WARRANTY.\n"
                   "This is free software, and you are welcome to redistribute it under\n"
                   "certain conditions; see <http://www.gnu.org/licenses/> for details.\n");
            return 0;
         }
         if(opt=='h'){
            helpargv[0]=argv[0];
            fuse_main(2,helpargv,(const struct fuse_operations*)NULL,NULL);
            printf("\nrar2fs options:\n");
            printf("    --img-type=E1[;E2...]   additional image file type extensions beyond the default [.iso;.img;.nrg]\n");
            printf("    --show-comp-img\t    show image file types also for compressed archives\n");
            printf("    --preopen-img\t    prefetch volume file descriptors for image file types\n");
            printf("    --fake-iso[=E1[;E2...]] fake .iso extension for specified image file types\n");
            printf("    --exclude=F1[;F2...]    exclude file filter\n");
            printf("    --seek-length=n\t    set number of volume files that are traversed in search for headers [0=All]\n");
            printf("    --seek-depth=n\t    set number of levels down RAR files are parsed inside main archive [0=Off]\n");
            printf("    --no-idx-mmap\t    use direct file I/O instead of mmap() for .r2i files\n");
            printf("    --unrar-path=PATH\t    path to external unrar binary (overide libunrar)\n");
            printf("    --no-password\t    disable password file support\n");
            printf("    --no-lib-check\t    disable validation of library version(s)\n");
#ifndef USE_STATIC_IOB_
            printf("    --iob-size=n\t    I/O buffer size in 'power of 2' MiB (1,2,4,8, etc.) [4]\n");
            printf("    --hist-size=n\t    I/O buffer history size as a percentage (0-75) of total buffer size [50]\n");
#endif
#if defined ( HAVE_SCHED_SETAFFINITY ) && defined ( HAVE_CPU_SET_T )
            printf("    --no-smp\t\t    disable SMP support (bind to CPU #0)\n");
#endif
            return 0;
         }
         int consume = 1;
         if (collect_obj(OBJ_BASE(opt), optarg))
            consume = 0;
         if (consume) CONSUME_LONG_ARG();
      }

      if(argc<3 || !argv[optind])
      {
         usage(*argv);
         return -1;
      }

      /* Check I/O buffer and history size */
      if (check_iob(*argv, 1))
      {
         return -1;
      }

      /* Check src/dst path */
      char* dst_path;
      char* src_path;
      if (check_paths(argv[optind], argv[optind+1], &src_path, &dst_path, 1))
      {
         return -1;
      }
      collect_obj(OBJ_SRC, src_path);
      collect_obj(OBJ_DST, dst_path);
      free(src_path);
      free(dst_path);

      /* Check library versions */
      if (!OBJ_SET(OBJ_NO_LIB_CHECK))
      {
         if (check_libunrar(1) || check_libfuse(1))
         {
            return -1;
         }
      }
   }
   else
   {
      /* Call external configuration domain here */
   }

   if (argc >= 2)
   {
      argv[optind]=NULL;
      argc-=2;
   }
   else
   {
      argc = 0;
   }
   struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
   fuse_opt_parse(&args, NULL, NULL, NULL);
   fuse_opt_add_arg(&args, "-s");
   fuse_opt_add_arg(&args, "-osync_read,fsname=rar2fs,subtype=rar2fs,default_permissions");
   fuse_opt_add_arg(&args, OBJ_STR(OBJ_DST,0));

   /* All static setup is ready, the rest is taken from the configuration.
    * Continue in work() function which will not return until the process
    * is about to terminate. */
   res = work(&args);

   /* Clean up what has not already been taken care of */
   fuse_opt_free_args(&args);
   configdb_destroy();

   /* Why nothing is ever simple ? */
#ifndef __APPLE__
   pthread_exit(NULL);
#endif

   return res;
}

#undef CONSUME_LONG_ARG


