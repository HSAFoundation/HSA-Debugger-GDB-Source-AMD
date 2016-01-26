/*
HSAIL GDB utilities functions

Copyright (c) 2015 ADVANCED MICRO DEVICES, INC.  All rights reserved.
This file includes code originally published under

Copyright (C) 1986-2014 Free Software Foundation, Inc.

This file is part of GDB.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stdlib.h>
#include <string.h>

/* GDB headers */
#include "defs.h"
#include "gdb_assert.h"

/* HSAIL GDB headers */
#include "hsail-utils.h"

void hsail_utils_copy_string(char** dest_str, const char* src_str)
{
  {
    int str_len = 0;
    char* l_str = NULL;
    if (NULL == dest_str || NULL == src_str)
    {
      gdb_assert(NULL != dest_str);
      gdb_assert(NULL != src_str);
      return;
    }

    /* +1 for the null-terminator */
    str_len = strlen(src_str) + 1;
    l_str = xmalloc(str_len);
    gdb_assert(NULL != l_str);

    if (NULL != l_str)
    {
      memset(l_str, '\0', str_len);
      strcpy(l_str, src_str);

      /* Free the old buffer before assigning the new one to it */
      if(NULL != *dest_str)
      {
        free(*dest_str);
        *dest_str = NULL;
      }
      *dest_str = l_str;
    }
  }
}

void hsail_utils_copy_wavedim3(HsailWaveDim3* dest_wavedim, const HsailWaveDim3* src_wavedim)
{
  gdb_assert(src_wavedim != NULL);
  gdb_assert(dest_wavedim != NULL);

  dest_wavedim->x = src_wavedim->x;
  dest_wavedim->y = src_wavedim->y;
  dest_wavedim->z = src_wavedim->z;
}

bool hsail_utils_compare_wavedim3(const HsailWaveDim3* op1, const HsailWaveDim3* op2)
{
  gdb_assert(op1 != NULL);
  gdb_assert(op2 != NULL);
  return ((op1->x == op2->x) && (op1->y == op2->y) && (op1->z == op2->z));
}
