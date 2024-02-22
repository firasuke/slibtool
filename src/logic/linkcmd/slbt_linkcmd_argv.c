/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <slibtool/slibtool.h>
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_spawn_impl.h"
#include "slibtool_linkcmd_impl.h"
#include "slibtool_mapfile_impl.h"
#include "slibtool_metafile_impl.h"
#include "slibtool_snprintf_impl.h"
#include "slibtool_symlink_impl.h"
#include "slibtool_readlink_impl.h"
#include "slibtool_visibility_impl.h"


static int slbt_linkcmd_exit(
	struct slbt_deps_meta *	depsmeta,
	int			ret)
{
	if (depsmeta->altv)
		free(depsmeta->altv);

	if (depsmeta->args)
		free(depsmeta->args);

	return ret;
}


static int slbt_emit_fdwrap_amend_dl_path(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	struct slbt_deps_meta * 	depsmeta,
	const char *			fmt,
					...)
{
	va_list		ap;
	char *		buf;
	int		cnt;
	char		dlpathbuf[2048];
	int		fdwrap;
	const char *	fdwrap_fmt;
	int		size;

	va_start(ap,fmt);

	size = sizeof(dlpathbuf);

	buf  = ((cnt = vsnprintf(dlpathbuf,size,fmt,ap)) < size)
		? dlpathbuf : malloc((size = cnt + 1));

	va_end(ap);

	if (buf == dlpathbuf) {
		(void)0;

	} else if (buf) {
		va_start(ap,fmt);
		vsprintf(buf,fmt,ap);
		va_end(ap);

	} else {
		return slbt_linkcmd_exit(
			depsmeta,
			SLBT_SYSTEM_ERROR(dctx,0));
	}

	if ((fdwrap = slbt_exec_get_fdwrapper(ectx)) >= 0) {
		if (buf[0] == '/') {
			fdwrap_fmt =
				"DL_PATH=\"${DL_PATH}${COLON}%s\"\n"
				"COLON=':'\n\n";
		} else {
			fdwrap_fmt =
				"DL_PATH=\"${DL_PATH}${COLON}${DL_PATH_FIXUP}%s\"\n"
				"COLON=':'\n\n";
		}

		if (slbt_dprintf(fdwrap,fdwrap_fmt,buf) < 0) {
			return slbt_linkcmd_exit(
				depsmeta,
				SLBT_SYSTEM_ERROR(dctx,0));
		}
	}

	return 0;
}


slbt_hidden bool slbt_adjust_object_argument(
	char *		arg,
	bool		fpic,
	bool		fany,
	int		fdcwd)
{
	char *	slash;
	char *	dot;
	char	base[PATH_MAX];

	if (*arg == '-')
		return false;

	/* object argument: foo.lo or foo.o */
	if (!(dot = strrchr(arg,'.')))
		return false;

	if ((dot[1]=='l') && (dot[2]=='o') && !dot[3]) {
		dot[1] = 'o';
		dot[2] = 0;

	} else if ((dot[1]=='o') && !dot[2]) {
		(void)0;

	} else {
		return false;
	}

	/* foo.o requested and is present? */
	if (!fpic && !faccessat(fdcwd,arg,0,0))
		return true;

	/* .libs/foo.o */
	if ((slash = strrchr(arg,'/')))
		slash++;
	else
		slash = arg;

	if (slbt_snprintf(base,sizeof(base),
			"%s",slash) < 0)
		return false;

	sprintf(slash,".libs/%s",base);

	if (!faccessat(fdcwd,arg,0,0))
		return true;

	/* foo.o requested and neither is present? */
	if (!fpic) {
		strcpy(slash,base);
		return true;
	}

	/* .libs/foo.o explicitly requested and is not present? */
	if (!fany)
		return true;

	/* use foo.o in place of .libs/foo.o */
	strcpy(slash,base);

	if (faccessat(fdcwd,arg,0,0))
		sprintf(slash,".libs/%s",base);

	return true;
}


slbt_hidden bool slbt_adjust_wrapper_argument(
	char *		arg,
	bool		fpic)
{
	char *	slash;
	char *	dot;
	char	base[PATH_MAX];

	if (*arg == '-')
		return false;

	if (!(dot = strrchr(arg,'.')))
		return false;

	if (strcmp(dot,".la"))
		return false;

	if (fpic) {
		if ((slash = strrchr(arg,'/')))
			slash++;
		else
			slash = arg;

		if (slbt_snprintf(base,sizeof(base),
				"%s",slash) < 0)
			return false;

		sprintf(slash,".libs/%s",base);
		dot = strrchr(arg,'.');
	}

	strcpy(dot,".a");
	return true;
}


slbt_hidden int slbt_adjust_linker_argument(
	const struct slbt_driver_ctx *	dctx,
	char *				arg,
	char **				xarg,
	bool				fpic,
	const char *			dsosuffix,
	const char *			arsuffix,
	struct slbt_deps_meta * 	depsmeta)
{
	int	fdcwd;
	int	fdlib;
	char *	slash;
	char *	dot;
	char	base[PATH_MAX];

	if (*arg == '-')
		return 0;

	if (!(dot = strrchr(arg,'.')))
		return 0;

	if (!(strcmp(dot,arsuffix))) {
		*xarg = arg;
		return slbt_get_deps_meta(dctx,arg,1,depsmeta);
	}

	if (!(strcmp(dot,dsosuffix)))
		return slbt_get_deps_meta(dctx,arg,1,depsmeta);

	if (strcmp(dot,".la"))
		return 0;

	if (fpic) {
		if ((slash = strrchr(arg,'/')))
			slash++;
		else
			slash = arg;

		if (slbt_snprintf(base,sizeof(base),
				"%s",slash) < 0)
			return 0;

		sprintf(slash,".libs/%s",base);
		dot = strrchr(arg,'.');
	}

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* shared library dependency? */
	if (fpic) {
		sprintf(dot,"%s",dsosuffix);

		if (slbt_symlink_is_a_placeholder(fdcwd,arg))
			sprintf(dot,"%s",arsuffix);
		else if ((fdlib = openat(fdcwd,arg,O_RDONLY)) >= 0)
			close(fdlib);
		else
			sprintf(dot,"%s",arsuffix);

		return slbt_get_deps_meta(dctx,arg,0,depsmeta);
	}

	/* input archive */
	sprintf(dot,"%s",arsuffix);
	return slbt_get_deps_meta(dctx,arg,0,depsmeta);
}


slbt_hidden int slbt_exec_link_adjust_argument_vector(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	struct slbt_deps_meta *		depsmeta,
	const char *			cwd,
	bool				flibrary)
{
	int			fd;
	int			fdcwd;
	char ** 		carg;
	char ** 		aarg;
	char *			ldir;
	char *			slash;
	char *			mark;
	char *			darg;
	char *			dot;
	char *			base;
	char *			dpath;
	int			argc;
	char			arg[PATH_MAX];
	char			lib[PATH_MAX];
	char			depdir  [PATH_MAX];
	char			rpathdir[PATH_MAX];
	char			rpathlnk[PATH_MAX];
	struct stat		st;
	size_t			size;
	size_t			dlen;
	struct slbt_map_info *	mapinfo = 0;
	bool			fwholearchive = false;
	int			ret;

	for (argc=0,carg=ectx->cargv; *carg; carg++)
		argc++;

	if (!(depsmeta->args = calloc(1,depsmeta->infolen)))
		return SLBT_SYSTEM_ERROR(dctx,0);

	argc *= 3;
	argc += depsmeta->depscnt;

	if (!(depsmeta->altv = calloc(argc,sizeof(char *))))
		return slbt_linkcmd_exit(
			depsmeta,
			SLBT_SYSTEM_ERROR(dctx,0));

	fdcwd = slbt_driver_fdcwd(dctx);

	carg = ectx->cargv;
	aarg = depsmeta->altv;
	darg = depsmeta->args;
	size = depsmeta->infolen;

	for (; *carg; ) {
		dpath = 0;

		if (!strcmp(*carg,"-Wl,--whole-archive"))
			fwholearchive = true;
		else if (!strcmp(*carg,"-Wl,--no-whole-archive"))
			fwholearchive = false;



		/* output annotation */
		if (carg == ectx->lout[0]) {
			ectx->mout[0] = &aarg[0];
			ectx->mout[1] = &aarg[1];
		}

		/* argument translation */
		mark = *carg;

		if ((mark[0] == '-') && (mark[1] == 'L')) {
			if (mark[2]) {
				ldir = &mark[2];
			} else {
				*aarg++ = *carg++;
				ldir    = *carg;
			}

			mark = ldir + strlen(ldir);

			if (mark[-1] == '/')
				strcpy(mark,".libs");
			else
				strcpy(mark,"/.libs");

			if ((fd = openat(fdcwd,ldir,O_DIRECTORY,0)) < 0)
				*mark = 0;
			else {
				close(fd);

				if ((ret = slbt_emit_fdwrap_amend_dl_path(
						dctx,ectx,depsmeta,
						"%s",ldir)) < 0)
					return ret;
			}

			*aarg++ = *carg++;

		} else if (**carg == '-') {
			*aarg++ = *carg++;

		} else if (!(dot = strrchr(*carg,'.'))) {
			*aarg++ = *carg++;

		} else if (ectx->xargv[carg - ectx->cargv]) {
			*aarg++ = *carg++;

		} else if (!(strcmp(dot,".a"))) {
			if (flibrary && !fwholearchive) {
				strcpy(lib,*carg);
				dot = strrchr(lib,'.');
				strcpy(dot,".lai");

				if ((fd = openat(fdcwd,lib,O_RDONLY,0)) < 0)
					*aarg++ = "-Wl,--whole-archive";
			}

			dpath = lib;
			sprintf(lib,"%s.slibtool.deps",*carg);
			*aarg++ = *carg++;

			if (flibrary && !fwholearchive) {
				if (fd < 0) {
					*aarg++ = "-Wl,--no-whole-archive";
				} else {
					close(fd);
				}
			}

		} else if (strcmp(dot,dctx->cctx->settings.dsosuffix)) {
			*aarg++ = *carg++;

		} else if (carg == ectx->lout[1]) {
			/* ^^^hoppla^^^ */
			*aarg++ = *carg++;
		} else {
			/* -rpath */
			sprintf(rpathlnk,"%s.slibtool.rpath",*carg);

			if (!fstatat(fdcwd,rpathlnk,&st,AT_SYMLINK_NOFOLLOW)) {
				if (slbt_readlinkat(
						fdcwd,
						rpathlnk,
						rpathdir,
						sizeof(rpathdir)))
					return slbt_linkcmd_exit(
						depsmeta,
						SLBT_SYSTEM_ERROR(dctx,rpathlnk));

				sprintf(darg,"-Wl,%s",rpathdir);
				*aarg++ = "-Wl,-rpath";
				*aarg++ = darg;
				darg   += strlen(darg);
				darg++;
			}

			dpath = lib;
			sprintf(lib,"%s.slibtool.deps",*carg);

			/* account for {'-','L','-','l'} */
			if (slbt_snprintf(arg,
					sizeof(arg) - 4,
					"%s",*carg) < 0)
				return slbt_linkcmd_exit(
					depsmeta,
					SLBT_BUFFER_ERROR(dctx));

			if ((slash = strrchr(arg,'/'))) {
				sprintf(*carg,"-L%s",arg);

				mark   = strrchr(*carg,'/');
				*mark  = 0;
				*slash = 0;

				if ((ret = slbt_emit_fdwrap_amend_dl_path(
						dctx,ectx,depsmeta,
						"%s%s%s",
						((arg[0] == '/') ? "" : cwd),
						((arg[0] == '/') ? "" : "/"),
						arg)) < 0) {
					return ret;
				}

				dlen = strlen(dctx->cctx->settings.dsoprefix);

				/* -module? (todo: non-portable usage, display warning) */
				if (strncmp(++slash,dctx->cctx->settings.dsoprefix,dlen)) {
					*--slash = '/';
					strcpy(*carg,arg);
					*aarg++ = *carg++;
				} else {
					*aarg++ = *carg++;
					*aarg++ = ++mark;

					slash += dlen;

					sprintf(mark,"-l%s",slash);
					dot  = strrchr(mark,'.');
					*dot = 0;
				}
			} else {
				*aarg++ = *carg++;
			}
		}

		if (dpath && !fstatat(fdcwd,dpath,&st,0)) {
			if (!(mapinfo = slbt_map_file(
					fdcwd,dpath,
					SLBT_MAP_INPUT)))
				return slbt_linkcmd_exit(
					depsmeta,
					SLBT_SYSTEM_ERROR(dctx,dpath));

			if (!(strncmp(lib,".libs/",6))) {
				*aarg++ = "-L.libs";
				lib[1] = 0;
			} else if ((base = strrchr(lib,'/'))) {
				if (base - lib == 5) {
					if (!(strncmp(&base[-5],".libs/",6)))
						base -= 4;

				} else if (base - lib >= 6) {
					if (!(strncmp(&base[-6],"/.libs/",7)))
						base -= 6;
				}

				*base = 0;
			} else {
				lib[0] = '.';
				lib[1] = 0;
			}

			while (mapinfo->mark < mapinfo->cap) {
				if (slbt_mapped_readline(dctx,mapinfo,darg,size))
					return slbt_linkcmd_exit(
						depsmeta,
						SLBT_NESTED_ERROR(dctx));

				*aarg++   = darg;
				mark      = darg;

				dlen      = strlen(darg);
				size     -= dlen;
				darg     += dlen;
				darg[-1]  = 0;

				/* handle -L... as needed */
				if ((mark[0] == '-')
						&& (mark[1] == 'L')
						&& (mark[2] != '/')) {
					if (strlen(mark) >= sizeof(depdir) - 1)
						return slbt_linkcmd_exit(
							depsmeta,
							SLBT_BUFFER_ERROR(dctx));

					darg = mark;
					strcpy(depdir,&mark[2]);
					sprintf(darg,"-L%s/%s",lib,depdir);

					darg += strlen(darg);
					darg++;

					if ((ret = slbt_emit_fdwrap_amend_dl_path(
							dctx,ectx,depsmeta,
							"%s/%s",lib,depdir)) < 0)
						return ret;

				} else if ((mark[0] == '-') && (mark[1] == 'L')) {
					if ((ret = slbt_emit_fdwrap_amend_dl_path(
							dctx,ectx,depsmeta,
							"%s",&mark[2])) < 0)
						return ret;
				}
			}
		}

		if (mapinfo) {
			slbt_unmap_file(mapinfo);
			mapinfo = 0;
		}
	}

	if (dctx->cctx->drvflags & SLBT_DRIVER_EXPORT_DYNAMIC)
		*aarg++ = "-Wl,--export-dynamic";

	return 0;
}


static int slbt_exec_link_remove_file(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			target)
{
	int fdcwd;

	(void)ectx;

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* remove target (if any) */
	if (!unlinkat(fdcwd,target,0) || (errno == ENOENT))
		return 0;

	return SLBT_SYSTEM_ERROR(dctx,0);
}


static int slbt_exec_link_create_expsyms_archive(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_exec_ctx *          ectx,
	char **                         lobjv,
	char **                         cnvlv)
{
	int                             ret;
	char *                          dot;
	char **                         argv;
	char **                         aarg;
	char **                         parg;
	struct slbt_archive_ctx *       arctx;
	char **                         ectx_argv;
	char *                          ectx_program;
	char                            program[PATH_MAX];
	char                            output [PATH_MAX];

	/* output */
	if (slbt_snprintf(output,sizeof(output),
			"%s",ectx->mapfilename) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	if (!(dot = strrchr(output,'.')))
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_FLOW_ERROR);

	dot[1] = 'a';
	dot[2] = '\0';

	/* tool-specific argument vector */
	argv = (slbt_get_driver_ictx(dctx))->host.ar_argv;

	/* ar alternate argument vector */
	if (!argv)
		if (slbt_snprintf(program,sizeof(program),
				"%s",dctx->cctx->host.ar) < 0)
			return SLBT_BUFFER_ERROR(dctx);

	/* ar command argument vector */
	aarg = lobjv;

	if ((parg = argv)) {
		for (; *parg; )
			*aarg++ = *parg++;
	} else {
		*aarg++ = program;
	}

	*aarg++ = "-crs";
	*aarg++ = output;

	ectx_argv     = ectx->argv;
	ectx_program  = ectx->program;

	ectx->argv    = lobjv;
	ectx->program = ectx->argv[0];

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(ectx))
			return SLBT_NESTED_ERROR(dctx);

	/* remove old archive as needed */
	if (slbt_exec_link_remove_file(dctx,ectx,output))
		return SLBT_NESTED_ERROR(dctx);

	/* ar spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return SLBT_SPAWN_ERROR(dctx);

	} else if (ectx->exitcode) {
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_ERROR);
	}

	/* restore link command ectx */
	ectx->argv    = ectx_argv;
	ectx->program = ectx_program;

	/* input objects associated with .la archives */
	for (parg=cnvlv; *parg; parg++)
		if (slbt_util_import_archive(ectx,output,*parg))
			return SLBT_NESTED_ERROR(dctx);

	/* do the thing */
	if (slbt_ar_get_archive_ctx(dctx,output,&arctx) < 0)
		return SLBT_NESTED_ERROR(dctx);

	ret = slbt_ar_create_mapfile(
		arctx->meta,
		ectx->mapfilename,
		0644);

	slbt_ar_free_archive_ctx(arctx);

	return (ret < 0) ? SLBT_NESTED_ERROR(dctx) : 0;
}


slbt_hidden int slbt_exec_link_finalize_argument_vector(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	char *		sargv[1024];
	char **		sargvbuf;
	char **		base;
	char **		parg;
	char **		pcap;
	char **         argv;
	char **         mark;
	char **		aarg;
	char **		oarg;
	char **		lobj;
	char **		cnvl;
	char **		larg;
	char **		darg;
	char **		earg;
	char **		rarg;
	char **		aargv;
	char **		oargv;
	char **		lobjv;
	char **		cnvlv;
	char **		cap;
	char **		src;
	char **		dst;
	char *		arg;
	char *		dot;
	char *		ccwrap;
	const char *	arsuffix;

	/* vector size */
	base     = ectx->argv;
	arsuffix = dctx->cctx->settings.arsuffix;

	for (parg=base; *parg; parg++)
		(void)0;

	if (dctx->cctx->regex) {
		argv = (slbt_get_driver_ictx(dctx))->host.ar_argv;

		for (mark=argv; mark && *mark; mark++)
			(void)0;
	} else {
		argv = 0;
		mark  = 0;
	}

	/* buffer */
	if ((parg - base) + (mark - argv) < 256) {
		aargv    = &sargv[0];
		oargv    = &sargv[1*256];
		lobjv    = &sargv[2*256];
		cnvlv    = &sargv[3*256];
		sargvbuf = 0;

		parg = &sargv[0];
		pcap = &sargv[1024];

		for (; parg<pcap; )
			*parg++ = 0;

	} else if (!(sargvbuf = calloc(3*(parg-base+1),sizeof(char *)))) {
		return SLBT_SYSTEM_ERROR(dctx,0);

	} else {
		aargv = &sargvbuf[0];
		oargv = &sargvbuf[1*(parg-base+1)];
		lobjv = &sargvbuf[2*(parg-base+1)];
		cnvlv = &sargvbuf[3*(parg-base+1)];
	}

	aarg = aargv;
	oarg = oargv;
	cnvl = cnvlv;
	lobj = lobjv;

	/* -export-symbols-regex: lobjv in place: ar [arg] [arg] -crs <output> */
	if (dctx->cctx->regex && argv)
		lobj += mark - argv + 2;
	else
		lobj += 3;

	/* (program name) */
	parg = &base[1];

	/* split object args from all other args, record output */
	/* annotation, and remove redundant -l arguments; and   */
	/* create additional vectors of all input objects as    */
	/* convenience libraries for -export-symbols-regex.     */
	for (; *parg; ) {
		if (ectx->lout[0] == parg) {
			ectx->lout[0] = &aarg[0];
			ectx->lout[1] = &aarg[1];
		}

		if (ectx->mout[0] == parg) {
			ectx->mout[0] = &aarg[0];
			ectx->mout[1] = &aarg[1];
		}

		arg = *parg;
		dot = strrchr(arg,'.');

		/* object input argument? */
		if (dot && (!strcmp(dot,".o") || !strcmp(dot,".lo"))) {
			*lobj++ = *parg;
			*oarg++ = *parg++;

		/* --whole-archive input argument? */
		} else if ((arg[0] == '-')
				&& (arg[1] == 'W')
				&& (arg[2] == 'l')
				&& (arg[3] == ',')
				&& !strcmp(&arg[4],"--whole-archive")
				&& parg[1] && parg[2]
				&& !strcmp(parg[2],"-Wl,--no-whole-archive")
				&& (dot = strrchr(parg[1],'.'))
				&& !strcmp(dot,arsuffix)) {
			*cnvl++ = parg[1];
			*oarg++ = *parg++;
			*oarg++ = *parg++;
			*oarg++ = *parg++;

		/* local archive input argument? */
		} else if (dot && !strcmp(dot,arsuffix)) {
			*aarg++ = *parg++;

		/* -l argument? */
		} else if ((parg[0][0] == '-') && (parg[0][1] == 'l')) {
			/* find the previous occurence of this -l argument */
			for (rarg=0, larg=&aarg[-1]; !rarg && (larg>=aargv); larg--)
				if (!strcmp(*larg,*parg))
					rarg = larg;

			/* first occurence of this specific -l argument? */
			if (!rarg) {
				*aarg++ = *parg++;

			} else {
				larg = rarg;

				/* if all -l arguments following the previous */
				/* occurence had already appeared before the */
				/* previous argument, then the current      */
				/* occurence is redundant.                 */

				for (darg=&larg[1]; rarg && darg<aarg; darg++) {
					/* only test -l arguments */
					if ((darg[0][0] == '-') && (darg[0][1] == 'l')) {
						for (rarg=0, earg=aargv; !rarg && earg<larg; earg++)
							if (!strcmp(*earg,*darg))
								rarg = darg;
					}
				}

				/* final verdict: repeated -l argument? */
				if (rarg) {
					parg++;

				} else {
					*aarg++ = *parg++;
				}
			}

		/* -L argument? */
		} else if ((parg[0][0] == '-') && (parg[0][1] == 'L')) {
			/* find a previous occurence of this -L argument */
			for (rarg=0, larg=aargv; !rarg && (larg<aarg); larg++)
				if (!strcmp(*larg,*parg))
					rarg = larg;

			/* repeated -L argument? */
			if (rarg) {
				parg++;
			} else {
				*aarg++ = *parg++;
			}

		/* placeholder argument? */
		} else if (!strncmp(*parg,"-USLIBTOOL_PLACEHOLDER_",23)) {
			parg++;

		/* all other arguments */
		} else {
			*aarg++ = *parg++;
		}
	}

	/* export-symbols-regex */
	if (dctx->cctx->regex)
		if (slbt_exec_link_create_expsyms_archive(
				dctx,ectx,lobjv,cnvlv) < 0)
			return SLBT_NESTED_ERROR(dctx);

	/* program name, ccwrap */
	if ((ccwrap = (char *)dctx->cctx->ccwrap)) {
		base[1] = base[0];
		base[0] = ccwrap;
		base++;
	}

	/* join object args */
	src = oargv;
	cap = oarg;
	dst = &base[1];

	for (; src<cap; )
		*dst++ = *src++;

	/* join all other args */
	src = aargv;
	cap = aarg;

	for (; src<cap; )
		*dst++ = *src++;

	/* properly null-terminate argv, accounting for redundant -l arguments */
	*dst = 0;

	/* output annotation */
	if (ectx->lout[0]) {
		ectx->lout[0] = &base[1] + (oarg - oargv) + (ectx->lout[0] - aargv);
		ectx->lout[1] = ectx->lout[0] + 1;
	}

	if (ectx->mout[0]) {
		ectx->mout[0] = &base[1] + (oarg - oargv) + (ectx->mout[0] - aargv);
		ectx->mout[1] = ectx->mout[0] + 1;
	}

	/* all done */
	if (sargvbuf)
		free(sargvbuf);

	return 0;
}
