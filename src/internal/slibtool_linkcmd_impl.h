#ifndef SLIBTOOL_LINKCMD_IMPL_H
#define SLIBTOOL_LINKCMD_IMPL_H

struct slbt_deps_meta {
	char ** altv;
	char *	args;
	int	depscnt;
	int	infolen;
};

int slbt_get_deps_meta(
	const struct slbt_driver_ctx *	dctx,
	char *				libfilename,
	int				fexternal,
	struct slbt_deps_meta *		depsmeta);

int slbt_exec_link_create_dep_file(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char **				altv,
	const char *			libfilename,
	bool				farchive);

bool slbt_adjust_object_argument(
	char *		arg,
	bool		fpic,
	bool		fany,
	int		fdcwd);

bool slbt_adjust_wrapper_argument(
	char *		arg,
	bool		fpic);

int slbt_adjust_linker_argument(
	const struct slbt_driver_ctx *	dctx,
	char *				arg,
	char **				xarg,
	bool				fpic,
	const char *			dsosuffix,
	const char *			arsuffix,
	struct slbt_deps_meta * 	depsmeta);

int slbt_exec_link_adjust_argument_vector(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	struct slbt_deps_meta *		depsmeta,
	const char *			cwd,
	bool				flibrary);

int slbt_exec_link_finalize_argument_vector(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx);

int slbt_exec_link_create_host_tag(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				deffilename);

int slbt_exec_link_create_import_library(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				impfilename,
	char *				deffilename,
	char *				soname);

#endif
