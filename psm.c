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

#include "config.h"

#define COMM_MAX 16
#define CMD_DISPLAY_MAX 32
#define PAGE_SIZE 4096
// from ps_mem - average error due to truncation in the kernel pss
// calculations
#define PSS_ADJUST       .5
#define MAP_DETAIL_LEN   sizeof("Size:                  4 kB")-1
#define MAP_DETAIL_OFF   16
#define SMAP_DETAILS_LEN 392
#define LINE_BUF_SIZE    SMAP_DETAILS_LEN + 1

#define TY_VM_FLAGS      "VmFlags:"
#define LEN_VM_FLAGS     sizeof(TY_VM_FLAGS)-1

#define OFF_NAME 73


typedef struct {
	int npids;
	char *name;
	float pss;
	float shared;
	float heap;
	float swap;
} CmdInfo;


static void die(const char *, ...);

static CmdInfo *cmdinfo_new(int);
static void cmdinfo_free(CmdInfo *);

static int proc_name(CmdInfo *, int);
static int proc_mem(CmdInfo *, int);
static int proc_cmdline(int, char *buf, size_t len);
static char *_readlink(char *);

static void usage(void);
static void print_results(CmdInfo **, size_t, bool, bool, char *);

static int cmp_cmdinfop_name(const void *, const void *);
static int cmp_cmdinfop_pss(const void *, const void *);

static ssize_t list_pids(int **);


static char *argv0;


void __attribute__((noreturn))
die(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

/// _readlink is a simple wrapper around readlink which returns a
/// malloc'ed buffer that the caller now owns containing the
/// null-terminated contents of the symbolic link.
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

CmdInfo *
cmdinfo_new(int pid)
{
	int err;
	CmdInfo *mi = calloc(sizeof(CmdInfo), 1);
	mi->npids = 1;

	err = proc_name(mi, pid);
	if (err) goto error;

	err = proc_mem(mi, pid);
	if (err) goto error;

	return mi;
error:
	cmdinfo_free(mi);
	mi = NULL;
	return NULL;
}

void
cmdinfo_free(CmdInfo *mi)
{
	if (mi && mi->name)
		free(mi->name);
	free(mi);
}

int
proc_name(CmdInfo *mi, int pid)
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
proc_mem(CmdInfo *mi, int pid)
{
	float priv;
	FILE *f;
	bool skip_read;
	char path[32];
	snprintf(path, sizeof(path), "/proc/%d/smaps", pid);

	priv = 0;
	skip_read = false;

	f = fopen(path, "r");
	if (!f)
		return -1;

	while (true) {
		size_t len;
		float m;
		bool is_heap = false;
		char *ok;
		char line[LINE_BUF_SIZE];

		if (skip_read)
			ok = line;
		else
			ok = fgets(line, LINE_BUF_SIZE, f);
		if (!ok)
			break;
		skip_read = false;

		// first line is VMA info - if not anonymous, the name
		// of the file/section is at offset OFF_NAME (73)
		len = strlen(line);
		if (len > OFF_NAME)
			is_heap = strncmp(&line[OFF_NAME], "[heap]", 6) == 0;
		if (!len)
			break;

		len = fread(line, 1, SMAP_DETAILS_LEN, f);
		if (len != SMAP_DETAILS_LEN)
			die("couldn't read details (%zu != %zu) - out of sync?",
			    len, SMAP_DETAILS_LEN);
		line[SMAP_DETAILS_LEN] = '\0';

		// Pss - line 3
		m = atoi(&line[2*MAP_DETAIL_LEN+MAP_DETAIL_OFF]);
		mi->pss += m + PSS_ADJUST;
		// we don't need PSS_ADJUST for heap because
		// the heap is private and anonymous.
		if (is_heap)
			mi->heap += m;

		// Private_Clean & Private_Dirty are lines 6 and 7
		priv += atoi(&line[5*MAP_DETAIL_LEN+MAP_DETAIL_OFF]);
		priv += atoi(&line[6*MAP_DETAIL_LEN+MAP_DETAIL_OFF]);

		mi->swap += atoi(&line[10*MAP_DETAIL_LEN+MAP_DETAIL_OFF]);

		// after the constant-sized smap details, there is an
		// optional Nonlinear line, followed by the final
		// VmFlags line.
		while (true) {
			ok = fgets(line, LINE_BUF_SIZE, f);
			if (!ok) {
				goto end;
			} else if (strncmp(line, TY_VM_FLAGS, LEN_VM_FLAGS) == 0) {
				break;
			} else if (strlen(line) > MAP_DETAIL_LEN) {
				// older kernels don't have VmFlags,
				// but can have Nonlinear.  If we have
				// a line longer than a detail line it
				// is the VMA info and we should skip
				// reading the VMA info at the start
				// of the next loop iteration.
				skip_read = true;
				break;
			}
		}
	}
end:
	mi->shared = mi->pss - priv;

	fclose(f);
	return 0;
}

ssize_t
list_pids(int **result)
{
	DIR *proc;
	ssize_t count, n;
	struct dirent *de;
	int *pids;

	pids = NULL;
	count = 0;
	n = 0;

	proc = opendir("/proc");
	if (!proc)
		return -1;

	// we loop through the dirents twice, so that we can count
	// them the first time through and allocate a minimally-sized
	// buffer to store the pids in.
	while ((de = readdir(proc))) {
		if (isdigit(de->d_name[0]))
			count++;
	}
	rewinddir(proc);

	pids = calloc(count+1, sizeof(int));
	if (!pids) {
		closedir(proc);
		return -1;
	}

	while ((de = readdir(proc))) {
		if (!isdigit(de->d_name[0]))
			continue;
		// between the original readdir and now, the contents
		// of /proc could have changed.  If there are
		// additional processes, make sure we don't overwrite
		// our buffer.
		if (count-- == 0)
			break;
		pids[n++] = atoi(de->d_name);
	}

	closedir(proc);

	*result = pids;
	return n;
}

int
cmp_cmdinfop_name(const void *a_in, const void *b_in)
{
	const CmdInfo *a = *(CmdInfo *const *)a_in;
	const CmdInfo *b = *(CmdInfo *const *)b_in;
	return strcmp(a->name, b->name);
}

int
cmp_cmdinfop_pss(const void *a_in, const void *b_in)
{
	const CmdInfo *a = *(CmdInfo *const *)a_in;
	const CmdInfo *b = *(CmdInfo *const *)b_in;
	if (a->pss < b->pss)
		return -1;
	else if (a->pss > b->pss)
		return 1;
	return strcmp(a->name, b->name);
}

void
print_results(CmdInfo **cmds, size_t count, bool show_heap, bool quiet, char *filter)
{
	float tot_pss, tot_swap;
	const char *tot_fmt;

	tot_pss = 0;
	tot_swap = 0;

	if (show_heap) {
		tot_fmt = "#%9.1f%30.1f\tTOTAL USED BY PROCESSES\n";
		if (!quiet)
			printf("%10s%10s%10s%10s\t%s\n", "MB RAM", "SHARED",
			       "HEAP", "SWAPPED", "PROCESS (COUNT)");
	} else {
		tot_fmt = "#%9.1f%20.1f\tTOTAL USED BY PROCESSES\n";
		if (!quiet)
			printf("%10s%10s%10s\t%s\n", "MB RAM", "SHARED",
			       "SWAPPED", "PROCESS (COUNT)");
	}

	for (size_t i = 0; i < count; i++) {
		char sbuf[16];
		CmdInfo *c = cmds[i];
		char *n = c->name;
		float pss;

		if (filter && !strstr(n, filter))
			continue;

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

	if (!quiet)
		printf(tot_fmt, tot_pss, tot_swap);
	fflush(stdout);
}

void
usage(void)
{
	die("Usage: %s [OPTION...]\n" \
	    "Simple, accurate RAM and swap reporting.\n\n" \
	    "Options:\n" \
	    "\t-q\tquiet - supress column header + total footer\n" \
	    "\t-heap\tshow heap column\n" \
	    "\t-filter=''\tsimple string to test process names against\n",
	    argv0);
}

int
main(int argc, char *const argv[])
{
	int n, nuniq, next;
	ssize_t proc_count;
	int *pids;
	CmdInfo **cmds, **cmd_sums;
	char *filter;
	bool show_heap, quiet;

	show_heap = false;
	quiet = false;
	filter = NULL;

	for (argv0 = argv[0], argv++, argc--; argc > 0; argv++, argc--) {
		char const* arg = argv[0];
		if (strcmp("-help", arg) == 0) {
			usage();
		} else if (strcmp("-q", arg) == 0) {
			quiet = true;
		} else if (strcmp("-heap", arg) == 0) {
			show_heap = true;
		} else if (strncmp("-filter=", arg, 8) == 0) {
			filter = strdup(&arg[8]);
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

	proc_count = list_pids(&pids);
	if (proc_count <= 0)
		die("list_pids failed\n");

	cmds = calloc(sizeof(CmdInfo*), proc_count);
	n = 0;
	for (int *pid = pids; *pid; pid++) {
		CmdInfo *mi = cmdinfo_new(*pid);
		if (mi)
			cmds[n++] = mi;
	}
	free(pids);
	pids = NULL;

	// n is potentially smaller than pids.count, so free any
	// unused space
	cmds = realloc(cmds, n*sizeof(CmdInfo*));

	qsort(cmds, n, sizeof(CmdInfo*), cmp_cmdinfop_name);

	nuniq = 0;
	for (int i = 0; i < n; i++) {
		if (i == 0 || strcmp(cmds[i-1]->name, cmds[i]->name) != 0)
			nuniq++;
	}

	cmd_sums = calloc(sizeof(CmdInfo*), nuniq);
	next = 0;

	for (int i = 0; i < n; i++) {
		if (i == 0 || strcmp(cmds[i-1]->name, cmds[i]->name) != 0) {
			cmd_sums[next++] = cmds[i];
		} else {
			CmdInfo *curr = cmds[i];
			CmdInfo *sum = cmd_sums[next-1];
			sum->npids++;
			sum->pss += curr->pss;
			sum->shared += curr->shared;
			sum->heap += curr->heap;
			sum->swap += curr->swap;
		}
	}

	qsort(cmd_sums, nuniq, sizeof(CmdInfo*), cmp_cmdinfop_pss);

	print_results(cmd_sums, nuniq, show_heap, quiet, filter);

	free(cmd_sums);
	free(filter);

	for (int i = 0; i < n; i++)
		cmdinfo_free(cmds[i]);
	free(cmds);

	return 0;
}
