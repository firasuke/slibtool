/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"

#ifndef SLBT_DRIVER_FLAGS
#define SLBT_DRIVER_FLAGS	SLBT_DRIVER_VERBOSITY_ERRORS \
				| SLBT_DRIVER_VERBOSITY_USAGE
#endif

static const char vermsg[] = "%s%s%s (https://git.foss21.org/slibtool): "
			     "version %s%d.%d.%d%s.\n"
			     "%s%s%s%s%s\n";

static const char * const slbt_ver_color[6] = {
		"\x1b[1m\x1b[35m","\x1b[0m",
		"\x1b[1m\x1b[32m","\x1b[0m",
		"\x1b[1m\x1b[34m","\x1b[0m"
};

static const char * const slbt_ver_plain[6] = {
		"","",
		"","",
		"",""
};

static ssize_t slbt_version(struct slbt_driver_ctx * dctx, int fdout)
{
	const struct slbt_source_version * verinfo;
	const char * const * verclr;
	bool gitver;

	verinfo = slbt_source_version();
	verclr  = isatty(fdout) ? slbt_ver_color : slbt_ver_plain;
	gitver  = strcmp(verinfo->commit,"unknown");

	if (dctx->cctx->drvflags & SLBT_DRIVER_ANNOTATE_NEVER)
		verclr = slbt_ver_plain;

	return slbt_dprintf(fdout,vermsg,
			verclr[0],dctx->program,verclr[1],
			verclr[2],verinfo->major,verinfo->minor,
			verinfo->revision,verclr[3],
			gitver ? "[commit reference: " : "",
			verclr[4],gitver ? verinfo->commit : "",
			verclr[5],gitver ? "]" : "");
}

static void slbt_perform_driver_actions(struct slbt_driver_ctx * dctx)
{
	if (dctx->cctx->drvflags & SLBT_DRIVER_INFO)
		slbt_output_info(dctx);

	if (dctx->cctx->drvflags & SLBT_DRIVER_FEATURES)
		slbt_output_features(dctx);

	if (dctx->cctx->mode == SLBT_MODE_COMPILE)
		slbt_exec_compile(dctx,0);

	if (dctx->cctx->mode == SLBT_MODE_EXECUTE)
		slbt_exec_execute(dctx,0);

	if (dctx->cctx->mode == SLBT_MODE_INSTALL)
		slbt_exec_install(dctx,0);

	if (dctx->cctx->mode == SLBT_MODE_LINK)
		slbt_exec_link(dctx,0);

	if (dctx->cctx->mode == SLBT_MODE_UNINSTALL)
		slbt_exec_uninstall(dctx,0);

	if (dctx->cctx->mode == SLBT_MODE_AR)
		slbt_exec_ar(dctx,0);
}

static int slbt_exit(struct slbt_driver_ctx * dctx, int ret)
{
	slbt_output_error_vector(dctx);
	slbt_free_driver_ctx(dctx);
	return ret;
}

int slbt_main(char ** argv, char ** envp, const struct slbt_fd_ctx * fdctx)
{
	int				ret;
	int				fdout;
	uint64_t			flags;
	uint64_t			noclr;
	struct slbt_driver_ctx *	dctx;
	char *				program;
	char *				dash;

	flags = SLBT_DRIVER_FLAGS;
	fdout = fdctx ? fdctx->fdout : STDOUT_FILENO;
	noclr = getenv("NO_COLOR") ? SLBT_DRIVER_ANNOTATE_NEVER : 0;

	/* program */
	if ((program = strrchr(argv[0],'/')))
		program++;
	else
		program = argv[0];

	/* dash */
	if ((dash = strrchr(program,'-')))
		dash++;

	/* flags */
	if (dash == 0)
		flags = SLBT_DRIVER_FLAGS;

	else if (!(strcmp(dash,"shared")))
		flags = SLBT_DRIVER_FLAGS | SLBT_DRIVER_DISABLE_STATIC;

	else if (!(strcmp(dash,"static")))
		flags = SLBT_DRIVER_FLAGS | SLBT_DRIVER_DISABLE_SHARED;

	/* internal ar mode */
	else if (!(strcmp(dash,"ar")))
		flags |= SLBT_DRIVER_MODE_AR;

	/* debug */
	if (!(strcmp(program,"dlibtool")))
		flags |= SLBT_DRIVER_DEBUG;

	else if (!(strncmp(program,"dlibtool",8)))
		if ((program[8] == '-') || (program[8] == '.'))
			flags |= SLBT_DRIVER_DEBUG;

	/* legabits */
	if (!(strcmp(program,"clibtool")))
		flags |= SLBT_DRIVER_LEGABITS;

	else if (!(strncmp(program,"clibtool",8)))
		if ((program[8] == '-') || (program[8] == '.'))
			flags |= SLBT_DRIVER_LEGABITS;

	/* heuristics */
	if (!(strcmp(program,"rlibtool")))
		flags |= SLBT_DRIVER_HEURISTICS;

	/* heuristics + legabits */
	if (!(strcmp(program,"rclibtool")))
		flags |= (SLBT_DRIVER_HEURISTICS
                          | SLBT_DRIVER_LEGABITS);

	/* heuristics + debug */
	if (!(strcmp(program,"rdlibtool")))
		flags |= (SLBT_DRIVER_HEURISTICS
                          | SLBT_DRIVER_DEBUG);

	/* heuristics + debug + legabits */
	if (!(strcmp(program,"rdclibtool")))
		flags |= (SLBT_DRIVER_HEURISTICS
                          | SLBT_DRIVER_DEBUG
                          | SLBT_DRIVER_LEGABITS);

	/* driver context */
	if ((ret = slbt_get_driver_ctx(argv,envp,flags|noclr,fdctx,&dctx)))
		return (ret == SLBT_USAGE)
			? !argv || !argv[0] || !argv[1] || !argv[2]
			: SLBT_ERROR;

	/* --dumpmachine disables all other actions */
	if (dctx->cctx->drvflags & SLBT_DRIVER_OUTPUT_MACHINE)
		return slbt_output_machine(dctx)
			? SLBT_ERROR : SLBT_OK;

	/* --version must be the first (and only) action */
	if (dctx->cctx->drvflags & SLBT_DRIVER_VERSION)
		if (dctx->cctx->mode != SLBT_MODE_AR)
			return (slbt_version(dctx,fdout) < 0)
				? slbt_exit(dctx,SLBT_ERROR)
				: slbt_exit(dctx,SLBT_OK);

	/* perform all other actions */
	slbt_perform_driver_actions(dctx);

	/* print --version on behalf of a secondary tool as needed */
	if (dctx->cctx->drvflags & SLBT_DRIVER_VERSION)
		return (slbt_version(dctx,fdout) < 0)
			? slbt_exit(dctx,SLBT_ERROR)
			: slbt_exit(dctx,SLBT_OK);

	return slbt_exit(dctx,dctx->errv[0] ? SLBT_ERROR : SLBT_OK);
}
