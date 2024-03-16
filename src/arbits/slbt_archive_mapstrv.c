/*******************************************************************/
/*  slibtool: a strong libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdlib.h>
#include <inttypes.h>
#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_visibility_impl.h"
#include "slibtool_ar_impl.h"

static int slbt_strcmp(const void * a, const void * b)
{
	return strcmp(*(const char **)a,*(const char **)b);
}

static int slbt_coff_strcmp(const void * a, const void * b)
{
	const char *    dot;
	const char *    mark;
	const char *    stra;
	const char *    strb;
	const char **   pstra;
	const char **   pstrb;
	char            strbufa[4096];
	char            strbufb[4096];

	pstra = (const char **)a;
	pstrb = (const char **)b;

	stra  = *pstra;
	strb  = *pstrb;

	if (!strncmp(*pstra,".weak.",6)) {
		stra = strbufa;
		mark = &(*pstra)[6];
		dot  = strchr(mark,'.');

		strncpy(strbufa,mark,dot-mark);
		strbufa[dot-mark] = '\0';
	}

	if (!strncmp(*pstrb,".weak.",6)) {
		strb = strbufb;
		mark = &(*pstrb)[6];
		dot  = strchr(mark,'.');

		strncpy(strbufb,mark,dot-mark);
		strbufb[dot-mark] = '\0';
	}

	return strcmp(stra,strb);
}

slbt_hidden int slbt_update_mapstrv(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * mctx)
{
	bool            fcoff;
	size_t          nsyms;
	const char **   symv;
	const char **   mapstrv;

	fcoff  = slbt_host_objfmt_is_coff(dctx);
	fcoff |= (mctx->ofmtattr & AR_OBJECT_ATTR_COFF);

	for (nsyms=0,symv=mctx->symstrv; *symv; symv++)
		nsyms++;

	if (!(mapstrv = calloc(nsyms+1,sizeof(const char *))))
		return SLBT_SYSTEM_ERROR(dctx,0);

	for (nsyms=0,symv=mctx->symstrv; *symv; symv++)
		mapstrv[nsyms++] = *symv;

	qsort(mapstrv,nsyms,sizeof(const char *),fcoff ? slbt_coff_strcmp : slbt_strcmp);

	mctx->mapstrv = mapstrv;

	return 0;
}
