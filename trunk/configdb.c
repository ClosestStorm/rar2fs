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

#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>
#include "common.h"
#include "configdb.h"

#define MAX_NOF_CFG_OBJ (11)
static CfgObj config_objects[MAX_NOF_CFG_OBJ] = 
{
   {NULL, 0, 0, 0, 1},
   {NULL, 0, 0, 0, 0},
   {NULL, 0, 0, 0, 0},
   {NULL, 0, 0, 0, 0},
   {NULL, 0, 0, 0, 0},
   {NULL, 0, 0, 0, 0},
   {NULL, 0, 0, 0, 0},
   {NULL, 0, 0, 0, 0},
   {NULL, 0, 0, 0, 0},
   {NULL, 0, 0, 0, 0},
   {NULL, 0, 0, 0, 0}
};

CfgObj* config_objects_ = &config_objects[0];

#define OBJ_(o) (config_objects_+(o))
#define OBJ_STR_ u.v_arr_str,char*,0
#define OBJ_INT_ u.v_arr_int,long,1
#define ADD_OBJ_(o, s1, mtn) ADD_OBJ__(o, s1, mtn)
#define ADD_OBJ__(o, s1, m, t, n) { \
  if ((OBJ_(o))->n_elem == (OBJ_(o))->n_max) { \
     OBJ_(o)->n_max += 16; \
     OBJ_(o)->m = (t*)realloc((t*)OBJ_(o)->m,OBJ_(o)->n_max * sizeof(t*)); \
  }\
  if (!n) OBJ_(o)->m[OBJ_(o)->n_elem++] = (t)strdup(s1); \
  else    OBJ_(o)->m[OBJ_(o)->n_elem++] = (t)strtoul(s1,NULL,10); \
}

int collect_obj(int obj, char* s)
{
   if (obj < 0 || obj>=MAX_NOF_CFG_OBJ) return 1;

   char* s1 = NULL;
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
           no_warn_result_ fread(s1, 1, st.st_size, fp);
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
         ADD_OBJ_(obj, s1, OBJ_INT_);
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
      tprintf("config object %d : ", obj);
      for(i = 0; i<OBJ_(obj)->n_elem;i++)
        if (obj!=OBJ_SEEK_DEPTH&&obj!=OBJ_SEEK_LENGTH)
           tprintf("\"%s\" ", OBJ_(obj)->u.v_arr_str[i]);
        else
           tprintf("\"%d\" ", OBJ_(obj)->u.v_arr_int[i]);
      tprintf("\n");
   }
#endif
   return 0;
}

void configdb_init()
{
    /* Prepare known built-in default image file types */
    OBJ_(OBJ_IMG_TYPE)->is_set = 1;
    ADD_OBJ_(OBJ_IMG_TYPE, ".iso", OBJ_STR_);
    ADD_OBJ_(OBJ_IMG_TYPE, ".img", OBJ_STR_);
    ADD_OBJ_(OBJ_IMG_TYPE, ".nrg", OBJ_STR_);
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
         if (!strcmp(basename(path), OBJ_STR(OBJ_EXCLUDE, i)))
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
            if (!strcmp((path)+(strlen(path)-l), OBJ_STR(obj, i)))
               return l-1;
            ++i;
         }
      }
   }
   return 0;
}

