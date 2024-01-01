#include "slibtool_ar_impl.h"
#include "argv/argv.h"

const struct argv_option slbt_ar_options[] = {
	{"help",	'h',TAG_AR_HELP,ARGV_OPTARG_NONE,0,0,0,
			"display ar mode help"},

	{0,0,0,0,0,0,0,0}
};
