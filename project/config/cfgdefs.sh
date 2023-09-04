# in projects where [ $mb_use_custom_cfgdefs = yes ],
# cfgdefs.sh is invoked from within ./configure via
# . $mb_project_dir/project/cfgdefs.sh

# a successful return from cfgdefs.sh will be followed
# by a second invocation of the config_copy() function,
# reflecting any changes to common config variables
# made by cfgdefs.sh.

# finally, cfgdefs.sh may update the contents of the
# config-time generated cfgdefs.mk.


# sofort's config test framework
. "$mb_project_dir/sofort/cfgtest/cfgtest.sh"


for arg ; do
	case "$arg" in
		*)
			error_msg ${arg#}: "unsupported config argument."
			exit 2
	esac
done


cfgdefs_output_custom_defs()
{
	cat "$mb_project_dir/project/config/cfgdefs.in" > cfgdefs.mk
}


cfgdefs_perform_common_tests()
{
	# headers
	cfgtest_header_presence 'sys/syscall.h'
}


cfgdefs_perform_target_tests()
{
	# init
	cfgtest_newline
	cfgtest_host_section

	# common tests
	cfgdefs_perform_common_tests

	# pretty cfgdefs.mk
	cfgtest_newline
}

# cfgdefs.in --> cfgdefs.mk
cfgdefs_output_custom_defs

# strict: some tests might fail
set +e

# strict: some tests might fail
set +e

# target-specific tests
cfgdefs_perform_target_tests

# strict: restore mode
set -e

# all done
return 0
