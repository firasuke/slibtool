/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2021  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#ifndef SLIBTOOL_DRIVER_IMPL_H
#define SLIBTOOL_DRIVER_IMPL_H

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <slibtool/slibtool.h>
#include "slibtool_dprintf_impl.h"
#include "argv/argv.h"

#define SLBT_OPTV_ELEMENTS 128

extern const struct argv_option slbt_default_options[];

enum app_tags {
	TAG_HELP,
	TAG_HELP_ALL,
	TAG_VERSION,
	TAG_CONFIG,
	TAG_DUMPMACHINE,
	TAG_DEBUG,
	TAG_DRY_RUN,
	TAG_FEATURES,
	TAG_LEGABITS,
	TAG_MODE,
	TAG_FINISH,
	TAG_WARNINGS,
	TAG_ANNOTATE,
	TAG_DEPS,
	TAG_SILENT,
	TAG_TAG,
	TAG_CCWRAP,
	TAG_VERBOSE,
	TAG_TARGET,
	TAG_HOST,
	TAG_FLAVOR,
	TAG_AR,
	TAG_RANLIB,
	TAG_WINDRES,
	TAG_DLLTOOL,
	TAG_MDSO,
	TAG_IMPLIB,
	TAG_OUTPUT,
	TAG_BINDIR,
	TAG_LDRPATH,
	TAG_SHREXT,
	TAG_RPATH,
	TAG_SYSROOT,
	TAG_RELEASE,
	TAG_OBJECTLIST,
	TAG_DLOPEN,
	TAG_DLPREOPEN,
	TAG_EXPORT_DYNAMIC,
	TAG_EXPSYM_FILE,
	TAG_EXPSYM_REGEX,
	TAG_VERSION_INFO,
	TAG_VERSION_NUMBER,
	TAG_NO_SUPPRESS,
	TAG_NO_INSTALL,
	TAG_PREFER_PIC,
	TAG_PREFER_NON_PIC,
	TAG_HEURISTICS,
	TAG_SHARED,
	TAG_STATIC,
	TAG_STATIC_LIBTOOL_LIBS,
	TAG_ALL_STATIC,
	TAG_DISABLE_STATIC,
	TAG_DISABLE_SHARED,
	TAG_NO_UNDEFINED,
	TAG_MODULE,
	TAG_AVOID_VERSION,
	TAG_COMPILER_FLAG,
	TAG_VERBATIM_FLAG,
	TAG_THREAD_SAFE,
	TAG_WEAK,
	/* ar mode */
	TAG_AR_HELP,
	TAG_AR_VERSION,
	TAG_AR_CHECK,
	TAG_AR_PRINT,
	TAG_AR_PRETTY,
	TAG_AR_VERBOSE,
};

struct slbt_split_vector {
	char *		dargs;
	char **		dargv;
	char **		targv;
	char **		cargv;
};

struct slbt_host_strs {
	char *		machine;
	char *		host;
	char *		flavor;
	char *		ar;
	char *		ranlib;
	char *		windres;
	char *		dlltool;
	char *		mdso;
};

struct slbt_obj_list {
	const char *	name;
	void *		addr;
	size_t		size;
	int		objc;
	char **		objv;
};

struct slbt_driver_ctx_impl {
	struct slbt_common_ctx	cctx;
	struct slbt_driver_ctx	ctx;
	struct slbt_host_strs	host;
	struct slbt_host_strs	ahost;
	struct slbt_fd_ctx	fdctx;
	struct slbt_obj_list *	objlistv;

	const struct slbt_archive_ctx * arctx;
	const char *                    arpath;

	char *			libname;
	char *			dargs;
	char **			dargv;
	char **			targv;
	char **			cargv;
	char **			envp;
	struct slbt_error_info**errinfp;
	struct slbt_error_info**erricap;
	struct slbt_error_info *erriptr[64];
	struct slbt_error_info	erribuf[64];
};

struct slbt_exec_ctx_impl {
	int			argc;
	char *			args;
	char *			shadow;
	char *			dsoprefix;
	size_t			size;
	struct slbt_exec_ctx	ctx;
	int			fdwrapper;
	char **			lout[2];
	char **			mout[2];
	char *			vbuffer[];
};

struct slbt_archive_ctx_impl {
	const struct slbt_driver_ctx *	dctx;
	const char *			path;
	struct slbt_raw_archive		map;
	struct slbt_archive_meta *	meta;
	struct slbt_archive_ctx		actx;
};

static inline struct slbt_archive_ctx_impl * slbt_get_archive_ictx(const struct slbt_archive_ctx * actx)
{
	uintptr_t addr;

	if (actx) {
		addr = (uintptr_t)actx - offsetof(struct slbt_archive_ctx_impl,actx);
		return (struct slbt_archive_ctx_impl *)addr;
	}

	return 0;
}

static inline struct slbt_driver_ctx_impl * slbt_get_driver_ictx(const struct slbt_driver_ctx * dctx)
{
	uintptr_t addr;

	if (dctx) {
		addr = (uintptr_t)dctx - offsetof(struct slbt_driver_ctx_impl,ctx);
		return (struct slbt_driver_ctx_impl *)addr;
	}

	return 0;
}

static inline void slbt_driver_set_arctx(
	const struct slbt_driver_ctx *  dctx,
	const struct slbt_archive_ctx * arctx,
	const char *                    arpath)
{
	struct slbt_driver_ctx_impl * ictx;


	ictx         = slbt_get_driver_ictx(dctx);
	ictx->arctx  = arctx;
	ictx->arpath = arpath;
}

static inline char ** slbt_driver_envp(const struct slbt_driver_ctx * dctx)
{
	struct slbt_driver_ctx_impl * ictx;
	ictx = slbt_get_driver_ictx(dctx);
	return ictx->envp;
}

static inline int slbt_driver_fdin(const struct slbt_driver_ctx * dctx)
{
	struct slbt_fd_ctx fdctx;
	slbt_get_driver_fdctx(dctx,&fdctx);
	return fdctx.fdin;
}

static inline int slbt_driver_fdout(const struct slbt_driver_ctx * dctx)
{
	struct slbt_fd_ctx fdctx;
	slbt_get_driver_fdctx(dctx,&fdctx);
	return fdctx.fdout;
}

static inline int slbt_driver_fderr(const struct slbt_driver_ctx * dctx)
{
	struct slbt_fd_ctx fdctx;
	slbt_get_driver_fdctx(dctx,&fdctx);
	return fdctx.fderr;
}

static inline int slbt_driver_fdlog(const struct slbt_driver_ctx * dctx)
{
	struct slbt_fd_ctx fdctx;
	slbt_get_driver_fdctx(dctx,&fdctx);
	return fdctx.fdlog;
}

static inline int slbt_driver_fdcwd(const struct slbt_driver_ctx * dctx)
{
	struct slbt_fd_ctx fdctx;
	slbt_get_driver_fdctx(dctx,&fdctx);
	return fdctx.fdcwd;
}

static inline int slbt_driver_fddst(const struct slbt_driver_ctx * dctx)
{
	struct slbt_fd_ctx fdctx;
	slbt_get_driver_fdctx(dctx,&fdctx);
	return fdctx.fddst;
}

static inline struct slbt_exec_ctx_impl * slbt_get_exec_ictx(const struct slbt_exec_ctx * ectx)
{
	uintptr_t addr;

	addr = (uintptr_t)ectx - offsetof(struct slbt_exec_ctx_impl,ctx);
	return (struct slbt_exec_ctx_impl *)addr;
}

static inline int slbt_exec_get_fdwrapper(const struct slbt_exec_ctx * ectx)
{
	struct slbt_exec_ctx_impl * ictx;
	ictx = slbt_get_exec_ictx(ectx);
	return ictx->fdwrapper;
}

static inline void slbt_exec_set_fdwrapper(const struct slbt_exec_ctx * ectx, int fd)
{
	struct slbt_exec_ctx_impl * ictx;
	ictx = slbt_get_exec_ictx(ectx);
	ictx->fdwrapper = fd;
}

static inline void slbt_exec_close_fdwrapper(const struct slbt_exec_ctx * ectx)
{
	struct slbt_exec_ctx_impl * ictx;
	ictx = slbt_get_exec_ictx(ectx);
	close(ictx->fdwrapper);
	ictx->fdwrapper = (-1);
}

#endif
