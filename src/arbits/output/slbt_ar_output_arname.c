/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>
#include <slibtool/slibtool_output.h>
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_ar_impl.h"

#define SLBT_PRETTY_FLAGS       (SLBT_PRETTY_YAML      \
	                         | SLBT_PRETTY_POSIX    \
	                         | SLBT_PRETTY_HEXDATA)


static int slbt_ar_output_arname_posix(
	const struct slbt_driver_ctx *  dctx,
	const struct slbt_archive_ctx * actx,
	const struct slbt_fd_ctx *      fdctx)
{
	(void)dctx;
	(void)actx;
	(void)fdctx;

	/* posix ar(1) does not print the <archive> file-name */

	return 0;
}

static int slbt_ar_output_arname_yaml(
	const struct slbt_driver_ctx *  dctx,
	const struct slbt_archive_ctx * actx,
	const struct slbt_fd_ctx *      fdctx)
{
	const char * path;
	const char   mema[] = "<memory_object>";

	path = actx->path && *actx->path ? *actx->path : mema;

	if (slbt_dprintf(fdctx->fdout,"Archive: %s\n",path) < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	return 0;
}

int slbt_ar_output_arname(const struct slbt_archive_ctx * actx)
{
	const struct slbt_driver_ctx *  dctx;
	struct slbt_fd_ctx              fdctx;

	dctx = (slbt_get_archive_ictx(actx))->dctx;

	if (slbt_get_driver_fdctx(dctx,&fdctx) < 0)
		return SLBT_NESTED_ERROR(dctx);

	switch (dctx->cctx->fmtflags & SLBT_PRETTY_FLAGS) {
		case SLBT_PRETTY_YAML:
			return slbt_ar_output_arname_yaml(
				dctx,actx,&fdctx);

		case SLBT_PRETTY_POSIX:
			return slbt_ar_output_arname_posix(
				dctx,actx,&fdctx);

		default:
			return slbt_ar_output_arname_yaml(
				dctx,actx,&fdctx);
	}
}
