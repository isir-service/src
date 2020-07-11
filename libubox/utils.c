/*
 * utils - misc libubox utility functions
 *
 * Copyright (C) 2012 Felix Fietkau <nbd@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/mman.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include "utils.h"
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include "hash.h"
#include <dirent.h>
#include <fcntl.h>
#include <execinfo.h>

#define BACKTRACE_SIZE   20

#define C_PTR_ALIGN	(sizeof(size_t))
#define C_PTR_MASK	(-C_PTR_ALIGN)

#ifdef LIBUBOX_COMPAT_CLOCK_GETTIME
#include <mach/mach_host.h>		/* host_get_clock_service() */
#include <mach/mach_port.h>		/* mach_port_deallocate() */
#include <mach/mach_init.h>		/* mach_host_self(), mach_task_self() */
#include <mach/clock.h>			/* clock_get_time() */

static clock_serv_t clock_realtime;
static clock_serv_t clock_monotonic;

static void __constructor clock_name_init(void)
{
	mach_port_t host_self = mach_host_self();

	host_get_clock_service(host_self, CLOCK_REALTIME, &clock_realtime);
	host_get_clock_service(host_self, CLOCK_MONOTONIC, &clock_monotonic);
}

static void __destructor clock_name_dealloc(void)
{
	mach_port_t self = mach_task_self();

	mach_port_deallocate(self, clock_realtime);
	mach_port_deallocate(self, clock_monotonic);
}

int clock_gettime(int type, struct timespec *tv)
{
	int retval = -1;
	mach_timespec_t mts;

	switch (type) {
		case CLOCK_REALTIME:
			retval = clock_get_time(clock_realtime, &mts);
			break;
		case CLOCK_MONOTONIC:
			retval = clock_get_time(clock_monotonic, &mts);
			break;
		default:
			goto out;
	}

	tv->tv_sec = mts.tv_sec;
	tv->tv_nsec = mts.tv_nsec;
out:
	return retval;
}

#endif

void *cbuf_alloc(unsigned int order)
{
	char path[] = "/tmp/cbuf-XXXXXX";
	unsigned long size = cbuf_size(order);
	void *ret = NULL;
	int fd;

	fd = mkstemp(path);
	if (fd < 0)
		return NULL;

	if (unlink(path))
		goto close;

	if (ftruncate(fd, cbuf_size(order)))
		goto close;

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

	ret = mmap(NULL, size * 2, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (ret == MAP_FAILED) {
		ret = NULL;
		goto close;
	}

	if (mmap(ret, size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED,
		 fd, 0) != ret ||
	    mmap((char*)ret + size, size, PROT_READ | PROT_WRITE,
		 MAP_FIXED | MAP_SHARED, fd, 0) != (char*)ret + size) {
		munmap(ret, size * 2);
		ret = NULL;
	}

close:
	close(fd);
	return ret;
}

void cbuf_free(void *ptr, unsigned int order)
{
	munmap(ptr, cbuf_size(order) * 2);
}

int isipv4(const char *ip)
{
	int dots = 0;
	int setions = 0;

	if (NULL == ip || *ip == '.') {
		return -1;
	}

	while (*ip) {
		
		if (*ip == '.') {
			dots ++;
			if (setions >= 0 && setions <= 255) {
				setions = 0;
				ip++;
				continue;
			}
			return -1;
		}else if (*ip >= '0' && *ip <= '9') {
			setions = setions * 10 + (*ip - '0');
		} else {
			return -1;
		}
		
		ip++;
	}

	if (setions >= 0 && setions <= 255) {
		if (dots == 3) {
			return 0;
		}
	}

	return -1;
}


int isport(const char *port)
{

	int i = 0;
	int len = 0;
	int port_num = -1;

	
	if (port == NULL) {
		return 0;
	}

	len = strlen(port);
	
	if (port[0] != '+' && (port[0] < '0' || port[0] > '9')) {
		return 0;
	}
	for (i = 1; i < len; i++) {
		if (port[i] < 0 || port[i] > '9') {
			return 0;
		}
	}

	port_num = atoi(port);

	if (port_num <=0 || port_num >65535) {
		return 0;
	}
	
	return 1;
}


void op_daemon(void) 
{ 
	int pid = 0; 
	int fd = 0;
	
	pid = fork();
	if(pid > 0) {
		exit(0);
	}
	else if(pid < 0) { 
		return;
	}
 
	setsid();
 
	pid = fork();
	
	if( pid > 0) {
		exit(0);
	}
	
	else if( pid< 0) {
		return;
	}

	fd = open ("/dev/null", O_RDWR);
	if (fd < 0) {
		return;
	}
	
	dup2 (fd, 0);
	dup2 (fd, 1);
	dup2 (fd, 2);
	
	
	return;
}



unsigned int ipv4touint(const char *str_ip)
{
	struct in_addr addr;
	unsigned int int_ip = 0;
	
	if(inet_aton(str_ip,&addr)) {
		int_ip = ntohl(addr.s_addr);
	}
	
	return int_ip;
}

char * uinttoipv4(unsigned int ip)
{

	struct in_addr addr;

	static char paddr[64] = {};
	memset(paddr, 0, sizeof(paddr));

	addr.s_addr = htonl(ip); 

	snprintf(paddr, sizeof(paddr), "%s", inet_ntoa(addr));

	return paddr;
}


int strlcpy(char *src, const char *dest, unsigned int size)
{

	unsigned int size_copy = 0;

	if (!src || !dest || !size)
		return 0;

	size_copy = size > strlen(dest)?strlen(dest):size-1;
	memcpy(src, dest, size_copy);
	src[size_copy] = 0;

	return size_copy;
}

int is_dir_exist(char *dir, int create)
{
	DIR *_dir = NULL;
	if (!(_dir = opendir(dir))) {
		if (create) {
			if (mkdir(dir, 0755) < 0) {
				return 0;
			}else 
				return 1;
		} else
			return 0;
	}

	closedir(_dir);
	return 1;
}


static void dump_trace(int signo)
{
	(void)signo;
	int j, nptrs;
	void *buffer[BACKTRACE_SIZE];
	char **strings;
	
	nptrs = backtrace(buffer, BACKTRACE_SIZE);
	
	printf("------------------------SEGV!!!------------------------------\n");
 
	strings = backtrace_symbols(buffer, nptrs);
	if (strings == NULL) {
		perror("backtrace_symbols");
		return;
	}

	for (j = 0; j < nptrs; j++)
		printf("[%02d] %s\n", j, strings[j]);

	free(strings);
	printf("------------------------END------------------------------\n");
	return;

}

void signal_segvdump(void)
{
	signal(SIGSEGV, dump_trace);
	return;
}
void signal_action(void)
{
	signal(SIGPIPE, SIG_IGN);
	return;

}


