/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <time.h>
#include <locale.h>
#include <slibtool/slibtool.h>
#include <slibtool/slibtool_output.h>
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_ar_impl.h"

#define SLBT_PRETTY_FLAGS       (SLBT_PRETTY_YAML      \
	                         | SLBT_PRETTY_POSIX    \
	                         | SLBT_PRETTY_HEXDATA)

const char slbt_ar_perm_strs[8][4] = {
	{'-','-','-','\0'},
	{'-','-','x','\0'},
	{'-','w','-','\0'},
	{'-','w','x','\0'},
	{'r','-','-','\0'},
	{'r','-','x','\0'},
	{'r','w','-','\0'},
	{'r','w','x','\0'}
};

static unsigned slbt_ar_output_decimal_len_from_val(size_t val, unsigned min)
{
	unsigned ret;

	for (ret=0; val; ret++)
		val /= 10;

	return (ret > min) ? ret : min;
}

static int slbt_ar_output_one_member_posix(
	int                             fdout,
	struct ar_meta_member_info *    memberp)
{
	return slbt_dprintf(
		fdout,"%s\n",
		memberp->ar_file_header.ar_member_name);
}

static int slbt_ar_output_one_member_posix_verbose(
	int                             fdout,
	struct ar_meta_member_info *    memberp,
	const char *                    fmtstr,
	locale_t                        arlocale)
{
	unsigned    ownerbits;
	unsigned    groupbits;
	unsigned    worldbits;
	time_t      artimeval;
	struct tm   artimeloc;
	char        artimestr[64] = {0};

	ownerbits = (memberp->ar_file_header.ar_file_mode & 0700) >> 6;
	groupbits = (memberp->ar_file_header.ar_file_mode & 0070) >> 3;
	worldbits = (memberp->ar_file_header.ar_file_mode & 0007);
	artimeval = memberp->ar_file_header.ar_time_date_stamp;

	if (localtime_r(&artimeval,&artimeloc))
		strftime_l(
			artimestr,sizeof(artimestr),
			"%b %e %H:%M %Y",&artimeloc,
			arlocale);

	return slbt_dprintf(
		fdout,fmtstr,
		slbt_ar_perm_strs[ownerbits],
		slbt_ar_perm_strs[groupbits],
		slbt_ar_perm_strs[worldbits],
		memberp->ar_file_header.ar_uid,
		memberp->ar_file_header.ar_gid,
		memberp->ar_file_header.ar_file_size,
		artimestr,
		memberp->ar_file_header.ar_member_name);
}

static int slbt_ar_output_members_posix(
	const struct slbt_driver_ctx *  dctx,
	const struct slbt_archive_meta * meta,
	const struct slbt_fd_ctx *       fdctx)
{
	struct ar_meta_member_info **   memberp;
	int                             fdout;
	size_t                          testval;
	size_t                          sizelen;
	size_t                          uidlen;
	size_t                          gidlen;
	locale_t                        arloc;
	char                            fmtstr[64];

	fdout = fdctx->fdout;
	arloc = 0;

	if (dctx->cctx->fmtflags & SLBT_PRETTY_VERBOSE) {
		for (sizelen=0,memberp=meta->a_memberv; *memberp; memberp++)
			if ((testval = memberp[0]->ar_file_header.ar_file_size) > sizelen)
				sizelen = testval;

		for (uidlen=0,memberp=meta->a_memberv; *memberp; memberp++)
			if ((testval = memberp[0]->ar_file_header.ar_uid) > uidlen)
				uidlen = testval;

		for (gidlen=0,memberp=meta->a_memberv; *memberp; memberp++)
			if ((testval = memberp[0]->ar_file_header.ar_gid) > gidlen)
				gidlen = testval;

		sizelen = slbt_ar_output_decimal_len_from_val(sizelen,6);
		uidlen  = slbt_ar_output_decimal_len_from_val(uidlen,1);
		gidlen  = slbt_ar_output_decimal_len_from_val(gidlen,1);
		arloc   = newlocale(LC_ALL,setlocale(LC_ALL,0),0);

		sprintf(fmtstr,"%%s%%s%%s %%%zuu/%%-%zuu %%%zuu %%s %%s\n",uidlen,gidlen,sizelen);
	}

	for (memberp=meta->a_memberv; *memberp; memberp++) {
		switch ((*memberp)->ar_member_attr) {
			case AR_MEMBER_ATTR_ARMAP:
			case AR_MEMBER_ATTR_LINKINFO:
			case AR_MEMBER_ATTR_NAMESTRS:
				break;

			default:
				if (arloc) {
					if (slbt_ar_output_one_member_posix_verbose(
							fdout,*memberp,fmtstr,arloc) < 0)
						return SLBT_SYSTEM_ERROR(dctx,0);
				} else {
					if (slbt_ar_output_one_member_posix(
							fdout,*memberp) < 0)
						return SLBT_SYSTEM_ERROR(dctx,0);
				}
		}
	}

	if (arloc)
		freelocale(arloc);

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
