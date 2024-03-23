/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>
#include <slibtool/slibtool_output.h>
#include "slibtool_driver_impl.h"
#include "slibtool_stoolie_impl.h"
#include "slibtool_errinfo_impl.h"
#include "argv/argv.h"

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

static int slbt_exec_stoolie_perform_actions(
	const struct slbt_driver_ctx *  dctx)
{
	(void)dctx;
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

	/* missing arguments? */
	slbt_optv_init(slbt_stoolie_options,optv);

	if (!iargv[1] && (dctx->cctx->drvflags & SLBT_DRIVER_VERBOSITY_USAGE))
		return slbt_stoolie_usage(
			fdout,
			dctx->program,
			0,optv,0,ectx,
			dctx->cctx->drvflags & SLBT_DRIVER_ANNOTATE_NEVER);

	/* <stoolie> argv meta */
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

	/* slibtoolize operations */
	ret = slbt_exec_stoolie_perform_actions(dctx);

	/* all done */
	slbt_argv_free(meta);
	slbt_ectx_free_exec_ctx(ectx);

	return ret;
}

int slbt_exec_slibtoolize(const struct slbt_driver_ctx * dctx)
{
	return slbt_exec_stoolie(dctx);
}
