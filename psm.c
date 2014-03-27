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
#include <ftw.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "utf.h"

#include "config.h"

int n_cpu;
bool show_heap;
char *filter;
char *argv0;


static void die(const char *, ...);
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
	int err, procfd;
	cpu_set_t n_cpu_set;
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

	err = sched_getaffinity(0, sizeof(n_cpu_set), &n_cpu_set);
	n_cpu = err ? 1 : CPU_COUNT(&n_cpu_set);

	pids = list_pids();
	if (!pids)
		die("list_pids failed\n");

	procfd = open("/proc", O_DIRECTORY | O_CLOEXEC);
	if (!procfd)
		die("couldn't open /proc\n");

	for (int *pid = pids; *pid; pid++) {
	}

	close(procfd);

	return 0;
}
