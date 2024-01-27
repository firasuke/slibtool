#include "slibtool_driver_impl.h"
#include "argv/argv.h"

const struct argv_option slbt_ar_options[] = {
	{"version",	0,TAG_AR_VERSION,ARGV_OPTARG_NONE,0,0,0,
			"show version information"},

	{"help",	'h',TAG_AR_HELP,ARGV_OPTARG_NONE,0,0,0,
			"display ar mode help"},

	{"Wcheck",	0,TAG_AR_CHECK,ARGV_OPTARG_NONE,
			ARGV_OPTION_HYBRID_ONLY,
			"[ARCHIVE-FILE]",0,
			"verify that %s is a valid archive; "
			"supported variants are BSD, SysV, and PE/COFF"},

	{"Wprint",	0,TAG_AR_PRINT,ARGV_OPTARG_OPTIONAL,
			ARGV_OPTION_HYBRID_EQUAL|ARGV_OPTION_HYBRID_COMMA,
			"members",0,
			"print out information pertaining to each archive file "
			"and its various internal elements"},

	{"Wpretty",	0,TAG_AR_PRETTY,ARGV_OPTARG_REQUIRED,
			ARGV_OPTION_HYBRID_EQUAL,
			"posix|yaml|hexdata",0,
			"select the pretty printer to be used: "
			"'posix' for ar(1) compatible output; "
			"'yaml' for yaml-formatted data; and "
			"'hexdata' for yaml-formatted data with additional "
			"hexdump output"},

	{0,0,0,0,0,0,0,0}
};
