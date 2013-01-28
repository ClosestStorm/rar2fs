/*
    Copyright (C) 2009-2013 Hans Beckerus (hans.beckerus@gmail.com)

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

#ifndef CONFIGDB_H_
#define CONFIGDB_H_

#define IS_IMG(s) \
        ((OBJ_CNT(OBJ_IMG_TYPE) && chk_obj(OBJ_IMG_TYPE, s)) || \
        (OBJ_CNT(OBJ_FAKE_ISO) && chk_obj(OBJ_FAKE_ISO, s)))

#define IS_ISO(s) (!strcasecmp((s)+(strlen(s)-4), ".iso"))
#define IS_AVI(s) (!strcasecmp((s)+(strlen(s)-4), ".avi"))
#define IS_MKV(s) (!strcasecmp((s)+(strlen(s)-4), ".mkv"))

#define CHK_FILTER(path) \
        (OBJ_CNT(OBJ_EXCLUDE) && chk_obj(OBJ_EXCLUDE, (char*)(path)))

#define OBJ_BASE    (1000)
#define OBJ_ADDR(o) ((o) + OBJ_BASE)
#define OBJ_ID(a)   ((a) - OBJ_BASE)

enum {
        OBJ_SRC = 0,
        OBJ_DST,
        OBJ_EXCLUDE,
        OBJ_FAKE_ISO,
        OBJ_IMG_TYPE,
        OBJ_PREOPEN_IMG,
        OBJ_SHOW_COMP_IMG,
        OBJ_NO_IDX_MMAP,
        OBJ_SEEK_LENGTH,
        OBJ_SEEK_DEPTH,
        OBJ_NO_PASSWD,
        OBJ_NO_SMP,
        OBJ_UNRAR_PATH,
        OBJ_NO_LIB_CHECK,
        OBJ_HIST_SIZE,
        OBJ_BUFF_SIZE,
        OBJ_SAVE_EOF,
        OBJ_LAST_ENTRY /* Must *always* be last */
};

struct cfg_obj {
        union
        {
                long *v_arr_int;
                char **v_arr_str;
                void *p;
        } u;
        int is_set;
        int n_elem;
        int n_max;
        int read_from_file;
        int type;
};

#define OBJ_CNT(o)     (config_objects_[(o)].n_elem)
#define OBJ_STR(o, n)  (OBJ_SET(o)?config_objects_[(o)].u.v_arr_str[(n)]:NULL)
#define OBJ_STR2(o, n) (OBJ_SET(o)?config_objects_[(o)].u.v_arr_str[(n)]:"")
#define OBJ_INT(o, n)  (OBJ_SET(o)?config_objects_[(o)].u.v_arr_int[(n)]:0)
#define OBJ_SET(o)     (config_objects_[(o)].is_set)

extern struct cfg_obj *config_objects_;

int collect_obj(int obj, const char*);
int chk_obj(int obj, char*);
void configdb_init();
void configdb_destroy();

#endif
