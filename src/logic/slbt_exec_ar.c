/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>
#include <slibtool/slibtool_output.h>
#include "slibtool_driver_impl.h"
#include "slibtool_ar_impl.h"
#include "slibtool_errinfo_impl.h"
#include "argv/argv.h"

#define SLBT_DRIVER_MODE_AR_ACTIONS     (SLBT_DRIVER_MODE_AR_CHECK)

#define SLBT_DRIVER_MODE_AR_OUTPUTS     (SLBT_OUTPUT_ARCHIVE_MEMBERS  \
	                                 | SLBT_OUTPUT_ARCHIVE_HEADERS \
	                                 | SLBT_OUTPUT_ARCHIVE_SYMBOLS  \
	                                 | SLBT_OUTPUT_ARCHIVE_ARMAPS)

#define SLBT_PRETTY_FLAGS               (SLBT_PRETTY_YAML      \
	                                 | SLBT_PRETTY_POSIX    \
	                                 | SLBT_PRETTY_HEXDATA)

static int slbt_ar_usage(
	int				fdout,
	const char *			program,
	const char *			arg,
	const struct argv_option **	optv,
	struct argv_meta *		meta,
	struct slbt_exec_ctx *		ectx,
	int				noclr)
{
	char		header[512];
	bool		armode;
	const char *	dash;

	armode = (dash = strrchr(program,'-'))
		&& !strcmp(++dash,"ar");

	snprintf(header,sizeof(header),
		"Usage: %s%s [options] [ARCHIVE-FILE] [ARCHIVE_FILE] ...\n"
		"Options:\n",
		program,
		armode ? "" : " --mode=ar");

	switch (noclr) {
		case 0:
			slbt_argv_usage(fdout,header,optv,arg);
			break;

		default:
			slbt_argv_usage_plain(fdout,header,optv,arg);
			break;
	}

	if (ectx)
		slbt_free_exec_ctx(ectx);

	slbt_argv_free(meta);

	return SLBT_USAGE;
}

static int slbt_exec_ar_fail(
	struct slbt_exec_ctx *	actx,
	struct argv_meta *	meta,
	int			ret)
{
	slbt_argv_free(meta);
	slbt_free_exec_ctx(actx);
	return ret;
}

static int slbt_exec_ar_perform_archive_actions(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_ctx **      arctxv)
{
	struct slbt_archive_ctx **      arctxp;

	for (arctxp=arctxv; *arctxp; arctxp++) {
		if (dctx->cctx->fmtflags & SLBT_DRIVER_MODE_AR_OUTPUTS)
			if (slbt_ar_output_arname(*arctxp) < 0)
				return SLBT_NESTED_ERROR(dctx);

		if (dctx->cctx->fmtflags & SLBT_OUTPUT_ARCHIVE_MEMBERS)
			if (slbt_ar_output_members((*arctxp)->meta) < 0)
				return SLBT_NESTED_ERROR(dctx);
	}

	return 0;
}

int slbt_exec_ar(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int				ret;
	int				fdout;
	int				fderr;
	char **				argv;
	char **				iargv;
	struct slbt_driver_ctx_impl *	ictx;
	const struct slbt_common_ctx *	cctx;
	struct slbt_archive_ctx **	arctxv;
	struct slbt_archive_ctx **	arctxp;
	const char **			unitv;
	const char **			unitp;
	size_t				nunits;
	struct slbt_exec_ctx *		actx;
	struct argv_meta *		meta;
	struct argv_entry *		entry;
	const struct argv_option *	optv[SLBT_OPTV_ELEMENTS];

	/* context */
	if (ectx)
		actx = 0;
	else if ((ret = slbt_get_exec_ctx(dctx,&ectx)))
		return ret;
	else
		actx = ectx;

	/* initial state, ar mode skin */
	slbt_reset_arguments(ectx);
	slbt_disable_placeholders(ectx);

	ictx  = slbt_get_driver_ictx(dctx);
	cctx  = dctx->cctx;
	iargv = ectx->cargv;

	fdout = slbt_driver_fdout(dctx);
	fderr = slbt_driver_fderr(dctx);

	/* missing arguments? */
	slbt_optv_init(slbt_ar_options,optv);

	if (!iargv[1] && (dctx->cctx->drvflags & SLBT_DRIVER_VERBOSITY_USAGE))
		return slbt_ar_usage(
			fdout,
			dctx->program,
			0,optv,0,actx,
			dctx->cctx->drvflags & SLBT_DRIVER_ANNOTATE_NEVER);

	/* <ar> argv meta */
	if (!(meta = slbt_argv_get(
			iargv,optv,
			dctx->cctx->drvflags & SLBT_DRIVER_VERBOSITY_ERRORS
				? ARGV_VERBOSITY_ERRORS
				: ARGV_VERBOSITY_NONE,
			fdout)))
		return slbt_exec_ar_fail(
			actx,meta,
			SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_AR_FAIL));

	/* dest, alternate argument vector options */
	argv    = ectx->altv;
	*argv++ = iargv[0];
	nunits  = 0;

	for (entry=meta->entries; entry->fopt || entry->arg; entry++) {
		if (entry->fopt) {
			switch (entry->tag) {
				case TAG_AR_HELP:
					slbt_ar_usage(
						fdout,
						dctx->program,
						0,optv,0,ectx,
						dctx->cctx->drvflags
							& SLBT_DRIVER_ANNOTATE_NEVER);

					ictx->cctx.drvflags |= SLBT_DRIVER_VERSION;
					ictx->cctx.drvflags ^= SLBT_DRIVER_VERSION;

					slbt_argv_free(meta);

					return SLBT_OK;

				case TAG_AR_VERSION:
					ictx->cctx.drvflags |= SLBT_DRIVER_VERSION;
					break;

				case TAG_AR_CHECK:
					ictx->cctx.drvflags |= SLBT_DRIVER_MODE_AR_CHECK;
					break;

				case TAG_AR_PRINT:
					if (!entry->arg)
						ictx->cctx.fmtflags |= SLBT_OUTPUT_ARCHIVE_MEMBERS;

					else if (!strcmp(entry->arg,"members"))
						ictx->cctx.fmtflags |= SLBT_OUTPUT_ARCHIVE_MEMBERS;

					else if (!strcmp(entry->arg,"headers"))
						ictx->cctx.fmtflags |= SLBT_OUTPUT_ARCHIVE_HEADERS;

					else if (!strcmp(entry->arg,"symbols"))
						ictx->cctx.fmtflags |= SLBT_OUTPUT_ARCHIVE_SYMBOLS;

					else if (!strcmp(entry->arg,"armaps"))
						ictx->cctx.fmtflags |= SLBT_OUTPUT_ARCHIVE_ARMAPS;

					break;

				case TAG_AR_PRETTY:
					if (!strcmp(entry->arg,"yaml")) {
						ictx->cctx.fmtflags &= ~(uint64_t)SLBT_PRETTY_FLAGS;
						ictx->cctx.fmtflags |= SLBT_PRETTY_YAML;

					} else if (!strcmp(entry->arg,"posix")) {
						ictx->cctx.fmtflags &= ~(uint64_t)SLBT_PRETTY_FLAGS;
						ictx->cctx.fmtflags |= SLBT_PRETTY_POSIX;
					}

					break;

				case TAG_AR_POSIX:
					ictx->cctx.fmtflags &= ~(uint64_t)SLBT_PRETTY_FLAGS;
					ictx->cctx.fmtflags |= SLBT_PRETTY_POSIX;
					break;

				case TAG_AR_YAML:
					ictx->cctx.fmtflags &= ~(uint64_t)SLBT_PRETTY_FLAGS;
					ictx->cctx.fmtflags |= SLBT_PRETTY_YAML;
					break;

				case TAG_AR_VERBOSE:
					ictx->cctx.fmtflags |= SLBT_PRETTY_VERBOSE;
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
		slbt_free_exec_ctx(actx);
		return SLBT_OK;
	}

	/* at least one action must be specified */
	if (cctx->fmtflags & SLBT_DRIVER_MODE_AR_OUTPUTS) {
		(void)0;

	} else if (!(cctx->drvflags & SLBT_DRIVER_MODE_AR_ACTIONS)) {
		if (cctx->drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
			slbt_dprintf(fderr,
				"%s: at least one action must be specified\n",
				dctx->program);

		return slbt_exec_ar_fail(
			actx,meta,
			SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_AR_NO_ACTION_SPECIFIED));
	}

	/* at least one unit must be specified */
	if (!nunits) {
		if (cctx->drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
			slbt_dprintf(fderr,
				"%s: all actions require at least one input unit\n",
				dctx->program);

		return slbt_exec_ar_fail(
			actx,meta,
			SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_AR_NO_INPUT_SPECIFIED));
	}

	/* archive vector allocation */
	if (!(arctxv = calloc(nunits+1,sizeof(struct slbt_archive_ctx *))))
		return slbt_exec_ar_fail(
			actx,meta,
			SLBT_SYSTEM_ERROR(dctx,0));

	/* unit vector allocation */
	if (!(unitv = calloc(nunits+1,sizeof(const char *)))) {
		free (arctxv);

		return slbt_exec_ar_fail(
			actx,meta,
			SLBT_SYSTEM_ERROR(dctx,0));
	}

	/* unit vector initialization */
	for (entry=meta->entries,unitp=unitv; entry->fopt || entry->arg; entry++)
		if (!entry->fopt)
			*unitp++ = entry->arg;

	/* archive context vector initialization */
	for (unitp=unitv,arctxp=arctxv; *unitp; unitp++,arctxp++) {
		if (slbt_get_archive_ctx(dctx,*unitp,arctxp) < 0) {
			for (arctxp=arctxv; *arctxp; arctxp++)
				slbt_free_archive_ctx(*arctxp);

			free(unitv);
			free(arctxv);

			return slbt_exec_ar_fail(
				actx,meta,
				SLBT_NESTED_ERROR(dctx));
		}
	}

	/* archive operations */
	ret = slbt_exec_ar_perform_archive_actions(dctx,arctxv);

	/* all done */
	for (arctxp=arctxv; *arctxp; arctxp++)
		slbt_free_archive_ctx(*arctxp);

	free(unitv);
	free(arctxv);

	slbt_argv_free(meta);
	slbt_free_exec_ctx(actx);

	return ret;
}
