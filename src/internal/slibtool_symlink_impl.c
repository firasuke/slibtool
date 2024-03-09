/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_symlink_impl.h"
#include "slibtool_readlink_impl.h"
#include "slibtool_snprintf_impl.h"
#include "slibtool_visibility_impl.h"

#define SLBT_DEV_NULL_FLAGS	(SLBT_DRIVER_ALL_STATIC      \
				| SLBT_DRIVER_DISABLE_SHARED \
				| SLBT_DRIVER_DISABLE_STATIC)

slbt_hidden int slbt_create_symlink(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			target,
	const char *			lnkname,
	uint32_t			options)
{
	int		fdcwd;
	int		fliteral;
	int		fwrapper;
	int		fdevnull;
	char **		oargv;
	const char *	slash;
	char *		ln[5];
	char *          dot;
	char *		dotdot;
	char		tmplnk [PATH_MAX];
	char		lnkarg [PATH_MAX];
	char		alnkarg[PATH_MAX];
	char		atarget[PATH_MAX];
	char *		suffix = 0;

	/* options */
	fliteral = (options & SLBT_SYMLINK_LITERAL);
	fwrapper = (options & SLBT_SYMLINK_WRAPPER);
	fdevnull = (options & SLBT_SYMLINK_DEVNULL);

	/* symlink is a placeholder? */
	if (fliteral) {
		slash = target;

	/* .disabled .so or .a file */
	} else if (fdevnull) {
		slash  = target;
		suffix = ".disabled";

	/* symlink target contains a dirname? */
	} else if ((slash = strrchr(target,'/'))) {
		slash++;

	/* symlink target is a basename */
	} else {
		slash = target;
	}

	/* .la wrapper? */
	dotdot = fwrapper ? "../" : "";

	/* atarget */
	if (slbt_snprintf(atarget,sizeof(atarget),
			"%s%s",dotdot,slash) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	/* tmplnk */
	if (slbt_snprintf(tmplnk,sizeof(tmplnk),
			"%s.symlink.tmp",
			lnkname) <0)
		return SLBT_BUFFER_ERROR(dctx);

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* placeholder? */
	if (fdevnull) {
		if (unlinkat(fdcwd,lnkname,0) && (errno != ENOENT))
			return SLBT_SYSTEM_ERROR(dctx,0);

		if ((dot = strrchr(lnkname,'.'))) {
			if (!strcmp(dot,dctx->cctx->settings.dsosuffix)) {
				strcpy(dot,".expsyms.a");

				if (unlinkat(fdcwd,lnkname,0) && (errno != ENOENT))
					return SLBT_SYSTEM_ERROR(dctx,0);

				strcpy(dot,dctx->cctx->settings.dsosuffix);
			}
		}
	}

	if (suffix) {
		sprintf(alnkarg,"%s%s",lnkname,suffix);
		lnkname = alnkarg;
	}

	/* lnkarg */
	strcpy(lnkarg,lnkname);

	/* ln argv (fake) */
	ln[0] = "ln";
	ln[1] = "-s";
	ln[2] = atarget;
	ln[3] = lnkarg;
	ln[4] = 0;

	oargv      = ectx->argv;
	ectx->argv = ln;

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT)) {
		if (dctx->cctx->mode == SLBT_MODE_LINK) {
			if (slbt_output_link(ectx)) {
				ectx->argv = oargv;
				return SLBT_NESTED_ERROR(dctx);
			}
		} else {
			if (slbt_output_install(ectx)) {
				ectx->argv = oargv;
				return SLBT_NESTED_ERROR(dctx);
			}
		}
	}

	/* restore execution context */
	ectx->argv = oargv;

	/* create symlink */
	if (symlinkat(atarget,fdcwd,tmplnk))
		return SLBT_SYSTEM_ERROR(dctx,tmplnk);

	return renameat(fdcwd,tmplnk,fdcwd,lnkname)
		? SLBT_SYSTEM_ERROR(dctx,lnkname)
		: 0;
}

slbt_hidden int slbt_symlink_is_a_placeholder(int fdcwd, const char * lnkpath)
{
	size_t		len;
	char		slink [PATH_MAX];
	char		target[PATH_MAX];
	const char	suffix[] = ".disabled";

	if ((sizeof(slink)-sizeof(suffix)) < (len=strlen(lnkpath)))
		return 0;

	memcpy(slink,lnkpath,len);
	memcpy(&slink[len],suffix,sizeof(suffix));

	return (!slbt_readlinkat(fdcwd,slink,target,sizeof(target)))
		&& (!strcmp(target,"/dev/null"));
}
