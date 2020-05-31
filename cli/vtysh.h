#ifndef _VTYSH_H
#define _VTYSH_H

int vtysh_init(void);
char *vtysh_prompt (void);

char *vtysh_rl_gets (char **line_read);

void vtysh_execute (char *line_read);


void vtysh_readline_init (void);
#endif