/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <time.h>
#include <locale.h>
#include <regex.h>
#include <inttypes.h>
#include <slibtool/slibtool.h>
#include <slibtool/slibtool_output.h>
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_ar_impl.h"

#define SLBT_PRETTY_FLAGS       (SLBT_PRETTY_YAML      \
	                         | SLBT_PRETTY_POSIX    \
	                         | SLBT_PRETTY_HEXDATA)

static int slbt_au_output_symbols_posix(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * mctx,
	const struct slbt_fd_ctx *      fdctx)
{
	int             fdout;
	bool            fsort;
	bool            fcoff;
	const char *    regex;
	const char **   symv;
	const char **   symstrv;
	regex_t         regctx;
	regmatch_t      pmatch[2] = {{0,0},{0,0}};

	fdout = fdctx->fdout;
	fsort = !(dctx->cctx->fmtflags & SLBT_OUTPUT_ARCHIVE_NOSORT);
	fcoff = (mctx->ofmtattr & AR_OBJECT_ATTR_COFF);

	if (fsort && !mctx->mapstrv)
		if (slbt_update_mapstrv(dctx,mctx) < 0)
			return SLBT_NESTED_ERROR(dctx);

	if ((regex = dctx->cctx->regex))
		if (regcomp(&regctx,regex,REG_NEWLINE))
			return SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_FLOW_ERROR);

	symstrv = fsort ? mctx->mapstrv : mctx->symstrv;

	for (symv=symstrv; *symv; symv++)
		if (!fcoff || strncmp(*symv,"__imp_",6))
			if (!regex || !regexec(&regctx,*symv,1,pmatch,0))
				if (slbt_dprintf(fdout,"%s\n",*symv) < 0)
					return SLBT_SYSTEM_ERROR(dctx,0);

	if (regex)
		regfree(&regctx);

	return 0;
}

static int slbt_au_output_symbols_yaml(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * mctx,
	const struct slbt_fd_ctx *      fdctx)
{
	(void)dctx;
	(void)mctx;
	(void)fdctx;

	return 0;
}

int slbt_au_output_symbols(const struct slbt_archive_meta * meta)
{
	struct slbt_archive_meta_impl * mctx;
	const struct slbt_driver_ctx *  dctx;
	struct slbt_fd_ctx              fdctx;

	mctx = slbt_archive_meta_ictx(meta);
	dctx = (slbt_archive_meta_ictx(meta))->dctx;

	if (slbt_lib_get_driver_fdctx(dctx,&fdctx) < 0)
		return SLBT_NESTED_ERROR(dctx);

	if (!meta->a_memberv)
		return 0;

	switch (dctx->cctx->fmtflags & SLBT_PRETTY_FLAGS) {
		case SLBT_PRETTY_YAML:
			return slbt_au_output_symbols_yaml(
				dctx,mctx,&fdctx);

		case SLBT_PRETTY_POSIX:
			return slbt_au_output_symbols_posix(
				dctx,mctx,&fdctx);

		default:
			return slbt_au_output_symbols_yaml(
				dctx,mctx,&fdctx);
	}
}
