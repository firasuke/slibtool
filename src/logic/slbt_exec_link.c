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
