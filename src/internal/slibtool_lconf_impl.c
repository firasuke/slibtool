
/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2021  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "slibtool_lconf_impl.h"
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_symlink_impl.h"
#include "slibtool_readlink_impl.h"

enum slbt_lconf_opt {
	SLBT_LCONF_OPT_UNKNOWN,
	SLBT_LCONF_OPT_NO,
	SLBT_LCONF_OPT_YES,
};

static const char aclr_reset[]   = "\x1b[0m";
static const char aclr_bold[]    = "\x1b[1m";

static const char aclr_red[]     = "\x1b[31m";
static const char aclr_green[]   = "\x1b[32m";
static const char aclr_yellow[]  = "\x1b[33m";
static const char aclr_blue[]    = "\x1b[34m";
static const char aclr_magenta[] = "\x1b[35m";

static void slbt_lconf_close(int fdcwd, int fdlconfdir)
{
	if (fdlconfdir != fdcwd)
		close(fdlconfdir);
}

static int slbt_lconf_trace_lconf_plain(
	struct slbt_driver_ctx *	dctx,
	const char *			lconf)
{
	int fderr = slbt_driver_fderr(dctx);

	if (slbt_dprintf(
			fderr,
			"%s: %s: {.name=%c%s%c}.\n",
			dctx->program,
			"lconf",
			'"',lconf,'"') < 0)
		return -1;

	return 0;
}

static int slbt_lconf_trace_lconf_annotated(
	struct slbt_driver_ctx *	dctx,
	const char *			lconf)
{
	int fderr = slbt_driver_fderr(dctx);

	if (slbt_dprintf(
			fderr,
			"%s%s%s%s: %s%s%s: {.name=%s%s%c%s%c%s}.\n",

			aclr_bold,aclr_magenta,
			dctx->program,
			aclr_reset,

			aclr_bold,
			"lconf",
			aclr_reset,

			aclr_bold,aclr_green,
			'"',lconf,'"',
			aclr_reset) < 0)
		return -1;

	return 0;
}

static int slbt_lconf_trace_openat_silent(
	struct slbt_driver_ctx *	dctx,
	int				fdat,
	const char *			path,
	int				oflag,
	int				mode)
{
	(void)dctx;
	return openat(fdat,path,oflag,mode);
}

static int slbt_lconf_trace_openat_plain(
	struct slbt_driver_ctx *	dctx,
	int				fdat,
	const char *			path,
	int				oflag,
	int				mode)
{
	char scwd[20];
	char serr[512];

	int  ret   = openat(fdat,path,oflag,mode);
	int  fderr = slbt_driver_fderr(dctx);

	if (fdat == AT_FDCWD) {
		strcpy(scwd,"AT_FDCWD");
	} else {
		sprintf(scwd,"%d",fdat);
	}

	if ((ret < 0) && (errno == ENOENT)) {
		strcpy(serr," [ENOENT]");
	} else if (ret < 0) {
		memset(serr,0,sizeof(serr));
		strerror_r(errno,&serr[2],sizeof(serr)-4);
		serr[0] = ' ';
		serr[1] = '(';
		serr[strlen(serr)] = ')';
	} else {
		serr[0] = 0;
	}

	slbt_dprintf(
		fderr,
		"%s: %s: openat(%s,%c%s%c,%s,%d) = %d%s.\n",
		dctx->program,
		"lconf",
		scwd,
		'"',path,'"',
		(oflag == O_DIRECTORY) ? "O_DIRECTORY" : "O_RDONLY",
		mode,ret,serr);

	return ret;
}

static int slbt_lconf_trace_openat_annotated(
	struct slbt_driver_ctx *	dctx,
	int				fdat,
	const char *			path,
	int				oflag,
	int				mode)
{
	char scwd[20];
	char serr[512];

	int  ret   = openat(fdat,path,oflag,mode);
	int  fderr = slbt_driver_fderr(dctx);

	if (fdat == AT_FDCWD) {
		strcpy(scwd,"AT_FDCWD");
	} else {
		sprintf(scwd,"%d",fdat);
	}

	if ((ret < 0) && (errno == ENOENT)) {
		strcpy(serr," [ENOENT]");
	} else if (ret < 0) {
		memset(serr,0,sizeof(serr));
		strerror_r(errno,&serr[2],sizeof(serr)-4);
		serr[0] = ' ';
		serr[1] = '(';
		serr[strlen(serr)] = ')';
	} else {
		serr[0] = 0;
	}

	slbt_dprintf(
		fderr,
		"%s%s%s%s: %s%s%s: openat(%s%s%s%s,%s%s%c%s%c%s,%s%s%s%s,%d) = %s%d%s%s%s%s%s.\n",

		aclr_bold,aclr_magenta,
		dctx->program,
		aclr_reset,

		aclr_bold,
		"lconf",
		aclr_reset,

		aclr_bold,aclr_blue,
		scwd,
		aclr_reset,

		aclr_bold,aclr_green,
		'"',path,'"',
		aclr_reset,

		aclr_bold,aclr_blue,
		(oflag == O_DIRECTORY) ? "O_DIRECTORY" : "O_RDONLY",
		aclr_reset,

		mode,

		aclr_bold,
		ret,
		aclr_reset,

		aclr_bold,aclr_red,
		serr,
		aclr_reset);

	return ret;
}

static int slbt_lconf_trace_fstat_silent(
	struct slbt_driver_ctx *	dctx,
	int				fd,
	const char *			path,
	struct stat *			st)
{
	(void)dctx;

	return path ? fstatat(fd,path,st,0) : fstat(fd,st);
}

static int slbt_lconf_trace_fstat_plain(
	struct slbt_driver_ctx *	dctx,
	int				fd,
	const char *			path,
	struct stat *			st)
{
	char scwd[20];
	char serr[512];
	char quot[2] = {'"',0};

	int  ret   = path ? fstatat(fd,path,st,0) : fstat(fd,st);
	int  fderr = slbt_driver_fderr(dctx);

	if (fd == AT_FDCWD) {
		strcpy(scwd,"AT_FDCWD");
	} else {
		sprintf(scwd,"%d",fd);
	}

	if ((ret < 0) && (errno == ENOENT)) {
		strcpy(serr," [ENOENT]");
	} else if (ret < 0) {
		memset(serr,0,sizeof(serr));
		strerror_r(errno,&serr[2],sizeof(serr)-4);
		serr[0] = ' ';
		serr[1] = '(';
		serr[strlen(serr)] = ')';
	} else {
		serr[0] = 0;
	}

	slbt_dprintf(
		fderr,
		"%s: %s: %s(%s%s%s%s%s,...) = %d%s%s",
		dctx->program,
		"lconf",
		path ? "fstatat" : "fstat",
		scwd,
		path ? "," : "",
		path ? quot : "",
		path ? path : "",
		path ? quot : "",
		ret,
		serr,
		ret ? ".\n" : "");

	if (ret == 0)
		slbt_dprintf(
			fderr,
			" {.st_dev = %ld, .st_ino = %ld}.\n",
			st->st_dev,
			st->st_ino);

	return ret;
}

static int slbt_lconf_trace_fstat_annotated(
	struct slbt_driver_ctx *	dctx,
	int				fd,
	const char *			path,
	struct stat *			st)
{
	char scwd[20];
	char serr[512];
	char quot[2] = {'"',0};

	int  ret   = path ? fstatat(fd,path,st,0) : fstat(fd,st);
	int  fderr = slbt_driver_fderr(dctx);

	if (fd == AT_FDCWD) {
		strcpy(scwd,"AT_FDCWD");
	} else {
		sprintf(scwd,"%d",fd);
	}

	if ((ret < 0) && (errno == ENOENT)) {
		strcpy(serr," [ENOENT]");
	} else if (ret < 0) {
		memset(serr,0,sizeof(serr));
		strerror_r(errno,&serr[2],sizeof(serr)-4);
		serr[0] = ' ';
		serr[1] = '(';
		serr[strlen(serr)] = ')';
	} else {
		serr[0] = 0;
	}

	slbt_dprintf(
		fderr,
		"%s%s%s%s: %s%s%s: %s(%s%s%s%s%s%s%s%s%s%s%s,...) = %s%d%s%s%s%s%s%s",

		aclr_bold,aclr_magenta,
		dctx->program,
		aclr_reset,

		aclr_bold,
		"lconf",
		aclr_reset,

		path ? "fstatat" : "fstat",

		aclr_bold,aclr_blue,
		scwd,
		aclr_reset,

		aclr_bold,aclr_green,
		path ? "," : "",
		path ? quot : "",
		path ? path : "",
		path ? quot : "",
		aclr_reset,

		aclr_bold,
		ret,
		aclr_reset,

		aclr_bold,aclr_red,
		serr,
		aclr_reset,

		ret ? ".\n" : "");

	if (ret == 0)
		slbt_dprintf(
			fderr,
			" {%s%s.st_dev%s = %s%ld%s, %s%s.st_ino%s = %s%ld%s}.\n",

			aclr_bold,aclr_yellow,aclr_reset,

			aclr_bold,
			st->st_dev,
			aclr_reset,

			aclr_bold,aclr_yellow,aclr_reset,

			aclr_bold,
			st->st_ino,
			aclr_reset);

	return ret;
}

static int slbt_lconf_trace_result_silent(
	struct slbt_driver_ctx *	dctx,
	int				fd,
	int				fdat,
	const char *			lconf,
	int				err)
{
	(void)dctx;
	(void)fd;
	(void)fdat;
	(void)lconf;
	return err ? (-1) : fd;
}

static int slbt_lconf_trace_result_plain(
	struct slbt_driver_ctx *	dctx,
	int				fd,
	int				fdat,
	const char *			lconf,
	int				err)
{
	int             fderr;
	const char *    cpath;
	char            path[PATH_MAX];

	fderr = slbt_driver_fderr(dctx);

	cpath = !(slbt_realpath(fdat,lconf,0,path,sizeof(path)))
		? path : lconf;

	switch (err) {
		case 0:
			slbt_dprintf(
				fderr,
				"%s: %s: found %c%s%c.\n",
				dctx->program,
				"lconf",
				'"',cpath,'"');
			return fd;

		case EXDEV:
			slbt_dprintf(
				fderr,
				"%s: %s: stopped in %c%s%c "
				"(config file not found on current device).\n",
				dctx->program,
				"lconf",
				'"',cpath,'"');
			return -1;

		default:
			slbt_dprintf(
				fderr,
				"%s: %s: stopped in %c%s%c "
				"(top-level directory reached).\n",
				dctx->program,
				"lconf",
				'"',cpath,'"');
			return -1;
	}
}

static int slbt_lconf_trace_result_annotated(
	struct slbt_driver_ctx *	dctx,
	int				fd,
	int				fdat,
	const char *			lconf,
	int				err)
{
	int             fderr;
	const char *    cpath;
	char            path[PATH_MAX];

	fderr = slbt_driver_fderr(dctx);

	cpath = !(slbt_realpath(fdat,lconf,0,path,sizeof(path)))
		? path : lconf;

	switch (err) {
		case 0:
			slbt_dprintf(
				fderr,
				"%s%s%s%s: %s%s%s: found %s%s%c%s%c%s.\n",

				aclr_bold,aclr_magenta,
				dctx->program,
				aclr_reset,

				aclr_bold,
				"lconf",
				aclr_reset,

				aclr_bold,aclr_green,
				'"',cpath,'"',
				aclr_reset);
			return fd;

		case EXDEV:
			slbt_dprintf(
				fderr,
				"%s%s%s%s: %s%s%s: stopped in %s%s%c%s%c%s "
				"%s%s(config file not found on current device)%s.\n",

				aclr_bold,aclr_magenta,
				dctx->program,
				aclr_reset,

				aclr_bold,
				"lconf",
				aclr_reset,

				aclr_bold,aclr_green,
				'"',cpath,'"',
				aclr_reset,

				aclr_bold,aclr_red,
				aclr_reset);
			return -1;

		default:
			slbt_dprintf(
				fderr,
				"%s%s%s%s: %s%s%s: stopped in %s%s%c%s%c%s "
				"%s%s(top-level directory reached)%s.\n",

				aclr_bold,aclr_magenta,
				dctx->program,
				aclr_reset,

				aclr_bold,
				"lconf",
				aclr_reset,

				aclr_bold,aclr_green,
				'"',cpath,'"',
				aclr_reset,

				aclr_bold,aclr_red,
				aclr_reset);
			return -1;
	}
}

static int slbt_lconf_open(
	struct slbt_driver_ctx *	dctx,
	const char *			lconf)
{
	int		fderr;
	int		fdcwd;
	int		fdlconf;
	int		fdlconfdir;
	int		fdparent;
	struct stat	stcwd;
	struct stat	stparent;
	ino_t		stinode;

	int             (*trace_lconf)(struct slbt_driver_ctx *,
	                                const char *);

	int             (*trace_fstat)(struct slbt_driver_ctx *,
	                                int,const char *, struct stat *);

	int             (*trace_openat)(struct slbt_driver_ctx *,
	                                int,const char *,int,int);

	int             (*trace_result)(struct slbt_driver_ctx *,
	                                int,int,const char *,int);

	lconf      = lconf ? lconf : "libtool";
	fderr      = slbt_driver_fderr(dctx);
	fdcwd      = slbt_driver_fdcwd(dctx);
	fdlconfdir = fdcwd;

	if (dctx->cctx->drvflags & SLBT_DRIVER_SILENT) {
		trace_lconf  = 0;
		trace_fstat  = slbt_lconf_trace_fstat_silent;
		trace_openat = slbt_lconf_trace_openat_silent;
		trace_result = slbt_lconf_trace_result_silent;

	} else if (dctx->cctx->drvflags & SLBT_DRIVER_ANNOTATE_NEVER) {
		trace_lconf  = slbt_lconf_trace_lconf_plain;
		trace_fstat  = slbt_lconf_trace_fstat_plain;
		trace_openat = slbt_lconf_trace_openat_plain;
		trace_result = slbt_lconf_trace_result_plain;

	} else if (dctx->cctx->drvflags & SLBT_DRIVER_ANNOTATE_ALWAYS) {
		trace_lconf  = slbt_lconf_trace_lconf_annotated;
		trace_fstat  = slbt_lconf_trace_fstat_annotated;
		trace_openat = slbt_lconf_trace_openat_annotated;
		trace_result = slbt_lconf_trace_result_annotated;

	} else if (isatty(fderr)) {
		trace_lconf  = slbt_lconf_trace_lconf_annotated;
		trace_fstat  = slbt_lconf_trace_fstat_annotated;
		trace_openat = slbt_lconf_trace_openat_annotated;
		trace_result = slbt_lconf_trace_result_annotated;

	} else {
		trace_lconf  = slbt_lconf_trace_lconf_plain;
		trace_fstat  = slbt_lconf_trace_fstat_plain;
		trace_openat = slbt_lconf_trace_openat_plain;
		trace_result = slbt_lconf_trace_result_plain;
	}

	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT)) {
		trace_lconf(dctx,lconf);
		slbt_output_fdcwd(dctx);
	}

	if (lconf && strchr(lconf,'/'))
		return ((fdlconf = trace_openat(dctx,fdcwd,lconf,O_RDONLY,0)) < 0)
			? SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LCONF_OPEN)
			: trace_result(dctx,fdlconf,fdcwd,lconf,0);

	if (trace_fstat(dctx,fdlconfdir,".",&stcwd) < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	stinode = stcwd.st_ino;
	fdlconf = trace_openat(dctx,fdlconfdir,lconf,O_RDONLY,0);

	while (fdlconf < 0) {
		fdparent = trace_openat(dctx,fdlconfdir,"../",O_DIRECTORY,0);
		slbt_lconf_close(fdcwd,fdlconfdir);

		if (fdparent < 0)
			return SLBT_SYSTEM_ERROR(dctx,0);

		if (trace_fstat(dctx,fdparent,0,&stparent) < 0) {
			close(fdparent);
			return SLBT_SYSTEM_ERROR(dctx,0);
		}

		if (stparent.st_dev != stcwd.st_dev) {
			trace_result(dctx,fdparent,fdparent,".",EXDEV);
			close(fdparent);
			return SLBT_CUSTOM_ERROR(
				dctx,SLBT_ERR_LCONF_OPEN);
		}

		if (stparent.st_ino == stinode) {
			trace_result(dctx,fdparent,fdparent,".",ELOOP);
			close(fdparent);
			return SLBT_CUSTOM_ERROR(
				dctx,SLBT_ERR_LCONF_OPEN);
		}

		fdlconfdir = fdparent;
		fdlconf    = trace_openat(dctx,fdlconfdir,lconf,O_RDONLY,0);
		stinode    = stparent.st_ino;
	}

	trace_result(dctx,fdlconf,fdlconfdir,lconf,0);

	slbt_lconf_close(fdcwd,fdlconfdir);

	return fdlconf;
}

int slbt_get_lconf_flags(
	struct slbt_driver_ctx *	dctx,
	const char *			lconf,
	uint64_t *			flags)
{
	int				fdlconf;
	struct stat			st;
	void *				addr;
	const char *			mark;
	const char *			cap;
	uint64_t			optshared;
	uint64_t			optstatic;
	int				optsharedlen;
	int				optstaticlen;
	const char *			optsharedstr;
	const char *			optstaticstr;

	/* open relative libtool script */
	if ((fdlconf = slbt_lconf_open(dctx,lconf)) < 0)
		return SLBT_NESTED_ERROR(dctx);

	/* map relative libtool script */
	if (fstat(fdlconf,&st) < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	addr = mmap(
		0,st.st_size,
		PROT_READ,MAP_SHARED,
		fdlconf,0);

	close(fdlconf);

	if (addr == MAP_FAILED)
		return SLBT_CUSTOM_ERROR(
			dctx,SLBT_ERR_LCONF_MAP);

	mark = addr;
	cap  = &mark[st.st_size];

	/* scan */
	optshared = 0;
	optstatic = 0;

	optsharedstr = "build_libtool_libs=";
	optstaticstr = "build_old_libs=";

	optsharedlen = strlen(optsharedstr);
	optstaticlen = strlen(optstaticstr);

	for (; mark && mark<cap; ) {
		if (!optshared && (cap - mark < optsharedlen)) {
			mark = 0;

		} else if (!optstatic && (cap - mark < optstaticlen)) {
			mark = 0;

		} else if (!optshared && !strncmp(mark,optsharedstr,optsharedlen)) {
			mark += optsharedlen;

			if ((cap - mark >= 3)
					&& (mark[0]=='n')
					&& (mark[1]=='o')
					&& (mark[2]=='\n')
					&& (mark = &mark[3]))
				optshared = SLBT_DRIVER_DISABLE_SHARED;

			else if ((cap - mark >= 4)
					&& (mark[0]=='y')
					&& (mark[1]=='e')
					&& (mark[2]=='s')
					&& (mark[3]=='\n')
					&& (mark = &mark[4]))
				optshared = SLBT_DRIVER_SHARED;

			if (!optshared)
				mark--;

		} else if (!optstatic && !strncmp(mark,optstaticstr,optstaticlen)) {
			mark += optstaticlen;

			if ((cap - mark >= 3)
					&& (mark[0]=='n')
					&& (mark[1]=='o')
					&& (mark[2]=='\n')
					&& (mark = &mark[3]))
				optstatic = SLBT_DRIVER_DISABLE_STATIC;

			else if ((cap - mark >= 4)
					&& (mark[0]=='y')
					&& (mark[1]=='e')
					&& (mark[2]=='s')
					&& (mark[3]=='\n')
					&& (mark = &mark[4]))
				optstatic = SLBT_DRIVER_STATIC;

			if (!optstatic)
				mark--;
		} else {
			for (; (mark<cap) && (*mark!='\n'); )
				mark++;
			mark++;
		}

		if (optshared && optstatic)
			mark = 0;
	}

	munmap(addr,st.st_size);

	if (!optshared || !optstatic)
		return SLBT_CUSTOM_ERROR(
			dctx,SLBT_ERR_LCONF_PARSE);

	*flags = optshared | optstatic;

	return 0;
}
