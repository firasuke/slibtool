###############################################################################
#                                                                             #
#   slibtool.m4: native slibtool integration for autoconf-based projects      #
#                                                                             #
#   Copyright (C) 2021  Z. Gilboa                                             #
#                                                                             #
#   Permission is hereby granted, free of charge, to any person obtaining     #
#   a copy of this software and associated documentation files (the           #
#   "Software"), to deal in the Software without restriction, including       #
#   without limitation the rights to use, copy, modify, merge, publish,       #
#   distribute, sublicense, and/or sell copies of the Software, and to        #
#   permit persons to whom the Software is furnished to do so, subject to     #
#   the following conditions:                                                 #
#                                                                             #
#   The above copyright notice and this permission notice shall be included   #
#   in all copies or substantial portions of the Software.                    #
#                                                                             #
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS   #
#   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF                #
#   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.    #
#   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY      #
#   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,      #
#   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE         #
#   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                    #
#                                                                             #
###############################################################################



# _SLIBTOOL_DEFAULTS
# ------------------
AC_DEFUN([_SLIBTOOL_DEFAULTS],[
AC_BEFORE([$0],[SLIBTOOL_INIT])
AC_BEFORE([$0],[_SLIBTOOL_ARGUMENT_HANDLING])

# slibtool: implementation defaults
# ---------------------------------
slibtool_enable_shared_default='yes'
slibtool_enable_static_default='yes'
slibtool_enable_dlopen_default='yes'
slibtool_enable_win32_dll_default='yes'
slibtool_enable_fast_install_default='yes'
slibtool_pic_mode_default='default'
])


# _SLIBTOOL_ARGUMENT_HANDLING
# ---------------------------
AC_DEFUN([_SLIBTOOL_ARGUMENT_HANDLING],[
AC_BEFORE([$0],[_SLIBTOOL_ARG_ENABLE])
AC_BEFORE([$0],[_SLIBTOOL_ARG_WITH])

# slibtool: argument handling
# ---------------------------
slibtool_arg_enable()
{
	case "${enableval}" in
		'yes')
			slbt_eval_expr="${slbt_var}='yes'"
			eval $slbt_eval_expr
			;;

		'no')
			slbt_eval_expr="${slbt_var}='no'"
			eval $slbt_eval_expr
			;;

		*)
			slbt_package="${PACKAGE:-default}"

			slbt_eval_expr="${slbt_var}='no'"
			eval $slbt_eval_expr

			slbt_cfg_ifs="${IFS}"
			IFS="${PATH_SEPARATOR}${IFS}"

			for slbt_pkg in ${enableval}; do
				if [ "_${slbt_pkg}" = "_${slbt_package}" ]; then
					slbt_eval_expr="${slbt_var}='yes'"
					eval $slbt_eval_expr
				fi
			done

			IFS="${slbt_cfg_ifs}"
			unset slbt_cfg_ifs
			;;
	esac
}


slibtool_arg_with()
{
	enableval="${withval}"
	slibtool_arg_enable
}
])


# _SLIBTOOL_ARG_ENABLE(_feature_,_help_string_,_var_)
# ---------------------------------------------------
AC_DEFUN([_SLIBTOOL_ARG_ENABLE],[
AC_ARG_ENABLE($1,
[AS_HELP_STRING([--enable-]$1[@<:@=PKGS@:>@],$2  @<:@default=[$]$3_default@:>@)],[
  slbt_var=$3
  slibtool_arg_enable],[dnl
$3=[$]$3_default])
])


# _SLIBTOOL_ARG_WITH(_feature_,_help_string_,_var_)
# -------------------------------------------------
AC_DEFUN([_SLIBTOOL_ARG_WITH],[
AC_ARG_WITH($1,
[AS_HELP_STRING([--with-]$1[@<:@=PKGS@:>@],$2  @<:@default=[$]$3_default@:>@)],[
  slbt_var=$3
  slibtool_arg_with],[dnl
$3=[$]$3_default])
])


# _SLIBTOOL_SET_FLAVOR
# --------------------
AC_DEFUN([_SLIBTOOL_SET_FLAVOR],[
AC_BEFORE([$0],[SLIBTOOL_INIT])

# slibtool: set SLIBTOOL to the default/package-default/user-requested flavor
# ---------------------------------------------------------------------------
slibtool_set_flavor()
{
	case "_${slibtool_enable_shared}_${slibtool_enable_static}" in
		'_yes_yes')
			SLIBTOOL='dlibtool'
			;;

		'_yes_no')
			SLIBTOOL='dlibtool-shared'
			;;

		'_no_yes')
			SLIBTOOL='dlibtool-static'
			;;

		'_no_no')
			SLIBTOOL='false'
			;;

		*)
			SLIBTOOL='false'
			;;
	esac

	# drop-in replacement
	enable_shared=${slibtool_enable_shared}
	enable_static=${slibtool_enable_static}
	enable_dlopen=${slibtool_enable_dlopen}
	enable_win32_dll=${slibtool_enable_win32_dll}
	enable_fast_install=${slibtool_enable_fast_install}
	pic_mode=${slibtool_pic_mode}
}
])


# SLIBTOOL_INIT(_options_)
# ------------------------
AC_DEFUN([SLIBTOOL_INIT],[
AC_REQUIRE([_SLIBTOOL_DEFAULTS])
AC_REQUIRE([_SLIBTOOL_SET_FLAVOR])
AC_REQUIRE([_SLIBTOOL_ARGUMENT_HANDLING])


# slibtool: package defaults
# ---------------------
slbt_cfg_ifs="${IFS}"
IFS="${PATH_SEPARATOR}${IFS}"

for slbt_opt in $@; do
	case "${slbt_opt}" in
		'shared')
			slibtool_enable_shared_default='yes'
			;;

		'disable-shared')
			slibtool_enable_shared_default='no'
			;;

		'static')
			slibtool_enable_static_default='yes'
			;;

		'disable-static')
			slibtool_enable_static_default='no'
			;;

		'dlopen')
			slibtool_enable_dlopen_default='yes'
			;;

		'disable-dlopen')
			slibtool_enable_dlopen_default='no'
			;;

		'win32-dll')
			slibtool_enable_win32_dll_default='yes'
			;;

		'disable-win32-dll')
			slibtool_enable_win32_dll_default='no'
			;;

		'fast-install')
			slibtool_enable_fast_install_default='yes'
			;;

		'disable-fast-install')
			slibtool_enable_fast_install_default='no'
			;;

		'pic-only')
			slibtool_pic_mode_default='yes'
			;;

		'no-pic')
			slibtool_pic_mode_default='no'
			;;
	esac
done

IFS="${slbt_cfg_ifs}"
unset slbt_cfg_ifs


# slibtool: features and argument handline
# ----------------------------------------
_SLIBTOOL_ARG_ENABLE([shared],[build shared libraries],[slibtool_enable_shared])
_SLIBTOOL_ARG_ENABLE([static],[build static libraries],[slibtool_enable_static])
_SLIBTOOL_ARG_ENABLE([dlopen],[allow -dlopen and -dlpreopen],[slibtool_enable_dlopen])
_SLIBTOOL_ARG_ENABLE([win32-dll],[natively support win32 dll's],[slibtool_enable_win32_dll])
_SLIBTOOL_ARG_ENABLE([fast-install],[optimize for fast installation],[slibtool_enable_fast_install])
_SLIBTOOL_ARG_WITH([pic],[override defaults for pic object usage],[slibtool_pic_mode])


# slibtool: set flavor
# --------------------
slibtool_set_flavor
LIBTOOL=${SLIBTOOL}

AC_SUBST([LIBTOOL])
AC_SUBST([SLIBTOOL])
m4_define([SLIBTOOL_INIT])
])


# SLIBTOOL_PREREQ(_VERSION_)
# --------------------------
AC_DEFUN([SLIBTOOL_PREREQ],[
])


# drop-in replacement
# -------------------
AC_DEFUN([LT_INIT],             [SLIBTOOL_INIT($@)])
AC_DEFUN([LT_PREREQ],           [SLIBTOOL_PREREQ($@)])

AC_DEFUN([AC_PROG_LIBTOOL],     [SLIBTOOL_INIT($@)])
AC_DEFUN([AM_PROG_LIBTOOL],     [SLIBTOOL_INIT($@)])
