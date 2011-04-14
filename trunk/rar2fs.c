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

#define _BSD_SOURCE /* or _SVID_SOURCE or _GNU_SOURCE */
#define _GNU_SOURCE
#define _XOPEN_SOURCE 500

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dirent.h>
#include <libgen.h>
#include <fuse.h>
#include <fcntl.h>
#include <endian.h>
#include <getopt.h>
#include <syslog.h>
#include <sys/mman.h>
#include <limits.h>
#include <assert.h>
#include "common.h"
#include "dllwrapper.h"
#include "filecache.h"
#include "iobuffer.h"
#include "configdb.h"

#define P_ALIGN_(a) (((a)+page_size)&~(page_size-1))

#ifdef __UCLIBC__
#define stack_trace(a,b,c)
#else
#include <stdio.h>
#include <signal.h>
#include <execinfo.h>

/* get REG_EIP from ucontext.h */
#include <ucontext.h>

stack_trace(int sig, siginfo_t *info, void *secret)
{
  void *trace[30];
  char **messages = (char **)NULL;
  int i, trace_size = 0;
  ucontext_t *uc = (ucontext_t *)secret;

  /* Do something useful with siginfo_t */
  char buf[256];
  snprintf(buf, sizeof(buf), "Got signal %d, faulty address is 0x%p, "
     "from 0x%p", sig, info->si_addr, 
     (void*)uc->uc_mcontext.gregs[REG_EIP]);
  printf("%s\n", buf);
  syslog(LOG_INFO, "%s", buf);
#if 1
  trace_size = backtrace(trace, 30);
  /* overwrite sigaction with caller's address */
  trace[1] = (void *) uc->uc_mcontext.gregs[REG_EIP];
  messages = backtrace_symbols(trace, trace_size);
  if (messages)
  {
     /* skip first stack frame (points here) */
     for (i=1; i<trace_size; ++i)
     {
        printf("%s\n", messages[i]);
        syslog(LOG_INFO, "%s", messages[i]);
     }
     free(messages);
  }
#else
  void *pc0 = __builtin_return_address(0);
  void *pc1 = __builtin_return_address(1);
  void *pc2 = __builtin_return_address(2);
  void *pc3 = __builtin_return_address(3);

  printf("Frame 0: PC=%08p\n", pc0);
  printf("Frame 1: PC=%08p\n", pc1);
  printf("Frame 2: PC=%08p\n", pc2);
  printf("Frame 3: PC=%08p\n", pc3);
#endif 
}
#endif

/* The maximum number of volume files that will be opened if --preopen-img
   option is specified. */
#define MAX_NOF_OPEN_VOL (101)

typedef struct dir_entry_list dir_entry_list_t;
struct dir_entry_list
{
  struct dir_entry
  {
     char* name;
     struct stat* st;
  } entry;
  dir_entry_list_t* next;
};

#define DIR_LIST_RST(l) \
{  (l)->next = NULL;\
   (l)->entry.name = NULL;\
   (l)->entry.st = NULL;}
#define DIR_ENTRY_ADD(l, n, s) \
{  (l)->next = malloc(sizeof(dir_entry_list_t));\
   (l)=(l)->next;\
   (l)->entry.name=strdup(n);\
   (l)->entry.st=(s);\
   (l)->next = NULL;}
#define DIR_LIST_EMPTY(l) (!(l)->next)
#define DIR_LIST_FREE(l)\
{  dir_entry_list_t* next = (l)->next;\
   while(next){\
      dir_entry_list_t* tmp = next;\
      next = next->next;\
      free(tmp->entry.name);\
      free(tmp);}}

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
#define FH_ISADDR(fh)          (FH_ISSET(fh) && !FH_ISFH(fh))
#define FH_ISFH(fh)            (FH_ISSET(fh) && ((fh)&0x1000000000000000ULL))
#define FH_SETFH(fh, v)        {((IOFileHandle*)(fh))->bits = (uint64_t)(size_t)(v)|0x1000000000000000ULL;}
#define FH_SETCONTEXT(fh, v)   {((IOFileHandle*)(fh))->bits = (uint64_t)(size_t)(v)&~0x1000000000000000ULL;}
#define FH_TOCONTEXT(fh)       (((IOFileHandle)(fh)).context)
#define FH_TOFD(fh)            (((IOFileHandle)(fh)).fd)
#define FH_TOFP(fh)            (((IOFileHandle)(fh)).fp)
#define FH_TOFH(fh)            (fh)

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
   int pfd[2];
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
       no_warn_result_ read(fd, buf, 1); /* consume byte */\
       tprintf("%u thread wakeup (%d, %u)\n", pthread_self(), retval, (int)buf[0]);\
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

char* src_path = NULL;
static long page_size;

static int glibc_test = 0;
static pthread_mutex_t file_access_mutex;

static void sync_dir(const char *dir);

#include <sys/wait.h>
static void
sig_handler(int signum, siginfo_t *info, void* secret)
{
   switch(signum)
   {
   case SIGUSR1:
   {
      tprintf("Invalidating path cache\n");
      pthread_mutex_lock(&file_access_mutex);
      inval_cache_path(NULL);
      pthread_mutex_unlock(&file_access_mutex);
   }
   break;
   case SIGSEGV:
   {
      if (!glibc_test)
      {
         tprintf("SIGSEGV\n");
         stack_trace(SIGSEGV, info, secret);
      } 
      else
      {
         printf("glibc validation failed\n");
      }
      exit(EXIT_FAILURE);
   }
   break;
   case SIGCHLD:
   {
      tprintf("SIGCHLD\n");
   }
   break;
   }
}

static void*
extract_to_mem(const char* file, off_t sz, FILE* fp, const dir_elem_t* entry_p)
{
  int out_pipe[2];
  if(pipe(out_pipe) != 0 ) {          /* make a pipe */
    return MAP_FAILED;
  }

  tprintf("extract_to_mem() extracting %s (%llu bytes) resident in %s\n", file, sz, entry_p->rar_p);
  pid_t pid = fork();
  if (pid == 0)
  {
     close(out_pipe[0]);
     fflush(stdout);
     dup2(out_pipe[1], STDOUT_FILENO);   /* redirect stdout to the pipe */

     /* anything sent to stdout should now go down the pipe */
     RARExtractToStdout(entry_p->rar_p, file, entry_p->password_p, fp);
     fflush(stdout);
     _exit(EXIT_SUCCESS);
  }
  else if (pid < 0)
  {
     close(out_pipe[0]);
     close(out_pipe[1]);
     /* The fork failed.  Report failure.  */
     return MAP_FAILED;
  }

  close(out_pipe[1]);
  char* buffer = malloc(sz);
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
  tprintf("read %llu bytes from pipe %d\n", off, out_pipe[0]);
  close(out_pipe[0]);

  return buffer;
}

#define UNRAR_ unrar_path, unrar_path

static FILE*
_popen(const dir_elem_t* entry_p, pid_t* cpid, void** mmap_addr, FILE** mmap_fp, int* mmap_fd)
{
   char* maddr = MAP_FAILED;
   FILE* fp = NULL;
   char* unrar_path = OBJ_STR(OBJ_UNRAR_PATH,0);
   if (entry_p->flags.mmap)
   {
       int fd = open(entry_p->rar_p, O_RDONLY, S_IREAD);
       if (fd != -1)
       {
#ifdef HAS_GLIBC_CUSTOM_STREAMS_
          if (entry_p->flags.mmap==2)
          {
             maddr = extract_to_mem(entry_p->file_p, entry_p->msize, NULL, entry_p);
             if (maddr != MAP_FAILED)
                fp = fmemopen(maddr, entry_p->msize, "r");
          }
          else
#endif
          {
#ifdef HAS_GLIBC_CUSTOM_STREAMS_
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
             if(entry_p->flags.mmap==1) munmap(maddr, P_ALIGN_(entry_p->msize));
             else free(maddr);
          if (fd != -1) close(fd);
          return NULL;
       }
   }
   int status;
   int pfd[2];
   pid_t pid;
   char buf;
   if (pipe(pfd) == -1) { perror("pipe"); return NULL; }

   pid = fork();
   if (pid == 0)
   {
      setpgid(getpid(), 0);
      close(pfd[0]);          /* Close unused read end */
      fflush(stdout);
      dup2(pfd[1], STDOUT_FILENO);
      /* This is the child process.  Execute the shell command. */
      if (unrar_path && !entry_p->flags.mmap)
      {
         if (entry_p->password_p)
         {
            char parg[MAXPASSWORD+2];
            snprintf(parg, MAXPASSWORD+2, "%s%s", "-p", entry_p->password_p);
            execl(UNRAR_, parg, "P", "-inul", entry_p->rar_p, entry_p->file_p, NULL);
         }
         else
         {
            execl(UNRAR_, "P", "-inul", entry_p->rar_p, entry_p->file_p, NULL);
         }
         /* Should never come here! */
         _exit (EXIT_FAILURE);
      }
      /* anything sent to stdout should now go down the pipe */
      RARExtractToStdout(entry_p->rar_p, entry_p->flags.mmap ? basename(entry_p->name_p) : entry_p->file_p, entry_p->password_p, fp);
      fflush(stdout);	/* flush any pending data */
      _exit(EXIT_SUCCESS);
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

static int
_pclose(FILE* fd, pid_t pid)
{
   int status;
   tprintf("_pclose(%lu, %d)\n", (unsigned long)fd, pid);
   fclose(fd);
   killpg(pid, SIGKILL);
   /* Sync */
   while (waitpid(pid, &status, WNOHANG|WUNTRACED) != -1);
   return 0;
}

/* Size of file in first volume in which it exists */
#define VOL_FIRST_SZ op->entry_p->vsize 

/* Size of file in the following volume(s) (if situated in more than one) */
#define VOL_NEXT_SZ op->entry_p->vsize_next /*(VOL_REAL_SZ-30)*/

/* Size of file data in first volume file */
#define VOL_REAL_SZ op->entry_p->vsize_real /*(10000-20)*/

/* Calculate volume number (base 0) using archived file offset */
#define VOL_NO(off) (off < VOL_FIRST_SZ ? 0 : ((offset-VOL_FIRST_SZ) / VOL_NEXT_SZ)+1)

static char*
get_volfn(int t, const char* str, int vol, int len, int pos)
{
   char* s = strdup(str);
   tprintf("get_volfn(%s): vol=%d, len=%d, pos=%d\n", str, vol,len,pos);
   if (!vol) return s;
   if (t)
   {
      char f[16];
      char f1[16];
      sprintf(f1, "%%0%dd", len);
      sprintf(f, f1, vol);
      strncpy(&s[pos], f, len);
      return s;
   }
   else if (vol <= MAX_NOF_OPEN_VOL)
   {
      char f[16];
      if (vol==1) sprintf(f, "%s", "ar");
      else sprintf(f, "%02d", vol-2);

      strncpy(&s[pos], f, len);
      return s;
   }
   return NULL;
}

static int
lread_raw(char *buf, size_t size,  off_t offset, struct fuse_file_info *fi)
{
   int n = 0;
   IOContext* op = FH_TOCONTEXT(fi->fh);
   if (!op) return -EACCES;

   tprintf("%d calling lread_raw, seq = %d\n", getpid(), op->seq, op->pos);

   off_t chunk;
   int tot = 0;
   int force_seek = 0;
   ++op->seq;

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
                   * Some media players tend to react "bettter" on that and 
                   * terminate playback as expected. */
                  char* tmp = get_volfn(
                     op->entry_p->vtype, op->entry_p->rar_p, op->vno+op->entry_p->vno_base, op->entry_p->vlen, op->entry_p->vpos);
                  if (tmp)
                  {
                     tprintf("Opening %s\n", tmp);
                     fp = fopen(tmp, "r");
                     free(tmp);
                     if (fp == NULL)
                     {
                        perror("open");
                        return 0;
                     }
                     fclose(FH_TOFP(op->fh));
                     FH_SETFH(&op->fh, fp);
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
            tprintf("real_size = %llu, chunk = %llu\n", VOL_REAL_SZ, chunk);
            src_off = VOL_REAL_SZ - chunk;
            tprintf("SEEK src_off = %llu\n", src_off);
            fseeko(fp, src_off, SEEK_SET);
            force_seek = 0;
         }
         tprintf("size = %d, chunk=%llu\n", size, chunk);
         chunk = size < chunk ? size : chunk;
      }
      else
      {
         fp = FH_TOFP(op->fh);
         chunk = size;
         if (!offset || offset != op->pos)
         {
            src_off = offset + op->entry_p->offset;
            tprintf("SEEK src_off = %llu\n", src_off);
            fseeko(fp, src_off, SEEK_SET);
         }
      }
      n = fread(buf, 1, (size_t)chunk, fp);
      tprintf("read %d bytes from vol=%d,base=%d\n", n, op->vno, op->entry_p->vno_base);
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

static int
lread_rar(char *buf, size_t size,  off_t offset, struct fuse_file_info *fi)
{
   IOContext* op = FH_TOCONTEXT(fi->fh);
   if (!op) return -EIO;

   ++op->seq;
   tprintf("(%d) lread_rar : seq = %d, offset = %llu (%llu), size = %d\n", getpid(), op->seq, offset, op->pos, size);

   FILE* fp = FH_TOFP(op->fh);
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
         tprintf("Copying data from preloaded index information @ %llu\n", offset);
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
#ifdef USE_STATIC_WINDOW
         if ((offset + size) < FHD_SZ)
         {
            memcpy(buf, op->buf->sbuf_p+(size_t)offset, size);
            return size;
         }
#endif
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
            tprintf("I/O ERROR %d\n",op->terminate);
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
             WAKE_THREAD(op->pfd, 2);
             WAIT_THREAD(op->pfd2);
         } while (errno==EINTR);

         while(!feof(FH_TOFP(op->fh)) && (offset+size)>op->buf->offset)
         {
            /* consume buffer */
            op->pos += op->buf->used;
            op->buf->ri = op->buf->wi;
            (void)readTo(op->buf, FH_TOFP(op->fh), IOB_NO_HIST);
         }
      }
   }

   if (size)
   {
      int off = offset - op->pos;
      n += readFrom(buf, op->buf, size, off);
      op->pos += (off + n);
      if (!op->terminate) WAKE_THREAD(op->pfd, 0);
   }
   tprintf("RETURN %d\n", errno ? -errno : n);
   return errno ? -errno : n;
}

static int lflush(const char *path,
                  struct fuse_file_info *fi)
{
   tprintf("lflush %s\n", path);   
   (void)fi; /* touch */
   return 0;
}

static int lrelease(const char *path,
                    struct fuse_file_info *fi) 
{
   tprintf("lrelease %s\n", path);   
   close(FH_TOFD(fi->fh));
   FH_ZERO(&fi->fh);
   return 0;
}

static int
lread(const char *path,
      char *buffer, size_t size, off_t offset,
      struct fuse_file_info *fi)
{
   tprintf("lread %s : size = %d, offset = %llu\n", path, size, offset);
   return pread(FH_TOFD(fi->fh), buffer, size, offset);
}

static int
lopen(const char *path,
      struct fuse_file_info *fi)
{
   tprintf("lopen %s\n", path);   
   int res = open(path, fi->flags);
   if (res == -1)
      return -errno;
   FH_SETFH(&fi->fh, res);
   return 0;
}

static int
rar2_getattr(const char *path, struct stat *stbuf)
{
   tprintf("getattr() %s\n", path);
   memset(stbuf, 0, sizeof(struct stat));
#if 0
   if(strcmp(path, "/") == 0) {
      stbuf->st_mode = S_IFDIR | 0755;
      stbuf->st_nlink = 2;
      /* Hardcoded defaults. Just cosmetics really. */
      stbuf->st_size = 4096;
      stbuf->st_blocks = 8;
      return 0;
   }
#endif

   pthread_mutex_lock(&file_access_mutex);
   if (cache_path(path, stbuf))
   {
      pthread_mutex_unlock(&file_access_mutex);
      return 0;
   }
   pthread_mutex_unlock(&file_access_mutex);
   /* There was a cache miss and the file could not be found locally!
    * This is bad! To make sure the files does not really exist all
    * rar archives need to be scanned for a matching file = slow! */
   CHK_FILTER;
   char* dir = alloca(strlen(path)+1);
   strcpy(dir, path);
   dir = dirname(dir);
   sync_dir(dir);
   pthread_mutex_lock(&file_access_mutex);
   dir_elem_t* e_p = cache_path_get(path);
   pthread_mutex_unlock(&file_access_mutex);
   if (e_p)
   {
      memcpy(stbuf, &e_p->stat, sizeof(struct stat));
      return 0;
   } 
   return -ENOENT;
}

int
is_rxx_vol(const char* name)
{
   size_t len = strlen(name);
   {
      if (name[len-4] == '.' &&
         name[len-3] == 'r' &&
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

static int
get_vnfm(const char* s, int t, int* l, int* p)
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
            if (!strncmp(&s[pos], "ar", 2))
            {
               vol = 1;
            }
            else
            {
               errno = 0;
               vol = strtoul(&s[pos], NULL, 10);
               vol = errno ? 0 : vol + 2;
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
#define IS_RAR_DIR(l) ((l)->HostOS != HOST_UNIX && (l)->HostOS != HOST_BEOS \
   ? (l)->FileAttr & 0x10 : (l)->FileAttr & S_IFDIR)
#define GET_RAR_MODE(l) ((l)->HostOS != HOST_UNIX && (l)->HostOS != HOST_BEOS \
   ? IS_RAR_DIR(l) ? (S_IFDIR|0755) : (S_IFREG|0644) : (l)->FileAttr)
#define GET_RAR_SZ(l) (IS_RAR_DIR(l) ? 4096 : (((l)->UnpSizeHigh * 0x100000000ULL) | (l)->UnpSize))
#define GET_RAR_PACK_SZ(l) (IS_RAR_DIR(l) ? 4096 : (((l)->PackSizeHigh * 0x100000000ULL) | (l)->PackSize))

#define BS_TO_UNIX(p) do{char*s=(p);while(*s++)if(*s==92)*s='/';}while(0)
static inline IS_ROOT(const char* s)
{
    int ret;
    char* dup = strdup(s);
    char* dir = dirname(dup);
    ret = (*dir == '.' || *dir == '/') ? 1 : 0;
    free(dup);
    return ret;
}

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
        no_warn_result_ fscanf(fp, FMT, buf);
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

static void
set_rarstats(dir_elem_t* entry_p,  RARArchiveListEx* alist_p)
{
   entry_p->stat.st_mode = GET_RAR_MODE(alist_p);
   entry_p->stat.st_nlink = IS_RAR_DIR(alist_p) ? 2 : 1;
   entry_p->stat.st_size = GET_RAR_SZ(alist_p);
   /* This is far from perfect but does the job pretty well! 
    * If there is some obvious way to calculate the number of blocks
    * used by a file, please tell me! Most Linux systems seems to
    * apply some sort of multiple of 8 blocks scheme? */
   entry_p->stat.st_blocks = (((entry_p->stat.st_size + (8*512)) & ~((8*512)-1)) / 512);

   struct tm t;
   memset(&t, 0, sizeof(struct tm));
   struct dos_time_t
   {
#if __BYTE_ORDER == __LITTLE_ENDIAN
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
   struct dos_time_t* dos_time = (struct dos_time_t*)&alist_p->FileTime;
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

#define NEED_PASSWORD() ((MainHeaderFlags&MHD_PASSWORD)||(next->Flags&LHD_PASSWORD))

static int
listrar(const char* path, dir_entry_list_t** buffer, const char* arch, const char* Password)
{
   pthread_mutex_lock(&file_access_mutex);
   tprintf("Listing %s\n", arch);
   tprintf("source path %s\n", path);

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

   RARArchiveListEx L;
   RARArchiveListEx* next = &L;
   if (RARListArchiveEx(&hdl, next))
   {
      const unsigned int MainHeaderSize = RARGetMainHeaderSize(hdl);
      const unsigned int MainHeaderFlags = RARGetMainHeaderFlags(hdl);
      const off_t RawFileDataEnd = RARGetRawFileDataEnd(hdl);

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
         //XXXbool isRAR = IS_RAR(next->FileName);
         int display = 0;
         char* rar_name  = strdup(next->FileName);
         char* tmp1 = rar_name;
         rar_name = dirname(rar_name);
         char* rar_root = strdup(arch);
         char* tmp2 = rar_root;
         rar_root = dirname(rar_root) + strlen(src_path);
         if (!strcmp(rar_root, path) || !strcmp("/", path))
         { 
            if (!strcmp(".", rar_name)) display = 1;
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

         tprintf("Looking up %s in cache\n",mp);
         dir_elem_t* entry_p = cache_path_get(mp);
         if (entry_p == NULL)
         {
            tprintf("Adding %s to cache\n", mp);
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
                  tprintf("%llu byte RAR file %s found in archive %s\n", GET_RAR_PACK_SZ(next), entry_p->name_p, arch);
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
#ifdef HAS_GLIBC_CUSTOM_STREAMS_
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
#ifdef HAS_GLIBC_CUSTOM_STREAMS_
                     else 
                     {
                         FILE* fp_ = RARGetFileHandle(hdl);
                         off_t curr_pos = ftello(fp_);
                         fseeko(fp_, 0, SEEK_SET);
                         maddr = extract_to_mem(basename(entry_p->name_p), GET_RAR_SZ(next), fp_, entry_p);
                         fseeko(fp_, curr_pos, SEEK_SET);
                         if (maddr != MAP_FAILED)
                            fp = fmemopen(maddr, GET_RAR_SZ(next), "r");
                         msize = GET_RAR_SZ(next);
                         mflags = 2;
                     }
#endif
                  } 
                  if (fp) hdl2 = RARInitArchive(&d2, fp);
                  if (hdl2)
                  {
                     RARArchiveListEx LL;
                     RARArchiveListEx* next2 = &LL;
		     if (RARListArchiveEx(&hdl2, next2))
                     {
                        const unsigned int MHF = RARGetMainHeaderFlags(hdl2);
                        while (next2)
                        {
                           if (!(MHF & MHD_VOLUME))
                           {
                              dir_elem_t* entry2_p;
                              tprintf("file inside archive is %s\n", next2->FileName);
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
                                 set_rarstats(entry2_p, next2);
                              }
                              if (buffer)
                                 DIR_ENTRY_ADD(*buffer, next2->FileName, &entry2_p->stat);
                          }
                          next2 = next2->next;
                        }
                     }
                     RARFreeListEx(&hdl2, &LL);
                     RARFreeArchive(hdl2);
                  }
                  if (fp) fclose(fp);
                  if (maddr != MAP_FAILED) 
                     if (mflags==1) munmap(maddr, P_ALIGN_(msize));
                     else free(maddr);

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
               if ((MainHeaderFlags & MHD_VOLUME) &&  /* volume ? */
                  ((next->Flags & (LHD_SPLIT_BEFORE|LHD_SPLIT_AFTER)) ||
                     (IS_RAR_DIR(next))))
               {
                  int len,pos;

                  entry_p->flags.image = IS_IMG(next->FileName);
                  entry_p->vtype = MainHeaderFlags & MHD_NEWNUMBERING?1:0;
                  entry_p->vno_base = get_vnfm(entry_p->rar_p,
                     entry_p->vtype,
                     &len,
                     &pos);

                  if (!len)
                     entry_p->offset = 0;
                  else
                  {
                     entry_p->vlen = len;
                     entry_p->vpos = pos;
                     entry_p->offset = 1; /* Any value but 0 will do */
                     if (!IS_RAR_DIR(next))
                     {
                        entry_p->vsize_real = RawFileDataEnd;
                        tprintf("vsize_real = %llu\n",  entry_p->vsize_real);
                        entry_p->vsize_next =
                           RawFileDataEnd - (SIZEOF_MARKHEAD + MainHeaderSize+next->HeadSize);
                        entry_p->vsize = GET_RAR_PACK_SZ(next);
                     }
                     else
                     {
                        entry_p->vsize = 1;  /* Any value but 0 is ok */
                     }
                  }
               }
               else
               {
                  entry_p->offset = (next->Offset + next->HeadSize);
                  entry_p->vsize = 0;
               }
            }
            else /* Compressed and/or Encrypted */
            {
               entry_p->offset = 0;
               if (!IS_RAR_DIR(next))
               {
                  entry_p->vsize = 0;
               }
               /* Check if part of a volume */
               else if (MainHeaderFlags & MHD_VOLUME)
               {
                  int len,pos;
                  entry_p->vtype = MainHeaderFlags & MHD_NEWNUMBERING?1:0;
                  entry_p->vsize = 1;
                  (void)get_vnfm(entry_p->rar_p, entry_p->vtype, &len, &pos);
                  entry_p->vlen = len;
                  entry_p->vpos = pos;
               }
            }
            entry_p->file_p = strdup(next->FileName);
            set_rarstats(entry_p, next);
         } 
         /* To protect from display of the same file name multiple times
          * the cache entry is compared with current archive name.
          * A true cache hit must also use located inside the same
          * archive. */
         else if (!entry_p->rar_p || strcmp(entry_p->rar_p, arch)) display = 0;

         /* Check if this is a continued volume or not --
          * in that case we might need to skip presentation of the file */
         if ((next->Flags & MHD_VOLUME) && !IS_RAR_DIR(next))
         {
            if(get_vnfm(arch, MainHeaderFlags & MHD_NEWNUMBERING?1:0, NULL, NULL)
               != entry_p->vno_base)
            {
               display = 0;
            }
         }

         /* Check if there is already a ordinary/rared file with the same name.
          * In that case this one will loose :( */
         if (display && buffer)
         {
            DIR_ENTRY_ADD(*buffer, basename(entry_p->name_p), &entry_p->stat);
         }

         next = next->next;
      }

clean_up:
      RARFreeListEx(&hdl, &L);
      RARCloseArchive(hdl);
      pthread_mutex_unlock(&file_access_mutex);
      return 0;
   }
   pthread_mutex_unlock(&file_access_mutex);
   return 1;
}

#undef NEED_PASSWORD

int f0(const struct dirent* e) { return (!IS_RAR(e->d_name) && !IS_RXX(e->d_name)); }
int f1(const struct dirent* e) { return IS_RAR(e->d_name); }
int f2(const struct dirent* e) { return IS_RXX(e->d_name); }

#define NOF_FILTERS 3

static void
sync_dir(const char *dir)
{
   tprintf("sync_dir() %s\n", dir);
   DIR *dp;
   struct dirent *ep;
   char* root;
   ABS_ROOT(root, dir);

   dp = opendir (root);
   if (dp != NULL)
   {
      tprintf("opendir() succeeded\n");
      char tmpbuf[MAXPASSWORD]; 
      const char* password = NULL;
      struct dirent **namelist;
      int n, f;
      int(*filter[NOF_FILTERS])(const struct dirent *) = {f0, f1, f2};
      for (f = 0; f < NOF_FILTERS; f++)
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
                  char* file;
                  ABS_MP(file, dir, namelist[i]->d_name); 
                  pthread_mutex_lock(&file_access_mutex);
                  (void)cache_path(file, NULL);
                  pthread_mutex_unlock(&file_access_mutex);
               }
               else
               {
                  int vno =  get_vnfm(namelist[i]->d_name, !(f-1), NULL, NULL);
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
               }
               free(namelist[i]);
               ++i;
            }
            free(namelist);
         }
      }
      (void)closedir(dp);
      tprintf("closedir() done\n");
   }
}

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

static void
sort_dir(dir_entry_list_t* root)
{
   /* Simple bubble sort of directory entries in 
    * alphabetical order */
   if (root && root->next)
   {
      int n;
      do
      {
         dir_entry_list_t* next = root->next;
         n = 0;
         while (next->next)
         {
           n += swap(next, next->next);
           next = next->next;
         }
      } while (n != 0);	/* while swaps performed */
   }
}

static int
rar2_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info *fi)
{
   tprintf ("readdir %s\n", path);
   char tmpbuf[MAXPASSWORD]; 
   const char* password = NULL;
   dir_entry_list_t dir_list;    /* internal list root */
   dir_entry_list_t* next = &dir_list;
   DIR_LIST_RST(next);
   DIR *dp;
   struct dirent *ep;
   char* root;
   ABS_ROOT(root, path);

   dp = opendir (root);
   if (dp != NULL)
   {
      tprintf("opendir() succeeded\n");
      struct dirent **namelist;
      int n, f;
      int(*filter[NOF_FILTERS])(const struct dirent *) = {f0, f1, f2};
      for (f = 0; f < NOF_FILTERS; f++)
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
                  char* file;
                  ABS_MP(file, path, namelist[i]->d_name);
                  pthread_mutex_lock(&file_access_mutex);
                  (void)cache_path(file, NULL);
                  pthread_mutex_unlock(&file_access_mutex);
                  char* tmp = strdup(namelist[i]->d_name);
                  if (OBJ_SET(OBJ_FAKE_ISO))
                  {
                     int l = OBJ_CNT(OBJ_FAKE_ISO) 
                        ? chk_obj(OBJ_FAKE_ISO, tmp) : chk_obj(OBJ_IMG_TYPE, tmp);
                     if (l)
                     {
                        if (l < 3) tmp = realloc(tmp, strlen(tmp)+1+(3-l));
                        strcpy(tmp+(strlen(tmp)-l), "iso");
                     }
                  }
                  DIR_ENTRY_ADD(next, tmp, NULL);
                  free(tmp);
               }
               else
               {
                  int vno =  get_vnfm(namelist[i]->d_name, !(f-1), NULL, NULL);
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
      tprintf("closedir() done\n");
   }
   else
   {
      pthread_mutex_lock(&file_access_mutex);
      dir_elem_t* entry_p = cache_path_get(path);
      pthread_mutex_unlock(&file_access_mutex);
      char* tmp = NULL;
      int vol = 1;
      if (entry_p && entry_p->vsize)
      {
         do
         {
            if (tmp) free(tmp);
            tmp = get_volfn(entry_p->vtype, entry_p->rar_p, vol, entry_p->vlen, entry_p->vpos);
            tprintf("search for local directory in %s\n", tmp);
            if (vol == 1) /* first file */
            {
               password = getArcPassword(tmp, tmpbuf);
            }
            ++vol;
         } while (!listrar(path, &next, tmp, password));
         if (tmp) free(tmp);
      }
      else if (entry_p && entry_p->rar_p)
      {
         tprintf("search for local directory in %s\n", entry_p->rar_p);
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
   }
 
   if(!DIR_LIST_EMPTY(&dir_list))
   {
      sort_dir(&dir_list);
      dir_entry_list_t* next = dir_list.next;
      while(next)
      {
        filler(buffer, next->entry.name, next->entry.st, 0);
        next = next->next;
      }
      DIR_LIST_FREE(&dir_list);
   }

   return 0;
}

#undef NOF_FILTERS

void*
reader_task(void* arg)
{
   IOContext* op = (IOContext*)arg;
   op->terminate = 0;
   tprintf("Reader thread started : fp=0x%x\n", FH_TOFP(op->fh));

   int fd = op->pfd[0];
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
         tprintf("Reader thread wakeup (%d)\n", retval);
         char buf[2];
         ssize_t n;
         no_warn_result_ read(fd, buf, 1); /* consume byte */
         {
            if (buf[0]<2 && !feof(FH_TOFP(op->fh))) 
               (void)readTo(op->buf, FH_TOFP(op->fh), IOB_SAVE_HIST);
            if (buf[0])
            {
               tprintf("Reader thread acknowledge\n");
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
   tprintf("Reader thread stopped\n");
   pthread_exit(NULL);
}

static void
preload_index(IoBuf* buf, const char* path)
{
   char* r2i;
   ABS_ROOT(r2i, path);
   strcpy(&r2i[strlen(r2i)-3], "r2i");
   tprintf("preload_index() for %s\n", r2i);

   buf->idx.data_p = MAP_FAILED;
   int fd = open(r2i, O_RDONLY, S_IREAD);
   if (fd==-1)
   {
      return;
   }
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
      buf->idx.data_p = mmap(NULL, P_ALIGN_(h->size), PROT_READ, MAP_SHARED, fd, 0);
      munmap(h, sizeof(IdxHead));
      if (buf->idx.data_p == MAP_FAILED)
      {
         close(fd);
         return;
      }
      buf->idx.mmap = 1;
   } else {
      buf->idx.data_p = malloc(sizeof(IdxData));
      no_warn_result_ read(fd, buf->idx.data_p, sizeof(IdxHead));
      buf->idx.mmap = 0;
   }
   buf->idx.fd = fd;
}

static int
rar2_open(const char *path, struct fuse_file_info *fi)
{
   tprintf ("(%05d) %-08s%s [0x%08x][called from %05d]\n", getpid(), "OPEN", path, (int)(fi->fh), fuse_get_context()->pid);
   dir_elem_t* entry_p;
   char* root;

   errno = 0;
   pthread_mutex_lock(&file_access_mutex);
   entry_p = cache_path(path, NULL);
   pthread_mutex_unlock(&file_access_mutex);
   if (entry_p == NULL)
   {
      /* There was a cache miss and the file could not be found locally!
       * This is bad! To make sure the files does not really exist all 
       * rar archives need to be scanned for a matching file = slow! */
      CHK_FILTER;
      char* dir = alloca(strlen(path)+1);
      strcpy(dir, path);
      dir = dirname(dir);
      sync_dir(dir);
      pthread_mutex_lock(&file_access_mutex);
      entry_p = cache_path_get(path);
      pthread_mutex_unlock(&file_access_mutex);
      if (entry_p == NULL)
      {
         return -ENOENT;
      }
   }
   if ((fi->flags & 3) != O_RDONLY)
   {
      return -EACCES;
   }

   if (entry_p->rar_p == NULL)
   {
      ABS_ROOT(root, entry_p->file_p);
      return lopen(root, fi);
   }
   if (entry_p->offset != 0 && !entry_p->flags.mmap)
   {
      if (!FH_ISSET(fi->fh))
      {
         FILE* fp = fopen(entry_p->rar_p, "r");
         if (fp != NULL)
         {
            tprintf("Opened %s\n", entry_p->rar_p);
            IOContext* op = malloc(sizeof(IOContext));
            FH_SETFH(&op->fh, fp);
            FH_SETCONTEXT(&fi->fh, op);
            tprintf ("(%05d) %-08s%s [0x%08x]\n", getpid(), "ALLOC", path, FH_TOCONTEXT(fi->fh));
            op->pid = 0;
            op->entry_p = entry_p;
            op->seq = 0;
            op->buf = NULL;
            op->pos = 0;
            op->vno = -1; /* force a miss 1st time */
            op->terminate = 1;
            if (entry_p->vsize && 
                OBJ_SET(OBJ_PREOPEN_IMG) && 
                entry_p->flags.image)
            {
               int i = entry_p->vno_base;
               int j = i - 1;
               op->volHdl = malloc(MAX_NOF_OPEN_VOL * sizeof(VolHandle));
               memset(op->volHdl, 0, MAX_NOF_OPEN_VOL * sizeof(VolHandle));
               for (;j<MAX_NOF_OPEN_VOL;j++)
               {
                  char* tmp = get_volfn(op->entry_p->vtype, op->entry_p->rar_p, i++, op->entry_p->vlen, op->entry_p->vpos);
                  FILE* fp_ = fopen(tmp, "r");
                  if (fp_ == NULL) 
                  {
                     free(tmp);
                     break;
                  }
                  tprintf("Pre-open %s\n", tmp);
                  free(tmp);
                  op->volHdl[j].fp = fp_;
                  op->volHdl[j].pos = VOL_REAL_SZ - VOL_FIRST_SZ;
                  tprintf("SEEK src_off = %llu\n", op->volHdl[j].pos);
                  fseeko(fp_, op->volHdl[j].pos, SEEK_SET);
               }
            }
            else op->volHdl = NULL;
         }
         else return -errno;
      }
      return 0;
   }
   if (!FH_ISSET(fi->fh))
   {
      /* Open PIPE(s) and create child process */
      pid_t pid;
      void* mmap_addr;
      FILE* mmap_fp;
      int mmap_fd;
      FILE* fp = _popen(entry_p, &pid, &mmap_addr, &mmap_fp, &mmap_fd);
      if (fp!=NULL)
      {
         IoBuf* buf = malloc(sizeof(IoBuf));
         IOB_RST(buf);
         buf->idx.data_p = MAP_FAILED;
         buf->idx.fd = -1;
         preload_index(buf, path);
// XXX check for error from preload_index and release memory
         IOContext* op = malloc(sizeof(IOContext));
         op->mmap_addr = mmap_addr;
         op->mmap_fp = mmap_fp;
         op->mmap_fd = mmap_fd;
         op->entry_p = entry_p;
         op->seq = 0;
         op->buf = buf;
         op->pos = 0;
         FH_SETCONTEXT(&fi->fh, op);
         tprintf ("(%05d) %-08s%s [0x%08x]\n", getpid(), "ALLOC", path, FH_TOCONTEXT(fi->fh));
         FH_SETFH(&op->fh, fp);
         op->pid = pid;
         tprintf("pipe 0x%08x created towards child %d\n", FH_TOFP(op->fh), pid);
#ifdef USE_STATIC_WINDOW 
         /* Prefetch buffer and possible file header cache */
         size_t size = readTo(op->buf, fp, IOB_NO_HIST);
         size = size > FHD_SZ-1 ? FHD_SZ-1 : size;
         tprintf("Copying %d bytes to static window @ %08x\n", size, (unsigned int)op->buf->sbuf_p);
         memcpy(op->buf->sbuf_p, op->buf->data_p, size);
#endif

         /* Create pipe to be used between threads.
          * Both these pipes are used for communication between 
          * parent (this thread) and reader thread. One pipe is for 
          * requests (w->r) and the other is for responses (r<-w). */
         if (pipe(op->pfd) == -1) { perror("pipe"); return -EIO; }
         if (pipe(op->pfd2) == -1) { perror("pipe"); return -EIO; }
// XXX check for error release memory

         /* Create reader thread */
         op->terminate = 1;
         pthread_create(&op->thread, NULL,reader_task,(void*)op);
         while (op->terminate);
         WAKE_THREAD(op->pfd, 0);
      }
      else 
      {
         /* This is the best we can return here. So many different things
          * might go wrong and errno can actually be set to something that
          * FUSE is accepting and thus proceeds with next operation! */
         return -EIO;
      }
   }
   return 0;
}

static void*
rar2_init(struct fuse_conn_info *conn)
{
   tprintf ("init\n");

   pthread_mutex_init(&file_access_mutex, NULL);

   struct sigaction act;

#if 1
   /* Avoid child zombies for SIGCHLD */
   sigaction(SIGCHLD, NULL, &act);
   act.sa_handler = (void*)sig_handler;
   act.sa_flags |= SA_NOCLDWAIT;
   sigaction(SIGCHLD, &act, NULL);
#endif

   signal (SIGUSR1, (void*)sig_handler);
   
   sigaction(SIGSEGV, NULL, &act);
   sigemptyset(&act.sa_mask);
   act.sa_handler = (void*)sig_handler;
   act.sa_flags = SA_RESTART | SA_SIGINFO;
   sigaction(SIGSEGV, &act, NULL);

   return NULL;
}

static void
rar2_destroy(void *data)
{
   tprintf("destroy\n");
   if (src_path) free(src_path);
}

static int
rar2_flush(const char *path, struct fuse_file_info *fi)
{
   tprintf ("(%05d) %-08s%s [0x%08x][called from %05d]\n", getpid(), "FLUSH", path, FH_TOCONTEXT(fi->fh), fuse_get_context()->pid);
   char* root;
   ABS_ROOT(root, path);
   return lflush(root, fi);
}

static int
rar2_release(const char *path, struct fuse_file_info *fi)
{
   tprintf ("(%05d) %-08s%s [0x%08x]\n", getpid(), "RELEASE", path, FH_TOCONTEXT(fi->fh));
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
         WAKE_THREAD(op->pfd, 2);
         pthread_join(op->thread, NULL);
      }
      if (FH_TOFP(op->fh))
      {
         if (op->entry_p->offset && !op->entry_p->flags.mmap)
         {
            if (op->volHdl)
            {
               int j;
               for (j=0;j<MAX_NOF_OPEN_VOL;j++)
               {
                  if (op->volHdl[j].fp) fclose(op->volHdl[j].fp);
               }
               free(op->volHdl);
            }
            fclose(FH_TOFP(op->fh));
            tprintf("closing file handle 0x%x\n", FH_TOFP(op->fh));
         } 
         else
         {
            tprintf("closing pipe 0x%x\n", FH_TOFP(op->fh));
            close(op->pfd[0]);
            close(op->pfd[1]);
            close(op->pfd2[0]);
            close(op->pfd2[1]);
            if (_pclose(FH_TOFP(op->fh), op->pid) == -1)
            {
               perror("pclose");
            }
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
      tprintf("Releasing IO context fi->fh=0x%x\n", FH_TOCONTEXT(fi->fh));
      if (op->buf) 
      { 
         /* XXX clean up */
         if (op->buf->idx.data_p != MAP_FAILED && op->buf->idx.mmap) 
            munmap(op->buf->idx.data_p, P_ALIGN_(op->buf->idx.data_p->head.size));
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
}

static int
rar2_read(const char *path, char *buffer, size_t size, off_t offset,
          struct fuse_file_info *fi)
{
   tprintf ("read %s:%u:%lld fh=%llu\n", path, size, offset, FH_TOFH(fi->fh));

   dir_elem_t* entry_p;
   pthread_mutex_lock(&file_access_mutex);
   entry_p = cache_path_get(path);
   pthread_mutex_unlock(&file_access_mutex);
   if (entry_p)
   {
      if (entry_p->rar_p == NULL)
      {
         char* root;
         tprintf("%d calling lread() for %s\n", getpid(),path);
         ABS_ROOT(root, entry_p->file_p);
         return lread(root, buffer, size, offset, fi);
      }
      if (entry_p->offset && !entry_p->flags.mmap)
      {
         tprintf("%d calling lread_raw() for %s\n", getpid(), path);
         return lread_raw(buffer, size, offset, fi); 
      }
      tprintf("%d calling lread_rar() for %s\n", getpid(), path);
      int res = lread_rar(buffer, size, offset, fi);
      return res; 
   }
   return -ENOENT;
}

static int
rar2_create(const char * path, mode_t mode, struct fuse_file_info* fi)
{
   tprintf("create %s\n", path);
   /* fake cache entry */
   pthread_mutex_lock(&file_access_mutex);
   dir_elem_t* e_p = cache_path_alloc(path);
   memset(&e_p->stat, 0, sizeof(struct stat));
   e_p->stat.st_mode = mode;
   e_p->stat.st_nlink = 1;
   if (!e_p->name_p) e_p->name_p = strdup(path);
   if (!e_p->file_p) e_p->file_p = strdup(path);
   pthread_mutex_unlock(&file_access_mutex);
   FH_ZERO(&fi->fh);
   return 0;
}

static int
rar2_utime(const char * path, const struct timespec tv[2])
{
   tprintf("utime() %s\n", path);
   return 0;
}

#define CONSUME_LONG_ARG() { \
   int i;\
   --argc;\
   --optind;\
   for(i=optind;i<argc;i++){\
      argv[i] = argv[i+1];}\
}

#include <sched.h>
int
main(int argc, char* argv[])
{
   int opt;
   char *end;
   char *helpargv[2]={NULL,"-h"};

   /* mapping of FUSE file system operations */
   static struct fuse_operations rar2_operations = {
      .init    = rar2_init,
      .create  = rar2_create,
      .utimens = rar2_utime,
      .destroy = rar2_destroy,
      .getattr = rar2_getattr,
      .readdir = rar2_readdir,
      .open    = rar2_open,
      .read    = rar2_read,
      .release = rar2_release,
      .flush   = rar2_flush
   };

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
#if defined ( __linux ) && defined ( __cpu_set_t_defined )
      {"no-smp",        no_argument,       NULL, OBJ_ADDR(OBJ_NO_SMP)},
#endif
      {"img-type",      required_argument, NULL, OBJ_ADDR(OBJ_IMG_TYPE)},
      {"version",       no_argument,       NULL, 'V'},
      {"help",          no_argument,       NULL, 'h'},
      {NULL,0,NULL,0}
   };

   if (RARGetDllVersion() < RAR_DLL_VERSION)
   {
      printf("libunrar.so (v%d.%d%s) or compatible library not found\n",
         RARVER_MAJOR, RARVER_MINOR, !RARVER_BETA ? "" : " beta");
      return -1;
   }
   if (fuse_version() < FUSE_VERSION)
   {
      printf("libfuse.so.%d.%d or compatible library not found\n",
         FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);
      return -1;
   }

#ifdef HAS_GLIBC_CUSTOM_STREAMS_
   /* Check fmemopen() support */
   {
       char tmp[64];
       glibc_test = 1;
       fclose(fmemopen(tmp, 64, "r"));
       glibc_test = 0;
   }
#endif

   configdb_init();

   //openlog("rarfs2",LOG_CONS|LOG_NDELAY|LOG_PERROR|LOG_PID,LOG_DAEMON);
   opterr=0;
   while((opt=getopt_long(argc,argv,"Vhfo:",longopts,NULL))!=-1)
   {
      if(opt=='V'){
         printf("rar2fs v1.11.1 (DLL version %d, FUSE version %d)    Copyright (C) 2009-2011 Hans Beckerus\n",
            RARGetDllVersion(),
            FUSE_VERSION);
         printf("This program comes with ABSOLUTELY NO WARRANTY.\n"
                "This is free software, and you are welcome to redistribute it under\n"
                "certain conditions; see <http://www.gnu.org/licenses/> for details.\n");
         return 0;
      }
      if(opt=='h'){
         helpargv[0]=argv[0];
         fuse_main(2,helpargv,NULL,NULL);
         printf("\nrar2fs options:\n");
         printf("    --img-type=E1[;E2...]   additional image file type extensions beyond the default (.iso;.img;.nrg)\n");
         printf("    --show-comp-img\t    show image file types also for compressed archives\n");
         printf("    --preopen-img\t    prefetch volume file descriptors for image file types\n");
         printf("    --fake-iso[=E1[;E2...]] fake .iso extension for specified image file types\n");
         printf("    --exclude=F1[;F2...]    exclude file filter\n");
         printf("    --seek-length=n\t    set number of volume files that are traversed in search for headers [0=All]\n");
         printf("    --seek-depth=n\t    set number of levels down RAR files are parsed inside main archive [0=0ff]\n");
         printf("    --no-idx-mmap\t    use direct file I/O instead of mmap() for .r2i files\n");
         printf("    --unrar-path=PATH\t    path to external unrar binary (overide unrarlib)\n");
         printf("    --no-password\t    disable password file support\n");
#if defined ( __linux ) && defined ( __cpu_set_t_defined )
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
      printf("Usage: %s [options] <root dir> <mount point>\n",*argv);
      return -1;
   }

   /* Validate src/dst path */
   {
      char p1[PATH_MAX];
      char p2[PATH_MAX];
      char* a1 = realpath(argv[optind], p1);
      char* a2 = realpath(argv[optind+1], p2);
      if (!a1||!a2) 
      {
         printf("invalid root and/or mount point\n");
         exit(-1);
      }
      if (!strcmp(a1,a2))
      {
         printf("root and mount must not point to the same location\n");
         exit(-1);
      }
      src_path = strdup(a1);
   }

   init_cache();

   long ps = -1;
#if defined ( _SC_PAGE_SIZE )
   ps = sysconf(_SC_PAGE_SIZE);
#elif defined ( _SC_PAGESIZE )
   ps = sysconf(_SC_PAGESIZE);
#endif
   if (ps != -1) page_size = ps;
   else          page_size = 4096;

#if defined ( __linux ) && defined ( __cpu_set_t_defined )
   if (OBJ_SET(OBJ_NO_SMP))
   {
      cpu_set_t cpu_mask;
      CPU_ZERO(&cpu_mask);
      CPU_SET(1, &cpu_mask);
      if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask)) {
         perror("sched_setaffinity");
         exit(-1);
      }
   }
#endif

   argv[optind]=argv[optind+1];
   argc-=1;

   struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
   fuse_opt_parse(&args, NULL, NULL, NULL);
   fuse_opt_add_arg(&args, "-osync_read,fsname=rar2fs,default_permissions");
   fuse_opt_add_arg(&args, "-s");

   return fuse_main(args.argc, args.argv, &rar2_operations, NULL);
}

