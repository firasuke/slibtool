/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2021  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>

#define ARGV_DRIVER

#include <slibtool/slibtool.h>
#include "slibtool_version.h"
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"
#include "slibtool_lconf_impl.h"
#include "argv/argv.h"

extern char ** environ;

/* package info */
static const struct slbt_source_version slbt_src_version = {
	SLBT_TAG_VER_MAJOR,
	SLBT_TAG_VER_MINOR,
	SLBT_TAG_VER_PATCH,
	SLIBTOOL_GIT_VERSION
};

/* default fd context */
static const struct slbt_fd_ctx slbt_default_fdctx = {
	.fdin  = STDIN_FILENO,
	.fdout = STDOUT_FILENO,
	.fderr = STDERR_FILENO,
	.fdcwd = AT_FDCWD,
	.fddst = AT_FDCWD,
	.fdlog = (-1),
};

/* flavor settings */
#define SLBT_FLAVOR_SETTINGS(flavor,          \
		bfmt,pic,                     \
		arp,ars,dsop,dsos,osds,osdf,  \
		exep,exes,impp,imps,          \
		ldenv)                        \
	static const struct slbt_flavor_settings flavor = {  \
		bfmt,arp,ars,dsop,dsos,osds,osdf,           \
		exep,exes,impp,imps,                       \
		ldenv,pic}

SLBT_FLAVOR_SETTINGS(host_flavor_default,       \
	"elf","-fPIC",                          \
	"lib",".a","lib",".so",".so","",        \
	"","","","",                            \
	"LD_LIBRARY_PATH");

SLBT_FLAVOR_SETTINGS(host_flavor_midipix,       \
	"pe","-fPIC",                           \
	"lib",".a","lib",".so",".so","",        \
	"","","lib",".lib.a",                   \
	"LD_LIBRARY_PATH");

SLBT_FLAVOR_SETTINGS(host_flavor_mingw,         \
	"pe",0,                                 \
	"lib",".a","lib",".dll","",".dll",      \
	"",".exe","lib",".dll.a",               \
	"PATH");

SLBT_FLAVOR_SETTINGS(host_flavor_cygwin,        \
	"pe",0,                                 \
	"lib",".a","lib",".dll","",".dll",      \
	"",".exe","lib",".dll.a",               \
	"PATH");

SLBT_FLAVOR_SETTINGS(host_flavor_darwin,        \
	"macho","-fPIC",                        \
	"lib",".a","lib",".dylib","",".dylib",  \
	"","","","",                            \
	"DYLD_LIBRARY_PATH");


/* annotation strings */
static const char cfgexplicit[] = "command-line argument";
static const char cfghost[]     = "derived from <host>";
static const char cfgtarget[]   = "derived from <target>";
static const char cfgcompiler[] = "derived from <compiler>";
static const char cfgnmachine[] = "native (cached in ccenv/host.mk)";
static const char cfgxmachine[] = "foreign (derived from -dumpmachine)";
static const char cfgnative[]   = "native";


/* default compiler argv */
static char * slbt_default_cargv[] = {"cc",0};

/* elf rpath */
static const char*ldrpath_elf[] = {
	"/lib",
	"/lib/64",
	"/usr/lib",
	"/usr/lib64",
	"/usr/local/lib",
	"/usr/local/lib64",
	0};

static const char aclr_reset [] = "\x1b[0m";
static const char aclr_bold  [] = "\x1b[1m";
static const char aclr_red   [] = "\x1b[31m";
static const char aclr_green [] = "\x1b[32m";
static const char aclr_yellow[] = "\x1b[33m";
static const char aclr_blue  [] = "\x1b[34m";
static const char aclr_cyan  [] = "\x1b[36m";
static const char aclr_white [] = "\x1b[37m";

struct slbt_driver_ctx_alloc {
	struct argv_meta *		meta;
	struct slbt_driver_ctx_impl	ctx;
	uint64_t			guard;
};

static void slbt_output_raw_vector(int fderr, char ** argv, char ** envp, bool fcolor)
{
	char **		parg;
	char *		dot;
	const char *	color;

	(void)envp;

	if (fcolor)
		slbt_dprintf(fderr,"%s%s",aclr_bold,aclr_red);

	slbt_dprintf(fderr,"\n\n\n%s",argv[0]);

	for (parg=&argv[1]; *parg; parg++) {
		if (!fcolor)
			color = "";
		else if (*parg[0] == '-')
			color = aclr_blue;
		else if (!(dot = strrchr(*parg,'.')))
			color = aclr_green;
		else if (!(strcmp(dot,".lo")))
			color = aclr_cyan;
		else if (!(strcmp(dot,".la")))
			color = aclr_yellow;
		else
			color = aclr_white;

		slbt_dprintf(fderr," %s%s",color,*parg);
	}

	slbt_dprintf(fderr,"%s\n\n",fcolor ? aclr_reset : "");
}

static uint32_t slbt_argv_flags(uint32_t flags)
{
	uint32_t ret = 0;

	if (flags & SLBT_DRIVER_VERBOSITY_NONE)
		ret |= ARGV_VERBOSITY_NONE;

	if (flags & SLBT_DRIVER_VERBOSITY_ERRORS)
		ret |= ARGV_VERBOSITY_ERRORS;

	if (flags & SLBT_DRIVER_VERBOSITY_STATUS)
		ret |= ARGV_VERBOSITY_STATUS;

	return ret;
}

static int slbt_free_argv_buffer(struct slbt_split_vector * sargv)
{
	if (sargv->dargs)
		free(sargv->dargs);

	if (sargv->dargv)
		free(sargv->dargv);

	if (sargv->targv)
		free(sargv->targv);

	return -1;
}

static int slbt_driver_usage(
	int				fdout,
	const char *			program,
	const char *			arg,
	const struct argv_option **	optv,
	struct argv_meta *		meta,
	struct slbt_split_vector *	sargv,
	int				noclr)
{
	char header[512];

	snprintf(header,sizeof(header),
		"Usage: %s [options] <file>...\n" "Options:\n",
		program);

	switch (noclr) {
		case 0:
			argv_usage(fdout,header,optv,arg);
			break;

		default:
			argv_usage_plain(fdout,header,optv,arg);
			break;
	}

	argv_free(meta);
	slbt_free_argv_buffer(sargv);

	return SLBT_USAGE;
}

static struct slbt_driver_ctx_impl * slbt_driver_ctx_alloc(
	struct argv_meta *		meta,
	const struct slbt_fd_ctx *	fdctx,
	const struct slbt_common_ctx *	cctx,
	struct slbt_split_vector *	sargv,
	char **				envp)
{
	struct slbt_driver_ctx_alloc *	ictx;
	size_t				size;
	int				elements;

	size =  sizeof(struct slbt_driver_ctx_alloc);

	if (!(ictx = calloc(1,size))) {
		slbt_free_argv_buffer(sargv);
		return 0;
	}

	ictx->ctx.dargs = sargv->dargs;
	ictx->ctx.dargv = sargv->dargv;
	ictx->ctx.targv = sargv->targv;
	ictx->ctx.cargv = sargv->cargv;
	ictx->ctx.envp  = envp;

	memcpy(&ictx->ctx.fdctx,fdctx,sizeof(*fdctx));
	memcpy(&ictx->ctx.cctx,cctx,sizeof(*cctx));

	elements = sizeof(ictx->ctx.erribuf) / sizeof(*ictx->ctx.erribuf);

	ictx->ctx.errinfp  = &ictx->ctx.erriptr[0];
	ictx->ctx.erricap  = &ictx->ctx.erriptr[--elements];

	ictx->meta = meta;
	ictx->ctx.ctx.errv  = ictx->ctx.errinfp;
	return &ictx->ctx;
}

static int slbt_get_driver_ctx_fail(
	struct slbt_driver_ctx * dctx,
	struct argv_meta *       meta)
{
	if (dctx) {
		slbt_output_error_vector(dctx);
		slbt_free_driver_ctx(dctx);
	} else {
		argv_free(meta);
	}

	return -1;
}

static int slbt_split_argv(
	char **				argv,
	uint32_t			flags,
	struct slbt_split_vector *	sargv,
	int				fderr)
{
	int				i;
	int				argc;
	const char *			program;
	char *				compiler;
	char *				csysroot;
	char **				dargv;
	char **				targv;
	char **				cargv;
	char *				dst;
	bool				flast;
	bool				fcopy;
	size_t				size;
	const char *			base;
	struct argv_meta *		meta;
	struct argv_entry *		entry;
	struct argv_entry *		mode;
	struct argv_entry *		help;
	struct argv_entry *		version;
	struct argv_entry *		config;
	struct argv_entry *		finish;
	struct argv_entry *		features;
	struct argv_entry *		ccwrap;
	struct argv_entry *		dumpmachine;
	const struct argv_option **	popt;
	const struct argv_option **	optout;
	const struct argv_option *	optv[SLBT_OPTV_ELEMENTS];
	struct argv_ctx			ctx = {ARGV_VERBOSITY_NONE,
						ARGV_MODE_SCAN,
						0,0,0,0,0,0,0};

	program = argv_program_name(argv[0]);

	/* missing arguments? */
	argv_optv_init(slbt_default_options,optv);

	if (!argv[1] && (flags & SLBT_DRIVER_VERBOSITY_USAGE))
		return slbt_driver_usage(
			fderr,program,
			0,optv,0,sargv,
			!!getenv("NO_COLOR"));

	/* initial argv scan: ... --mode=xxx ... <compiler> ... */
	argv_scan(argv,optv,&ctx,0);

	/* invalid slibtool arguments? */
	if (ctx.erridx && !ctx.unitidx) {
		if (flags & SLBT_DRIVER_VERBOSITY_ERRORS)
			argv_get(
				argv,optv,
				slbt_argv_flags(flags),
				fderr);
		return -1;
	}

	/* obtain slibtool's own arguments */
	if (ctx.unitidx) {
		compiler = argv[ctx.unitidx];
		argv[ctx.unitidx] = 0;

		meta = argv_get(argv,optv,ARGV_VERBOSITY_NONE,fderr);
		argv[ctx.unitidx] = compiler;
	} else {
		meta = argv_get(argv,optv,ARGV_VERBOSITY_NONE,fderr);
	}

	/* missing all of --mode, --help, --version, --config, --dumpmachine, --features, and --finish? */
	mode = help = version = config = finish = features = ccwrap = dumpmachine = 0;

	for (entry=meta->entries; entry->fopt; entry++)
		if (entry->tag == TAG_MODE)
			mode = entry;
		else if (entry->tag == TAG_HELP)
			help = entry;
		else if (entry->tag == TAG_VERSION)
			version = entry;
		else if (entry->tag == TAG_CONFIG)
			config = entry;
		else if (entry->tag == TAG_FINISH)
			finish = entry;
		else if (entry->tag == TAG_FEATURES)
			features = entry;
		else if (entry->tag == TAG_CCWRAP)
			ccwrap = entry;
		else if (entry->tag == TAG_DUMPMACHINE)
			dumpmachine = entry;

	argv_free(meta);

	if (!mode && !help && !version && !config && !finish && !features && !dumpmachine) {
		slbt_dprintf(fderr,
			"%s: error: --mode must be specified.\n",
			program);
		return -1;
	}

	/* missing compiler? */
	if (!ctx.unitidx && !help && !version && !finish && !features && !dumpmachine) {
		if (flags & SLBT_DRIVER_VERBOSITY_ERRORS)
			slbt_dprintf(fderr,
				"%s: error: <compiler> is missing.\n",
				program);
		return -1;
	}

	/* clone and normalize the argv vector (-l, --library) */
	for (argc=0,size=0,dargv=argv; *dargv; argc++,dargv++)
		size += strlen(*dargv) + 1;

	if (!(sargv->dargv = calloc(argc+1,sizeof(char *))))
		return -1;

	else if (!(sargv->dargs = calloc(1,size+1)))
		return -1;

	csysroot = 0;

	for (i=0,flast=false,dargv=sargv->dargv,dst=sargv->dargs; i<argc; i++) {
		if ((fcopy = flast)) {
			(void)0;

		} else if (!strcmp(argv[i],"--")) {
			flast = true;
			fcopy = true;

		} else if (!strcmp(argv[i],"-l")) {
			*dargv++ = dst;
			*dst++ = '-';
			*dst++ = 'l';
			strcpy(dst,argv[++i]);
			dst += strlen(dst)+1;

		} else if (!strncmp(argv[i],"-l",2)) {
			fcopy = true;

		} else if (!strcmp(argv[i],"--library")) {
			*dargv++ = dst;
			*dst++ = '-';
			*dst++ = 'l';
			strcpy(dst,argv[++i]);
			dst += strlen(dst)+1;

		} else if (!strncmp(argv[i],"--library=",10)) {
			*dargv++ = dst;
			*dst++ = '-';
			*dst++ = 'l';
			strcpy(dst,&argv[++i][10]);
			dst += strlen(dst)+1;

		} else if (!strcmp(argv[i],"-L")) {
			*dargv++ = dst;
			*dst++ = '-';
			*dst++ = 'L';
			strcpy(dst,argv[++i]);
			dst += strlen(dst)+1;

		} else if (!strncmp(argv[i],"-L",2)) {
			fcopy = true;

		} else if (!strcmp(argv[i],"-Xlinker")) {
			*dargv++ = dst;
			*dst++ = '-';
			*dst++ = 'W';
			*dst++ = 'l';
			*dst++ = ',';
			strcpy(dst,argv[++i]);
			dst += strlen(dst)+1;

		} else if (!strcmp(argv[i],"--library-path")) {
			*dargv++ = dst;
			*dst++ = '-';
			*dst++ = 'L';
			strcpy(dst,argv[++i]);
			dst += strlen(dst)+1;

		} else if (!strncmp(argv[i],"--library-path=",15)) {
			*dargv++ = dst;
			*dst++ = '-';
			*dst++ = 'L';
			strcpy(dst,&argv[i][15]);
			dst += strlen(dst)+1;

		} else if (!strcmp(argv[i],"--sysroot") && (i<ctx.unitidx)) {
			*dargv++ = dst;
			csysroot = dst;
			strcpy(dst,argv[i]);
			dst[9] = '=';
			strcpy(&dst[10],argv[++i]);
			dst += strlen(dst)+1;
			ctx.unitidx--;

		} else if (!strncmp(argv[i],"--sysroot=",10) && (i<ctx.unitidx)) {
			*dargv++ = dst;
			csysroot = dst;
			strcpy(dst,argv[i]);
			dst += strlen(dst)+1;

		} else {
			fcopy = true;
		}

		if (fcopy) {
			*dargv++ = dst;
			strcpy(dst,argv[i]);
			dst += strlen(dst)+1;
		}
	}

	/* update argc,argv */
	argc = dargv - sargv->dargv;
	argv = sargv->dargv;

	/* allocate split vectors, account for cargv's added sysroot */
	if ((sargv->targv = calloc(2*(argc+3),sizeof(char *))))
		sargv->cargv = sargv->targv + argc + 2;
	else
		return -1;

	/* --features and no <compiler>? */
	if (ctx.unitidx) {
		(void)0;

	} else if (help || version || features || dumpmachine) {
		for (i=0; i<argc; i++)
			sargv->targv[i] = argv[i];

		sargv->cargv = slbt_default_cargv;

		return 0;
	}

	/* split vectors: slibtool's own options */
	for (i=0; i<ctx.unitidx; i++)
		sargv->targv[i] = argv[i];

	/* split vector marks */
	targv = sargv->targv + i;
	cargv = sargv->cargv;

	/* known wrappers */
	if (ctx.unitidx && !ccwrap) {
		if ((base = strrchr(argv[i],'/')))
			base++;
		else if ((base = strrchr(argv[i],'\\')))
			base++;
		else
			base = argv[i];

		if (!strcmp(base,"ccache")
				|| !strcmp(base,"distcc")
				|| !strcmp(base,"compiler")
				|| !strcmp(base,"purify")) {
			*targv++ = "--ccwrap";
			*targv++ = argv[i++];
		}
	}

	/* split vectors: legacy mixture */
	for (optout=optv; optout[0]->tag != TAG_OUTPUT; optout++)
		(void)0;

	/* compiler */
	*cargv++ = argv[i++];

	/* sysroot */
	if (csysroot)
		*cargv++ = csysroot;

	/* remaining vector */
	for (; i<argc; i++) {
		if (argv[i][0] != '-') {
			if (argv[i+1] && (argv[i+1][0] == '+')
					&& (argv[i+1][1] == '=')
					&& (argv[i+1][2] == 0)
					&& !(strrchr(argv[i],'.')))
				/* libfoo_la_LDFLAGS += -Wl,.... */
				i++;
			else
				*cargv++ = argv[i];

		} else if (argv[i][1] == 'o') {
			*targv++ = argv[i];

			if (argv[i][2] == 0)
				*targv++ = argv[++i];
		} else if ((argv[i][1] == 'W')  && (argv[i][2] == 'c')) {
			*cargv++ = argv[i];

		} else if (!(strcmp("Xcompiler",&argv[i][1]))) {
			*cargv++ = argv[++i];

		} else if (!(strcmp("XCClinker",&argv[i][1]))) {
			*cargv++ = argv[++i];

		} else if ((argv[i][1] == 'R')  && (argv[i][2] == 0)) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (argv[i][1] == 'R') {
			*targv++ = argv[i];

		} else if (!(strncmp("-target=",&argv[i][1],8))) {
			*cargv++ = argv[i];
			*targv++ = argv[i];

		} else if (!(strcmp("-target",&argv[i][1]))) {
			*cargv++ = argv[i];
			*targv++ = argv[i++];

			*cargv++ = argv[i];
			*targv++ = argv[i];

		} else if (!(strncmp("-sysroot=",&argv[i][1],9))) {
			*cargv++ = argv[i];
			*targv++ = argv[i];

		} else if (!(strcmp("-sysroot",&argv[i][1]))) {
			*cargv++ = argv[i];
			*targv++ = argv[i++];

			*cargv++ = argv[i];
			*targv++ = argv[i];

		} else if (!(strcmp("bindir",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("shrext",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("rpath",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("release",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("dlopen",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("static-libtool-libs",&argv[i][1]))) {
			*targv++ = argv[i];

		} else if (!(strcmp("export-dynamic",&argv[i][1]))) {
			*targv++ = argv[i];

		} else if (!(strcmp("export-symbols",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("export-symbols-regex",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("version-info",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("version-number",&argv[i][1]))) {
			*targv++ = argv[i++];
			*targv++ = argv[i];

		} else if (!(strcmp("dlpreopen",&argv[i][1]))) {
			(void)0;

		} else {
			for (popt=optout; popt[0] && popt[0]->long_name; popt++)
				if (!(strcmp(popt[0]->long_name,&argv[i][1])))
					break;

			if (popt[0] && popt[0]->long_name)
				*targv++ = argv[i];
			else
				*cargv++ = argv[i];
		}
	}

	return 0;
}

static void slbt_get_host_quad(
	char *	hostbuf,
	char ** hostquad)
{
	char *	mark;
	char *	ch;
	int	i;

	for (i=0, ch=hostbuf, mark=hostbuf; *ch && i<4; ch++) {
		if (*ch == '-') {
			*ch = 0;
			hostquad[i++] = mark;
			mark = &ch[1];
		}
	}

	if (i<4)
		hostquad[i] = mark;

	if (i==3) {
		hostquad[1] = hostquad[2];
		hostquad[2] = hostquad[3];
		hostquad[3] = 0;
	}
}

static void slbt_spawn_ar(char ** argv, int * ecode)
{
	int	estatus;
	pid_t	pid;

	*ecode = 127;

	if ((pid = fork()) < 0) {
		return;

	} else if (pid == 0) {
		execvp(argv[0],argv);
		_exit(errno);

	} else {
		waitpid(pid,&estatus,0);

		if (WIFEXITED(estatus))
			*ecode = WEXITSTATUS(estatus);
	}
}

static int slbt_init_host_params(
	const struct slbt_driver_ctx *	dctx,
	const struct slbt_common_ctx *	cctx,
	struct slbt_host_strs *		drvhost,
	struct slbt_host_params *	host,
	struct slbt_host_params *	cfgmeta)
{
	int		fdcwd;
	int		arprobe;
	int		arfd;
	int		ecode;
	size_t		toollen;
	char *		dash;
	char *		base;
	char *		mark;
	const char *	machine;
	bool		ftarget       = false;
	bool		fhost         = false;
	bool		fcompiler     = false;
	bool		fnative       = false;
	bool		fdumpmachine  = false;
	char		buf        [256];
	char		hostbuf    [256];
	char		machinebuf [256];
	char *		hostquad   [4];
	char *		machinequad[4];
	char *		arprobeargv[4];
	char		archivename[] = "/tmp/slibtool.ar.probe.XXXXXXXXXXXXXXXX";

	/* base */
	if ((base = strrchr(cctx->cargv[0],'/')))
		base++;
	else
		base = cctx->cargv[0];

	fdumpmachine  = (cctx->mode == SLBT_MODE_COMPILE)
			|| (cctx->mode == SLBT_MODE_LINK)
			|| (cctx->mode == SLBT_MODE_INFO);

	fdumpmachine &= (!strcmp(base,"xgcc")
			|| !strcmp(base,"xg++"));

	/* support the portbld <--> unknown synonym */
	if (!(drvhost->machine = strdup(SLBT_MACHINE)))
		return -1;

	if ((mark = strstr(drvhost->machine,"-portbld-")))
		memcpy(mark,"-unknown",8);

	/* host */
	if (host->host) {
		cfgmeta->host = cfgexplicit;
		fhost         = true;

	} else if (cctx->target) {
		host->host    = cctx->target;
		cfgmeta->host = cfgtarget;
		ftarget       = true;

	} else if (strrchr(base,'-')) {
		if (!(drvhost->host = strdup(cctx->cargv[0])))
			return -1;

		dash          = strrchr(drvhost->host,'-');
		*dash         = 0;
		host->host    = drvhost->host;
		cfgmeta->host = cfgcompiler;
		fcompiler     = true;

	} else if (!fdumpmachine) {
		host->host    = drvhost->machine;
		cfgmeta->host = cfgnmachine;

	} else if (slbt_dump_machine(cctx->cargv[0],buf,sizeof(buf)) < 0) {
		if (dctx)
			slbt_dprintf(
				slbt_driver_fderr(dctx),
				"%s: could not determine host "
				"via -dumpmachine\n",
				dctx->program);
		return -1;

	} else {
		if (!(drvhost->host = strdup(buf)))
			return -1;

		host->host    = drvhost->host;
		fcompiler     = true;
		fnative       = !strcmp(host->host,drvhost->machine);
		cfgmeta->host = fnative ? cfgnmachine : cfgxmachine;

		if (!fnative) {
			strcpy(hostbuf,host->host);
			strcpy(machinebuf,drvhost->machine);

			slbt_get_host_quad(hostbuf,hostquad);
			slbt_get_host_quad(machinebuf,machinequad);

			if (hostquad[2] && machinequad[2])
				fnative = !strcmp(hostquad[0],machinequad[0])
					&& !strcmp(hostquad[1],machinequad[1])
					&& !strcmp(hostquad[2],machinequad[2]);
		}
	}

	/* flavor */
	if (host->flavor) {
		cfgmeta->flavor = cfgexplicit;
	} else {
		if (fhost) {
			machine         = host->host;
			cfgmeta->flavor = cfghost;
		} else if (ftarget) {
			machine         = cctx->target;
			cfgmeta->flavor = cfgtarget;
		} else if (fcompiler) {
			machine         = drvhost->host;
			cfgmeta->flavor = cfgcompiler;
		} else {
			machine         = drvhost->machine;
			cfgmeta->flavor = cfgnmachine;
		}

		dash = strrchr(machine,'-');
		cfgmeta->flavor = cfghost;

		if ((dash && !strcmp(dash,"-bsd")) || strstr(machine,"-bsd-"))
			host->flavor = "bsd";
		else if ((dash && !strcmp(dash,"-cygwin")) || strstr(machine,"-cygwin-"))
			host->flavor = "cygwin";
		else if ((dash && !strcmp(dash,"-darwin")) || strstr(machine,"-darwin"))
			host->flavor = "darwin";
		else if ((dash && !strcmp(dash,"-linux")) || strstr(machine,"-linux-"))
			host->flavor = "linux";
		else if ((dash && !strcmp(dash,"-midipix")) || strstr(machine,"-midipix-"))
			host->flavor = "midipix";
		else if ((dash && !strcmp(dash,"-mingw")) || strstr(machine,"-mingw-"))
			host->flavor = "mingw";
		else if ((dash && !strcmp(dash,"-mingw32")) || strstr(machine,"-mingw32-"))
			host->flavor = "mingw";
		else if ((dash && !strcmp(dash,"-mingw64")) || strstr(machine,"-mingw64-"))
			host->flavor = "mingw";
		else if ((dash && !strcmp(dash,"-windows")) || strstr(machine,"-windows-"))
			host->flavor = "mingw";
		else {
			host->flavor   = "default";
			cfgmeta->flavor = "fallback, unverified";
		}

		if (fcompiler && !fnative)
			if ((mark = strstr(drvhost->machine,host->flavor)))
				if (mark > drvhost->machine)
					fnative = (*--mark == '-');
	}

	/* toollen */
	toollen =  fnative ? 0 : strlen(host->host);
	toollen += strlen("-utility-name");

	/* ar */
	if (host->ar)
		cfgmeta->ar = cfgexplicit;
	else {
		if (!(drvhost->ar = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->ar,"ar");
			cfgmeta->ar = cfgnative;
			arprobe = 0;
		} else if (cctx->mode == SLBT_MODE_LINK) {
			arprobe = true;
		} else if (cctx->mode == SLBT_MODE_INFO) {
			arprobe = true;
		} else {
			arprobe = false;
		}

		/* arprobe */
		if (arprobe) {
			sprintf(drvhost->ar,"%s-ar",host->host);
			cfgmeta->ar = cfghost;
			ecode       = 127;

			/* empty archive */
			if ((arfd = mkstemp(archivename)) >= 0) {
				slbt_dprintf(arfd,"!<arch>\n");

				arprobeargv[0] = drvhost->ar;
				arprobeargv[1] = "-t";
				arprobeargv[2] = archivename;
				arprobeargv[3] = 0;

				/* <target>-ar */
				slbt_spawn_ar(
					arprobeargv,
					&ecode);
			}

			/* <target>-<compiler>-ar */
			if (ecode && !strchr(base,'-')) {
				sprintf(drvhost->ar,"%s-%s-ar",host->host,base);

				slbt_spawn_ar(
					arprobeargv,
					&ecode);
			}

			/* <compiler>-ar */
			if (ecode && !strchr(base,'-')) {
				sprintf(drvhost->ar,"%s-ar",base);

				slbt_spawn_ar(
					arprobeargv,
					&ecode);
			}

			/* if target is the native target, fallback to native ar */
			if (ecode && !strcmp(host->host,SLBT_MACHINE)) {
				strcpy(drvhost->ar,"ar");
				cfgmeta->ar = cfgnative;
			}

			/* fdcwd */
			fdcwd = slbt_driver_fdcwd(dctx);

			/* clean up */
			if (arfd >= 0) {
				unlinkat(fdcwd,archivename,0);
				close(arfd);
			}
		}

		host->ar = drvhost->ar;
	}

	/* ranlib */
	if (host->ranlib)
		cfgmeta->ranlib = cfgexplicit;
	else {
		if (!(drvhost->ranlib = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->ranlib,"ranlib");
			cfgmeta->ranlib = cfgnative;
		} else {
			sprintf(drvhost->ranlib,"%s-ranlib",host->host);
			cfgmeta->ranlib = cfghost;
		}

		host->ranlib = drvhost->ranlib;
	}

	/* windres */
	if (host->windres)
		cfgmeta->windres = cfgexplicit;

	else if (strcmp(host->flavor,"cygwin")
			&& strcmp(host->flavor,"midipix")
			&& strcmp(host->flavor,"mingw")) {
		host->windres    = "";
		cfgmeta->windres = "not applicable";

	} else {
		if (!(drvhost->windres = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->windres,"windres");
			cfgmeta->windres = cfgnative;
		} else {
			sprintf(drvhost->windres,"%s-windres",host->host);
			cfgmeta->windres = cfghost;
		}

		host->windres = drvhost->windres;
	}

	/* dlltool */
	if (host->dlltool)
		cfgmeta->dlltool = cfgexplicit;

	else if (strcmp(host->flavor,"cygwin")
			&& strcmp(host->flavor,"midipix")
			&& strcmp(host->flavor,"mingw")) {
		host->dlltool = "";
		cfgmeta->dlltool = "not applicable";

	} else {
		if (!(drvhost->dlltool = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->dlltool,"dlltool");
			cfgmeta->dlltool = cfgnative;
		} else {
			sprintf(drvhost->dlltool,"%s-dlltool",host->host);
			cfgmeta->dlltool = cfghost;
		}

		host->dlltool = drvhost->dlltool;
	}

	/* mdso */
	if (host->mdso)
		cfgmeta->mdso = cfgexplicit;

	else if (strcmp(host->flavor,"cygwin")
			&& strcmp(host->flavor,"midipix")
			&& strcmp(host->flavor,"mingw")) {
		host->mdso = "";
		cfgmeta->mdso = "not applicable";

	} else {
		if (!(drvhost->mdso = calloc(1,toollen)))
			return -1;

		if (fnative) {
			strcpy(drvhost->mdso,"mdso");
			cfgmeta->mdso = cfgnative;
		} else {
			sprintf(drvhost->mdso,"%s-mdso",host->host);
			cfgmeta->mdso = cfghost;
		}

		host->mdso = drvhost->mdso;
	}

	return 0;
}

static void slbt_free_host_params(struct slbt_host_strs * host)
{
	if (host->machine)
		free(host->machine);

	if (host->host)
		free(host->host);

	if (host->flavor)
		free(host->flavor);

	if (host->ar)
		free(host->ar);

	if (host->ranlib)
		free(host->ranlib);

	if (host->windres)
		free(host->windres);

	if (host->dlltool)
		free(host->dlltool);

	if (host->mdso)
		free(host->mdso);

	memset(host,0,sizeof(*host));
}

static void slbt_init_flavor_settings(
	struct slbt_common_ctx *	cctx,
	const struct slbt_host_params * ahost,
	struct slbt_flavor_settings *	psettings)
{
	const struct slbt_host_params *     host;
	const struct slbt_flavor_settings * settings;

	host = ahost ? ahost : &cctx->host;

	if (!strcmp(host->flavor,"midipix"))
		settings = &host_flavor_midipix;
	else if (!strcmp(host->flavor,"mingw"))
		settings = &host_flavor_mingw;
	else if (!strcmp(host->flavor,"cygwin"))
		settings = &host_flavor_cygwin;
	else if (!strcmp(host->flavor,"darwin"))
		settings = &host_flavor_darwin;
	else
		settings = &host_flavor_default;

	if (!ahost) {
		if (!strcmp(settings->imagefmt,"elf"))
			cctx->drvflags |= SLBT_DRIVER_IMAGE_ELF;
		else if (!strcmp(settings->imagefmt,"pe"))
			cctx->drvflags |= SLBT_DRIVER_IMAGE_PE;
		else if (!strcmp(settings->imagefmt,"macho"))
			cctx->drvflags |= SLBT_DRIVER_IMAGE_MACHO;
	}

	memcpy(psettings,settings,sizeof(*settings));

	if (cctx->shrext)
		psettings->dsosuffix = cctx->shrext;
}

static int slbt_init_ldrpath(
	struct slbt_common_ctx *  cctx,
	struct slbt_host_params * host)
{
	char *         buf;
	const char **  ldrpath;

	if (!cctx->rpath || !(cctx->drvflags & SLBT_DRIVER_IMAGE_ELF)) {
		host->ldrpath = 0;
		return 0;
	}

	/* common? */
	for (ldrpath=ldrpath_elf; *ldrpath; ldrpath ++)
		if (!(strcmp(cctx->rpath,*ldrpath))) {
			host->ldrpath = 0;
			return 0;
		}

	/* buf */
	if (!(buf = malloc(12 + strlen(cctx->host.host))))
		return -1;

	/* /usr/{host}/lib */
	sprintf(buf,"/usr/%s/lib",cctx->host.host);

	if (!(strcmp(cctx->rpath,buf))) {
		host->ldrpath = 0;
		free(buf);
		return 0;
	}

	/* /usr/{host}/lib64 */
	sprintf(buf,"/usr/%s/lib64",cctx->host.host);

	if (!(strcmp(cctx->rpath,buf))) {
		host->ldrpath = 0;
		free(buf);
		return 0;
	}

	host->ldrpath = cctx->rpath;

	free(buf);
	return 0;
}

static int slbt_init_version_info(
	struct slbt_driver_ctx_impl *	ictx,
	struct slbt_version_info *	verinfo)
{
	int	current;
	int	revision;
	int	age;

	if (!verinfo->verinfo && !verinfo->vernumber)
		return 0;

	if (verinfo->vernumber) {
		sscanf(verinfo->vernumber,"%d:%d:%d",
			&verinfo->major,
			&verinfo->minor,
			&verinfo->revision);
		return 0;
	}

	current = revision = age = 0;

	sscanf(verinfo->verinfo,"%d:%d:%d",
		&current,&revision,&age);

	if (current < age) {
		if (ictx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
			slbt_dprintf(ictx->fdctx.fderr,
				"%s: error: invalid version info: "
				"<current> may not be smaller than <age>.\n",
				argv_program_name(ictx->cctx.targv[0]));
		return -1;
	}

	verinfo->major    = current - age;
	verinfo->minor    = age;
	verinfo->revision = revision;

	return 0;
}

static int slbt_init_link_params(struct slbt_driver_ctx_impl * ctx)
{
	const char * program;
	const char * libname;
	const char * prefix;
	const char * base;
	char *       dot;
	bool         fmodule;
	int          fderr;

	fderr   = ctx->fdctx.fderr;
	program = argv_program_name(ctx->cctx.targv[0]);
	libname = 0;
	prefix  = 0;
	fmodule = false;

	/* output */
	if (!(ctx->cctx.output)) {
		if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
			slbt_dprintf(fderr,
				"%s: error: output file must be "
				"specified in link mode.\n",
				program);
		return -1;
	}

	/* executable? */
	if (!(dot = strrchr(ctx->cctx.output,'.')))
		if (!(ctx->cctx.drvflags & SLBT_DRIVER_MODULE))
			return 0;

	/* todo: archive? library? wrapper? inlined function, avoid repetition */
	if ((base = strrchr(ctx->cctx.output,'/')))
		base++;
	else
		base = ctx->cctx.output;

	/* archive? */
	if (dot && !strcmp(dot,ctx->cctx.settings.arsuffix)) {
		prefix = ctx->cctx.settings.arprefix;

		if (!strncmp(prefix,base,strlen(prefix)))
			libname = base;
		else {
			if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
				slbt_dprintf(fderr,
					"%s: error: output file prefix does "
					"not match its (archive) suffix; "
					"the expected prefix was '%s'\n",
					program,prefix);
			return -1;
		}
	}

	/* library? */
	else if (dot && !strcmp(dot,ctx->cctx.settings.dsosuffix)) {
		prefix = ctx->cctx.settings.dsoprefix;

		if (!strncmp(prefix,base,strlen(prefix))) {
			libname = base;

		} else if (ctx->cctx.drvflags & SLBT_DRIVER_MODULE) {
			libname = base;
			fmodule = true;

		} else {
			if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
				slbt_dprintf(fderr,
					"%s: error: output file prefix does "
					"not match its (shared library) suffix; "
					"the expected prefix was '%s'\n",
					program,prefix);
			return -1;
		}
	}

	/* wrapper? */
	else if (dot && !strcmp(dot,".la")) {
		prefix = ctx->cctx.settings.dsoprefix;

		if (!strncmp(prefix,base,strlen(prefix))) {
			libname = base;
			fmodule = !!(ctx->cctx.drvflags & SLBT_DRIVER_MODULE);
		} else if (ctx->cctx.drvflags & SLBT_DRIVER_MODULE) {
			libname = base;
			fmodule = true;
		} else {
			if (ctx->cctx.drvflags & SLBT_DRIVER_VERBOSITY_ERRORS)
				slbt_dprintf(fderr,
					"%s: error: output file prefix does "
					"not match its (libtool wrapper) suffix; "
					"the expected prefix was '%s'\n",
					program,prefix);
			return -1;
		}
	} else
		return 0;

	/* libname alloc */
	if (!fmodule)
		libname += strlen(prefix);

	if (!(ctx->libname = strdup(libname)))
		return -1;

	if ((dot  = strrchr(ctx->libname,'.')))
		*dot = 0;

	ctx->cctx.libname = ctx->libname;

	return 0;
}

static int slbt_driver_fail_incompatible_args(
	int				fderr,
	uint64_t			drvflags,
	struct argv_meta *		meta,
	const char *			program,
	const char *			afirst,
	const char *			asecond)
{
	int fcolor;

	fcolor = (drvflags & SLBT_DRIVER_ANNOTATE_NEVER)
		? 0 : isatty(fderr);

	if (drvflags & SLBT_DRIVER_VERBOSITY_ERRORS){
		if (fcolor)
			slbt_dprintf(
				fderr,"%s%s",
				aclr_bold,aclr_red);

		slbt_dprintf(fderr,
			"%s: error: incompatible arguments: "
			"at the most one of %s and %s "
			"may be used.\n",
			program,afirst,asecond);

		if (fcolor)
			slbt_dprintf(
				fderr,"%s",
				aclr_reset);
	}

	return slbt_get_driver_ctx_fail(0,meta);
}

int slbt_get_driver_ctx(
	char **				argv,
	char **				envp,
	uint32_t			flags,
	const struct slbt_fd_ctx *	fdctx,
	struct slbt_driver_ctx **	pctx)
{
	struct slbt_split_vector	sargv;
	struct slbt_driver_ctx_impl *	ctx;
	struct slbt_common_ctx		cctx;
	const struct argv_option *	optv[SLBT_OPTV_ELEMENTS];
	struct argv_meta *		meta;
	struct argv_entry *		entry;
	struct argv_entry *		cmdstatic;
	struct argv_entry *		cmdshared;
	struct argv_entry *		cmdnostatic;
	struct argv_entry *		cmdnoshared;
	const char *			program;
	const char *			lconf;
	uint64_t			lflags;

	argv_optv_init(slbt_default_options,optv);

	if (!fdctx)
		fdctx = &slbt_default_fdctx;

	sargv.dargs = 0;
	sargv.dargv = 0;
	sargv.targv = 0;
	sargv.cargv = 0;

	if (slbt_split_argv(argv,flags,&sargv,fdctx->fderr))
		return slbt_free_argv_buffer(&sargv);

	if (!(meta = argv_get(
			sargv.targv,optv,
			slbt_argv_flags(flags),
			fdctx->fderr)))
		return slbt_free_argv_buffer(&sargv);

	lconf   = 0;
	program = argv_program_name(argv[0]);

	memset(&cctx,0,sizeof(cctx));

	/* shared and static objects: enable by default, disable by ~switch */
	cctx.drvflags = flags | SLBT_DRIVER_SHARED | SLBT_DRIVER_STATIC;

	/* full annotation when annotation is on; */
	if (!(cctx.drvflags & SLBT_DRIVER_ANNOTATE_NEVER))
		cctx.drvflags |= SLBT_DRIVER_ANNOTATE_FULL;

	/* track incompatible command-line arguments */
	cmdstatic   = 0;
	cmdshared   = 0;
	cmdnostatic = 0;
	cmdnoshared = 0;

	/* get options */
	for (entry=meta->entries; entry->fopt || entry->arg; entry++) {
		if (entry->fopt) {
			switch (entry->tag) {
				case TAG_HELP:
				case TAG_HELP_ALL:
					switch (cctx.mode) {
						case SLBT_MODE_INSTALL:
						case SLBT_MODE_UNINSTALL:
							break;

					default:
						return (flags & SLBT_DRIVER_VERBOSITY_USAGE)
							? slbt_driver_usage(
								fdctx->fdout,program,
								entry->arg,optv,
								meta,&sargv,
								(cctx.drvflags & SLBT_DRIVER_ANNOTATE_NEVER))
							: SLBT_USAGE;
					}

					break;

				case TAG_VERSION:
					cctx.drvflags |= SLBT_DRIVER_VERSION;
					break;

				case TAG_HEURISTICS:
					cctx.drvflags |= SLBT_DRIVER_HEURISTICS;
					lconf = entry->arg;
					break;

				case TAG_MODE:
					if (!strcmp("clean",entry->arg))
						cctx.mode = SLBT_MODE_CLEAN;

					else if (!strcmp("compile",entry->arg))
						cctx.mode = SLBT_MODE_COMPILE;

					else if (!strcmp("execute",entry->arg))
						cctx.mode = SLBT_MODE_EXECUTE;

					else if (!strcmp("finish",entry->arg))
						cctx.mode = SLBT_MODE_FINISH;

					else if (!strcmp("install",entry->arg))
						cctx.mode = SLBT_MODE_INSTALL;

					else if (!strcmp("link",entry->arg))
						cctx.mode = SLBT_MODE_LINK;

					else if (!strcmp("uninstall",entry->arg))
						cctx.mode = SLBT_MODE_UNINSTALL;
					break;

				case TAG_FINISH:
					cctx.mode = SLBT_MODE_FINISH;
					break;

				case TAG_DRY_RUN:
					cctx.drvflags |= SLBT_DRIVER_DRY_RUN;
					break;

				case TAG_TAG:
					if (!strcmp("CC",entry->arg))
						cctx.tag = SLBT_TAG_CC;

					else if (!strcmp("CXX",entry->arg))
						cctx.tag = SLBT_TAG_CXX;

					else if (!strcmp("FC",entry->arg))
						cctx.tag = SLBT_TAG_FC;

					else if (!strcmp("F77",entry->arg))
						cctx.tag = SLBT_TAG_F77;

					else if (!strcmp("NASM",entry->arg))
						cctx.tag = SLBT_TAG_NASM;

					else if (!strcmp("RC",entry->arg))
						cctx.tag = SLBT_TAG_RC;

					else if (!strcmp("disable-static",entry->arg))
						cmdnostatic = entry;

					else if (!strcmp("disable-shared",entry->arg))
						cmdnoshared = entry;
					break;

				case TAG_CONFIG:
					cctx.drvflags |= SLBT_DRIVER_CONFIG;
					break;

				case TAG_DUMPMACHINE:
					cctx.drvflags |= SLBT_DRIVER_OUTPUT_MACHINE;
					break;

				case TAG_DEBUG:
					cctx.drvflags |= SLBT_DRIVER_DEBUG;
					break;

				case TAG_FEATURES:
					cctx.drvflags |= SLBT_DRIVER_FEATURES;
					break;

				case TAG_LEGABITS:
					if (!entry->arg)
						cctx.drvflags |= SLBT_DRIVER_LEGABITS;

					else if (!strcmp("enabled",entry->arg))
						cctx.drvflags |= SLBT_DRIVER_LEGABITS;

					else
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_LEGABITS;

					break;

				case TAG_CCWRAP:
					cctx.ccwrap = entry->arg;
					break;

				case TAG_IMPLIB:
					if (!strcmp("idata",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_IMPLIB_IDATA;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_IMPLIB_DSOMETA;

					} else if (!strcmp("never",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_IMPLIB_DSOMETA;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_IMPLIB_IDATA;
					}

					break;

				case TAG_WARNINGS:
					if (!strcmp("all",entry->arg))
						cctx.warnings = SLBT_WARNING_LEVEL_ALL;

					else if (!strcmp("error",entry->arg))
						cctx.warnings = SLBT_WARNING_LEVEL_ERROR;

					else if (!strcmp("none",entry->arg))
						cctx.warnings = SLBT_WARNING_LEVEL_NONE;
					break;

				case TAG_ANNOTATE:
					if (!strcmp("always",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_ANNOTATE_ALWAYS;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_ANNOTATE_NEVER;

					} else if (!strcmp("never",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_ANNOTATE_NEVER;
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_ANNOTATE_ALWAYS;

					} else if (!strcmp("minimal",entry->arg)) {
						cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_ANNOTATE_FULL;

					} else if (!strcmp("full",entry->arg)) {
						cctx.drvflags |= SLBT_DRIVER_ANNOTATE_FULL;
					}

					break;

				case TAG_DEPS:
					cctx.drvflags |= SLBT_DRIVER_DEPS;
					break;

				case TAG_SILENT:
					cctx.drvflags |= SLBT_DRIVER_SILENT;
					break;

				case TAG_VERBOSE:
					cctx.drvflags |= SLBT_DRIVER_VERBOSE;
					break;

				case TAG_HOST:
					cctx.host.host = entry->arg;
					break;

				case TAG_FLAVOR:
					cctx.host.flavor = entry->arg;
					break;

				case TAG_AR:
					cctx.host.ar = entry->arg;
					break;

				case TAG_RANLIB:
					cctx.host.ranlib = entry->arg;
					break;

				case TAG_WINDRES:
					cctx.host.windres = entry->arg;
					break;

				case TAG_DLLTOOL:
					cctx.host.dlltool = entry->arg;
					break;

				case TAG_MDSO:
					cctx.host.mdso = entry->arg;
					break;

				case TAG_OUTPUT:
					cctx.output = entry->arg;
					break;

				case TAG_SHREXT:
					cctx.shrext = entry->arg;
					break;

				case TAG_RPATH:
					cctx.rpath = entry->arg;
					break;

				case TAG_SYSROOT:
					cctx.sysroot = entry->arg;
					break;

				case TAG_RELEASE:
					cctx.release = entry->arg;
					break;

				case TAG_DLOPEN:
					break;

				case TAG_STATIC_LIBTOOL_LIBS:
					cctx.drvflags |= SLBT_DRIVER_STATIC_LIBTOOL_LIBS;
					break;

				case TAG_EXPORT_DYNAMIC:
					cctx.drvflags |= SLBT_DRIVER_EXPORT_DYNAMIC;
					break;

				case TAG_EXPSYM_FILE:
					cctx.symfile = entry->arg;
					break;

				case TAG_EXPSYM_REGEX:
					cctx.regex = entry->arg;
					break;

				case TAG_VERSION_INFO:
					cctx.verinfo.verinfo = entry->arg;
					break;

				case TAG_VERSION_NUMBER:
					cctx.verinfo.vernumber = entry->arg;
					break;

				case TAG_TARGET:
					cctx.target = entry->arg;
					break;

				case TAG_PREFER_PIC:
					cctx.drvflags |= SLBT_DRIVER_PRO_PIC;
					break;

				case TAG_PREFER_NON_PIC:
					cctx.drvflags |= SLBT_DRIVER_ANTI_PIC;
					break;

				case TAG_NO_UNDEFINED:
					cctx.drvflags |= SLBT_DRIVER_NO_UNDEFINED;
					break;

				case TAG_MODULE:
					cctx.drvflags |= SLBT_DRIVER_MODULE;
					break;

				case TAG_ALL_STATIC:
					cctx.drvflags |= SLBT_DRIVER_ALL_STATIC;
					break;

				case TAG_DISABLE_STATIC:
					cmdnostatic = entry;
					break;

				case TAG_DISABLE_SHARED:
					cmdnoshared = entry;
					break;

				case TAG_AVOID_VERSION:
					cctx.drvflags |= SLBT_DRIVER_AVOID_VERSION;
					break;

				case TAG_SHARED:
					cmdshared = entry;
					break;

				case TAG_STATIC:
					cmdstatic = entry;
					break;
			}
		}
	}

	/* incompatible command-line arguments? */
	if (cmdstatic && cmdshared)
		return slbt_driver_fail_incompatible_args(
			fdctx->fderr,
			cctx.drvflags,
			meta,program,
			"-static",
			"-shared");

	if (cmdstatic && cmdnostatic)
		return slbt_driver_fail_incompatible_args(
			fdctx->fderr,
			cctx.drvflags,
			meta,program,
			"-static",
			"--disable-static");

	if (cmdshared && cmdnoshared)
		return slbt_driver_fail_incompatible_args(
			fdctx->fderr,
			cctx.drvflags,
			meta,program,
			"-shared",
			"--disable-shared");

	if (cmdnostatic && cmdnoshared)
		return slbt_driver_fail_incompatible_args(
			fdctx->fderr,
			cctx.drvflags,
			meta,program,
			"--disable-static",
			"--disable-shared");

	/* -static? */
	if (cmdstatic) {
		cctx.drvflags |= SLBT_DRIVER_STATIC;
		cctx.drvflags |= SLBT_DRIVER_DISABLE_SHARED;
		cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_STATIC;
	}

	/* shared? */
	if (cmdshared) {
		cctx.drvflags |= SLBT_DRIVER_SHARED;
		cctx.drvflags |= SLBT_DRIVER_DISABLE_STATIC;
		cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_SHARED;
	}

	/* -disable-static? */
	if (cmdnostatic) {
		cctx.drvflags |= SLBT_DRIVER_DISABLE_STATIC;
		cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_STATIC;
	}

	/* -disable-shared? */
	if (cmdnoshared) {
		cctx.drvflags |= SLBT_DRIVER_DISABLE_SHARED;
		cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_SHARED;
	}

	/* debug: raw argument vector */
	if (cctx.drvflags & SLBT_DRIVER_DEBUG)
		slbt_output_raw_vector(
			fdctx->fderr,argv,envp,
			(cctx.drvflags & SLBT_DRIVER_ANNOTATE_NEVER)
				? 0 : isatty(fdctx->fderr));

	/* -o in install mode means USER */
	if ((cctx.mode == SLBT_MODE_INSTALL) && cctx.output) {
		cctx.user   = cctx.output;
		cctx.output = 0;
	}

	/* info mode */
	if (cctx.drvflags & (SLBT_DRIVER_CONFIG | SLBT_DRIVER_FEATURES))
		cctx.mode = SLBT_MODE_INFO;

	/* --tag */
	if (cctx.mode == SLBT_MODE_COMPILE)
		if (cctx.tag == SLBT_TAG_UNKNOWN)
			cctx.tag = SLBT_TAG_CC;

	/* driver context */
	if (!(ctx = slbt_driver_ctx_alloc(meta,fdctx,&cctx,&sargv,envp)))
		return slbt_get_driver_ctx_fail(0,meta);

	/* ctx */
	ctx->ctx.program	= program;
	ctx->ctx.cctx		= &ctx->cctx;

	ctx->cctx.targv		= sargv.targv;
	ctx->cctx.cargv		= sargv.cargv;

	/* heuristics */
	if (cctx.drvflags & SLBT_DRIVER_HEURISTICS) {
		if (slbt_get_lconf_flags(&ctx->ctx,lconf,&lflags) < 0)
			return slbt_get_driver_ctx_fail(&ctx->ctx,0);

		if (cmdnoshared)
			lflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_STATIC;

		if (cmdnostatic)
			if (lflags & SLBT_DRIVER_DISABLE_SHARED)
				cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_STATIC;

		cctx.drvflags |= lflags;
		cctx.drvflags |= SLBT_DRIVER_SHARED;
		cctx.drvflags |= SLBT_DRIVER_STATIC;

		if (cmdstatic) {
			cctx.drvflags |= SLBT_DRIVER_DISABLE_SHARED;
			cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_STATIC;
		}

		if (cmdshared) {
			cctx.drvflags |= SLBT_DRIVER_DISABLE_STATIC;
			cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_DISABLE_SHARED;
		}

		/* -disable-static? */
		if (cctx.drvflags & SLBT_DRIVER_DISABLE_STATIC)
			cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_STATIC;

		/* -disable-shared? */
		if (cctx.drvflags & SLBT_DRIVER_DISABLE_SHARED)
			cctx.drvflags &= ~(uint64_t)SLBT_DRIVER_SHARED;

		ctx->cctx.drvflags = cctx.drvflags;
	}

	/* host params */
	if (slbt_init_host_params(
			&ctx->ctx,
			&ctx->cctx,
			&ctx->host,
			&ctx->cctx.host,
			&ctx->cctx.cfgmeta))
		return slbt_get_driver_ctx_fail(&ctx->ctx,0);

	/* flavor settings */
	slbt_init_flavor_settings(
		&ctx->cctx,0,
		&ctx->cctx.settings);

	/* ldpath */
	if (slbt_init_ldrpath(&ctx->cctx,&ctx->cctx.host))
		return slbt_get_driver_ctx_fail(&ctx->ctx,0);

	/* version info */
	if (slbt_init_version_info(ctx,&ctx->cctx.verinfo))
		return slbt_get_driver_ctx_fail(&ctx->ctx,0);

	/* link params */
	if (cctx.mode == SLBT_MODE_LINK)
		if (slbt_init_link_params(ctx))
			return slbt_get_driver_ctx_fail(&ctx->ctx,0);

	*pctx = &ctx->ctx;

	return 0;
}

static void slbt_free_driver_ctx_impl(struct slbt_driver_ctx_alloc * ictx)
{
	struct slbt_error_info ** perr;
	struct slbt_error_info *  erri;

	for (perr=ictx->ctx.errinfp; *perr; perr++) {
		erri = *perr;

		if (erri->eany && (erri->esyscode == ENOENT))
			free(erri->eany);
	}


	if (ictx->ctx.libname)
		free(ictx->ctx.libname);

	free(ictx->ctx.dargs);
	free(ictx->ctx.dargv);
	free(ictx->ctx.targv);

	slbt_free_host_params(&ictx->ctx.host);
	slbt_free_host_params(&ictx->ctx.ahost);
	argv_free(ictx->meta);
	free(ictx);
}

void slbt_free_driver_ctx(struct slbt_driver_ctx * ctx)
{
	struct slbt_driver_ctx_alloc *	ictx;
	uintptr_t			addr;

	if (ctx) {
		addr = (uintptr_t)ctx - offsetof(struct slbt_driver_ctx_impl,ctx);
		addr = addr - offsetof(struct slbt_driver_ctx_alloc,ctx);
		ictx = (struct slbt_driver_ctx_alloc *)addr;
		slbt_free_driver_ctx_impl(ictx);
	}
}

void slbt_reset_alternate_host(const struct slbt_driver_ctx * ctx)
{
	struct slbt_driver_ctx_alloc *	ictx;
	uintptr_t			addr;

	addr = (uintptr_t)ctx - offsetof(struct slbt_driver_ctx_alloc,ctx);
	addr = addr - offsetof(struct slbt_driver_ctx_impl,ctx);
	ictx = (struct slbt_driver_ctx_alloc *)addr;

	slbt_free_host_params(&ictx->ctx.ahost);
}

int  slbt_set_alternate_host(
	const struct slbt_driver_ctx *	ctx,
	const char *			host,
	const char *			flavor)
{
	struct slbt_driver_ctx_alloc *	ictx;
	uintptr_t			addr;

	addr = (uintptr_t)ctx - offsetof(struct slbt_driver_ctx_alloc,ctx);
	addr = addr - offsetof(struct slbt_driver_ctx_impl,ctx);
	ictx = (struct slbt_driver_ctx_alloc *)addr;
	slbt_free_host_params(&ictx->ctx.ahost);

	if (!(ictx->ctx.ahost.host = strdup(host)))
		return SLBT_SYSTEM_ERROR(ctx,0);

	if (!(ictx->ctx.ahost.flavor = strdup(flavor))) {
		slbt_free_host_params(&ictx->ctx.ahost);
		return SLBT_SYSTEM_ERROR(ctx,0);
	}

	ictx->ctx.cctx.ahost.host   = ictx->ctx.ahost.host;
	ictx->ctx.cctx.ahost.flavor = ictx->ctx.ahost.flavor;

	if (slbt_init_host_params(
			0,
			ctx->cctx,
			&ictx->ctx.ahost,
			&ictx->ctx.cctx.ahost,
			&ictx->ctx.cctx.acfgmeta)) {
		slbt_free_host_params(&ictx->ctx.ahost);
		return SLBT_CUSTOM_ERROR(ctx,SLBT_ERR_HOST_INIT);
	}

	slbt_init_flavor_settings(
		&ictx->ctx.cctx,
		&ictx->ctx.cctx.ahost,
		&ictx->ctx.cctx.asettings);

	if (slbt_init_ldrpath(
			&ictx->ctx.cctx,
			&ictx->ctx.cctx.ahost)) {
		slbt_free_host_params(&ictx->ctx.ahost);
		return SLBT_CUSTOM_ERROR(ctx,SLBT_ERR_LDRPATH_INIT);
	}

	return 0;
}

int slbt_get_flavor_settings(
	const char *                            flavor,
	const struct slbt_flavor_settings **    settings)
{
	if (!strcmp(flavor,"midipix"))
		*settings = &host_flavor_midipix;
	else if (!strcmp(flavor,"mingw"))
		*settings = &host_flavor_mingw;
	else if (!strcmp(flavor,"cygwin"))
		*settings = &host_flavor_cygwin;
	else if (!strcmp(flavor,"darwin"))
		*settings = &host_flavor_darwin;
	else if (!strcmp(flavor,"default"))
		*settings = &host_flavor_default;
	else
		*settings = 0;

	return *settings ? 0 : -1;
}

const struct slbt_source_version * slbt_source_version(void)
{
	return &slbt_src_version;
}

int slbt_get_driver_fdctx(
	const struct slbt_driver_ctx *	dctx,
	struct slbt_fd_ctx *		fdctx)
{
	struct slbt_driver_ctx_impl *	ictx;

	ictx = slbt_get_driver_ictx(dctx);

	fdctx->fdin  = ictx->fdctx.fdin;
	fdctx->fdout = ictx->fdctx.fdout;
	fdctx->fderr = ictx->fdctx.fderr;
	fdctx->fdlog = ictx->fdctx.fdlog;
	fdctx->fdcwd = ictx->fdctx.fdcwd;
	fdctx->fddst = ictx->fdctx.fddst;

	return 0;
}

int slbt_set_driver_fdctx(
	struct slbt_driver_ctx *	dctx,
	const struct slbt_fd_ctx *	fdctx)
{
	struct slbt_driver_ctx_impl *	ictx;

	ictx = slbt_get_driver_ictx(dctx);

	ictx->fdctx.fdin  = fdctx->fdin;
	ictx->fdctx.fdout = fdctx->fdout;
	ictx->fdctx.fderr = fdctx->fderr;
	ictx->fdctx.fdlog = fdctx->fdlog;
	ictx->fdctx.fdcwd = fdctx->fdcwd;
	ictx->fdctx.fddst = fdctx->fddst;

	return 0;
}
