#include "slibtool_driver_impl.h"
#include "slibtool_visibility_impl.h"
#include "argv/argv.h"

const slbt_hidden struct argv_option slbt_stoolie_options[] = {
	{"version",	0,TAG_STLE_VERSION,ARGV_OPTARG_NONE,0,0,0,
			"show version information"},

	{"help",	'h',TAG_STLE_HELP,ARGV_OPTARG_NONE,0,0,0,
			"display slibtoolize (stoolie) mode help"},

	{"copy",	'c',TAG_STLE_COPY,ARGV_OPTARG_NONE,0,0,0,
			"copy build-auxiliary m4 files "
			"(create symbolic links otherwise."},

	{"force",	'f',TAG_STLE_FORCE,ARGV_OPTARG_NONE,0,0,0,
			"replace existing build-auxiliary and m4 "
			"files and/or symbolic links."},

	{"install",	'i',TAG_STLE_INSTALL,ARGV_OPTARG_NONE,0,0,0,
			"create symbolic links to, or otherwise copy "
			"missing build-auxiliary and m4 files."},

	{"debug",	'd',TAG_STLE_DEBUG,ARGV_OPTARG_NONE,0,0,0,
			"display internal debug information."},

	{"dry-run",	'n',TAG_STLE_DRY_RUN,ARGV_OPTARG_NONE,0,0,0,
			"do not spawn any processes, "
			"do not make any changes to the file system."},

	{0,0,0,0,0,0,0,0}
};
