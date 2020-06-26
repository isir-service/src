/*
 * Virtual terminal [aka TeletYpe] interface routine.
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */
#include "buffer.h"
#include "command.h"
#include "sockunion.h"
#include "thread.h"
#include <arpa/telnet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include<netdb.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>

#define MAXPATHLEN 1204
/* Vty events */
enum event 
  {
    VTY_SERV,
    VTY_READ,
    VTY_WRITE,
    VTY_TIMEOUT_RESET,
  };


int vty_timeout_val;
struct thread_master *vty_master = NULL;
struct _host host;

static void vty_event (enum event, int, struct vty *);

/* Extern host structure from command.c */
/* Vector which store each vty structure. */




/* VTY standard output function. */
int
vty_out (struct vty *vty, const char *format, ...)
{
	if (!vty)
		return -1;

	va_list args;
	int len = 0;
	int size = sizeof(vty->format_buf);

	va_start (args, format);
	if (vty->type == VTY_SHELL)
		vprintf (format, args);
	else {

		len = vsnprintf (vty->format_buf, sizeof (vty->format_buf), format, args);

		/* Initial buffer is not enough.  */
		if (len < 0 || len >= size)
			return -1;
		/* Pointer p must point out buffer. */
		buffer_write (vty->obuf, (u_char *) vty->format_buf, len);
	}
	va_end (args);
	return len;
}

#define TIME_BUF 27

/* Put out prompt and wait input from user. */
static void
vty_prompt (struct vty *vty)
{
  struct utsname names;
  const char*hostname;
  if (vty->type == VTY_TERM)
    {
      hostname = host.name;
      if (!hostname)
	{
	  uname (&names);
	  hostname = names.nodename;
	}
      vty_out (vty, cmd_prompt (vty->node),hostname);
    }
}

/* Send WILL TELOPT_ECHO to remote server. */
void
vty_will_echo (struct vty *vty)
{
  char cmd[] = { IAC, WILL, TELOPT_ECHO, '\0' };
  vty_out (vty, "%s", cmd);
}

/* Make suppress Go-Ahead telnet option. */
static void
vty_will_suppress_go_ahead (struct vty *vty)
{
  char cmd[] = { IAC, WILL, TELOPT_SGA, '\0' };
  vty_out (vty, "%s", cmd);
}

/* Make don't use linemode over telnet. */
static void
vty_dont_linemode (struct vty *vty)
{
  char cmd[] = { IAC, DONT, TELOPT_LINEMODE, '\0' };
  vty_out (vty, "%s", cmd);
}

/* Use window size. */
static void
vty_do_window_size (struct vty *vty)
{
  char cmd[] = { IAC, DO, TELOPT_NAWS, '\0' };
  vty_out (vty, "%s", cmd);
}


/* Allocate new vty struct. */
struct vty *
vty_new ()
{
  struct vty *new = calloc (1,sizeof (struct vty));

  new->obuf = (struct buffer *) buffer_new (100);
  new->buf = calloc (1,VTY_BUFSIZ);
  new->max = VTY_BUFSIZ;
  new->sb_buffer = NULL;

  return new;
}


/* Command execution over the vty interface. */
int
vty_command (struct vty *vty, char *buf)
{
  int ret;
  vector vline;

  /* Split readline string up into the vector */
  vline = cmd_make_strvec (buf);

  if (vline == NULL)
    return CMD_SUCCESS;

  ret = cmd_execute_command (vline, vty, NULL);

  if (ret != CMD_SUCCESS)
    switch (ret)
      {
      case CMD_WARNING:
	if (vty->type == VTY_FILE)
	  vty_out (vty, "Warning...%s", VTY_NEWLINE);
	break;
      case CMD_ERR_AMBIGUOUS:
	vty_out (vty, "%% Ambiguous command.%s", VTY_NEWLINE);
	break;
      case CMD_ERR_NO_MATCH:
	vty_out (vty, "%% Unknown command.%s", VTY_NEWLINE);
	break;
      case CMD_ERR_INCOMPLETE:
	vty_out (vty, "%% Command incomplete.%s", VTY_NEWLINE);
	break;
      }
  cmd_free_strvec (vline);

  return ret;
}

char telnet_backward_char = 0x08;
char telnet_space_char = ' ';

/* Basic function to write buffer to vty. */
static void
vty_write (struct vty *vty, char *buf, size_t nbytes)
{
  /* Should we do buffering here ?  And make vty_flush (vty) ? */
  buffer_write (vty->obuf, (u_char *)buf, nbytes);
}

/* Ensure length of input buffer.  Is buffer is short, double it. */
static void
vty_ensure (struct vty *vty, int length)
{
  if (vty->max <= length)
    {
      vty->max *= 2;
      vty->buf = realloc (vty->buf, vty->max);
    }
}

/* Basic function to insert character into vty. */
static void
vty_self_insert (struct vty *vty, char c)
{
  int i;
  int length;

  vty_ensure (vty, vty->length + 1);
  length = vty->length - vty->cp;
  memmove (&vty->buf[vty->cp + 1], &vty->buf[vty->cp], length);
  vty->buf[vty->cp] = c;

  vty_write (vty, &vty->buf[vty->cp], length + 1);
  for (i = 0; i < length; i++)
    vty_write (vty, &telnet_backward_char, 1);

  vty->cp++;
  vty->length++;
}

/* Self insert character 'c' in overwrite mode. */
static void
vty_self_insert_overwrite (struct vty *vty, char c)
{
  vty_ensure (vty, vty->length + 1);
  vty->buf[vty->cp++] = c;

  if (vty->cp > vty->length)
    vty->length++;

  vty_write (vty, &c, 1);
}

/* Insert a word into vty interface with overwrite mode. */
static void
vty_insert_word_overwrite (struct vty *vty, char *str)
{
  int len = strlen (str);
  vty_write (vty, str, len);
  strcpy (&vty->buf[vty->cp], str);
  vty->cp += len;
  vty->length = vty->cp;
}

/* Forward character. */
static void
vty_forward_char (struct vty *vty)
{
  if (vty->cp < vty->length)
    {
      vty_write (vty, &vty->buf[vty->cp], 1);
      vty->cp++;
    }
}

/* Backward character. */
static void
vty_backward_char (struct vty *vty)
{
  if (vty->cp > 0)
    {
      vty->cp--;
      vty_write (vty, &telnet_backward_char, 1);
    }
}

/* Move to the beginning of the line. */
static void
vty_beginning_of_line (struct vty *vty)
{
  while (vty->cp)
    vty_backward_char (vty);
}

/* Move to the end of the line. */
static void
vty_end_of_line (struct vty *vty)
{
  while (vty->cp < vty->length)
    vty_forward_char (vty);
}

static void vty_kill_line_from_beginning (struct vty *);
static void vty_redraw_line (struct vty *);

/* Print command line history.  This function is called from
   vty_next_line and vty_previous_line. */
static void
vty_history_print (struct vty *vty)
{
  int length;

  vty_kill_line_from_beginning (vty);

  /* Get previous line from history buffer */
  length = strlen (vty->hist[vty->hp]);
  memcpy (vty->buf, vty->hist[vty->hp], length);
  vty->cp = vty->length = length;

  /* Redraw current line */
  vty_redraw_line (vty);
}

/* Show next command line history. */
void
vty_next_line (struct vty *vty)
{
  int try_index;

  if (vty->hp == vty->hindex)
    return;

  /* Try is there history exist or not. */
  try_index = vty->hp;
  if (try_index == (VTY_MAXHIST - 1))
    try_index = 0;
  else
    try_index++;

  /* If there is not history return. */
  if (vty->hist[try_index] == NULL)
    return;
  else
    vty->hp = try_index;

  vty_history_print (vty);
}

/* Show previous command line history. */
void
vty_previous_line (struct vty *vty)
{
  int try_index;

  try_index = vty->hp;
  if (try_index == 0)
    try_index = VTY_MAXHIST - 1;
  else
    try_index--;

  if (vty->hist[try_index] == NULL)
    return;
  else
    vty->hp = try_index;

  vty_history_print (vty);
}

/* This function redraw all of the command line character. */
static void
vty_redraw_line (struct vty *vty)
{
  vty_write (vty, vty->buf, vty->length);
  vty->cp = vty->length;
}

/* Forward word. */
static void
vty_forward_word (struct vty *vty)
{
  while (vty->cp != vty->length && vty->buf[vty->cp] != ' ')
    vty_forward_char (vty);
  
  while (vty->cp != vty->length && vty->buf[vty->cp] == ' ')
    vty_forward_char (vty);
}

/* Backward word without skipping training space. */
static void
vty_backward_pure_word (struct vty *vty)
{
  while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
    vty_backward_char (vty);
}

/* Backward word. */
static void
vty_backward_word (struct vty *vty)
{
  while (vty->cp > 0 && vty->buf[vty->cp - 1] == ' ')
    vty_backward_char (vty);

  while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
    vty_backward_char (vty);
}

/* When '^D' is typed at the beginning of the line we move to the down
   level. */
static void
vty_down_level (struct vty *vty)
{
  vty_out (vty, "%s", VTY_NEWLINE);
  vty_prompt (vty);
  vty->cp = 0;
}

/* When '^Z' is received from vty, move down to the enable mode. */
void
vty_end_config (struct vty *vty)
{
  vty_out (vty, "%s", VTY_NEWLINE);

  switch (vty->node)
    {
    case ENABLE_NODE:
	case VIEW_NODE:
      /* Nothing to do. */
      break;
    case VTY_NODE:
      vty->node = ENABLE_NODE;
      break;
    default:
      /* Unknown node, we have to ignore it. */
      break;
    }

  vty_prompt (vty);
  vty->cp = 0;
}

/* Delete a charcter at the current point. */
static void
vty_delete_char (struct vty *vty)
{
  int i;
  int size;

  if (vty->length == 0)
    {
      vty_down_level (vty);
      return;
    }

  if (vty->cp == vty->length)
    return;			/* completion need here? */

  size = vty->length - vty->cp;

  vty->length--;
  memmove (&vty->buf[vty->cp], &vty->buf[vty->cp + 1], size - 1);
  vty->buf[vty->length] = '\0';

  vty_write (vty, &vty->buf[vty->cp], size - 1);
  vty_write (vty, &telnet_space_char, 1);

  for (i = 0; i < size; i++)
    vty_write (vty, &telnet_backward_char, 1);
}

/* Delete a character before the point. */
static void
vty_delete_backward_char (struct vty *vty)
{
  if (vty->cp == 0)
    return;

  vty_backward_char (vty);
  vty_delete_char (vty);
}

/* Kill rest of line from current point. */
static void
vty_kill_line (struct vty *vty)
{
  int i;
  int size;

  size = vty->length - vty->cp;
  
  if (size == 0)
    return;

  for (i = 0; i < size; i++)
    vty_write (vty, &telnet_space_char, 1);
  for (i = 0; i < size; i++)
    vty_write (vty, &telnet_backward_char, 1);

  memset (&vty->buf[vty->cp], 0, size);
  vty->length = vty->cp;
}

/* Kill line from the beginning. */
static void
vty_kill_line_from_beginning (struct vty *vty)
{
  vty_beginning_of_line (vty);
  vty_kill_line (vty);
}

/* Delete a word before the point. */
static void
vty_forward_kill_word (struct vty *vty)
{
  while (vty->cp != vty->length && vty->buf[vty->cp] == ' ')
    vty_delete_char (vty);
  while (vty->cp != vty->length && vty->buf[vty->cp] != ' ')
    vty_delete_char (vty);
}

/* Delete a word before the point. */
static void
vty_backward_kill_word (struct vty *vty)
{
  while (vty->cp > 0 && vty->buf[vty->cp - 1] == ' ')
    vty_delete_backward_char (vty);
  while (vty->cp > 0 && vty->buf[vty->cp - 1] != ' ')
    vty_delete_backward_char (vty);
}

/* Transpose chars before or at the point. */
static void
vty_transpose_chars (struct vty *vty)
{
  char c1, c2;

  /* If length is short or point is near by the beginning of line then
     return. */
  if (vty->length < 2 || vty->cp < 1)
    return;

  /* In case of point is located at the end of the line. */
  if (vty->cp == vty->length)
    {
      c1 = vty->buf[vty->cp - 1];
      c2 = vty->buf[vty->cp - 2];

      vty_backward_char (vty);
      vty_backward_char (vty);
      vty_self_insert_overwrite (vty, c1);
      vty_self_insert_overwrite (vty, c2);
    }
  else
    {
      c1 = vty->buf[vty->cp];
      c2 = vty->buf[vty->cp - 1];

      vty_backward_char (vty);
      vty_self_insert_overwrite (vty, c1);
      vty_self_insert_overwrite (vty, c2);
    }
}

/* Do completion at vty interface. */
static void
vty_complete_command (struct vty *vty)
{
  int i;
  int ret;
  char **matched = NULL;
  vector vline;

  vline = cmd_make_strvec (vty->buf);
  if (vline == NULL)
    return;

  /* In case of 'help \t'. */
  if (isspace ((int) vty->buf[vty->length - 1]))
    vector_set (vline, '\0');

  matched = cmd_complete_command (vline, vty, &ret);
  
  cmd_free_strvec (vline);

  vty_out (vty, "%s", VTY_NEWLINE);
  switch (ret)
    {
    case CMD_ERR_AMBIGUOUS:
      vty_out (vty, "%% Ambiguous command.%s", VTY_NEWLINE);
      vty_prompt (vty);
      vty_redraw_line (vty);
      break;
    case CMD_ERR_NO_MATCH:
       vty_out (vty, "%% There is no matched command.%s", VTY_NEWLINE); 
      vty_prompt (vty);
      vty_redraw_line (vty);
      break;
    case CMD_COMPLETE_FULL_MATCH:
      vty_prompt (vty);
      vty_redraw_line (vty);
      vty_backward_pure_word (vty);
      vty_insert_word_overwrite (vty, matched[0]);
      vty_self_insert (vty, ' ');
      free (matched[0]);
      break;
    case CMD_COMPLETE_MATCH:
      vty_prompt (vty);
      vty_redraw_line (vty);
      vty_backward_pure_word (vty);
      vty_insert_word_overwrite (vty, matched[0]);
      free (matched[0]);
      vector_only_index_free (matched);
      return;
      break;
    case CMD_COMPLETE_LIST_MATCH:
      for (i = 0; matched[i] != NULL; i++)
	{
	  if (i != 0 && ((i % 6) == 0))
	    vty_out (vty, "%s", VTY_NEWLINE);
	  vty_out (vty, "%-10s ", matched[i]);
	  free (matched[i]);
	}
      vty_out (vty, "%s", VTY_NEWLINE);

      vty_prompt (vty);
      vty_redraw_line (vty);
      break;
    case CMD_ERR_NOTHING_TODO:
      vty_prompt (vty);
      vty_redraw_line (vty);
      break;
    default:
      break;
    }
  if (matched)
    vector_only_index_free (matched);
}

void
vty_describe_fold (struct vty *vty, int cmd_width,
		   int desc_width, struct desc *desc)
{
  char *buf, *cmd, *p;
  int pos;

  cmd = desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd;

  if (desc_width <= 0)
    {
      vty_out (vty, "  %-*s  %s%s", cmd_width, cmd, desc->str, VTY_NEWLINE);
      return;
    }

  buf = calloc (1,strlen (desc->str) + 1);

  for (p = desc->str; (int)strlen (p) > desc_width; p += pos + 1)
    {
      for (pos = desc_width; pos > 0; pos--)
	if (*(p + pos) == ' ')
	  break;

      if (pos == 0)
	break;

      strncpy (buf, p, pos);
      buf[pos] = '\0';
      vty_out (vty, "  %-*s  %s%s", cmd_width, cmd, buf, VTY_NEWLINE);

      cmd = "";
    }

  vty_out (vty, "  %-*s  %s%s", cmd_width, cmd, p, VTY_NEWLINE);

  free (buf);
}

/* Describe matched command function. */
static void
vty_describe_command (struct vty *vty)
{
  int ret;
  vector vline;
  vector describe;
  unsigned i, width, desc_width;
  struct desc *desc, *desc_cr = NULL;

  vline = cmd_make_strvec (vty->buf);

  /* In case of '> ?'. */
  if (vline == NULL)
    {
      vline = vector_init (1);
      vector_set (vline, '\0');
    }
  else 
    if (isspace ((int) vty->buf[vty->length - 1]))
      vector_set (vline, '\0');

  describe = cmd_describe_command (vline, vty, &ret);

  vty_out (vty, "%s", VTY_NEWLINE);

  /* Ambiguous error. */
  switch (ret)
    {
    case CMD_ERR_AMBIGUOUS:
      cmd_free_strvec (vline);
      vty_out (vty, "%% Ambiguous command.%s", VTY_NEWLINE);
      vty_prompt (vty);
      vty_redraw_line (vty);
      return;
      break;
    case CMD_ERR_NO_MATCH:
      cmd_free_strvec (vline);
      vty_out (vty, "%% There is no matched command.%s", VTY_NEWLINE);
      vty_prompt (vty);
      vty_redraw_line (vty);
      return;
      break;
    }  

  /* Get width of command string. */
  width = 0;
  for (i = 0; i < vector_max (describe); i++)
    if ((desc = vector_slot (describe, i)) != NULL)
      {
	unsigned int len;

	if (desc->cmd[0] == '\0')
	  continue;

	len = strlen (desc->cmd);
	if (desc->cmd[0] == '.')
	  len--;

	if (width < len)
	  width = len;
      }

  /* Get width of description string. */
  desc_width = vty->width - (width + 6);

  /* Print out description. */
  for (i = 0; i < vector_max (describe); i++)
    if ((desc = vector_slot (describe, i)) != NULL)
      {
	if (desc->cmd[0] == '\0')
	  continue;
	
	if (strcmp (desc->cmd, "<cr>") == 0)
	  {
	    desc_cr = desc;
	    continue;
	  }

	if (!desc->str)
	  vty_out (vty, "  %-s%s",
		   desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd,
		   VTY_NEWLINE);
	else if (desc_width >= strlen (desc->str))
	  vty_out (vty, "  %-*s  %s%s", width,
		   desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd,
		   desc->str, VTY_NEWLINE);
	else
	  vty_describe_fold (vty, width, desc_width, desc);

      }

  if ((desc = desc_cr))
    {
      if (!desc->str)
	vty_out (vty, "  %-s%s",
		 desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd,
		 VTY_NEWLINE);
      else if (desc_width >= strlen (desc->str))
	vty_out (vty, "  %-*s  %s%s", width,
		 desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd,
		 desc->str, VTY_NEWLINE);
      else
	vty_describe_fold (vty, width, desc_width, desc);
    }

  cmd_free_strvec (vline);
  vector_free (describe);

  vty_prompt (vty);
  vty_redraw_line (vty);
}

void
vty_clear_buf (struct vty *vty)
{
  memset (vty->buf, 0, vty->max);
}

/* ^C stop current input and do not add command line to the history. */
static void
vty_stop_input (struct vty *vty)
{
  vty->cp = vty->length = 0;
  vty_clear_buf (vty);
  vty_out (vty, "%s", VTY_NEWLINE);

  switch (vty->node)
    {
    case ENABLE_NODE:
      /* Nothing to do. */
      break;
    case VTY_NODE:
      vty->node = ENABLE_NODE;
      break;
    default:
      /* Unknown node, we have to ignore it. */
      break;
    }
  vty_prompt (vty);

  /* Set history pointer to the latest one. */
  vty->hp = vty->hindex;
}

/* Add current command line to the history buffer. */
static void
vty_hist_add (struct vty *vty)
{
  int index;

  if (vty->length == 0)
    return;

  index = vty->hindex ? vty->hindex - 1 : VTY_MAXHIST - 1;

  /* Ignore the same string as previous one. */
  if (vty->hist[index])
    if (strcmp (vty->buf, vty->hist[index]) == 0)
      {
	vty->hp = vty->hindex;
	return;
      }

  /* Insert history entry. */
  if (vty->hist[vty->hindex])
    free ( vty->hist[vty->hindex]);
  vty->hist[vty->hindex] = strdup (vty->buf);

  /* History index rotation. */
  vty->hindex++;
  if (vty->hindex == VTY_MAXHIST)
    vty->hindex = 0;

  vty->hp = vty->hindex;
}

/* #define TELNET_OPTION_DEBUG */

/* Get telnet window size. */
static int
vty_telnet_option (struct vty *vty, unsigned char *buf, int nbytes)
{
(void)nbytes;
  switch (buf[0])
    {
    case SB:
      buffer_reset(vty->sb_buffer);
      vty->iac_sb_in_progress = 1;
      return 0;
      break;
    case SE: 
      {
	char *buffer;
	int length;

	if (!vty->iac_sb_in_progress)
	  return 0;

	buffer = (char *)vty->sb_buffer->head->data;
	length = vty->sb_buffer->length;

	if (buffer == NULL)
	  return 0;

	if (buffer[0] == '\0')
	  {
	    vty->iac_sb_in_progress = 0;
	    return 0;
	  }
	switch (buffer[0])
	  {
	  case TELOPT_NAWS:
	    if (length < 5)
	      break;
	    vty->width = buffer[2];
	    vty->height = vty->lines >= 0 ? vty->lines : buffer[4];
	    break;
	  }
	vty->iac_sb_in_progress = 0;
	return 0;
	break;
      }
    default:
      break;
    }
  return 1;
}

/* Execute current command line. */
static int
vty_execute (struct vty *vty)
{
  int ret;

  ret = CMD_SUCCESS;

  switch (vty->node)
    {
    default:
      ret = vty_command (vty, vty->buf);
      if (vty->type == VTY_TERM)
	vty_hist_add (vty);
      break;
    }

  /* Clear command line buffer. */
  vty->cp = vty->length = 0;
  vty_clear_buf (vty);

  if (vty->status != VTY_CLOSE 
      && vty->status != VTY_START
      && vty->status != VTY_CONTINUE)
    vty_prompt (vty);

  return ret;
}

#define CONTROL(X)  ((X) - '@')
#define VTY_NORMAL     0
#define VTY_PRE_ESCAPE 1
#define VTY_ESCAPE     2

/* Escape character command map. */
static void
vty_escape_map (unsigned char c, struct vty *vty)
{
  switch (c)
    {
    case ('A'):
      vty_previous_line (vty);
      break;
    case ('B'):
      vty_next_line (vty);
      break;
    case ('C'):
      vty_forward_char (vty);
      break;
    case ('D'):
      vty_backward_char (vty);
      break;
    default:
      break;
    }

  /* Go back to normal mode. */
  vty->escape = VTY_NORMAL;
}

/* Quit print out to the buffer. */
static void
vty_buffer_reset (struct vty *vty)
{
  buffer_reset (vty->obuf);
  vty_prompt (vty);
  vty_redraw_line (vty);
}

/* Read data via vty socket. */
static int
vty_read (struct thread *thread)
{
  int i;
  int ret;
  int nbytes;
  unsigned char buf[VTY_READ_BUFSIZ];

  int vty_sock = THREAD_FD (thread);
  struct vty *vty = THREAD_ARG (thread);
  vty->t_read = NULL;

  /* Read raw data from socket */
  nbytes = read (vty->fd, buf, VTY_READ_BUFSIZ);
  if (nbytes <= 0)
    vty->status = VTY_CLOSE;

  for (i = 0; i < nbytes; i++) 
    {
      if (buf[i] == IAC)
	{
	  if (!vty->iac)
	    {
	      vty->iac = 1;
	      continue;
	    }
	  else
	    {
	      vty->iac = 0;
	    }
	}
      
      if (vty->iac_sb_in_progress && !vty->iac)
	{
	  buffer_putc(vty->sb_buffer, buf[i]);
	  continue;
	}

      if (vty->iac)
	{
	  /* In case of telnet command */
	  ret = vty_telnet_option (vty, buf + i, nbytes - i);
	  vty->iac = 0;
	  i += ret;
	  continue;
	}

      if (vty->status == VTY_MORE)
	{
	  switch (buf[i])
	    {
	    case CONTROL('C'):
	    case 'q':
	    case 'Q':
	      if (vty->output_func)
		(*vty->output_func) (vty, 1);
	      vty_buffer_reset (vty);
	      break;
	    case '\n':
	    case '\r':
	      vty->status = VTY_MORELINE;
	      if (vty->output_func)
		(*vty->output_func) (vty, 0);
	      break;
	    default:
	      if (vty->output_func)
		(*vty->output_func) (vty, 0);
	      break;
	    }
	  continue;
	}

      /* Escape character. */
      if (vty->escape == VTY_ESCAPE)
	{
	  vty_escape_map (buf[i], vty);
	  continue;
	}

      /* Pre-escape status. */
      if (vty->escape == VTY_PRE_ESCAPE)
	{
	  switch (buf[i])
	    {
	    case '[':
	      vty->escape = VTY_ESCAPE;
	      break;
	    case 'b':
	      vty_backward_word (vty);
	      vty->escape = VTY_NORMAL;
	      break;
	    case 'f':
	      vty_forward_word (vty);
	      vty->escape = VTY_NORMAL;
	      break;
	    case 'd':
	      vty_forward_kill_word (vty);
	      vty->escape = VTY_NORMAL;
	      break;
	    case CONTROL('H'):
	    case 0x7f:
	      vty_backward_kill_word (vty);
	      vty->escape = VTY_NORMAL;
	      break;
	    default:
	      vty->escape = VTY_NORMAL;
	      break;
	    }
	  continue;
	}

      switch (buf[i])
	{
	case CONTROL('A'):
	  vty_beginning_of_line (vty);
	  break;
	case CONTROL('B'):
	  vty_backward_char (vty);
	  break;
	case CONTROL('C'):
	  vty_stop_input (vty);
	  break;
	case CONTROL('D'):
	  vty_delete_char (vty);
	  break;
	case CONTROL('E'):
	  vty_end_of_line (vty);
	  break;
	case CONTROL('F'):
	  vty_forward_char (vty);
	  break;
	case CONTROL('H'):
	case 0x7f:
	  vty_delete_backward_char (vty);
	  break;
	case CONTROL('K'):
	  vty_kill_line (vty);
	  break;
	case CONTROL('N'):
	  vty_next_line (vty);
	  break;
	case CONTROL('P'):
	  vty_previous_line (vty);
	  break;
	case CONTROL('T'):
	  vty_transpose_chars (vty);
	  break;
	case CONTROL('U'):
	  vty_kill_line_from_beginning (vty);
	  break;
	case CONTROL('W'):
	  vty_backward_kill_word (vty);
	  break;
	case CONTROL('Z'):
	  vty_end_config (vty);
	  break;
	case '\n':
	case '\r':
	  vty_out (vty, "%s", VTY_NEWLINE);
	  vty_execute (vty);
	  break;
	case '\t':
	  vty_complete_command (vty);
	  break;
	case '?':
	    vty_describe_command (vty);
	  break;
	case '\033':
	  if (i + 1 < nbytes && buf[i + 1] == '[')
	    {
	      vty->escape = VTY_ESCAPE;
	      i++;
	    }
	  else
	    vty->escape = VTY_PRE_ESCAPE;
	  break;
	default:
	  if (buf[i] > 31 && buf[i] < 127)
	    vty_self_insert (vty, buf[i]);
	  break;
	}
    }

  /* Check status. */
  if (vty->status == VTY_CLOSE)
    vty_close (vty);
  else
    {
      vty_event (VTY_WRITE, vty_sock, vty);
      vty_event (VTY_READ, vty_sock, vty);
    }
  return 0;
}

/* Flush buffer to the vty. */
static int
vty_flush (struct thread *thread)
{
  int erase;
  int dont_more;
  int vty_sock = THREAD_FD (thread);
  struct vty *vty = THREAD_ARG (thread);
  vty->t_write = NULL;

  /* Tempolary disable read thread. */
  if (vty->lines == 0)
    if (vty->t_read)
      {
	thread_cancel (vty->t_read);
	vty->t_read = NULL;
      }

  /* Function execution continue. */
  if (vty->status == VTY_START || vty->status == VTY_CONTINUE)
    {
      if (vty->status == VTY_CONTINUE && vty->output_func)
	erase = 1;
      else
	erase = 0;

      if (vty->output_func == NULL)
	dont_more = 1;
      else
	dont_more = 0;

      if (vty->lines == 0)
	{
	  erase = 0;
	  dont_more = 1;
	}

      buffer_flush_vty_all (vty->obuf, vty->fd, erase, dont_more);

      if (vty->status == VTY_CLOSE)
	{
	  vty_close (vty);
	  return 0;
	}

      if (vty->output_func == NULL)
	{
	  vty->status = VTY_NORMAL;
	  vty_prompt (vty);
	  vty_event (VTY_WRITE, vty_sock, vty);
	}
      else
	vty->status = VTY_MORE;

      if (vty->lines == 0)
	{
	  if (vty->output_func == NULL)
	    vty_event (VTY_READ, vty_sock, vty);
	  else
	    {
	      if (vty->output_func)
		(*vty->output_func) (vty, 0);
	      vty_event (VTY_WRITE, vty_sock, vty);
	    }
	}
    }
  else
    {
      if (vty->status == VTY_MORE || vty->status == VTY_MORELINE)
	erase = 1;
      else
	erase = 0;

      if (vty->lines == 0)
	buffer_flush_window (vty->obuf, vty->fd, vty->width, 25, 0, 1);
      else if (vty->status == VTY_MORELINE)
	buffer_flush_window (vty->obuf, vty->fd, vty->width, 1, erase, 0);
      else
	buffer_flush_window (vty->obuf, vty->fd, vty->width,
			     vty->lines >= 0 ? vty->lines : vty->height,
			     erase, 0);
  
      if (buffer_empty (vty->obuf))
	{
	  if (vty->status == VTY_CLOSE)
	    vty_close (vty);
	  else
	    {
	      vty->status = VTY_NORMAL;
	  
	      if (vty->lines == 0)
		vty_event (VTY_READ, vty_sock, vty);
	    }
	}
      else
	{
	  vty->status = VTY_MORE;

	  if (vty->lines == 0)
	    vty_event (VTY_WRITE, vty_sock, vty);
	}
    }

  return 0;
}

/* Create new vty structure. */
struct vty *
vty_create (int vty_sock, union sockunion *su)
{
  struct vty *vty;
  /* Allocate new vty structure and set up default values. */
  vty = vty_new ();
  vty->fd = vty_sock;
  vty->type = VTY_TERM;
  vty->address = sockunion_su2str (su);
   vty->node = VIEW_NODE;
  vty->fail = 0;
  vty->cp = 0;
  vty_clear_buf (vty);
  vty->length = 0;
  memset (vty->hist, 0, sizeof (vty->hist));
  vty->hp = 0;
  vty->hindex = 0;
//  vector_set_index (vtyvec, vty_sock, vty);
  vty->status = VTY_NORMAL;
  vty->v_timeout = vty_timeout_val;
    vty->lines = -1;
  vty->iac = 0;
  vty->iac_sb_in_progress = 0;
  vty->sb_buffer = buffer_new (1024);
  /* Setting up terminal. */
  vty_will_echo (vty);
  vty_will_suppress_go_ahead (vty);

  vty_dont_linemode (vty);
  vty_do_window_size (vty);
  /* vty_dont_lflow_ahead (vty); */
  vty_prompt (vty);

  /* Add read/write thread. */
  vty_event (VTY_WRITE, vty_sock, vty);
  vty_event (VTY_READ, vty_sock, vty);

  return vty;
}

/* Accept connection from the network. */
static int
vty_accept (struct thread *thread)
{
  int vty_sock;
  struct vty *vty;
  (void)vty;
  union sockunion su;
  int ret;
  (void)ret;
  unsigned int on;
  int accept_sock;

  accept_sock = THREAD_FD (thread);

  /* We continue hearing vty socket. */
  vty_event (VTY_SERV, accept_sock, NULL);

  memset (&su, 0, sizeof (union sockunion));

  /* We can handle IPv4 or IPv6 socket. */
  vty_sock = sockunion_accept (accept_sock, &su);
  if (vty_sock < 0)
    {
      return -1;
    }

  on = 1;
  ret = setsockopt (vty_sock, IPPROTO_TCP, TCP_NODELAY, 
		    (char *) &on, sizeof (on));

  vty = vty_create (vty_sock, &su);

  return 0;
}

void
vty_serv_sock_addrinfo (const char *hostname, unsigned short port)
{
  int ret;
  struct addrinfo req;
  struct addrinfo *ainfo;
  struct addrinfo *ainfo_save;
  int sock;
  char port_str[BUFSIZ];

  memset (&req, 0, sizeof (struct addrinfo));
  req.ai_flags = AI_PASSIVE;
  req.ai_family = AF_UNSPEC;
  req.ai_socktype = SOCK_STREAM;
  sprintf (port_str, "%d", port);
  port_str[sizeof (port_str) - 1] = '\0';

  ret = getaddrinfo (hostname, port_str, &req, &ainfo);

  if (ret != 0)
    {
      fprintf (stderr, "getaddrinfo failed: %s\n", gai_strerror (ret));
      return;
    }

  ainfo_save = ainfo;

  do
    {
      if (ainfo->ai_family != AF_INET
	  && ainfo->ai_family != AF_INET6
	  )
	continue;

      sock = socket (ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
      if (sock < 0)
	continue;

      sockopt_reuseaddr (sock);
      sockopt_reuseport (sock);

      ret = bind (sock, ainfo->ai_addr, ainfo->ai_addrlen);
      if (ret < 0)
	{
	  close (sock);	/* Avoid sd leak. */
	  continue;
	}

      ret = listen (sock, 3);
      if (ret < 0) 
	{
	  close (sock);	/* Avoid sd leak. */
	  continue;
	}

      vty_event (VTY_SERV, sock, NULL);
    }
  while ((ainfo = ainfo->ai_next) != NULL);

  freeaddrinfo (ainfo_save);
}

/* Make vty server socket. */
void
vty_serv_sock_family (unsigned short port, int family)
{
  int ret;
  union sockunion su;
  int accept_sock;

  memset (&su, 0, sizeof (union sockunion));
  su.sa.sa_family = family;

  /* Make new socket. */
  accept_sock = sockunion_stream_socket (&su);
  if (accept_sock < 0)
    return;

  /* This is server, so reuse address. */
  sockopt_reuseaddr (accept_sock);
  sockopt_reuseport (accept_sock);

  /* Bind socket to universal address and given port. */
  ret = sockunion_bind (accept_sock, &su, port, NULL);
  if (ret < 0)
    {
      close (accept_sock);	/* Avoid sd leak. */
      return;
    }

  /* Listen socket under queue 3. */
  ret = listen (accept_sock, 3);
  if (ret < 0) 
    {
      close (accept_sock);	/* Avoid sd leak. */
      return;
    }

  /* Add vty server event. */
  vty_event (VTY_SERV, accept_sock, NULL);
}


/* Determine address family to bind. */
void
vty_serv_sock (const char *hostname, unsigned short port, char *path)
{
(void)path;
  /* If port is set to 0, do not listen on TCP/IP at all! */
  if (port)
    {
      vty_serv_sock_addrinfo (hostname, port);
    }
}

/* Close vty interface. */
void
vty_close (struct vty *vty)
{
  int i;

  /* Cancel threads.*/
  if (vty->t_read)
    thread_cancel (vty->t_read);
  if (vty->t_write)
    thread_cancel (vty->t_write);
  if (vty->t_timeout)
    thread_cancel (vty->t_timeout);
  if (vty->t_output)
    thread_cancel (vty->t_output);

  /* Flush buffer. */
  if (! buffer_empty (vty->obuf))
    buffer_flush_all (vty->obuf, vty->fd);

  /* Free input buffer. */
  buffer_free (vty->obuf);

  /* Free SB buffer. */
  if (vty->sb_buffer)
    buffer_free (vty->sb_buffer);

  /* Free command history. */
  for (i = 0; i < VTY_MAXHIST; i++)
    if (vty->hist[i])
      free (vty->hist[i]);

  /* Unset vector. */
 // vector_unset (vtyvec, vty->fd);

  /* Close socket. */
  if (vty->fd > 0)
    close (vty->fd);

  if (vty->address)
    free (vty->address);
  if (vty->buf)
    free (vty->buf);

  /* OK free vty. */
  free (vty);
}

/* When time out occur output message then close connection. */
static int
vty_timeout (struct thread *thread)
{
  struct vty *vty;

  vty = THREAD_ARG (thread);
  vty->t_timeout = NULL;
  vty->v_timeout = 0;

  /* Clear buffer*/
  buffer_reset (vty->obuf);
  vty_out (vty, "%sVty connection is timed out.%s", VTY_NEWLINE, VTY_NEWLINE);

  /* Close connection. */
  vty->status = VTY_CLOSE;
  vty_close (vty);

  return 0;
}

/* Read up configuration file from file_name. */
void
vty_read_file (FILE *confp)
{
  int ret;
  struct vty *vty;

  vty = vty_new ();
  vty->fd = 0;			/* stdout */
  vty->type = VTY_TERM;
  vty->node =  VIEW_NODE;
  
  /* Execute configuration file */
  ret = config_from_file (vty, confp);

  if (ret != CMD_SUCCESS) 
    {
      switch (ret)
	{
	case CMD_ERR_AMBIGUOUS:
	  fprintf (stderr, "Ambiguous command.\n");
	  break;
	case CMD_ERR_NO_MATCH:
	  fprintf (stderr, "There is no such command.\n");
	  break;
	}
      vty_close (vty);
      return;
    }

  vty_close (vty);
}





static void
vty_event (enum event event, int sock, struct vty *vty)
{
  struct thread *vty_serv_thread;
  (void)vty_serv_thread;
  switch (event)
    {
    case VTY_SERV:
      vty_serv_thread = thread_add_read (vty_master, vty_accept, vty, sock);
     // vector_set_index (Vvty_serv_thread, sock, vty_serv_thread);
      break;
    case VTY_READ:
      vty->t_read = thread_add_read (vty_master, vty_read, vty, sock);

      /* Time out treatment. */
      if (vty->v_timeout)
	{
	  if (vty->t_timeout)
	    thread_cancel (vty->t_timeout);
	  vty->t_timeout = 
	    thread_add_timer (vty_master, vty_timeout, vty, vty->v_timeout);
	}
      break;
    case VTY_WRITE:
      if (! vty->t_write)
	vty->t_write = thread_add_write (vty_master, vty_flush, vty, sock);
      break;
    case VTY_TIMEOUT_RESET:
      if (vty->t_timeout)
	{
	  thread_cancel (vty->t_timeout);
	  vty->t_timeout = NULL;
	}
      if (vty->v_timeout)
	{
	  vty->t_timeout = 
	    thread_add_timer (vty_master, vty_timeout, vty, vty->v_timeout);
	}
      break;
    }
}



/* Install vty's own commands like `who' command. */
void
vty_init (struct thread_master *master)
{
	vty_master = master;
	vty_timeout_val = VTY_TIMEOUT_DEFAULT;
	return;
}
