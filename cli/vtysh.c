#include "vtysh.h"
#include "vty.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include "command.h"

#include <stdlib.h>


struct vty *vtysh_vty;
int vtysh_init(void)
{

	vtysh_vty = vty_new ();
	vtysh_vty->node = VIEW_NODE;
	vtysh_vty->type = VTY_SHELL;
	cmd_init();
	return 0;
}

char *
vtysh_prompt (void)
{
  struct utsname names;
  static char buf[100];
  const char*hostname;
  extern struct _host host;

  hostname = host.name;

  if (!hostname)
    {
      uname (&names);
      hostname = names.nodename;
    }

  snprintf (buf, sizeof buf, cmd_prompt (vtysh_vty->node), hostname);

  return buf;
}


char *
vtysh_rl_gets (char **line_read)
{
  /* If the buffer has already been allocated, return the memory
     to the free pool. */
  if (*line_read)
    {
      free (*line_read);
      *line_read = NULL;
    }
     
  /* Get a line from the user.  Change prompt according to node.  XXX. */
  *line_read = readline (vtysh_prompt ());
   if (*line_read && **line_read)
     add_history(*line_read);
   
  return (*line_read);
}



void
vtysh_execute_func (char *line, int pager)
{
  int ret, cmd_stat;
  (void)cmd_stat;
  (void)pager;
  vector vline;
  struct cmd_element *cmd;
  FILE *fp = NULL;
	(void)fp;
  /* Split readline string up into the vector */
  vline = cmd_make_strvec (line);

  if (vline == NULL)
    return;

  ret = cmd_execute_command (vline, vtysh_vty, &cmd);

  cmd_free_strvec (vline);

  switch (ret)
    {
    case CMD_WARNING:
      if (vtysh_vty->type == VTY_FILE)
	printf ("Warning...\n");
      break;
    case CMD_ERR_AMBIGUOUS:
      printf ("%% Ambiguous command.\n");
      break;
    case CMD_ERR_NO_MATCH:
      printf ("%% Unknown command.\n");
      break;
    case CMD_ERR_INCOMPLETE:
      printf ("%% Command incomplete.\n");
      break;
    default:
		break;

  	}
	return;
}

void vtysh_execute (char *line_read)
{
  vtysh_execute_func (line_read, 1);
  return;
}

int
vtysh_rl_describe ()
{
  int ret;
  unsigned int i;
  vector vline;
  vector describe;
  int width;
  struct desc *desc;

  vline = cmd_make_strvec (rl_line_buffer);

  /* In case of '> ?'. */
  if (vline == NULL)
    {
      vline = vector_init (1);
      vector_set (vline, '\0');
    }
  else 
    if (rl_end && isspace ((int) rl_line_buffer[rl_end - 1]))
      vector_set (vline, '\0');

  describe = cmd_describe_command (vline, vtysh_vty, &ret);

  printf ("\n");

  /* Ambiguous and no match error. */
  switch (ret)
    {
    case CMD_ERR_AMBIGUOUS:
      cmd_free_strvec (vline);
      printf ("%% Ambiguous command.\n");
      rl_on_new_line ();
      return 0;
      break;
    case CMD_ERR_NO_MATCH:
      cmd_free_strvec (vline);
      printf ("%% There is no matched command.\n");
      rl_on_new_line ();
      return 0;
      break;
    }  

  /* Get width of command string. */
  width = 0;
  for (i = 0; i < vector_max (describe); i++)
    if ((desc = vector_slot (describe, i)) != NULL)
      {
	int len;

	if (desc->cmd[0] == '\0')
	  continue;

	len = strlen (desc->cmd);
	if (desc->cmd[0] == '.')
	  len--;

	if (width < len)
	  width = len;
      }

  for (i = 0; i < vector_max (describe); i++)
    if ((desc = vector_slot (describe, i)) != NULL)
      {
	if (desc->cmd[0] == '\0')
	  continue;

	if (! desc->str)
	  printf ("  %-s\n",
		  desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd);
	else
	  printf ("  %-*s  %s\n",
		  width,
		  desc->cmd[0] == '.' ? desc->cmd + 1 : desc->cmd,
		  desc->str);
      }

  cmd_free_strvec (vline);
  vector_free (describe);

  rl_on_new_line();

  return 0;
}



char*
vtysh_completion_entry_function (const char* ignore, int invoking_key)
{
(void)ignore;
(void)invoking_key;
  return NULL;
}

int complete_status;


char *
command_generator (const char *text, int state)
{
(void)text;
  vector vline;
  static char **matched = NULL;
  static int index = 0;

  /* First call. */
  if (! state)
    {
      index = 0;

      vline = cmd_make_strvec (rl_line_buffer);
      if (vline == NULL)
	return NULL;

      if (rl_end && isspace ((int) rl_line_buffer[rl_end - 1]))
	vector_set (vline, '\0');

      matched = cmd_complete_command (vline, vtysh_vty, &complete_status);
    }

  if (matched && matched[index])
    return matched[index++];

  return NULL;
}


char **
new_completion (char *text, int start, int end)
{
  char **matches;
	(void)start;
	(void)end;
  matches = rl_completion_matches (text, command_generator);

  if (matches)
    {
      rl_point = rl_end;
      if (complete_status == CMD_COMPLETE_FULL_MATCH)
	rl_pending_input = ' ';
    }

  return matches;
}

void vtysh_readline_init (void)
{
  /* readline related settings. */
  rl_bind_key ('?', vtysh_rl_describe);
  rl_completion_entry_function = vtysh_completion_entry_function;
  rl_attempted_completion_function = (rl_completion_func_t *)new_completion;
  /* do not append space after completion. It will be appended
     in new_completion() function explicitly */
  rl_completion_append_character = '\0';
}




