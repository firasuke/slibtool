#include "slibtool_ar_impl.h"
#include "argv/argv.h"

const struct argv_option slbt_ar_options[] = {
	{"help",	'h',TAG_AR_HELP,ARGV_OPTARG_NONE,0,0,0,
			"display ar mode help"},

	{"Wcheck",	0,TAG_AR_CHECK,ARGV_OPTARG_NONE,
			ARGV_OPTION_HYBRID_ONLY,
			"[ARCHIVE-FILE]",0,
			"verify that %s is a valid archive; "
			"supported variants are BSD, SysV, and PE/COFF"},


	{0,0,0,0,0,0,0,0}
};
