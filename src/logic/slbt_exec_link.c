/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2021  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <slibtool/slibtool.h>
#include "slibtool_spawn_impl.h"
#include "slibtool_mkdir_impl.h"
#include "slibtool_driver_impl.h"
#include "slibtool_dprintf_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_mapfile_impl.h"
#include "slibtool_metafile_impl.h"
#include "slibtool_readlink_impl.h"
#include "slibtool_snprintf_impl.h"
#include "slibtool_symlink_impl.h"

struct slbt_deps_meta {
	char ** altv;
	char *	args;
	int	depscnt;
	int	infolen;
};

/*******************************************************************/
/*                                                                 */
/* -o <ltlib>  switches              input   result                */
/* ----------  --------------------- -----   ------                */
/* libfoo.a    [-shared|-static]     bar.lo  libfoo.a              */
/*                                                                 */
/* ar -crs libfoo.a bar.o                                          */
/*                                                                 */
/*******************************************************************/

/*******************************************************************/
/*                                                                 */
/* -o <ltlib>  switches              input   result                */
/* ----------  --------------------- -----   ------                */
/* libfoo.la   -shared               bar.lo  libfoo.la             */
/*                                           .libs/libfoo.a        */
/*                                           .libs/libfoo.la (lnk) */
/*                                                                 */
/* ar -crs .libs/libfoo.a .libs/bar.o                              */
/* (generate libfoo.la)                                            */
/* ln -s ../libfoo.la .libs/libfoo.la                              */
/*                                                                 */
/*******************************************************************/

/*******************************************************************/
/*                                                                 */
/* -o <ltlib>  switches              input   result                */
/* ----------  --------------------- -----   ------                */
/* libfoo.la   -static               bar.lo  libfoo.la             */
/*                                           .libs/libfoo.a        */
/*                                           .libs/libfoo.la (lnk) */
/*                                                                 */
/* ar -crs .libs/libfoo.a bar.o                                    */
/* (generate libfoo.la)                                            */
/* ln -s ../libfoo.la .libs/libfoo.la                              */
/*                                                                 */
/*******************************************************************/

static int slbt_exec_link_exit(
	struct slbt_deps_meta *	depsmeta,
	int			ret)
{
	if (depsmeta->altv)
		free(depsmeta->altv);

	if (depsmeta->args)
		free(depsmeta->args);

	return ret;
}

static int slbt_get_deps_meta(
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

static bool slbt_adjust_object_argument(
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

static bool slbt_adjust_wrapper_argument(
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

static int slbt_adjust_linker_argument(
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
		return slbt_exec_link_exit(
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
			return slbt_exec_link_exit(
				depsmeta,
				SLBT_SYSTEM_ERROR(dctx,0));
		}
	}

	return 0;
}

static void slbt_emit_fdwrap_dl_path_fixup(
	char *	cwd,
	char *	dpfixup,
	size_t	dpfixup_size,
	char *	wrapper)
{
	char *	p;
	char *	q;
	char *	wrapper_dname;

	/* obtain cwd-relative directory name of wrapper */
	for (p=cwd,q=wrapper; *p && *q && (*p==*q); p++,q++)
		(void)0;

	wrapper_dname = (*q == '/') ? (q + 1) : q;

	dpfixup[0] = 0; strncat(dpfixup,"${0%/*}",dpfixup_size - 1);

	/* append parent directory fixup for each level of depth in wrapper_dname */
	for (p=wrapper_dname,q=0; *p; ) {
		if ((p[0] == '.') && (p[1] == '/')) {
			p++; p++;
		} else if ((q = strchr(p, '/'))) {
			strncat(dpfixup,"/..",dpfixup_size-1); p = (q + 1);
		} else {
			break;
		}
	}

	strncat(dpfixup,"/",dpfixup_size-1);
}

static int slbt_exec_link_adjust_argument_vector(
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
	struct slbt_map_info *	mapinfo;
	bool			fwholearchive = false;
	int			ret;

	for (argc=0,carg=ectx->cargv; *carg; carg++)
		argc++;

	if (!(depsmeta->args = calloc(1,depsmeta->infolen)))
		return SLBT_SYSTEM_ERROR(dctx,0);

	argc *= 3;
	argc += depsmeta->depscnt;

	if (!(depsmeta->altv = calloc(argc,sizeof(char *))))
		return slbt_exec_link_exit(
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
			if (flibrary && !fwholearchive)
				*aarg++ = "-Wl,--whole-archive";

			dpath = lib;
			sprintf(lib,"%s.slibtool.deps",*carg);
			*aarg++ = *carg++;

			if (flibrary && !fwholearchive)
				*aarg++ = "-Wl,--no-whole-archive";

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
					return slbt_exec_link_exit(
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
				return slbt_exec_link_exit(
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
				return slbt_exec_link_exit(
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
					return slbt_exec_link_exit(
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
						return slbt_exec_link_exit(
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
	}

	if (dctx->cctx->drvflags & SLBT_DRIVER_EXPORT_DYNAMIC)
		*aarg++ = "-Wl,--export-dynamic";

	return 0;
}

static int slbt_exec_link_finalize_argument_vector(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	char *		sargv[1024];
	char **		sargvbuf;
	char **		base;
	char **		parg;
	char **		aarg;
	char **		oarg;
	char **		larg;
	char **		darg;
	char **		earg;
	char **		rarg;
	char **		aargv;
	char **		oargv;
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

	/* buffer */
	if (parg - base < 512) {
		aargv    = &sargv[0];
		oargv    = &sargv[512];
		aarg     = aargv;
		oarg     = oargv;
		sargvbuf = 0;

	} else if (!(sargvbuf = calloc(2*(parg-base+1),sizeof(char *)))) {
		return SLBT_SYSTEM_ERROR(dctx,0);

	} else {
		aargv = &sargvbuf[0];
		oargv = &sargvbuf[parg-base+1];
		aarg  = aargv;
		oarg  = oargv;
	}

	/* (program name) */
	parg = &base[1];

	/* split object args from all other args, record output */
	/* annotation, and remove redundant -l arguments       */
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
			*oarg++ = *parg++;
			*oarg++ = *parg++;
			*oarg++ = *parg++;

		/* local archive input argument? */
		} else if (dot && !strcmp(dot,arsuffix)) {
			*oarg++ = *parg++;

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

static int slbt_exec_link_create_dep_file(
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

static int slbt_exec_link_create_host_tag(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				deffilename)
{
	char *  slash;
	char	hosttag[PATH_MAX];
	char	hostlnk[PATH_MAX];

	/* libfoo.so.def.{flavor} */
	if (slbt_snprintf(hosttag,
			sizeof(hosttag),
			"%s.%s",
			deffilename,
			dctx->cctx->host.flavor) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	if (slbt_snprintf(hostlnk,
			sizeof(hostlnk),
			"%s.host",
			deffilename) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	/* libfoo.so.def is under .libs/ */
	if (!(slash = strrchr(deffilename,'/')))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LINK_FLOW);

	if (slbt_create_symlink(
		dctx,ectx,
		deffilename,
		hosttag,
		SLBT_SYMLINK_DEFAULT))
	return SLBT_NESTED_ERROR(dctx);

	/* libfoo.so.def.{flavor} is under .libs/ */
	if (!(slash = strrchr(hosttag,'/')))
		return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LINK_FLOW);

	if (slbt_create_symlink(
			dctx,ectx,
			++slash,
			hostlnk,
			SLBT_SYMLINK_DEFAULT))
		return SLBT_NESTED_ERROR(dctx);

	return 0;
}

static int slbt_exec_link_create_import_library(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	char *				impfilename,
	char *				deffilename,
	char *				soname)
{
	int	fmdso;
	char *	eargv[8];
	char	program[PATH_MAX];

	/* dlltool or mdso? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_IMPLIB_DSOMETA)
		fmdso = 1;

	else if (dctx->cctx->drvflags & SLBT_DRIVER_IMPLIB_DSOMETA)
		fmdso = 0;

	else if (!(strcmp(dctx->cctx->host.flavor,"midipix")))
		fmdso = 1;

	else
		fmdso = 0;

	/* eargv */
	if (fmdso) {
		if (slbt_snprintf(program,sizeof(program),
				"%s",dctx->cctx->host.mdso) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		eargv[0] = program;
		eargv[1] = "-i";
		eargv[2] = impfilename;
		eargv[3] = "-n";
		eargv[4] = soname;
		eargv[5] = deffilename;
		eargv[6] = 0;
	} else {
		if (slbt_snprintf(program,sizeof(program),
				"%s",dctx->cctx->host.dlltool) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		eargv[0] = program;
		eargv[1] = "-l";
		eargv[2] = impfilename;
		eargv[3] = "-d";
		eargv[4] = deffilename;
		eargv[5] = "-D";
		eargv[6] = soname;
		eargv[7] = 0;
	}

	/* alternate argument vector */
	ectx->argv    = eargv;
	ectx->program = program;

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return SLBT_NESTED_ERROR(dctx);

	/* dlltool/mdso spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return SLBT_SPAWN_ERROR(dctx);

	} else if (ectx->exitcode) {
		return SLBT_CUSTOM_ERROR(
			dctx,
			fmdso ? SLBT_ERR_MDSO_ERROR : SLBT_ERR_DLLTOOL_ERROR);
	}

	return 0;
}

static int slbt_exec_link_create_noop_symlink(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			arfilename)
{
	struct stat st;
	int         fdcwd;

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* file exists? */
	if (!fstatat(fdcwd,arfilename,&st,AT_SYMLINK_NOFOLLOW))
		return 0;

	/* needed? */
	if (errno == ENOENT) {
		if (slbt_create_symlink(
				dctx,ectx,
				"/dev/null",
				arfilename,
				SLBT_SYMLINK_LITERAL))
			return SLBT_NESTED_ERROR(dctx);
		return 0;
	}

	return SLBT_SYSTEM_ERROR(dctx,arfilename);
}

static int slbt_exec_link_create_archive(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			arfilename,
	bool				fpic)
{
	int		fdcwd;
	char ** 	aarg;
	char ** 	parg;
	char		program[PATH_MAX];
	char		output [PATH_MAX];

	/* -disable-static? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_STATIC)
		if (dctx->cctx->rpath)
			return slbt_exec_link_create_noop_symlink(
				dctx,ectx,arfilename);

	/* initial state */
	slbt_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* alternate program (ar, ranlib) */
	ectx->program = program;

	/* output */
	if (slbt_snprintf(output,sizeof(output),
			"%s",arfilename) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	/* ar alternate argument vector */
	if (slbt_snprintf(program,sizeof(program),
			"%s",dctx->cctx->host.ar) < 0)
		return SLBT_BUFFER_ERROR(dctx);


	/* fdcwd */
	fdcwd   = slbt_driver_fdcwd(dctx);

	/* input argument adjustment */
	aarg    = ectx->altv;
	*aarg++ = program;
	*aarg++ = "-crs";
	*aarg++ = output;

	for (parg=ectx->cargv; *parg; parg++)
		if (slbt_adjust_object_argument(*parg,fpic,!fpic,fdcwd))
			*aarg++ = *parg;

	*aarg = 0;
	ectx->argv = ectx->altv;

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return SLBT_NESTED_ERROR(dctx);

	/* remove old archive as needed */
	if (slbt_exec_link_remove_file(dctx,ectx,output))
		return SLBT_NESTED_ERROR(dctx);

	/* .deps */
	if (slbt_exec_link_create_dep_file(
			dctx,ectx,ectx->cargv,
			arfilename,true))
		return SLBT_NESTED_ERROR(dctx);

	/* ar spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return SLBT_SPAWN_ERROR(dctx);

	} else if (ectx->exitcode) {
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_ERROR);
	}

	/* input objects associated with .la archives */
	for (parg=ectx->cargv; *parg; parg++)
		if (slbt_adjust_wrapper_argument(*parg,true))
			if (slbt_archive_import(dctx,ectx,output,*parg))
				return SLBT_NESTED_ERROR(dctx);

	return 0;
}

static int slbt_exec_link_create_library(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			dsobasename,
	const char *			dsofilename,
	const char *			relfilename)
{
	int                     fdcwd;
	char **                 parg;
	char **                 xarg;
	char *	                ccwrap;
	const char *            laout;
	const char *            dot;
	char                    cwd    [PATH_MAX];
	char                    output [PATH_MAX];
	char                    soname [PATH_MAX];
	char                    symfile[PATH_MAX];
	struct slbt_deps_meta   depsmeta = {0,0,0,0};

	/* initial state */
	slbt_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* input argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		slbt_adjust_object_argument(*parg,true,false,fdcwd);

	/* .deps */
	if (slbt_exec_link_create_dep_file(
			dctx,ectx,ectx->cargv,
			dsofilename,false))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_NESTED_ERROR(dctx));

	/* linker argument adjustment */
	for (parg=ectx->cargv, xarg=ectx->xargv; *parg; parg++, xarg++)
		if (slbt_adjust_linker_argument(
				dctx,
				*parg,xarg,true,
				dctx->cctx->settings.dsosuffix,
				dctx->cctx->settings.arsuffix,
				&depsmeta) < 0)
			return SLBT_NESTED_ERROR(dctx);

	/* --no-undefined */
	if (dctx->cctx->drvflags & SLBT_DRIVER_NO_UNDEFINED)
		*ectx->noundef = "-Wl,--no-undefined";

	/* -soname */
	dot   = strrchr(dctx->cctx->output,'.');
	laout = (dot && !strcmp(dot,".la"))
			? dctx->cctx->output
			: 0;

	if ((dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_MACHO)) {
		(void)0;

	} else if (!laout && (dctx->cctx->drvflags & SLBT_DRIVER_MODULE)) {
		if (slbt_snprintf(soname,sizeof(soname),
				"-Wl,%s",dctx->cctx->output) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
		*ectx->lsoname = soname;

	} else if (relfilename && dctx->cctx->verinfo.verinfo) {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s-%s%s.%d%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.osdsuffix,
					dctx->cctx->verinfo.major,
					dctx->cctx->settings.osdfussix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
		*ectx->lsoname = soname;

	} else if (relfilename) {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s-%s%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->release,
					dctx->cctx->settings.dsosuffix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
		*ectx->lsoname = soname;

	} else if (dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION) {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->settings.dsosuffix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
		*ectx->lsoname = soname;

	} else {
		if (slbt_snprintf(soname,sizeof(soname),
					"-Wl,%s%s%s.%d%s",
					ectx->sonameprefix,
					dctx->cctx->libname,
					dctx->cctx->settings.osdsuffix,
					dctx->cctx->verinfo.major,
					dctx->cctx->settings.osdfussix) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->soname  = "-Wl,-soname";
		*ectx->lsoname = soname;
	}

	/* PE: --output-def */
	if (dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_PE) {
		if (slbt_snprintf(symfile,sizeof(symfile),
					"-Wl,%s",
					ectx->deffilename) < 0)
			return SLBT_BUFFER_ERROR(dctx);

		*ectx->symdefs = "-Wl,--output-def";
		*ectx->symfile = symfile;
	}

	/* shared/static */
	if (dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC) {
		*ectx->dpic = "-static";
	} else if (dctx->cctx->settings.picswitch) {
		*ectx->dpic = "-shared";
		*ectx->fpic = dctx->cctx->settings.picswitch;
	} else {
		*ectx->dpic = "-shared";
	}

	/* output */
	if (!laout && dctx->cctx->drvflags & SLBT_DRIVER_MODULE) {
		strcpy(output,dctx->cctx->output);
	} else if (relfilename) {
		strcpy(output,relfilename);
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION) {
		strcpy(output,dsofilename);
	} else {
		if (slbt_snprintf(output,sizeof(output),
					"%s%s.%d.%d.%d%s",
					dsobasename,
					dctx->cctx->settings.osdsuffix,
					dctx->cctx->verinfo.major,
					dctx->cctx->verinfo.minor,
					dctx->cctx->verinfo.revision,
					dctx->cctx->settings.osdfussix) < 0)
			return SLBT_BUFFER_ERROR(dctx);
	}

	*ectx->lout[0] = "-o";
	*ectx->lout[1] = output;

	/* ldrpath */
	if (dctx->cctx->host.ldrpath) {
		if (slbt_exec_link_remove_file(dctx,ectx,ectx->rpathfilename))
			return SLBT_NESTED_ERROR(dctx);

		if (slbt_create_symlink(
				dctx,ectx,
				dctx->cctx->host.ldrpath,
				ectx->rpathfilename,
				SLBT_SYMLINK_LITERAL))
			return SLBT_NESTED_ERROR(dctx);
	}

	/* cwd */
	if (slbt_realpath(fdcwd,".",O_DIRECTORY,cwd,sizeof(cwd)))
		return SLBT_SYSTEM_ERROR(dctx,0);

	/* .libs/libfoo.so --> -L.libs -lfoo */
	if (slbt_exec_link_adjust_argument_vector(
			dctx,ectx,&depsmeta,cwd,true))
		return SLBT_NESTED_ERROR(dctx);

	/* using alternate argument vector */
	ccwrap        = (char *)dctx->cctx->ccwrap;
	ectx->argv    = depsmeta.altv;
	ectx->program = ccwrap ? ccwrap : depsmeta.altv[0];

	/* sigh */
	if (slbt_exec_link_finalize_argument_vector(dctx,ectx))
		return SLBT_NESTED_ERROR(dctx);

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return slbt_exec_link_exit(
				&depsmeta,
				SLBT_NESTED_ERROR(dctx));

	/* spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SPAWN_ERROR(dctx));

	} else if (ectx->exitcode) {
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_LINK_ERROR));
	}

	return slbt_exec_link_exit(&depsmeta,0);
}

static int slbt_exec_link_create_executable(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	const char *			exefilename)
{
	int	fdcwd;
	int	fdwrap;
	char ** parg;
	char ** xarg;
	char *	base;
	char *	ccwrap;
	char	cwd    [PATH_MAX];
	char	dpfixup[PATH_MAX];
	char	output [PATH_MAX];
	char	wrapper[PATH_MAX];
	char	wraplnk[PATH_MAX];
	bool	fabspath;
	bool	fpic;
	const struct slbt_source_version * verinfo;
	struct slbt_deps_meta depsmeta = {0,0,0,0};
	struct stat st;

	/* initial state */
	slbt_reset_arguments(ectx);

	/* placeholders */
	slbt_reset_placeholders(ectx);

	/* fdcwd */
	fdcwd = slbt_driver_fdcwd(dctx);

	/* fpic */
	fpic = !(dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC);

	/* input argument adjustment */
	for (parg=ectx->cargv; *parg; parg++)
		slbt_adjust_object_argument(*parg,fpic,true,fdcwd);

	/* linker argument adjustment */
	for (parg=ectx->cargv, xarg=ectx->xargv; *parg; parg++, xarg++)
		if (slbt_adjust_linker_argument(
				dctx,
				*parg,xarg,true,
				dctx->cctx->settings.dsosuffix,
				dctx->cctx->settings.arsuffix,
				&depsmeta) < 0)
			return SLBT_NESTED_ERROR(dctx);

	/* --no-undefined */
	if (dctx->cctx->drvflags & SLBT_DRIVER_NO_UNDEFINED)
		*ectx->noundef = "-Wl,--no-undefined";

	/* executable wrapper: create */
	if (slbt_snprintf(wrapper,sizeof(wrapper),
				"%s.wrapper.tmp",
				dctx->cctx->output) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	if ((fdwrap = openat(fdcwd,wrapper,O_RDWR|O_CREAT|O_TRUNC,0644)) < 0)
		return SLBT_SYSTEM_ERROR(dctx,wrapper);

	slbt_exec_set_fdwrapper(ectx,fdwrap);

	/* executable wrapper: header */
	verinfo = slbt_source_version();

	/* cwd, DL_PATH fixup */
	if (slbt_realpath(fdcwd,".",O_DIRECTORY,cwd,sizeof(cwd)))
		return SLBT_SYSTEM_ERROR(dctx,0);

	slbt_emit_fdwrap_dl_path_fixup(
		cwd,dpfixup,sizeof(dpfixup),
		wrapper);

	if (slbt_dprintf(fdwrap,
			"#!/bin/sh\n"
			"# libtool compatible executable wrapper\n"
			"# Generated by %s (slibtool %d.%d.%d)\n"
			"# [commit reference: %s]\n\n"

			"if [ -z \"$%s\" ]; then\n"
			"\tDL_PATH=\n"
			"\tCOLON=\n"
			"\tLCOLON=\n"
			"else\n"
			"\tDL_PATH=\n"
			"\tCOLON=\n"
			"\tLCOLON=':'\n"
			"fi\n\n"
			"DL_PATH_FIXUP=\"%s\";\n\n",

			dctx->program,
			verinfo->major,verinfo->minor,verinfo->revision,
			verinfo->commit,
			dctx->cctx->settings.ldpathenv,
			dpfixup) < 0)
		return SLBT_SYSTEM_ERROR(dctx,0);

	/* output */
	if (slbt_snprintf(output,sizeof(output),
			"%s",exefilename) < 0)
		return SLBT_BUFFER_ERROR(dctx);

	*ectx->lout[0] = "-o";
	*ectx->lout[1] = output;

	/* static? */
	if (dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC)
		*ectx->dpic = "-static";

	/* .libs/libfoo.so --> -L.libs -lfoo */
	if (slbt_exec_link_adjust_argument_vector(
			dctx,ectx,&depsmeta,cwd,false))
		return SLBT_NESTED_ERROR(dctx);

	/* using alternate argument vector */
	ccwrap        = (char *)dctx->cctx->ccwrap;
	ectx->argv    = depsmeta.altv;
	ectx->program = ccwrap ? ccwrap : depsmeta.altv[0];

	/* executable wrapper symlink */
	if (slbt_snprintf(wraplnk,sizeof(wraplnk),
			"%s.exe.wrapper",
			exefilename) < 0)
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_BUFFER_ERROR(dctx));

	/* executable wrapper: base name */
	base = strrchr(wraplnk,'/');
	base++;

	/* executable wrapper: footer */
	fabspath = (exefilename[0] == '/');

	if (slbt_dprintf(fdwrap,
			"DL_PATH=\"${DL_PATH}${LCOLON}${%s}\"\n\n"
			"export %s=\"$DL_PATH\"\n\n"
			"if [ $(basename \"$0\") = \"%s\" ]; then\n"
			"\tprogram=\"$1\"; shift\n"
			"\texec \"$program\" \"$@\"\n"
			"fi\n\n"
			"exec %s/%s \"$@\"\n",
			dctx->cctx->settings.ldpathenv,
			dctx->cctx->settings.ldpathenv,
			base,
			fabspath ? "" : cwd,
			fabspath ? &exefilename[1] : exefilename) < 0)
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,0));

	/* sigh */
	if (slbt_exec_link_finalize_argument_vector(dctx,ectx))
		return SLBT_NESTED_ERROR(dctx);

	/* step output */
	if (!(dctx->cctx->drvflags & SLBT_DRIVER_SILENT))
		if (slbt_output_link(dctx,ectx))
			return slbt_exec_link_exit(
				&depsmeta,
				SLBT_NESTED_ERROR(dctx));

	/* spawn */
	if ((slbt_spawn(ectx,true) < 0) && (ectx->pid < 0)) {
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SPAWN_ERROR(dctx));

	} else if (ectx->exitcode) {
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_LINK_ERROR));
	}

	/* executable wrapper: finalize */
	slbt_exec_close_fdwrapper(ectx);

	if (slbt_create_symlink(
			dctx,ectx,
			dctx->cctx->output,wraplnk,
			SLBT_SYMLINK_WRAPPER))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_NESTED_ERROR(dctx));

	if (fstatat(fdcwd,wrapper,&st,0))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,wrapper));

	if (renameat(fdcwd,wrapper,fdcwd,dctx->cctx->output))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,dctx->cctx->output));

	if (fchmodat(fdcwd,dctx->cctx->output,0755,0))
		return slbt_exec_link_exit(
			&depsmeta,
			SLBT_SYSTEM_ERROR(dctx,dctx->cctx->output));

	return slbt_exec_link_exit(&depsmeta,0);
}

static int slbt_exec_link_create_library_symlink(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx,
	bool				fmajor)
{
	char	target[PATH_MAX];
	char	lnkname[PATH_MAX];

	if (ectx->relfilename && dctx->cctx->verinfo.verinfo) {
		strcpy(target,ectx->relfilename);
		sprintf(lnkname,"%s.dualver",ectx->dsofilename);

		if (slbt_create_symlink(
				dctx,ectx,
				target,lnkname,
				SLBT_SYMLINK_DEFAULT))
			return SLBT_NESTED_ERROR(dctx);
	} else if (ectx->relfilename) {
		strcpy(target,ectx->relfilename);
		sprintf(lnkname,"%s.release",ectx->dsofilename);

		if (slbt_create_symlink(
				dctx,ectx,
				target,lnkname,
				SLBT_SYMLINK_DEFAULT))
			return SLBT_NESTED_ERROR(dctx);
	} else {
		sprintf(target,"%s%s.%d.%d.%d%s",
			ectx->dsobasename,
			dctx->cctx->settings.osdsuffix,
			dctx->cctx->verinfo.major,
			dctx->cctx->verinfo.minor,
			dctx->cctx->verinfo.revision,
			dctx->cctx->settings.osdfussix);
	}


	if (fmajor && ectx->dsorellnkname) {
		sprintf(lnkname,"%s.%d",
			ectx->dsorellnkname,
			dctx->cctx->verinfo.major);

	} else if (fmajor) {
		sprintf(lnkname,"%s%s.%d%s",
			ectx->dsobasename,
			dctx->cctx->settings.osdsuffix,
			dctx->cctx->verinfo.major,
			dctx->cctx->settings.osdfussix);

	} else {
		strcpy(lnkname,ectx->dsofilename);
	}


	if (fmajor && (dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_PE))
		return slbt_copy_file(
			dctx,ectx,
			target,lnkname);
	else
		return slbt_create_symlink(
			dctx,ectx,
			target,lnkname,
			SLBT_SYMLINK_DEFAULT);
}

int slbt_exec_link(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_exec_ctx *		ectx)
{
	int			ret;
	const char *		output;
	char *			dot;
	struct slbt_exec_ctx *	actx;
	bool			fpic;
	bool			fstaticonly;
	char			soname[PATH_MAX];
	char			soxyz [PATH_MAX];
	char			solnk [PATH_MAX];
	char			arname[PATH_MAX];
	char			target[PATH_MAX];
	char			lnkname[PATH_MAX];

	/* dry run */
	if (dctx->cctx->drvflags & SLBT_DRIVER_DRY_RUN)
		return 0;

	/* context */
	if (ectx)
		actx = 0;
	else if ((ret = slbt_get_exec_ctx(dctx,&ectx)))
		return SLBT_NESTED_ERROR(dctx);
	else
		actx = ectx;

	/* libfoo.so.x.y.z */
	if (slbt_snprintf(soxyz,sizeof(soxyz),
				"%s%s%s%s%s.%d.%d.%d%s",
				ectx->sonameprefix,
				dctx->cctx->libname,
				dctx->cctx->release ? "-" : "",
				dctx->cctx->release ? dctx->cctx->release : "",
				dctx->cctx->settings.osdsuffix,
				dctx->cctx->verinfo.major,
				dctx->cctx->verinfo.minor,
				dctx->cctx->verinfo.revision,
				dctx->cctx->settings.osdfussix) < 0) {
		slbt_free_exec_ctx(actx);
		return SLBT_BUFFER_ERROR(dctx);
	}

	/* libfoo.so.x */
	sprintf(soname,"%s%s%s%s%s.%d%s",
		ectx->sonameprefix,
		dctx->cctx->libname,
		dctx->cctx->release ? "-" : "",
		dctx->cctx->release ? dctx->cctx->release : "",
		dctx->cctx->settings.osdsuffix,
		dctx->cctx->verinfo.major,
		dctx->cctx->settings.osdfussix);

	/* libfoo.so */
	sprintf(solnk,"%s%s%s",
		ectx->sonameprefix,
		dctx->cctx->libname,
		dctx->cctx->settings.dsosuffix);

	/* libfoo.a */
	sprintf(arname,"%s%s%s",
		dctx->cctx->settings.arprefix,
		dctx->cctx->libname,
		dctx->cctx->settings.arsuffix);

	/* output suffix */
	output = dctx->cctx->output;
	dot    = strrchr(output,'.');

	/* .libs directory */
	if (slbt_mkdir(dctx,ectx->ldirname)) {
		ret = SLBT_SYSTEM_ERROR(dctx,ectx->ldirname);
		slbt_free_exec_ctx(actx);
		return ret;
	}

	/* non-pic libfoo.a */
	if (dot && !strcmp(dot,".a"))
		if (slbt_exec_link_create_archive(dctx,ectx,output,false)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

	/* fpic, fstaticonly */
	if (dctx->cctx->drvflags & SLBT_DRIVER_ALL_STATIC) {
		fstaticonly = true;
		fpic        = false;
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_SHARED) {
		fstaticonly = true;
		fpic        = false;
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_DISABLE_STATIC) {
		fstaticonly = false;
		fpic        = true;
	} else if (dctx->cctx->drvflags & SLBT_DRIVER_SHARED) {
		fstaticonly = false;
		fpic        = true;
	} else {
		fstaticonly = false;
		fpic        = false;
	}

	/* libfoo.so.def.{flavor} */
	if (dctx->cctx->libname) {
		if (slbt_exec_link_create_host_tag(
				dctx,ectx,
				ectx->deffilename))
			return SLBT_NESTED_ERROR(dctx);
	}

	/* pic libfoo.a */
	if (dot && !strcmp(dot,".la"))
		if (slbt_exec_link_create_archive(
				dctx,ectx,
				ectx->arfilename,
				fpic)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

	/* static-only libfoo.la */
	if (fstaticonly && dot && !strcmp(dot,".la")) {
		const struct slbt_flavor_settings * dflavor;

		if (slbt_get_flavor_settings("default",&dflavor) < 0)
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LINK_FLOW);

		if (strcmp(dctx->cctx->settings.dsosuffix,dflavor->dsosuffix)) {
			strcpy(target,ectx->lafilename);
			sprintf(lnkname,"%s.shrext%s",
				ectx->lafilename,
				dctx->cctx->settings.dsosuffix);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

			strcpy(target,lnkname);
			sprintf(lnkname,"%s.shrext",ectx->lafilename);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);
		}

		if (slbt_create_symlink(
				dctx,ectx,
				"/dev/null",
				ectx->deffilename,
				SLBT_SYMLINK_LITERAL|SLBT_SYMLINK_DEVNULL))
			return SLBT_NESTED_ERROR(dctx);
	}

	/* -all-static library */
	if (fstaticonly && dctx->cctx->libname)
		if (slbt_create_symlink(
				dctx,ectx,
				"/dev/null",
				ectx->dsofilename,
				SLBT_SYMLINK_LITERAL))
			return SLBT_NESTED_ERROR(dctx);

	/* dynamic library via -module */
	if (dctx->cctx->rpath && !fstaticonly) {
		if (dctx->cctx->drvflags & SLBT_DRIVER_MODULE) {
			if (!dot || strcmp(dot,".la")) {
				if (slbt_exec_link_create_library(
						dctx,ectx,
						ectx->dsobasename,
						ectx->dsofilename,
						ectx->relfilename)) {
					slbt_free_exec_ctx(actx);
					return SLBT_NESTED_ERROR(dctx);
				}

				slbt_free_exec_ctx(actx);
				return 0;
			}
		}
	}

	/* dynamic library */
	if (dot && !strcmp(dot,".la") && dctx->cctx->rpath && !fstaticonly) {
		const struct slbt_flavor_settings * dflavor;

		if (slbt_get_flavor_settings("default",&dflavor) < 0)
			return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_LINK_FLOW);

		/* -shrext support */
		if (dctx->cctx->shrext) {
			strcpy(target,ectx->lafilename);
			sprintf(lnkname,"%s.shrext%s",ectx->lafilename,dctx->cctx->shrext);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

			strcpy(target,lnkname);
			sprintf(lnkname,"%s.shrext",ectx->lafilename);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

		/* non-default shared-object suffix support */
		} else if (strcmp(dctx->cctx->settings.dsosuffix,dflavor->dsosuffix)) {
			strcpy(target,ectx->lafilename);
			sprintf(lnkname,"%s.shrext%s",
				ectx->lafilename,
				dctx->cctx->settings.dsosuffix);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

			strcpy(target,lnkname);
			sprintf(lnkname,"%s.shrext",ectx->lafilename);

			if (slbt_create_symlink(
					dctx,ectx,
					target,lnkname,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);
		}

		/* linking: libfoo.so.x.y.z */
		if (slbt_exec_link_create_library(
				dctx,ectx,
				ectx->dsobasename,
				ectx->dsofilename,
				ectx->relfilename)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}

		if (!(dctx->cctx->drvflags & SLBT_DRIVER_AVOID_VERSION)) {
			/* symlink: libfoo.so.x --> libfoo.so.x.y.z */
			if (slbt_exec_link_create_library_symlink(
					dctx,ectx,
					true)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}

			/* symlink: libfoo.so --> libfoo.so.x.y.z */
			if (slbt_exec_link_create_library_symlink(
					dctx,ectx,
					false)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}
		} else if (ectx->relfilename) {
			/* symlink: libfoo.so --> libfoo-x.y.z.so */
			if (slbt_exec_link_create_library_symlink(
					dctx,ectx,
					false)) {
				slbt_free_exec_ctx(actx);
				return SLBT_NESTED_ERROR(dctx);
			}
		}

		/* PE import libraries */
		if (dctx->cctx->drvflags & SLBT_DRIVER_IMAGE_PE) {
			/* libfoo.x.lib.a */
			if (slbt_exec_link_create_import_library(
					dctx,ectx,
					ectx->pimpfilename,
					ectx->deffilename,
					soname))
				return SLBT_NESTED_ERROR(dctx);

			/* symlink: libfoo.lib.a --> libfoo.x.lib.a */
			if (slbt_create_symlink(
					dctx,ectx,
					ectx->pimpfilename,
					ectx->dimpfilename,
					SLBT_SYMLINK_DEFAULT))
				return SLBT_NESTED_ERROR(dctx);

			/* libfoo.x.y.z.lib.a */
			if (slbt_exec_link_create_import_library(
					dctx,ectx,
					ectx->vimpfilename,
					ectx->deffilename,
					soxyz))
				return SLBT_NESTED_ERROR(dctx);
		} else {
			if (slbt_create_symlink(
					dctx,ectx,
					"/dev/null",
					ectx->deffilename,
					SLBT_SYMLINK_LITERAL|SLBT_SYMLINK_DEVNULL))
				return SLBT_NESTED_ERROR(dctx);
		}
	}

	/* executable */
	if (!dctx->cctx->libname) {
		/* linking: .libs/exefilename */
		if (slbt_exec_link_create_executable(
				dctx,ectx,
				ectx->exefilename)) {
			slbt_free_exec_ctx(actx);
			return SLBT_NESTED_ERROR(dctx);
		}
	}

	/* no wrapper? */
	if (!dot || strcmp(dot,".la")) {
		slbt_free_exec_ctx(actx);
		return 0;
	}

	/* library wrapper */
	if (slbt_create_library_wrapper(
			dctx,ectx,
			arname,soname,soxyz,solnk)) {
		slbt_free_exec_ctx(actx);
		return SLBT_NESTED_ERROR(dctx);
	}

	/* wrapper symlink */
	if ((ret = slbt_create_symlink(
			dctx,ectx,
			output,
			ectx->lafilename,
			SLBT_SYMLINK_WRAPPER)))
		SLBT_NESTED_ERROR(dctx);

	/* .lai wrapper symlink */
	if (ret == 0)
		if ((ret = slbt_create_symlink(
				dctx,ectx,
				output,
				ectx->laifilename,
				SLBT_SYMLINK_WRAPPER)))
			SLBT_NESTED_ERROR(dctx);

	/* all done */
	slbt_free_exec_ctx(actx);

	return ret;
}
