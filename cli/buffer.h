#ifndef _CLI_BUFFER_H
#define _CLI_BUFFER_H
#include <sys/types.h>

/* Buffer master. */
struct buffer
{
  /* Data list. */
  struct buffer_data *head;
  struct buffer_data *tail;

  /* Current allocated data. */
  unsigned long alloc;

  /* Total length of buffer. */
  unsigned long size;

  /* For allocation. */
  struct buffer_data *unused_head;
  struct buffer_data *unused_tail;

  /* Current total length of this buffer. */
  unsigned long length;
};

/* Data container. */
struct buffer_data
{
  struct buffer *parent;
  struct buffer_data *next;
  struct buffer_data *prev;

  /* Acctual data stream. */
  unsigned char *data;

  /* Current pointer. */
  unsigned long cp;

  /* Start pointer. */
  unsigned long sp;
};

/* Buffer prototypes. */
struct buffer *buffer_new (size_t size);
int buffer_write (struct buffer *, u_char *, size_t);
void buffer_free (struct buffer *);
char *buffer_getstr (struct buffer *);
int buffer_putc (struct buffer *, u_char);
int buffer_putstr (struct buffer *, u_char *);
void buffer_reset (struct buffer *);
int buffer_flush_all (struct buffer *, int);
int buffer_flush_vty_all (struct buffer *, int, int, int);
int buffer_flush_window (struct buffer *, int, int, int, int, int);
int buffer_empty (struct buffer *);

#endif /* _ZEBRA_BUFFER_H */
