/* Socket union related function.
 * Copyright (c) 1997, 98 Kunihiro Ishiguro
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
#include "vty.h"
#include "sockunion.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

const char *
inet_sutop (union sockunion *su, char *str)
{
  switch (su->sa.sa_family)
    {
    case AF_INET:
      inet_ntop (AF_INET, &su->sin.sin_addr, str, INET_ADDRSTRLEN);
      break;
    }
  return str;
}

int
str2sockunion (char *str, union sockunion *su)
{
  int ret;

  memset (su, 0, sizeof (union sockunion));

  ret = inet_pton (AF_INET, str, &su->sin.sin_addr);
  if (ret > 0)			/* Valid IPv4 address format. */
    {
      su->sin.sin_family = AF_INET;
      return 0;
    }
  return -1;
}

const char *
sockunion2str (union sockunion *su, char *buf, size_t len)
{
  if  (su->sa.sa_family == AF_INET)
    return inet_ntop (AF_INET, &su->sin.sin_addr, buf, len);
  return NULL;
}

union sockunion *
sockunion_str2su (char *str)
{
  int ret;
  union sockunion *su;

  su = calloc (1,sizeof (union sockunion));
  memset (su, 0, sizeof (union sockunion));

  ret = inet_pton (AF_INET, str, &su->sin.sin_addr);
  if (ret > 0)			/* Valid IPv4 address format. */
    {
      su->sin.sin_family = AF_INET;
      return su;
    }

  free (su);
  return NULL;
}

char *
sockunion_su2str (union sockunion *su)
{
  char str[INET6_ADDRSTRLEN];

  switch (su->sa.sa_family)
    {
    case AF_INET:
      inet_ntop (AF_INET, &su->sin.sin_addr, str, sizeof (str));
      break;
    }
  return strdup (str);
}

/* Return socket of sockunion. */
int
sockunion_socket (union sockunion *su)
{
  int sock;

  sock = socket (su->sa.sa_family, SOCK_STREAM, 0);
  if (sock < 0)
    {
      return -1;
    }

  return sock;
}

/* Return accepted new socket file descriptor. */
int
sockunion_accept (int sock, union sockunion *su)
{
  socklen_t len;
  int client_sock;

  len = sizeof (union sockunion);
  client_sock = accept (sock, (struct sockaddr *) su, &len);
  
  /* Convert IPv4 compatible IPv6 address to IPv4 address. */

  return client_sock;
}

/* Return sizeof union sockunion.  */
int
sockunion_sizeof (union sockunion *su)
{
  int ret;

  ret = 0;
  switch (su->sa.sa_family)
    {
    case AF_INET:
      ret = sizeof (struct sockaddr_in);
      break;
    }
  return ret;
}

/* return sockunion structure : this function should be revised. */
char *
sockunion_log (union sockunion *su)
{
  static char buf[SU_ADDRSTRLEN];

  switch (su->sa.sa_family) 
    {
    case AF_INET:
      snprintf (buf, BUFSIZ, "%s", inet_ntoa (su->sin.sin_addr));
      break;
    default:
      snprintf (buf, BUFSIZ, "af_unknown %d ", su->sa.sa_family);
      break;
    }
  return buf;
}

/* sockunion_connect returns
   -1 : error occured
   0 : connect success
   1 : connect is in progress */
enum connect_result
sockunion_connect (int fd, union sockunion *peersu, unsigned short port,
		   unsigned int ifindex)
{
(void)ifindex;
  int ret;
  int val;
  union sockunion su;

  memcpy (&su, peersu, sizeof (union sockunion));

  switch (su.sa.sa_family)
    {
    case AF_INET:
      su.sin.sin_port = port;
      break;
    }      

  /* Make socket non-block. */
  val = fcntl (fd, F_GETFL, 0);
  fcntl (fd, F_SETFL, val|O_NONBLOCK);

  /* Call connect function. */
  ret = connect (fd, (struct sockaddr *) &su, sockunion_sizeof (&su));

  /* Immediate success */
  if (ret == 0)
    {
      fcntl (fd, F_SETFL, val);
      return connect_success;
    }

  /* If connect is in progress then return 1 else it's real error. */
  if (ret < 0)
    {
      if (errno != EINPROGRESS)
	{

	  return connect_error;
	}
    }

  fcntl (fd, F_SETFL, val);

  return connect_in_progress;
}

/* Make socket from sockunion union. */
int
sockunion_stream_socket (union sockunion *su)
{
  int sock;

  if (su->sa.sa_family == 0)
    su->sa.sa_family = AF_INET_UNION;

  sock = socket (su->sa.sa_family, SOCK_STREAM, 0);

  return sock;
}

/* Bind socket to specified address. */
int
sockunion_bind (int sock, union sockunion *su, unsigned short port, 
		union sockunion *su_addr)
{
  int size = 0;
  int ret;

  if (su->sa.sa_family == AF_INET)
    {
      size = sizeof (struct sockaddr_in);
      su->sin.sin_port = htons (port);
      if (su_addr == NULL)
	su->sin.sin_addr.s_addr = htonl (INADDR_ANY);
    }  

  ret = bind (sock, (struct sockaddr *)su, size);


  return ret;
}

int
sockopt_reuseaddr (int sock)
{
  int ret;
  int on = 1;

  ret = setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, 
		    (void *) &on, sizeof (on));
  if (ret < 0)
    {
      return -1;
    }
  return 0;
}

int
sockopt_reuseport (int sock)
{
  int ret;
  int on = 1;

  ret = setsockopt (sock, SOL_SOCKET, SO_REUSEPORT, 
		    (void *) &on, sizeof (on));
  if (ret < 0)
    {
      return -1;
    }
  return 0;
}




/* If same family and same prefix return 1. */
int
sockunion_same (union sockunion *su1, union sockunion *su2)
{
  int ret = 0;

  if (su1->sa.sa_family != su2->sa.sa_family)
    return 0;

  switch (su1->sa.sa_family)
    {
    case AF_INET:
      ret = memcmp (&su1->sin.sin_addr, &su2->sin.sin_addr,
		    sizeof (struct in_addr));
      break;
    }
  if (ret == 0)
    return 1;
  else
    return 0;
}

/* After TCP connection is established.  Get local address and port. */
union sockunion *
sockunion_getsockname (int fd)
{
  int ret;
  socklen_t len;
  union
  {
    struct sockaddr sa;
    struct sockaddr_in sin;
    char tmp_buffer[128];
  } name;
  union sockunion *su;

  memset (&name, 0, sizeof name);
  len = sizeof name;

  ret = getsockname (fd, (struct sockaddr *)&name, &len);
  if (ret < 0)
    {

      return NULL;
    }

  if (name.sa.sa_family == AF_INET)
    {
      su = calloc (1,sizeof (union sockunion));
      memcpy (su, &name, sizeof (struct sockaddr_in));
      return su;
    }
  return NULL;
}

/* After TCP connection is established.  Get remote address and port. */
union sockunion *
sockunion_getpeername (int fd)
{
  int ret;
  socklen_t len;
  union
  {
    struct sockaddr sa;
    struct sockaddr_in sin;
    char tmp_buffer[128];
  } name;
  union sockunion *su;

  memset (&name, 0, sizeof name);
  len = sizeof name;
  ret = getpeername (fd, (struct sockaddr *)&name, &len);
  if (ret < 0)
    {

      return NULL;
    }

  if (name.sa.sa_family == AF_INET)
    {
      su = calloc ( 1,sizeof (union sockunion));
      memcpy (su, &name, sizeof (struct sockaddr_in));
      return su;
    }
  return NULL;
}

/* Print sockunion structure */
void
sockunion_print (union sockunion *su)
{
  if (su == NULL)
    return;

  switch (su->sa.sa_family) 
    {
    case AF_INET:
      printf ("%s\n", inet_ntoa (su->sin.sin_addr));
      break;
    default:
      printf ("af_unknown %d\n", su->sa.sa_family);
      break;
    }
}


int
sockunion_cmp (union sockunion *su1, union sockunion *su2)
{
  if (su1->sa.sa_family > su2->sa.sa_family)
    return 1;
  if (su1->sa.sa_family < su2->sa.sa_family)
    return -1;

  if (su1->sa.sa_family == AF_INET)
    {
      if (ntohl (su1->sin.sin_addr.s_addr) == ntohl (su2->sin.sin_addr.s_addr))
	return 0;
      if (ntohl (su1->sin.sin_addr.s_addr) > ntohl (su2->sin.sin_addr.s_addr))
	return 1;
      else
	return -1;
    }
  return 0;
}

/* Duplicate sockunion. */
union sockunion *
sockunion_dup (union sockunion *su)
{
  union sockunion *dup = calloc (1,sizeof (union sockunion));
  memcpy (dup, su, sizeof (union sockunion));
  return dup;
}

void
sockunion_free (union sockunion *su)
{
  free (su);
}
