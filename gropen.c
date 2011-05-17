// neededd for RTLD_NEXT
#define _GNU_SOURCE

#include <stdio.h>
#include <stdarg.h>
#define _FCNTL_H
#include <bits/fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * Compile with:
 *
 *    gcc -nostartfiles -fpic -shared -o gropen.so gropen.c -ldl
 */

/* #define DEBUG 1 */

int (*next__open)(const char *, int, mode_t);
int (*nextopen)(const char *, int, mode_t);
int (*nextopen64)(const char *, int, mode_t);
int (*nextstat)(int ver, const char *, struct stat *);
int (*next__lxstat)(int ver, const char *, struct stat *);

char buffer[4096];
char tmpname[4096];
char *fetch_program = NULL;

void _init(void)
{
  int retval;
  const char *errval;

#ifdef DEBUG
  __write(2,"GotInit\n",8);
#endif

  next__open = dlsym(RTLD_NEXT,"__open");
  if ((errval = dlerror()) != NULL) {
    fprintf(stderr, "dlsym(__open): %s\n", errval);
  }

  nextopen = dlsym(RTLD_NEXT,"open");
  if ((errval = dlerror()) != NULL) {
    fprintf(stderr, "dlsym(open): %s\n", errval);
  }
  nextopen64 = dlsym(RTLD_NEXT,"open64");
  if ((errval = dlerror()) != NULL) {
    fprintf(stderr, "dlsym(open): %s\n", errval);
  }

  nextstat = dlsym(RTLD_NEXT,"__xstat");
  if ((errval = dlerror()) != NULL) {
    fprintf(stderr, "dlsym(__xstat): %s\n", errval);
  }

  next__lxstat = dlsym(RTLD_NEXT,"__lxstat");
  if ((errval = dlerror()) != NULL) {
    fprintf(stderr, "dlsym(__lxstat): %s\n", errval);
  }

  fetch_program=getenv("GFETCH_PROGRAM");
#ifdef DEBUG
  fprintf(stderr, "FP %s\n", fetch_program);
#endif
  if (fetch_program && !strstr(fetch_program, "%s"))
	  fetch_program=NULL;

#ifdef DEBUG
  fprintf(stderr, "EndInit %s\n", fetch_program);
#endif
}

int openhandler(const char *pathname, int flags, int mode, int stat)
{
	FILE *remote;
	int len = -1;
	pid_t pid;
	int pipes[2];
	int fd, tb, nb, rv;
	char *p;

#ifdef DEBUG
  printf("GotHandler %s, %x, %x\n", pathname, flags, mode);
#endif
	snprintf(buffer, sizeof(buffer), fetch_program, pathname);

	/* program i call should not, also, preload me */
	unsetenv("LD_PRELOAD");
	if (rv=system(buffer)) {
		fprintf(stderr, "Can't run fetch program: %s\n", buffer);
		return rv;
	}

#ifdef DEBUG
  fprintf(stderr, "FP OK %d\n", rv);
#endif
	fd = (*nextopen)(pathname, flags, mode);
	return fd;
}

int open(const char *pathname, int flags, mode_t mode)
{
#ifdef DEBUG
  fprintf(stderr, "GotOPEN: %s, %s\n", pathname, fetch_program);
#endif
	struct stat st; 
	if (fetch_program && ((flags & O_ACCMODE) != O_CREAT)) {
                stat(pathname, &st);
	}
	return (*nextopen)(pathname, flags, mode);
}

int open64(const char *pathname, int flags, mode_t mode)
{
#ifdef DEBUG
  fprintf(stderr, "GotOPEN 64: %s, %s\n", pathname, fetch_program);
#endif
        struct stat st;
        if (fetch_program && ((flags & O_ACCMODE) != O_CREAT)) {
                stat(pathname, &st);
        }
        return (*nextopen64)(pathname, flags, mode);
}

int __open(const char *pathname, int flags, int mode)
{
#ifdef DEBUG
  fprintf(stderr, "Got__OPEN\n", pathname);
#endif
	struct stat st; 
        if (fetch_program && ((flags & O_ACCMODE) != O_CREAT)) {
                stat(pathname, &st);
        }
        return (*nextopen)(pathname, flags, mode);
}


int __xstat(int ver, const char *pathname, struct stat *buf)
{
        int ret;
	ret=(*nextstat)(ver, pathname, buf);
	if (fetch_program && ret && errno == ENOENT) {
		openhandler(pathname, O_RDONLY, 00755, 1);
		ret=(*nextstat)(ver, pathname, buf);
	}
	return ret;
}

int __lxstat(int ver, const char *pathname, struct stat *buf)
{
        int ret;
	ret=(*nextstat)(ver, pathname, buf);
	if (fetch_program && ret && errno == ENOENT) {
		openhandler(pathname, O_RDONLY, 00755, 1);
		ret=(*nextstat)(ver, pathname, buf);
	}
	return ret;
}

