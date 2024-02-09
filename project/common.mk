API_SRCS = \
	src/arbits/slbt_archive_ctx.c \
	src/arbits/slbt_archive_merge.c \
	src/arbits/slbt_archive_meta.c \
	src/arbits/slbt_archive_store.c \
	src/arbits/slbt_armap_bsd_32.c \
	src/arbits/slbt_armap_bsd_64.c \
	src/arbits/slbt_armap_sysv_32.c \
	src/arbits/slbt_armap_sysv_64.c \
	src/arbits/output/slbt_ar_output_arname.c \
	src/arbits/output/slbt_ar_output_members.c \
	src/driver/slbt_amain.c \
	src/driver/slbt_driver_ctx.c \
	src/driver/slbt_host_params.c \
	src/driver/slbt_split_argv.c \
	src/fallback/slbt_archive_import_mri.c \
	src/helper/slbt_archive_import.c \
	src/helper/slbt_copy_file.c \
	src/helper/slbt_dump_machine.c \
	src/helper/slbt_map_input.c \
	src/helper/slbt_realpath.c \
	src/logic/slbt_exec_ar.c \
	src/logic/slbt_exec_compile.c \
	src/logic/slbt_exec_ctx.c \
	src/logic/slbt_exec_execute.c \
	src/logic/slbt_exec_install.c \
	src/logic/slbt_exec_link.c \
	src/logic/slbt_exec_uninstall.c \
	src/output/slbt_output_config.c \
	src/output/slbt_output_error.c \
	src/output/slbt_output_exec.c \
	src/output/slbt_output_fdcwd.c \
	src/output/slbt_output_features.c \
	src/output/slbt_output_machine.c \
	src/skin/slbt_skin_ar.c \
	src/skin/slbt_skin_default.c \
	src/skin/slbt_skin_install.c \
	src/skin/slbt_skin_uninstall.c \

INTERNAL_SRCS = \
	src/internal/$(PACKAGE)_dprintf_impl.c \
	src/internal/$(PACKAGE)_errinfo_impl.c \
	src/internal/$(PACKAGE)_lconf_impl.c \
	src/internal/$(PACKAGE)_libmeta_impl.c \
	src/internal/$(PACKAGE)_mapfile_impl.c \
	src/internal/$(PACKAGE)_objlist_impl.c \
	src/internal/$(PACKAGE)_objmeta_impl.c \
	src/internal/$(PACKAGE)_symlink_impl.c \

APP_SRCS = \
	src/slibtool.c

COMMON_SRCS = $(API_SRCS) $(INTERNAL_SRCS)
