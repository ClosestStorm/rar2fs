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

#include <memory.h>
#include "common.h"
#include "filecache.h"
#include "configdb.h"

extern char* src_path;

#define PATH_CACHE_SZ  (1024)
static dir_elem_t path_cache[PATH_CACHE_SZ];

/* djb2 xor variant (favored by Bernstein) */
static unsigned int
get_hash(const char* s)
{
   unsigned int hash = 5381;
   int c;

   while((c = *s++))
   {
      /* hash = hash * 33 ^ c */
      hash = ((hash << 5) + hash) ^ c;
   }
   return hash & (PATH_CACHE_SZ - 1);
}

#define FREE_CACHE_MEM(e)\
{\
   if ((e)->name_p)     free ((e)->name_p);\
   if ((e)->rar_p)      free ((e)->rar_p);\
   if ((e)->file_p)     free ((e)->file_p);\
   if ((e)->password_p) free ((e)->password_p);\
}

dir_elem_t*
cache_path_alloc(const char* path)
{
   dir_elem_t* p = &path_cache[get_hash(path)];
   if (p->rar_p)
   {
      if (p->name_p && !strcmp(path, p->name_p)) return p;
      while (p->next_p)
      {
         p = p->next_p;
         if (p->name_p && !strcmp(path, p->name_p)) return p;
      }
      p->next_p = malloc(sizeof(dir_elem_t));
      p = p->next_p;
      memset(p, 0, sizeof(dir_elem_t));
   }
   return p;
}

dir_elem_t*
cache_path_get(const char* path)
{
   int hash = get_hash(path);
   dir_elem_t* p = &path_cache[hash];
   while (p)
   {
      if (p->name_p && !strcmp(path, p->name_p)) return p;
      p = p->next_p;
   }
   return NULL;
}

dir_elem_t*
cache_path(const char* path, struct stat *stbuf)
{
   dir_elem_t* e_p = cache_path_get(path);
   if (e_p && !e_p->flags.fake_iso)
   {
      if (stbuf)
      {
         memcpy(stbuf, &e_p->stat, sizeof(struct stat));
      }
      return e_p;
   }
   else /* CACHE MISS */
   {
      struct stat st;
      char* root;

      tprintf("CACHE MISS %s   (collision: %s)\n", path, (e_p && e_p->name_p) ? e_p->name_p : "no");

      /* Do _NOT_ remember fake .ISO entries between getattr() calls */
      if (e_p && e_p->flags.fake_iso) inval_cache_path(path);
      
      ABS_ROOT(root, path);
      if(!stat(root, stbuf?stbuf:&st))
      {
         tprintf("STAT retrieved for %s\n", root);
         return LOCAL_FS_ENTRY;
      }
      else
      {
         if (OBJ_SET(OBJ_FAKE_ISO))
         {
            /* Check if the missing file might be a fake .iso file */
            if(IS_ISO(root))
            {
               int i;
               int res;
               int obj = OBJ_CNT(OBJ_FAKE_ISO) ? OBJ_FAKE_ISO : OBJ_IMG_TYPE;

               /* Try the image file extensions one by one */
               for (i = 0; i < OBJ_CNT(obj); i++)
               {
                  char* tmp = (OBJ_STR(obj,i));
                  int l = strlen(tmp?tmp:"");
                  char* root1 = strdup(root);
                  if (l>4)
                     root1 = realloc(root1, strlen(root1)+1+(l-4));
                  strcpy(root1+(strlen(root1)-4), tmp?tmp:"");
                  res = stat(root1, &st);
                  if (!res)
                  {
                     e_p = cache_path_alloc(path);
                     e_p->name_p = strdup(path);
                     e_p->file_p = strdup(path);
                     e_p->flags.fake_iso = 1;
                     if (l>4)
                        e_p->file_p = realloc(e_p->file_p, strlen(path)+1+(l-4));
                     /* back-patch *real* file name */
                     strncpy(e_p->file_p+(strlen(e_p->file_p)-4), tmp?tmp:"", l);
                     *(e_p->file_p+(strlen(path)-4+l)) = 0;
                     memcpy(&e_p->stat, &st, sizeof(struct stat));
                     if (stbuf)
                     {
                        memcpy(stbuf, &st, sizeof(struct stat));
                     }
                     free(root1);
                     return e_p;
                  }
                  free(root1);
               }
            }
         }
      }
   }
   return NULL;
}

void
inval_cache_path(const char* path)
{
   int i;
   if (path)
   {
      int hash = get_hash(path);
      tprintf("Invalidating cache path %s\n", path);
      dir_elem_t* e_p = &path_cache[hash];
      dir_elem_t* p = e_p;
      /* Search collision chain */
      while (p->next_p)
      {
         dir_elem_t* prev_p = p;
         p = p->next_p;
         if (p->name_p && !strcmp(path, p->name_p))
         {
            FREE_CACHE_MEM(p);
            prev_p->next_p = p->next_p;
            free(p);
            /* Entry purged. We can leave now. */
            return;
         }
      }
      /* Entry not found in collision chain.
       * Most likely it is in the bucket, but double check. */
      if (e_p->name_p && !strcmp(e_p->name_p, path))
      {
         FREE_CACHE_MEM(e_p);
         memset(e_p, 0, sizeof(dir_elem_t));
      }
   }
   /* Invalidate entire cache */
   else
   {
      tprintf("Invalidating all cache entries\n");
      for (i = 0; i < PATH_CACHE_SZ;i++)
      {
         dir_elem_t* e_p = &path_cache[i];
         dir_elem_t* p = e_p;

         /* Search collision chain */
         while (p->next_p)
         {
            p = p->next_p;
            FREE_CACHE_MEM(p);
            free(p);
         }
         FREE_CACHE_MEM(e_p);
         memset(e_p, 0, sizeof(dir_elem_t));
      }
   }
}

void
init_cache()
{
   memset(path_cache, 0, sizeof(dir_elem_t)*PATH_CACHE_SZ);
}
