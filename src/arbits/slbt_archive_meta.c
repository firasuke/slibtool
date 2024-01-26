/*******************************************************************/
/*  slibtool: a skinny libtool implementation, written in C        */
/*  Copyright (C) 2016--2024  SysDeer Technologies, LLC            */
/*  Released under the Standard MIT License; see COPYING.SLIBTOOL. */
/*******************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <slibtool/slibtool.h>
#include <slibtool/slibtool_arbits.h>
#include "slibtool_ar_impl.h"
#include "slibtool_driver_impl.h"
#include "slibtool_errinfo_impl.h"

/* decimal values in archive header are right padded with ascii spaces */
#define AR_DEC_PADDING (0x20)

/* archive file members are right padded as needed with ascii newline */
#define AR_OBJ_PADDING (0x0A)

/* initial number of elements in the transient, on-stack vector */
# define AR_STACK_VECTOR_ELEMENTS   (0x200)

/* transient header info vector */
struct ar_header_info {
	struct ar_raw_file_header * phdr;
	uint32_t                    attr;
};

static const char ar_signature[] = AR_SIGNATURE;

static int slbt_free_archive_meta_impl(struct slbt_archive_meta_impl * meta, int ret)
{
	if (meta) {
		if (meta->hdrinfov)
			free(meta->hdrinfov);

		if (meta->namestrs)
			free(meta->namestrs);

		if (meta->memberv)
			free(meta->memberv);

		if (meta->offsetv)
			free(meta->offsetv);

		if (meta->members)
			free(meta->members);

		if (meta->symstrv)
			free(meta->symstrv);

		free(meta);
	}

	return ret;
}


static int slbt_ar_read_octal(const char * mark, int len, uint32_t * dec)
{
	int       i;
	uint64_t  res;

	for (; len && (mark[len-1]==AR_DEC_PADDING); )
		len--;

	for (i=0,res=0; i<len; i++) {
		if ((mark[i] >= '0') && (mark[i] <= '7')) {
			res *= 8;
			res += (mark[i] - '0');
		} else {
			return -1;
		}
	}

	*dec = res;

	return 0;
}

static int slbt_ar_read_decimal_64(const char * mark, int len, uint64_t * dec)
{
	int       i;
	uint64_t  res;

	for (; len && (mark[len-1]==AR_DEC_PADDING); )
		len--;

	for (i=0,res=0; i<len; i++) {
		if ((mark[i] >= '0') && (mark[i] <= '9')) {
			res *= 10;
			res += (mark[i] - '0');
		} else {
			return -1;
		}
	}

	*dec = res;

	return 0;
}

static int slbt_ar_read_decimal_32(const char * mark, int len, uint32_t * dec)
{
	uint64_t res;

	if (slbt_ar_read_decimal_64(mark,len,&res) < 0)
		return -1;

	*dec = res;

	return 0;
}

static uint32_t slbt_ar_get_member_attr(struct ar_meta_member_info * m)
{
	const char *            hdrname;
	uint32_t                hdrattr;
	const char *            data;
	const char *            data_cap;
	const unsigned char *   udata;
	unsigned char           uch;
	const size_t            siglen = sizeof(struct ar_raw_signature);

	hdrname  = m->ar_file_header.ar_member_name;
	hdrattr  = m->ar_file_header.ar_header_attr;

	data     = m->ar_object_data;
	data_cap = &data[m->ar_file_header.ar_file_size];

	if (hdrattr & AR_HEADER_ATTR_SYSV) {
		/* long names member? */
		if ((hdrname[0] == '/') && (hdrname[1] == '/'))
			return AR_MEMBER_ATTR_NAMESTRS;

		/* mips 64-bit armap member? */
		else if (!strncmp(hdrname,"/SYM64/",7))
			return AR_MEMBER_ATTR_ARMAP;

		/* armap member? */
		else if (hdrname[0] == '/' && (hdrname[1] == '\0'))
			return AR_MEMBER_ATTR_ARMAP;

		/* nested archive? */
		else if (m->ar_file_header.ar_file_size >= siglen)
			if (!strncmp(data,ar_signature,siglen))
				return AR_MEMBER_ATTR_ARCHIVE;

	} else if (hdrattr & AR_HEADER_ATTR_BSD) {
		if (!strcmp(hdrname,"__.SYMDEF"))
			return AR_MEMBER_ATTR_ARMAP;

		else if (!strcmp(hdrname,"__.SYMDEF SORTED"))
			return AR_MEMBER_ATTR_ARMAP;

		else if (!strcmp(hdrname,"__.SYMDEF_64"))
			return AR_MEMBER_ATTR_ARMAP;

		else if (!strcmp(hdrname,"__.SYMDEF_64 SORTED"))
			return AR_MEMBER_ATTR_ARMAP;
	}

	/* ascii only data? */
	for (; data<data_cap; ) {
		if ((uch = *data) >= 0x80)
			break;

		data++;
	}

	if (data == data_cap)
		return AR_MEMBER_ATTR_ASCII;

	data  = m->ar_object_data;
	udata = (unsigned char *)data;

	/* elf object? [quick and dirty] */
	if (m->ar_file_header.ar_file_size >= 5)
		if ((udata[0] == 0x7f)
				&& (udata[1] == 'E')
				&& (udata[2] == 'L')
				&& (udata[3] == 'F'))
			if ((m->ar_object_attr = AR_OBJECT_ATTR_ELF))
				return AR_MEMBER_ATTR_OBJECT;

	/* coff i386 object? [quick and dirty] */
	if (m->ar_file_header.ar_file_size >= 2)
		if ((udata[0] == 0x4c) && (udata[1] == 0x01))
			if ((m->ar_object_attr = AR_OBJECT_ATTR_COFF))
				return AR_MEMBER_ATTR_OBJECT;

	/* coff x86_64 object? [quick and dirty] */
	if (m->ar_file_header.ar_file_size >= 2)
		if ((udata[0] == 0x64) && (udata[1] == 0x86))
			if ((m->ar_object_attr = AR_OBJECT_ATTR_COFF))
				return AR_MEMBER_ATTR_OBJECT;

	/* big endian 32-bit macho object? [quick and dirty] */
	if (m->ar_file_header.ar_file_size >= 4)
		if ((udata[0] == 0xfe) && (udata[1] == 0xed))
			if ((udata[2] == 0xfa) && (udata[3] == 0xce))
				if ((m->ar_object_attr = AR_OBJECT_ATTR_MACHO))
					return AR_MEMBER_ATTR_OBJECT;

	/* big endian 64-bit macho object? [quick and dirty] */
	if (m->ar_file_header.ar_file_size >= 4)
		if ((udata[0] == 0xfe) && (udata[1] == 0xed))
			if ((udata[2] == 0xfa) && (udata[3] == 0xcf))
				if ((m->ar_object_attr = AR_OBJECT_ATTR_MACHO))
					return AR_MEMBER_ATTR_OBJECT;

	/* little endian 32-bit macho object? [quick and dirty] */
	if (m->ar_file_header.ar_file_size >= 4)
		if ((udata[3] == 0xfe) && (udata[2] == 0xed))
			if ((udata[1] == 0xfa) && (udata[0] == 0xce))
				if ((m->ar_object_attr = AR_OBJECT_ATTR_MACHO))
					return AR_MEMBER_ATTR_OBJECT;

	/* little endian 64-bit macho object? [quick and dirty] */
	if (m->ar_file_header.ar_file_size >= 4)
		if ((udata[3] == 0xfe) && (udata[2] == 0xed))
			if ((udata[1] == 0xfa) && (udata[0] == 0xcf))
				if ((m->ar_object_attr = AR_OBJECT_ATTR_MACHO))
					return AR_MEMBER_ATTR_OBJECT;

	/* all other */
	return AR_MEMBER_ATTR_DEFAULT;
}

static int slbt_ar_parse_primary_armap_bsd_32(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * m)

{
	struct ar_raw_armap_bsd_32 *    armap;
	struct ar_meta_member_info *    memberp;
	struct ar_meta_armap_common_32 *armapref;
	uint32_t                        attr;
	uint32_t                        nsyms;
	uint32_t                        nstrs;
	uint32_t                        sizeofrefs_le;
	uint32_t                        sizeofrefs_be;
	uint32_t                        sizeofrefs;
	uint32_t                        sizeofstrs;
	const char *                    ch;
	const char *                    cap;
	unsigned char *                 uch;
	unsigned char                   (*mark)[0x04];

	armap   = &m->armaps.armap_bsd_32;
	memberp = m->memberv[0];

	mark = memberp->ar_object_data;

	armap->ar_size_of_refs = mark;
	uch = *mark++;

	armap->ar_first_name_offset = mark;

	sizeofrefs_le = (uch[3] << 24) + (uch[2] << 16) + (uch[1] << 8) + uch[0];
	sizeofrefs_be = (uch[0] << 24) + (uch[1] << 16) + (uch[2] << 8) + uch[3];

	if (sizeofrefs_le < memberp->ar_object_size - sizeof(*mark)) {
		sizeofrefs = sizeofrefs_le;
		attr       = AR_ARMAP_ATTR_LE_32;

	} else if (sizeofrefs_be < memberp->ar_object_size - sizeof(*mark)) {
		sizeofrefs = sizeofrefs_be;
		attr       = AR_ARMAP_ATTR_BE_32;
	} else {
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_SIZE_OF_REFS);
	}

	nsyms  = sizeofrefs / sizeof(struct ar_raw_armap_ref_32);
	mark  += (sizeofrefs / sizeof(*mark));

	armap->ar_size_of_strs = mark;
	uch = *mark++;

	sizeofstrs = (attr == AR_ARMAP_ATTR_LE_32)
		? (uch[3] << 24) + (uch[2] << 16) + (uch[1] << 8) + uch[0]
		: (uch[0] << 24) + (uch[1] << 16) + (uch[2] << 8) + uch[3];

	if (sizeofstrs > memberp->ar_object_size - 2*sizeof(*mark) - sizeofrefs)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_SIZE_OF_STRS);

	m->symstrs = (const char *)mark;

	cap  = memberp->ar_object_data;
	cap += memberp->ar_object_size;

	if ((cap == m->symstrs) && nsyms)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (nsyms && !m->symstrs[0])
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	for (ch=&m->symstrs[1],nstrs=0; ch<cap; ch++) {
		if (!ch[0] && !ch[-1] && (nstrs < nsyms))
			return SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

		if (!ch[0] && ch[-1])
			nstrs++;
	}

	if (nstrs != nsyms)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (cap[-1])
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (!(m->symstrv = calloc(nsyms + 1,sizeof(const char *))))
		return SLBT_SYSTEM_ERROR(dctx,0);

	armap->ar_string_table = m->symstrv;

	armapref = &m->armaps.armap_common_32;
	armapref->ar_member         = memberp;
	armapref->ar_armap_bsd      = armap;
	armapref->ar_armap_attr     = AR_ARMAP_ATTR_BSD | attr;
	armapref->ar_num_of_symbols = nsyms;
	armapref->ar_size_of_refs   = sizeofrefs;
	armapref->ar_size_of_strs   = sizeofstrs;
	armapref->ar_string_table   = m->symstrs;

	m->armaps.armap_nsyms = nsyms;

	m->armeta.a_armap_primary.ar_armap_common_32 = armapref;

	return 0;
}

static int slbt_ar_parse_primary_armap_bsd_64(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * m)
{
	struct ar_raw_armap_bsd_64 *    armap;
	struct ar_meta_member_info *    memberp;
	struct ar_meta_armap_common_64 *armapref;
	uint32_t                        attr;
	uint64_t                        u64_lo;
	uint64_t                        u64_hi;
	uint64_t                        nsyms;
	uint64_t                        nstrs;
	uint64_t                        sizeofrefs_le;
	uint64_t                        sizeofrefs_be;
	uint64_t                        sizeofrefs;
	uint64_t                        sizeofstrs;
	const char *                    ch;
	const char *                    cap;
	unsigned char *                 uch;
	unsigned char                   (*mark)[0x08];

	armap   = &m->armaps.armap_bsd_64;
	memberp = m->memberv[0];

	mark = memberp->ar_object_data;

	armap->ar_size_of_refs = mark;
	uch = *mark++;

	armap->ar_first_name_offset = mark;

	u64_lo = (uch[3] << 24) + (uch[2] << 16) + (uch[1] << 8) + uch[0];
	u64_hi = (uch[7] << 24) + (uch[6] << 16) + (uch[5] << 8) + uch[4];

	sizeofrefs_le = u64_lo + (u64_hi << 32);

	u64_hi = (uch[0] << 24) + (uch[1] << 16) + (uch[2] << 8) + uch[3];
	u64_lo = (uch[4] << 24) + (uch[5] << 16) + (uch[6] << 8) + uch[7];

	sizeofrefs_be = (u64_hi << 32) + u64_lo;

	if (sizeofrefs_le < memberp->ar_object_size - sizeof(*mark)) {
		sizeofrefs = sizeofrefs_le;
		attr       = AR_ARMAP_ATTR_LE_64;

	} else if (sizeofrefs_be < memberp->ar_object_size - sizeof(*mark)) {
		sizeofrefs = sizeofrefs_be;
		attr       = AR_ARMAP_ATTR_BE_64;
	} else {
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_SIZE_OF_REFS);
	}

	nsyms  = sizeofrefs / sizeof(struct ar_raw_armap_ref_64);
	mark  += (sizeofrefs / sizeof(*mark));

	armap->ar_size_of_strs = mark;
	uch = *mark++;

	if (attr == AR_ARMAP_ATTR_LE_64) {
		u64_lo = (uch[3] << 24) + (uch[2] << 16) + (uch[1] << 8) + uch[0];
		u64_hi = (uch[7] << 24) + (uch[6] << 16) + (uch[5] << 8) + uch[4];
	} else {
		u64_hi = (uch[0] << 24) + (uch[1] << 16) + (uch[2] << 8) + uch[3];
		u64_lo = (uch[4] << 24) + (uch[5] << 16) + (uch[6] << 8) + uch[7];
	}

	sizeofstrs = u64_lo + (u64_hi << 32);
	m->symstrs = (const char *)mark;

	cap  = memberp->ar_object_data;
	cap += memberp->ar_object_size;

	if ((cap == m->symstrs) && nsyms)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (nsyms && !m->symstrs[0])
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	for (ch=&m->symstrs[1],nstrs=0; ch<cap; ch++) {
		if (!ch[0] && !ch[-1] && (nstrs < nsyms))
			return SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

		if (!ch[0] && ch[-1])
			nstrs++;
	}

	if (nstrs != nsyms)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (cap[-1])
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (!(m->symstrv = calloc(nsyms + 1,sizeof(const char *))))
		return SLBT_SYSTEM_ERROR(dctx,0);

	armap->ar_string_table = m->symstrv;

	armapref = &m->armaps.armap_common_64;
	armapref->ar_member         = memberp;
	armapref->ar_armap_bsd      = armap;
	armapref->ar_armap_attr     = AR_ARMAP_ATTR_BSD | attr;
	armapref->ar_num_of_symbols = nsyms;
	armapref->ar_size_of_refs   = sizeofrefs;
	armapref->ar_size_of_strs   = sizeofstrs;
	armapref->ar_string_table   = m->symstrs;

	m->armaps.armap_nsyms = nsyms;

	m->armeta.a_armap_primary.ar_armap_common_64 = armapref;

	return 0;
}

static int slbt_ar_parse_primary_armap_sysv_32(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * m)
{
	struct ar_raw_armap_sysv_32 *   armap;
	struct ar_meta_member_info *    memberp;
	struct ar_meta_armap_common_32 *armapref;
	uint32_t                        nsyms;
	uint32_t                        nstrs;
	const char *                    ch;
	const char *                    cap;
	unsigned char *                 uch;
	unsigned char                   (*mark)[0x04];

	armap   = &m->armaps.armap_sysv_32;
	memberp = m->memberv[0];

	mark = memberp->ar_object_data;

	armap->ar_num_of_syms = mark;
	uch = *mark++;

	armap->ar_first_ref_offset = mark;

	nsyms = (uch[0] << 24) + (uch[1] << 16) + (uch[2] << 8) + uch[3];
	mark += nsyms;

	if (memberp->ar_object_size < (sizeof(*mark) + (nsyms * sizeof(*mark))))
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_NUMBER_OF_SYMS);

	m->symstrs = (const char *)mark;

	cap  = memberp->ar_object_data;
	cap += memberp->ar_object_size;

	if ((cap == m->symstrs) && nsyms)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (nsyms && !m->symstrs[0])
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	for (ch=&m->symstrs[1],nstrs=0; ch<cap; ch++) {
		if (!ch[0] && !ch[-1] && (nstrs < nsyms))
			return SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

		if (!ch[0] && ch[-1])
			nstrs++;
	}

	if (nstrs != nsyms)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (cap[-1])
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (!(m->symstrv = calloc(nsyms + 1,sizeof(const char *))))
		return SLBT_SYSTEM_ERROR(dctx,0);

	armap->ar_string_table = m->symstrv;

	armapref = &m->armaps.armap_common_32;
	armapref->ar_member         = memberp;
	armapref->ar_armap_sysv     = armap;
	armapref->ar_armap_attr     = AR_ARMAP_ATTR_SYSV | AR_ARMAP_ATTR_BE_32;
	armapref->ar_num_of_symbols = nsyms;
	armapref->ar_string_table   = m->symstrs;

	m->armaps.armap_nsyms = nsyms;

	m->armeta.a_armap_primary.ar_armap_common_32 = armapref;

	return 0;
}

static int slbt_ar_parse_primary_armap_sysv_64(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * m)
{
	struct ar_raw_armap_sysv_64 *   armap;
	struct ar_meta_member_info *    memberp;
	struct ar_meta_armap_common_64 *armapref;
	uint64_t                        nsyms_hi;
	uint64_t                        nsyms_lo;
	uint64_t                        nsyms;
	uint64_t                        nstrs;
	const char *                    ch;
	const char *                    cap;
	unsigned char *                 uch;
	unsigned char                   (*mark)[0x08];

	armap   = &m->armaps.armap_sysv_64;
	memberp = m->memberv[0];

	mark = memberp->ar_object_data;

	armap->ar_num_of_syms = mark;
	uch = *mark++;

	armap->ar_first_ref_offset = mark;

	nsyms_hi = (uch[0] << 24) + (uch[1] << 16) + (uch[2] << 8) + uch[3];
	nsyms_lo = (uch[4] << 24) + (uch[5] << 16) + (uch[6] << 8) + uch[7];

	nsyms = (nsyms_hi << 32) + nsyms_lo;
	mark += nsyms;

	if (memberp->ar_object_size < (sizeof(*mark) + (nsyms * sizeof(*mark))))
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_NUMBER_OF_SYMS);

	m->symstrs = (const char *)mark;

	cap  = memberp->ar_object_data;
	cap += memberp->ar_object_size;

	if ((cap == m->symstrs) && nsyms)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (nsyms && !m->symstrs[0])
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	for (ch=&m->symstrs[1],nstrs=0; ch<cap; ch++) {
		if (!ch[0] && !ch[-1] && (nstrs < nsyms))
			return SLBT_CUSTOM_ERROR(
				dctx,
				SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

		if (!ch[0] && ch[-1])
			nstrs++;
	}

	if (nstrs != nsyms)
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (cap[-1])
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_ARMAP_STRING_TABLE);

	if (!(m->symstrv = calloc(nsyms + 1,sizeof(const char *))))
		return SLBT_SYSTEM_ERROR(dctx,0);

	armap->ar_string_table = m->symstrv;

	armapref = &m->armaps.armap_common_64;
	armapref->ar_member         = memberp;
	armapref->ar_armap_sysv     = armap;
	armapref->ar_armap_attr     = AR_ARMAP_ATTR_SYSV | AR_ARMAP_ATTR_BE_64;
	armapref->ar_num_of_symbols = nsyms;
	armapref->ar_string_table   = m->symstrs;

	m->armaps.armap_nsyms = nsyms;

	m->armeta.a_armap_primary.ar_armap_common_64 = armapref;

	return 0;
}

static int slbt_ar_parse_primary_armap(
	const struct slbt_driver_ctx *  dctx,
	struct slbt_archive_meta_impl * m)

{
	struct ar_meta_member_info *    memberp;
	const char *                    hdrname;
	uint32_t                        hdrattr;

	memberp = m->memberv[0];
	hdrname = memberp->ar_file_header.ar_member_name;
	hdrattr = memberp->ar_file_header.ar_header_attr;

	if (!(memberp->ar_member_attr & AR_MEMBER_ATTR_ARMAP))
		return 0;

	if (hdrattr & AR_HEADER_ATTR_SYSV) {
		/* mips 64-bit armap member? */
		if (!strncmp(hdrname,"/SYM64/",7))
			return slbt_ar_parse_primary_armap_sysv_64(
				dctx,m);

		/* sysv 32-bit armap member */
		return slbt_ar_parse_primary_armap_sysv_32(
			dctx,m);

	} else if (hdrattr & AR_HEADER_ATTR_BSD) {
		if (!strcmp(hdrname,"__.SYMDEF"))
			return slbt_ar_parse_primary_armap_bsd_32(
				dctx,m);

		else if (!strcmp(hdrname,"__.SYMDEF SORTED"))
			return slbt_ar_parse_primary_armap_bsd_32(
				dctx,m);

		else if (!strcmp(hdrname,"__.SYMDEF_64"))
			return slbt_ar_parse_primary_armap_bsd_64(
				dctx,m);

		else if (!strcmp(hdrname,"__.SYMDEF_64 SORTED"))
			return slbt_ar_parse_primary_armap_bsd_64(
				dctx,m);
	}

	return SLBT_CUSTOM_ERROR(dctx,SLBT_ERR_FLOW_ERROR);
}

int slbt_get_archive_meta(
	const struct slbt_driver_ctx *  dctx,
	const struct slbt_raw_archive * archive,
	struct slbt_archive_meta **     meta)
{
	const char *                    mark;
	const char *                    cap;
	struct slbt_archive_meta_impl * m;
	const char *                    slash;
	const char *                    ch;
	const char *                    fldcap;
	size_t				nelements;
	uint64_t                        nentries;
	uint64_t                        stblsize;
	uint64_t                        filesize;
	uint64_t                        namelen;
	uint64_t                        nameoff;
	uint32_t			attr;
	void *                          s_addr;
	void *                          m_addr;
	const char *                    s_ptr;
	const char *                    m_ptr;
	struct ar_raw_file_header *	arhdr;
	struct ar_raw_file_header *	arlongnames;
	struct ar_meta_member_info *    memberp;
	char *				longnamep;
	size_t				idx;
	struct ar_header_info *		hdrinfov;
	struct ar_header_info *		hdrinfov_cap;
	struct ar_header_info *		hdrinfov_next;
	struct ar_header_info		hdrinfobuf[AR_STACK_VECTOR_ELEMENTS];

	/* init */
	hdrinfov     = hdrinfobuf;
	hdrinfov_cap = &hdrinfobuf[AR_STACK_VECTOR_ELEMENTS];
	nelements    = AR_STACK_VECTOR_ELEMENTS;

	memset(hdrinfobuf,0,sizeof(hdrinfobuf));

	mark = archive->map_addr;
	cap  = &mark[archive->map_size];

	/* preliminary validation */
	if (archive->map_size < sizeof(struct ar_raw_signature))
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_SIGNATURE);

	else if (strncmp(mark,ar_signature,sizeof(struct ar_raw_signature)))
		return SLBT_CUSTOM_ERROR(
			dctx,
			SLBT_ERR_AR_INVALID_SIGNATURE);

	/* alloc */
	if (!(m = calloc(1,sizeof(*m))))
		return SLBT_SYSTEM_ERROR(dctx,0);

	/* archive map info */
	m->armeta.r_archive.map_addr = archive->map_addr;
	m->armeta.r_archive.map_size = archive->map_size;

	/* archive signature */
	m->armeta.r_signature = (struct ar_raw_signature *)mark;
	m->armeta.m_signature = (struct ar_meta_signature *)ar_signature;

	/* signature only? */
	if (archive->map_size == sizeof(struct ar_raw_signature)) {
		*meta = &m->armeta;
		return 0;
	}

	mark += sizeof(struct ar_raw_signature);

	/* only trailing null characters past the signature? */
	if (cap < &mark[sizeof(*arhdr)])
		for (ch=mark; ch<cap; ch++)
			if (*ch)
				return slbt_free_archive_meta_impl(
					m,SLBT_CUSTOM_ERROR(
						dctx,
						SLBT_ERR_AR_INVALID_HEADER));

	/* count entries, calculate string table size */
	for (nentries=0,stblsize=0,arlongnames=0; mark<cap; nentries++) {
		arhdr = (struct ar_raw_file_header *)mark;

		/* file size */
		if ((slbt_ar_read_decimal_64(
				arhdr->ar_file_size,
				sizeof(arhdr->ar_file_size),
				&filesize)) < 0)
			return slbt_free_archive_meta_impl(
				m,SLBT_CUSTOM_ERROR(
					dctx,
					SLBT_ERR_AR_INVALID_HEADER));

		mark += sizeof(struct ar_raw_file_header);

		/* stblsize, member name type */
		fldcap = &arhdr->ar_file_id[sizeof(arhdr->ar_file_id)];

		/* sysv long names table? */
		if ((arhdr->ar_file_id[0] == '/') && (arhdr->ar_file_id[1] == '/')) {
			for (ch=&arhdr->ar_file_id[2]; ch<fldcap; ch++)
				if (*ch != AR_DEC_PADDING)
					return slbt_free_archive_meta_impl(
						m,SLBT_CUSTOM_ERROR(
							dctx,
							SLBT_ERR_AR_INVALID_HEADER));

			if (slbt_ar_read_decimal_64(
					arhdr->ar_file_size,
					sizeof(arhdr->ar_file_size),
					&namelen) < 0)
				return slbt_free_archive_meta_impl(
					m,SLBT_CUSTOM_ERROR(
						dctx,
						SLBT_ERR_AR_INVALID_HEADER));


			/* duplicate long names member? */
			if (arlongnames)
				return slbt_free_archive_meta_impl(
					m,SLBT_CUSTOM_ERROR(
						dctx,
						SLBT_ERR_AR_DUPLICATE_LONG_NAMES));

			attr = AR_HEADER_ATTR_FILE_ID | AR_HEADER_ATTR_SYSV;

			stblsize++;
			stblsize++;
			stblsize++;

			stblsize += namelen;

			arlongnames = arhdr;

		/* the /SYM64/ string must be special cased, also below when it gets copied */
		} else if (!strncmp(arhdr->ar_file_id,"/SYM64/",7)) {
			for (ch=&arhdr->ar_file_id[7]; ch<fldcap; ch++)
				if (*ch != AR_DEC_PADDING)
					return slbt_free_archive_meta_impl(
						m,SLBT_CUSTOM_ERROR(
							dctx,
							SLBT_ERR_AR_INVALID_HEADER));

			attr      = AR_HEADER_ATTR_FILE_ID | AR_HEADER_ATTR_SYSV;
			stblsize += 8;

		/* sysv armap member or sysv long name reference? */
		} else if (arhdr->ar_file_id[0] == '/') {
			if (slbt_ar_read_decimal_64(
					&arhdr->ar_file_id[1],
					sizeof(arhdr->ar_file_id)-1,
					&nameoff) < 0)
				return slbt_free_archive_meta_impl(
					m,SLBT_CUSTOM_ERROR(
						dctx,
						SLBT_ERR_AR_INVALID_HEADER));

			if (arhdr->ar_file_id[1] == AR_DEC_PADDING) {
				attr = AR_HEADER_ATTR_FILE_ID | AR_HEADER_ATTR_SYSV;
				stblsize++;
				stblsize++;
			} else {
				attr = AR_HEADER_ATTR_NAME_REF | AR_HEADER_ATTR_SYSV;
			}

		/* bsd long name reference? */
		} else if ((arhdr->ar_file_id[0] == '#')
				&& (arhdr->ar_file_id[1] == '1')
				&& (arhdr->ar_file_id[2] == '/')) {
			if (slbt_ar_read_decimal_64(
					&arhdr->ar_file_id[3],
					sizeof(arhdr->ar_file_id)-3,
					&namelen) < 0)
				return slbt_free_archive_meta_impl(
					m,SLBT_CUSTOM_ERROR(
						dctx,
						SLBT_ERR_AR_INVALID_HEADER));

			attr = AR_HEADER_ATTR_NAME_REF | AR_HEADER_ATTR_BSD;

			stblsize += namelen + 1;

		/* must be either a sysv short member name, or a (legacy) bsd short name */
		} else {
			for (ch=arhdr->ar_file_id,slash=0; (ch<fldcap) && !slash; ch++)
				if (*ch == '/')
					slash = ch;

			if (slash) {
				attr      = AR_HEADER_ATTR_FILE_ID | AR_HEADER_ATTR_SYSV;
				stblsize += (slash - arhdr->ar_file_id) + 1;
			} else {
				attr      = AR_HEADER_ATTR_FILE_ID | AR_HEADER_ATTR_BSD;
				stblsize += sizeof(arhdr->ar_file_id) + 1;
			}

			for (; ch<fldcap; )
				if (*ch++ != AR_DEC_PADDING)
					return slbt_free_archive_meta_impl(
						m,SLBT_CUSTOM_ERROR(
							dctx,
							SLBT_ERR_AR_INVALID_HEADER));

		}

		/* truncated data? */
		if (cap < &mark[filesize])
			return slbt_free_archive_meta_impl(
				m,SLBT_CUSTOM_ERROR(
					dctx,
					SLBT_ERR_AR_TRUNCATED_DATA));

		/* ar member alignment */
		filesize += 1;
		filesize |= 1;
		filesize ^= 1;

		mark += filesize;

		/* only trailing null characters past the signature? */
		if (cap < &mark[sizeof(*arhdr)])
			for (; mark<cap; )
				if (*mark++)
					return slbt_free_archive_meta_impl(
						m,SLBT_CUSTOM_ERROR(
							dctx,
							SLBT_ERR_AR_INVALID_HEADER));

		/* transient header info vector */
		if (&hdrinfov[nentries] == hdrinfov_cap) {
			nelements = (nelements == AR_STACK_VECTOR_ELEMENTS)
				? (nelements << 4) : (nelements << 1);

			if (!(hdrinfov_next = calloc(nelements,sizeof(*hdrinfov))))
				return slbt_free_archive_meta_impl(
					m,SLBT_CUSTOM_ERROR(
						dctx,
						SLBT_ERR_AR_TRUNCATED_DATA));

			for (idx=0; idx<nentries; idx++) {
				hdrinfov_next[idx].phdr = hdrinfov[idx].phdr;
				hdrinfov_next[idx].attr = hdrinfov[idx].attr;
			};

			if (hdrinfov != hdrinfobuf)
				free(hdrinfov);

			hdrinfov     = hdrinfov_next;
			hdrinfov_cap = &hdrinfov_next[nelements];
			m->hdrinfov  = hdrinfov;
		}

		hdrinfov[nentries].phdr = arhdr;
		hdrinfov[nentries].attr = attr;
	}

	/* allocate name strings, member vector */
	if (!(m->namestrs = calloc(1,stblsize)))
		return slbt_free_archive_meta_impl(
			m,SLBT_SYSTEM_ERROR(dctx,0));

	if (!(m->offsetv = calloc(nentries+1,sizeof(*m->offsetv))))
		return slbt_free_archive_meta_impl(
			m,SLBT_SYSTEM_ERROR(dctx,0));

	if (!(m->memberv = calloc(nentries+1,sizeof(*m->memberv))))
		return slbt_free_archive_meta_impl(
			m,SLBT_SYSTEM_ERROR(dctx,0));

	if (!(m->members = calloc(nentries,sizeof(*m->members))))
		return slbt_free_archive_meta_impl(
			m,SLBT_SYSTEM_ERROR(dctx,0));

	/* archive signature reference */
	s_addr = archive->map_addr;
	s_ptr  = s_addr;

	/* iterate, store meta data in library-friendly form */
	for (idx=0,longnamep=m->namestrs; idx<nentries; idx++) {
		arhdr           = hdrinfov[idx].phdr;
		attr            = hdrinfov[idx].attr;

		m_addr          = arhdr;
		m_ptr           = m_addr;

		memberp         = &m->members[idx];
		m->offsetv[idx] = m_ptr - s_ptr;
		m->memberv[idx] = memberp;

		memberp->ar_file_header.ar_header_attr = attr;

		slbt_ar_read_decimal_64(
			arhdr->ar_time_date_stamp,
			sizeof(arhdr->ar_time_date_stamp),
			&memberp->ar_file_header.ar_time_date_stamp);

		slbt_ar_read_decimal_32(
			arhdr->ar_uid,
			sizeof(arhdr->ar_uid),
			&memberp->ar_file_header.ar_uid);

		slbt_ar_read_decimal_32(
			arhdr->ar_gid,
			sizeof(arhdr->ar_gid),
			&memberp->ar_file_header.ar_gid);

		slbt_ar_read_octal(
			arhdr->ar_file_mode,
			sizeof(arhdr->ar_file_mode),
			&memberp->ar_file_header.ar_file_mode);

		slbt_ar_read_decimal_64(
			arhdr->ar_file_size,
			sizeof(arhdr->ar_file_size),
			&memberp->ar_file_header.ar_file_size);

		memberp->ar_file_header.ar_member_name = longnamep;

		if (attr == (AR_HEADER_ATTR_FILE_ID | AR_HEADER_ATTR_SYSV)) {
			if ((arhdr->ar_file_id[0] == '/') && (arhdr->ar_file_id[1] == '/')) {
				*longnamep++ = '/';
				*longnamep++ = '/';
				longnamep++;

			} else if ((arhdr->ar_file_id[0] == '/') && (arhdr->ar_file_id[1] == 'S')) {
				*longnamep++ = '/';
				*longnamep++ = 'S';
				*longnamep++ = 'Y';
				*longnamep++ = 'M';
				*longnamep++ = '6';
				*longnamep++ = '4';
				*longnamep++ = '/';
				longnamep++;

			} else if (arhdr->ar_file_id[0] == '/') {
				*longnamep++ = '/';
				longnamep++;

			} else {
				ch = arhdr->ar_file_id;

				for (; (*ch != '/'); )
					*longnamep++ = *ch++;

				longnamep++;
			}

		} else if (attr == (AR_HEADER_ATTR_FILE_ID | AR_HEADER_ATTR_BSD)) {
			ch     = arhdr->ar_file_id;
			fldcap = &ch[sizeof(arhdr->ar_file_id)];

			for (; (ch<fldcap) && (*ch != AR_DEC_PADDING); )
				*longnamep++ = *ch++;

			longnamep++;

		} else if (attr == (AR_HEADER_ATTR_NAME_REF | AR_HEADER_ATTR_SYSV)) {
			slbt_ar_read_decimal_64(
				&arhdr->ar_file_id[1],
				sizeof(arhdr->ar_file_id) - 1,
				&nameoff);

			ch  = arlongnames->ar_file_id;
			ch += sizeof(*arlongnames);
			ch += nameoff;

			for (; *ch && (*ch != '/') && (*ch != AR_OBJ_PADDING); )
				*longnamep++ = *ch++;

			longnamep++;

		} else if (attr == (AR_HEADER_ATTR_NAME_REF | AR_HEADER_ATTR_BSD)) {
			slbt_ar_read_decimal_64(
				&arhdr->ar_file_id[3],
				sizeof(arhdr->ar_file_id) - 3,
				&namelen);

			mark  = arhdr->ar_file_id;
			mark += sizeof(*arhdr);

			memcpy(longnamep,mark,namelen);

			longnamep += namelen;
			longnamep++;
		}

		/* member raw header, object size, object data */
		mark    = arhdr->ar_file_id;
		mark   += sizeof(*arhdr);
		namelen = 0;

		if (attr == (AR_HEADER_ATTR_NAME_REF | AR_HEADER_ATTR_BSD)) {
			slbt_ar_read_decimal_64(
				&arhdr->ar_file_id[3],
				sizeof(arhdr->ar_file_id)-3,
				&namelen);

			namelen += 1;
			namelen |= 1;
			namelen ^= 1;

			mark += namelen;
		};

		memberp->ar_member_data = arhdr;
		memberp->ar_object_data = (void *)mark;
		memberp->ar_object_size = memberp->ar_file_header.ar_file_size - namelen;

		/* member attribute */
		memberp->ar_member_attr = slbt_ar_get_member_attr(memberp);

		/* pe/coff second linker member? */
		if ((idx == 1) && (memberp->ar_member_attr == AR_MEMBER_ATTR_ARMAP))
			if (hdrinfov[0].attr & AR_HEADER_ATTR_SYSV)
				if (m->members[0].ar_member_attr == AR_MEMBER_ATTR_ARMAP)
					if (attr & AR_HEADER_ATTR_SYSV)
						memberp->ar_member_attr = AR_MEMBER_ATTR_LINKINFO;

		/* armap member must be the first */
		if ((memberp->ar_member_attr == AR_MEMBER_ATTR_ARMAP) && (idx > 0)) {
			if (m->members[0].ar_member_attr == AR_MEMBER_ATTR_ARMAP)
				return slbt_free_archive_meta_impl(
					m,SLBT_CUSTOM_ERROR(
						dctx,
						SLBT_ERR_AR_DUPLICATE_ARMAP_MEMBER));

			return slbt_free_archive_meta_impl(
				m,SLBT_CUSTOM_ERROR(
					dctx,
					SLBT_ERR_AR_MISPLACED_ARMAP_MEMBER));
		}
	}

	/* primary armap (first linker member) */
	if (slbt_ar_parse_primary_armap(dctx,m) < 0)
		return slbt_free_archive_meta_impl(
			m,SLBT_NESTED_ERROR(dctx));

	for (idx=0,ch=m->symstrs; idx<m->armaps.armap_nsyms; idx++) {
		m->symstrv[idx] = ch;
		ch += strlen(ch);
		ch++;
	}

	/* pe/coff armap attributes (second linker member) */
	(void)m->armeta.a_armap_pecoff;

	/* member vector */
	m->armeta.a_memberv = m->memberv;

	/* all done */
	if (m->hdrinfov) {
		free(m->hdrinfov);
		m->hdrinfov = 0;
	}

	*meta = &m->armeta;

	return 0;
}

void slbt_free_archive_meta(struct slbt_archive_meta * meta)
{
	struct slbt_archive_meta_impl * m;

	if (meta) {
		m = slbt_archive_meta_ictx(meta);
		slbt_free_archive_meta_impl(m,0);
	}
}
