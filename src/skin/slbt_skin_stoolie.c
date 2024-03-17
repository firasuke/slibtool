#include "slibtool_driver_impl.h"
#include "slibtool_visibility_impl.h"
#include "argv/argv.h"

const slbt_hidden struct argv_option slbt_stoolie_options[] = {
	{"version",	0,TAG_STLE_VERSION,ARGV_OPTARG_NONE,0,0,0,
			"show version information"},

	{"help",	'h',TAG_STLE_HELP,ARGV_OPTARG_NONE,0,0,0,
			"display slibtoolize (stoolie) mode help"},

	{0,0,0,0,0,0,0,0}
};
