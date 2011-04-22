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

#ifndef CONFIGDB_H
#define CONFIGDB_H

#define IS_IMG(s) \
   ((OBJ_CNT(OBJ_IMG_TYPE) && chk_obj(OBJ_IMG_TYPE, s)) || \
   (OBJ_CNT(OBJ_FAKE_ISO) && chk_obj(OBJ_FAKE_ISO, s)))
#define IS_ISO(s) (!strcasecmp((s)+(strlen(s)-4), ".iso"))

#define CHK_FILTER \
   if (OBJ_CNT(OBJ_EXCLUDE) && \
       chk_obj(OBJ_EXCLUDE, (char*)path)\
   ) return -ENOENT

#define OBJ_ADDR(o) ((o)+1000)
#define OBJ_BASE(a) ((a)-1000)

#define OBJ_EXCLUDE       (0)
#define OBJ_FAKE_ISO      (1)
#define OBJ_IMG_TYPE      (2)
#define OBJ_PREOPEN_IMG   (3)
#define OBJ_SHOW_COMP_IMG (4)
#define OBJ_NO_IDX_MMAP   (5)
#define OBJ_SEEK_LENGTH   (6)
#define OBJ_SEEK_DEPTH    (7)
#define OBJ_NO_PASSWD     (8)
#define OBJ_NO_SMP        (9)
#define OBJ_UNRAR_PATH    (10)
#define OBJ_NO_LIB_CHECK  (11)

typedef struct
{
  union
  {
     long* v_arr_int;
     char** v_arr_str;
     void* p;
  } u;
  int is_set;
  int n_elem;
  int n_max;
  int read_from_file;
  int type;
} CfgObj;

#define OBJ_CNT(o)    (config_objects_[(o)].n_elem)
#define OBJ_STR(o, n) (OBJ_SET(o)?config_objects_[(o)].u.v_arr_str[(n)]:NULL)
#define OBJ_INT(o, n) (OBJ_SET(o)?config_objects_[(o)].u.v_arr_int[(n)]:0)
#define OBJ_SET(o)    (config_objects_[(o)].is_set)

extern CfgObj* config_objects_;

int collect_obj(int obj, char*);
void reset_obj(int obj);
int chk_obj(int obj, char*);
void configdb_init();

#endif
