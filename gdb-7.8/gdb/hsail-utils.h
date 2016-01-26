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


#if !defined (HSAIL_UTILS_H)
#define HSAIL_UTILS_H 1

#include "CommunicationControl.h"

/* Helper macros: */

#define SKIP_LEADING_SPACES(str, str_len) \
  while ((0 < str_len) && (' ' == *str || '\t' == *str || '\r' == *str || '\n' == *str)) \
  { ++str; --str_len; }

#define TRIM_TAILING_SPACES(str, str_len) \
  while ((0 < str_len) && (' ' == str[str_len - 1] || '\t' == str[str_len - 1] || '\r' == str[str_len - 1] || '\n' == str[str_len - 1])) \
  { --str_len; }


/* Allocate memory for *dest_str and copy src_str into it, and append a null-terminator. *dest_str cannot be char array.*/
void hsail_utils_copy_string(char** dest_str, const char* src_str);

void hsail_utils_copy_wavedim3(HsailWaveDim3* dest_wavedim, const HsailWaveDim3* src_wavedim);

bool hsail_utils_compare_wavedim3(const HsailWaveDim3* op1, const HsailWaveDim3* op2);

#endif /* HSAIL_UTILS_H */
