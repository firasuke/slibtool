/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
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

static const char lconf_begin[] = "# ### BEGIN LIBTOOL CONFIG\n";
static const char lconf_end  [] = "# ### END LIBTOOL CONFIG\n";

static int slbt_output_config_lconf(
	const struct slbt_driver_ctx * dctx,
	const struct slbt_map_info *   lconf)
{
	const char *    ch;
	const char *    cfg_begin;
	const char *    cfg_end;
	const char *    map_cap;
	size_t          cmp_len;
	size_t          end_len;
	size_t          min_len;
	size_t          nbytes;
	ssize_t         written;
	int             fdout;

	cmp_len = strlen(lconf_begin);
	end_len = strlen(lconf_end);
	min_len = cmp_len + end_len;

	if (lconf->size < min_len)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_FLOW_ERROR);

	map_cap  = lconf->addr;
	map_cap += lconf->size;
	map_cap -= strlen(lconf_end);
	map_cap -= strlen(lconf_begin);

	cfg_begin = cfg_end = 0;

	for (ch=lconf->addr; !cfg_begin && (ch < map_cap); ch++)
		if (!strncmp(ch,lconf_begin,cmp_len))
			cfg_begin = ch;

	if (!cfg_begin)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_FLOW_ERROR);

	for (++ch; !cfg_end && (ch < map_cap); ch++)
		if (!strncmp(ch,lconf_end,end_len))
			cfg_end = ch;

	if (!cfg_end)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_FLOW_ERROR);

	fdout  = slbt_driver_fdout(dctx);
	nbytes = cfg_end - cfg_begin - cmp_len;

	for (ch=&cfg_begin[cmp_len]; nbytes; ) {
		written = write(fdout,ch,nbytes);

		while ((written < 0) && (errno == EINTR))
			written = write(fdout,ch,nbytes);

		if (written < 0)
			return SLBT_SYSTEM_ERROR(dctx,0);

		nbytes -= written;
		ch     += written;
	}

	return 0;
}

int slbt_output_config(const struct slbt_driver_ctx * dctx)
{
	struct slbt_driver_ctx_impl *   ictx;
	const struct slbt_map_info *    lconf;

	ictx  = slbt_get_driver_ictx(dctx);
	lconf = &ictx->lconf;

	if (lconf->addr)
		return slbt_output_config_lconf(
			dctx,lconf);

	return 0;
}
