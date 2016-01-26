/*
   HSAIL commands for GNU debugger GDB.

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

/* \todo: Cleanup the naming conventions of all the functions in this file
 * hsail_cmd_COMMANDNAME_command()
 * It is basically appending hsail_cmd to the GDB naming for a command
 * */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GDB headers */
#include "defs.h"
#include "gdbtypes.h"
#include "cli/cli-cmds.h"
#include "command.h"
#include "expression.h"
#include "gdb_assert.h"
#include "value.h"

/* hsail-gdb headers */
#include "hsail-cmd.h"
#include "hsail-fifo-control.h"
#include "hsail-kernel.h"
#include "hsail-print.h"
#include "hsail-tdep.h"
#include "hsail-utils.h"

/* The header files shared with the agent*/
#include "CommunicationControl.h"

/* forward declaration of functions */
static void hsail_info_command(char *arg, int from_tty);
static void hsail_command(char *arg, int from_tty);

void _initialize_hsailcmd (void);

/* A buffer that is used by gdb's set command processing logic to save the
 * arguments passed to the "set hsail" command
 * For e.g: for set hsail logging on, the "logging on" should be save to this buffer
 * */
char* g_hsail_argument_buff = NULL;

static const int gs_hsail_argument_buff_len = 256;

/* active work group and work item */
static const HsailWaveDim3 gs_unknown_wave_dim = {-1,-1,-1};
static HsailWaveDim3 gs_active_work_group = {-1,-1,-1};
static HsailWaveDim3 gs_active_work_item = {-1,-1,-1};

static int hsail_info_command_index(char* arg, struct ui_out *uiout)
{
  char index_buffer[256] = { 0 };
  int buffer_length = 0;
  int arg_length = 0;
  int ret_val = -1;

  gdb_assert(NULL != arg);
  gdb_assert(NULL != uiout);
  arg_length = strlen(arg);
  memset(index_buffer, '\0', sizeof(index_buffer));

  SKIP_LEADING_SPACES(arg,arg_length);

  while((buffer_length < arg_length) && isdigit(arg[buffer_length]) && buffer_length < 256)
  {
    index_buffer[buffer_length] = arg[buffer_length];
    buffer_length++;
  }

  /* validate the that index termiates in end of line of space, not alpha so it is valid (spaces)*/
  if (buffer_length >= arg_length || arg[buffer_length] == ' ' || arg[buffer_length] == '\t' || arg[buffer_length] == '\r' || arg[buffer_length] == '\n')
  {
    /* convert the buffer to value */
    index_buffer[buffer_length] = 0;
    ret_val = atoi(index_buffer);
  }

  /* if no conversion was made or fo some reason we got a negative value */
  if (ret_val < 0)
  {
    ui_out_text(uiout,"Invalid index provided for info command\n");
  }

  return ret_val;
}

#define HSAIL_INFO_HELP_STRING()\
"info hsail [kernels]: print all HSAIL kernel dispatches\n"\
"info hsail [kernel <kernel name>]: print all HSAIL kernel dispatches with a specific kernel name\n"\
"info hsail [work-groups | wgs]: print all HSAIL work-group items\n"\
"info hsail [work-group <flattened id> | wg <flattened id> | work-group <x,y,z> | wg <x,y,z>]: print a specific HSAIL work-group item\n"\
"info hsail [work-item | wi | work-items | wis]: print the focus HSAIL work-item\n"\
"info hsail [work-item <x,y,z> | wi <x,y,z>]: print a specific HSAIL work-item\n"\

static void hsail_info_param_print_help(void)
{
  struct ui_out *uiout = current_uiout;
  ui_out_text(uiout,"Invalid parameter\n");
  ui_out_text(uiout, HSAIL_INFO_HELP_STRING());
}

/*
 argName:is an input string that the function looks for and starts to dig out the dim3 entry after
 */
bool hsail_command_get_argument(const char* cmdInfo, const char* argName, unsigned int* itemDim3, int* numItems)
{
  int cmdLen = 0;
  int argLen = 0;
  int i = 0;
  char digitBuf[256]="";
  int currentDigit = 0;

  gdb_assert(NULL != cmdInfo);
  gdb_assert(NULL != argName);
  gdb_assert(NULL != itemDim3);
  gdb_assert(NULL != numItems);

  argLen = strlen(argName);
  *numItems = 0;

  /* check if the argName exists */
  SKIP_LEADING_SPACES(argName, argLen);

  /* Find the first argName sub string in cmdInfo */
  cmdInfo = strstr(cmdInfo, argName);
  if (NULL == cmdInfo)
  {
    return false;
  }

  cmdLen = strlen(cmdInfo);

  if (argLen != 0)
  {
    cmdInfo += argLen;
    cmdLen -= argLen;
    /* check for ':' */
    SKIP_LEADING_SPACES(cmdInfo, cmdLen);
    if (cmdInfo[0] != ':')
      return false;

    cmdInfo += 1;
    cmdLen -= 1;
  }

  /* read up to three digits separated by comma */
  for (i = 0 ; i < 3 && cmdLen > 0; i++)
  {
    SKIP_LEADING_SPACES(cmdInfo, cmdLen);
    currentDigit = 0;
    /* if starting with a digit check the digit comma format if not exit assuming next section might be
       part of the next needed parameters
    */
    if (isdigit(cmdInfo[0]))
    {
      while (isdigit(cmdInfo[0]) && cmdLen > 0)
      {
        digitBuf[currentDigit] = cmdInfo[0];
        cmdLen--;
        currentDigit++;
        cmdInfo++;
      }
      digitBuf[currentDigit] = '\0';
      itemDim3[i] = atoi(digitBuf);

      /* at the end check it is the end of the data or it ends with comma */
      SKIP_LEADING_SPACES(cmdInfo, cmdLen);

      /* we don't return a false here since there could be a valid command still left
         over in the arg buffer.
         For example the original input could be wg:1,2,3 wi:4,5,6.
         as part of "b hsail:46 if wg:1,2,3 wi:4,5,6"
         In this case, the cmdInfo buffer at this stage includes the "wi: 4,5,6"

         We probably need to improve this logic to detect if a valid command follows
         in the buffer */
      if ((cmdLen != 0) && cmdInfo[0] != ',')
        {
          return true;
        }

      cmdInfo++;
      cmdLen--;
    }
    /* count the actual number of items that were added to the itemDim */
    *numItems = i + 1;
  }

  return true;
}

static bool hsail_info_parameter_check(char* arg)
{
  /* We need to copy the input buffer since strtok changes arg*/
  char* arg_copy = NULL;
  int arg_len = 0;

  char* token = NULL;
  bool ret_code = false;
  const char s[2] = " ";

  gdb_assert(NULL != arg);

  arg_len = strlen(arg)+1;
  arg_copy = (char*)xmalloc(sizeof(char)*(arg_len));
  gdb_assert(NULL != arg_copy);

  if (arg_copy == NULL)
    {
      return ret_code;
    }
  memset(arg_copy,'\0',arg_len);
  strcpy(arg_copy, arg);

  /* we just need to get the first token */
  token = strtok(arg_copy, s);
  ret_code = false;

  if (token != NULL)
    {
      if (strcmp(token, "kernel") != 0 && strcmp(token, "kernels") != 0 &&
          strcmp(token, "wgs") != 0 && strcmp(token, "wg") != 0 &&
          strcmp(token, "work-group") != 0 && strcmp(token, "work-groups") != 0 &&
          strcmp(token, "wis") != 0  && strcmp(token, "wi") != 0 &&
          strcmp(token, "work-item") != 0  && strcmp(token, "work-items") != 0
          )
        {
          ret_code = false;
        }
      else
        {
          ret_code = true;
        }
    }

  xfree(arg_copy);
  return ret_code;
}

static void hsail_info_command(char *arg, int from_tty)
{
  struct ui_out *uiout = current_uiout;
  int index = 0;
  int numItems = 0;
  unsigned int workGroup[3] = {0,0,0};
  unsigned int workItem[3] = {0,0,0};
  bool foundParam = true;
  HsailWaveDim3 temp_work_item = {0,0,0};

  if (arg == NULL)
    {
      hsail_info_param_print_help();
      return ;
    }
  if (!hsail_info_parameter_check(arg))
    {
      hsail_info_param_print_help();
      return ;
    }

  if (strcmp(arg,"wavefronts") == 0)
  {
    /* todo: FB 11279 Disable for Alpha
    * hsail_print_wave_info (current_uiout, -1);
    * */
    hsail_info_param_print_help();
  }
  else if (strncmp(arg,"kernels", 7) == 0)
  {
    hsail_kernel_print_info (current_uiout, -1);
  }
  else if (strncmp(arg,"kernel ", 7) == 0)
  {
    arg += 7;
    hsail_kernel_print_specific_info(arg, current_uiout, -1);
  }
  else if (strncmp(arg,"work-groups", 11) == 0 || strncmp(arg,"wgs", 3) == 0)
  {
    hsail_print_workgroups_info (gs_active_work_group, current_uiout, -1);
  }
  else if (strncmp(arg,"work-group ", 11) == 0 || strncmp(arg,"wg ", 3) == 0)
  {
    if (strncmp(arg,"work-group ", 11) == 0)
    {
      arg += 11;
    }
    else
    {
      arg += 3;
    }
    foundParam = hsail_command_get_argument(arg, "", workGroup, &numItems);
    if (foundParam && (numItems == 1 || numItems == 3))
    {
      /* numItems = 1 assume it is flattened id */
      if (numItems == 1)
      {
        if (workGroup[0] >= 0)
        {
          hsail_print_specific_workgroup_by_id_info(workGroup[0], current_uiout, -1);
        }
      }
      else if (numItems == 3)
      {
          hsail_print_specific_workgroup_info(workGroup, current_uiout, -1);
      }
    }
    else
    {
      ui_out_text(uiout,"use 'info hsail work-group' with Flattened id or x,y,z identifier:\n");
      ui_out_text(uiout,"'info hsail work-group ID' or 'info hsail work-group x,y,z'\n");
    }

  }
  else if (strncmp(arg,"work-item", 9) == 0 || strncmp(arg,"wi", 2) == 0)
  {
    if (strncmp(arg,"work-item", 9) == 0)
    {
      arg += 9;
    }
    else
    {
      arg += 2;
    }

    /* If it is "work-item" or "wis", we need to skip the "s" so that it won't take the "s" as
       another input parameter*/
    if(strncmp(arg, "s", 1) == 0)
    {
      arg += 1;
    }

    foundParam = hsail_command_get_argument(arg, "", workItem, &numItems);
    /* if no param was entered then use active work item, else use work item x,y,z. Any other
       formats are not allowed and explain to the user the allowed formats */
    if (!foundParam || numItems == 0)
    {
      hsail_print_workitem_info (gs_active_work_group, gs_active_work_item, true, current_uiout, -1);
    }
    else if (numItems == 3)
    {
      foundParam = false;
      if (workItem[0] == gs_active_work_item.x && workItem[1] == gs_active_work_item.y && workItem[2] == gs_active_work_item.z)
      {
        foundParam = true;
      }
      temp_work_item.x = workItem[0];
      temp_work_item.y = workItem[1];
      temp_work_item.z = workItem[2];

      hsail_print_workitem_info (gs_active_work_group, temp_work_item, foundParam, current_uiout, -1);
    }
    else
    {
      ui_out_text(uiout,"'info hsail work-item' with no arguments will print info for current work-item\n");
      ui_out_text(uiout,"'info hsail work-item x,y,z'  will print info for work-item x,y,z\n");
    }
  }
  else
  {
    hsail_info_param_print_help();
  }
}


static bool hsail_thread_command_validate_active(const unsigned int* workGroup, const unsigned int* workItem)
{
  /* get the waves info */
  int num_waves = hsail_tdep_get_active_wave_count();
  HsailAgentWaveInfo* wave_info_buffer = (HsailAgentWaveInfo*)hsail_tdep_map_wave_buffer();
  int nWave = 0;
  int nExec = 0;
  uint64_t current_bit = 0;
  bool found = false;

  /* A NULL is possible if no dispatch is active*/
  if (NULL == wave_info_buffer)
    {
      return found;
    }

  gdb_assert(NULL != workGroup);
  gdb_assert(NULL != workItem);


  for (nWave = 0 ; nWave < num_waves ; nWave++)
  {
      /* check the work-group first */
      if (workGroup[0] == wave_info_buffer[nWave].workGroupId.x &&
          workGroup[1] == wave_info_buffer[nWave].workGroupId.y &&
          workGroup[2] == wave_info_buffer[nWave].workGroupId.z)
      {
        /* check all exec flags for the work item */
        current_bit = 1;
        for (nExec = 0 ; nExec < 64 ; nExec++)
        {
          if (wave_info_buffer[nWave].execMask & current_bit)
          {
            if (workItem[0] == wave_info_buffer[nWave].workItemId[nExec].x &&
                workItem[1] == wave_info_buffer[nWave].workItemId[nExec].y &&
                workItem[2] == wave_info_buffer[nWave].workItemId[nExec].z)
            {
              found = true;
            }
            current_bit = current_bit<<1;
          }
        }
      }
  }

  /* release the wave buffer */
  hsail_tdep_unmap_wave_buffer((void*)wave_info_buffer);

  return found;
}

static void hsail_command(char *arg, int from_tty)
{
  struct ui_out *uiout = current_uiout;
  unsigned int workGroup[3] = {0,0,0};
  unsigned int workItem[3] = {0,0,0};
  int l = 0;
  int dummynumItems = 0;
  bool foundParam = true;
  if (arg == NULL)
  {
    ui_out_text(uiout,"hsail command needs a parameter \n");
    ui_out_text(uiout,"hsail thread wg:x,y,z wi:x,y,z\n");
    return ;
  }

  /* Get the size: */
  l = strlen(arg);

  /* Verify the prefix: */
  if (l > 6)
  {
    if ('t' != arg[0] ||  'h' != arg[1] || 'r' != arg[2] || 'e' != arg[3] || 'a' != arg[4] || 'd' != arg[5])
        foundParam = false;
  }

  /* check for the wg section */
  if (foundParam)
  {
    arg += 6;
    foundParam = hsail_command_get_argument(arg, "wg", workGroup, &dummynumItems);
  }

  if (foundParam)
  {
    foundParam = hsail_command_get_argument(arg, "wi", workItem, &dummynumItems);
  }
  /*
   *  printf("IP: work-group (%d,%d,%d) and work-item (%d,%d,%d)]\n",
   *  workGroup[0],workGroup[1],workGroup[2],
   *  workItem[0],workItem[1],workItem[2]);
   */

  if (!foundParam)
  {
    ui_out_text(uiout,"Unsupported parameter\n");
    ui_out_text(uiout,"hsail thread wg:x,y,z wi:x,y,z\n");
  }
  else
  {
    /* validate that the wg and wi are active */
    if (hsail_thread_command_validate_active(workGroup, workItem))
    {
      char funcExp[256] = "";
      struct expression *expr = NULL;

      sprintf(funcExp, "SetHsailThreadCmdInfo(%d,%d,%d,%d,%d,%d)",workGroup[0],workGroup[1],workGroup[2],workItem[0],workItem[1],workItem[2]);

      /*
       * This message will be printed in the Agent
      printf("[hsail-gdb: Switching to work-group (%d,%d,%d) and work-item (%d,%d,%d)]\n",workGroup[0],workGroup[1],workGroup[2],workItem[0],workItem[1],workItem[2]);
      */
      /* Create the expression */
      expr = parse_expression (funcExp);

      /* Call the evaluator */
      evaluate_expression (expr);

      gs_active_work_group.x = workGroup[0];
      gs_active_work_group.y = workGroup[1];
      gs_active_work_group.z = workGroup[2];
      gs_active_work_item.x = workItem[0];
      gs_active_work_item.y = workItem[1];
      gs_active_work_item.z = workItem[2];
    }
    else
    {
      ui_out_text(uiout,"work-group and work-item provided not active\n");
    }
  }
}

static void hsail_set_config_command (char *args, int from_tty, struct cmd_list_element *c)
{
  /* Note that args is NULL always, the real args that we care for
   * this command are in g_hsail_argument_buffer
   * */
  char* pch = NULL;
  char* temp_hsail_argument_buff = NULL;
  temp_hsail_argument_buff = (char*)xmalloc(gs_hsail_argument_buff_len*sizeof(char));
  gdb_assert(temp_hsail_argument_buff != NULL);

  memset(temp_hsail_argument_buff, '\0', gs_hsail_argument_buff_len);

  /*g_hsail_argument_buff is populated by the caller of this function*/
  strcpy(temp_hsail_argument_buff, g_hsail_argument_buff);

  /*We only support doing this once hsail is initialized*/
  gdb_assert(is_hsail_linux_initialized() == 1);

  pch = strtok (temp_hsail_argument_buff, " ");

  if (pch != NULL)
    {
      /*set hsail logging*/
      if (strcmp(pch,"logging") == 0)
        {
          pch = strtok(NULL, " ");
          if (pch!= NULL)
            {
              if(strcmp(pch,"on") == 0)
                {
                  printf_filtered("Enable logging in the HSA Agent and the DBE\n");
                  hsail_enqueue_set_logging(HSAIL_LOGGING_ENABLE_ALL);
                }
              else if(strcmp(pch,"off") == 0)
                {
                  printf_filtered("Disable logging in the HSA Agent and the DBE\n");
                  hsail_enqueue_set_logging(HSAIL_LOGGING_DISABLE_ALL);
                }
              else
                {
                  printf_filtered("HSAIL Logging options\n");
                  printf_filtered("set hsail logging [on|off] \n");
                }
            }
        }
    }

  xfree(temp_hsail_argument_buff);
}

static void hsail_show_config_command (struct ui_file *file, int from_tty,
                                       struct cmd_list_element *c, const char *value)
{
  gdb_assert(NULL != value);
  gdb_assert(NULL != g_hsail_argument_buff);
  printf("HSAIL show command \t Value is %s \n",value);
  printf("Logging param is %s\n",g_hsail_argument_buff);
}


void hsail_command_clear_argument_buff(void)
{
  if(g_hsail_argument_buff != NULL)
    {
      xfree(g_hsail_argument_buff);
      g_hsail_argument_buff = NULL;
    }
}

/* Clear the focus wave and work item at the end of the dispatch */
void hsail_cmd_clear_focus(void)
{
  hsail_utils_copy_wavedim3(&gs_active_work_group, &gs_unknown_wave_dim);
  hsail_utils_copy_wavedim3(&gs_active_work_item, &gs_unknown_wave_dim);
}

void hsail_cmd_set_focus(HsailWaveDim3 focusWg, HsailWaveDim3 focusWi)
{
  /* To work with the available validate function, we need to
   * move the input parameters into an array. We don't want to
   * do C-casting which may cause silent failure (such as
   * HsailWaveDim3 internal data type change to long int).*/
  unsigned int wg_buff[3] = {0, 0, 0};
  unsigned int wi_buff[3] = {0, 0, 0};

  struct ui_out *uiout = current_uiout;

  wg_buff[0] = focusWg.x;
  wg_buff[1] = focusWg.y;
  wg_buff[2] = focusWg.z;

  wi_buff[0] = focusWi.x;
  wi_buff[1] = focusWi.y;
  wi_buff[2] = focusWi.z;

  /* If unknown, we cant validate it yet
   * */
  if (hsail_utils_compare_wavedim3(&gs_active_work_group, &gs_unknown_wave_dim) ||
      hsail_utils_compare_wavedim3(&gs_active_work_item, &gs_unknown_wave_dim))
    {
      hsail_utils_copy_wavedim3(&gs_active_work_group, &focusWg);
      hsail_utils_copy_wavedim3(&gs_active_work_item, &focusWi);
    }
  else if (hsail_thread_command_validate_active(wg_buff, wi_buff))
    {
      hsail_utils_copy_wavedim3(&gs_active_work_group, &focusWg);
      hsail_utils_copy_wavedim3(&gs_active_work_item, &focusWi);

    }

  /* We cant print an error message in the else case since in the initial case when the
   * dispatch starts and sends the focus info, the waveinfo buffer is not yet populated.
   *
   * We could special case the start dispatch case, but that would just be weird.
   * */
}

/* Command for printing hsail source within gdb's terminal
 * */
void hsail_cmd_list_command(char* arg, int from_tty)
{
  printf_filtered("HSAIL list command is not presently supported when in HSAIL dispatch\n");
}

void
_initialize_hsailcmd (void)
{
  const char *hsail_set_name = "hsail";
  struct cmd_list_element *c = NULL;

  /* info hsail
   *
   * When help info is printed only the "Display HSAIL related information" is printed.
   * When you print help info hsail, the full string is printed.
   * */
  add_info ("hsail", hsail_info_command,
            _("Display information about HSAIL dispatches .\n"HSAIL_INFO_HELP_STRING()));

  /* hsail thread .... */
  add_com ("hsail", class_stack, hsail_command, _("\
  Switch focus for hsail variable printing.\n"));
  add_com_alias ("hl", "hsail", class_stack, 1);


#if 0
  /* I am leaving this here to show another way of how boolean parameters can be passed*/
  int hsail_logging_param = 0; /*this declaration would need to be global*/
  /* set hsailbool on   -> result will be stored in hsail_logging_param
   * set hsailbool off  -> result will be stored in hsail_logging_param
   * */
  add_setshow_boolean_cmd("hsailbool",
                          class_run,
                           &hsail_logging_param,
                           _("Set HSAIL configuration options."),
                           _("Show  HSAIL configuration options."),
                           _("Possible HSAIL configuration options."),
                           hsail_set_config_command,
                           hsail_show_config_command,
                           &setlist, &showlist);
#endif

  /* set hsail logging */
  /* Buffer freed in final hsail cleanup on gdb exit
   * */
  g_hsail_argument_buff = (char*)malloc(gs_hsail_argument_buff_len*sizeof(char));
  gdb_assert(g_hsail_argument_buff != NULL);

  memset(g_hsail_argument_buff, '\0', gs_hsail_argument_buff_len);

  add_setshow_string_noescape_cmd("hsail",
                                  class_run,
                                   &g_hsail_argument_buff,
                                   _("Set HSAIL configuration options."),
                                   _("Show  HSAIL configuration options."),
                                   _("Possible HSAIL configuration options."),
                                   hsail_set_config_command,
                                   hsail_show_config_command,
                                   &setlist, &showlist);

  c = lookup_cmd (&hsail_set_name, setlist, "", -1, 1);
  gdb_assert (c != NULL);

}
