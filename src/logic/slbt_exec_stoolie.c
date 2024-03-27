/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <sys/stat.h>
#include <slibtool/slibtool.h>
#include <slibtool/slibtool_output.h>
#include "slibtool_driver_impl.h"
#include "slibtool_stoolie_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_realpath_impl.h"
#include "slibtool_snprintf_impl.h"
#include "slibtool_symlink_impl.h"
#include "argv/argv.h"

static const char slbt_this_dir[2] = {'.',0};

static int slbt_stoolie_usage(
	int				fdout,
	const char *			program,
	const char *			arg,
	const struct argv_option **	optv,
	struct argv_meta *		meta,
	struct slbt_exec_ctx *		ectx,
	int				noclr)
{
	char    header[512];
	bool    stooliemode;

	stooliemode = !strcmp(program,"slibtoolize");

	snprintf(header,sizeof(header),
		"Usage: %s%s [options] ...\n"
		"Options:\n",
		program,
		stooliemode ? "" : " --mode=slibtoolize");

	switch (noclr) {
		case 0:
			slbt_argv_usage(fdout,header,optv,arg);
			break;

		default:
			slbt_argv_usage_plain(fdout,header,optv,arg);
			break;
	}

	if (ectx)
		slbt_ectx_free_exec_ctx(ectx);

	slbt_argv_free(meta);

	return SLBT_USAGE;
}

static int slbt_exec_stoolie_fail(
	struct slbt_exec_ctx *	ectx,
	struct argv_meta *	meta,
	int			ret)
{
	slbt_argv_free(meta);
	slbt_ectx_free_exec_ctx(ectx);
	return ret;
}

static int slbt_exec_stoolie_remove_file(
	const struct slbt_driver_ctx *	dctx,
	int                             fddst,
	const char *			target)
{
	/* remove target (if any) */
	if (!unlinkat(fddst,target,0) || (errno == ENOENT))
		return 0;

	return SLBT_SYSTEM_ERROR(dctx,0);
}

static int slbt_exec_stoolie_perform_actions(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_exec_ctx *          ectx,
	struct slbt_stoolie_ctx *       stctx)
{
	struct slbt_stoolie_ctx_impl *  ictx;
	struct stat                     st;
	char                            m4dir [PATH_MAX];
	char                            auxdir[PATH_MAX];
	char                            slibm4[PATH_MAX];
	char                            ltmain[PATH_MAX];
	bool                            fslibm4;
	bool                            fltmain;

	ictx = slbt_get_stoolie_ictx(stctx);

	/* source files */
	if (slbt_snprintf(
			slibm4,sizeof(slibm4),"%s/%s",
			SLBT_PACKAGE_DATADIR,
			"slibtool.m4") < 0)
		return SLBT_BUFFER_ERROR(dctx);

	if (slbt_snprintf(
			ltmain,sizeof(slibm4),"%s/%s",
			SLBT_PACKAGE_DATADIR,
			"ltmain.sh") < 0)
		return SLBT_BUFFER_ERROR(dctx);

	/* --force? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_STOOLIE_FORCE) {
		if (ictx->fdm4 >= 0)
			if (slbt_exec_stoolie_remove_file(dctx,ictx->fdm4,"slibtool.m4") < 0)
				return SLBT_NESTED_ERROR(dctx);

		if (slbt_exec_stoolie_remove_file(dctx,ictx->fdaux,"ltmain.sh") < 0)
			return SLBT_NESTED_ERROR(dctx);

		fslibm4 = (ictx->fdm4 >= 0);
		fltmain = true;
	} else {
		if (ictx->fdm4 < 0) {
			fslibm4 = false;

		} else if (fstatat(ictx->fdm4,"slibtool.m4",&st,AT_SYMLINK_NOFOLLOW) == 0) {
			fslibm4 = false;

		} else if (errno == ENOENT) {
			fslibm4 = true;

		} else {
			return SLBT_SYSTEM_ERROR(dctx,"slibtool.m4");
		}

		if (fstatat(ictx->fdaux,"ltmain.sh",&st,AT_SYMLINK_NOFOLLOW) == 0) {
			fltmain = false;

		} else if (errno == ENOENT) {
			fltmain = true;

		} else {
			return SLBT_SYSTEM_ERROR(dctx,"ltmain.sh");
		}
	}

	/* --copy? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_STOOLIE_COPY) {
		if (fslibm4) {
			if (slbt_realpath(ictx->fdm4,".",0,m4dir,sizeof(m4dir)) < 0)
				return SLBT_SYSTEM_ERROR(dctx,0);

			if (slbt_util_copy_file(ectx,slibm4,m4dir) < 0)
				return SLBT_NESTED_ERROR(dctx);
		}

		if (fltmain) {
			if (slbt_realpath(ictx->fdaux,".",0,auxdir,sizeof(auxdir)) < 0)
				return SLBT_SYSTEM_ERROR(dctx,0);

			if (slbt_util_copy_file(ectx,ltmain,auxdir) < 0)
				return SLBT_NESTED_ERROR(dctx);
		}
	} else {
		/* default to symlinks */
		if (fslibm4)
			if (slbt_create_symlink_ex(
					dctx,ectx,
					ictx->fdm4,
					slibm4,
					"slibtool.m4",
					SLBT_SYMLINK_LITERAL) < 0)
				return SLBT_NESTED_ERROR(dctx);

		if (fltmain)
			if (slbt_create_symlink_ex(
					dctx,ectx,
					ictx->fdaux,
					ltmain,
					"ltmain.sh",
					SLBT_SYMLINK_LITERAL) < 0)
				return SLBT_NESTED_ERROR(dctx);
	}

	return 0;
}

int slbt_exec_stoolie(const struct slbt_driver_ctx * dctx)
{
	int				ret;
	int				fdout;
	int				fderr;
	char **				argv;
	char **				iargv;
	struct slbt_exec_ctx *		ectx;
	struct slbt_driver_ctx_impl *	ictx;
	const struct slbt_common_ctx *	cctx;
	struct argv_meta *		meta;
	struct argv_entry *		entry;
	size_t				nunits;
	size_t                          cunits;
	const char **                   unitv;
	const char **                   unitp;
	struct slbt_stoolie_ctx **      stctxv;
	struct slbt_stoolie_ctx **      stctxp;
	const struct argv_option *	optv[SLBT_OPTV_ELEMENTS];

	/* context */
	if (slbt_ectx_get_exec_ctx(dctx,&ectx) < 0)
		return SLBT_NESTED_ERROR(dctx);

	/* initial state, slibtoolize (stoolie) mode skin */
	slbt_ectx_reset_arguments(ectx);
	slbt_disable_placeholders(ectx);

	ictx  = slbt_get_driver_ictx(dctx);
	cctx  = dctx->cctx;
	iargv = ectx->cargv;

	fdout = slbt_driver_fdout(dctx);
	fderr = slbt_driver_fderr(dctx);

	(void)fderr;

	/* <stoolie> argv meta */
	slbt_optv_init(slbt_stoolie_options,optv);

	if (!(meta = slbt_argv_get(
			iargv,optv,
			dctx->cctx->drvflags & SLBT_DRIVER_VERBOSITY_ERRORS
				? ARGV_VERBOSITY_ERRORS
				: ARGV_VERBOSITY_NONE,
			fdout)))
		return slbt_exec_stoolie_fail(
			ectx,meta,
			SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_AR_FAIL));

	/* dest, alternate argument vector options */
	argv    = ectx->altv;
	*argv++ = iargv[0];
	nunits  = 0;

	for (entry=meta->entries; entry->fopt || entry->arg; entry++) {
		if (entry->fopt) {
			switch (entry->tag) {
				case TAG_STLE_HELP:
					slbt_stoolie_usage(
						fdout,
						dctx->program,
						0,optv,0,ectx,
						dctx->cctx->drvflags
							& SLBT_DRIVER_ANNOTATE_NEVER);

					ictx->cctx.drvflags |= SLBT_DRIVER_VERSION;
					ictx->cctx.drvflags ^= SLBT_DRIVER_VERSION;

					slbt_argv_free(meta);

					return SLBT_OK;

				case TAG_STLE_VERSION:
					ictx->cctx.drvflags |= SLBT_DRIVER_VERSION;
					break;

				case TAG_STLE_COPY:
					ictx->cctx.drvflags |= SLBT_DRIVER_STOOLIE_COPY;
					break;

				case TAG_STLE_FORCE:
					ictx->cctx.drvflags |= SLBT_DRIVER_STOOLIE_FORCE;
					break;

				case TAG_STLE_INSTALL:
					ictx->cctx.drvflags |= SLBT_DRIVER_STOOLIE_INSTALL;
					break;

				case TAG_STLE_DEBUG:
					ictx->cctx.drvflags |= SLBT_DRIVER_DEBUG;
					break;

				case TAG_STLE_DRY_RUN:
					ictx->cctx.drvflags |= SLBT_DRIVER_DRY_RUN;
					break;

				case TAG_STLE_SILENT:
					ictx->cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_VERBOSE;
					ictx->cctx.drvflags |= SLBT_DRIVER_SILENT;
					break;

				case TAG_STLE_VERBOSE:
					ictx->cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_SILENT;
					ictx->cctx.drvflags |= SLBT_DRIVER_VERBOSE;
					break;
			}

			if (entry->fval) {
				*argv++ = (char *)entry->arg;
			}
		} else {
			nunits++;
		};
	}

	/* defer --version printing to slbt_main() as needed */
	if (cctx->drvflags & SLBT_DRIVER_VERSION) {
		slbt_argv_free(meta);
		slbt_ectx_free_exec_ctx(ectx);
		return SLBT_OK;
	}

	/* default to this-dir as needed */
	if (!(cunits = nunits))
		nunits++;

	/* slibtoolize target directory vector allocation */
	if (!(stctxv = calloc(nunits+1,sizeof(struct slbt_stoolie_ctx *))))
		return slbt_exec_stoolie_fail(
			ectx,meta,
			SLBT_SYSTEM_ERROR(dctx,0));

	/* unit vector allocation */
	if (!(unitv = calloc(nunits+1,sizeof(const char *)))) {
		free (stctxv);

		return slbt_exec_stoolie_fail(
			ectx,meta,
			SLBT_SYSTEM_ERROR(dctx,0));
	}

	/* unit vector initialization */
	for (entry=meta->entries,unitp=unitv; entry->fopt || entry->arg; entry++)
		if (!entry->fopt)
			*unitp++ = entry->arg;

	if (!cunits)
		unitp[0] = slbt_this_dir;

	/* slibtoolize target directory vector initialization */
	for (unitp=unitv,stctxp=stctxv; *unitp; unitp++,stctxp++) {
		if (slbt_st_get_stoolie_ctx(dctx,*unitp,stctxp) < 0) {
			for (stctxp=stctxv; *stctxp; stctxp++)
				slbt_st_free_stoolie_ctx(*stctxp);

			free(unitv);
			free(stctxv);

			return slbt_exec_stoolie_fail(
				ectx,meta,
				SLBT_NESTED_ERROR(dctx));
		}
	}

	/* slibtoolize operations */
	for (ret=0,stctxp=stctxv; !ret && *stctxp; stctxp++)
		ret = slbt_exec_stoolie_perform_actions(dctx,ectx,*stctxp);

	/* all done */
	for (stctxp=stctxv; *stctxp; stctxp++)
		slbt_st_free_stoolie_ctx(*stctxp);

	free(unitv);
	free(stctxv);

	slbt_argv_free(meta);
	slbt_ectx_free_exec_ctx(ectx);

	return ret;
}

int slbt_exec_slibtoolize(const struct slbt_driver_ctx * dctx)
{
	return slbt_exec_stoolie(dctx);
}
