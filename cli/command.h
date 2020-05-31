/*
 * Zebra configuration command interface routine
 * Copyright (C) 1997, 98 Kunihiro Ishiguro
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2, or (at your
 * option) any later version.
 * 
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _ZEBRA_COMMAND_H
#define _ZEBRA_COMMAND_H

#include "vector.h"
#include "vty.h"
#include <sys/types.h>
#include <stdio.h>
/* Host configuration variable */
struct _host
{
  /* Host name of this router. */
  char *name;
 char name_recv[60];
  /* Password for vty interface. */
  char *password;
  
  char password_recv[120];
  char *password_encrypt;

  /* Enable password */
  char *enable;
  char *enable_encrypt;

  /* System wide terminal lines. */
  int lines;

  /* Log filename. */
  char *logfile;

  /* Log stdout. */
  unsigned char log_stdout;

  /* Log syslog. */
  unsigned char log_syslog;
  
  /* config file name of this host */
  char *config;

  /* Flags for services */
  int advanced;
  int encrypt;

  /* Banner configuration. */
  char *motd;
};

/* There are some command levels which called from command node. */
enum node_type 
{
	VIEW_NODE,
	ENABLE_NODE,			/* Enable node. */
	CONFIG_NODE,
	DEBUG_NODE,
	SHOW_NODE,
	VTY_NODE			/* Vty node. */
};

/* Node which has some commands and prompt string and configuration
   function pointer . */
struct cmd_node 
{
  /* Node index. */
  enum node_type node;		

  /* Prompt character at vty interface. */
  char *prompt;			

  /* Is this node's configuration goes to vtysh ? */
  int vtysh;
  
  /* Node's configuration write function */
  int (*func) (struct vty *);

  /* Vector of this node's command list. */
  vector cmd_vector;	
};

/* Structure of command element. */
struct cmd_element 
{
  char *string;			/* Command specification by string. */
  int (*func) (struct cmd_element *, struct vty *, int, char **);
  char *doc;			/* Documentation of this command. */
  int daemon;                   /* Daemon to which this command belong. */
  vector strvec;		/* Pointing out each description vector. */
  int cmdsize;			/* Command index count. */
  char *config;			/* Configuration string */
  vector subconfig;		/* Sub configuration string */
};

/* Command description structure. */
struct desc
{
  char *cmd;			/* Command string. */
  char *str;			/* Command's description. */
};

/* Return value of the commands. */
#define CMD_SUCCESS              0
#define CMD_WARNING              1
#define CMD_ERR_NO_MATCH         2
#define CMD_ERR_AMBIGUOUS        3
#define CMD_ERR_INCOMPLETE       4
#define CMD_ERR_EXEED_ARGC_MAX   5
#define CMD_ERR_NOTHING_TODO     6
#define CMD_COMPLETE_FULL_MATCH  7
#define CMD_COMPLETE_MATCH       8
#define CMD_COMPLETE_LIST_MATCH  9
#define CMD_SUCCESS_DAEMON      10

/* Argc max counts. */
#define CMD_ARGC_MAX   25

/* Turn off these macros when uisng cpp with extract.pl */

/* DEFUN for vty command interafce. Little bit hacky ;-). */
#define DEFUN(funcname, cmdname, cmdstr, helpstr) \
  int funcname (struct cmd_element *, struct vty *, int, char **); \
  struct cmd_element cmdname = \
  { \
    cmdstr, \
    funcname, \
    helpstr ,\
    0,\
    NULL,\
    0,\
    NULL,\
    NULL,\
  }; \
  int funcname \
  (struct cmd_element *self, struct vty *vty, int argc, char **argv)

/* DEFUN_NOSH for commands that vtysh should ignore */
#define DEFUN_NOSH(funcname, cmdname, cmdstr, helpstr) \
  DEFUN(funcname, cmdname, cmdstr, helpstr)

/* DEFSH for vtysh. */
#define DEFSH(daemon, cmdname, cmdstr, helpstr) \
  struct cmd_element cmdname = \
  { \
    cmdstr, \
    NULL, \
    helpstr, \
    daemon \
  }; \

/* DEFUN + DEFSH */
#define DEFUNSH(daemon, funcname, cmdname, cmdstr, helpstr) \
  int funcname (struct cmd_element *, struct vty *, int, char **); \
  struct cmd_element cmdname = \
  { \
    cmdstr, \
    funcname, \
    helpstr, \
    daemon \
  }; \
  int funcname \
  (struct cmd_element *self, struct vty *vty, int argc, char **argv)

/* ALIAS macro which define existing command's alias. */
#define ALIAS(funcname, cmdname, cmdstr, helpstr) \
  struct cmd_element cmdname = \
  { \
    cmdstr, \
    funcname, \
    helpstr \
  };


/* Some macroes */
#define CMD_OPTION(S)   ((S[0]) == '[')
#define CMD_VARIABLE(S) (((S[0]) >= 'A' && (S[0]) <= 'Z') || ((S[0]) == '<'))
#define CMD_VARARG(S)   ((S[0]) == '.')
#define CMD_RANGE(S)	((S[0] == '<'))

#define CMD_IPV4(S)	   ((strcmp ((S), "A.B.C.D") == 0))
#define CMD_IPV4_PREFIX(S) ((strcmp ((S), "A.B.C.D/M") == 0))
#define CMD_IPV6(S)        ((strcmp ((S), "X:X::X:X") == 0))
#define CMD_IPV6_PREFIX(S) ((strcmp ((S), "X:X::X:X/M") == 0))

/* Prototypes. */
void install_node (struct cmd_node *, int (*) (struct vty *));
void install_element (enum node_type, struct cmd_element *);
void sort_node (void);

vector cmd_make_strvec (char *);
void cmd_free_strvec (vector);
vector cmd_describe_command ();
char **cmd_complete_command ();
char *cmd_prompt (enum node_type);
int config_from_file (struct vty *, FILE *);
int cmd_execute_command (vector, struct vty *, struct cmd_element **);
int cmd_execute_command_strict (vector, struct vty *, struct cmd_element **);
void config_replace_string (struct cmd_element *, char *, ...);
void cmd_init (void);

void install_list(int node_type);
void install_end(int node_type);
void cmd_exit(void);
/* Export typical functions. */



#endif /* _ZEBRA_COMMAND_H */
