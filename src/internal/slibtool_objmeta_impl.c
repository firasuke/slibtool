/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2017  Z. Gilboa                                  */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <slibtool/slibtool.h>
#include "slibtool_errinfo_impl.h"
#include "slibtool_metafile_impl.h"

static int  slbt_create_default_object_wrapper(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int					ret;
	FILE *					fout;
	const struct slbt_source_version *	verinfo;

	if (!(fout = fopen(ectx->ltobjname,"w")))
		return SLBT_SYSTEM_ERROR(dctx);

	verinfo = slbt_source_version();

	ret = fprintf(fout,
		"# libtool compatible object wrapper\n"
		"# Generated by %s (slibtool %d.%d.%d)\n"
		"# [commit reference: %s]\n\n"

		"pic_object='%s'\n"
		"non_pic_object='%s'\n",

		dctx->program,
		verinfo->major,verinfo->minor,verinfo->revision,
		verinfo->commit,

		(dctx->cctx->drvflags & SLBT_DRIVER_SHARED)
			? ectx->lobjname
			: "none",
		(dctx->cctx->drvflags & SLBT_DRIVER_STATIC)
			? ectx->aobjname
			: "none");

	return (ret <= 0) || fclose(fout)
		? SLBT_SYSTEM_ERROR(dctx)
		: 0;
}

static int  slbt_create_compatible_object_wrapper(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int					ret;
	FILE *					fout;
	const struct slbt_source_version *	verinfo;

	if (!(fout = fopen(ectx->ltobjname,"w")))
		return SLBT_SYSTEM_ERROR(dctx);

	verinfo = slbt_source_version();

	ret = fprintf(fout,
		"# %s - a libtool object file\n"
		"# Generated by %s (slibtool %d.%d.%d)\n"
		"# [commit reference: %s]\n"
		"#\n"
		"# Please DO NOT delete this file!\n"
		"# It is necessary for linking the library.\n\n"

		"# Name of the PIC object.\n"
		"pic_object='%s'\n\n"

		"# Name of the non-PIC object\n"
		"non_pic_object='%s'\n",

		ectx->ltobjname,
		dctx->program,
		verinfo->major,verinfo->minor,verinfo->revision,
		verinfo->commit,

		(dctx->cctx->drvflags & SLBT_DRIVER_SHARED)
			? ectx->lobjname
			: "none",
		(dctx->cctx->drvflags & SLBT_DRIVER_STATIC)
			? ectx->aobjname
			: "none");

	return (ret <= 0) || fclose(fout)
		? SLBT_SYSTEM_ERROR(dctx)
		: 0;
}

int  slbt_create_object_wrapper(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	if (dctx->cctx->drvflags & SLBT_DRIVER_LEGABITS)
		return slbt_create_compatible_object_wrapper(dctx,ectx);
	else
		return slbt_create_default_object_wrapper(dctx,ectx);
}
