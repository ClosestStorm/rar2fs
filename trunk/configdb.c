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

#ifdef HAVE_CONFIG_H
#include <config.h>
#else
#include <compat.h>
#endif
#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>
#include "common.h"
#include "configdb.h"

#define MAX_NOF_CFG_OBJ (16)
static CfgObj config_objects[MAX_NOF_CFG_OBJ] = 
{
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 1, 0},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 1},
   {{NULL,}, 0, 0, 0, 0, 1},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 0},
   {{NULL,}, 0, 0, 0, 0, 1},
   {{NULL,}, 0, 0, 0, 0, 1}
};

CfgObj* config_objects_ = &config_objects[0];

#define OBJ_(o) (config_objects_+(o))
#define OBJ_STR_ u.v_arr_str,char*
#define OBJ_INT_ u.v_arr_int,long
#define ADD_OBJ_(o, s1, mt) ADD_OBJ__(o, s1, mt)
#define ADD_OBJ__(o, s1, m, t) { \
  if ((OBJ_(o))->n_elem == (OBJ_(o))->n_max) { \
     OBJ_(o)->n_max += 16; \
     OBJ_(o)->m = (t*)realloc((t*)OBJ_(o)->m,OBJ_(o)->n_max * sizeof(t*)); \
  }\
  if (IS_STR_(o)) OBJ_(o)->m[OBJ_(o)->n_elem++] = (t)strdup(s1); \
  else            OBJ_(o)->m[OBJ_(o)->n_elem++] = (t)strtoul(s1,NULL,10); \
}
#define CLR_OBJ_(o) { \
   OBJ_(obj)->is_set = 0; \
   if ((OBJ_(o))->n_elem && IS_STR_(o)) { \
      int i = (OBJ_(o))->n_elem; \
      while (i--) { \
         free((void*)OBJ_(o)->u.v_arr_str[i]);\
      } \
      OBJ_(o)->n_elem = 0; \
   }\
}
#define IS_INT_(o) (OBJ_(o)->type)
#define IS_STR_(o) (!OBJ_(o)->type)

int
collect_obj(int obj, char* s)
{
   char* s1 = NULL;

   if (obj < 0 || obj>=MAX_NOF_CFG_OBJ) return 1;

   OBJ_(obj)->is_set = 1;
   if (OBJ_(obj)->read_from_file && s && *s == '/')
   {
      FILE* fp = fopen(s, "r");
      if (fp)
      {
        struct stat st;
        (void)fstat(fileno(fp), &st);
        s1 = malloc(st.st_size*2);
        if (s1)
        {
           s = s1;
           NO_UNUSED_RESULT fread(s1, 1, st.st_size, fp);
           while(*s1)
           {
              if(*s1=='\n') *s1=';';
              s1++;
           }
           s1 = s;
        }
        fclose(fp);
      }
   }
   else
   {
      s1 = s;
      s = NULL;
   }
   if (!s1) return 0;

   switch (obj) 
   {
      case OBJ_SEEK_LENGTH:
      case OBJ_SEEK_DEPTH:
      case OBJ_HIST_SIZE:
         ADD_OBJ_(obj, s1, OBJ_INT_);
         break; 
      case OBJ_SRC:
      case OBJ_DST:
         ADD_OBJ_(obj, s1, OBJ_STR_);
         break; 
      default:
      {
         /* One could easily have used strsep() here but I choose not to:
          * "This function suffers from the same problems as strtok().
          * In particular, it modifies the original string. Avoid it." */
         char* s2 = s1;
         if (strlen(s1))
         {
            while ((s2 = strchr(s2, ';')))
            {
               *s2++ = 0;
               if (strlen(s1) > 1) ADD_OBJ_(obj, s1, OBJ_STR_);
               s1 = s2;
            }
            if(*s1) ADD_OBJ_(obj, s1, OBJ_STR_);
         }
      }
      break;
   }
   if (s) free(s);

#ifdef DEBUG_
   {
      int i;
      printd(5, "config object %d : ", obj);
      for(i = 0; i<OBJ_(obj)->n_elem;i++)
        if (obj!=OBJ_SEEK_DEPTH&&obj!=OBJ_SEEK_LENGTH)
           printd(5, "\"%s\" ", OBJ_(obj)->u.v_arr_str[i]);
        else
           printd(5, "\"%ld\" ", OBJ_(obj)->u.v_arr_int[i]);
      printd(5, "\n");
   }
#endif
   return 0;
}

static void 
reset_obj(int obj, int init)
{
   if (obj < 0 || obj>=MAX_NOF_CFG_OBJ) return;

   CLR_OBJ_(obj);
   if (init)
   {
      switch (obj)
      {
         case OBJ_IMG_TYPE:
            OBJ_(OBJ_IMG_TYPE)->is_set = 1;
            ADD_OBJ_(OBJ_IMG_TYPE, ".iso", OBJ_STR_);
            ADD_OBJ_(OBJ_IMG_TYPE, ".img", OBJ_STR_);
            ADD_OBJ_(OBJ_IMG_TYPE, ".nrg", OBJ_STR_);
            break;
      default:
            break;
      }
   }
}

void 
configdb_init()
{
   int i = MAX_NOF_CFG_OBJ;
   while (i--) reset_obj(i, 1);
}

void
configdb_destroy()
{
   int i = MAX_NOF_CFG_OBJ;
   while (i--) reset_obj(i, 0);
}

#undef ADD_OBJ_
#undef OBJ_
#undef OBJ_INT_
#undef OBJ_STR_

static inline int
get_ext_len(char* s)
{
   char* s1 = s+strlen(s);
   while (s1 != s && *s1 != '.') --s1;
   return s1==s?0:strlen(s1);
}

int chk_obj(int obj, char* path)
{
   int i = 0;
   if (obj==OBJ_EXCLUDE)
   {
      while (i!=OBJ_CNT(obj))
      {
         char* tmp =  OBJ_STR(OBJ_EXCLUDE, i);
         if (!strcmp(basename(path), tmp?tmp:""))
            return 1;
         ++i;
      }
   }
   else if (obj==OBJ_FAKE_ISO||obj==OBJ_IMG_TYPE)
   {
      int l = get_ext_len(path);
      if (l>1)
      {
         while (i!=OBJ_CNT(obj))
         {
            char* tmp =  OBJ_STR(obj, i);
            if (!strcmp((path)+(strlen(path)-l), tmp?tmp:""))
               return l-1;
            ++i;
         }
      }
   }
   return 0;
}

