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

#ifndef DIRLIST_H
#define DIRLIST_H

struct stat;

typedef struct dir_entry_list dir_entry_list_t;
struct dir_entry_list {
        struct dir_entry {
                char *name;
                struct stat *st;
                int valid;
        } entry;
        dir_entry_list_t *next;
};

#define DIR_LIST_RST(l) \
        do {\
                (l)->next = NULL;\
                (l)->entry.name = NULL;\
                (l)->entry.st = NULL;\
        } while(0)

#define DIR_ENTRY_ADD(l, n, s) \
        do {\
                (l)->next = malloc(sizeof(dir_entry_list_t));\
                if ((l)->next) {\
                        (l)=(l)->next;\
                        (l)->entry.name=strdup(n);\
                        (l)->entry.st=(s);\
                        (l)->entry.valid=1; /* assume entry is valid */ \
                        (l)->next = NULL;\
                }\
        } while (0)

#define DIR_LIST_EMPTY(l) (!(l)->next)

#define DIR_LIST_FREE(l) \
        do {\
                dir_entry_list_t* next = (l)->next;\
                while(next) {\
                        dir_entry_list_t* tmp = next;\
                        next = next->next;\
                        free(tmp->entry.name);\
                        free(tmp);\
                }\
        } while(0)

#endif
