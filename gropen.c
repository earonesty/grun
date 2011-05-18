// neededd for RTLD_NEXT
#define _GNU_SOURCE

#include <stdio.h>
#include <stdarg.h>

#define _FCNTL_H
#include <bits/fcntl.h>
//#define _SYS_STAT_H
//#include <bits/stat.h>
#include <sys/stat.h>

#include <sys/types.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
#ifndef stat
	extern int stat (__const char *__restrict __file, struct stat *buf);
# else
	extern int stat64 (__const char *__restrict __file, struct stat64 *buf);
#endif
*/

/*
 * Compile with:
 *
 *    gcc -nostartfiles -fpic -shared -o gropen.so gropen.c -ldl
 */

/* #define DEBUG 1 */

static int (*next__open)(const char *, int, mode_t) = NULL;
static int (*nextopen)(const char *, int, mode_t) = NULL;
static int (*nextopen64)(const char *, int, mode_t) = NULL;
static int (*nextstat)(int ver, const char *, struct stat *) = NULL;
static int (*next__lxstat)(int ver, const char *, struct stat *) = NULL;
static int (*nextstat64)(int ver, const char *, struct stat64 *) = NULL;
static int (*next__lxstat64)(int ver, const char *, struct stat64 *) = NULL;

#define BUF_MAX 4096
static char buffer[BUF_MAX];
static char tmpname[4096];
static char *fetch_program = NULL;
static int did_init = 0;

void do_init(void);

void _init(void)
{
	if (did_init != 1) do_init();
}

struct tree {
	char *p;
	int n;
	struct tree *l;	
	struct tree *r;	
};

static struct tree *path_tree;

void tree_insert(struct tree **n, const char *p, int lp) {
	if (!*n) {
		*n = (struct tree *) malloc(sizeof(struct tree));
		(*n)->p = (char *) malloc(lp+1);
		strncpy((*n)->p, p, lp);
		(*n)->n=lp;
		(*n)->p[lp]='\0';
		(*n)->r = (*n)->l = NULL;
		//printf("ADD %s %d, n:%x l:%x r:%x\n", p, lp, *n, &(*n)->l, &(*n)->r);
	} else {
		int l = lp > (*n)->n ? (*n)->n : lp;
		int rv = strncmp(p, (*n)->p, l);
		if (rv > 0) {
			tree_insert(&(*n)->r, p, lp);
		} else if (rv < 0) {
			tree_insert(&(*n)->l, p, lp);
		} else {
			if ((*n)->n > lp) {
				(*n)->p[lp]='\0';
				(*n)->n=lp;
			}
		}
	}
}

int tree_search(struct tree *n, const char *p, int lp) {
        if (!n) return 0;
	int l = lp > n->n ? n->n : lp;
	int rv = strncmp(p, n->p, l);
	if (rv > 0) {
		return tree_search(n->r, p, lp);
	} else if (rv < 0) {
		return tree_search(n->l, p, lp);
	} else {
		return 1;
	}
}

void do_init(void)
{
  char *p, *e;
  next__open = dlsym(RTLD_NEXT,"__open");
  nextopen = dlsym(RTLD_NEXT,"open");
  nextopen64 = dlsym(RTLD_NEXT,"open64");
  nextstat = dlsym(RTLD_NEXT,"__xstat");
  next__lxstat = dlsym(RTLD_NEXT,"__lxstat");
  nextstat64 = dlsym(RTLD_NEXT,"__xstat64");
  next__lxstat64 = dlsym(RTLD_NEXT,"__lxstat64");

  fetch_program=getenv("GFETCH_PROGRAM");
  if (fetch_program && !strstr(fetch_program, "%s")) {
  	fprintf(stderr, "Warning: Invalid GFETCH_PROGRAM\n");
	fetch_program=NULL;
  }
  p=getenv("GFETCH_PATH");

#ifdef DEBUG
  fprintf(stderr, "GFETCH INIT %s, %s\n", p, fetch_program);
#endif

  e = strchr(p,':');
  while (e) {
	tree_insert(&path_tree, p, e-p);
	p = e+1;
  	e = strchr(p,':');
  }
  tree_insert(&path_tree, p, strlen(p));

//printf("HERE %d\n", tree_search(path_tree, "/tmp", 4));
  
  did_init = 1;
}

int openhandler(const char *pathname, int flags, int mode, int stat)
{
	FILE *remote;
	int len = -1;
	pid_t pid;
	int pipes[2];
	int fd, tb, nb, rv;
	char *p;

	if (*pathname == '/') {
		p = strrchr(pathname, '/');
		rv = tree_search(path_tree, pathname, p-pathname);
	} else {
		*buffer='\0';
		if (!getcwd(buffer, sizeof(buffer))) {
			buffer[BUF_MAX-1] ='\0';
		}
		rv = tree_search(path_tree, buffer, strlen(buffer));
	}
	if (!rv) return -1; 

#ifdef DEBUG
  printf("GotHandler %s, %x, %x\n", pathname, flags, mode);
#endif
	snprintf(buffer, sizeof(buffer), fetch_program, pathname);

	/* program i call should not, also, preload me */
	unsetenv("LD_PRELOAD");
	unsetenv("GFETCH_PROGRAM");
	if (rv=system(buffer)) {
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
	if (did_init != 1) do_init();

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
	if (did_init != 1) do_init();
#ifdef DEBUG
  fprintf(stderr, "GotOPEN 64: %d, %s, %s\n", did_init, pathname, fetch_program);
#endif
        struct stat st;
        if (fetch_program && ((flags & O_ACCMODE) != O_CREAT)) {
                stat(pathname, &st);
        }
	if (!nextopen64) {
		errno=EINVAL;
		return -1;
	}
        return (*nextopen64)(pathname, flags, mode);
}

int __open(const char *pathname, int flags, int mode)
{
	if (did_init != 1) do_init();
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
	if (did_init != 1) do_init();
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
	if (did_init != 1) do_init();
        int ret;
	ret=(*next__lxstat)(ver, pathname, buf);
	if (fetch_program && ret && errno == ENOENT) {
		openhandler(pathname, O_RDONLY, 00755, 1);
		ret=(*next__lxstat)(ver, pathname, buf);
	}
	return ret;
}

int __xstat64(int ver, const char *pathname, struct stat64 *buf)
{
	if (did_init != 1) do_init();
        int ret;
        ret=(*nextstat64)(ver, pathname, buf);
        if (fetch_program && ret && errno == ENOENT) {
                openhandler(pathname, O_RDONLY, 00755, 1);
        	ret=(*nextstat64)(ver, pathname, buf);
        }
        return ret;
}

int __lxstat64(int ver, const char *pathname, struct stat64 *buf)
{
	if (did_init != 1) do_init();
        int ret;
        ret=(*next__lxstat64)(ver, pathname, buf);
        if (fetch_program && ret && errno == ENOENT) {
                openhandler(pathname, O_RDONLY, 00755, 1);
        	ret=(*next__lxstat64)(ver, pathname, buf);
        }
        return ret;
}

