/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include "slibtool_realpath_impl.h"

int slbt_util_real_path(
	int             fdat,
	const char *    path,
	int             options,
	char *          buf,
	size_t          buflen)
{
	return slbt_realpath(fdat,path,options,buf,buflen);
}
