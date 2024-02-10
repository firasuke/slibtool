/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2021  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_linkcmd_impl.h"
#include "slibtool_mapfile_impl.h"
#include "slibtool_metafile_impl.h"
#include "slibtool_snprintf_impl.h"
#include "slibtool_symlink_impl.h"
#include "slibtool_spawn_impl.h"

int slbt_exec_link_create_import_library(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				impfilename,
	char *				deffilename,
	char *				soname)
{
	int	fmdso;
	char *	eargv[8];
	char	program[PATH_MAX];

	/* dlltool or mdso? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_IMPLIB_DSOMETA)
		fmdso = 1;

	else if (dctx->cctx->drvflags & SLBT_DRIVER_IMPLIB_DSOMETA)
		fmdso = 0;

	else if (!(strcmp(dctx->cctx->host.flavor,"midipix")))
		fmdso = 1;

	else
		fmdso = 0;

	/* eargv */
	if (fmdso) {
		if (slbt_snprintf(program,sizeof(program),
				"%s",dctx->cctx->host.mdso) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		eargv[0] = program;
		eargv[1] = "-i";
		eargv[2] = impfilename;
		eargv[3] = "-n";
		eargv[4] = soname;
		eargv[5] = deffilename;
		eargv[6] = 0;
	} else {
		if (slbt_snprintf(program,sizeof(program),
				"%s",dctx->cctx->host.dlltool) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		eargv[0] = program;
		eargv[1] = "-l";
		eargv[2] = impfilename;
		eargv[3] = "-d";
		eargv[4] = deffilename;
		eargv[5] = "-D";
		eargv[6] = soname;
		eargv[7] = 0;
	}

	/* alternate argument vector */
	ectx->argv    = eargv;
	ectx->program = program;

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return SLBT_NESTED_ERROR(dctx);

	/* dlltool/mdso spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return SLBT_SPAWN_ERROR(dctx);

	} else if (ectx->exitcode) {
		return SLBT_CUSTOM_ERROR(
			dctx,
			fmdso ? SLBT_ERR_MDSO_ERROR : SLBT_ERR_DLLTOOL_ERROR);
	}

	return 0;
}
