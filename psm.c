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
#define CMD_DISPLAY_MAX 32
#define PAGE_SIZE 4096
// from ps_mem - average error due to truncation in the kernel pss
// calculations
#define PSS_ADJUST .5
#define MAP_DETAIL_LEN sizeof("Size:                  4 kB")
#define LINE_BUF_SIZE 256

#define TY_VM_FLAGS      "VmFlags:"
#define TY_PSS           "Pss:"
#define TY_SWAP          "Swap:"
#define TY_PRIVATE_CLEAN "Private_Clean:"
#define TY_PRIVATE_DIRTY "Private_Dirty:"

#define OFF_NAME 73

char *filter;
char *argv0;

typedef struct {
	int npids;
	char *name;
	float pss;
	float shared;
	float heap;
	float swap;
} MemInfo;

typedef struct {
	int count;
	int *list;
} PIDList;

static void die(const char *, ...);

static MemInfo *meminfo_new(int);
static void meminfo_free(MemInfo *);

static int proc_name(MemInfo *, int);
static int proc_mem(MemInfo *, int);
static int proc_cmdline(int, char *buf, size_t len);
static char *_readlink(char *);

static void usage(void);

static int cmp_meminfop_name(const void *, const void *);
static int cmp_meminfop_pss(const void *, const void *);

static PIDList list_pids(void);


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
		ssize_t n = readlink(path, b, len);
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
meminfo_new(int pid)
{
	int err;
	MemInfo *mi = calloc(sizeof(MemInfo), 1);
	mi->npids = 1;

	err = proc_name(mi, pid);
	if (err) goto error;

	err = proc_mem(mi, pid);
	if (err) goto error;

	return mi;
error:
	meminfo_free(mi);
	mi = NULL;
	return NULL;
}

void
meminfo_free(MemInfo *mi)
{
	if (mi && mi->name)
		free(mi->name);
	free(mi);
}

int
proc_name(MemInfo *mi, int pid)
{
	int n;
	char path[32], buf[BUFSIZ], shortbuf[COMM_MAX+1];
	snprintf(path, sizeof(path), "/proc/%d/exe", pid);

	char *p = _readlink(path);
	// we can't read the exe in 2 cases: the process has exited,
	// or it refers to a kernel thread.  Either way, we don't want
	// to gather info on it.
	if (!p)
		return -1;

	n = proc_cmdline(pid, buf, BUFSIZ);
	if (n <= 0) {
		free(p);
		return -1;
	}

	strncpy(shortbuf, buf, COMM_MAX);
	shortbuf[COMM_MAX] = '\0';

	if (strcmp(p, shortbuf) >= 0)
		mi->name = strdup(basename(p));
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
	float priv;
	FILE *f;
	char *curr;
	char path[32];
	snprintf(path, sizeof(path), "/proc/%d/smaps", pid);

	priv = 0;
	curr = NULL;

	f = fopen(path, "r");
	if (!f)
		return -1;

	while (true) {
		char line[LINE_BUF_SIZE], *ok, *rest;
		size_t len;
		float m;

		ok = fgets(line, LINE_BUF_SIZE, f);
		if (!ok)
			break;

		len = strlen(line);
		if (len != MAP_DETAIL_LEN) {
			if (strcmp(line, TY_VM_FLAGS) < 0) {
				if (curr) {
					free(curr);
					curr = NULL;
				}
				if (len > OFF_NAME) {
					curr = strdup(&line[OFF_NAME]);
					*strchr(curr, '\n') = '\0';
				}
			}
			if (!len)
				break;
			continue;
		}

		rest = &line[16];
		if (strncmp(line, TY_PSS, sizeof(TY_PSS)-1) == 0) {
			m = atoi(rest);
			mi->pss += m + PSS_ADJUST;
			// we don't need PSS_ADJUST for heap because
			// the heap is private and anonymous.
			if (curr && strcmp(curr, "[heap]") == 0)
				mi->heap = m;
		} else if (strncmp(line, TY_PRIVATE_CLEAN, sizeof(TY_PRIVATE_CLEAN)-1) == 0 ||
			   strncmp(line, TY_PRIVATE_DIRTY, sizeof(TY_PRIVATE_DIRTY)-1) == 0) {
			priv += atoi(rest);
		} else if (strncmp(line, TY_SWAP, sizeof(TY_SWAP)-1) == 0) {
			mi->swap += atoi(rest);
		}
	}
	mi->shared = mi->pss - priv;

	free(curr);
	fclose(f);
	return 0;
}

PIDList
list_pids(void)
{
	DIR *proc;
	int nproc;
	size_t n;
	struct dirent *de;
	PIDList pids;

	memset(&pids, 0, sizeof(pids));

	proc = opendir("/proc");
	nproc = 0;
	n = 0;

	while ((de = readdir(proc))) {
		if (isdigit(de->d_name[0]))
			nproc++;
	}
	rewinddir(proc);

	pids.count = nproc;
	pids.list = calloc(nproc+1, sizeof(int));

	while ((de = readdir(proc))) {
		if (!isdigit(de->d_name[0]))
			continue;
		// between the original readdir and now, the contents
		// of /proc could have changed.  If there are
		// additional processes, make sure we don't overwrite
		// our buffer.
		if (nproc-- == 0)
			break;
		pids.list[n++] = atoi(de->d_name);
	}

	closedir(proc);
	return pids;
}

int
cmp_meminfop_name(const void *a_in, const void *b_in)
{
	const MemInfo *a = *(MemInfo *const *)a_in;
	const MemInfo *b = *(MemInfo *const *)b_in;
	return strcmp(a->name, b->name);
}

int
cmp_meminfop_pss(const void *a_in, const void *b_in)
{
	const MemInfo *a = *(MemInfo *const *)a_in;
	const MemInfo *b = *(MemInfo *const *)b_in;
	if (a->pss < b->pss)
		return -1;
	else if (a->pss > b->pss)
		return 1;
	return strcmp(a->name, b->name);
}

void
usage(void)
{
	die("Usage: %s [OPTION...]\n" \
	    "Simple, accurate RAM and swap reporting.\n\n" \
	    "Options:\n" \
	    "  -heap:\tshow heap column\n", argv0);
}

int
main(int argc, char *const argv[])
{
	float tot_pss, tot_swap;
	int n, nuniq, next;
	PIDList pids;
	MemInfo **cmds, **cmd_sums;
	const char *tot_fmt;
	bool show_heap;

	tot_pss = 0;
	tot_swap = 0;
	show_heap = false;

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
		die("%s requires root privileges. (try 'sudo `which %s`)\n",
		    argv0, argv0);
	}

	pids = list_pids();
	if (!pids.count)
		die("list_pids failed\n");

	cmds = calloc(sizeof(MemInfo*), pids.count);
	n = 0;
	for (int *pid = pids.list; *pid; pid++) {
		MemInfo *mi = meminfo_new(*pid);
		if (mi)
			cmds[n++] = mi;
	}
	free(pids.list);
	pids.list = NULL;

	// n is potentially smaller than pids.count, so free any
	// unused space
	cmds = realloc(cmds, n*sizeof(MemInfo*));

	qsort(cmds, n, sizeof(MemInfo*), cmp_meminfop_name);

	nuniq = 0;
	for (int i = 0; i < n; i++) {
		if (i == 0 || strcmp(cmds[i-1]->name, cmds[i]->name) != 0)
			nuniq++;
	}

	cmd_sums = calloc(sizeof(MemInfo*), nuniq);
	next = 0;

	for (int i = 0; i < n; i++) {
		if (i == 0 || strcmp(cmds[i-1]->name, cmds[i]->name) != 0) {
			cmd_sums[next++] = cmds[i];
		} else {
			MemInfo *curr = cmds[i];
			MemInfo *sum = cmd_sums[next-1];
			sum->npids++;
			sum->pss += curr->pss;
			sum->shared += curr->shared;
			sum->heap += curr->heap;
			sum->swap += curr->swap;
		}
	}

	if (show_heap) {
		tot_fmt = "#%9.1f%30.1f\tTOTAL USED BY PROCESSES\n";
		printf("%10s%10s%10s%10s\t%s\n", "MB RAM", "SHARED", "HEAP", "SWAPPED", "PROCESS (COUNT)");
	} else {
		tot_fmt = "#%9.1f%20.1f\tTOTAL USED BY PROCESSES\n";
		printf("%10s%10s%10s\t%s\n", "MB RAM", "SHARED", "SWAPPED", "PROCESS (COUNT)");
	}

	qsort(cmd_sums, nuniq, sizeof(MemInfo*), cmp_meminfop_pss);
	for (int i = 0; i < nuniq; i++) {
		char sbuf[16];
		MemInfo *c = cmd_sums[i];
		char *n = c->name;
		float pss;
		if (strlen(n) > CMD_DISPLAY_MAX) {
			if (n[0] == '[' && strchr(n, ']'))
				strchr(n, ']')[1] = '\0';
			else
				n[CMD_DISPLAY_MAX] = '\0';
		}
		sbuf[0] = '\0';
		if (c->swap > 0) {
			float swap = c->swap / 1024.;
			tot_swap += swap;
			snprintf(sbuf, sizeof(sbuf), "%10.1f", swap);
		}
		pss = c->pss / 1024.;
		tot_pss += pss;
		if (show_heap)
			printf("%10.1f%10.1f%10.1f%10s\t%s (%d)\n", pss,
			       c->shared/1024., c->heap/1024., sbuf, n, c->npids);
		else
			printf("%10.1f%10.1f%10s\t%s (%d)\n", pss,
			       c->shared/1024., sbuf, n, c->npids);
	}
	free(cmd_sums);

	for (int i = 0; i < n; i++)
		meminfo_free(cmds[i]);
	free(cmds);

	printf(tot_fmt, tot_pss, tot_swap);
	fflush(stdout);

	return 0;
}
