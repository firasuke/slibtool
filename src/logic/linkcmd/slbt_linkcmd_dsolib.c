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

int slbt_exec_link_create_library(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			dsobasename,
	const char *			dsofilename,
	const char *			relfilename)
{
	int                     fdcwd;
	char **                 parg;
	char **                 xarg;
	char *	                ccwrap;
	const char *            laout;
	const char *            dot;
	char                    cwd    [PATH_MAX];
	char                    output [PATH_MAX];
	char                    soname [PATH_MAX];
	char                    symfile[PATH_MAX];
	struct slbt_deps_meta   depsmeta = {0,0,0,0};

	/* initial state */
	slbt_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* input argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		slbt_adjust_object_argument(*parg,true,false,fdcwd);

	/* .deps */
	if (slbt_exec_link_create_dep_file(
			dctx,ectx,ectx->cargv,
			dsofilename,false))
		return slbt_linkcmd_exit(
			&depsmeta,
			SLBT_NESTED_ERROR(dctx));

	/* linker argument adjustment */
	for (parg=ectx->cargv, xarg=ectx->xargv; *parg; parg++, xarg++)
		if (slbt_adjust_linker_argument(
				dctx,
				*parg,xarg,true,
				dctx->cctx->settings.dsosuffix,
				dctx->cctx->settings.arsuffix,
				&depsmeta) < 0)
			return SLBT_NESTED_ERROR(dctx);

	/* --no-undefined */
	if (dctx->cctx->drvflags & SLBT_DRIVER_NO_UNDEFINED)
		*ectx->noundef = !strcmp(dctx->cctx->host.host,"darwin")
			? "-Wl,-undefined,error"
			: "-Wl,--no-undefined";

	/* -soname */
	dot   = strrchr(dctx->cctx->output,'.');
	laout = (dot && !strcmp(dot,".la"))
			? dctx->cctx->output
			: 0;

	char wl_soname[24];

	if (!strcmp(dctx->cctx->host.host,"darwin")) {
		strcpy(wl_soname,"-Wl,-install_name");
	} else {
		strcpy(wl_soname,"-Wl,-soname");
	}

	if ((dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_MACHO)) {
		(void)0;

	} else if (!laout && (dctx->cctx->drvflags & SLBT_DRIVER_MODULE)) {
		if (slbt_snprintf(soname,sizeof(soname),
				"-Wl,%s",dctx->cctx->output) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = wl_soname;
		*ectx->lsoname = soname;

	} else if (relfilename && dctx->cctx->verinfo.verinfo) {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s-%s%s.%d%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.osdsuffix,
					dctx->cctx->verinfo.major,
					dctx->cctx->settings.osdfussix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = wl_soname;
		*ectx->lsoname = soname;

	} else if (relfilename) {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s-%s%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.dsosuffix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = wl_soname;
		*ectx->lsoname = soname;

	} else if (dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION) {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->settings.dsosuffix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = wl_soname;
		*ectx->lsoname = soname;

	} else {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s%s.%d%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->settings.osdsuffix,
					dctx->cctx->verinfo.major,
					dctx->cctx->settings.osdfussix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = wl_soname;
		*ectx->lsoname = soname;
	}

	/* PE: --output-def */
	if (dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_PE) {
		if (slbt_snprintf(symfile,sizeof(symfile),
					"-Wl,%s",
					ectx->deffilename) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->symdefs = "-Wl,--output-def";
		*ectx->symfile = symfile;
	}

	/* shared/static */
	if (dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC) {
		*ectx->dpic = "-static";
	} else if (dctx->cctx->settings.picswitch) {
		*ectx->dpic = "-shared";
		*ectx->fpic = dctx->cctx->settings.picswitch;
	} else {
		*ectx->dpic = "-shared";
	}

	/* output */
	if (!laout && dctx->cctx->drvflags & SLBT_DRIVER_MODULE) {
		strcpy(output,dctx->cctx->output);
	} else if (relfilename) {
		strcpy(output,relfilename);
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION) {
		strcpy(output,dsofilename);
	} else {
		if (slbt_snprintf(output,sizeof(output),
					"%s%s.%d.%d.%d%s",
					dsobasename,
					dctx->cctx->settings.osdsuffix,
					dctx->cctx->verinfo.major,
					dctx->cctx->verinfo.minor,
					dctx->cctx->verinfo.revision,
					dctx->cctx->settings.osdfussix) < 0)
			return SLBT_BUFFER_ERROR(dctx);
	}

	*ectx->lout[0] = "-o";
	*ectx->lout[1] = output;

	/* ldrpath */
	if (dctx->cctx->host.ldrpath) {
		if (slbt_exec_link_remove_file(dctx,ectx,ectx->rpathfilename))
			return SLBT_NESTED_ERROR(dctx);

		if (slbt_create_symlink(
				dctx,ectx,
				dctx->cctx->host.ldrpath,
				ectx->rpathfilename,
				SLBT_SYMLINK_LITERAL))
			return SLBT_NESTED_ERROR(dctx);
	}

	/* cwd */
	if (slbt_realpath(fdcwd,".",O_DIRECTORY,cwd,sizeof(cwd)))
		return SLBT_SYSTEM_ERROR(dctx,0);

	/* .libs/libfoo.so --> -L.libs -lfoo */
	if (slbt_exec_link_adjust_argument_vector(
			dctx,ectx,&depsmeta,cwd,true))
		return SLBT_NESTED_ERROR(dctx);

	/* using alternate argument vector */
	ccwrap        = (char *)dctx->cctx->ccwrap;
	ectx->argv    = depsmeta.altv;
	ectx->program = ccwrap ? ccwrap : depsmeta.altv[0];

	/* sigh */
	if (slbt_exec_link_finalize_argument_vector(dctx,ectx))
		return SLBT_NESTED_ERROR(dctx);

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
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

	return slbt_linkcmd_exit(&depsmeta,0);
}
