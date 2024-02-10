/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"

static const char enable[]  = "enable";
static const char disable[] = "disable";

int slbt_output_features(const struct slbt_driver_ctx * dctx)
{
	int          fdout;
	const char * shared_option;
	const char * static_option;

	fdout = slbt_driver_fdout(dctx);

	shared_option = (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_SHARED)
		? disable : enable;

	static_option = (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_STATIC)
		? disable : enable;

	if (slbt_dprintf(fdout,"host: %s\n",dctx->cctx->host.host) < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	if (slbt_dprintf(fdout,"%s shared libraries\n",shared_option) < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	if (slbt_dprintf(fdout,"%s static libraries\n",static_option) < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	return 0;
}
