/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2021  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <slibtool/slibtool.h>
#include "slibtool_spawn_impl.h"
#include "slibtool_mkdir_impl.h"
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_linkcmd_impl.h"
#include "slibtool_mapfile_impl.h"
#include "slibtool_metafile_impl.h"
#include "slibtool_readlink_impl.h"
#include "slibtool_snprintf_impl.h"
#include "slibtool_symlink_impl.h"

/*******************************************************************/
/*                                                                 */
/* -o <ltlib>  switches              input   result                */
/* ----------  --------------------- -----   ------                */
/* libfoo.a    [-shared|-static]     bar.lo  libfoo.a              */
/*                                                                 */
/* ar -crs libfoo.a bar.o                                          */
/*                                                                 */
/*******************************************************************/

/*******************************************************************/
/*                                                                 */
/* -o <ltlib>  switches              input   result                */
/* ----------  --------------------- -----   ------                */
/* libfoo.la   -shared               bar.lo  libfoo.la             */
/*                                           .libs/libfoo.a        */
/*                                           .libs/libfoo.la (lnk) */
/*                                                                 */
/* ar -crs .libs/libfoo.a .libs/bar.o                              */
/* (generate libfoo.la)                                            */
/* ln -s ../libfoo.la .libs/libfoo.la                              */
/*                                                                 */
/*******************************************************************/

/*******************************************************************/
/*                                                                 */
/* -o <ltlib>  switches              input   result                */
/* ----------  --------------------- -----   ------                */
/* libfoo.la   -static               bar.lo  libfoo.la             */
/*                                           .libs/libfoo.a        */
/*                                           .libs/libfoo.la (lnk) */
/*                                                                 */
/* ar -crs .libs/libfoo.a bar.o                                    */
/* (generate libfoo.la)                                            */
/* ln -s ../libfoo.la .libs/libfoo.la                              */
/*                                                                 */
/*******************************************************************/

static int slbt_exec_link_exit(
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

static int slbt_exec_link_create_library(
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
		return slbt_exec_link_exit(
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
		*ectx->noundef = "-Wl,--no-undefined";

	/* -soname */
	dot   = strrchr(dctx->cctx->output,'.');
	laout = (dot && !strcmp(dot,".la"))
			? dctx->cctx->output
			: 0;

	if ((dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_MACHO)) {
		(void)0;

	} else if (!laout && (dctx->cctx->drvflags & SLBT_DRIVER_MODULE)) {
		if (slbt_snprintf(soname,sizeof(soname),
				"-Wl,%s",dctx->cctx->output) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
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

		*ectx->soname  = "-Wl,-soname";
		*ectx->lsoname = soname;

	} else if (relfilename) {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s-%s%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.dsosuffix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
		*ectx->lsoname = soname;

	} else if (dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION) {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->settings.dsosuffix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
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

		*ectx->soname  = "-Wl,-soname";
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
			return slbt_exec_link_exit(
				&depsmeta,
				SLBT_NESTED_ERROR(dctx));

	/* spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SPAWN_ERROR(dctx));

	} else if (ectx->exitcode) {
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_LINK_ERROR));
	}

	return slbt_exec_link_exit(&depsmeta,0);
}

static int slbt_exec_link_create_executable(
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
	slbt_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* fpic */
	fpic = !(dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC);

	/* input argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		slbt_adjust_object_argument(*parg,fpic,true,fdcwd);

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
		*ectx->noundef = "-Wl,--no-undefined";

	/* executable wrapper: create */
	if (slbt_snprintf(wrapper,sizeof(wrapper),
				"%s.wrapper.tmp",
				dctx->cctx->output) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	if ((fdwrap = openat(fdcwd,wrapper,O_RDWR|O_CREAT|O_TRUNC,0644)) < 0)
		return SLBT_SYSTEM_ERROR(dctx,wrapper);

	slbt_exec_set_fdwrapper(ectx,fdwrap);

	/* executable wrapper: header */
	verinfo = slbt_source_version();

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
		return slbt_exec_link_exit(
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
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,0));

	/* sigh */
	if (slbt_exec_link_finalize_argument_vector(dctx,ectx))
		return SLBT_NESTED_ERROR(dctx);

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return slbt_exec_link_exit(
				&depsmeta,
				SLBT_NESTED_ERROR(dctx));

	/* spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SPAWN_ERROR(dctx));

	} else if (ectx->exitcode) {
		return slbt_exec_link_exit(
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
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_NESTED_ERROR(dctx));

	if (fstatat(fdcwd,wrapper,&st,0))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,wrapper));

	if (renameat(fdcwd,wrapper,fdcwd,dctx->cctx->output))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,dctx->cctx->output));

	if (fchmodat(fdcwd,dctx->cctx->output,0755,0))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,dctx->cctx->output));

	return slbt_exec_link_exit(&depsmeta,0);
}

static int slbt_exec_link_create_library_symlink(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	bool				fmajor)
{
	char	target[PATH_MAX];
	char	lnkname[PATH_MAX];

	if (ectx->relfilename && dctx->cctx->verinfo.verinfo) {
		strcpy(target,ectx->relfilename);
		sprintf(lnkname,"%s.dualver",ectx->dsofilename);

		if (slbt_create_symlink(
				dctx,ectx,
				target,lnkname,
				SLBT_SYMLINK_DEFAULT))
			return SLBT_NESTED_ERROR(dctx);
	} else if (ectx->relfilename) {
		strcpy(target,ectx->relfilename);
		sprintf(lnkname,"%s.release",ectx->dsofilename);

		if (slbt_create_symlink(
				dctx,ectx,
				target,lnkname,
				SLBT_SYMLINK_DEFAULT))
			return SLBT_NESTED_ERROR(dctx);
	} else {
		sprintf(target,"%s%s.%d.%d.%d%s",
			ectx->dsobasename,
			dctx->cctx->settings.osdsuffix,
			dctx->cctx->verinfo.major,
			dctx->cctx->verinfo.minor,
			dctx->cctx->verinfo.revision,
			dctx->cctx->settings.osdfussix);
	}


	if (fmajor && ectx->dsorellnkname) {
		sprintf(lnkname,"%s.%d",
			ectx->dsorellnkname,
			dctx->cctx->verinfo.major);

	} else if (fmajor) {
		sprintf(lnkname,"%s%s.%d%s",
			ectx->dsobasename,
			dctx->cctx->settings.osdsuffix,
			dctx->cctx->verinfo.major,
			dctx->cctx->settings.osdfussix);

	} else {
		strcpy(lnkname,ectx->dsofilename);
	}


	if (fmajor && (dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_PE))
		return slbt_copy_file(
			dctx,ectx,
			target,lnkname);
	else
		return slbt_create_symlink(
			dctx,ectx,
			target,lnkname,
			SLBT_SYMLINK_DEFAULT);
}

int slbt_exec_link(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int			ret;
	const char *		output;
	char *			dot;
	struct slbt_exec_ctx *	actx;
	bool			fpic;
	bool			fstaticonly;
	char			soname[PATH_MAX];
	char			soxyz [PATH_MAX];
	char			solnk [PATH_MAX];
	char			arname[PATH_MAX];
	char			target[PATH_MAX];
	char			lnkname[PATH_MAX];

	/* dry run */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DRY_RUN)
		return 0;

	/* context */
	if (ectx)
		actx = 0;
	else if ((ret = slbt_get_exec_ctx(dctx,&ectx)))
		return SLBT_NESTED_ERROR(dctx);
	else
		actx = ectx;

	/* libfoo.so.x.y.z */
	if (slbt_snprintf(soxyz,sizeof(soxyz),
				"%s%s%s%s%s.%d.%d.%d%s",
				ectx->sonameprefix,
				dctx->cctx->libname,
				dctx->cctx->release ? "-" : "",
				dctx->cctx->release ? dctx->cctx->release : "",
				dctx->cctx->settings.osdsuffix,
				dctx->cctx->verinfo.major,
				dctx->cctx->verinfo.minor,
				dctx->cctx->verinfo.revision,
				dctx->cctx->settings.osdfussix) < 0) {
		slbt_free_exec_ctx(actx);
		return SLBT_BUFFER_ERROR(dctx);
	}

	/* libfoo.so.x */
	sprintf(soname,"%s%s%s%s%s.%d%s",
		ectx->sonameprefix,
		dctx->cctx->libname,
		dctx->cctx->release ? "-" : "",
		dctx->cctx->release ? dctx->cctx->release : "",
		dctx->cctx->settings.osdsuffix,
		dctx->cctx->verinfo.major,
		dctx->cctx->settings.osdfussix);

	/* libfoo.so */
	sprintf(solnk,"%s%s%s",
		ectx->sonameprefix,
		dctx->cctx->libname,
		dctx->cctx->settings.dsosuffix);

	/* libfoo.a */
	sprintf(arname,"%s%s%s",
		dctx->cctx->settings.arprefix,
		dctx->cctx->libname,
		dctx->cctx->settings.arsuffix);

	/* output suffix */
	output = dctx->cctx->output;
	dot    = strrchr(output,'.');

	/* .libs directory */
	if (slbt_mkdir(dctx,ectx->ldirname)) {
		ret = SLBT_SYSTEM_ERROR(dctx,ectx->ldirname);
		slbt_free_exec_ctx(actx);
		return ret;
	}

	/* non-pic libfoo.a */
	if (dot && !strcmp(dot,".a"))
		if (slbt_exec_link_create_archive(dctx,ectx,output,false)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

	/* fpic, fstaticonly */
	if (dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC) {
		fstaticonly = true;
		fpic        = false;
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_SHARED) {
		fstaticonly = true;
		fpic        = false;
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_STATIC) {
		fstaticonly = false;
		fpic        = true;
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_SHARED) {
		fstaticonly = false;
		fpic        = true;
	} else {
		fstaticonly = false;
		fpic        = false;
	}

	/* libfoo.so.def.{flavor} */
	if (dctx->cctx->libname) {
		if (slbt_exec_link_create_host_tag(
				dctx,ectx,
				ectx->deffilename))
			return SLBT_NESTED_ERROR(dctx);
	}

	/* pic libfoo.a */
	if (dot && !strcmp(dot,".la"))
		if (slbt_exec_link_create_archive(
				dctx,ectx,
				ectx->arfilename,
				fpic)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

	/* static-only libfoo.la */
	if (fstaticonly && dot && !strcmp(dot,".la")) {
		const struct slbt_flavor_settings * dflavor;

		if (slbt_get_flavor_settings("default",&dflavor) < 0)
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LINK_FLOW);

		if (strcmp(dctx->cctx->settings.dsosuffix,dflavor->dsosuffix)) {
			strcpy(target,ectx->lafilename);
			sprintf(lnkname,"%s.shrext%s",
				ectx->lafilename,
				dctx->cctx->settings.dsosuffix);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

			strcpy(target,lnkname);
			sprintf(lnkname,"%s.shrext",ectx->lafilename);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);
		}

		if (slbt_create_symlink(
				dctx,ectx,
				"/dev/null",
				ectx->deffilename,
				SLBT_SYMLINK_LITERAL|SLBT_SYMLINK_DEVNULL))
			return SLBT_NESTED_ERROR(dctx);
	}

	/* -all-static library */
	if (fstaticonly && dctx->cctx->libname)
		if (slbt_create_symlink(
				dctx,ectx,
				"/dev/null",
				ectx->dsofilename,
				SLBT_SYMLINK_LITERAL))
			return SLBT_NESTED_ERROR(dctx);

	/* dynamic library via -module */
	if (dctx->cctx->rpath && !fstaticonly) {
		if (dctx->cctx->drvflags & SLBT_DRIVER_MODULE) {
			if (!dot || strcmp(dot,".la")) {
				if (slbt_exec_link_create_library(
						dctx,ectx,
						ectx->dsobasename,
						ectx->dsofilename,
						ectx->relfilename)) {
					slbt_free_exec_ctx(actx);
					return SLBT_NESTED_ERROR(dctx);
				}

				slbt_free_exec_ctx(actx);
				return 0;
			}
		}
	}

	/* dynamic library */
	if (dot && !strcmp(dot,".la") && dctx->cctx->rpath && !fstaticonly) {
		const struct slbt_flavor_settings * dflavor;

		if (slbt_get_flavor_settings("default",&dflavor) < 0)
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LINK_FLOW);

		/* -shrext support */
		if (dctx->cctx->shrext) {
			strcpy(target,ectx->lafilename);
			sprintf(lnkname,"%s.shrext%s",ectx->lafilename,dctx->cctx->shrext);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

			strcpy(target,lnkname);
			sprintf(lnkname,"%s.shrext",ectx->lafilename);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

		/* non-default shared-object suffix support */
		} else if (strcmp(dctx->cctx->settings.dsosuffix,dflavor->dsosuffix)) {
			strcpy(target,ectx->lafilename);
			sprintf(lnkname,"%s.shrext%s",
				ectx->lafilename,
				dctx->cctx->settings.dsosuffix);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

			strcpy(target,lnkname);
			sprintf(lnkname,"%s.shrext",ectx->lafilename);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);
		}

		/* linking: libfoo.so.x.y.z */
		if (slbt_exec_link_create_library(
				dctx,ectx,
				ectx->dsobasename,
				ectx->dsofilename,
				ectx->relfilename)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

		if (!(dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION)) {
			/* symlink: libfoo.so.x --> libfoo.so.x.y.z */
			if (slbt_exec_link_create_library_symlink(
					dctx,ectx,
					true)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}

			/* symlink: libfoo.so --> libfoo.so.x.y.z */
			if (slbt_exec_link_create_library_symlink(
					dctx,ectx,
					false)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}
		} else if (ectx->relfilename) {
			/* symlink: libfoo.so --> libfoo-x.y.z.so */
			if (slbt_exec_link_create_library_symlink(
					dctx,ectx,
					false)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}
		}

		/* PE import libraries */
		if (dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_PE) {
			/* libfoo.x.lib.a */
			if (slbt_exec_link_create_import_library(
					dctx,ectx,
					ectx->pimpfilename,
					ectx->deffilename,
					soname))
				return SLBT_NESTED_ERROR(dctx);

			/* symlink: libfoo.lib.a --> libfoo.x.lib.a */
			if (slbt_create_symlink(
					dctx,ectx,
					ectx->pimpfilename,
					ectx->dimpfilename,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

			/* libfoo.x.y.z.lib.a */
			if (slbt_exec_link_create_import_library(
					dctx,ectx,
					ectx->vimpfilename,
					ectx->deffilename,
					soxyz))
				return SLBT_NESTED_ERROR(dctx);
		} else {
			if (slbt_create_symlink(
					dctx,ectx,
					"/dev/null",
					ectx->deffilename,
					SLBT_SYMLINK_LITERAL|SLBT_SYMLINK_DEVNULL))
				return SLBT_NESTED_ERROR(dctx);
		}
	}

	/* executable */
	if (!dctx->cctx->libname) {
		/* linking: .libs/exefilename */
		if (slbt_exec_link_create_executable(
				dctx,ectx,
				ectx->exefilename)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}
	}

	/* no wrapper? */
	if (!dot || strcmp(dot,".la")) {
		slbt_free_exec_ctx(actx);
		return 0;
	}

	/* library wrapper */
	if (slbt_create_library_wrapper(
			dctx,ectx,
			arname,soname,soxyz,solnk)) {
		slbt_free_exec_ctx(actx);
		return SLBT_NESTED_ERROR(dctx);
	}

	/* wrapper symlink */
	if ((ret = slbt_create_symlink(
			dctx,ectx,
			output,
			ectx->lafilename,
			SLBT_SYMLINK_WRAPPER)))
		SLBT_NESTED_ERROR(dctx);

	/* .lai wrapper symlink */
	if (ret == 0)
		if ((ret = slbt_create_symlink(
				dctx,ectx,
				output,
				ectx->laifilename,
				SLBT_SYMLINK_WRAPPER)))
			SLBT_NESTED_ERROR(dctx);

	/* all done */
	slbt_free_exec_ctx(actx);

	return ret;
}
