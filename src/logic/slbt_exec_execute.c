/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <slibtool/slibtool.h>
#include "slibtool_spawn_impl.h"
#include "slibtool_driver_impl.h"
#include "slibtool_snprintf_impl.h"
#include "slibtool_errinfo_impl.h"

int  slbt_exec_execute(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int			ret;
	int			fdcwd;
	char *			program;
	char *			script;
	char *			base;
	char *			mark;
	char			exeref [PATH_MAX];
	char			wrapper[PATH_MAX];
	struct stat		st;
	struct slbt_exec_ctx *	actx = 0;

	/* dry run */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DRY_RUN)
		return 0;

	/* context */
	if (ectx)
		slbt_disable_placeholders(ectx);
	else if ((ret = slbt_ectx_get_exec_ctx(dctx,&ectx)))
		return ret;
	else {
		actx = ectx;
		slbt_disable_placeholders(ectx);
	}

	/* script, program */
	program = ectx->cargv[0];
	script  = ectx->cargv[1];

	if (script) {
		/* exeref */
		if ((base = strrchr(script,'/')))
			base++;
		else
			base = script;

		strcpy(exeref,script);
		mark = exeref + (base - script);
		sprintf(mark,".libs/%s",base);

		/* wrapper */
		if (slbt_snprintf(wrapper,sizeof(wrapper),
					"%s.exe.wrapper",
					exeref) < 0) {
			slbt_ectx_free_exec_ctx(actx);
			return SLBT_BUFFER_ERROR(dctx);
		}

		/* fdcwd */
		fdcwd = slbt_driver_fdcwd(dctx);

		/* swap vector */
		if (!fstatat(fdcwd,script,&st,0) && !fstatat(fdcwd,wrapper,&st,0)) {
			ectx->cargv[0] = wrapper;
			ectx->cargv[1] = program;
			ectx->cargv[2] = exeref;
		} else {
			script = program;
		}

		/* execute mode */
		ectx->program = script;
		ectx->argv    = ectx->cargv;
	} else {
		ectx->program = program;
		ectx->argv    = ectx->cargv;
	}

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_execute(ectx)) {
			slbt_ectx_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

	execvp(ectx->cargv[0],ectx->argv);

	slbt_ectx_free_exec_ctx(actx);
	return SLBT_SYSTEM_ERROR(dctx,0);
}
