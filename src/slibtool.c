/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <slibtool/slibtool.h>

#define SLBT_UNUSED_PARAMETER(p) (void)p

int main(int argc, char ** argv, char ** envp)
{
	SLBT_UNUSED_PARAMETER(argc);
	return slbt_main(argv,envp,0);
}
