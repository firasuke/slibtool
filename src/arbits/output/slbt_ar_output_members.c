/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>
#include <slibtool/slibtool_output.h>
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_ar_impl.h"

#define SLBT_PRETTY_FLAGS       (SLBT_PRETTY_YAML      \
	                         | SLBT_PRETTY_POSIX    \
	                         | SLBT_PRETTY_HEXDATA)


static int slbt_ar_output_members_posix(
	const struct slbt_driver_ctx *  dctx,
	const struct slbt_archive_meta * meta,
	const struct slbt_fd_ctx *       fdctx)
{
	struct ar_meta_member_info **   memberp;
	const char *                    name;

	for (memberp=meta->a_memberv; *memberp; memberp++) {
		switch ((*memberp)->ar_member_attr) {
			case AR_MEMBER_ATTR_ARMAP:
			case AR_MEMBER_ATTR_LINKINFO:
			case AR_MEMBER_ATTR_NAMESTRS:
				break;

			default:
				name = (*memberp)->ar_file_header.ar_member_name;

				if (slbt_dprintf(fdctx->fdout,"%s\n",name) < 0)
					return SLBT_SYSTEM_ERROR(dctx,0);
		}
	}

	return 0;
}

static int slbt_ar_output_members_yaml(
	const struct slbt_driver_ctx *  dctx,
	const struct slbt_archive_meta * meta,
	const struct slbt_fd_ctx *       fdctx)
{
	struct ar_meta_member_info **   memberp;
	const char *                    name;

	if (slbt_dprintf(fdctx->fdout,"  - Members = {\n") < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	for (memberp=meta->a_memberv; *memberp; memberp++) {
		switch ((*memberp)->ar_member_attr) {
			case AR_MEMBER_ATTR_ARMAP:
			case AR_MEMBER_ATTR_LINKINFO:
			case AR_MEMBER_ATTR_NAMESTRS:
				break;

			default:
				name = (*memberp)->ar_file_header.ar_member_name;

				if (slbt_dprintf(fdctx->fdout,"    - %s\n",name) < 0)
					return SLBT_SYSTEM_ERROR(dctx,0);
		}
	}

	if (slbt_dprintf(fdctx->fdout,"  }\n") < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	return 0;
}

int slbt_ar_output_members(const struct slbt_archive_meta * meta)
{
	const struct slbt_driver_ctx *  dctx;
	struct slbt_fd_ctx              fdctx;

	dctx = (slbt_archive_meta_ictx(meta))->dctx;

	if (slbt_get_driver_fdctx(dctx,&fdctx) < 0)
		return SLBT_NESTED_ERROR(dctx);

	if (!meta->a_memberv)
		return 0;

	switch (dctx->cctx->fmtflags & SLBT_PRETTY_FLAGS) {
		case SLBT_PRETTY_YAML:
			return slbt_ar_output_members_yaml(
				dctx,meta,&fdctx);

		case SLBT_PRETTY_POSIX:
			return slbt_ar_output_members_posix(
				dctx,meta,&fdctx);

		default:
			return slbt_ar_output_members_yaml(
				dctx,meta,&fdctx);
	}
}
