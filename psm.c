// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#define _GNU_SOURCE // asprintf
#include <stdio.h>   // for NULL, fprintf, snprintf, stderr, BUFSIZ, fclose, etc
#undef _GNU_SOURCE

#include <ctype.h>   // for isdigit
#include <dirent.h>  // for closedir, readdir, dirent, opendir, rewinddir, etc
#include <errno.h>   // for errno
#include <fcntl.h>   // for O_RDONLY
#include <libgen.h>  // for basename
#include <stdarg.h>  // for va_list
#include <stdbool.h> // for bool
#include <stdlib.h>  // for free, calloc, atoi, exit, malloc, realloc, etc
#include <string.h>  // for strcmp, strdup, strncmp, strncpy
#include <unistd.h>  // for ssize_t, geteuid

#include "config.h"

#define COMM_MAX 16
#define CMD_DISPLAY_MAX 32
#define PAGE_SIZE 4096
// from ps_mem - average error due to truncation in the kernel pss
// calculations
#define PSS_ADJUST       .5
#define MAP_DETAIL_LEN   sizeof("Size:                  4 kB")-1
#define MAP_DETAIL_OFF   16

#define LINE_BUF_SIZE    512

#define TY_NONLINEAR     "Nonlinear:"
#define LEN_NONLINEAR    sizeof(TY_NONLINEAR)-1
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

static char *_readlink(char *);

static CmdInfo *cmdinfo_new(int, size_t, char *);
static void cmdinfo_free(CmdInfo *);

static int smap_read_int(char *, int);
static size_t smap_details_len(void);

static int proc_name(CmdInfo *, int);
static int proc_mem(CmdInfo *, int, size_t, char *);
static int proc_cmdline(int, char *buf, size_t len);

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
/// malloc'ed buffer that the caller now owns, containing the
/// null-terminated contents of the symbolic link.
char *
_readlink(char *path)
{
	ssize_t n;
	char *b;

	for (int len=64;; len*=2) {
		b = malloc(len);
		if (!b)
			return NULL;

		n = readlink(path, b, len);
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
cmdinfo_new(int pid, size_t details_len, char *details_buf)
{
	int err;
	CmdInfo *ci;

	ci = calloc(sizeof(CmdInfo), 1);
	if (!ci)
		goto error;
	ci->npids = 1;

	err = proc_name(ci, pid);
	if (err)
		goto error;

	err = proc_mem(ci, pid, details_len, details_buf);
	if (err)
		goto error;

	return ci;
error:
	cmdinfo_free(ci);
	return NULL;
}

void
cmdinfo_free(CmdInfo *ci)
{
	if (ci && ci->name)
		free(ci->name);
	free(ci);
}

int
smap_read_int(char *chunk, int line)
{
	// line is 1-indexed, but offsets into the smap are 0-index.
	return atoi(&chunk[(line-1)*MAP_DETAIL_LEN+MAP_DETAIL_OFF]);
}

int
proc_name(CmdInfo *ci, int pid)
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

	// XXX: I thnk this is wrong.  a) we can use strncmp rather
	// than the copy.  b) >= doesn/t make sense, it should be !=.
	if (strcmp(p, shortbuf) >= 0)
		ci->name = strdup(basename(p));
	else
		ci->name = strdup(buf);
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


size_t
smap_details_len(void)
{
	size_t len = 0, result = 0;
	FILE *f = NULL;
	char *ok;
	char line[LINE_BUF_SIZE];

	f = fopen("/proc/self/smaps", "r");
	if (!f)
		return 0;

	// skip first line, which is the name + address of the mapping
	ok = fgets(line, LINE_BUF_SIZE, f);
	if (!ok)
		goto out;

	while (true) {
		ok = fgets(line, LINE_BUF_SIZE, f);
		if (!ok)
			goto out;
		else if (strncmp(line, TY_NONLINEAR, LEN_NONLINEAR) == 0)
			break;
		else if (strncmp(line, TY_VM_FLAGS, LEN_VM_FLAGS) == 0)
			break;
		if (strlen(line) != MAP_DETAIL_LEN+1)
			die("unexpected line len %zu != %zu",
			    strlen(line), MAP_DETAIL_LEN+1);
		len += MAP_DETAIL_LEN+1;
	}
	result = len;

out:
	fclose(f);
	return result;
}

int
proc_mem(CmdInfo *ci, int pid, size_t details_len, char *details_buf)
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
		char *line = details_buf;

		if (skip_read)
			ok = line;
		else
			ok = fgets(line, details_len+1, f);
		skip_read = false;

		if (!ok)
			break;

		// first line is VMA info - if not anonymous, the name
		// of the file/section is at offset OFF_NAME (73)
		len = strlen(line);
		if (len > OFF_NAME)
			is_heap = strncmp(&line[OFF_NAME], "[heap]", 6) == 0;
		if (!len)
			break;

		//memset(line, 0, details_len+1);
		len = fread(line, 1, details_len, f);
		if (len != details_len)
			die("couldn't read details of %d (%zu != %zu) - out of sync?:\n%s",
			    pid, len, details_len, line);
		line[details_len] = '\0';

		// Pss - line 3
		m = smap_read_int(line, 5);
		ci->pss += m + PSS_ADJUST;
		// we don't need PSS_ADJUST for heap because
		// the heap is private and anonymous.
		if (is_heap)
			ci->heap += m;

		// Private_Clean & Private_Dirty are lines 6 and 7
		priv += smap_read_int(line, 8);
		priv += smap_read_int(line, 9);

		ci->swap += smap_read_int(line, 11);

		// after the constant-sized smap details, there is an
		// optional Nonlinear line, followed by the final
		// VmFlags line.
		while (true) {
			ok = fgets(line, details_len, f);
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
	ci->shared = ci->pss - priv;

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
	size_t details_len;
	ssize_t proc_count;
	int *pids;
	CmdInfo *ci, **cmds, **cmd_sums;
	char *filter, *details_buf;
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

	/* if (geteuid() != 0) { */
	/* 	die("%s requires root privileges. (try 'sudo `which %s`)\n", */
	/* 	    argv0, argv0); */
	/* } */

	details_len = smap_details_len();

	proc_count = list_pids(&pids);
	if (proc_count <= 0)
		die("list_pids failed\n");

	cmds = calloc(sizeof(CmdInfo*), proc_count);
	if (!cmds)
		die("calloc cmds failed\n");

	details_buf = malloc(details_len+1);
	if (!details_buf)
		die("malloc(details_len+1 failed\n");
	details_buf[details_len] = 0;

	n = 0;
	for (int *pid = pids; *pid; pid++) {
		ci = cmdinfo_new(*pid, details_len, details_buf);
		if (ci)
			cmds[n++] = ci;
	}
	free(details_buf);
	free(pids);
	pids = NULL;

	// n is potentially smaller than pids.count, so free any
	// unused space (no CmdInfo is available for kernel threads or
	// processes that have exited in between listing PIDs and
	// reading their proc entries).  This will never increase the
	// size of cmds.
	if (n)
		cmds = realloc(cmds, n*sizeof(CmdInfo*));

	qsort(cmds, n, sizeof(CmdInfo*), cmp_cmdinfop_name);

	nuniq = 0;
	for (int i = 0; i < n; i++) {
		if (i == 0 || strcmp(cmds[i-1]->name, cmds[i]->name) != 0)
			nuniq++;
	}

	cmd_sums = NULL;

	if (nuniq)
		cmd_sums = calloc(sizeof(CmdInfo*), nuniq);

	if (!cmd_sums)
		die("calloc cmd_sums failed\n");

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

/*

- the structure of /proc dir

- the structure/format of /proc/$pid/smaps

*/
