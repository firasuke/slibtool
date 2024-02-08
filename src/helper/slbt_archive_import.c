/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2021  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"

/* legacy fallback, no longer in use */
extern int slbt_archive_import_mri(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				dstarchive,
	char *				srcarchive);

/* use slibtool's in-memory archive merging facility */
static int slbt_archive_import_impl(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				dstarchive,
	char *				srcarchive)
{
	int                             ret;
	struct slbt_archive_ctx *       arctxv[3] = {0,0,0};
	struct slbt_archive_ctx *       arctx;

	(void)ectx;

	if (slbt_get_archive_ctx(dctx,dstarchive,&arctxv[0]) < 0)
		return SLBT_NESTED_ERROR(dctx);

	if (slbt_get_archive_ctx(dctx,srcarchive,&arctxv[1]) < 0) {
		slbt_free_archive_ctx(arctxv[0]);
		return SLBT_NESTED_ERROR(dctx);
	}

	ret = slbt_merge_archives(arctxv,&arctx);

	slbt_free_archive_ctx(arctxv[0]);
	slbt_free_archive_ctx(arctxv[1]);

	if (ret == 0) {
		ret = slbt_store_archive(arctx,dstarchive,0644);
		slbt_free_archive_ctx(arctx);
	}

	return (ret < 0) ? SLBT_NESTED_ERROR(dctx) : 0;
}


int slbt_archive_import(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				dstarchive,
	char *				srcarchive)
{
	return slbt_archive_import_impl(
		dctx,ectx,
		dstarchive,
		srcarchive);
}
