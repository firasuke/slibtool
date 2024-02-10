/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_spawn_impl.h"
#include "slibtool_symlink_impl.h"
#include "slibtool_errinfo_impl.h"

int slbt_copy_file(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				src,
	char *				dst)
{
	int	fdcwd;
	char **	oargv;
	char *	oprogram;
	char *	cp[4];
	int	ret;

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* placeholder? */
	if (slbt_symlink_is_a_placeholder(fdcwd,src))
		return 0;

	/* cp argv */
	cp[0] = "cp";
	cp[1] = src;
	cp[2] = dst;
	cp[3] = 0;

	/* alternate argument vector */
	oprogram      = ectx->program;
	oargv         = ectx->argv;
	ectx->argv    = cp;
	ectx->program = "cp";

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT)) {
		if (dctx->cctx->mode == SLBT_MODE_LINK) {
			if (slbt_output_link(dctx,ectx)) {
				ectx->argv = oargv;
				ectx->program = oprogram;
				return SLBT_NESTED_ERROR(dctx);
			}
		} else {
			if (slbt_output_install(dctx,ectx)) {
				ectx->argv = oargv;
				ectx->program = oprogram;
				return SLBT_NESTED_ERROR(dctx);
			}
		}
	}

	/* cp spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		ret = SLBT_SPAWN_ERROR(dctx);

	} else if (ectx->exitcode) {
		ret = SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_COPY_ERROR);
	} else {
		ret = 0;
	}

	ectx->argv = oargv;
	ectx->program = oprogram;
	return ret;
}
