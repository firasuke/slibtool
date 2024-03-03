/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
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
#include "slibtool_visibility_impl.h"

static int slbt_exec_link_create_noop_symlink(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			arfilename)
{
	struct stat st;
	int         fdcwd;

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* file exists? */
	if (!fstatat(fdcwd,arfilename,&st,AT_SYMLINK_NOFOLLOW))
		return 0;

	/* needed? */
	if (errno == ENOENT) {
		if (slbt_create_symlink(
				dctx,ectx,
				"/dev/null",
				arfilename,
				SLBT_SYMLINK_LITERAL))
			return SLBT_NESTED_ERROR(dctx);
		return 0;
	}

	return SLBT_SYSTEM_ERROR(dctx,arfilename);
}

static int slbt_exec_link_remove_file(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			target)
{
	int fdcwd;

	(void)ectx;

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* remove target (if any) */
	if (!unlinkat(fdcwd,target,0) || (errno == ENOENT))
		return 0;

	return SLBT_SYSTEM_ERROR(dctx,0);
}

slbt_hidden int slbt_exec_link_create_archive(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			arfilename,
	bool				fpic)
{
	int		fdcwd;
	char **         argv;
	char ** 	aarg;
	char ** 	parg;
	char		program[PATH_MAX];
	char		output [PATH_MAX];

	/* -disable-static? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_STATIC)
		if (dctx->cctx->rpath)
			return slbt_exec_link_create_noop_symlink(
				dctx,ectx,arfilename);

	/* dlopen, dlpreopen: object compilation (derived from dynamic linking) */
	if (ectx->dlopenobj) {
		slbt_ectx_reset_arguments(ectx);
		slbt_reset_placeholders(ectx);

		if (slbt_exec_link_create_library(
				dctx,ectx,
				"@ARDLOPEN",
				"@ARDLOPEN",
				"@ARDLOPEN",
				true) < 0)
			return SLBT_NESTED_ERROR(dctx);
	}

	/* restore initial state */
	slbt_ectx_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* output */
	if (slbt_snprintf(output,sizeof(output),
			"%s",arfilename) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	/* tool-specific argument vector */
	argv = (slbt_get_driver_ictx(dctx))->host.ar_argv;

	/* fdcwd */
	fdcwd   = slbt_driver_fdcwd(dctx);

	/* input argument adjustment */
	aarg    = ectx->altv;

	if ((parg = argv)) {
		for (; *parg; )
			*aarg++ = *parg++;
	} else {
		*aarg++ = program;
	}

	*aarg++ = "-crs";
	*aarg++ = output;

	for (parg=ectx->cargv; *parg; parg++)
		if (slbt_adjust_object_argument(*parg,fpic,!fpic,fdcwd))
			*aarg++ = *parg;

	if (ectx->dlopenobj)
		*aarg++ = ectx->dlopenobj;

	*aarg = 0;
	ectx->argv = ectx->altv;

	/* ar program */
	if (argv) {
		ectx->program = argv[0];
	} else {
		if (slbt_snprintf(program,sizeof(program),
				"%s",dctx->cctx->host.ar) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		ectx->program = program;
	}

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(ectx))
			return SLBT_NESTED_ERROR(dctx);

	/* remove old archive as needed */
	if (slbt_exec_link_remove_file(dctx,ectx,output))
		return SLBT_NESTED_ERROR(dctx);

	/* .deps */
	if (slbt_exec_link_create_dep_file(
			dctx,ectx,ectx->cargv,
			arfilename,true))
		return SLBT_NESTED_ERROR(dctx);

	/* ar spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return SLBT_SPAWN_ERROR(dctx);

	} else if (ectx->exitcode) {
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_ERROR);
	}

	/* input objects associated with .la archives */
	for (parg=ectx->cargv; *parg; parg++)
		if (slbt_adjust_wrapper_argument(*parg,true))
			if (slbt_util_import_archive(ectx,output,*parg))
				return SLBT_NESTED_ERROR(dctx);

	return 0;
}
