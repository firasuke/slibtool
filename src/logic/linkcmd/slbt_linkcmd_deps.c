/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2021  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_linkcmd_impl.h"
#include "slibtool_mapfile_impl.h"
#include "slibtool_metafile_impl.h"
#include "slibtool_snprintf_impl.h"

int slbt_get_deps_meta(
	const struct slbt_driver_ctx *	dctx,
	char *				libfilename,
	int				fexternal,
	struct slbt_deps_meta *		depsmeta)
{
	int			fdcwd;
	char *			ch;
	char *			cap;
	char *			base;
	size_t			libexlen;
	struct stat		st;
	struct slbt_map_info *	mapinfo;
	char			depfile[PATH_MAX];

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* -rpath */
	if (slbt_snprintf(depfile,sizeof(depfile),
				"%s.slibtool.rpath",
				libfilename) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	/* -Wl,%s */
	if (!fstatat(fdcwd,depfile,&st,AT_SYMLINK_NOFOLLOW)) {
		depsmeta->infolen += st.st_size + 4;
		depsmeta->infolen++;
	}

	/* .deps */
	if (slbt_snprintf(depfile,sizeof(depfile),
				"%s.slibtool.deps",
				libfilename) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	/* mapinfo */
	if (!(mapinfo = slbt_map_file(fdcwd,depfile,SLBT_MAP_INPUT)))
		return (fexternal && (errno == ENOENT))
			? 0 : SLBT_SYSTEM_ERROR(dctx,depfile);

	/* copied length */
	depsmeta->infolen += mapinfo->size;
	depsmeta->infolen++;

	/* libexlen */
	libexlen = (base = strrchr(libfilename,'/'))
		? strlen(depfile) + 2 + (base - libfilename)
		: strlen(depfile) + 2;

	/* iterate */
	ch  = mapinfo->addr;
	cap = mapinfo->cap;

	for (; ch<cap; ) {
		if (*ch++ == '\n') {
			depsmeta->infolen += libexlen;
			depsmeta->depscnt++;
		}
	}

	slbt_unmap_file(mapinfo);

	return 0;
}


int slbt_exec_link_create_dep_file(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char **				altv,
	const char *			libfilename,
	bool				farchive)
{
	int			ret;
	int			deps;
	int			slen;
	int			fdcwd;
	char **			parg;
	char *			popt;
	char *			plib;
	char *			path;
	char *			mark;
	char *			base;
	size_t			size;
	char			deplib [PATH_MAX];
	bool			is_reladir;
	char			reladir[PATH_MAX];
	char			depfile[PATH_MAX];
	struct stat		st;
	int			ldepth;
	int			fdyndep;
	struct slbt_map_info *  mapinfo;

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* depfile */
	if (slbt_snprintf(depfile,sizeof(depfile),
			"%s.slibtool.deps",
			libfilename) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	/* deps */
	if ((deps = openat(fdcwd,depfile,O_RDWR|O_CREAT|O_TRUNC,0644)) < 0)
		return SLBT_SYSTEM_ERROR(dctx,depfile);

	/* iterate */
	for (parg=altv; *parg; parg++) {
		popt    = 0;
		plib    = 0;
		path    = 0;
		mapinfo = 0;

		if (!strncmp(*parg,"-l",2)) {
			popt = *parg;
			plib = popt + 2;

		} else if (!strncmp(*parg,"-L",2)) {
			popt = *parg;
			path = popt + 2;

		} else if (!strncmp(*parg,"-f",2)) {
			(void)0;

		} else if ((popt = strrchr(*parg,'.')) && !strcmp(popt,".la")) {
			/* import dependency list */
			if ((base = strrchr(*parg,'/')))
				base++;
			else
				base = *parg;

			/* [relative .la directory] */
			if (base > *parg) {
				slen = slbt_snprintf(
					reladir,
					sizeof(reladir),
					"%s",*parg);

				if (slen < 0) {
					close(deps);
					return SLBT_BUFFER_ERROR(dctx);
				}

				is_reladir = true;
				reladir[base - *parg - 1] = 0;
			} else {
				is_reladir = false;
				reladir[0] = '.';
				reladir[1] = 0;
			}


			/* dynamic library dependency? */
			strcpy(depfile,*parg);
			mark = depfile + (base - *parg);
			size = sizeof(depfile) - (base - *parg);
			slen = slbt_snprintf(mark,size,".libs/%s",base);

			if (slen < 0) {
				close(deps);
				return SLBT_BUFFER_ERROR(dctx);
			}

			mark = strrchr(mark,'.');
			strcpy(mark,dctx->cctx->settings.dsosuffix);

			fdyndep = !fstatat(fdcwd,depfile,&st,0);

			/* [-L... as needed] */
			if (fdyndep && (ectx->ldirdepth >= 0)) {
				if (slbt_dprintf(deps,"-L") < 0) {
					close(deps);
					return SLBT_SYSTEM_ERROR(dctx,0);
				}

				for (ldepth=ectx->ldirdepth; ldepth; ldepth--) {
					if (slbt_dprintf(deps,"../") < 0) {
						close(deps);
						return SLBT_SYSTEM_ERROR(dctx,0);
					}
				}

				if (slbt_dprintf(deps,"%s%s.libs\n",
						 (is_reladir ? reladir : ""),
						 (is_reladir ? "/" : "")) < 0) {
					close(deps);
					return SLBT_SYSTEM_ERROR(dctx,0);
				}
			}

			/* -ldeplib */
			if (fdyndep) {
				*popt = 0;
				mark  = base;
				mark += strlen(dctx->cctx->settings.dsoprefix);

				if (slbt_dprintf(deps,"-l%s\n",mark) < 0) {
					close(deps);
					return SLBT_SYSTEM_ERROR(dctx,0);
				}

				*popt = '.';
			}

			/* [open dependency list] */
			strcpy(depfile,*parg);
			mark = depfile + (base - *parg);
			size = sizeof(depfile) - (base - *parg);
			slen = slbt_snprintf(mark,size,".libs/%s",base);

			if (slen < 0) {
				close(deps);
				return SLBT_BUFFER_ERROR(dctx);
			}

			mapinfo = 0;

			mark = strrchr(mark,'.');
			size = sizeof(depfile) - (mark - depfile);

			if (!farchive) {
				slen = slbt_snprintf(mark,size,
					"%s.slibtool.deps",
					dctx->cctx->settings.dsosuffix);

				if (slen < 0) {
					close(deps);
					return SLBT_BUFFER_ERROR(dctx);
				}

				mapinfo = slbt_map_file(
					fdcwd,depfile,
					SLBT_MAP_INPUT);

				if (!mapinfo && (errno != ENOENT)) {
					close(deps);
					return SLBT_SYSTEM_ERROR(dctx,0);
				}
			}

			if (!mapinfo) {
				slen = slbt_snprintf(mark,size,
					".a.slibtool.deps");

				if (slen < 0) {
					close(deps);
					return SLBT_BUFFER_ERROR(dctx);
				}

				mapinfo = slbt_map_file(
					fdcwd,depfile,
					SLBT_MAP_INPUT);

				if (!mapinfo) {
					strcpy(mark,".a.disabled");

					if (fstatat(fdcwd,depfile,&st,AT_SYMLINK_NOFOLLOW)) {
						close(deps);
						return SLBT_SYSTEM_ERROR(dctx,depfile);
					}
				}
			}

			/* [-l... as needed] */
			while (mapinfo && (mapinfo->mark < mapinfo->cap)) {
				ret = slbt_mapped_readline(
					dctx,mapinfo,
					deplib,sizeof(deplib));

				if (ret) {
					close(deps);
					return SLBT_NESTED_ERROR(dctx);
				}

				ret = ((deplib[0] == '-')
						&& (deplib[1] == 'L')
						&& (deplib[2] != '/'))
					? slbt_dprintf(
						deps,"-L%s/%s",
						reladir,&deplib[2])
					: slbt_dprintf(
						deps,"%s",
						deplib);

				if (ret < 0) {
					close(deps);
					return SLBT_SYSTEM_ERROR(dctx,0);
				}
			}

			if (mapinfo)
				slbt_unmap_file(mapinfo);
		}

		if (plib && (slbt_dprintf(deps,"-l%s\n",plib) < 0)) {
			close(deps);
			return SLBT_SYSTEM_ERROR(dctx,0);
		}

		if (path && (slbt_dprintf(deps,"-L%s\n",path) < 0)) {
			close(deps);
			return SLBT_SYSTEM_ERROR(dctx,0);
		}
	}

	return 0;
}
