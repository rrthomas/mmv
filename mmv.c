/*
	mmv

	Copyright (c) 2021-2024 Reuben Thomas.
	Copyright (c) 1990 Vladimir Lanin.

	This program is distributed under the GNU GPL version 3, or, at your
	option, any later version.

	Maintainer: Reuben Thomas <rrt@sc3d.org>

	The original author of mmv was Vladimir Lanin <lanin@csd2.nyu.edu>.

	Many thanks to those who have to contributed to the design
	and/or coding of this program:

	Tom Albrecht:	initial Sys V adaptation, consultation, and testing
	Carl Mascott:	V7 adaptation
	Mark Lewis:	-n flag idea, consultation.
	Dave Bernhold:	upper/lowercase conversion idea.
	Paul Stodghill:	copy option, argv[0] checking.
	Frank Fiamingo:	consultation and testing.
	Tom Jordahl:	bug reports and testing.
	John Lukas, Hugh Redelmeyer, Barry Nelson, John Sauter,
	Phil Dench, John Nelson:
			bug reports.
*/

#include "config.h"

/* NAME_MAX is not guaranteed to exist and its value should really be
 * obtained with pathconf(), but that doesn't exist on mingw */
#ifdef _WIN32
#define _POSIX_ /* For NAME_MAX in limits.h on mingw */
#endif

#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <utime.h>
#include <dirent.h>
#include <limits.h>

#include "progname.h"
#include "binary-io.h"
#include "dirname.h"
#include "pathmax.h"
#include "xalloc.h"

#include "cmdline.h"


#ifndef PATH_MAX
#define PATH_MAX (pathconf("/", _PC_PATH_MAX))
#endif

#define ESC '\\'
#define SLASH '/'

#define STRLEN(s) (sizeof(s) - 1)


#define NORMCOPY 0x002
#define OVERWRITE 0x004
#define NORMMOVE 0x008
#define XMOVE 0x010
#define APPEND 0x040
#define HARDLINK 0x100
#define SYMLINK 0x200

#define COPY (NORMCOPY | OVERWRITE)
#define MOVE (NORMMOVE | XMOVE)
#define LINK (HARDLINK | SYMLINK)

static char COPYNAME[] = "mcp";
static char APPENDNAME[] = "mad";
static char LINKNAME[] = "mln";

#define ASKDEL 0
#define ALLDEL 1
#define NODEL 2

#define ASKBAD 0
#define SKIPBAD 1
#define ABORTBAD 2

#define STAY 0
#define LOWER 1
#define UPPER 2
#define CAPITALIZE 3

#define MAXWILD 20
#define MAXPATLEN PATH_MAX
#define INITROOM 10

#define FI_STTAKEN 0x01
#define FI_LINKERR 0x02
#define FI_INSTICKY 0x04
#define FI_NODEL 0x08
#define FI_KNOWWRITE 0x010
#define FI_CANWRITE 0x20
#define FI_ISDIR 0x40
#define FI_ISLNK 0x80

typedef struct {
	char *fi_name;
	struct rep *fi_rep;
	mode_t fi_mode;
	int fi_stflags;
} FILEINFO;

#define DI_KNOWWRITE 0x01
#define DI_CANWRITE 0x02
#define DI_NONEXISTENT 0x04

typedef struct {
	dev_t di_vid;
	ino_t di_did;
	size_t di_nfils;
	FILEINFO **di_fils;
	char di_flags;
	const char *di_path; /* Only set when DI_NONEXISTENT is set */
} DIRINFO;

#define H_NODIR 1
#define H_NOREADDIR 2

typedef struct {
	char *h_name;
	DIRINFO *h_di;
	char h_err;
} HANDLE;

#define R_SKIP 0x02
#define R_DELOK 0x04
#define R_ISALIASED 0x08
#define R_ISCYCLE 0x10
#define R_ONEDIRLINK 0x20

typedef struct rep {
	HANDLE *r_hfrom;
	FILEINFO *r_ffrom;
	HANDLE *r_hto;
	char *r_nto;			/* non-path part of new name */
	FILEINFO *r_fdel;
	struct rep *r_first;
	struct rep *r_thendo;
	struct rep *r_next;
	int r_flags;
} REP;

typedef struct {
	REP *rd_p;
	DIRINFO *rd_dto;
	char *rd_nto;
	unsigned rd_i;
} REPDICT;


static int op, badstyle, delstyle, verbose, noex, matchall, mkdirs;

static size_t ndirs = 0, dirroom;
static size_t ndirs_nonexistent = 0, dirroom_nonexistent;
static DIRINFO **dirs, **dirs_nonexistent;
static size_t nhandles = 0, handleroom;
static HANDLE **handles;
static unsigned nreps = 0;
static REP hrep, *lastrep = &hrep;

static int badreps = 0, paterr = 0, direrr, failed = 0, gotsig = 0, repbad;

static char TEMP[] = "$$mmvtmp.";
static char TOOLONG[] = "(too long)";
static char EMPTY[] = "(empty)";

static char SLASHSTR[] = {SLASH, '\0'};

#define PATLONG "%.40s... : pattern too long.\n"

char from[MAXPATLEN], to[MAXPATLEN];
static size_t fromlen, tolen;
static char *(stagel[MAXWILD]), *(firstwild[MAXWILD]), *(stager[MAXWILD]);
static int nwilds[MAXWILD];
static int nstages;
char pathbuf[PATH_MAX];
char fullrep[PATH_MAX + 1];
static char *(start[MAXWILD]);
static size_t length[MAXWILD];
static REP mistake;
#define MISTAKE (&mistake)


static const char *home;
static size_t homelen;
static uid_t uid;
static mode_t oldumask;
static ino_t cwdd = (ino_t)-1L;
static dev_t cwdv = (dev_t)-1L;


static void quit(void)
{
	fprintf(stderr, "Aborting, nothing done.\n");
	exit(1);
}

static void breakout(int signum _GL_UNUSED)
{
	fflush(stdout);
	quit();
}

static void breakrep(int signum _GL_UNUSED)
{
	gotsig = 1;
}

static void breakstat(int signum _GL_UNUSED)
{
	_exit(1);
}

static void printchain(REP *p)
{
	if (p->r_thendo != NULL)
		printchain(p->r_thendo);
	printf("%s%s -> ", p->r_hfrom->h_name, p->r_ffrom->fi_name);
	badreps++;
	nreps--;
	p->r_ffrom->fi_rep = MISTAKE;
}

static void nochains(void)
{
	for (REP *q = &hrep, *p = q->r_next; p != NULL; q = p, p = p->r_next)
		if (p->r_flags & R_ISCYCLE || p->r_thendo != NULL) {
			printchain(p);
			printf("%s%s : no chain copies allowed.\n",
				p->r_hto->h_name, p->r_nto);
			q->r_next = p->r_next;
			p = q;
		}
}

static bool getreply(void)
{
	static FILE *tty = NULL;
	if (tty == NULL && (tty = fopen("/dev/tty", "r")) == NULL) {
		fprintf(stderr, "Cannot open terminal to get reply.\n");
		quit();
	}

	/* Test against "^[yY]", hardcoded to avoid requiring getline,
	   regex, and rpmatch.  */
	int c = getchar();
	if (c == EOF) {
		fprintf(stderr, "Cannot get reply.\n");
		quit();
	}
	bool yes = (c == 'y' || c == 'Y');
	while (c != '\n' && c != EOF)
		c = getchar();
	return yes;
}

static void goonordie(void)
{
	if ((paterr || badreps) && nreps > 0) {
		fprintf(stderr, "Not everything specified can be done.");
		if (badstyle == ABORTBAD) {
			fprintf(stderr, " Aborting.\n");
			exit(1);
		}
		else if (badstyle == SKIPBAD)
			fprintf(stderr, " Proceeding with the rest.\n");
		else {
			fprintf(stderr, " Proceed with the rest? ");
			if (!getreply())
				exit(1);
		}
	}
}

static int trymatch(FILEINFO *ffrom, char *pat)
{
	char *p;

	if (ffrom->fi_rep != NULL)
		return(0);

	p = ffrom->fi_name;

	if (*p == '.') {
		if (p[1] == '\0' || (p[1] == '.' && p[2] == '\0'))
			return(strcmp(pat, p) == 0);
		else if (!matchall && *pat != '.')
			return(0);
	}
	return(-1);
}

static int match(char *pat, char *s, char **start1, size_t *len1)
{
	char c;

	*start1 = 0;
	for(;;)
		switch (c = *pat) {
		case '\0':
		case SLASH:
			return(*s == '\0');
		case '*':
			*start1 = s;
			if ((c = *(++pat)) == '\0') {
				*len1 = strlen(s);
				return(1);
			}
			else {
				for (*len1=0; !match(pat, s, start1+1, len1+1); (*len1)++, s++)
					if (
						*s == '\0'
					)
						return(0);
				return(1);
			}
		case '?':
			if (
				*s == '\0'
			)
				return(0);
			*(start1++) = s;
			*(len1++) = 1;
			pat++;
			s++;
			break;
		case '[':
			{
				int matched = 0, notin = 0, inrange = 0;
				char prevc = '\0';

				if ((c = *(++pat)) == '^') {
					notin = 1;
					c = *(++pat);
				}
				while (c != ']') {
					if (c == '-' && !inrange)
						inrange = 1;
					else {
						if (c == ESC) {
							c = *(++pat);
						}
						if (inrange) {
							if (*s >= prevc && *s <= c)
								matched = 1;
							inrange = 0;
						}
						else if (c == *s)
							matched = 1;
						prevc = c;
					}
					c = *(++pat);
				}
				if (inrange && *s >= prevc)
					matched = 1;
				if (!(matched ^ notin))
					return(0);
				*(start1++) = s;
				*(len1++) = 1;
				pat++;
				s++;
			}
			break;
		case ESC:
			c = *(++pat);
			/* FALLTHROUGH */
		default:
			if (c == *s) {
				pat++;
				s++;
			}
			else
				return(0);
		}
}

static int getstat(char *ffull, FILEINFO *f)
{
	struct stat fstat;
	int flags;

	if ((flags = f->fi_stflags) & FI_STTAKEN)
		return(flags & FI_LINKERR);
	flags |= FI_STTAKEN;
	if (lstat(ffull, &fstat)) {
		fprintf(stderr, "Strange, couldn't lstat %s.\n", ffull);
		quit();
	}
	if ((flags & FI_INSTICKY) && fstat.st_uid != uid && uid != 0)
		flags |= FI_NODEL;
#ifdef S_IFLNK
	if ((fstat.st_mode & S_IFMT) == S_IFLNK) {
		flags |= FI_ISLNK;
		if (stat(ffull, &fstat)) {
			f->fi_stflags = flags | FI_LINKERR;
			return(1);
		}
	}
#endif
	if ((fstat.st_mode & S_IFMT) == S_IFDIR)
		flags |= FI_ISDIR;
	f->fi_stflags = flags;
	f->fi_mode = fstat.st_mode;
	return(0);
}

static int keepmatch(FILEINFO *ffrom, char *pathend, size_t *pk, int needslash, int fils)
{
	*pk = strlen(ffrom->fi_name);
	if ((size_t)(pathend - pathbuf) + *pk + (size_t)needslash >= PATH_MAX) {
		*pathend = '\0';
		printf("%s -> %s : search path %s%s too long.\n",
			from, to, pathbuf, ffrom->fi_name);
		paterr = 1;
		return(0);
	}
	strcpy(pathend, ffrom->fi_name);
	getstat(pathbuf, ffrom);
	if (!(ffrom->fi_stflags & FI_ISDIR) && !fils) {
		if (verbose)
			printf("ignoring file %s\n", ffrom->fi_name);
		return(0);
	}

	if (needslash) {
		strcpy(pathend + *pk, SLASHSTR);
		(*pk)++;
	}
	return(1);
}

static char *getpath(char *tpath)
{
	char *pathstart, *pathend, c;

	pathstart = fullrep;
	pathend = pathstart + strlen(pathstart) - 1;
	while (pathend >= pathstart && *pathend != SLASH)
		--pathend;
	pathend++;

	c = *pathend;
	*pathend = '\0';
	strcpy(tpath, fullrep);
	*pathend = c;
	return(pathend);
}

static int fcmp(const void *pf1, const void *pf2)
{
	return(strcmp((*(FILEINFO **)pf1)->fi_name, (*(FILEINFO **)pf2)->fi_name));
}

static FILEINFO *fsearch(char *s, DIRINFO *d)
{
	FILEINFO *f = (FILEINFO *)xmalloc(sizeof(FILEINFO));
	f->fi_name = s;
	FILEINFO **res = bsearch(&f, d->di_fils, d->di_nfils, sizeof(FILEINFO *), fcmp);
	return res != NULL ? *res : NULL;
}

static _GL_ATTRIBUTE_PURE size_t ffirst(char *s, size_t n, DIRINFO *d)
{
	FILEINFO **fils = d->di_fils;
	size_t nfils = d->di_nfils;

	if (nfils == 0 || n == 0)
		return(0);
	size_t first = 0;
	size_t last = nfils - 1;
	for(;;) {
		size_t k = (first + last) / 2;
		int res = strncmp(s, fils[k]->fi_name, n);
		if (first == last)
			return(res == 0 ? k : nfils);
		else if (res > 0)
			first = k + 1;
		else
			last = k;
	}
}

#define IRWXMASK (S_IRUSR | S_IWUSR | S_IXUSR)
#define RWXMASK (IRWXMASK | (IRWXMASK >> 3) | (IRWXMASK >> 6))

/* This function adapted from bash's mkdir.c
   Make all the directories leading up to PATH, then create PATH.  Note that
   this changes the process's umask; make sure that all paths leading to a
   return reset it to OLDUMASK */
static int make_path(char *path)
{
	struct stat sb;
	char *p, *npath;
	int tail;

	/* If we don't have to do any work, don't do any work. */
	if (stat(path, &sb) == 0) {
		if (S_ISDIR(sb.st_mode) == 0) {
			if (verbose)
				printf("`%s': file exists but is not a directory", path);
			return(1);
		}

		return(0);
	}

	npath = xstrdup(path);	/* So we can write to it. */

	/* Check whether or not we need to do anything with intermediate dirs. */

	/* Skip leading slashes. */
	p = npath;
	while (*p == '/')
		p++;

	tail = 0;
	while (tail == 0) {
		if (*p == '\0')
			tail = 1;
		else
			p = strchr(p, '/');
		if (p)
			*p = '\0';
		else
			tail = 1;
		if (mkdir(npath, ~oldumask & RWXMASK) < 0) {
			/* "Each dir operand that names an existing directory shall be
			   ignored without error." */
			if (errno == EEXIST || errno == EISDIR) {
				int e = errno;
				int fail = 0;

				if (stat(npath, &sb) != 0) {
					fail = 1;
					if (verbose)
						printf("cannot create directory `%s': %s", npath, strerror(e));
				}
				else if (e == EEXIST && S_ISDIR(sb.st_mode) == 0) {
					fail = 1;
					if (verbose)
						printf("`%s': file exists but is not a directory", npath);
				}
				if (fail) {
					umask(oldumask);
					return(1);
				}
			}
			else {
				if (verbose)
					printf("cannot create directory `%s': %s", npath, strerror(errno));
				umask(oldumask);
				return(1);
			}
		}
		if (tail == 0)
			*p++ = '/';	/* restore slash */
		while (p && *p == '/')	/* skip consecutive slashes or trailing slash */
			p++;
	}

	umask(oldumask);
	return(0);
}

/* Create a non-existent directory and update its DIRINFO. */
static int make_directory(HANDLE *h) {
	int res = make_path(h->h_name);
	if (res != 0)
		fprintf(stderr, "Strange, couldn't create directory %s.\n",  h->h_name);
	else {
		struct stat dstat;
		if (stat(h->h_name, &dstat) || (dstat.st_mode & S_IFMT) != S_IFDIR) {
			fprintf(stderr, "Strange, couldn't stat new directory %s.\n", h->h_name);
			res = -1;
		}
		h->h_di->di_vid = dstat.st_dev;
		h->h_di->di_did = dstat.st_ino;
	}
	return res;
}

static HANDLE *hadd(char *n)
{
	if (nhandles == handleroom)
		handles = (HANDLE **)x2nrealloc(handles, &handleroom, sizeof(HANDLE *));
	HANDLE *h = (HANDLE *)xmalloc(sizeof(HANDLE));
	h->h_name = xcharalloc(strlen(n) + 1);
	strcpy(h->h_name, n);
	h->h_di = NULL;
	handles[nhandles++] = h;
	return(h);
}

static int hsearch(char *n, HANDLE **pret)
{
	for (unsigned i = 0; i < nhandles; i++)
		if (strcmp(n, handles[i]->h_name) == 0) {
			*pret = handles[i];
			return(1);
		}

	*pret = hadd(n);
	return(0);
}

static DIRINFO *dadd(dev_t v, ino_t d)
{
	if (ndirs == dirroom)
		dirs = (DIRINFO **)x2nrealloc(dirs, &dirroom, sizeof(DIRINFO *));
	DIRINFO *di = (DIRINFO *)xmalloc(sizeof(DIRINFO));
	di->di_vid = v;
	di->di_did = d;
	di->di_nfils = 0;
	di->di_fils = NULL;
	di->di_flags = 0;
	di->di_path = NULL;
	dirs[ndirs++] = di;
	return(di);
}

static DIRINFO *dadd_nonexistent(const char *dir)
{
	if (ndirs_nonexistent == dirroom_nonexistent)
		dirs_nonexistent = (DIRINFO **)x2nrealloc(dirs_nonexistent, &dirroom_nonexistent, sizeof(DIRINFO *));
	DIRINFO *di = (DIRINFO *)xmalloc(sizeof(DIRINFO));
	di->di_vid = (dev_t)-1;
	di->di_did = (ino_t)-1;
	di->di_nfils = 0;
	di->di_fils = NULL;
	di->di_flags = DI_KNOWWRITE | DI_CANWRITE | DI_NONEXISTENT;
	di->di_path = xstrdup(dir);
	dirs_nonexistent[ndirs_nonexistent++] = di;
	return(di);
}

static _GL_ATTRIBUTE_PURE DIRINFO *dsearch(dev_t v, ino_t d)
{
	for (unsigned i = 0; i < ndirs; i++)
		if (v == dirs[i]->di_vid && d == dirs[i]->di_did)
			return(dirs[i]);
	return(NULL);
}

static _GL_ATTRIBUTE_PURE DIRINFO *dsearch_nonexistent(const char *dir)
{
	for (unsigned i = 0; i < ndirs_nonexistent; i++)
		if (strcmp(dirs_nonexistent[i]->di_path, dir) == 0)
			return(dirs_nonexistent[i]);
	return(NULL);
}

static void takedir(const char *p, DIRINFO *di, int sticky)
{
	struct dirent *dp;
	FILEINFO *f, **fils;
	DIR *dirp;

	if ((dirp = opendir(p)) == NULL) {
		fprintf(stderr, "Strange, can't scan %s.\n", p);
		quit();
	}
	size_t room = INITROOM;
	di->di_fils = fils = (FILEINFO **)xmalloc(room * sizeof(FILEINFO *));
	size_t cnt = 0;
	while ((dp = readdir(dirp)) != NULL) {
		if (cnt == room) {
			di->di_fils = (FILEINFO **)x2nrealloc(di->di_fils, &room, sizeof(FILEINFO *));
			fils = di->di_fils + cnt;
		}
		*fils = f = (FILEINFO *)xmalloc(sizeof(FILEINFO));
		f->fi_name = xstrdup(dp->d_name);
		f->fi_stflags = sticky;
		f->fi_rep = NULL;
		cnt++;
		fils++;
	}
	closedir(dirp);
	qsort(di->di_fils, cnt, sizeof(FILEINFO *), fcmp);
	di->di_nfils = cnt;
}

static HANDLE *checkdir(char *p, char *pathend, int makedirs)
{
	struct stat dstat;
	ino_t d;
	dev_t v;
	DIRINFO *di = NULL;
	const char *myp;
	char *lastslash = NULL;
	int sticky;
	HANDLE *h;

	if (hsearch(p, &h)) {
		if (h->h_di == NULL) {
			direrr = h->h_err;
			return(NULL);
		}
		return(h);
	}

	if (*p == '\0')
		myp = ".";
	else if (pathend == p + 1)
		myp = SLASHSTR;
	else {
		lastslash = pathend - 1;
		*lastslash = '\0';
		myp = p;
	}

	if (stat(myp, &dstat) || (dstat.st_mode & S_IFMT) != S_IFDIR) {
		if (makedirs) {
			if ((di = dsearch_nonexistent(myp)) == NULL)
				di = dadd_nonexistent(myp);
		} else
			direrr = h->h_err = H_NODIR;
	} else if (access(myp, R_OK | X_OK))
		direrr = h->h_err = H_NOREADDIR;
	else {
		direrr = 0;
		sticky = (dstat.st_mode & S_ISVTX) && uid != 0 && uid != dstat.st_uid ?
			FI_INSTICKY : 0;
		v = dstat.st_dev;
		d = dstat.st_ino;

		if ((di = dsearch(v, d)) == NULL)
			takedir(myp, di = dadd(v, d), sticky);
	}

	if (lastslash != NULL)
		*lastslash = SLASH;
	if (direrr != 0)
		return(NULL);
	h->h_di = di;
	return(h);
}

static int checkto(char *f, HANDLE **phto, char **pnto, FILEINFO **pfdel)
{
	char tpath[PATH_MAX + 1];
	FILEINFO *fdel = NULL;

	char *pathend = getpath(tpath);
	size_t hlen = (size_t)(pathend - fullrep);
	*phto = checkdir(tpath, tpath + hlen, mkdirs);
	if (
	    *phto != NULL &&
	    *pathend != '\0' &&
	    (fdel = *pfdel = fsearch(pathend, (*phto)->h_di)) != NULL &&
	    (getstat(fullrep, fdel), fdel->fi_stflags & FI_ISDIR) &&
	    (strcmp(pathend, fullrep) != 0)
	    ) {
		size_t tlen = strlen(pathend);
		strcpy(pathend + tlen, SLASHSTR);
		tlen++;
		strcpy(tpath + hlen, pathend);
		pathend += tlen;
		hlen += tlen;
		*phto = checkdir(tpath, tpath + hlen, mkdirs);
	}

	if (*pathend == '\0') {
		*pnto = xstrdup(f);
		if ((size_t)(pathend - fullrep) + strlen(f) >= PATH_MAX) {
			strcpy(fullrep, TOOLONG);
			return(-1);
		}
		strcat(pathend, f);
		if (*phto != NULL) {
			fdel = *pfdel = fsearch(f, (*phto)->h_di);
			if (fdel != NULL)
				getstat(fullrep, fdel);
		}
	}
	else if (fdel != NULL)
		*pnto = fdel->fi_name;
	else
		*pnto = xstrdup(pathend);
	return(0);
}

static int badname(char *s)
{
	return (
		(*s == '.' && (s[1] == '\0' || strcmp(s, "..") == 0)) ||
		strlen(s) > NAME_MAX
	);
}

static int dwritable(HANDLE *h)
{
	char *p = h->h_name, *lastslash = NULL, *pathend;
	const char *myp;
	char *pw = &(h->h_di->di_flags), r;

	if (uid == 0)
		return(1);

	if (*pw & DI_KNOWWRITE)
		return(*pw & DI_CANWRITE);

	pathend = p + strlen(p);
	if (*p == '\0')
		myp = ".";
	else if (pathend == p + 1)
		myp = SLASHSTR;
	else {
		lastslash = pathend - 1;
		*lastslash = '\0';
		myp = p;
	}
	r = !access(myp, W_OK) ? DI_CANWRITE : 0;
	*pw |= DI_KNOWWRITE | r;

	if (lastslash != NULL)
		*lastslash = SLASH;
	return(r);
}

static int fwritable(char *hname, FILEINFO *f)
{
	int r;

	if (f->fi_stflags & FI_KNOWWRITE)
		return(f->fi_stflags & FI_CANWRITE);

	strcpy(fullrep, hname);
	strcat(fullrep, f->fi_name);
	r = !access(fullrep, W_OK) ? FI_CANWRITE : 0;
	f->fi_stflags |= FI_KNOWWRITE | r;
	return(r);
}

static int badrep(HANDLE *hfrom, FILEINFO *ffrom, HANDLE **phto, char **pnto, FILEINFO **pfdel, int *pflags)
{
	char *f = ffrom->fi_name;

	*pflags = 0;
	if ((ffrom->fi_stflags & FI_LINKERR) && !(op & (MOVE | SYMLINK)))
		printf("%s -> %s : source file is a badly aimed symbolic link.\n",
			pathbuf, fullrep);
	else if ((op & (COPY | APPEND)) && access(pathbuf, R_OK))
		printf("%s -> %s : no read permission for source file.\n",
			pathbuf, fullrep);
	else if (
		*f == '.' &&
		(f[1] == '\0' || strcmp(f, "..") == 0) &&
		!(op & SYMLINK)
	)
		printf("%s -> %s : . and .. can't be renamed.\n", pathbuf, fullrep);
	else if (repbad || checkto(f, phto, pnto, pfdel) || badname(*pnto))
		printf("%s -> %s : bad new name.\n", pathbuf, fullrep);
	else if (*phto == NULL)
		printf("%s -> %s : %s.\n", pathbuf, fullrep,
			direrr == H_NOREADDIR ?
			"no read or search permission for target directory" :
			"target directory does not exist (you could use --makedirs)");
	else if (!dwritable(*phto))
		printf("%s -> %s : no write permission for target directory.\n",
			pathbuf, fullrep);
	else if (
		(*phto)->h_di->di_vid != hfrom->h_di->di_vid &&
		(op & (NORMMOVE | HARDLINK))
	)
		printf("%s -> %s : cross-device move.\n",
			pathbuf, fullrep);
	else if (
		*pflags && (op & MOVE) &&
		!(ffrom->fi_stflags & FI_ISLNK) &&
		access(pathbuf, R_OK)
	)
		printf("%s -> %s : no read permission for source file.\n",
			pathbuf, fullrep);
	else if (
		(op & SYMLINK) &&
		!(
			((*phto)->h_di->di_vid == cwdv && (*phto)->h_di->di_did == cwdd) ||
			*(hfrom->h_name) == SLASH ||
			(*pflags |= R_ONEDIRLINK, hfrom->h_di == (*phto)->h_di)
		)
	)
		printf("%s -> %s : symbolic link would be badly aimed.\n",
			pathbuf, fullrep);
	else
		return(0);
	badreps++;
	return(-1);
}

static void makerep(void)
{
	size_t l, x, i;
	int cnv;
	char *q, *p, *pat, c, pc;

	repbad = 0;
	p = fullrep;
	for (pat = to, l = 0; (c = *pat) != '\0'; pat++, l++) {
		if (c == '#') {
			c = *(++pat);
			if (c == 'l') {
				cnv = LOWER;
				c = *(++pat);
			}
			else if (c == 'u') {
				cnv = UPPER;
				c = *(++pat);
			}
			else if (c == 'c') {
				cnv = CAPITALIZE;
				c = *(++pat);
			}
			else
				cnv = STAY;
			for(x = 0; ;x *= 10) {
				x += (size_t)(c - '0');
				c = *(pat+1);
				if (!isdigit(c))
					break;
				pat++;
			}
			--x;
			if (l + length[x] >= PATH_MAX)
				goto toolong;
			switch (cnv) {
			case STAY:
				memmove(p, start[x], length[x]);
				p += length[x];
				break;
			case LOWER:
				for (i = length[x], q = start[x]; i > 0; i--, p++, q++)
					*p = (char)tolower(*q);
				break;
			case UPPER:
				for (i = length[x], q = start[x]; i > 0; i--, p++, q++)
					*p = (char)toupper(*q);
				break;
			case CAPITALIZE:
				for (i = length[x], q = start[x]; i > 0; i--, p++, q++){
					if (i == length[x])
						*p = (char)toupper(*q);
					else
						*p = (char)tolower(*q);
				}
				break;
			}
		}
		else {
			if (c == ESC)
				c = *(++pat);
			if (l == PATH_MAX)
				goto toolong;
			if (c == SLASH &&
				(
					p == fullrep ? pat != to :
					(
						(
							(pc = *(p - 1)) == SLASH
						) &&
						*(pat - 1) != pc
					)
				)
			) {
				repbad = 1;
				if (l + STRLEN(EMPTY) >= PATH_MAX)
					goto toolong;
				strcpy(p, EMPTY);
				p += STRLEN(EMPTY);
				l += STRLEN(EMPTY);
			}
			*(p++)= c;
		}
	}
	if (p == fullrep) {
		strcpy(fullrep, EMPTY);
		repbad = 1;
	}
	*(p++) = '\0';
	return;

toolong:
	repbad = 1;
	strcpy(fullrep, TOOLONG);
}

static int dostage(char *lastend, char *pathend, char **start1, size_t *len1, int stage, int anylev)
{
	DIRINFO *di;
	HANDLE *h, *hto;
	size_t prelen, litlen, i, k, nfils;
	int flags, try;
	FILEINFO **pf, *fdel = NULL;
	char *nto, *firstesc;
	REP *p;
	int ret = 1, laststage = (stage + 1 == nstages);

	if (!anylev) {
		prelen = (size_t)(stagel[stage] - lastend);
		if ((size_t)(pathend - pathbuf) + prelen >= PATH_MAX) {
			printf("%s -> %s : search path after %s too long.\n",
				from, to, pathbuf);
			paterr = 1;
			return(1);
		}
		memmove(pathend, lastend, prelen);
		pathend += prelen;
		*pathend = '\0';
		lastend = stagel[stage];
	}

	if ((h = checkdir(pathbuf, pathend, mkdirs)) == NULL) {
		if (stage == 0 || direrr == H_NOREADDIR) {
			printf("%s -> %s : directory %s does not %s.\n",
				from, to, pathbuf, direrr == H_NOREADDIR ?
				"allow reads/searches" : "exist");
			paterr = 1;
		}
		return(stage);
	}
	di = h->h_di;

	if (*lastend == ';') {
		anylev = 1;
		*start1 = pathend;
		*len1 = 0;
		lastend++;
	}

	nfils = di->di_nfils;

	if ((op & MOVE) && !dwritable(h)) {
		printf("%s -> %s : directory %s does not allow writes.\n",
			from, to, pathbuf);
		paterr = 1;
		goto skiplev;
	}

	firstesc = strchr(lastend, ESC);
	if (firstesc == NULL || firstesc > firstwild[stage])
		firstesc = firstwild[stage];
	litlen = (size_t)(firstesc - lastend);
	pf = di->di_fils + (i = ffirst(lastend, litlen, di));
	if (i < nfils)
	do {
		if (
			(try = trymatch(*pf, lastend)) != 0 &&
			(
				try == 1 ||
				match(lastend + litlen, (*pf)->fi_name + litlen,
					start1 + anylev, len1 + anylev)
			) &&
			keepmatch(*pf, pathend, &k, 0, laststage)
		) {
			if (!laststage)
				ret &= dostage(stager[stage], pathend + k,
					start1 + nwilds[stage], len1 + nwilds[stage],
					stage + 1, 0);
			else {
				ret = 0;
				makerep();
				if (badrep(h, *pf, &hto, &nto, &fdel, &flags)) {
					(*pf)->fi_rep = MISTAKE;
				} else {
					(*pf)->fi_rep = p = (REP *)xmalloc(sizeof(REP));
					p->r_flags = flags;
					p->r_hfrom = h;
					p->r_ffrom = *pf;
					p->r_hto = hto;
					p->r_nto = xstrdup(nto);
					p->r_fdel = fdel;
					p->r_first = p;
					p->r_thendo = NULL;
					p->r_next = NULL;
					lastrep->r_next = p;
					lastrep = p;
					nreps++;
				}
			}
		}
		i++, pf++;
	} while (i < nfils && strncmp(lastend, (*pf)->fi_name, litlen) == 0);

skiplev:
	if (anylev)
		for (pf = di->di_fils, i = 0; i < nfils; i++, pf++)
			if (
				*((*pf)->fi_name) != '.' &&
				keepmatch(*pf, pathend, &k, 1, 0)
			) {
				*len1 = (size_t)(pathend - *start1) + k;
				ret &= dostage(lastend, pathend + k, start1, len1, stage, 1);
			}

	return(ret);
}

static int parsepat(void)
{
	char *p, *lastname, c;
	int totwilds, instage;
#define TRAILESC "%s -> %s : trailing %c is superfluous.\n"

	lastname = from;
	if (from[0] == '~' && from[1] == SLASH) {
		if ((homelen = strlen(home)) + fromlen > MAXPATLEN) {
			printf(PATLONG, from);
			return(-1);
		}
		memmove(from + homelen, from + 1, fromlen);
		memmove(from, home, homelen);
		lastname += homelen + 1;
	}
	totwilds = nstages = instage = 0;
	for (p = lastname; (c = *p) != '\0'; p++)
		switch (c) {
		case SLASH:
			lastname = p + 1;
			if (instage) {
				if (firstwild[nstages] == NULL)
					firstwild[nstages] = p;
				stager[nstages++] = p;
				instage = 0;
			}
			break;
		case ';':
			if (lastname != p) {
				printf("%s -> %s : badly placed ;.\n", from, to);
				return(-1);
			}
			/* FALLTHROUGH */
		case '*':
		case '?':
		case '[':
			if (totwilds++ == MAXWILD) {
				printf("%s -> %s : too many wildcards.\n", from, to);
				return(-1);
			}
			if (instage) {
				nwilds[nstages]++;
				if (firstwild[nstages] == NULL)
					firstwild[nstages] = p;
			}
			else {
				stagel[nstages] = lastname;
				firstwild[nstages] = (c == ';' ? NULL : p);
				nwilds[nstages] = 1;
				instage = 1;
			}
			if (c != '[')
				break;
			while ((c = *(++p)) != ']') {
				switch (c) {
				case '\0':
					printf("%s -> %s : missing ].\n", from, to);
					return(-1);
				case SLASH:
					printf("%s -> %s : '%c' cannot be part of [].\n",
						from, to, c);
					return(-1);
				case ESC:
					if ((c = *(++p)) == '\0') {
						printf(TRAILESC, from, to, ESC);
						return(-1);
					}
				}
			}
			break;
		case ESC:
			if ((c = *(++p)) == '\0') {
				printf(TRAILESC, from, to, ESC);
				return(-1);
			}
		}

	if (instage) {
		if (firstwild[nstages] == NULL)
			firstwild[nstages] = p;
		stager[nstages++] = p;
	}
	else {
		stagel[nstages] = lastname;
		nwilds[nstages] = 0;
		firstwild[nstages] = p;
		stager[nstages++] = p;
	}

	lastname = to;
	if (to[0] == '~' && to[1] == SLASH) {
		if ((homelen = strlen(home)) + tolen > MAXPATLEN) {
			printf(PATLONG, to);
				return(-1);
		}
		memmove(to + homelen, to + 1, tolen);
		memmove(to, home, homelen);
		lastname += homelen + 1;
	}

	for (p = lastname; (c = *p) != '\0'; p++)
		switch (c) {
		case SLASH:
			lastname = p + 1;
			break;
		case '#':
			c = *(++p);
			if (c == 'l' || c == 'u' || c == 'c') {
				c = *(++p);
			}
			if (!isdigit(c)) {
				printf("%s -> %s : expected digit (not '%c') after #.\n",
					from, to, c);
				return(-1);
			}
			int x;
			for (x = 0; ;x *= 10) {
				x += c - '0';
				c = *(p+1);
				if (!isdigit(c))
					break;
				p++;
			}
			if (x < 1 || x > totwilds) {
				printf("%s -> %s : wildcard #%d does not exist.\n",
					from, to, x);
				return(-1);
			}
			break;
		case ESC:
			if ((c = *(++p)) == '\0') {
				printf(TRAILESC, from, to, ESC);
				return(-1);
			}
		}

	return(0);
}

static void matchpat(void)
{
	if (parsepat())
		paterr = 1;
	else if (dostage(from, pathbuf, start, length, 0, 0)) {
		printf("%s -> %s : no match.\n", from, to);
		paterr = 1;
	}
}

static void domatch(char *cfrom, char *cto)
{
	if ((fromlen = strlen(cfrom)) >= MAXPATLEN) {
		printf(PATLONG, cfrom);
		paterr = 1;
	}
	else if ((tolen = strlen(cto)) >= MAXPATLEN) {
		printf(PATLONG, cto);
		paterr = 1;
	}
	else {
		strcpy(from, cfrom);
		strcpy(to, cto);
		matchpat();
	}
}

static int rdcmp(const void *p1, const void *p2)
{
	int ret;

	REPDICT *rd1 = (REPDICT *)p1;
	REPDICT *rd2 = (REPDICT *)p2;
	if (
		(ret = (int)(long)(rd1->rd_dto - rd2->rd_dto)) == 0 &&
		(ret = strcmp(rd1->rd_nto, rd2->rd_nto)) == 0
	)
		ret = (int)(long)(rd1->rd_i - rd2->rd_i);
	return(ret);
}

static void checkcollisions(void)
{
	REPDICT *rd, *prd;
	REP *p, *q;
	unsigned i, oldnreps;

	if (nreps == 0)
		return;
	rd = (REPDICT *)xmalloc(nreps * sizeof(REPDICT));
	for (
		q = &hrep, p = q->r_next, prd = rd, i = 0;
		p != NULL;
		q = p, p = p->r_next, prd++, i++
	) {
		prd->rd_p = p;
		prd->rd_dto = p->r_hto->h_di;
		prd->rd_nto = p->r_nto;
		prd->rd_i = i;
	}
	qsort(rd, nreps, sizeof(REPDICT), rdcmp);
	unsigned mult = 0;
	for (i = 0, prd = rd, oldnreps = nreps; i < oldnreps; i++, prd++)
		if (
			i < oldnreps - 1 &&
			prd->rd_dto == (prd + 1)->rd_dto &&
			strcmp(prd->rd_nto, (prd + 1)->rd_nto) == 0
		) {
			if (!mult)
				mult = 1;
			else
				printf(" , ");
			printf("%s%s", prd->rd_p->r_hfrom->h_name,
				prd->rd_p->r_ffrom->fi_name);
			prd->rd_p->r_flags |= R_SKIP;
			prd->rd_p->r_ffrom->fi_rep = MISTAKE;
			nreps--;
			badreps++;
		}
		else if (mult) {
			prd->rd_p->r_flags |= R_SKIP;
			prd->rd_p->r_ffrom->fi_rep = MISTAKE;
			nreps--;
			badreps++;
			printf(" , %s%s -> %s%s : collision.\n",
				prd->rd_p->r_hfrom->h_name, prd->rd_p->r_ffrom->fi_name,
				prd->rd_p->r_hto->h_name, prd->rd_nto);
			mult = 0;
		}
}

static void findorder(void)
{
	REP *first, *pred;
	FILEINFO *fi;

	for (REP *q = &hrep, *p = q->r_next; p != NULL; q = p, p = p->r_next)
		if (p->r_flags & R_SKIP) {
			q->r_next = p->r_next;
			p = q;
		}
		else if (
			(fi = p->r_fdel) == NULL ||
			(pred = fi->fi_rep) == NULL ||
			pred == MISTAKE
		)
			continue;
		else if ((first = pred->r_first) == p) {
			p->r_flags |= R_ISCYCLE;
			pred->r_flags |= R_ISALIASED;
			if (op & MOVE)
				p->r_fdel = NULL;
		}
		else {
			if (op & MOVE)
				p->r_fdel = NULL;
			while (pred->r_thendo != NULL)
				pred = pred->r_thendo;
			pred->r_thendo = p;
			for (REP *t = p; t != NULL; t = t->r_thendo)
				t->r_first = first;
			q->r_next = p->r_next;
			p = q;
		}
}

static void scandeletes(int (*pkilldel)(REP *))
{
	for (REP *q = &hrep, *p = q->r_next; p != NULL; q = p, p = p->r_next) {
		if (p->r_fdel != NULL)
			while ((*pkilldel)(p)) {
				nreps--;
				p->r_ffrom->fi_rep = MISTAKE;
				REP *n;
				if ((n = p->r_thendo) != NULL) {
					if (op & MOVE)
						n->r_fdel = p->r_ffrom;
					n->r_next = p->r_next;
					q->r_next = p = n;
				}
				else {
					q->r_next = p->r_next;
					p = q;
					break;
				}
			}
	}
}

static int baddel(REP *p)
{
	HANDLE *hfrom = p->r_hfrom, *hto = p->r_hto;
	FILEINFO *fto = p->r_fdel;
	char *t = fto->fi_name, *f = p->r_ffrom->fi_name;
	char *hnf = hfrom->h_name, *hnt = hto->h_name;

	if (delstyle == NODEL && !(p->r_flags & R_DELOK) && !(op & APPEND))
		printf("%s%s -> %s%s : old %s%s would have to be %s.\n",
			hnf, f, hnt, t, hnt, t,
			(op & OVERWRITE) ? "overwritten" : "deleted");
	else if (fto->fi_rep == MISTAKE)
		printf("%s%s -> %s%s : old %s%s was to be done first.\n",
			hnf, f, hnt, t, hnt, t);
	else if (
		fto->fi_stflags & FI_ISDIR
	)
		printf("%s%s -> %s%s : %s%s%s is a directory.\n",
		       hnf, f, hnt, t, (op & APPEND) ? "" : "old ", hnt, t);
	else if ((fto->fi_stflags & FI_NODEL) && !(op & (APPEND | OVERWRITE)))
		printf("%s%s -> %s%s : old %s%s lacks delete permission.\n",
			hnf, f, hnt, t, hnt, t);
	else if (
		(op & (APPEND | OVERWRITE)) &&
		!fwritable(hnt, fto)
	) {
		printf("%s%s -> %s%s : %s%s %s.\n",
			hnf, f, hnt, t, hnt, t,
			fto->fi_stflags & FI_LINKERR ?
			"is a badly aimed symbolic link" :
			"lacks write permission");
	}
	else
		return(0);
	badreps++;
	return(1);
}

static int skipdel(REP *p)
{
	if (p->r_flags & R_DELOK)
		return(0);
	fprintf(stderr, "%s%s -> %s%s : ",
		p->r_hfrom->h_name, p->r_ffrom->fi_name,
		p->r_hto->h_name, p->r_nto);
	if (
		!(p->r_ffrom->fi_stflags & FI_ISLNK) &&
		!fwritable(p->r_hto->h_name, p->r_fdel)
	)
		fprintf(stderr, "old %s%s lacks write permission. delete it",
			p->r_hto->h_name, p->r_nto);
	else
		fprintf(stderr, "%s old %s%s",
			(op & OVERWRITE) ? "overwrite" : "delete",
			p->r_hto->h_name, p->r_nto);
	fprintf(stderr, "? ");
	return(!getreply());
}

static void showdone(REP *fin)
{
	for (REP *first = hrep.r_next; ; first = first->r_next)
		for (REP *p = first; p != NULL; p = p->r_thendo) {
			if (p == fin)
				return;
			printf("%s%s %c%c %s%s : done%s\n",
				p->r_hfrom->h_name, p->r_ffrom->fi_name,
				p->r_flags & R_ISALIASED ? '=' : '-',
				p->r_flags & R_ISCYCLE ? '^' : '>',
				p->r_hto->h_name, p->r_nto,
				(p->r_fdel != NULL && !(op & APPEND)) ? " (*)" : "");
		}
}

static int snap(REP *first, REP *p)
{
	if (noex)
		exit(1);

	failed = 1;
	signal(SIGINT, breakstat);
	if (!verbose)
		showdone(p);
	printf("The following left undone:\n");
	noex = 1;
	return(first != p);
}

static long appendalias(REP *first, REP *p, int *pprintaliased)
{
	long ret = 0l;

	struct stat fstat;

	if (stat(fullrep, &fstat)) {
		fprintf(stderr, "append cycle stat on %s has failed.\n", fullrep);
		*pprintaliased = snap(first, p);
	}
	else
		ret = fstat.st_size;

	return(ret);
}

static int movealias(REP *first, REP *p, int *pprintaliased)
{
	char *fstart;
	int ret;

	strcpy(pathbuf, p->r_hto->h_name);
	fstart = pathbuf + strlen(pathbuf);
	strcpy(fstart, TEMP);
	for (
		ret = 0;
		sprintf(fstart + STRLEN(TEMP), "%03d", ret),
		fsearch(fstart, p->r_hto->h_di) != NULL;
		ret++
	)
		;
	if (rename(fullrep, pathbuf)) {
		fprintf(stderr,
			"%s -> %s has failed.\n", fullrep, pathbuf);
		*pprintaliased = snap(first, p);
	}
	return(ret);
}

#define IRWMASK (S_IRUSR | S_IWUSR)
#define RWMASK (IRWMASK | (IRWMASK >> 3) | (IRWMASK >> 6))

static int copy(FILEINFO *ff, off_t len)
{
	char buf[BUFSIZ];
	int f, t, mode;
	mode_t perm;
	ssize_t k;
	struct utimbuf tim;
	struct stat fstat;

	if ((f = open(pathbuf, O_RDONLY | O_BINARY, 0)) < 0)
		return(-1);
	perm = (op & (APPEND | OVERWRITE)) ?
		(~oldumask & RWMASK) | (ff->fi_mode & (mode_t)~RWMASK) :
		ff->fi_mode;

	mode = O_CREAT | (op & APPEND ? 0 : O_TRUNC) | O_WRONLY;
	t = open(fullrep, mode, perm);
	if (t < 0) {
		close(f);
		return(-1);
	}
	if (op & APPEND)
		lseek(t, (off_t)0, SEEK_END);
	if ((op & APPEND) && len != (off_t)-1) {
		while (
			len != 0 &&
			(k = read(f, buf, (len > BUFSIZ) ? BUFSIZ : (size_t)len)) > 0 &&
			write(t, buf, (size_t)k) == k
		)
			len -= k;
		if (len == 0)
			k = 0;
	}
	else
		while ((k = read(f, buf, BUFSIZ)) > 0 && write(t, buf, (size_t)k) == k)
			;
	if (!(op & (APPEND | OVERWRITE)))
		if (
			stat(pathbuf, &fstat) ||
			(
				tim.actime = fstat.st_atime,
				tim.modtime = fstat.st_mtime,
				utime(fullrep, &tim)
			)
		)
			fprintf(stderr, "Strange, couldn't transfer time from %s to %s.\n",
				pathbuf, fullrep);

	close(f);
	close(t);
	if (k != 0) {
		if (!(op & APPEND))
			unlink(fullrep);
		return(-1);
	}
	return(0);
}

static int myunlink(char *n)
{
	if (unlink(n)) {
		fprintf(stderr, "Strange, cannot unlink %s.\n", n);
		return(-1);
	}
	return(0);
}

static int copymove(REP *p)
{
	return(copy(p->r_ffrom, -1L) || myunlink(pathbuf));
}

static void doreps(void)
{
	char *fstart;
	unsigned k;
	int printaliased = 0, alias = 0;
	REP *first, *p;
	long aliaslen = 0l;

	signal(SIGINT, breakrep);

	for (first = hrep.r_next, k = 0; first != NULL; first = first->r_next) {
		for (p = first; p != NULL; p = p->r_thendo, k++) {
			if (gotsig) {
				fflush(stdout);
				fprintf(stderr, "User break.\n");
				printaliased = snap(first, p);
				gotsig = 0;
			}
			strcpy(fullrep, p->r_hto->h_name);
			if (mkdirs && p->r_hto->h_di->di_flags & DI_NONEXISTENT) {
				if (verbose)
					printf("creating directory %s\n", p->r_hto->h_name);
				// FIXME: check the return value.
				make_directory(p->r_hto);
				p->r_hto->h_di->di_flags &= ~DI_NONEXISTENT;
			}
			strcat(fullrep, p->r_nto);
			if (!noex && (p->r_flags & R_ISCYCLE)) {
				if (op & APPEND)
					aliaslen = appendalias(first, p, &printaliased);
				else
					alias = movealias(first, p, &printaliased);
			}
			strcpy(pathbuf, p->r_hfrom->h_name);
			fstart = pathbuf + strlen(pathbuf);
			if ((p->r_flags & R_ISALIASED) && !(op & APPEND))
				sprintf(fstart, "%s%03d", TEMP, alias);
			else
				strcpy(fstart, p->r_ffrom->fi_name);
			if (!noex) {
				if (p->r_fdel != NULL && !(op & (APPEND | OVERWRITE)))
					myunlink(fullrep);
				if (
					(op & (COPY | APPEND)) ?
						copy(p->r_ffrom,
							p->r_flags & R_ISALIASED ? aliaslen : -1L) :
					(op & HARDLINK) ?
						link(pathbuf, fullrep) :
					(op & SYMLINK) ?
						symlink((p->r_flags & R_ONEDIRLINK) ? fstart : pathbuf,
							fullrep) :
					p->r_hto->h_di->di_vid != p->r_hfrom->h_di->di_vid ?
						copymove(p) :
					/* move */
						rename(pathbuf, fullrep)
				) {
					fprintf(stderr,
						"%s -> %s has failed.\n", pathbuf, fullrep);
					printaliased = snap(first, p);
				}
			}
			if (verbose || noex) {
				if (p->r_flags & R_ISALIASED && !printaliased)
					strcpy(fstart, p->r_ffrom->fi_name);
				printf("%s %c%c %s%s%s\n",
					pathbuf,
					p->r_flags & R_ISALIASED ? '=' : '-',
					p->r_flags & R_ISCYCLE ? '^' : '>',
					fullrep,
					(p->r_fdel != NULL && !(op & APPEND)) ? " (*)" : "",
					noex ? "" : " : done");
			}
		}
		printaliased = 0;
	}
	if (k != nreps)
		fprintf(stderr, "Strange, did %u reps; %u were expected.\n",
			k, nreps);
	if (k == 0)
		fprintf(stderr, "Nothing done.\n");
}

int main(int argc, char *argv[])
{
	char *frompat, *topat;

	set_program_name(argv[0]);

	struct stat dstat;

	if ((home = getenv("HOME")) == NULL || strcmp(home, SLASHSTR) == 0)
		home = "";
	if (!stat(".", &dstat)) {
		cwdd = dstat.st_ino;
		cwdv = dstat.st_dev;
	}
	oldumask = umask(0);
#ifndef _WIN32
	uid = getuid();
#endif
	signal(SIGINT, breakout);

	dirroom = handleroom = INITROOM;
	dirs = (DIRINFO **)xmalloc(dirroom * sizeof(DIRINFO *));
	handles = (HANDLE **)xmalloc(handleroom * sizeof(HANDLE *));
	ndirs = nhandles = 0;

	dirroom_nonexistent = INITROOM;
	dirs_nonexistent = (DIRINFO **)xmalloc(dirroom_nonexistent * sizeof(DIRINFO *));
	ndirs_nonexistent = 0;

	struct gengetopt_args_info args_info;
	if (cmdline_parser(argc, argv, &args_info) != 0)
		exit(EXIT_FAILURE);

	verbose = args_info.verbose_given != 0;
	noex = args_info.dryrun_given != 0;
	matchall = args_info.hidden_given != 0;
	mkdirs = args_info.makedirs_given != 0;

	delstyle = ASKDEL;
	if (args_info.force_given != 0)
		delstyle = ALLDEL;
	else if (args_info.protect_given != 0)
		delstyle = NODEL;

	badstyle = ASKBAD;
	if (args_info.go_given != 0)
		badstyle = SKIPBAD;
	else if (args_info.terminate_given != 0)
		badstyle = ABORTBAD;

	// The hidden --rename/-r option is recognised for backwards compatibility.
	if (args_info.move_given != 0 || args_info.rename_given != 0)
		op = NORMMOVE;
	else if (args_info.copydel_given != 0)
		op = XMOVE;
	else if (args_info.copy_given != 0)
		op = NORMCOPY;
	else if (args_info.overwrite_given != 0)
		op = OVERWRITE;
	else if (args_info.append_given != 0)
		op = APPEND;
	else if (args_info.hardlink_given != 0)
		op = HARDLINK;
	else if (args_info.symlink_given != 0)
		op = SYMLINK;
	else {
		const char *name = base_name(program_name);

		if (strcmp(name, COPYNAME) == 0)
			op = NORMCOPY;
		else if (strcmp(name, APPENDNAME) == 0)
			op = APPEND;
		else if (strcmp(name, LINKNAME) == 0)
			op = HARDLINK;
		else
			op = XMOVE;
	}

	if (badstyle != ASKBAD && delstyle == ASKDEL)
		delstyle = NODEL;

	if (args_info.inputs_num == 2) {
		frompat = args_info.inputs[0];
		topat = args_info.inputs[1];
	}
	else {
		/* Print message to stderr, not stdout. */
		dup2(STDERR_FILENO, STDOUT_FILENO);
		cmdline_parser_print_help();
		exit(1);
	}

	domatch(frompat, topat);
	if (!(op & APPEND))
		checkcollisions();
	findorder();
	if (op & (COPY | LINK))
		nochains();
	scandeletes(baddel);
	goonordie();
	if (!(op & APPEND) && delstyle == ASKDEL)
		scandeletes(skipdel);
	doreps();

	return(failed ? 2 : nreps == 0 && (paterr || badreps));
}
