/*
   HSAIL Functions print HSAIL variable and work item information

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

#include <string.h>
#include <ctype.h>

/* Headers for shared mem */
#include <sys/ipc.h>
#include <sys/shm.h>

/* GDB headers */
#include "defs.h"
#include "expression.h"
#include "format.h"
#include "gdb_assert.h"
#include "ui-out.h"
#include "valprint.h"
#include "value.h"

#include "hsail-breakpoint.h"
#include "hsail-kernel.h"
#include "hsail-print.h"
#include "hsail-tdep.h"
#include "hsail-utils.h"

#include "CommunicationControl.h"

/* HwDbgFacilities: */
#include "FacilitiesInterface.h"

/* forward declaration of internal function that should not be in the header file*/
static struct value* hsail_print_var_info_with_location(HwDbgInfo_variable dbgVar, size_t var_size);

static struct value* hsail_print_var_with_addr(const char* print_name, uint64_t addr, char* printFormat, size_t* format_size);

static void hsail_print_all_vars_with_addr(uint64_t addr);

static void hsail_print_no_wave_msg(struct ui_out* uiout, const char* param_str);

HsailPrintStatus gLastPrintError = HSAIL_PRINT_UNKNOWN;

HsailPrintStatus hsail_print_get_last_error(void)
{
  return gLastPrintError;
}

void hsail_print_no_wave_msg(struct ui_out* uiout, const char* param_str)
{
  gdb_assert(NULL != uiout);
  gdb_assert(NULL != param_str);
  ui_out_text(uiout,"No dispatch is presently active\n");
  ui_out_text(uiout,"[info hsail ");
  ui_out_text(uiout, param_str);
  ui_out_text(uiout, "] command needs a dispatch to be active on the device\n");
}

bool hsail_parse_print_request(const char* pHsail_pr_expr, const char** var_name)
{
  size_t l = 0;

  /* Input check: */
  gdb_assert(NULL != var_name);
  gdb_assert(NULL != pHsail_pr_expr);

  /* Allowed formats:
   * hsai:var print information on var */

 /* Get the size: */
  l = strlen(pHsail_pr_expr);

 /* Verify the prefix: */
  if (5 > l)
      return false;

  if ('h' != pHsail_pr_expr[0] ||
      's' != pHsail_pr_expr[1] ||
      'a' != pHsail_pr_expr[2] ||
      'i' != pHsail_pr_expr[3] ||
      'l' != pHsail_pr_expr[4])
      return false;

  /* Move up the pointer: */
  pHsail_pr_expr += 5;
  l -= 5;

  /* Skip leading whitespace: */
  SKIP_LEADING_SPACES(pHsail_pr_expr, l);

  /* Else, verify the next character is a colon: */
  if (':' != pHsail_pr_expr[0])
      return false;

  /* Move up the pointer again: */
  ++pHsail_pr_expr;
  --l;

  /* Skip leading whitespace: */
  SKIP_LEADING_SPACES(pHsail_pr_expr, l);

  /* the var_name is found including white spaces in the end */
  *var_name = pHsail_pr_expr;

  return true;
}

void hsail_print_cleanup(void* pData)
{
  char funcExp[256] = "";
  struct expression* expr = NULL;

  sprintf(funcExp, "FreeVarValue()");

  /* Create the expression */
  expr = parse_expression (funcExp);

  /* Call the evaluator */
  evaluate_expression (expr);
}

struct value* hsail_print_expression(const char* exp, char* printFormat, size_t* format_size)
{
  char* var_name = NULL;
  char* clean_name = NULL;
  int varNameLen = 0;
  int pos = 0;
  uint64_t addr = 0;
  struct value* retVal = NULL;

  gLastPrintError = HSAIL_PRINT_SUCCESS;

  gdb_assert(NULL != exp);
  gdb_assert(NULL != printFormat);
  gdb_assert(NULL != format_size);

  /* Get the var name from the expression but it might include extra information after the name */
  if (!hsail_parse_print_request(exp, &var_name))
  {
    /* This point should never be reached since hsail_parse_print_request is called before calling
     * hsail_print_expression and its return value is checked
     */
    gdb_assert(false);
    gLastPrintError = HSAIL_PRINT_WRONG_FORMAT;
    return NULL;
  }

  gdb_assert(NULL != var_name);

  /* Copy the var_name without any space or extra information after the var_name: */
  varNameLen = strlen(var_name);
  clean_name = malloc(varNameLen + 1);
  gdb_assert(NULL != clean_name);

  /* Trim trailing spaces */
  for (;(NULL != var_name && ' ' != *var_name && '\t' != *var_name && '\r' != *var_name && '\n' != *var_name) && 0 < varNameLen;
        ++var_name, --varNameLen, pos++)
  {
    clean_name[pos] = *var_name;
  }
  clean_name[pos] = '\0';

  addr = hsail_get_current_pc();

  /* if addr is 0 then it is probably that there are no waves and the break point
   * is at the start of the kernel so notify the user and do nothing
   */
  if (0 == addr)
  {
    gLastPrintError = HSAIL_PRINT_NO_WAVES;
    return NULL;
  }
  //retVal =
  // hsail_print_all_vars_with_addr(addr);

  /* do the actual printing of the data */
  retVal = hsail_print_var_with_addr(clean_name, addr, printFormat, format_size);

  /* delete the cleaned var_name after using it */
  free(clean_name);

  return retVal;
}

struct value* hsail_print_var_with_addr(const char* print_name, uint64_t addr, char* printFormat, size_t* format_size)
{
  HwDbgInfo_err dbgErr = 0;
  HwDbgInfo_debug dbgInfo = NULL;
  HwDbgInfo_variable dbgVar = NULL;
  char* var_name = NULL;
  size_t var_name_len = 0;
  char* type_name = NULL;
  size_t type_name_len = 0;
  size_t var_size = 0;
  bool is_constant = false;
  HwDbgInfo_encoding encoding = HWDBGINFO_VENC_NONE;
  bool is_output = false;
  struct value* retVal = NULL;
  bool isRegister = false;
  int printNameLength = 0;

  gdb_assert(NULL != print_name);
  gdb_assert(0 != addr);
  gdb_assert(NULL != printFormat);

  printNameLength = strlen(print_name);

  dbgInfo = hsail_init_hwdbginfo(NULL);
  /* check for low level register */

  if (printNameLength >= 3 &&
      print_name[0] == '$' &&
      (print_name[1] == 'c' || print_name[1] == 'd' || print_name[1] == 's' || print_name[1] == 'q'))
  {
    int currentPos = 2;
    while (currentPos < printNameLength && isdigit(print_name[currentPos]))
    {
      currentPos++;
    }
    if (currentPos >= printNameLength)
    {
      isRegister = true;
    }
  }

  if (isRegister)
  {
    dbgVar = hwdbginfo_low_level_variable(dbgInfo, addr, true, print_name, &dbgErr);
  }
  else
  {
    dbgVar = hwdbginfo_variable(dbgInfo, addr, true, print_name, &dbgErr);
  }

  if (dbgErr != HWDBGINFO_E_SUCCESS)
  {
    printf("hsail-printf var not found error %d\n", dbgErr);
    gLastPrintError = HSAIL_PRINT_VAR_NOT_FOUND;
    return NULL;
  }

  dbgErr = hwdbginfo_variable_data(dbgVar, 0, var_name, &var_name_len, 0, type_name, &type_name_len, &var_size, &encoding, &is_constant, &is_output);
  if (dbgErr != HWDBGINFO_E_SUCCESS)
  {
    printf("hsail-printf get var data info error %d\n", dbgErr);
    return NULL;
  }

  var_name = (char*)malloc(var_name_len+1);
  type_name = (char*)malloc(type_name_len+1);

  gdb_assert(NULL != var_name);
  gdb_assert(NULL != type_name);

  memset(var_name, 0, var_name_len+1);
  memset(type_name, 0, type_name_len+1);

  dbgErr = hwdbginfo_variable_data(dbgVar, var_name_len, var_name, NULL, type_name_len, type_name, NULL, &var_size, &encoding, &is_constant, &is_output);
  if (dbgErr != HWDBGINFO_E_SUCCESS)
  {
    printf("hsail-printf get var data error %d\n", dbgErr);
    return NULL;
  }

  /* temporary work around due to bug in dwarf missing data*/
  if (0 == var_size)
  {
    var_size = 8;
  }

  /* Get the constant data */
  if (is_constant)
  {
    /* TODO: Need to print out the constant? */
    void* varValue = malloc(8); /* largest buffer needed */

    if(NULL == varValue)
    {
      printf("hsail-printf cannot malloc for varValue\n");
      free_current_contents(&var_name);
      free_current_contents(&type_name);
      return NULL;
    }
    memset(varValue, 0, 8);

    dbgErr = hwdbginfo_variable_const_value(dbgVar, var_size, varValue);

    if (dbgErr != HWDBGINFO_E_SUCCESS)
    {
      printf("hsail-printf get var const data %d\n", dbgErr);
    }

    free_current_contents(&varValue);
    free_current_contents(&var_name);
    free_current_contents(&type_name);
    return NULL;
  }

  retVal = hsail_print_var_info_with_location(dbgVar, var_size);

  free_current_contents(&var_name);
  free_current_contents(&type_name);

  /* set the format */
  printFormat[0] = 0;
  switch (encoding)
    {
    case HWDBGINFO_VENC_BOOLEAN: printFormat[0] = 'a';
      break;

    case HWDBGINFO_VENC_FLOAT: printFormat[0] = 'f';
      break;

    case HWDBGINFO_VENC_INTEGER: printFormat[0] = 'd';
      break;

    case HWDBGINFO_VENC_UINTEGER: printFormat[0] = 'u';
      break;

    case HWDBGINFO_VENC_CHARACTER: printFormat[0] = 'c';
      break;

    case HWDBGINFO_VENC_UCHARACTER: printFormat[0] = 'u';
      break;

    case HWDBGINFO_VENC_POINTER:
    default:
      break;
    }

  if (NULL != format_size)
  {
    *format_size = var_size;
  }

  printFormat[1] = 0;
  return retVal;
}

struct value* hsail_print_var_info_with_location(HwDbgInfo_variable dbgVar, size_t var_size)
{
  /* Exp data */
  struct expression* expr = NULL;
  struct value* retVal = NULL;

  int reg_type = 0;
  unsigned int reg_num = 0;
  bool deref_value = false;
  unsigned int offset = 0;
  unsigned int resource = 0;
  unsigned int isa_memory_region = 0;
  unsigned int piece_offset = 0;
  unsigned int piece_size = 0;
  int const_add = 0;

  char funcExp[256] = "";

  /* Get all the variable location information */
  HwDbgInfo_err dbgErr = hwdbginfo_variable_location(dbgVar, &reg_type, &reg_num, &deref_value, &offset, &resource, &isa_memory_region, &piece_offset, &piece_size, &const_add);

  if (dbgErr != HWDBGINFO_E_SUCCESS)
  {
    printf("dbgErr in getting the var location:%d\n" , dbgErr);
    return NULL;
  }

  /* build the buffer for the Exp eval for this function (safely assume that the initial buffer is max 256 char long):
   *void* GetVarValue(int reg_type, size_t var_size, unsigned int reg_num, bool deref_value, unsigned int offset, unsigned int resource, unsigned int isa_memory_region, unsigned int piece_offset, unsigned int piece_size, int const_add)
   */
  sprintf(funcExp, "GetVarValue(%d,%d,%d,%d,%d,%d,%d,%d,%d,%d)",reg_type,((int)var_size), ((int)reg_num), ((int)deref_value ? 1 : 0), ((int)offset), ((int)resource), ((int)isa_memory_region), ((int)piece_offset), ((int)piece_size), ((int)const_add));

  /* print information to screen for debugging */
  /*printf("reg type:%d var size:%d, reg num:%d\nderef val:%d, offset:%d, resource:%d\nisa mem region:%d, piece offset:%d, piece size:%d, const add:%d\n", reg_type,((int)var_size), ((int)reg_num), ((int)deref_value ? 1 : 0), ((int)offset), ((int)resource), ((int)isa_memory_region), ((int)piece_offset), ((int)piece_size), ((int)const_add));*/

  /* Create the expression */
  expr = parse_expression (funcExp);

  /* Call the evaluator */
  retVal = evaluate_expression (expr);

  return retVal;
}


void hsail_print_all_vars_with_addr(uint64_t addr)
{
  HwDbgInfo_err dbgErr = 0;
  HwDbgInfo_debug dbgInfo = hsail_init_hwdbginfo(NULL);
  /* HwDbgInfo_variable dbgVar = hwdbginfo_variable(dbgInfo, addr, true, var_name, &dbgErr); */
  HwDbgInfo_variable* vars = NULL;
  size_t var_count = 0;
  int nVar = 0;
  char* var_name = NULL;
  size_t var_name_len = 0;
  char* type_name = NULL;
  size_t type_name_len = 0;
  size_t var_size = 0;
  bool is_constant = false;
  bool is_output = false;
  HwDbgInfo_code_location loc = NULL;
  HwDbgInfo_linenum line_num = 0;
  char* file_name = NULL;
  size_t file_name_len = 0;
  HwDbgInfo_encoding encoding = HWDBGINFO_VENC_NONE;

  if (dbgInfo == NULL)
  {
    printf("dbg info NULL");
    return ;
  }

  dbgErr = hwdbginfo_frame_variables(dbgInfo, addr, -1, false, 0, vars, &var_count);

  if (dbgErr != HWDBGINFO_E_SUCCESS)
  {
    printf("dbgErr in getting the num vars%d\n" , dbgErr);
    return ;
  }

  printf ("Number of vars: %zu\n", var_count);
  gdb_assert(0 != var_count);

  /* get the line location for validation */
  dbgErr = hwdbginfo_addr_to_line(dbgInfo, addr, &loc);

  if (dbgErr != HWDBGINFO_E_SUCCESS)
  {
    printf("dbgErr in getting code location %d\n" , dbgErr);
    return ;
  }

  dbgErr = hwdbginfo_code_location_details(loc, &line_num, 0, file_name, &file_name_len);
  if (dbgErr != HWDBGINFO_E_SUCCESS)
  {
    printf("dbgErr in getting location data %d\n" , dbgErr);
    return ;
  }

  vars = (HwDbgInfo_variable*)malloc(sizeof(HwDbgInfo_variable)*var_count);
  if (NULL == vars)
  {
    printf("failed to allocate vars\n");
    return ;
  }

  memset(vars, 0, sizeof(HwDbgInfo_variable)*var_count);

  dbgErr = hwdbginfo_frame_variables(dbgInfo, addr, -1, false, var_count, vars, NULL);
  if (dbgErr != HWDBGINFO_E_SUCCESS)
  {
    printf("dbgErr in getting the vars data%d\n" , dbgErr);
    free_current_contents(&vars);
    return ;
  }

  /* print information for each variable: */
  for (nVar = 0 ; nVar < var_count ; nVar++)
  {
    var_name = NULL;
    var_name_len = 0;
    type_name = NULL;
    type_name_len = 0;
    var_size = 0;
    is_constant = false;
    is_output = false;

    dbgErr = hwdbginfo_variable_data(vars[nVar], 0, var_name, &var_name_len, 0, type_name, &type_name_len, &var_size, &encoding, &is_constant, &is_output);
    if (dbgErr != HWDBGINFO_E_SUCCESS)
    {
      printf("dbgErr in getting var info %d %d\n", nVar, dbgErr);
      free_current_contents(&vars);
      return ;
    }

    var_name = (char*)malloc(var_name_len+1);
    type_name = (char*)malloc(type_name_len+1);

    gdb_assert(NULL != var_name);
    gdb_assert(NULL != type_name);

    memset(var_name, 0, var_name_len+1);
    memset(type_name, 0, type_name_len+1);

    dbgErr = hwdbginfo_variable_data(vars[nVar], var_name_len, var_name, NULL, type_name_len, type_name, NULL, &var_size, &encoding, &is_constant, &is_output);
    if (dbgErr != HWDBGINFO_E_SUCCESS)
    {
      printf("dbgErr in getting var data %d %d\n", nVar, dbgErr);
      free_current_contents(&vars);
      free_current_contents(&var_name);
      free_current_contents(&type_name);
      return ;
    }
    /* temporary work around due to bug in dwarf missing data*/
    if (0 == var_size)
    {
      var_size = 8;
    }

    free_current_contents(&var_name);
    free_current_contents(&type_name);
  }

  free_current_contents(&vars);
  /*  HwDbgInfo_err hwdbginfo_variable_data(HwDbgInfo_variable var, size_t name_buf_len, char* var_name, size_t* var_name_len, size_t type_name_buf_len, char* type_name, size_t* type_name_len, size_t* var_size, bool* is_constant, bool* is_output); */
  hwdbginfo_release_code_locations(&loc, 1);
}

void hsail_print_wave_info (struct ui_out* uiout, int from_tty)
{
  int num_waves = hsail_tdep_get_active_wave_count();
  gdb_assert(NULL != uiout);
  ui_out_text(uiout,"Wave info\n");
  printf_filtered("Number of Active Waves: %d\n",num_waves);
}

/* build a vector of work groups, this is used when printing the work-groups, but also when printing a specific work group so it will related to the same
   position since in the wave buffer two elements can have the same work group so a vector is created where each work group appears only once and the
   index refer to the work group and from this vector the work group info can be retrieved */
static HsailWaveDim3* gs_workgroup_vector = NULL;
static int gs_num_workgroups_in_vector = 0;

static void hsail_build_workgroups_vector(void)
{
  int nWave = 0;
  int checked_group = 0;
  bool workgroup_found = false;
  int num_waves = hsail_tdep_get_active_wave_count();
  HsailAgentWaveInfo* wave_info_buffer = (HsailAgentWaveInfo*)hsail_tdep_map_wave_buffer();

  gdb_assert(NULL != wave_info_buffer);

  /* allocated memory for a buffer to hold the work group we already found. worst case is every wave belongs to a different group */
  free_current_contents(&gs_workgroup_vector);
  gs_workgroup_vector= (HsailWaveDim3*)malloc(sizeof(HsailWaveDim3)*num_waves);
  gdb_assert(NULL != gs_workgroup_vector);

  gs_num_workgroups_in_vector = 0;

  for (nWave = 0 ; nWave < num_waves ; nWave++)
  {
    workgroup_found = false;
    /* check if the work group was not already found */
    for (checked_group = 0 ; checked_group < gs_num_workgroups_in_vector ; checked_group++)
    {
      if (gs_workgroup_vector[checked_group].x == wave_info_buffer[nWave].workGroupId.x &&
          gs_workgroup_vector[checked_group].y == wave_info_buffer[nWave].workGroupId.y &&
          gs_workgroup_vector[checked_group].z == wave_info_buffer[nWave].workGroupId.z)
          {
            workgroup_found = true;
            break;
          }
    }

    /* if new group in the wave add it */
    if (!workgroup_found)
    {
      gs_workgroup_vector[gs_num_workgroups_in_vector].x = wave_info_buffer[nWave].workGroupId.x;
      gs_workgroup_vector[gs_num_workgroups_in_vector].y = wave_info_buffer[nWave].workGroupId.y;
      gs_workgroup_vector[gs_num_workgroups_in_vector].z = wave_info_buffer[nWave].workGroupId.z;
      gs_num_workgroups_in_vector++;
    }
  }

  /* release the wave buffer */
  hsail_tdep_unmap_wave_buffer((void*)wave_info_buffer);
}

void hsail_print_workgroups_info (HsailWaveDim3 active_work_group, struct ui_out* uiout, int from_tty)
{
  int nWorkgroup = 0;
  /* get the waves info */

  int num_waves = hsail_tdep_get_active_wave_count();
  int flattened_id = 0;
  int workgroupsizex = 0;
  int workgroupsizey = 0;
  struct hsail_dispatch* active_dispatch = hsail_kernel_active_dispatch();

  HsailAgentWaveInfo* wave_info_buffer = (HsailAgentWaveInfo*)hsail_tdep_map_wave_buffer();

  char index_buffer[10] = "";
  char wg_id_buffer[30] = "";
  char flat_id_buffer[10] = "";

  bool found_workgroup = false;

  gdb_assert(NULL != uiout);
  gdb_assert(NULL != active_dispatch);

  if (0 == num_waves || NULL == wave_info_buffer)
  {
    hsail_print_no_wave_msg(uiout, "work-groups");
    return;
  }

  /* build the vector list */
  hsail_build_workgroups_vector();

  /* print header */
  ui_out_text(uiout,"Active Work-groups Information\n");
  printf_filtered("%5s%15s%27s\n","Index","Work-group ID","Flattened Work-group ID");

  /* calculate work group size */
  if (NULL != active_dispatch)
  {
    workgroupsizex = (active_dispatch->work_items.x == 0 ? 1 : active_dispatch->work_items.x) / (active_dispatch->work_groups_size.x == 0 ? 1 : active_dispatch->work_groups_size.x);
    workgroupsizey = (active_dispatch->work_items.y == 0 ? 1 : active_dispatch->work_items.y) / (active_dispatch->work_groups_size.y == 0 ? 1 : active_dispatch->work_groups_size.y);
  }

  for (nWorkgroup = 0 ; nWorkgroup < gs_num_workgroups_in_vector ; nWorkgroup++)
  {
    found_workgroup = false;
    /* based on the equation in HSA programmer Ref page 22 sec 2.2.2 */
    if (NULL != active_dispatch)
    {
      flattened_id = gs_workgroup_vector[nWorkgroup].x +
                     gs_workgroup_vector[nWorkgroup].y * workgroupsizex +
                     gs_workgroup_vector[nWorkgroup].z * workgroupsizex * workgroupsizey;
    }

    if (gs_workgroup_vector[nWorkgroup].x == active_work_group.x &&
        gs_workgroup_vector[nWorkgroup].y == active_work_group.y &&
        gs_workgroup_vector[nWorkgroup].z == active_work_group.z)
        {
          found_workgroup = true;
        }

    sprintf(index_buffer,"%s%d", found_workgroup ? "*" : "", nWorkgroup);
    sprintf(wg_id_buffer,"%d,%d,%d",gs_workgroup_vector[nWorkgroup].x,
                                    gs_workgroup_vector[nWorkgroup].y,
                                    gs_workgroup_vector[nWorkgroup].z);
    sprintf(flat_id_buffer,"%d",flattened_id);

    printf_filtered("%5s%15s%27s\n", index_buffer, wg_id_buffer, flat_id_buffer);
  }

  /* release the vector data */
  free_current_contents(&gs_workgroup_vector);
  gs_num_workgroups_in_vector = 0;

  /* release the wave buffer */
  hsail_tdep_unmap_wave_buffer((void*)wave_info_buffer);

}

static void hsail_print_wave_data(HwDbgInfo_debug dbgInfo, HsailAgentWaveInfo* wave_info_buffer, int wave_index, int index_to_show, HsailWaveDim3 work_item, bool use_work_item, bool mark_active_item)
{
  /* vars used to pass through the exec mask */
  int nExec = 0;
  bool firstExec = true;
  int last_bit_num = 0;
  int first_bit_num = 0;
  /* current_bit_mask used with the exec mask */
  uint64_t current_bit_mask = 1;
  struct hsail_dispatch* active_dispatch = hsail_kernel_active_dispatch();
  bool found_work_item = false;

  /* get the source line information */
  HwDbgInfo_err dbgErr = 0;
  HwDbgInfo_addr addr = NULL;
  HwDbgInfo_code_location loc = NULL;
  HwDbgInfo_linenum line_num = 0;
  char* file_name = NULL;
  size_t file_name_len = 0;

  char index_buffer[10] = "";
  char wave_addr_buffer[30] = "";
  char wi_id1_buffer[30] = "";
  char wi_id2_buffer[30] = "";
  char wi_id_buffer[60] = "";
  char abs_wi_id1_buffer[30] = "";
  char abs_wi_id2_buffer[30] = "";
  char abs_wi_id_buffer[60] = "";
  char source_line_buffer[256] = "";
  char pc_buffer[30] = "";

  gdb_assert(NULL != wave_info_buffer);

  /* print the index and the wave front id */

  for (nExec = 0 ; nExec < 64 ; nExec++)
  {
    if (wave_info_buffer[wave_index].execMask & current_bit_mask)
    {
      /* use the work item if it is the filter work item or not using filter at all */
      if (!use_work_item || (work_item.x == wave_info_buffer[wave_index].workItemId[nExec].x &&
                             work_item.y == wave_info_buffer[wave_index].workItemId[nExec].y &&
                             work_item.z == wave_info_buffer[wave_index].workItemId[nExec].z))
      {
        last_bit_num = nExec;
        if (firstExec)
        {
          firstExec = false;
          first_bit_num = nExec;
          found_work_item = true;
        }
      }
    }
    /* move the current_bit_mask to the next bit so the next exec mask bit will be checked */
    current_bit_mask = current_bit_mask<<1;
  }

  if (!use_work_item || found_work_item)
  {
    sprintf(index_buffer,"%s%d",mark_active_item ? "*": "", index_to_show);
    sprintf(wave_addr_buffer,"0x%x",wave_info_buffer[wave_index].waveAddress);

    sprintf(wi_id1_buffer,"%2d,%2d,%2d",wave_info_buffer[wave_index].workItemId[first_bit_num].x,
                                        wave_info_buffer[wave_index].workItemId[first_bit_num].y,
                                        wave_info_buffer[wave_index].workItemId[first_bit_num].z );
    if (!use_work_item)
    {
      sprintf(wi_id2_buffer," - %2d,%2d,%2d",wave_info_buffer[wave_index].workItemId[last_bit_num].x,
                                             wave_info_buffer[wave_index].workItemId[last_bit_num].y,
                                             wave_info_buffer[wave_index].workItemId[last_bit_num].z );
    }
    else
    {
      sprintf(wi_id2_buffer,"%s","");
    }
    sprintf(wi_id_buffer,"%s%s",wi_id1_buffer,wi_id2_buffer);

    /* print absolute work-item id */
    if (NULL != active_dispatch)
    {
      uint32_t baseX = wave_info_buffer[wave_index].workGroupId.x * active_dispatch->work_groups_size.x;
      uint32_t baseY = wave_info_buffer[wave_index].workGroupId.y * active_dispatch->work_groups_size.y;
      uint32_t baseZ = wave_info_buffer[wave_index].workGroupId.z * active_dispatch->work_groups_size.z;
      sprintf(abs_wi_id1_buffer,"%2d,%2d,%2d",
                      baseX + wave_info_buffer[wave_index].workItemId[first_bit_num].x,
                      baseY + wave_info_buffer[wave_index].workItemId[first_bit_num].y,
                      baseZ + wave_info_buffer[wave_index].workItemId[first_bit_num].z);
      if (!use_work_item)
      {
        sprintf(abs_wi_id2_buffer," - %2d,%2d,%2d",
                      baseX + wave_info_buffer[wave_index].workItemId[last_bit_num].x,
                      baseY + wave_info_buffer[wave_index].workItemId[last_bit_num].y,
                      baseZ + wave_info_buffer[wave_index].workItemId[last_bit_num].z);
      }
      else
      {
        sprintf(abs_wi_id2_buffer, "%s", "");
      }
      sprintf(abs_wi_id_buffer, "%s%s", abs_wi_id1_buffer, abs_wi_id2_buffer);
    }
    else
    {
      sprintf(abs_wi_id_buffer,"%s","");
    }
    /* print the source line and pc */
    dbgErr = hwdbginfo_nearest_mapped_addr(dbgInfo, wave_info_buffer[wave_index].pc,  &addr);
    if (dbgErr == HWDBGINFO_E_SUCCESS)
    {
      dbgErr = hwdbginfo_addr_to_line(dbgInfo, addr, &loc);
    }

    if (dbgErr == HWDBGINFO_E_SUCCESS)
    {
      dbgErr = hwdbginfo_code_location_details(loc, &line_num, 0, file_name, &file_name_len);
    }

    if (dbgErr == HWDBGINFO_E_SUCCESS)
    {
      sprintf(source_line_buffer,"temp_source@line %d",((int)line_num));
    }
    else
    {
      sprintf(source_line_buffer,"dbginfo error");
    }

    sprintf(pc_buffer,"0x%x",((int)wave_info_buffer[wave_index].pc));

    printf_filtered("%5s%15s%27s%27s%12s%23s\n", index_buffer, wave_addr_buffer, wi_id_buffer, abs_wi_id_buffer, pc_buffer, source_line_buffer);

    /* release loc */
    hwdbginfo_release_code_locations(&loc, 1);
  }
}

void hsail_print_specific_workgroup_by_id_info (int index, struct ui_out* uiout, int from_tty)
{
  /* vars used to loop through the waves and count the groups */
  int nWorkgroup = 0;
  int nWave = 0;
  int count_index = 0;
  HsailWaveDim3 dummy_work_item = {-1, -1, -1};
  bool workgroup_found = false;
  /* get the waves info */
  int num_waves = hsail_tdep_get_active_wave_count();
  HsailAgentWaveInfo* wave_info_buffer = (HsailAgentWaveInfo*)hsail_tdep_map_wave_buffer();

  /* get the source line information */
  HwDbgInfo_debug dbgInfo = NULL;

  /* active dispatch to calculate the flattened id */
  struct hsail_dispatch* active_dispatch = hsail_kernel_active_dispatch();
  int workgroupnumX = 0;
  int workgroupnumY = 0;
  int flattened_id = 0;

  gdb_assert(NULL != uiout);

  /* calculate work group size */
  if (NULL != active_dispatch)
  {
    workgroupnumX = (active_dispatch->work_items.x == 0 ? 1 : active_dispatch->work_items.x) / (active_dispatch->work_groups_size.x == 0 ? 1 : active_dispatch->work_groups_size.x);
    workgroupnumY = (active_dispatch->work_items.y == 0 ? 1 : active_dispatch->work_items.y) / (active_dispatch->work_groups_size.y == 0 ? 1 : active_dispatch->work_groups_size.y);
  }

  if (NULL == wave_info_buffer || num_waves == 0)
  {
    hsail_print_no_wave_msg(uiout, "work-group <id>");
    return ;
  }

  dbgInfo = hsail_init_hwdbginfo(NULL);

  /* create the work-group vector and verify the index is valid */
  hsail_build_workgroups_vector();

  /* pass through all the work-groups in the vector and look for the flattened id */
  for (nWorkgroup = 0 ; nWorkgroup < gs_num_workgroups_in_vector ; nWorkgroup++)
  {

    if (NULL != active_dispatch)
    {
      flattened_id = gs_workgroup_vector[nWorkgroup].x +
                     gs_workgroup_vector[nWorkgroup].y * workgroupnumX +
                     gs_workgroup_vector[nWorkgroup].z * workgroupnumX * workgroupnumY;
    }

    if (flattened_id == index)
    {
      /* print the header */
      printf_filtered("Information for Work-group %d\n",index);
      printf_filtered("%5s%15s%27s%27s%12s%23s\n","Index","Wavefront ID","Work-item ID","Absolute Work-item ID","PC","Source line");

      /* pass through all the waves and check if their information match the work group of the index needed */
      for (nWave = 0 ; nWave < num_waves ; nWave++)
      {
          if (gs_workgroup_vector[nWorkgroup].x == wave_info_buffer[nWave].workGroupId.x &&
              gs_workgroup_vector[nWorkgroup].y == wave_info_buffer[nWave].workGroupId.y &&
              gs_workgroup_vector[nWorkgroup].z == wave_info_buffer[nWave].workGroupId.z)
          {
            hsail_print_wave_data(dbgInfo, wave_info_buffer, nWave, count_index, dummy_work_item, false, false);
            count_index++;
          }
      }
      workgroup_found = true;
    }
  }

  if (!workgroup_found)
  {
    ui_out_text(uiout,"Provided work-group ID not found.\n");
  }

  /* release the vector data */
  free_current_contents(&gs_workgroup_vector);
  gs_num_workgroups_in_vector = 0;

  /* release the wave buffer */
  hsail_tdep_unmap_wave_buffer((void*)wave_info_buffer);
}

void hsail_print_specific_workgroup_info (unsigned int* workgroupid, struct ui_out* uiout, int from_tty)
{
  /* convert the work_group_id to flattened_id */
  /* active dispatch to calculate the flattened_id */
  struct hsail_dispatch* active_dispatch = hsail_kernel_active_dispatch();
  int workgroupnumX = 0;
  int workgroupnumY = 0;
  int flattened_id = 0;

  gdb_assert(NULL != workgroupid);
  gdb_assert(NULL != uiout);

  if (NULL != active_dispatch)
  {
    /* calculate number of work group in X and Y dimensions */
    workgroupnumX = (active_dispatch->work_items.x == 0 ? 1 : active_dispatch->work_items.x) / (active_dispatch->work_groups_size.x == 0 ? 1 : active_dispatch->work_groups_size.x);
    workgroupnumY = (active_dispatch->work_items.y == 0 ? 1 : active_dispatch->work_items.y) / (active_dispatch->work_groups_size.y == 0 ? 1 : active_dispatch->work_groups_size.y);

    flattened_id = workgroupid[0] +
                   workgroupid[1] * workgroupnumX +
                   workgroupid[2] * workgroupnumX * workgroupnumY;
  }

  hsail_print_specific_workgroup_by_id_info(flattened_id, uiout, from_tty);
}

void hsail_print_workitem_info (HsailWaveDim3 active_work_group, HsailWaveDim3 active_work_item, bool mark_active_item, struct ui_out* uiout, int from_tty)
{
  int nWave = 0;
  /* get the waves info */
  HsailAgentWaveInfo* wave_info_buffer = (HsailAgentWaveInfo*)hsail_tdep_map_wave_buffer();

  /* get the source line information */
  int num_waves = hsail_tdep_get_active_wave_count();
  HwDbgInfo_debug dbgInfo = NULL;

  gdb_assert(NULL != uiout);
  if (NULL == wave_info_buffer || num_waves == 0)
  {
    hsail_print_no_wave_msg(uiout, "work-item");
    return ;
  }

  dbgInfo = hsail_init_hwdbginfo(NULL);

  printf_filtered("Information for Work-item\n");
  printf_filtered("%5s%15s%27s%27s%12s%23s\n","Index","Wavefront ID","Work-item ID","Absolute Work-item ID","PC","Source line");
  for (nWave = 0 ; nWave < num_waves ; nWave++)
  {
      if (active_work_group.x == wave_info_buffer[nWave].workGroupId.x &&
          active_work_group.y == wave_info_buffer[nWave].workGroupId.y &&
          active_work_group.z == wave_info_buffer[nWave].workGroupId.z)
      {
        hsail_print_wave_data(dbgInfo, wave_info_buffer, nWave, 0, active_work_item, true, mark_active_item);
      }
  }

  /* release the wave buffer */
  hsail_tdep_unmap_wave_buffer((void*)wave_info_buffer);
}
