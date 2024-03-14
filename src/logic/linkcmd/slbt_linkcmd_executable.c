/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
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
#include "slibtool_realpath_impl.h"
#include "slibtool_snprintf_impl.h"
#include "slibtool_symlink_impl.h"
#include "slibtool_spawn_impl.h"
#include "slibtool_visibility_impl.h"

static int slbt_linkcmd_exit(
	struct slbt_deps_meta *	depsmeta,
	int			ret)
{
	if (depsmeta->altv)
		free(depsmeta->altv);

	if (depsmeta->args)
		free(depsmeta->args);

	return ret;
}

static void slbt_emit_fdwrap_dl_path_fixup(
	char *	cwd,
	char *	dpfixup,
	size_t	dpfixup_size,
	char *	wrapper)
{
	char *	p;
	char *	q;
	char *	wrapper_dname;

	/* obtain cwd-relative directory name of wrapper */
	for (p=cwd,q=wrapper; *p && *q && (*p==*q); p++,q++)
		(void)0;

	wrapper_dname = (*q == '/') ? (q + 1) : q;

	dpfixup[0] = 0; strncat(dpfixup,"${0%/*}",dpfixup_size - 1);

	/* append parent directory fixup for each level of depth in wrapper_dname */
	for (p=wrapper_dname,q=0; *p; ) {
		if ((p[0] == '.') && (p[1] == '/')) {
			p++; p++;
		} else if ((q = strchr(p, '/'))) {
			strncat(dpfixup,"/..",dpfixup_size-1); p = (q + 1);
		} else {
			break;
		}
	}

	strncat(dpfixup,"/",dpfixup_size-1);
}

slbt_hidden int slbt_exec_link_create_executable(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			exefilename)
{
	int	fdcwd;
	int	fdwrap;
	char ** parg;
	char ** xarg;
	char *	base;
	char *	ccwrap;
	char	cwd    [PATH_MAX];
	char	dpfixup[PATH_MAX];
	char	output [PATH_MAX];
	char	wrapper[PATH_MAX];
	char	wraplnk[PATH_MAX];
	bool	fabspath;
	bool	fpic;
	const struct slbt_source_version * verinfo;
	struct slbt_deps_meta depsmeta = {0,0,0,0};
	struct stat st;

	/* initial state */
	slbt_ectx_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* fpic */
	fpic  = (dctx->cctx->drvflags & SLBT_DRIVER_SHARED);
	fpic &= !(dctx->cctx->drvflags & SLBT_DRIVER_PREFER_STATIC);

	/* input argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		slbt_adjust_object_argument(*parg,fpic,true,fdcwd);

	/* linker argument adjustment */
	for (parg=ectx->cargv, xarg=ectx->xargv; *parg; parg++, xarg++)
		if (slbt_adjust_linker_argument(
				dctx,
				*parg,xarg,fpic,
				dctx->cctx->settings.dsosuffix,
				dctx->cctx->settings.arsuffix,
				&depsmeta) < 0)
			return SLBT_NESTED_ERROR(dctx);

	/* --no-undefined */
	if (dctx->cctx->drvflags & SLBT_DRIVER_NO_UNDEFINED)
		*ectx->noundef = slbt_host_group_is_darwin(dctx)
			? "-Wl,-undefined,error"
			: "-Wl,--no-undefined";

	/* executable wrapper: create */
	if (slbt_snprintf(wrapper,sizeof(wrapper),
				"%s.wrapper.tmp",
				dctx->cctx->output) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	if ((fdwrap = openat(fdcwd,wrapper,O_RDWR|O_CREAT|O_TRUNC,0644)) < 0)
		return SLBT_SYSTEM_ERROR(dctx,wrapper);

	slbt_exec_set_fdwrapper(ectx,fdwrap);

	/* executable wrapper: header */
	verinfo = slbt_api_source_version();

	/* cwd, DL_PATH fixup */
	if (slbt_realpath(fdcwd,".",O_DIRECTORY,cwd,sizeof(cwd)))
		return SLBT_SYSTEM_ERROR(dctx,0);

	slbt_emit_fdwrap_dl_path_fixup(
		cwd,dpfixup,sizeof(dpfixup),
		wrapper);

	if (slbt_dprintf(fdwrap,
			"#!/bin/sh\n"
			"# libtool compatible executable wrapper\n"
			"# Generated by %s (slibtool %d.%d.%d)\n"
			"# [commit reference: %s]\n\n"

			"if [ -z \"$%s\" ]; then\n"
			"\tDL_PATH=\n"
			"\tCOLON=\n"
			"\tLCOLON=\n"
			"else\n"
			"\tDL_PATH=\n"
			"\tCOLON=\n"
			"\tLCOLON=':'\n"
			"fi\n\n"
			"DL_PATH_FIXUP=\"%s\";\n\n",

			dctx->program,
			verinfo->major,verinfo->minor,verinfo->revision,
			verinfo->commit,
			dctx->cctx->settings.ldpathenv,
			dpfixup) < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	/* output */
	if (slbt_snprintf(output,sizeof(output),
			"%s",exefilename) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	*ectx->lout[0] = "-o";
	*ectx->lout[1] = output;

	/* static? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC)
		*ectx->dpic = "-static";

	/* .libs/libfoo.so --> -L.libs -lfoo */
	if (slbt_exec_link_adjust_argument_vector(
			dctx,ectx,&depsmeta,cwd,false))
		return SLBT_NESTED_ERROR(dctx);

	/* using alternate argument vector */
	ccwrap        = (char *)dctx->cctx->ccwrap;
	ectx->argv    = depsmeta.altv;
	ectx->program = ccwrap ? ccwrap : depsmeta.altv[0];

	/* executable wrapper symlink */
	if (slbt_snprintf(wraplnk,sizeof(wraplnk),
			"%s.exe.wrapper",
			exefilename) < 0)
		return slbt_linkcmd_exit(
			&depsmeta,
			SLBT_BUFFER_ERROR(dctx));

	/* executable wrapper: base name */
	base = strrchr(wraplnk,'/');
	base++;

	/* executable wrapper: footer */
	fabspath = (exefilename[0] == '/');

	if (slbt_dprintf(fdwrap,
			"DL_PATH=\"${DL_PATH}${LCOLON}${%s}\"\n\n"
			"export %s=\"$DL_PATH\"\n\n"
			"if [ $(basename \"$0\") = \"%s\" ]; then\n"
			"\tprogram=\"$1\"; shift\n"
			"\texec \"$program\" \"$@\"\n"
			"fi\n\n"
			"exec %s/%s \"$@\"\n",
			dctx->cctx->settings.ldpathenv,
			dctx->cctx->settings.ldpathenv,
			base,
			fabspath ? "" : cwd,
			fabspath ? &exefilename[1] : exefilename) < 0)
		return slbt_linkcmd_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,0));

	/* sigh */
	if (slbt_exec_link_finalize_argument_vector(dctx,ectx))
		return SLBT_NESTED_ERROR(dctx);

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(ectx))
			return slbt_linkcmd_exit(
				&depsmeta,
				SLBT_NESTED_ERROR(dctx));

	/* spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return slbt_linkcmd_exit(
			&depsmeta,
			SLBT_SPAWN_ERROR(dctx));

	} else if (ectx->exitcode) {
		return slbt_linkcmd_exit(
			&depsmeta,
			SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_LINK_ERROR));
	}

	/* executable wrapper: finalize */
	slbt_exec_close_fdwrapper(ectx);

	if (slbt_create_symlink(
			dctx,ectx,
			dctx->cctx->output,wraplnk,
			SLBT_SYMLINK_WRAPPER))
		return slbt_linkcmd_exit(
			&depsmeta,
			SLBT_NESTED_ERROR(dctx));

	if (fstatat(fdcwd,wrapper,&st,0))
		return slbt_linkcmd_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,wrapper));

	if (renameat(fdcwd,wrapper,fdcwd,dctx->cctx->output))
		return slbt_linkcmd_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,dctx->cctx->output));

	if (fchmodat(fdcwd,dctx->cctx->output,0755,0))
		return slbt_linkcmd_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,dctx->cctx->output));

	return slbt_linkcmd_exit(&depsmeta,0);
}
