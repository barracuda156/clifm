/*
 * This file is part of Clifm
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2016-2025, L. Abramovich <leo.clifm@outlook.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
*/

/* long_view.c -- Construct entries in long view mode */

#include "helpers.h"

#include <string.h> /* memcpy(3), strrchr(3) */
#include <time.h>   /* strftime(3) */

#include "aux.h"    /* xitoa() */
#include "checks.h" /* check_file_access() */
#include "colors.h" /* remove_bold_attr() */
#include "long_view.h" /* macros */
#include "misc.h"   /* gen_diff_str() */
#include "properties.h" /* get_color_age, get_color_size, get_file_perms */

static char *
get_ext_info_long(const struct fileinfo *props, const size_t name_len,
	int *trunc, size_t *ext_len)
{
	char *ext_name = (char *)NULL;
	char *e = strrchr(props->name, '.');
	if (e && e != props->name && e[1]) {
		ext_name = e;
		*trunc = TRUNC_EXT;
		if (props->utf8 == 0)
			*ext_len = name_len - (size_t)(ext_name - props->name);
		else
			*ext_len = wc_xstrlen(ext_name);
	}

	if ((int)*ext_len >= conf.max_name_len || (int)*ext_len <= 0) {
		*ext_len = 0;
		*trunc = TRUNC_NO_EXT;
	}

	return ext_name;
}

/* Calculate the relative time of AGE, which is the difference between
 * NOW and the corresponding file time. */
static void
calc_relative_time(const time_t age, char *s)
{
	if (age < 0L) /* Future (AGE, however, is guaranteed to be positive) */
		snprintf(s, MAX_TIME_STR, " -     ");
	else if (age < RT_MINUTE)
		snprintf(s, MAX_TIME_STR, "%*ju  sec", 2, (uintmax_t)age);
	else if (age < RT_HOUR)
		snprintf(s, MAX_TIME_STR, "%*ju  min", 2, (uintmax_t)(age / RT_MINUTE));
	else if (age < RT_DAY)
		snprintf(s, MAX_TIME_STR, "%*ju hour", 2, (uintmax_t)(age / RT_HOUR));
	else if (age < RT_WEEK)
		snprintf(s, MAX_TIME_STR, "%*ju  day", 2, (uintmax_t)(age / RT_DAY));
	else if (age < RT_MONTH) {
		/* RT_MONTH is 30 days. But since Feb has only 28, we get 4 weeks
		 * in some cases, which is weird. Always make 4 weeks into 1 month */
		const long long n = age / RT_WEEK;
		if (n == 4)
			snprintf(s, MAX_TIME_STR, " 1  mon");
		else
			snprintf(s, MAX_TIME_STR, "%*ju week", 2, (uintmax_t)n);
	}
	else if (age < RT_YEAR) {
		const long long n = age / RT_MONTH;
		if (n == 12)
			snprintf(s, MAX_TIME_STR, " 1 year");
		else
			snprintf(s, MAX_TIME_STR, "%*ju  mon", 2, (uintmax_t)n);
	}
	else {
		snprintf(s, MAX_TIME_STR, "%*ju year", 2, (uintmax_t)(age / RT_YEAR));
	}
}

static void
construct_and_print_filename(const struct fileinfo *props,
	const int max_namelen)
{
	/* If filename length is greater than max, truncate it to max (later a
	 * tilde (~) will be appended to let the user know the filename was
	 * truncated). */
	int trunc = 0;

	/* Handle filenames with embedded control characters */
	size_t plen = props->len;
	char *wname = (char *)NULL;
	if (props->len == 0) {
		wname = replace_invalid_chars(props->name);
		plen = wc_xstrlen(wname);
	}

	const filesn_t n = (conf.max_files > UNSET
		&& files > (filesn_t)conf.max_files) ? (filesn_t)conf.max_files : files;

	size_t cur_len = (size_t)DIGINUM(n) + 1 + plen
		+ (conf.icons == 1 ? (size_t)ICON_LEN : 0);

	int diff = 0;
	char *name = wname ? wname : props->name;
	char *ext_name = (char *)NULL;

	if (cur_len > (size_t)max_namelen) {
		const int rest = (int)cur_len - max_namelen;
		trunc = TRUNC_NO_EXT;
		size_t ext_len = 0;
		ext_name = get_ext_info_long(props, plen, &trunc, &ext_len);

		int trunc_point = (int)plen - rest - 1 - (int)ext_len;
		if (trunc_point <= 0) {
			trunc_point = (int)plen - rest - 1;
			trunc = TRUNC_NO_EXT;
		}

		if (props->utf8 == 1) {
			if (wname)
				xstrsncpy(name_buf, name, sizeof(name_buf));
			else /* memcpy is faster: use it whenever possible. */
				memcpy(name_buf, name, props->bytes + 1);

			diff = u8truncstr(name_buf, (size_t)trunc_point);
		} else { /* Let's avoid u8truncstr() to get some extra speed. */
			const char c = name[trunc_point];
			name[trunc_point] = '\0';
			mbstowcs((wchar_t *)name_buf, name, NAME_BUF_SIZE);
			name[trunc_point] = c;
		}

		cur_len -= (size_t)rest;
	} else {
		mbstowcs((wchar_t *)name_buf, name, NAME_BUF_SIZE);
	}

	free(wname);

	/* Calculate pad for each filename */
	int pad = max_namelen - (int)cur_len;
	if (pad < 0)
		pad = 0;

	const char *trunc_diff = diff > 0 ? gen_diff_str(diff) : "";

	static char trunc_s[2] = {0};
	*trunc_s = trunc > 0 ? TRUNC_FILE_CHR : 0;

	printf("%s%s%s%s%s%ls%s%s%-*s%s\x1b[0m%s%s\x1b[0m%s%s%s  ",
		(conf.colorize == 1 && conf.icons == 1) ? props->icon_color : "",
		conf.icons == 1 ? props->icon : "", conf.icons == 1 ? " " : "", df_c,

		conf.colorize == 1 ? props->color : "",
		(wchar_t *)name_buf, trunc_diff,
		conf.light_mode == 1 ? "\x1b[0m" : df_c, pad, "", df_c,
		trunc ? tt_c : "", trunc_s,
		trunc == TRUNC_EXT ? props->color : "",
		trunc == TRUNC_EXT ? ext_name : "",
		trunc == TRUNC_EXT ? df_c : "");
}

static void
construct_file_size(const struct fileinfo *props, char *size_str,
	const int size_max, const int file_perm)
{
	if (prop_fields.size == 0) {
		*size_str = '\0';
		return;
	}

	if (props->stat_err == 1) {
		snprintf(size_str, SIZE_STR_LEN, "%*s", size_max
			+ (prop_fields.size == PROP_SIZE_HUMAN), UNKNOWN_STR);
		return;
	}

	const int no_dir_access =
		(file_perm == 0 && props->dir == 1 && conf.full_dir_size == 1);
	if (S_ISCHR(props->mode) || S_ISBLK(props->mode) || no_dir_access == 1) {
		snprintf(size_str, SIZE_STR_LEN, "%s%*c%s", dn_c, size_max
			+ (prop_fields.size == PROP_SIZE_HUMAN),
			no_dir_access == 1 ? UNKNOWN_CHR : '-', df_c);
		return;
	}

	const off_t size = (FILE_TYPE_NON_ZERO_SIZE(props->mode)
		|| props->type == DT_SHM || props->type == DT_TPO)
		? props->size : 0;

	/* Let's construct the color for the current file size */
	char *csize = dz_c;
	static char sf[MAX_SHADE_LEN];
	if (conf.colorize == 1) {
		if (!*dz_c) {
			get_color_size(size, sf, sizeof(sf));
			csize = sf;
		}
	}

	if (prop_fields.size != PROP_SIZE_HUMAN) {
		snprintf(size_str, SIZE_STR_LEN, "%s%*jd%s%c", csize,
			(props->du_status != 0 && size_max > 0) ? size_max - 1 : size_max,
			(intmax_t)size, df_c,
			props->du_status != 0 ? DU_ERR_CHAR : 0);
		return;
	}

	const int du_err = (props->dir == 1 && conf.full_dir_size == 1
		&& props->du_status != 0);
	const char *unit_color = conf.colorize == 0
		? (du_err == 1 ? "\x1b[1m" : "")
		: (du_err == 1 ? xf_cb : dim_c);

	snprintf(size_str, SIZE_STR_LEN, "%s%*s%s%c\x1b[0m%s",
		csize, size_max,
		*props->human_size.str ? props->human_size.str : UNKNOWN_STR,
		unit_color, props->human_size.unit, df_c);
}

static void
construct_file_perms(const mode_t mode, char *perm_str, const char file_type,
	const char *ctype)
{
	static char tmp_ctype[MAX_COLOR + 1];
	xstrsncpy(tmp_ctype, (file_type == UNK_PCHR ? df_c : ctype),
		sizeof(tmp_ctype));
	if (xargs.no_bold != 1)
		remove_bold_attr(tmp_ctype);

	if (prop_fields.perm == PERM_SYMBOLIC) {
		const struct perms_t perms = get_file_perms(mode);
		snprintf(perm_str, PERM_STR_LEN,
			"%s%c%s/%s%c%s%c%s%c%s.%s%c%s%c%s%c%s.%s%c%s%c%s%c%s",
			tmp_ctype, file_type, dn_c,
			perms.cur, perms.ur, perms.cuw, perms.uw, perms.cux, perms.ux, dn_c,
			perms.cgr, perms.gr, perms.cgw, perms.gw, perms.cgx, perms.gx, dn_c,
			perms.cor, perms.or, perms.cow, perms.ow, perms.cox, perms.ox, df_c);

	} else /* PERM_NUMERIC */ {
		snprintf(perm_str, PERM_STR_LEN, "%s%04o%s", do_c, mode & 07777, df_c);

	}
}

static char *
get_time_char(void)
{
	if (conf.time_follows_sort == 1) {
		switch (conf.sort) {
		case SATIME: return conf.relative_time == 1 ? "A" : "a";
		case SCTIME: return conf.relative_time == 1 ? "C" : "c";
		case SBTIME: return conf.relative_time == 1 ? "B" : "b";
		case SMTIME: return conf.relative_time == 1 ? "M" : "m";
		default: break;
		}
	}

	switch (prop_fields.time) {
	case PROP_TIME_ACCESS: return conf.relative_time == 1 ? "A" : "a";
	case PROP_TIME_BIRTH: return conf.relative_time == 1 ? "B" : "b";
	case PROP_TIME_CHANGE: return conf.relative_time == 1 ? "C" : "c";
	case PROP_TIME_MOD: /* fallthrough */
	default: return conf.relative_time == 1 ? "M" : "m";
	}
}

/* GCC (not clang) complains about tfmt being not a string literal. Let's
 * silence this warning until we find a better approach. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
static void
construct_timestamp(char *time_str, const struct fileinfo *props)
{
	const time_t t = props->ltime;

	/* Let's construct the color for the current timestamp. */
	char *cdate = dd_c;
	static char df[MAX_SHADE_LEN];
	if (conf.colorize == 1 && !*dd_c) {
		get_color_age(t, df, sizeof(df));
		cdate = df;
	}

	static char file_time[MAX_TIME_STR];
	struct tm tm;

	if (props->stat_err == 1) {
		/* Let' use the same string we use for invalid times, but
		 * replace '-' by '?'. */
		xstrsncpy(file_time, invalid_time_str, sizeof(file_time));
		const int index = conf.relative_time == 1 ? 1 : 0;
		file_time[index] = UNKNOWN_CHR;
		cdate = df_c;
	} else if (t >= 0 && t != (time_t)-1) {
		/* PROPS_NOW (global) is set by list_dir(), in listing.c before
		 * calling print_entry_props(), which calls this function. */
		const time_t age = props_now - t;
		/* AGE is negative if file time is in the future. */

		if (conf.relative_time == 1) {
			calc_relative_time(age < 0 ? -age : age, file_time);
		} else if (localtime_r(&t, &tm)) {
			/* If not user defined, let's mimic ls(1) behavior: a file is
			 * considered recent if it is within the past six months. */
			const int recent = age >= 0 && age < 14515200LL;
			/* 14515200 == 6*4*7*24*60*60 == six months */
			const char *tfmt = conf.time_str ? conf.time_str :
				(recent ? DEF_TIME_STYLE_RECENT : DEF_TIME_STYLE_OLDER);

			strftime(file_time, sizeof(file_time), tfmt, &tm);
		} else {
			xstrsncpy(file_time, invalid_time_str, sizeof(file_time));
		}
	} else {
		/* INVALID_TIME_STR (global) is generated at startup by
		 * check_time_str(), in init.c. */
		xstrsncpy(file_time, invalid_time_str, sizeof(file_time));
	}

	snprintf(time_str, TIME_STR_LEN, "%s%s%s%s%s", cdate, *file_time
		? file_time : UNKNOWN_STR, dt_c,
		conf.timestamp_mark == 1 ? get_time_char() : "", df_c);
}
#pragma GCC diagnostic pop

static void
construct_id_field(const struct fileinfo *props, char *id_str,
	const struct maxes_t *maxes, const int file_perm)
{
	const char *uid_color =
		(file_perm == 1 && conf.colorize == 1) ? du_c : df_c;

#define USER_NAME props->uid_i.name ? props->uid_i.name \
		: (props->stat_err == 1 ? UNKNOWN_STR : xitoa(props->uid))
#define GROUP_NAME props->gid_i.name ? props->gid_i.name \
		: (props->stat_err == 1 ? UNKNOWN_STR : xitoa(props->gid))

	if (prop_fields.no_group == 1) {
		if (prop_fields.ids == PROP_ID_NUM) {
			if (props->stat_err == 1) {
				snprintf(id_str, ID_STR_LEN, "%s%*s%s", uid_color,
					maxes->id_user, UNKNOWN_STR, df_c);
			} else {
				snprintf(id_str, ID_STR_LEN, "%s%*u%s", uid_color,
					maxes->id_user, props->uid, df_c);
			}
		} else { /* PROPS_ID_NAME */
			snprintf(id_str, ID_STR_LEN, "%s%-*s%s", uid_color,
				maxes->id_user, USER_NAME, df_c);
		}

		return;
	}

	const char *gid_color = conf.colorize == 0 ? "" :
		(file_perm == 1 ? dg_c : dim_c);

	if (prop_fields.ids == PROP_ID_NUM) {
		if (props->stat_err == 1) {
			snprintf(id_str, ID_STR_LEN, "%s%*c %*c",
				df_c, maxes->id_user, UNKNOWN_CHR,
				maxes->id_group, UNKNOWN_CHR);
		} else {
			snprintf(id_str, ID_STR_LEN, "%s%*u %s%*u%s",
				uid_color, maxes->id_user, props->uid,
				gid_color, maxes->id_group, props->gid, df_c);
		}
	} else { /* PROPS_ID_NAME */
		snprintf(id_str, ID_STR_LEN, "%s%-*s %s%-*s%s",
			uid_color, maxes->id_user, USER_NAME,
			props->stat_err == 1 ? "" : gid_color,
			maxes->id_group, GROUP_NAME, df_c);
	}

#undef USER_NAME
#undef GROUP_NAME
}

static void
construct_files_counter(const struct fileinfo *props, char *fc_str,
	const int max)
{
	if (props->filesn > 0) {
		snprintf(fc_str, FC_STR_LEN, "%s%*d%s", fc_c, max,
			(int)props->filesn, df_c);
	} else {
		snprintf(fc_str, FC_STR_LEN, "%s%*c%s", dn_c, max,
			props->filesn < 0 ? UNKNOWN_CHR /* Dir with no read permission */
			: (props->dir == 1 ? '0' : '-'),
			df_c);
	}
}

static void
set_file_type_and_color(const struct fileinfo *props, char *type, char **color)
{
	struct stat a;
	if (props->stat_err == 1 && conf.follow_symlinks_long == 1
	&& conf.long_view == 1 && conf.follow_symlinks == 1
	&& lstat(props->name, &a) == 0 && S_ISLNK(a.st_mode)) {
		*type = LNK_PCHR;
		*color = conf.colorize == 1 ? ln_c : df_c;
		return;
	}

	switch (props->mode & S_IFMT) {
	case S_IFREG:  *type = REG_PCHR; break;
	case S_IFDIR:  *type = DIR_PCHR; *color = di_c; break;
	case S_IFLNK:  *type = LNK_PCHR; *color = ln_c; break;
	case S_IFIFO:  *type = FIFO_PCHR; *color = pi_c; break;
	case S_IFSOCK: *type = SOCK_PCHR; *color = so_c; break;
	case S_IFBLK:  *type = BLKDEV_PCHR; *color = bd_c; break;
	case S_IFCHR:  *type = CHARDEV_PCHR; *color = cd_c; break;
#ifndef _BE_POSIX
# ifdef S_ARCH1
	case S_ARCH1:  *type = ARCH1_PCHR; *color = fi_c; break;
	case S_ARCH2:  *type = ARCH2_PCHR; *color = fi_c; break;
# endif /* S_ARCH1 */
# ifdef SOLARIS_DOORS
	case S_IFDOOR: *type = DOOR_PCHR; *color = oo_c; break;
	case S_IFPORT: *type = PORT_PCHR; *color = oo_c; break;
# endif /* SOLARIS_DOORS */
# ifdef S_IFWHT
	case S_IFWHT:  *type = WHT_PCHR; *color = fi_c; break;
# endif /* S_IFWHT */
#endif /* !_BE_POSIX */
	default:       *type = UNK_PCHR; break;
	}

	if (conf.colorize == 0)
		*color = df_c;
}

static void
construct_inode_num(const struct fileinfo *props, char *ino_str, const int max)
{
	if (props->stat_err == 1) {
		snprintf(ino_str, INO_STR_LEN, "\x1b[0m%*s%s", max, UNKNOWN_STR, df_c);
		return;
	}

	snprintf(ino_str, INO_STR_LEN, "\x1b[0m%s%*ju%s", de_c,
		max, (uintmax_t)props->inode, df_c);
}

static void
construct_links_str(const struct fileinfo *props, char *links_str, const int max)
{
	if (props->stat_err == 1) {
		snprintf(links_str, LINKS_STR_LEN, "\x1b[0m%*s%s",
			max, UNKNOWN_STR, df_c);
		return;
	}

	snprintf(links_str, LINKS_STR_LEN, "\x1b[0m%s%s%*ju%s",
		dk_c, props->linkn > 1 ? BOLD : "", max, (uintmax_t)props->linkn, df_c);
}

static void
construct_blocks_str(const struct fileinfo *props, char *blk_str, const int max)
{
	if (props->stat_err == 1) {
		snprintf(blk_str, BLK_STR_LEN, "\x1b[0m%*s%s", max, UNKNOWN_STR, df_c);
		return;
	}

	snprintf(blk_str, BLK_STR_LEN, "\x1b[0m%s%*jd%s",
		db_c, max, (intmax_t)props->blocks, df_c);
}

/* Compose the properties line for the current filename.
 * This function is called by list_dir(), in listing.c, for each filename
 * in the current directory when running in long view mode (after
 * printing the corresponding ELN). */
int
print_entry_props(const struct fileinfo *props, const struct maxes_t *maxes,
	const int have_xattr)
{
	char file_type = 0; /* File type indicator */
	char *ctype = dn_c; /* Color for file type indicator */

	set_file_type_and_color(props, &file_type, &ctype);
	const int file_perm =
		check_file_access(props->mode, props->uid, props->gid);

	/* Let's compose each properties field individually to be able to
	 * print only the desired ones. This is specified via the PropFields
	 * option in the config file. */

	construct_and_print_filename(props, maxes->name);

	static char perm_str[PERM_STR_LEN]; *perm_str = '\0';
	if (prop_fields.perm != 0)
		construct_file_perms(props->mode, perm_str, file_type, ctype);

	static char time_str[TIME_STR_LEN]; *time_str = '\0';
	if (prop_fields.time != 0)
		construct_timestamp(time_str, props);

	static char size_str[SIZE_STR_LEN]; *size_str = '\0';
	if (prop_fields.size != 0)
		construct_file_size(props, size_str, maxes->size, file_perm);

	static char id_str[ID_STR_LEN]; *id_str = '\0';
	if (prop_fields.ids != 0)
		construct_id_field(props, id_str, maxes, file_perm);

	static char links_str[LINKS_STR_LEN]; *links_str = '\0';
	if (prop_fields.links != 0)
		construct_links_str(props, links_str, maxes->links);

	static char ino_str[INO_STR_LEN]; *ino_str = '\0';
	if (prop_fields.inode != 0)
		construct_inode_num(props, ino_str, maxes->inode);

	static char blocks_str[BLK_STR_LEN]; *blocks_str = '\0';
	if (prop_fields.blocks != 0)
		construct_blocks_str(props, blocks_str, maxes->blocks);

	static char fc_str[FC_STR_LEN]; *fc_str = '\0';
	/* maxes->files_counter is zero if there are no subdirs in the current dir */
	if (prop_fields.counter != 0 && conf.files_counter != 0
	&& maxes->files_counter != 0)
		construct_files_counter(props, fc_str, maxes->files_counter);

	/* We only need a single character to print the xattributes indicator,
	 * which would be normally printed like this:
	 * printf("%c", x ? 'x' : 0);
	 * However, some terminals, like 'cons25', print the 0 above graphically,
	 * as a space, which is not what we want here. To fix this, let's
	 * construct this char as a string. */
	static char xattr_str[2] = {0};
	*xattr_str = have_xattr == 1 ? (props->xattr == 1 ? XATTR_CHAR : ' ') : 0;

	/* Print stuff */
	for (size_t i = 0; i < PROP_FIELDS_SIZE && prop_fields_str[i]; i++) {
		int print_space = prop_fields_str[i + 1] ? 1 : 0;

		switch (prop_fields_str[i]) {
		case 'B': if (*blocks_str) fputs(blocks_str, stdout); break;
		case 'f': fputs(fc_str, stdout); break;
		case 'd': if (*ino_str) fputs(ino_str, stdout); break;
		case 'p': /* fallthrough */
		case 'n': fputs(perm_str, stdout);
			if (*xattr_str) fputs(xattr_str, stdout);
			break;
		case 'i': /* fallthrough */
		case 'I': fputs(id_str, stdout); break;
		case 'l': if (*links_str) fputs(links_str, stdout); break;
		case 'a': /* fallthrough */
		case 'b': /* fallthrough */
		case 'm': /* fallthrough */
		case 'c': fputs(time_str, stdout); break;
		case 's': /* fallthrough */
		case 'S': fputs(size_str, stdout); break;
		default: print_space = 0; break;
		}

		if (print_space == 0)
			continue;

		if (conf.prop_fields_gap <= 1)
			putchar(' ');
		else
			MOVE_CURSOR_RIGHT(conf.prop_fields_gap);
	}
	putchar('\n');

	return FUNC_SUCCESS;
}
