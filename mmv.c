/*
	mmv

	Copyright (c) 2021 Reuben Thomas.
	Copyright (c) 1990 Vladimir Lanin.

	This program is distributed under the GNU GPL version 3, or, at your
	option, any later version..

	Maintainer: Reuben Thomas <rrt@sc3d.org>

	The original author of mmv was Vladimir Lanin <lanin@csd2.nyu.edu>, of
	330 Wadsworth Ave, Apt 6F,
	New York, NY 10040

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

#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <utime.h>
#include <dirent.h>

#include "xalloc.h"
#include "unused-parameter.h"


#define USAGE \
"Usage: \
%s [-m|x|r|c|o|a|l%s] [-h] [-d|p] [-g|t] [-v|n] [from to]\n\
\n\
Use #[l|u]N in the ``to'' pattern to get the [lowercase|uppercase of the]\n\
string matched by the N'th ``from'' pattern wildcard.\n\
\n\
A ``from'' pattern containing wildcards should be quoted when given\n\
on the command line. Also you may need to quote ``to'' pattern.\n\
\n\
Use -- as the end of options.\n"

#define OTHEROPT ""


#ifndef O_BINARY
#define O_BINARY 0
#endif

#define ESC '\\'
#define SLASH '/'

static char TTY[] = "/dev/tty";


#define STRLEN(s) (sizeof(s) - 1)


#define DFLT 0x001
#define NORMCOPY 0x002
#define OVERWRITE 0x004
#define NORMMOVE 0x008
#define XMOVE 0x010
#define DIRMOVE 0x020
#define NORMAPPEND 0x040
#define ZAPPEND 0x080
#define HARDLINK 0x100
#define SYMLINK 0x200

#define COPY (NORMCOPY | OVERWRITE)
#define MOVE (NORMMOVE | XMOVE | DIRMOVE)
#define APPEND (NORMAPPEND | ZAPPEND)
#define LINK (HARDLINK | SYMLINK)

static char MOVENAME[] = "mmv";
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

typedef struct {
	dev_t di_vid;
	ino_t di_did;
	size_t di_nfils;
	FILEINFO **di_fils;
	char di_flags;
} DIRINFO;

#define H_NODIR 1
#define H_NOREADDIR 2

typedef struct {
	char *h_name;
	DIRINFO *h_di;
	char h_err;
} HANDLE;

#define R_ISX 0x01
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


static void init(void);
static void procargs(int argc, char **argv,
	char **pfrompat, char **ptopat);
static void domatch(char *cfrom, char *cto);
static int getpat(void);
static size_t getword(char *buf);
static void matchpat(void);
static int parsepat(void);
static int dostage(char *lastend, char *pathend,
	char **start1, size_t *len1, int stage, int anylev);
static int trymatch(FILEINFO *ffrom, char *pat);
static int keepmatch(FILEINFO *ffrom, char *pathend,
	size_t *pk, int needslash, int fils);
static int badrep(HANDLE *hfrom, FILEINFO *ffrom,
	HANDLE **phto, char **pnto, FILEINFO **pfdel, int *pflags);
static int checkto(HANDLE *hfrom, char *f,
	HANDLE **phto, char **pnto, FILEINFO **pfdel);
static char *getpath(char *tpath);
static int badname(char *s);
static FILEINFO *fsearch(char *s, DIRINFO *d);
static size_t ffirst(char *s, size_t n, DIRINFO *d);
static HANDLE *checkdir(char *p, char *pathend, int which);
static void takedir(const char *p, DIRINFO *di, int sticky);
static int fcmp(const void *pf1, const void *pf2);
static HANDLE *hadd(char *n);
static int hsearch(char *n, int which, HANDLE **ph);
static DIRINFO *dadd(dev_t v, ino_t d);
static DIRINFO *dsearch(dev_t v, ino_t d);
static int match(char *pat, char *s, char **start1, size_t *len1);
static void makerep(void);
static void checkcollisions(void);
static int rdcmp(const void *rd1, const void *rd2);
static void findorder(void);
static void scandeletes(int (*pkilldel)(REP *p));
static int baddel(REP *p);
static int skipdel(REP *p);
static void nochains(void);
static void printchain(REP *p);
static void goonordie(void);
static void doreps(void);
static long appendalias(REP *first, REP *p, int *pprintaliased);
static int movealias(REP *first, REP *p, int *pprintaliased);
static int snap(REP *first, REP *p);
static void showdone(REP *fin);
static void breakout(int signum);
static void breakrep(int);
static void breakstat(int signum);
static void quit(void);
static int copymove(REP *p);
static int copy(FILEINFO *f, long len);
static int myunlink(char *n);
static int getreply(const char *m, int failact);
static char *mygets(char *s, int l);
static int getstat(char *full, FILEINFO *f);
static int dwritable(HANDLE *h);
static int fwritable(char *hname, FILEINFO *f);

static int op, badstyle, delstyle, verbose, noex, matchall;
static int patflags;

static unsigned ndirs = 0, dirroom;
static DIRINFO **dirs;
static unsigned nhandles = 0, handleroom;
static HANDLE **handles;
static char badhandle_name[] = "\200";
static HANDLE badhandle = {badhandle_name, NULL, 0};
static HANDLE *(lasthandle[2]) = {&badhandle, &badhandle};
static unsigned nreps = 0;
static REP hrep, *lastrep = &hrep;

static int badreps = 0, paterr = 0, direrr, failed = 0, gotsig = 0, repbad;
static FILE *outfile;

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


#define DFLTOP XMOVE

static const char *home;
static size_t homelen;
static uid_t uid, euid;
static mode_t oldumask;
static ino_t cwdd = -1UL;
static dev_t cwdv = -1UL;


int main(int argc, char *argv[])
{
	char *frompat, *topat;

	outfile = stdout;

	init();
	procargs(argc, argv, &frompat, &topat);
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

static void init(void)
{
	struct stat dstat;

	if ((home = getenv("HOME")) == NULL || strcmp(home, SLASHSTR) == 0)
		home = "";
	if (!stat(".", &dstat)) {
		cwdd = dstat.st_ino;
		cwdv = dstat.st_dev;
	}
	oldumask = umask(0);
	euid = geteuid();
	uid = getuid();
	signal(SIGINT, breakout);

	dirroom = handleroom = INITROOM;
	dirs = (DIRINFO **)xmalloc(dirroom * sizeof(DIRINFO *));
	handles = (HANDLE **)xmalloc(handleroom * sizeof(HANDLE *));
	ndirs = nhandles = 0;
}

static void procargs(int argc, char **argv, char **pfrompat, char **ptopat)
{
	char *p;
	int c;
	char *cmdname = basename(argv[0]);

#define CMDNAME cmdname

	op = DFLT;
	verbose = noex = matchall = 0;
	delstyle = ASKDEL;
	badstyle = ASKBAD;
	for (argc--, argv++; argc > 0 && **argv == '-'; argc--, argv++)
		for (p = *argv + 1; *p != '\0'; p++) {
			c = tolower(*p);
			if (c == '-') {
				argc--;
				argv++;
				goto endargs;
			}
			if (c == 'v' && !noex)
				verbose = 1;
			else if (c == 'n' && !verbose)
				noex = 1;
			else if (c == 'h')
				matchall = 1;
			else if (c == 'd' && delstyle == ASKDEL)
				delstyle = ALLDEL;
			else if (c == 'p' && delstyle == ASKDEL)
				delstyle = NODEL;
			else if (c == 'g' && badstyle == ASKBAD)
				badstyle = SKIPBAD;
			else if (c == 't' && badstyle == ASKBAD)
				badstyle = ABORTBAD;
			else if (c == 'm' && op == DFLT)
				op = NORMMOVE;
			else if (c == 'x' && op == DFLT)
				op = XMOVE;
			else if (c == 'r' && op == DFLT)
				op = DIRMOVE;
			else if (c == 'c' && op == DFLT)
				op = NORMCOPY;
			else if (c == 'o' && op == DFLT)
				op = OVERWRITE;
			else if (c == 'a' && op == DFLT)
				op = NORMAPPEND;
			else if (c == 'l' && op == DFLT)
				op = HARDLINK;
			else if (c == 's' && op == DFLT)
				op = SYMLINK;
			else {
				fprintf(stderr, USAGE, CMDNAME, OTHEROPT);
				exit(1);
			}
		}

endargs:
	if (op == DFLT) {
		if (strcmp(cmdname, MOVENAME) == 0)
			op = XMOVE;
		else if (strcmp(cmdname, COPYNAME) == 0)
			op = NORMCOPY;
		else if (strcmp(cmdname, APPENDNAME) == 0)
			op = NORMAPPEND;
		else if (strcmp(cmdname, LINKNAME) == 0)
			op = HARDLINK;
		else
			op = DFLTOP;
	}

	if (euid != uid && !(op & DIRMOVE)) {
		setuid(uid);
		setgid(getgid());
	}

	if (badstyle != ASKBAD && delstyle == ASKDEL)
		delstyle = NODEL;

	if (argc == 0)
		*pfrompat = NULL;
	else if (argc == 2) {
		*pfrompat = *(argv++);
		*ptopat = *(argv++);
	}
	else {
		fprintf(stderr, USAGE, CMDNAME, OTHEROPT);
		exit(1);
	}
}

static void domatch(char *cfrom, char *cto)
{
	if (cfrom == NULL)
		while (getpat())
			matchpat();
	else if ((fromlen = strlen(cfrom)) >= MAXPATLEN) {
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

static int getpat(void)
{
	int c, gotit = 0;
	char extra[MAXPATLEN];

	patflags = 0;
	do {
		if ((fromlen = getword(from)) == 0 || fromlen == SIZE_MAX)
			goto nextline;

		do {
			if ((tolen = getword(to)) == 0) {
				printf("%s -> ? : missing replacement pattern.\n", from);
				goto nextline;
			}
			if (tolen == SIZE_MAX)
				goto nextline;
		} while (
			tolen == 2 &&
			(to[0] == '-' || to[0] == '=') &&
			(to[1] == '>' || to[1] == '^')
		);
		if (getword(extra) == 0)
			gotit = 1;
		else if (strcmp(extra, "(*)") == 0) {
			patflags |= R_DELOK;
	    gotit = (getword(extra) == 0);
		}

nextline:
		while ((c = getchar()) != '\n' && c != EOF)
			;
		if (c == EOF)
			return(0);
	} while (!gotit);

	return(1);
}

static size_t getword(char *buf)
{
	int c, prevc;
	size_t n;
	char *p;

	p = buf;
	prevc = ' ';
	n = 0;
	while ((c = getchar()) != EOF && (prevc == ESC || !isspace(c))) {
		if (n == SIZE_MAX)
			continue;
		if (n == MAXPATLEN - 1) {
			*p = '\0';
			printf(PATLONG, buf);
			n = SIZE_MAX;
		}
		*(p++) = (char)c;
		n++;
		prevc = c;
	}
	*p = '\0';
	while (c != EOF && isspace(c) && c != '\n')
		c = getchar();
	if (c != EOF)
		ungetc(c, stdin);
	return(n);
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

static int parsepat(void)
{
	char *p, *lastname, c;
	int totwilds, instage, x;
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
		case '!':
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
					printf("%s -> %s : '%c' can not be part of [].\n",
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
			if (op & DIRMOVE) {
				printf("%s -> %s : no path allowed in target under -r.\n",
					from, to);
				return(-1);
			}
			lastname = p + 1;
			break;
		case '#':
			c = *(++p);
			if (c == 'l' || c == 'u') {
				c = *(++p);
			}
			if (!isdigit(c)) {
				printf("%s -> %s : expected digit (not '%c') after #.\n",
					from, to, c);
				return(-1);
			}
			for(x = 0; ;x *= 10) {
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

	if ((h = checkdir(pathbuf, pathend, 0)) == NULL) {
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
				if (badrep(h, *pf, &hto, &nto, &fdel, &flags))
					(*pf)->fi_rep = MISTAKE;
				else {
					(*pf)->fi_rep = p = (REP *)xmalloc(sizeof(REP));
					p->r_flags = flags | patflags;
					p->r_hfrom = h;
					p->r_ffrom = *pf;
					p->r_hto = hto;
					p->r_nto = nto;
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
	if (!(ffrom->fi_stflags & FI_ISDIR) && !fils)
	{
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
	else if (repbad || checkto(hfrom, f, phto, pnto, pfdel) || badname(*pnto))
		printf("%s -> %s : bad new name.\n", pathbuf, fullrep);
	else if (*phto == NULL)
		printf("%s -> %s : %s.\n", pathbuf, fullrep,
			direrr == H_NOREADDIR ?
			"no read or search permission for target directory" :
			"target directory does not exist");
	else if (!dwritable(*phto))
		printf("%s -> %s : no write permission for target directory.\n",
			pathbuf, fullrep);
	else if (
		(*phto)->h_di->di_vid != hfrom->h_di->di_vid &&
		(*pflags = R_ISX, (op & (NORMMOVE | HARDLINK)))
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

static int checkto(HANDLE *hfrom, char *f, HANDLE **phto, char **pnto, FILEINFO **pfdel)
{
	char tpath[PATH_MAX + 1];
	char *pathend;
	FILEINFO *fdel = NULL;
	size_t hlen, tlen;

	if (op & DIRMOVE) {
		*phto = hfrom;
		hlen = strlen(hfrom->h_name);
		pathend = fullrep + hlen;
		memmove(pathend, fullrep, strlen(fullrep) + 1);
		memmove(fullrep, hfrom->h_name, hlen);
		if ((fdel = *pfdel = fsearch(pathend, hfrom->h_di)) != NULL) {
			*pnto = fdel->fi_name;
			getstat(fullrep, fdel);
		}
		else
			*pnto = xstrdup(pathend);
	}
	else {
		pathend = getpath(tpath);
		hlen = (size_t)(pathend - fullrep);
		*phto = checkdir(tpath, tpath + hlen, 1);
		if (
			*phto != NULL &&
			*pathend != '\0' &&
			(fdel = *pfdel = fsearch(pathend, (*phto)->h_di)) != NULL &&
			(getstat(fullrep, fdel), fdel->fi_stflags & FI_ISDIR)
		) {
			tlen = strlen(pathend);
			strcpy(pathend + tlen, SLASHSTR);
			tlen++;
			strcpy(tpath + hlen, pathend);
			pathend += tlen;
			hlen += tlen;
			*phto = checkdir(tpath, tpath + hlen, 1);
		}

		if (*pathend == '\0') {
			*pnto = f;
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
	}
	return(0);
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

static int badname(char *s)
{
	return (
		(*s == '.' && (s[1] == '\0' || strcmp(s, "..") == 0)) ||
		strlen(s) > MAXNAMLEN
	);
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
	if ((fstat.st_mode & S_IFMT) == S_IFLNK) {
		flags |= FI_ISLNK;
		if (stat(ffull, &fstat)) {
			f->fi_stflags = flags | FI_LINKERR;
			return(1);
		}
	}
	if ((fstat.st_mode & S_IFMT) == S_IFDIR)
		flags |= FI_ISDIR;
	f->fi_stflags = flags;
	f->fi_mode = fstat.st_mode;
	return(0);
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

static FILEINFO *fsearch(char *s, DIRINFO *d)
{
	FILEINFO **fils = d->di_fils;
	size_t nfils = d->di_nfils, first, last, k;
	int res;

	for (first = 0, last = nfils - 1;;) {
		if (last < first)
			return(NULL);
		k = (first + last) >> 1;
		if ((res = strcmp(s, fils[k]->fi_name)) == 0)
			return(fils[k]);
		if (res < 0)
			last = k - 1;
		else
			first = k + 1;
	}
}

static size_t ffirst(char *s, size_t n, DIRINFO *d)
{
	size_t first, k, last;
	int res;
	FILEINFO **fils = d->di_fils;
	size_t nfils = d->di_nfils;

	if (nfils == 0 || n == 0)
		return(0);
	first = 0;
	last = nfils - 1;
	for(;;) {
		k = (first + last) >> 1;
		res = strncmp(s, fils[k]->fi_name, n);
		if (first == last)
			return(res == 0 ? k : nfils);
		else if (res > 0)
			first = k + 1;
		else
			last = k;
	}
}


/* checkdir, takedir */

static HANDLE *checkdir(char *p, char *pathend, int which)
{
	struct stat dstat;
	ino_t d;
	dev_t v;
	DIRINFO *di = NULL;
	const char *myp;
	char *lastslash = NULL;
	int sticky;
	HANDLE *h;

	if (hsearch(p, which, &h)) {
		if (h->h_di == NULL) {
			direrr = h->h_err;
			return(NULL);
		}
		else
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

	if (stat(myp, &dstat) || (dstat.st_mode & S_IFMT) != S_IFDIR)
		direrr = h->h_err = H_NODIR;
	else if (access(myp, R_OK | X_OK))
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
			room *= 2;
			fils = (FILEINFO **)xmalloc(room * sizeof(FILEINFO *));
			memcpy(fils, di->di_fils, cnt * sizeof(FILEINFO *));
			free(di->di_fils);
			di->di_fils = fils;
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

/* end of checkdir, takedir; back to general program */


static int fcmp(const void *pf1, const void *pf2)
{
	return(strcmp((*(FILEINFO **)pf1)->fi_name, (*(FILEINFO **)pf2)->fi_name));
}

static HANDLE *hadd(char *n)
{
	HANDLE **newhandles, *h;

	if (nhandles == handleroom) {
		handleroom *= 2;
		newhandles = (HANDLE **)xmalloc(handleroom * sizeof(HANDLE *));
		memcpy(newhandles, handles, nhandles * sizeof(HANDLE *));
		free(handles);
		handles = newhandles;
	}
	handles[nhandles++] = h = (HANDLE *)xmalloc(sizeof(HANDLE));
	h->h_name = (char *)xmalloc(strlen(n) + 1);
	strcpy(h->h_name, n);
	h->h_di = NULL;
	return(h);
}

static int hsearch(char *n, int which, HANDLE **pret)
{
	unsigned i;
	HANDLE **ph;

	if (strcmp(n, lasthandle[which]->h_name) == 0) {
		*pret = lasthandle[which];
		return(1);
	}

	for(i = 0, ph = handles; i < nhandles; i++, ph++)
		if (strcmp(n, (*ph)->h_name) == 0) {
			lasthandle[which] = *pret = *ph;
			return(1);
		}

	lasthandle[which] = *pret = hadd(n);
	return(0);
}

static DIRINFO *dadd(dev_t v, ino_t d)
{
	DIRINFO *di;
	DIRINFO **newdirs;

	if (ndirs == dirroom) {
		dirroom *= 2;
		newdirs = (DIRINFO **)xmalloc(dirroom * sizeof(DIRINFO *));
		memcpy(newdirs, dirs, ndirs * sizeof(DIRINFO *));
		free(dirs);
		dirs = newdirs;
	}
	dirs[ndirs++] = di = (DIRINFO *)xmalloc(sizeof(DIRINFO));
	di->di_vid = v;
	di->di_did = d;
	di->di_nfils = 0;
	di->di_fils = NULL;
	di->di_flags = 0;
	return(di);
}

static DIRINFO *dsearch(dev_t v, ino_t d)
{
	for (unsigned i = 0; i < ndirs; i++)
		if (v == dirs[i]->di_vid && d == dirs[i]->di_did)
			return(dirs[i]);
	return(NULL);
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
			}
		}
		else {
			if (c == ESC)
				c = *(++pat);
			if (l == PATH_MAX)
				goto toolong;
			if (
				(
					c == SLASH
				) &&
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

static void checkcollisions(void)
{
	REPDICT *rd, *prd;
	REP *p, *q;
	unsigned i, mult, oldnreps;

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
	mult = 0;
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
	free(rd);
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

static void findorder(void)
{
	REP *p, *q, *t, *first, *pred;
	FILEINFO *fi;

	for (q = &hrep, p = q->r_next; p != NULL; q = p, p = p->r_next)
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
			for (t = p; t != NULL; t = t->r_thendo)
				t->r_first = first;
			q->r_next = p->r_next;
			p = q;
		}
}

static void nochains(void)
{
	REP *p, *q;

	for (q = &hrep, p = q->r_next; p != NULL; q = p, p = p->r_next)
		if (p->r_flags & R_ISCYCLE || p->r_thendo != NULL) {
			printchain(p);
			printf("%s%s : no chain copies allowed.\n",
				p->r_hto->h_name, p->r_nto);
			q->r_next = p->r_next;
			p = q;
		}
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

static void scandeletes(int (*pkilldel)(REP *))
{
	REP *p, *q, *n;

	for (q = &hrep, p = q->r_next; p != NULL; q = p, p = p->r_next) {
		if (p->r_fdel != NULL)
			while ((*pkilldel)(p)) {
				nreps--;
				p->r_ffrom->fi_rep = MISTAKE;
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
	return(!getreply("? ", -1));
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
		else if (!getreply(" Proceed with the rest? ", -1))
			exit(1);
	}
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
					p->r_flags & R_ISX ?
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
				fprintf(outfile, "%s %c%c %s%s%s\n",
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

static int snap(REP *first, REP *p)
{
	char fname[80];
	int redirected = 0;

	if (noex)
		exit(1);

	failed = 1;
	signal(SIGINT, breakstat);
	if (
		badstyle == ASKBAD &&
		isatty(fileno(stdout)) &&
		getreply("Redirect standard output to file? ", 0)
	) {
		redirected = 1;
		umask(oldumask);
		while (
			fprintf(stderr, "File name> "),
			(outfile = fopen(mygets(fname, 80), "w")) == NULL
		)
			fprintf(stderr, "Can't open %s.\n", fname);
	}
	if (redirected || !verbose)
		showdone(p);
	fprintf(outfile, "The following left undone:\n");
	noex = 1;
	return(first != p);
}

static void showdone(REP *fin)
{
	REP *first, *p;

	for (first = hrep.r_next; ; first = first->r_next)
		for (p = first; p != NULL; p = p->r_thendo) {
			if (p == fin)
				return;
			fprintf(outfile, "%s%s %c%c %s%s : done%s\n",
				p->r_hfrom->h_name, p->r_ffrom->fi_name,
				p->r_flags & R_ISALIASED ? '=' : '-',
				p->r_flags & R_ISCYCLE ? '^' : '>',
				p->r_hto->h_name, p->r_nto,
				(p->r_fdel != NULL && !(op & APPEND)) ? " (*)" : "");
		}
}

static void breakout(int signum _GL_UNUSED_PARAMETER)
{
	fflush(stdout);
	fprintf(stderr, "Aborting, nothing done.\n");
	exit(1);
}

static void breakrep(int signum _GL_UNUSED_PARAMETER)
{
	gotsig = 1;
	return;
}

static void breakstat(int signum _GL_UNUSED_PARAMETER)
{
	exit(1);
}

static void quit(void)
{
	fprintf(stderr, "Aborting, nothing done.\n");
	exit(1);
}

static int copymove(REP *p)
{
	return(copy(p->r_ffrom, -1L) || myunlink(pathbuf));
}



#define IRWMASK (S_IREAD | S_IWRITE)
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
	perm =
		(op & (APPEND | OVERWRITE)) ?
			(~oldumask & RWMASK) | (ff->fi_mode & (mode_t)~RWMASK) :
			ff->fi_mode
		;

	mode = O_CREAT | (op & APPEND ? 0 : O_TRUNC) |
		O_WRONLY
		;
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
		fprintf(stderr, "Strange, can not unlink %s.\n", n);
		return(-1);
	}
	return(0);
}

static int getreply(const char *m, int failact)
{
	static FILE *tty = NULL;
	int c, r;

	fprintf(stderr, "%s", m);
	if (tty == NULL && (tty = fopen(TTY, "r")) == NULL) {
		fprintf(stderr, "Can not open %s to get reply.\n", TTY);
		if (failact == -1)
			quit();
		else
			return(failact);
	}
	for (;;) {
		r = fgetc(tty);
		if (r == EOF) {
			fprintf(stderr, "Can not get reply.\n");
			if (failact == -1)
				quit();
			else
				return(failact);
		}
		if (r != '\n')
			while ((c = fgetc(tty)) != '\n' && c != EOF)
				;
		r = tolower(r);
		if (r == 'y' || r == 'n')
			return(r == 'y');
		fprintf(stderr, "Yes or No? ");
	}
}

static char *mygets(char *s, int l)
{
	char *nl;

	for (;;) {
		if (fgets(s, l, stdin) == NULL)
			return(NULL);
		if ((nl = strchr(s, '\n')) != NULL)
			break;
		fprintf(stderr, "Input string too long. Try again> ");
	}
	*nl = '\0';
	return(s);
}
