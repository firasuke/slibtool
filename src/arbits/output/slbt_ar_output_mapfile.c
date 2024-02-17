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

static int slbt_ar_output_mapfile_impl(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * mctx,
	int                             fdout)
{
	bool            fsort;
	const char *    regex;
	const char **   symv;
	const char **   symstrv;
	regex_t         regctx;
	regmatch_t      pmatch[2] = {0};

	fsort = !(dctx->cctx->fmtflags & SLBT_OUTPUT_ARCHIVE_NOSORT);

	if (slbt_dprintf(fdout,"{\n" "\t" "global:\n") < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

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
		if (!regex || !regexec(&regctx,*symv,1,pmatch,0))
			if (slbt_dprintf(fdout,"\t\t%s;\n",*symv) < 0)
				return SLBT_SYSTEM_ERROR(dctx,0);

	if (regex)
		regfree(&regctx);

	if (slbt_dprintf(fdout,"\n\t" "local:\n" "\t\t*;\n" "};\n") < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	return 0;
}


static int slbt_create_mapfile_impl(
	const struct slbt_archive_meta *  meta,
	const char *                      path,
	mode_t                            mode)
{
	struct slbt_archive_meta_impl * mctx;
	const struct slbt_driver_ctx *  dctx;
	struct slbt_fd_ctx              fdctx;
	int                             fdout;

	mctx = slbt_archive_meta_ictx(meta);
	dctx = (slbt_archive_meta_ictx(meta))->dctx;

	if (slbt_get_driver_fdctx(dctx,&fdctx) < 0)
		return SLBT_NESTED_ERROR(dctx);

	if (!meta->a_memberv)
		return 0;

	if (path) {
		if ((fdout = openat(
				fdctx.fdcwd,path,
				O_WRONLY|O_CREAT|O_TRUNC,
				mode)) < 0)
			return SLBT_SYSTEM_ERROR(dctx,0);
	} else {
		fdout = fdctx.fdout;
	}

	return slbt_ar_output_mapfile_impl(
		dctx,mctx,fdout);
}


int slbt_ar_output_mapfile(const struct slbt_archive_meta * meta)
{
	return slbt_create_mapfile_impl(meta,0,0);
}


int slbt_create_mapfile(
	const struct slbt_archive_meta *  meta,
	const char *                      path,
	mode_t                            mode)
{
	return slbt_create_mapfile_impl(meta,path,mode);
}
