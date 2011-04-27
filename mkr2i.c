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
*/

#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include "common.h"

#define MAP_FAILED_   (0x1)
#define R2I_MAGIC     (htonl(0x72326900))   /* 'r2i ' */

#define STR_DUP(s, path) \
      int len = strlen(path);\
      (s) = alloca(len + 1); \
      strcpy((s), path);

typedef enum 
{
   M_UNKNOWN,
   M_RIFF,
   M_EBML
} Mode;

static int
count_c(char c, char* s)
{
   int cnt = 0;
   while(*s)
     if (*s++ == c) ++cnt;
   return cnt;
}

static void
parse_riff(IdxHead* h, FILE* fp, off_t sz)
{
   char s[256];
   char* needle = NULL;
   while (!needle && !feof(fp))
   {
      fgets(s, sizeof(s), fp);
      needle = strstr(s, "idx1");
   }
   char* tmp = alloca(strlen(needle));
   char* tmp2 = tmp;
   int cnt = 0;
   while(*needle)
   {
       if (*needle != 32 || !cnt) { *tmp++ = *needle; ++cnt; }
       if (*needle != 32) cnt = 0;
       ++needle;
   }
   *tmp = 0;
   tmp = tmp2;
   cnt = 0;
   while (*tmp)
   {
     if (*tmp++ == 32) ++cnt;
     if (cnt == 2)
     {
        needle = tmp;
        tmp2 = tmp;
        while(*needle)
        {
           if (*needle != ',') *tmp++ = *needle;
           ++needle;
        }
        *tmp = 0;
        h->offset = atoll(tmp2)&~4095; /* align offset */
        h->size = sz - h->offset;
        return;
     } 
   }
}

static void 
parse_ebml(IdxHead* h, FILE* fp, off_t sz)
{
   char s[256];
   char* needle = NULL;
   while (!needle && !feof(fp))
   {
      fgets(s, sizeof(s), fp);
      needle = strstr(s, "Cues");
   }
   if (needle)
   {
      needle = strstr(needle, "pos.:");
      if (needle)
      {
          unsigned int i1;
          unsigned int i2;
          unsigned int i3;
          unsigned int i4;
          int n = count_c(',', needle);
          switch(n)
          {
              case 0:
                 i4 = 0;
                 i3 = 0;
                 i2 = 0;
                 sscanf(needle, "pos.: %3u)", &i1);
              break;
              case 1:
                 i4 = 0;
                 i3 = 0;
                 sscanf(needle, "pos.: %3u,%3u)", &i2,&i1);
              break;
              case 2:
                 i4 = 0;
                 sscanf(needle, "pos.: %3u,%3u,%3u)", &i3,&i2,&i1);
              break;
              case 3:
                 sscanf(needle, "pos.: %3u,%3u,%3u,%3u)", &i4,&i3,&i2,&i1);
              break;
              default:
                 return; /* not supported */
              break;
          }
          /* XXX add error checking + size check sanity */
          h->offset = 0+i4*1000000000ULL+i3*1000000+i2*1000+i1;
          h->offset = h->offset&~4095; /* align offset */
          h->size = sz - h->offset;
      }
   }
};

static char*
map_file(int fd, size_t size)
{
  if (flock(fd, LOCK_EX|LOCK_NB) == -1) return NULL;

  /* Prepare the file */
  ftruncate(fd, size);

  /* Map the file into address space */
  char* addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) return NULL;
  return addr;
}

int main(int argn, char* argv[])
{
   setlocale(LC_CTYPE, "");
   if (argn != 3)
   {
      printf("Usage: mkr2i <dump file> <source file>\n");
      printf("   <dump file>     AVI-Mux GUI RIFF/EBML dump file (txt)\n");
      printf("   <source file>   RIFF(.avi)/EBML(.mkv) source file\n");
      exit(0);
   }
   char* dump = argv[1];
   char* src_file = argv[2];

   FILE* fd_dump = fopen(dump, "r");
   if (!fd_dump)
   {
      printf("Failed to open dump file %s\n", dump);
      exit(-1);
   }
   FILE* fd_src = fopen(src_file, "r");
   if (!fd_src)
   {
      printf("Failed to open source file %s\n", src_file);
      fclose(fd_dump);
      exit(-1);
   }

   Mode mode;
   char dump_magic[10];
   fscanf(fd_dump, "%s", dump_magic);
   if (!strcmp(dump_magic, "EBML")) mode = M_EBML; else
   if (!strcmp(dump_magic+3, "RIFF")) mode = M_RIFF; else
   {
      printf("Invalid dump file\n");
      exit(-1);
   }

   /* get size of source file */
   struct stat stat_src;
   fstat(fileno(fd_src),&stat_src);
   /* XXX error check */

   IdxHead head;
   head.size = 0;
   head.magic = R2I_MAGIC;
   head.version = 0;
   head.spare = 0;

   switch (mode)
   {
   case M_RIFF: parse_riff(&head, fd_dump, stat_src.st_size); break;
   case M_EBML: parse_ebml(&head, fd_dump, stat_src.st_size); break;
   default: break;
   }

   if (head.size)
   {
      char* dest;
      STR_DUP(dest, src_file);
      strcpy(&dest[strlen(dest)-4], ".r2i");
      fseeko(fd_src, head.offset, SEEK_SET);

      int fd = open(dest, O_RDWR|O_CREAT, S_IREAD|S_IWRITE);
      if (fd==-1) return;
      size_t map_size = (head.size+sizeof(IdxHead)+4096)&~4095;

      char* addr = map_file(fd, map_size);
      if (!addr)
      {
         printf("Internal error %x\n", MAP_FAILED_); 
         return;
      }

      char* tmp = addr;
      memcpy(tmp, &head, sizeof(IdxHead));
      tmp+=sizeof(IdxHead);
      size_t n = fread(tmp, 1, head.size, fd_src); 
      /* flush to medium */
      msync(addr, map_size, MS_SYNC); 
      munmap(addr, map_size);
      close(fd);
   }

   fclose(fd_src);
   fclose(fd_dump);
}
