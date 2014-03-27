// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#define _GNU_SOURCE // asprintf
#include <stdio.h>
#undef _GNU_SOURCE

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "utf.h"

#include "config.h"

#define COMM_MAX 16

int n_cpu;
bool show_heap;
char *filter;
char *argv0;

typedef struct {
	int npids;
	char *name;
	float pss;
	float shared;
	float heap;
	float swapped;
} MemInfo;

static void die(const char *, ...);
static MemInfo *meminfo(int);
static void free_meminfo(MemInfo *);
static int proc_name(MemInfo *, int);
static int proc_mem(MemInfo *, int);
static int proc_cmdline(int, char *buf, size_t len);
static char *_readlink(char *);
static void usage(void);
static int *list_pids(void);

void
die(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

char *
_readlink(char *path)
{
	for (int len=64;; len*=2) {
		char *b = malloc(len);
		ssize_t n = readlink(path, b, len-1);
		if (n == -1) {
			free(b);
			return NULL;
		}
		if (n < len) {
			b[n] = '\0';
			return b;
		}
		free(b);
	}
}

MemInfo *
meminfo(int pid)
{
	int err;
	MemInfo *mi = calloc(sizeof(MemInfo), 1);
	err = proc_name(mi, pid);
	if (err) goto error;

	err = proc_mem(mi, pid);
	if (err) goto error;

	return mi;
error:
	free_meminfo(mi);
	return NULL;
}

void
free_meminfo(MemInfo *mi)
{
	if (mi && mi->name)
		free(mi->name);
	free(mi);
}

int
proc_name(MemInfo *mi, int pid)
{
	int n;
	char path[32], buf[BUFSIZ];
	snprintf(path, sizeof(path), "/proc/%d/exe", pid);

	char *p = _readlink(path);
	// we can't read the exe in 2 cases: the process has exited,
	// or it refers to a kernel thread
	if (!p)
		return 0;

	n = proc_cmdline(pid, buf, BUFSIZ);
	if (n <= 0) {
		free(p);
		return -1;
	}

	if (strlen(buf) > COMM_MAX)
		buf[COMM_MAX] = '\0';
	if (strcmp(p, buf) >= 0)
		mi->name = NULL;//strdup(basename(p));
	else
		mi->name = strdup(buf);
	free(p);
	return 0;
}

int
proc_cmdline(int pid, char *buf, size_t len)
{
	int fd;
	char path[32];
	ssize_t n;

	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, len - 1);
	if (n < 0)
		die("proc_cmdline(%d): read failed: %s", pid, strerror(errno));
	if (!n)
		goto error;

	buf[n] = '\0';

	close(fd);
	return n;
error:
	close(fd);
	return -1;
}

int
proc_mem(MemInfo *mi, int pid)
{
	return 0;
}

int *
list_pids(void)
{
	DIR *proc;
	int nproc;
	size_t n;
	struct dirent *de;
	int *pids;

	proc = opendir("/proc");
	nproc = 0;
	n = 0;

	while ((de = readdir(proc))) {
		if (isdigit(de->d_name[0]))
			nproc++;
	}
	rewinddir(proc);

	pids = calloc(nproc+1, sizeof(int));

	while ((de = readdir(proc))) {
		if (!isdigit(de->d_name[0]))
			continue;
		// between the original readdir and now, the contents
		// of /proc could have changed.  If there are
		// additional processes, make sure we don't overwrite
		// our buffer.
		if (nproc-- == 0)
			break;
		pids[n++] = atoi(de->d_name);
	}

	closedir(proc);
	return pids;
}

void
usage(void)
{
	die("Usage: %s [OPTION...]\n" \
	    "Simple, accurate RAM and swap reporting.\n\n" \
	    "Options:\n" \
	    "  -heap=false:\tshow heap column\n", argv0);
}

int
main(int argc, char *const argv[])
{
	//int err;
	//cpu_set_t n_cpu_set;
	int *pids;

	for (argv0 = argv[0], argv++, argc--; argc > 0; argv++, argc--) {
		char const* arg = argv[0];
		if (strcmp("-help", arg) == 0) {
			usage();
		} else if (strcmp("-heap", arg) == 0) {
			show_heap = true;
		} else if (arg[0] == '-') {
			fprintf(stderr, "unknown arg '%s'\n", arg);
			usage();
		} else {
			fprintf(stderr, "unknown arg '%s'\n", arg);
			usage();
		}
	}

	if (geteuid() != 0) {
		/*
		die("%s requires root privileges. (try 'sudo `which %s`)\n",
		    argv0, argv0);
		*/
	}

	/*
	err = sched_getaffinity(0, sizeof(n_cpu_set), &n_cpu_set);
	n_cpu = err ? 1 : CPU_COUNT(&n_cpu_set);
	*/

	pids = list_pids();
	if (!pids)
		die("list_pids failed\n");

	for (int *pid = pids; *pid; pid++) {
		MemInfo *mi = meminfo(*pid);
		free_meminfo(mi);
	}
	free(pids);

	return 0;
}
