/* colors.c -- functions to control interface colors */

/*
 * This file is part of CliFM
 *
 * Copyright (C) 2016-2023, L. Abramovich <leo.clifm@outlook.com>
 * All rights reserved.

 * CliFM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * CliFM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
*/

#include "helpers.h"

#include <stdio.h>
#ifdef __linux__
# include <sys/capability.h>
#endif
#include <dirent.h>
#include <errno.h>
#include <string.h>

#include "aux.h"
#include "checks.h"
#include "colors.h"
#include "listing.h"
#include "misc.h"
#include "messages.h"
#include "file_operations.h"
#include "exec.h"
#include "config.h" /* set_div_line() */

#ifndef CLIFM_SUCKLESS
/* qsort(3) is used only by get_colorschemes(), which is not included
 * if CLIFM_SUCKLESS is defined */
# include "sort.h" /* compare_string() (used by qsort(3)) */
#endif

#define RL_PRINTABLE    1
#define RL_NO_PRINTABLE 0 /* Add non-printing flags (\001 and \002)*/

/* Tell split_color_line weather we're dealing with interface or file
 * type colors */
#define SPLIT_INTERFACE_COLORS 0
#define SPLIT_FILETYPE_COLORS  1

/* Max amount of custom color variables in the color scheme file */
#define MAX_DEFS 128

/* Macros for the set_shades function */
#define DATE_SHADES 0
#define SIZE_SHADES 1

#ifndef CLIFM_SUCKLESS
/* A struct to hold color variables */
struct defs_t {
	char *name;
	char *value;
};

static struct defs_t *defs;
static size_t defs_n = 0;
#endif /* !CLIFM_SUCKLESS */

/* Turn the first or second field of a color code sequence, provided
 * it is either 1 or 01 (bold attribute), into 0 (regular)
 * It returns no value: the change is made in place via a pointer
 * STR must be a color code with this form: \x1b[xx;xx;xx...
 * It cannot handle the bold attribute beyond the second field
 * Though this is usually enough, it's far from ideal
 *
 * This function is used to print properties strings ('p' command
 * and long view mode). It takes the user defined color of the
 * corresponding file type (e.g. dirs) and removes the bold attribute */
void
remove_bold_attr(char **str)
{
	if (!*str || !*(*str))
		return;

	char *p = *str, *q = *str;
	size_t c = 0;

	while (1) {
		if (*p == '\\' && *(p + 1) == 'x'
		&& *(p + 2) == '1' && *(p + 3) == 'b') {
			if (*(p + 4)) {
				p += 4;
				q = p;
				continue;
			} else {
				break;
			}
		}

		if (*p == '[') {
			p++;
			q = p;
			continue;
		}

		/* Skip leading "0;" or "00;" */
		if (*p == '0' && (p[1] == ';' || (p[1] == '0' && p[2] == ';'))) {
			p += p[1] == ';' ? 2 : 3;
			q = p;
		}

		if ( (*q == '0' && *(q + 1) == '1' && (*(q + 2) == ';'
		|| *(q + 2) == 'm')) || (*q == '1' && (*(q + 1) == 'm'
		|| *(q + 1) == ';')) ) {
			if (*q == '0')
				*(q + 1) = '0';
			else
				*q = '0';
			break;
		}

		if (*p == ';' && *(p + 1)) {
			q = p + 1;
			c++;
		}

		p++;
		if (!*p || c >= 2)
			break;
	}
}

char *
get_regfile_color(const char *filename, const struct stat *attr)
{
	if (conf.colorize == 0)
		return fi_c;

	if (check_file_access(attr->st_mode, attr->st_uid, attr->st_gid) == 0)
		return nf_c;

	char *color = get_file_color(filename, attr);
	if (color == ee_c || color == ex_c || color == su_c || color == sg_c
	|| color == ca_c)
		return color ? color : fi_c;

	char *ext = check_ext == 1 ? strrchr(filename, '.') : (char *)NULL;
	if (!ext)
		return color ? color : fi_c;

	char *extcolor = get_ext_color(ext);
	ext = (char *)NULL;
	if (!extcolor)
		return color ? color : fi_c;

	snprintf(tmp_color, sizeof(tmp_color), "\x1b[%sm", extcolor); // NOLINT
	color = tmp_color;
	extcolor = (char *)NULL;

	return color ? color : fi_c;
}

/* Retrieve the color corresponding to dir FILENAME with mode MODE
 * If LINKS > 2, we know the directory is populated, so that there's no need
 * to run count_dir(). If COUNT > -1, we already know whether the
 * directory is populatoed or not: use this value for FILES_DIR (do not run
 * count_dir() either). */
char *
get_dir_color(const char *filename, const mode_t mode, const nlink_t links,
	const int count)
{
	char *color = (char *)NULL;
	int sticky = 0;
	int is_oth_w = 0;
	if (mode & S_ISVTX)
		sticky = 1;

	if (mode & S_IWOTH)
		is_oth_w = 1;

	size_t files_dir = count > -1 ? (size_t)count : (links > 2 ? (size_t)links
		: (size_t)count_dir(filename, CPOP));

	color = sticky ? (is_oth_w ? tw_c : st_c) : is_oth_w ? ow_c
		   : ((files_dir == 2 || files_dir == 0) ? ed_c : di_c);

	return color;
}

char *
get_file_color(const char *filename, const struct stat *attr)
{
	char *color = (char *)NULL;

#ifdef _LINUX_CAP
	cap_t cap;
#else
	UNUSED(filename);
#endif
	if (attr->st_mode & 04000) { /* SUID */
		color = su_c;
	} else if (attr->st_mode & 02000) { /* SGID */
		color = sg_c;
	}
#ifdef _LINUX_CAP
	else if (check_cap && (cap = cap_get_file(filename))) {
		color = ca_c;
		cap_free(cap);
	}
#endif
	else if ((attr->st_mode & 00100) /* Exec */
	|| (attr->st_mode & 00010) || (attr->st_mode & 00001)) {
		color = FILE_SIZE_PTR == 0 ? ee_c : ex_c;
	} else if (FILE_SIZE_PTR == 0) {
		color = ef_c;
	} else if (attr->st_nlink > 1) { /* Multi-hardlink */
		color = mh_c;
	} else { /* Regular file */
		color = fi_c;
	}

	return color;
}

/* Validate a hex color code string with this format: RRGGBB-[1-9] */
static int
is_hex_color(const char *str)
{
	size_t c = 0;

	while (*str) {
		c++;
		if (c == 7 && *str == '-') {
			if (!*(str + 1))
				return 0;
			return (*(str + 1) >= '0' && *(str + 1) <= '9');
		}
		if ( !( (*str >= '0' && *str <= '9') || (*str >= 'a' && *str <= 'f')
		|| (*str >= 'A' && *str <= 'F') ) )
			return 0;
		str++;
	}

	if (c != 6)
		return 0;

	return 1;
}

/* Check if STR has the format of a color code string (a number or a
 * semicolon list (max 12 fields) of numbers of at most 3 digits each).
 * Hex color codes (#RRGGBB) are also validated
 * Returns 1 if true and 0 if false. */
static int
is_color_code(const char *str)
{
	if (!str || !*str)
		return 0;

	if (*str == '#')
		return is_hex_color(str + 1);

	size_t digits = 0, semicolon = 0;

	while (*str) {
		if (*str >= '0' && *str <= '9') {
			digits++;
		} else if (*str == ';') {
			if (*(str + 1) == ';') /* Consecutive semicolons */
				return 0;
			digits = 0;
			semicolon++;
		} else {
			if (*str != '\n') /* Neither digit nor semicolon */
				return 0;
		}
		str++;
	}

	/* No digits at all, ending semicolon, more than eleven fields, or
	 * more than three consecutive digits */
	if (!digits || digits > 3 || semicolon > 11)
		return 0;

	/* At this point, we have a semicolon separated string of digits (3
	 * consecutive max) with at most 12 fields. The only thing not
	 * validated here are numbers themselves */
	return 1;
}

#ifndef CLIFM_SUCKLESS
/* If STR is a valid color variable name, return the value of this variable */
static char *
check_defs(char *str)
{
	if (defs_n == 0 || !str || !*str)
		return (char *)NULL;

	char *val = (char *)NULL;
	int i = (int)defs_n;

	while (--i >= 0) {
		if (!defs[i].name || !defs[i].value || !*defs[i].name
		|| !*defs[i].value)
			continue;
		if (*defs[i].name == *str && strcmp(defs[i].name, str) == 0
		&& is_color_code(defs[i].value) == 1) {
			val = defs[i].value;
			return val;
		}
	}

	return val;
}

/* Free custom color variables set from the color scheme file */
static void
clear_defs(void)
{
	if (defs_n == 0)
		goto END;

	int i = (int)defs_n;
	while (--i >= 0) {
		free(defs[i].name);
		free(defs[i].value);
	}

	defs_n = 0;
END:
	free(defs);
	defs = (struct defs_t *)NULL;
}
#endif /* CLIFM_SUCKLESS */

/* Look for the hash HASH in the hash table.
 * Return a pointer to the corresponding color if found or NULL
 *
 * NOTE: We check all available hashes to avoid conflicts.
 * The hash table is built on user supplied data (color scheme file)
 * so that we cannot assume in advance that there will be no conflicts.
 * However, at least with the color schemes provided by default, we do know
 * there are no conflicts. In this latter case we can return the first matching
 * entry, which is a performace improvement. But we cannot assume the user
 * is using one of the provided themes and that it has been not modified. */
/*
static char *
check_ext_hash(const size_t hash, size_t *count)
{
	char *p = (char *)NULL;
	size_t i;
	for (i = 0; i < ext_colors_n; i++) {
		if (!ext_colors[i].name || !ext_colors[i].value
		|| hash != ext_colors[i].hash)
			continue;

//		(*count)++;
//		return ext_colors[i].value; // No conflicts check: faster, but might fail

		p = ext_colors[i].value;
		(*count)++;
	}

	return p;
} */

static char *
check_ext_string(const char *ext)
{
	/* Hold extension names. NAME_MAX should be enough: no file name should
	 * go beyond NAME_MAX, so it's pretty safe to assume that no file extension
	 * will be larger */
	static char tmp_ext[NAME_MAX];
	char *ptr = tmp_ext;

	int i;
	for (i = 0; ext[i] && i < NAME_MAX; i++) {
		if (ext[i] >= 'A' && ext[i] <= 'Z')
			tmp_ext[i] = ext[i] + ' ';
		else
		tmp_ext[i] = ext[i];
	}
	tmp_ext[i] = '\0';

	i = (int)ext_colors_n;
	while (--i >= 0) {
		if (!ext_colors[i].name || !*ext_colors[i].name
		|| *ptr != *ext_colors[i].name)
			continue;

		char *p = ptr + 1, *q = ext_colors[i].name + 1;

		size_t match = 1;
		while (*p) {
			if (*p != *q) {
				match = 0;
				break;
			}
			p++;
			q++;
		}

		if (match == 0 || *q != '\0')
			continue;

		return ext_colors[i].value;
	}

	return (char *)NULL;
}

/* Returns a pointer to the corresponding color code for the file
 * extension EXT.
 * The hash table is checked first, and, in case of a conflict, a regular
 * string comparison is performed to resolve it. */
char *
get_ext_color(char *ext)
{
	if (!ext || !*ext || !*(++ext) || ext_colors_n == 0)
		return (char *)NULL;

/*	size_t count = 0;
	char *ret = check_ext_hash(hashme(ext, 0), &count);
	if (count == 0)
		return (char *)NULL;

	if (ret && count == 1)
		return ret; */

	/* We have a conflict: a single hash is used for two or more extensions.
	 * Let's try to match the extension name itself */
	return check_ext_string(ext);
}

#ifndef CLIFM_SUCKLESS
/* Strip color lines from the config file (FiletypeColors, if mode is
 * 't', and ExtColors, if mode is 'x') returning the same string
 * containing only allowed characters */
static char *
strip_color_line(const char *str, char mode)
{
	if (!str || !*str) return (char *)NULL;

	char *buf = (char *)xnmalloc(strlen(str) + 1, sizeof(char));
	size_t len = 0;

	switch (mode) {
	case 't': /* di=01;31: */
		while (*str) {
			if ((*str >= '0' && *str <= '9') || (*str >= 'a' && *str <= 'z')
			|| (*str >= 'A' && *str <= 'Z')
			|| *str == '=' || *str == ';' || *str == ':'
			|| *str == '#' || *str == '-')
				{buf[len] = *str; len++;}
			str++;
		}
		break;

	case 'x': /* *.tar=01;31: */
		while (*str) {
			if ((*str >= '0' && *str <= '9') || (*str >= 'a' && *str <= 'z')
			|| (*str >= 'A' && *str <= 'Z') || *str == '*' || *str == '.'
			|| *str == '=' || *str == ';' || *str == ':'
			|| *str == '#' || *str == '-')
				{buf[len] = *str; len++;}
			str++;
		}
		break;
	default: break;
	}

	if (!len || !*buf)
		{free(buf); return (char *)NULL;}

	buf[len] = '\0';
	return buf;
}
#endif /* !CLIFM_SUCKLESS */

void
reset_filetype_colors(void)
{
	*nd_c = '\0';
	*nf_c = '\0';
	*di_c = '\0';
	*ed_c = '\0';
	*ex_c = '\0';
	*ee_c = '\0';
	*bd_c = '\0';
	*ln_c = '\0';
	*mh_c = '\0';
	*or_c = '\0';
	*so_c = '\0';
	*pi_c = '\0';
	*cd_c = '\0';
	*fi_c = '\0';
	*ef_c = '\0';
	*su_c = '\0';
	*sg_c = '\0';
	*ca_c = '\0';
	*st_c = '\0';
	*tw_c = '\0';
	*ow_c = '\0';
	*no_c = '\0';
	*uf_c = '\0';
}

void
reset_iface_colors(void)
{
	*hb_c = '\0';
	*hc_c = '\0';
	*hd_c = '\0';
	*he_c = '\0';
	*hn_c = '\0';
	*hp_c = '\0';
	*hq_c = '\0';
	*hr_c = '\0';
	*hs_c = '\0';
	*hv_c = '\0';
	*hw_c = '\0';

	*sb_c = '\0';
	*sc_c = '\0';
	*sd_c = '\0';
	*sf_c = '\0';
	*sh_c = '\0';
	*sp_c = '\0';
	*sx_c = '\0';
	*sz_c = '\0';

	*bm_c = '\0';
	*dl_c = '\0';
	*el_c = '\0';
	*mi_c = '\0';
	*tx_c = '\0';
	*df_c = '\0';
	*fc_c = '\0';
	*wc_c = '\0';
	*li_c = '\0';
	*li_cb = '\0';
	*ti_c = '\0';
	*em_c = '\0';
	*wm_c = '\0';
	*nm_c = '\0';
	*si_c = '\0';
	*ts_c = '\0';
	*wp_c = '\0';
	*tt_c = '\0';
	*xs_c = '\0';
	*xf_c = '\0';

	*ws1_c = '\0';
	*ws2_c = '\0';
	*ws3_c = '\0';
	*ws4_c = '\0';
	*ws5_c = '\0';
	*ws6_c = '\0';
	*ws7_c = '\0';
	*ws8_c = '\0';

	*dr_c = '\0';
	*dw_c = '\0';
	*dxd_c = '\0';
	*dxr_c = '\0';
	*dg_c = '\0';
	*dd_c = '\0';
	*dz_c = '\0';
	*do_c = '\0';
	*dp_c = '\0';
	*dn_c = '\0';
}

/* Disable colors for suggestions */
/*
void
unset_suggestions_color(void)
{
	strcpy(sb_c, SUG_NO_COLOR); // shell built-ins
	strcpy(sc_c, SUG_NO_COLOR); // external commands
	strcpy(sd_c, SUG_NO_COLOR); // internal commands description
	strcpy(sf_c, SUG_NO_COLOR); // file names
	strcpy(sh_c, SUG_NO_COLOR); // history
	strcpy(sp_c, SUG_NO_COLOR); // suggestions pointer
	strcpy(sx_c, SUG_NO_COLOR); // internal commands and params
	strcpy(sz_c, SUG_NO_COLOR); // file names (fuzzy)
} */

/* Import the color scheme NAME from DATADIR (usually /usr/local/share)
 * Return zero on success or one on failure */
int
import_color_scheme(const char *name)
{
	if (!data_dir || !*data_dir || !colors_dir || !*colors_dir
	|| !name || !*name)
		return EXIT_FAILURE;

	char dfile[PATH_MAX];
	snprintf(dfile, sizeof(dfile), "%s/%s/colors/%s.clifm", data_dir,
		PNL, name);

	struct stat attr;
	if (stat(dfile, &attr) == -1)
		return EXIT_FAILURE;

	char *cmd[] = {"cp", dfile, colors_dir, NULL};
	if (launch_execve(cmd, FOREGROUND, E_NOFLAG) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	return EXIT_FAILURE;
}

#ifndef CLIFM_SUCKLESS
static int
list_colorschemes(void)
{
	if (cschemes_n == 0) {
		printf(_("cs: No color scheme found\n"));
		return EXIT_SUCCESS;
	}

	size_t i;
	for (i = 0; color_schemes[i]; i++) {
		if (cur_cscheme == color_schemes[i])
			printf("%s>%s %s\n", mi_c, df_c, color_schemes[i]);
		else
			printf("  %s\n", color_schemes[i]);
	}

	return EXIT_SUCCESS;
}

/* Edit the current color scheme file
 * If the file is not in the local colors dir, try to copy it from DATADIR
 * into the local dir to avoid permission issues */
static int
edit_colorscheme(char *app)
{
	if (!colors_dir) {
		fprintf(stderr, _("cs: No color scheme found\n"));
		return EXIT_FAILURE;
	}

	if (!cur_cscheme) {
		fprintf(stderr, _("cs: Current color scheme is unknown\n"));
		return EXIT_FAILURE;
	}

	struct stat attr;
	char file[PATH_MAX];

	snprintf(file, sizeof(file), "%s/%s.clifm", colors_dir, cur_cscheme); /* NOLINT */
	if (stat(file, &attr) == -1
	&& import_color_scheme(cur_cscheme) != EXIT_SUCCESS) {
		fprintf(stderr, _("cs: %s: No such color scheme\n"), cur_cscheme);
		return EXIT_FAILURE;
	}

	stat(file, &attr);
	time_t mtime_bfr = (time_t)attr.st_mtime;

	int ret = EXIT_FAILURE;
	if (app && *app) {
		char *cmd[] = {app, file, NULL};
		ret = launch_execve(cmd, FOREGROUND, E_NOFLAG);
	} else {
		open_in_foreground = 1;
		ret = open_file(file);
		open_in_foreground = 0;
	}

	if (ret != EXIT_SUCCESS)
		return ret;

	stat(file, &attr);
	if (mtime_bfr != (time_t)attr.st_mtime
	&& set_colors(cur_cscheme, 0) == EXIT_SUCCESS && conf.autols == 1) {
		set_fzf_preview_border_type();
		reload_dirlist();
	}

	return ret;
}

static int
set_colorscheme(char *arg)
{
	if (!arg || !*arg)
		return EXIT_FAILURE;

	char *p = dequote_str(arg, 0);
	char *q = p ? p : arg;

	size_t i, cs_found = 0;
	for (i = 0; color_schemes[i]; i++) {
		if (*q != *color_schemes[i]
		|| strcmp(q, color_schemes[i]) != 0)
			continue;
		cs_found = 1;

		if (set_colors(q, 0) != EXIT_SUCCESS)
			continue;
		cur_cscheme = color_schemes[i];

		switch_cscheme = 1;
		if (conf.autols == 1)
			reload_dirlist();
		switch_cscheme = 0;

		free(p);

		return EXIT_SUCCESS;
	}

	if (cs_found == 0)
		fprintf(stderr, _("cs: %s: No such color scheme\n"), p);

	free(p);

	return EXIT_FAILURE;
}
#endif /* CLIFM_SUCKLESS */

int
cschemes_function(char **args)
{
#ifdef CLIFM_SUCKLESS
	UNUSED(args);
	printf("%s: colors: %s. Edit settings.h in the source code "
		"and recompile\n", PROGRAM_NAME, NOT_AVAILABLE);
	return EXIT_FAILURE;
#else
	if (xargs.stealth_mode == 1) {
		fprintf(stderr, _("%s: colors: %s\nTIP: To change the "
			"current color scheme use the following environment "
			"variables: CLIFM_FILE_COLORS, CLIFM_IFACE_COLORS, "
			"and CLIFM_EXT_COLORS\n"), PROGRAM_NAME, STEALTH_DISABLED);
		return EXIT_FAILURE;
	}

	if (conf.colorize == 0) {
		printf("%s: Colors are disabled\n", PROGRAM_NAME);
		return EXIT_FAILURE;
	}

	if (!args) return EXIT_FAILURE;

	if (!args[1]) return list_colorschemes();

	if (IS_HELP(args[1])) { puts(_(CS_USAGE)); return EXIT_SUCCESS;	}

	if (*args[1] == 'e' && (!args[1][1] || strcmp(args[1], "edit") == 0))
		return edit_colorscheme(args[2]);

	if (*args[1] == 'n' && (!args[1][1] || strcmp(args[1], "name") == 0)) {
		printf(_("cs: Current color scheme is '%s'\n"),
			cur_cscheme ? cur_cscheme : "?");
		return EXIT_SUCCESS;
	}

	return set_colorscheme(args[1]);
#endif /* CLIFM_SUCKLESS */
}

/* Set color variable VAR (static global) to _COLOR.
 * If not printable, add non-printing char flags (\001 and \002) */
static void
set_color(char *_color, char var[], const int flag)
{
#ifndef CLIFM_SUCKLESS
	char *color_code = (char *)NULL;
	if (is_color_code(_color) == 0
	&& (color_code = check_defs(_color)) == NULL)
#else
	if (is_color_code(_color) == 0)
#endif /* !CLIFM_SUCKLESS */
	{
		/* A null color string will be set to the default value by
		 * set_default_colors function */
		*var = '\0';
		return;
	}

#ifndef CLIFM_SUCKLESS
	char *p = color_code ? color_code : _color;
#else
	char *p = _color;
#endif /* !CLIFM_SUCKLESS */

	char *s = (char *)NULL;
	if (*p == '#') {
		if (!(s = hex2rgb(p))) {
			*var = '\0';
			return;
		}
	}

	if (flag == RL_NO_PRINTABLE)
		snprintf(var, MAX_COLOR + 2, "\001\x1b[%sm\002", s ? s : p); /* NOLINT */
	else
		snprintf(var, MAX_COLOR - 1, "\x1b[00;%sm", s ? s : p); /* NOLINT */
}

static void
set_filetype_colors(char **colors, const size_t words)
{
	int i = (int)words;
	while (--i >= 0) {
		if (!*colors[i] || !colors[i][1] || colors[i][2] != '=') {
			free(colors[i]);
			continue;
		}

		if (*colors[i] == 'b' && colors[i][1] == 'd')
			set_color(colors[i] + 3, bd_c, RL_PRINTABLE);

		else if (*colors[i] == 'c') {
			if (colors[i][1] == 'a')
				set_color(colors[i] + 3, ca_c, RL_PRINTABLE);
			else if (colors[i][1] == 'd')
				set_color(colors[i] + 3, cd_c, RL_PRINTABLE);
		}

		else if (*colors[i] == 'd' && colors[i][1] == 'i')
			set_color(colors[i] + 3, di_c, RL_PRINTABLE);

		else if (*colors[i] == 'e') {
			if (colors[i][1] == 'd')
				set_color(colors[i] + 3, ed_c, RL_PRINTABLE);
			else if (colors[i][1] == 'e')
				set_color(colors[i] + 3, ee_c, RL_PRINTABLE);
			else if (colors[i][1] == 'f')
				set_color(colors[i] + 3, ef_c, RL_PRINTABLE);
			else if (colors[i][1] == 'x')
				set_color(colors[i] + 3, ex_c, RL_PRINTABLE);
		}

		else if (*colors[i] == 'f' && colors[i][1] == 'i')
			set_color(colors[i] + 3, fi_c, RL_PRINTABLE);

		else if (*colors[i] == 'l' && colors[i][1] == 'n')
			set_color(colors[i] + 3, ln_c, RL_PRINTABLE);

		else if (*colors[i] == 'm' && colors[i][1] == 'h')
			set_color(colors[i] + 3, mh_c, RL_PRINTABLE);

		else if (*colors[i] == 'n') {
			if (colors[i][1] == 'd')
				set_color(colors[i] + 3, nd_c, RL_PRINTABLE);
			else if (colors[i][1] == 'f')
				set_color(colors[i] + 3, nf_c, RL_PRINTABLE);
			else if (colors[i][1] == 'o')
				set_color(colors[i] + 3, no_c, RL_PRINTABLE);
		}

		else if (*colors[i] == 'o') {
			if (colors[i][1] == 'r')
				set_color(colors[i] + 3, or_c, RL_PRINTABLE);
			else if (colors[i][1] == 'w')
				set_color(colors[i] + 3, ow_c, RL_PRINTABLE);
		}

		else if (*colors[i] == 'p' && colors[i][1] == 'i')
			set_color(colors[i] + 3, pi_c, RL_PRINTABLE);

		else if (*colors[i] == 's') {
			if (colors[i][1] == 'g')
				set_color(colors[i] + 3, sg_c, RL_PRINTABLE);
			else if (colors[i][1] == 'o')
				set_color(colors[i] + 3, so_c, RL_PRINTABLE);
			else if (colors[i][1] == 't')
				set_color(colors[i] + 3, st_c, RL_PRINTABLE);
			else if (colors[i][1] == 'u')
				set_color(colors[i] + 3, su_c, RL_PRINTABLE);
		}

		else if (*colors[i] == 't' && colors[i][1] == 'w')
			set_color(colors[i] + 3, tw_c, RL_PRINTABLE);

		else if (*colors[i] == 'u' && colors[i][1] == 'f')
			set_color(colors[i] + 3, uf_c, RL_PRINTABLE);

		free(colors[i]);
	}

	free(colors);
}

static void
set_iface_colors(char **colors, const size_t words)
{
	int i = (int)words;
	while (--i >= 0) {
		if (*colors[i] == 'b' && colors[i][1] == 'm' && colors[i][2] == '=')
			set_color(colors[i] + 3, bm_c, RL_PRINTABLE);

		else if (*colors[i] == 'd') {
			if (colors[i][1] == 'd' && colors[i][2] == '=')
				set_color(colors[i] + 3, dd_c, RL_PRINTABLE);
			else if (colors[i][1] == 'f' && colors[i][2] == '=')
				set_color(colors[i] + 3, df_c, RL_PRINTABLE);
			else if (colors[i][1] == 'g' && colors[i][2] == '=')
				set_color(colors[i] + 3, dg_c, RL_PRINTABLE);
			else if (colors[i][1] == 'l' && colors[i][2] == '=')
				set_color(colors[i] + 3, dl_c, RL_PRINTABLE);
			else if (colors[i][1] == 'n' && colors[i][2] == '=')
				set_color(colors[i] + 3, dn_c, RL_PRINTABLE);
			else if (colors[i][1] == 'o' && colors[i][2] == '=')
				set_color(colors[i] + 3, do_c, RL_PRINTABLE);
			else if (colors[i][1] == 'p' && colors[i][2] == '=')
				set_color(colors[i] + 3, dp_c, RL_PRINTABLE);
			else if (colors[i][1] == 'r' && colors[i][2] == '=')
				set_color(colors[i] + 3, dr_c, RL_PRINTABLE);
			else if (colors[i][1] == 'w' && colors[i][2] == '=')
				set_color(colors[i] + 3, dw_c, RL_PRINTABLE);
			else if (colors[i][1] == 'x' && colors[i][2] == 'd'
			&& colors[i][3] == '=')
				set_color(colors[i] + 4, dxd_c, RL_PRINTABLE);
			else if (colors[i][1] == 'x' && colors[i][2] == 'r'
			&& colors[i][3] == '=')
				set_color(colors[i] + 4, dxr_c, RL_PRINTABLE);
			else if (colors[i][1] == 'z' && colors[i][2] == '=')
				set_color(colors[i] + 3, dz_c, RL_PRINTABLE);
		}

		else if (*colors[i] == 'e') {
			if (colors[i][1] == 'l' && colors[i][2] == '=')
				set_color(colors[i] + 3, el_c, RL_PRINTABLE);
			else if (colors[i][1] == 'm' && colors[i][2] == '=')
				set_color(colors[i] + 3, em_c, RL_NO_PRINTABLE);
		}

		else if (*colors[i] == 'f' && colors[i][1] == 'c'
		&& colors[i][2] == '=')
			set_color(colors[i] + 3, fc_c, RL_PRINTABLE);

		else if (*colors[i] == 'h') {
			if (colors[i][1] == 'b' && colors[i][2] == '=')
				set_color(colors[i] + 3, hb_c, RL_PRINTABLE);
			else if (colors[i][1] == 'c' && colors[i][2] == '=')
				set_color(colors[i] + 3, hc_c, RL_PRINTABLE);
			else if (colors[i][1] == 'd' && colors[i][2] == '=')
				set_color(colors[i] + 3, hd_c, RL_PRINTABLE);
			else if (colors[i][1] == 'e' && colors[i][2] == '=')
				set_color(colors[i] + 3, he_c, RL_PRINTABLE);
			else if (colors[i][1] == 'n' && colors[i][2] == '=')
				set_color(colors[i] + 3, hn_c, RL_PRINTABLE);
			else if (colors[i][1] == 'p' && colors[i][2] == '=')
				set_color(colors[i] + 3, hp_c, RL_PRINTABLE);
			else if (colors[i][1] == 'q' && colors[i][2] == '=')
				set_color(colors[i] + 3, hq_c, RL_PRINTABLE);
			else if (colors[i][1] == 'r' && colors[i][2] == '=')
				set_color(colors[i] + 3, hr_c, RL_PRINTABLE);
			else if (colors[i][1] == 's' && colors[i][2] == '=')
				set_color(colors[i] + 3, hs_c, RL_PRINTABLE);
			else if (colors[i][1] == 'v' && colors[i][2] == '=')
				set_color(colors[i] + 3, hv_c, RL_PRINTABLE);
		}

		else if (*colors[i] == 'l' && colors[i][1] == 'i'
		&& colors[i][2] == '=') {
			set_color(colors[i] + 3, li_c, RL_NO_PRINTABLE);
			set_color(colors[i] + 3, li_cb, RL_PRINTABLE);
		}

		else if (*colors[i] == 'm' && colors[i][1] == 'i'
		&& colors[i][2] == '=')
			set_color(colors[i] + 3, mi_c, RL_PRINTABLE);

		else if (*colors[i] == 'n' && colors[i][1] == 'm'
		&& colors[i][2] == '=')
			set_color(colors[i] + 3, nm_c, RL_NO_PRINTABLE);

		else if (*colors[i] == 's') {
			if (colors[i][1] == 'b' && colors[i][2] == '=')
				set_color(colors[i] + 3, sb_c, RL_PRINTABLE);
			else if (colors[i][1] == 'c' && colors[i][2] == '=')
				set_color(colors[i] + 3, sc_c, RL_PRINTABLE);
			else if (colors[i][1] == 'd' && colors[i][2] == '=')
				set_color(colors[i] + 3, sd_c, RL_PRINTABLE);
			else if (colors[i][1] == 'h' && colors[i][2] == '=')
				set_color(colors[i] + 3, sh_c, RL_PRINTABLE);
			else if (colors[i][1] == 'i' && colors[i][2] == '=')
				set_color(colors[i] + 3, si_c, RL_NO_PRINTABLE);
			else if (colors[i][1] == 'f' && colors[i][2] == '=')
				set_color(colors[i] + 3, sf_c, RL_PRINTABLE);
			else if (colors[i][1] == 'p' && colors[i][2] == '=')
				set_color(colors[i] + 3, sp_c, RL_PRINTABLE);
			else if (colors[i][1] == 'x' && colors[i][2] == '=')
				set_color(colors[i] + 3, sx_c, RL_PRINTABLE);
			else if (colors[i][1] == 'z' && colors[i][2] == '=')
				set_color(colors[i] + 3, sz_c, RL_PRINTABLE);
		}

		else if (*colors[i] == 't') {
			if (colors[i][1] == 'i' && colors[i][2] == '=')
				set_color(colors[i] + 3, ti_c, RL_NO_PRINTABLE);
			else if (colors[i][1] == 's' && colors[i][2] == '=')
				set_color(colors[i] + 3, ts_c, RL_PRINTABLE);
			else if (colors[i][1] == 't' && colors[i][2] == '=')
				set_color(colors[i] + 3, tt_c, RL_PRINTABLE);
			else if (colors[i][1] == 'x' && colors[i][2] == '=')
				set_color(colors[i] + 3, tx_c, RL_PRINTABLE);
		}

		else if (*colors[i] == 'w') {
			if (colors[i][1] == 'c' && colors[i][2] == '=')
				set_color(colors[i] + 3, wc_c, RL_PRINTABLE);
			else if (colors[i][1] == 'm' && colors[i][2] == '=')
				set_color(colors[i] + 3, wm_c, RL_NO_PRINTABLE);
			else if (colors[i][1] == 'p' && colors[i][2] == '=')
				set_color(colors[i] + 3, wp_c, RL_PRINTABLE);

			else if (colors[i][1] == 's' && colors[i][2]
			&& colors[i][3] == '=') {
				if (colors[i][2] == '1')
					set_color(colors[i] + 4, ws1_c, RL_NO_PRINTABLE);
				else if (colors[i][2] == '2')
					set_color(colors[i] + 4, ws2_c, RL_NO_PRINTABLE);
				else if (colors[i][2] == '3')
					set_color(colors[i] + 4, ws3_c, RL_NO_PRINTABLE);
				else if (colors[i][2] == '4')
					set_color(colors[i] + 4, ws4_c, RL_NO_PRINTABLE);
				else if (colors[i][2] == '5')
					set_color(colors[i] + 4, ws5_c, RL_NO_PRINTABLE);
				else if (colors[i][2] == '6')
					set_color(colors[i] + 4, ws6_c, RL_NO_PRINTABLE);
				else if (colors[i][2] == '7')
					set_color(colors[i] + 4, ws7_c, RL_NO_PRINTABLE);
				else if (colors[i][2] == '8')
					set_color(colors[i] + 4, ws8_c, RL_NO_PRINTABLE);
			}
		}

		else if (*colors[i] == 'x') {
			if (colors[i][1] == 's' && colors[i][2] == '=')
				set_color(colors[i] + 3, xs_c, RL_NO_PRINTABLE);
			else if (colors[i][1] == 'f' && colors[i][2] == '=')
				set_color(colors[i] + 3, xf_c, RL_NO_PRINTABLE);
		}

		free(colors[i]);
	}

	free(colors);
}

static void
set_shades(char *line, const int type)
{
	char *l = remove_quotes(line);
	if (!l || !*l)
		return;

	char *str = strtok(l, ",");
	if (!str || !*str)
		return;

	int t = *str - '0';
	if (t < 0 || t > 3)
		return;

	if (type == DATE_SHADES)
		date_shades.type = (uint8_t)t;
	else
		size_shades.type = (uint8_t)t;

	int c = 0;
	while ((str = strtok(NULL, ",")) && c < NUM_SHADES) {
		if (*str == '#') {
			if (!*(str + 1) || t != SHADE_TYPE_TRUECOLOR)
				goto NEXT;

			int a = 0, r = 0, g = 0, b = 0;

			if (get_rgb(str + 1, &a, &r, &g, &b) == -1)
				goto NEXT;

			if (type == DATE_SHADES) {
				date_shades.shades[c].attr = (uint8_t)a;
				date_shades.shades[c].R = (uint8_t)r;
				date_shades.shades[c].G = (uint8_t)g;
				date_shades.shades[c].B = (uint8_t)b;
			} else {
				size_shades.shades[c].attr = (uint8_t)a;
				size_shades.shades[c].R = (uint8_t)r;
				size_shades.shades[c].G = (uint8_t)g;
				size_shades.shades[c].B = (uint8_t)b;
			}

			goto NEXT;
		}

		if (t == SHADE_TYPE_TRUECOLOR)
			goto NEXT;

		char *p = strchr(str, '-');
		uint8_t color_attr = 0;

		if (p) {
			*p = '\0';
			if (*(p + 1) && *(p + 1) >= '0' && *(p + 1) <= '9' && !*(p + 2))
				color_attr = (uint8_t)(*(p + 1) - '0');
		}

		int n = atoi(str);
		if (n < 0 || n > 255)
			goto NEXT;

		if (type == DATE_SHADES) {
			date_shades.shades[c].attr = color_attr;
			date_shades.shades[c].R = (uint8_t)n;
		} else {
			size_shades.shades[c].attr = color_attr;
			size_shades.shades[c].R = (uint8_t)n;
		}

NEXT:
		c++;
	}
}

static void
set_default_date_shades(void)
{
	char tmp[NAME_MAX];
	snprintf(tmp, sizeof(tmp), "%s", term_caps.color >= 256
		? DEF_DATE_SHADES_256 : DEF_DATE_SHADES_8);
	set_shades(tmp, DATE_SHADES);
}

static void
set_default_size_shades(void)
{
	char tmp[NAME_MAX];
	snprintf(tmp, sizeof(tmp), "%s", term_caps.color >= 256
		? DEF_SIZE_SHADES_256 : DEF_SIZE_SHADES_8);
	set_shades(tmp, SIZE_SHADES);
}

/* Check if LINE contains a valid color code, and store it into the
 * ext_colors global array
 * If LINE contains a color variable, expand it, check it, and store it */
static int
store_extension_line(char *line)
{
	/* Remove the leading "*." from the extension line */
	if (!line || *line != '*' || *(line + 1) != '.' || !*(line + 2))
		return EXIT_FAILURE;
	line += 2;

	char *q = strchr(line, '=');
	if (!q || !*(q + 1) || q == line)
		return EXIT_FAILURE;

	*q = '\0';

	char *def = (char *)NULL;
#ifndef CLIFM_SUCKLESS
	if (is_color_code(q + 1) == 0 && (def = check_defs(q + 1)) == NULL)
#else
	if (is_color_code(q + 1) == 0)
#endif /* !CLIFM_SUCKLESS */
		return EXIT_FAILURE;

	char *tmp = (def && *def) ? def : q + 1;
	char *code = *tmp == '#' ? hex2rgb(tmp) : tmp;
	if (!code || !*code)
		return EXIT_FAILURE;

	ext_colors = (struct ext_t *)xrealloc(ext_colors,
		(ext_colors_n + 1) * sizeof(struct ext_t));

	ext_colors[ext_colors_n].name = savestring(line, (size_t)(q - line));
	ext_colors[ext_colors_n].value =
		(char *)xnmalloc(strlen(code) + 3, sizeof(char));
	sprintf(ext_colors[ext_colors_n].value, "0;%s", code);
//	ext_color[ext_colors_n].hash = hashme(line, 0);

	*q = '=';
	ext_colors_n++;

	return EXIT_SUCCESS;
}

static void
free_extension_colors(void)
{
	int i = (int)ext_colors_n;
	while (--i >= 0) {
		free(ext_colors[i].name);
		free(ext_colors[i].value);
	}
	free(ext_colors);
	ext_colors = (struct ext_t *)NULL;
	ext_colors_n = 0;
}

static void
split_extension_colors(char *extcolors)
{
	char *p = extcolors, *buf = (char *)NULL;
	size_t len = 0;
	int eol = 0;

	free_extension_colors();

	while (eol == 0) {
		switch (*p) {

		case '\0': /* fallthrough */
		case '\n': /* fallthrough */
		case ':':
			if (!buf || !*buf) {
				eol = 1;
				break;
			}
			buf[len] = '\0';
			if (store_extension_line(buf) == EXIT_SUCCESS)
				*buf = '\0';

			if (!*p)
				eol = 1;
			len = 0;
			p++;

			break;

		default:
			buf = (char *)xrealloc(buf, (len + 2) * sizeof(char));
			buf[len] = *p;
			len++;
			p++;
			break;
		}
	}

	p = (char *)NULL;

	free(buf);

	if (ext_colors) {
		ext_colors = (struct ext_t *)xrealloc(ext_colors,
			(ext_colors_n + 1) * sizeof(struct ext_t));
		ext_colors[ext_colors_n].name = (char *)NULL;
		ext_colors[ext_colors_n].value = (char *)NULL;
	}
}

void
set_default_colors(void)
{
	if (size_shades.type == SHADE_TYPE_UNSET)
		set_default_size_shades();

	if (date_shades.type == SHADE_TYPE_UNSET)
		set_default_date_shades();

	if (!ext_colors)
		split_extension_colors(DEF_EXT_COLORS);

	if (!*hb_c) strcpy(hb_c, DEF_HB_C);
	if (!*hc_c) strcpy(hc_c, DEF_HC_C);
	if (!*hd_c) strcpy(hd_c, DEF_HD_C);
	if (!*he_c) strcpy(he_c, DEF_HE_C);
	if (!*hn_c) strcpy(hn_c, DEF_HN_C);
	if (!*hp_c) strcpy(hp_c, DEF_HP_C);
	if (!*hq_c) strcpy(hq_c, DEF_HQ_C);
	if (!*hr_c) strcpy(hr_c, DEF_HR_C);
	if (!*hs_c) strcpy(hs_c, DEF_HS_C);
	if (!*hv_c) strcpy(hv_c, DEF_HV_C);
//	if (!*hw_c) strcpy(hw_c, DEF_HW_C);
	if (!*tt_c) strcpy(tt_c, DEF_TT_C);

	if (!*sb_c) strcpy(sb_c, DEF_SB_C);
	if (!*sc_c) strcpy(sc_c, DEF_SC_C);
	if (!*sd_c) strcpy(sd_c, DEF_SD_C);
	if (!*sh_c) strcpy(sh_c, DEF_SH_C);
	if (!*sf_c) strcpy(sf_c, DEF_SF_C);
	if (!*sx_c) strcpy(sx_c, DEF_SX_C);
	if (!*sp_c) strcpy(sp_c, DEF_SP_C);
	if (!*sz_c) strcpy(sz_c, DEF_SZ_C);

	if (!*el_c) strcpy(el_c, DEF_EL_C);
	if (!*mi_c) strcpy(mi_c, DEF_MI_C);
	/* If unset from the config file, use current workspace color */
	if (!*dl_c && config_ok == 0) strcpy(dl_c, DEF_DL_C);

	if (!*df_c) strcpy(df_c, DEF_DF_C);
	if (!*fc_c) strcpy(fc_c, DEF_FC_C);
	if (!*wc_c) strcpy(wc_c, DEF_WC_C);
	if (!*tx_c) strcpy(tx_c, DEF_TX_C);
	if (!*li_c) strcpy(li_c, DEF_LI_C);
	if (!*li_cb) strcpy(li_cb, DEF_LI_CB);
	if (!*ti_c) strcpy(ti_c, DEF_TI_C);
	if (!*em_c) strcpy(em_c, DEF_EM_C);
	if (!*wm_c) strcpy(wm_c, DEF_WM_C);
	if (!*nm_c) strcpy(nm_c, DEF_NM_C);
	if (!*si_c) strcpy(si_c, DEF_SI_C);
	if (!*bm_c) strcpy(bm_c, DEF_BM_C);
	if (!*ts_c) strcpy(ts_c, DEF_TS_C);
	if (!*wp_c) strcpy(wp_c, DEF_WP_C);
	if (!*xs_c) strcpy(xs_c, DEF_XS_C);
	if (!*xf_c) strcpy(xf_c, DEF_XF_C);

	if (!*hw_c) strcpy(hw_c, wp_c);

	if (!*ws1_c) strcpy(ws1_c, DEF_WS1_C);
	if (!*ws2_c) strcpy(ws2_c, DEF_WS2_C);
	if (!*ws3_c) strcpy(ws3_c, DEF_WS3_C);
	if (!*ws4_c) strcpy(ws4_c, DEF_WS4_C);
	if (!*ws5_c) strcpy(ws5_c, DEF_WS5_C);
	if (!*ws6_c) strcpy(ws6_c, DEF_WS6_C);
	if (!*ws7_c) strcpy(ws7_c, DEF_WS7_C);
	if (!*ws8_c) strcpy(ws8_c, DEF_WS8_C);

	if (!*di_c) strcpy(di_c, DEF_DI_C);
	if (!*nd_c) strcpy(nd_c, DEF_ND_C);
	if (!*ed_c) strcpy(ed_c, DEF_ED_C);
	if (!*fi_c) strcpy(fi_c, DEF_FI_C);
	if (!*ef_c) strcpy(ef_c, DEF_EF_C);
	if (!*nf_c) strcpy(nf_c, DEF_NF_C);
	if (!*ln_c) strcpy(ln_c, DEF_LN_C);
	if (!*or_c) strcpy(or_c, DEF_OR_C);
	if (!*pi_c) strcpy(pi_c, DEF_PI_C);
	if (!*so_c) strcpy(so_c, DEF_SO_C);
	if (!*bd_c) strcpy(bd_c, DEF_BD_C);
	if (!*cd_c) strcpy(cd_c, DEF_CD_C);
	if (!*su_c) strcpy(su_c, DEF_SU_C);
	if (!*sg_c) strcpy(sg_c, DEF_SG_C);
	if (!*st_c) strcpy(st_c, DEF_ST_C);
	if (!*tw_c) strcpy(tw_c, DEF_TW_C);
	if (!*ow_c) strcpy(ow_c, DEF_OW_C);
	if (!*ex_c) strcpy(ex_c, DEF_EX_C);
	if (!*ee_c) strcpy(ee_c, DEF_EE_C);
	if (!*ca_c) strcpy(ca_c, DEF_CA_C);
	if (!*no_c) strcpy(no_c, DEF_NO_C);
	if (!*uf_c) strcpy(uf_c, DEF_UF_C);
	if (!*mh_c) strcpy(mh_c, DEF_MH_C);
#ifndef _NO_ICONS
	if (!*dir_ico_c) strcpy(dir_ico_c, DEF_DIR_ICO_C);
#endif

	if (!*dr_c) strcpy(dr_c, term_caps.color >= 256 ? DEF_DR_C256 : DEF_DR_C);
	if (!*dw_c) strcpy(dw_c, term_caps.color >= 256 ? DEF_DW_C256 : DEF_DW_C);
	if (!*dxd_c) strcpy(dxd_c, term_caps.color >= 256 ? DEF_DXD_C256 : DEF_DXD_C);
	if (!*dxr_c) strcpy(dxr_c, term_caps.color >= 256 ? DEF_DXR_C256 : DEF_DXR_C);
	if (!*dg_c) strcpy(dg_c, term_caps.color >= 256 ? DEF_DG_C256 : DEF_DG_C);
//	if (!*dd_c) strcpy(dd_c, DEF_DD_C); // Date color unset: let's use gradient
//	if (!*dz_c) strcpy(dz_c, DEF_DZ_C); // Size color unset: let's use gradient
	if (!*do_c) strcpy(do_c, term_caps.color >= 256 ? DEF_DO_C256 : DEF_DO_C);
	if (!*dp_c) strcpy(dp_c, term_caps.color >= 256 ? DEF_DP_C256 : DEF_DP_C);
	if (!*dn_c) strcpy(dn_c, DEF_DN_C);
}

/* Set a pointer to the current color scheme */
static int
get_cur_colorscheme(const char *colorscheme)
{
	char *def_cscheme = (char *)NULL;
	int i = (int)cschemes_n;

	while (--i >= 0) {
		if (*colorscheme == *color_schemes[i]
		&& strcmp(colorscheme, color_schemes[i]) == 0) {
			cur_cscheme = color_schemes[i];
			break;
		}

		if (strcmp(color_schemes[i], term_caps.color < 256 ? DEF_COLOR_SCHEME
		: DEF_COLOR_SCHEME_256) == 0)
			def_cscheme = color_schemes[i];
	}

	if (!cur_cscheme) {
		_err('w', PRINT_PROMPT, _("%s: colors: %s: No such color scheme. "
			"Falling back to default\n"), PROGRAM_NAME, colorscheme);

		if (def_cscheme)
			cur_cscheme = def_cscheme;
		else
			return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/* Try to retrieve colors from the environment */
static void
get_colors_from_env(char **file, char **ext, char **iface)
{
	char *env_filecolors = getenv("CLIFM_FILE_COLORS");
	char *env_extcolors = getenv("CLIFM_EXT_COLORS");
	char *env_ifacecolors = getenv("CLIFM_IFACE_COLORS");
	char *env_date_shades = getenv("CLIFM_DATE_SHADES");
	char *env_size_shades = getenv("CLIFM_SIZE_SHADES");

	if (env_date_shades)
		set_shades(env_date_shades, DATE_SHADES);

	if (env_size_shades)
		set_shades(env_size_shades, SIZE_SHADES);

	if (env_filecolors)
		*file = savestring(env_filecolors, strlen(env_filecolors));

	if (env_extcolors)
		*ext = savestring(env_extcolors, strlen(env_extcolors));

	if (env_ifacecolors)
		*iface = savestring(env_ifacecolors, strlen(env_ifacecolors));
}

#ifndef CLIFM_SUCKLESS
/* Store color variable defined in STR into the global defs struct */
static void
store_definition(char *str)
{
	if (!str || !*str || *str == '\n' || defs_n > MAX_DEFS)
		return;

	char *name = str;
	char *value = strchr(name, '=');
	if (!value || !*(value + 1) || value == name)
		return;

	*value = '\0';
	value++;

	defs[defs_n].name = savestring(name, (size_t)(value - name - 1));

	size_t val_len;
	char *s = strchr(value, ' ');
	if (s) {
		*s = '\0';
		val_len = (size_t)(s - value);
	} else {
		val_len = strlen(value);
		if (val_len > 0 && value[val_len - 1] == '\n') {
			value[val_len - 1] = '\0';
			val_len--;
		}
	}

	defs[defs_n].value = savestring(value, val_len);
	defs_n++;
}

/* Initialize the defs_t struct */
static void
init_defs(void)
{
	int n = MAX_DEFS;
	while (--n >= 0) {
		defs[n].name = (char *)NULL;
		defs[n].value = (char *)NULL;
	}
}

#ifndef _NO_FZF
static void
set_fzf_opts(char *line)
{
	free(conf.fzftab_options);

	if (!line) {
		char *p = conf.colorize == 1 ? DEF_FZFTAB_OPTIONS
			: DEF_FZFTAB_OPTIONS_NO_COLOR;
		conf.fzftab_options = savestring(p, strlen(p));
	}

	else if (*line == 'n' && strcmp(line, "none") == 0) {
		conf.fzftab_options = (char *)xnmalloc(1, sizeof(char));
		*conf.fzftab_options = '\0';
	}

	else if (sanitize_cmd(line, SNT_BLACKLIST) == EXIT_SUCCESS) {
		conf.fzftab_options = savestring(line, strlen(line));
	}

	else {
		_err('w', PRINT_PROMPT, _("%s: FzfTabOptions contains unsafe "
			"characters (<>|;&$`). Falling back to default values.\n"),
			PROGRAM_NAME);
		char *p = conf.colorize == 1 ? DEF_FZFTAB_OPTIONS
			: DEF_FZFTAB_OPTIONS_NO_COLOR;
		conf.fzftab_options = savestring(p, strlen(p));
	}

	fzf_height_set = 0;
	if (strstr(conf.fzftab_options, "--height"))
		fzf_height_set = 1;
}
#endif /* !_NO_FZF */

/* Get color lines from the configuration file */
static int
read_color_scheme_file(const char *colorscheme, char **filecolors,
			char **extcolors, char **ifacecolors, const int env)
{
	/* Allocate some memory for custom color variables */
	defs = (struct defs_t *)xnmalloc(MAX_DEFS + 1, sizeof(struct defs_t));
	defs_n = 0;
	init_defs();

	char colorscheme_file[PATH_MAX];
	*colorscheme_file = '\0';
	if (config_ok == 1 && colors_dir) {
		snprintf(colorscheme_file, sizeof(colorscheme_file), "%s/%s.clifm",
			colors_dir, colorscheme ? colorscheme : "default"); /* NOLINT */
	}

	/* If not in local dir, check system data dir as well */
	struct stat attr;
	if (data_dir && (!*colorscheme_file
	|| stat(colorscheme_file, &attr) == -1)) {
		snprintf(colorscheme_file, sizeof(colorscheme_file),
			"%s/%s/colors/%s.clifm", data_dir, PNL, colorscheme
			? colorscheme : "default");
	}

	FILE *fp_colors = fopen(colorscheme_file, "r");
	if (!fp_colors) {
		if (!env) {
			_err(ERR_NO_STORE, NOPRINT_PROMPT, "%s: colors: %s: %s\n",
				PROGRAM_NAME, colorscheme_file, strerror(errno));
			return EXIT_FAILURE;
		} else {
			_err('w', PRINT_PROMPT, _("%s: colors: %s: No such color scheme. "
				"Falling back to default\n"), PROGRAM_NAME, colorscheme);
			return EXIT_SUCCESS;
		}
	}

	/* If called from the color scheme function, reset all color values
	 * before proceeding */
	if (!env) {
		reset_filetype_colors();
		reset_iface_colors();
	}

	char *line = (char *)NULL;
	size_t line_size = 0;
	ssize_t line_len = 0;

	while ((line_len = getline(&line, &line_size, fp_colors)) > 0) {
		if (*line == '#' || *line == '\n')
			continue;

		if (*line == 'd' && strncmp(line, "define ", 7) == 0) {
			store_definition(line + 7);
		}

		else if (*line == 'P' && strncmp(line, "Prompt=", 7) == 0) {
			if (line[line_len - 1] == '\n') {
				line[line_len - 1] = '\0';
				line_len--;
			}
			char *p = line + 7;
			if (*p < ' ')
				continue;
			if ((*p == '\'' || *p == '"') && (!*(p + 1) || (*(p + 1) == '\''
			|| *(p + 1) == '"' )) && (*(p + 1) && !*(p + 2)))
				continue;

			if (expand_prompt_name(p) != EXIT_SUCCESS) {
				free(conf.encoded_prompt);
				conf.encoded_prompt = savestring(p, strlen(p));
			}
			continue;
		}

		/* THIS OPTION IS DEPRECATED */
		else if (*line == 'P' && strncmp(line, "PromptStyle=", 12) == 0) {
			char *p = line + 12;
			if (*p < ' ')
				continue;

			if (*p == 'd' && strncmp(p, "default", 7) == 0)
				prompt_notif = 1;
			else if (*p == 'c' && strncmp(p, "custom", 6) == 0)
				prompt_notif = 0;
			else
				prompt_notif = DEF_PROMPT_NOTIF;
		}

		/* The following values override those set via the Prompt line
		 * (provided it was set to a valid prompt name, as defined in the
		 * prompts file)*/
		else if (*line == 'N' && strncmp(line, "Notifications=", 14) == 0) {
			char *p = line + 14;
			if (*p < ' ')
				continue;

			if (*p == 't' && strncmp(p, "true", 4) == 0)
				prompt_notif = 1;
			else if (*p == 'f' && strncmp(p, "false", 5) == 0)
				prompt_notif = 0;
			else
				prompt_notif = DEF_PROMPT_NOTIF;
		}

/*		else if (*line == 'R' && strncmp(line, "RightPrompt=", 12) == 0) {
			char *p = line + 12;
			if (*p < ' ')
				continue;
			char *q = remove_quotes(p);
			if (!q)
				continue;
			free(right_prompt);
			right_prompt = savestring(q, strlen(q));
		} */

		else if (xargs.warning_prompt == UNSET && *line == 'E'
		&& strncmp(line, "EnableWarningPrompt=", 20) == 0) {
			char *p = line + 20;
			if (*p < ' ')
				continue;

			if (*p == 't' && strncmp(p, "true", 4) == 0)
				conf.warning_prompt = 1;
			else if (*p == 'f' && strncmp(p, "false", 5) == 0)
				conf.warning_prompt = 0;
			else
				conf.warning_prompt = DEF_WARNING_PROMPT;
		}

		else if (*line == 'W' && strncmp(line, "WarningPrompt=", 14) == 0) {
			char *p = line + 14;
			if (*p < ' ')
				continue;

			char *q = remove_quotes(p);
			if (!q)
				continue;

			free(conf.wprompt_str);
			conf.wprompt_str = savestring(q, strlen(q));
		}
#ifndef _NO_FZF
		else if (*line == 'F' && strncmp(line, "FzfTabOptions=", 14) == 0) {
			char *p = line + 14;
			if (*p < ' ')
				continue;

			char *q = remove_quotes(p);
			set_fzf_opts(q);
		}
#endif /* _NO_FZF */

		else if (*line == 'D' && strncmp(line, "DividingLine=", 13) == 0) {
			set_div_line(line + 13);
		}

		/* Interface colors */
		else if (!*ifacecolors && *line == 'I'
		&& strncmp(line, "InterfaceColors=", 16) == 0) {
			char *p = line + 16;
			if (*p < ' ')
				continue;

			char *color_line = strip_color_line(p, 't');
			if (!color_line)
				continue;

			*ifacecolors = savestring(color_line, strlen(color_line));
			free(color_line);
		}

		/* Filetype colors */
		else if (!*filecolors && *line == 'F'
		&& strncmp(line, "FiletypeColors=", 15) == 0) {
			char *p = line + 15;
			if (*p < ' ')
				continue;

			char *color_line = strip_color_line(p, 't');
			if (!color_line)
				continue;

			*filecolors = savestring(color_line, strlen(color_line));
			free(color_line);
		}

		/* File extension colors */
		else if (!*extcolors && *line == 'E'
		&& strncmp(line, "ExtColors=", 10) == 0) {
			char *p = line + 10;
			if (*p < ' ')
				continue;

			if (*p == '\'' || *p == '"')
				if (!*(++p))
					continue;

			ssize_t l = line_len - (p - line);
			if (l > 0 && p[l - 1] == '\n') {
				p[l - 1] = '\0';
				l--;
			}
			if (l > 0 && (p[l - 1] == '\'' || p[l - 1] == '"')) {
				p[l - 1] = '\0';
				l--;
			}
			*extcolors = savestring(p, (size_t)l);
		}

#ifndef _NO_ICONS
		/* Directory icon color */
		else if (*line == 'D' && strncmp(line, "DirIconColor=", 13) == 0) {
			char *p = line + 13;
			if (*p < ' ')
				continue;

			if ((*p == '\'' || *p == '"') && !*(++p))
				continue;

			if (line[line_len - 1] == '\n') {
				line[line_len - 1] = '\0';
				--line_len;
			}

			if (line[line_len - 1] == '\'' || line[line_len - 1] == '"')
				line[line_len - 1] = '\0';

			char *c = (char *)NULL;

			if (is_color_code(p) == 0 && (c = check_defs(p)) == NULL)
				continue;

			sprintf(dir_ico_c, "\x1b[%sm", c ? c : p);
		}
#endif /* !_NO_ICONS */

		else if (date_shades.type == SHADE_TYPE_UNSET
		&& *line == 'D' && strncmp(line, "DateShades=", 11) == 0) {
			set_shades(line + 11, DATE_SHADES);
		}

		else if (size_shades.type == SHADE_TYPE_UNSET
		&& *line == 'S' && strncmp(line, "SizeShades=", 11) == 0) {
			set_shades(line + 11, SIZE_SHADES);
		}
	}

	free(line);
	fclose(fp_colors);

	return EXIT_SUCCESS;
}
#endif /* !CLIFM_SUCKLESS */

/* Split the colors line COLORS_LINE and set the corresponding colors
 * according to TYPE (either interface or file type color) */
static void
split_color_line(char *colors_line, const int type)
{
	/* Split the colors line into substrings (one per color) */
	char *p = colors_line, *buf = (char *)NULL, **colors = (char **)NULL;
	size_t len = 0, words = 0;
	int eol = 0;

	while (eol == 0) {
		switch (*p) {

		case '\0': /* fallthrough */
		case '\n': /* fallthrough */
		case ':':
			if (!buf)
				break;
			buf[len] = '\0';
			colors = (char **)xrealloc(colors, (words + 1) * sizeof(char *));
			colors[words] = savestring(buf, len);
			words++;
			*buf = '\0';

			if (!*p)
				eol = 1;

			len = 0;
			p++;
			break;

		default:
			buf = (char *)xrealloc(buf, (len + 2) * sizeof(char));
			buf[len] = *p;
			len++;
			p++;
			break;
		}
	}

	p = (char *)NULL;

	free(buf);

	if (!colors)
		return;

	colors = (char **)xrealloc(colors, (words + 1) * sizeof(char *));
	colors[words] = (char *)NULL;

	/* Set the color variables
	 * The colors array is free'd by any of these functions */
	if (type == SPLIT_FILETYPE_COLORS)
		set_filetype_colors(colors, words);
	else
		set_iface_colors(colors, words);
}

/* Get color codes values from either the environment or the config file
 * and set colors accordingly. If some value is not found or is a wrong
 * value, the default is set. */
int
set_colors(const char *colorscheme, const int check_env)
{
	char *filecolors = (char *)NULL,
		 *extcolors = (char *)NULL,
	     *ifacecolors = (char *)NULL;

	date_shades.type = SHADE_TYPE_UNSET;
	size_shades.type = SHADE_TYPE_UNSET;

#ifndef _NO_ICONS
	*dir_ico_c = '\0';
#endif

	int ret = EXIT_SUCCESS;
	if (colorscheme && *colorscheme && color_schemes)
		ret = get_cur_colorscheme(colorscheme);

	/* CHECK_ENV is true only when this function is called from
	 * check_colors() (config.c) */
	if (ret == EXIT_SUCCESS && check_env == 1)
		get_colors_from_env(&filecolors, &extcolors, &ifacecolors);

#ifndef CLIFM_SUCKLESS
	if (ret == EXIT_SUCCESS && xargs.stealth_mode != 1) {
		if (read_color_scheme_file(cur_cscheme, &filecolors,
		&extcolors, &ifacecolors, check_env) == EXIT_FAILURE) {
			clear_defs();
			return EXIT_FAILURE;
		}
	}
#endif /* CLIFM_SUCKLESS */
	/* Split the color lines into substrings (one per color) */

	if (!extcolors) {
		/* Unload current extension colors */
		if (ext_colors_n > 0)
			free_extension_colors();
	} else {
		split_extension_colors(extcolors);
		free(extcolors);
	}

	if (!ifacecolors) {
		reset_iface_colors();
	} else {
		split_color_line(ifacecolors, SPLIT_INTERFACE_COLORS);
		free(ifacecolors);
	}

	if (!filecolors) {
		reset_filetype_colors();
	} else {
		split_color_line(filecolors, SPLIT_FILETYPE_COLORS);
		free(filecolors);
	}

#ifndef CLIFM_SUCKLESS
	clear_defs();
#endif

	/* If some color is unset or is a wrong color code, set the default value */
	set_default_colors();

	return EXIT_SUCCESS;
}

/* If completing trashed files (regular only) we need to remove the trash
 * extension in order to correctly determine the file color (according to
 * its actual extension).
 * Remove this extension (setting the initial dot to NULL) and return a
 * pointer to this character, so that we can later reinsert the dot */
char *
remove_trash_ext(char **ent)
{
	if (!(flags & STATE_COMPLETING) || (cur_comp_type != TCMP_UNTRASH
	&& cur_comp_type != TCMP_TRASHDEL))
		return (char *)NULL;

	char *d = strrchr(*ent, '.');
	if (d && d != *ent)
		*d = '\0';

	return d;
}

/* Print the entry ENT using color codes and ELN as ELN, right padding PAD
 * chars and terminate ENT with or without a new line char (NEW_LINE
 * 1 or 0 respectivelly)
 * ELN could be:
 * > 0: The ELN of a file in CWD
 * -1: Error getting ELN
 * 0: ELN should not be printed, for example, when listing files not in CWD */
void
colors_list(char *ent, const int eln, const int pad, const int new_line)
{
	char index[sizeof(int) + 8];
	*index = '\0';

	if (eln > 0)
		sprintf(index, "%d ", eln); /* NOLINT */
	else if (eln == -1)
		sprintf(index, "? "); /* NOLINT */
	else
		index[0] = '\0';

	struct stat attr;
	char *p = ent, *q = ent, t[PATH_MAX];
	char *eln_color = *index == '?' ? mi_c : el_c;

	if (*q == '~') {
		if (!*(q + 1) || (*(q + 1) == '/' && !*(q + 2)))
			xstrsncpy(t, user.home, PATH_MAX - 1);
		else
			snprintf(t, PATH_MAX, "%s/%s", user.home, q + 2);
		p = t;
	}

	size_t len = strlen(p);
	int rem_slash = 0;
	/* Remove the ending slash: lstat() won't take a symlink to dir as
	 * a symlink (but as a dir), if the file name ends with a slash */
	if (len > 1 && p[len - 1] == '/') {
		p[len - 1] = '\0';
		rem_slash = 1;
	}

	int ret = lstat(p, &attr);
	if (rem_slash)
		p[len - 1] = '/';

	char *wname = (char *)NULL;
	size_t wlen = wc_xstrlen(ent);
	if (wlen == 0)
		wname = truncate_wname(ent);

	char *color = fi_c;

	if (ret == -1) {
		color = uf_c;
	} else {
		switch (attr.st_mode & S_IFMT) {
		case S_IFREG: {
			char *d = remove_trash_ext(&ent);
			color = get_regfile_color(ent, &attr);
			if (d)
				*d = '.';
			}
			break;

		case S_IFDIR:
			if (conf.colorize == 0)
				color = di_c;
			else if (check_file_access(attr.st_mode, attr.st_uid, attr.st_gid) == 0)
				color = nd_c;
			else
				color = get_dir_color(ent, attr.st_mode, attr.st_nlink, -1);
			break;

		case S_IFLNK: {
			if (conf.colorize == 0) {
				color = ln_c;
			} else {
				char *linkname = realpath(ent, NULL);
				color = linkname ? ln_c : or_c;
				free(linkname);
			}
			}
			break;

		case S_IFIFO: color = pi_c; break;
		case S_IFBLK: color = bd_c; break;
		case S_IFCHR: color = cd_c; break;
		case S_IFSOCK: color = so_c; break;
		default: color = no_c; break;
		}
	}

	char *name = wname ? wname : ent;
	char *tmp = (flags & IN_SELBOX_SCREEN) ? abbreviate_file_name(name) : name;

	printf("%s%s%s%s%s%s%s%-*s", eln_color, index, df_c, color,
		tmp + tab_offset, df_c, new_line ? "\n" : "", pad, "");
	free(wname);

	if ((flags & IN_SELBOX_SCREEN) && tmp != name)
		free(tmp);
}

#ifndef CLIFM_SUCKLESS
size_t
get_colorschemes(void)
{
	struct stat attr;
	int schemes_total = 0;
	struct dirent *ent;
	DIR *dir_p;
	size_t i = 0;

	if (colors_dir && stat(colors_dir, &attr) == EXIT_SUCCESS) {
		schemes_total = count_dir(colors_dir, NO_CPOP) - 2;
		if (schemes_total) {
			if (!(dir_p = opendir(colors_dir))) {
				_err('e', PRINT_PROMPT, "opendir: %s: %s\n", colors_dir,
					strerror(errno));
				return 0;
			}

			color_schemes = (char **)xrealloc(color_schemes,
				((size_t)schemes_total + 2) * sizeof(char *));

			while ((ent = readdir(dir_p)) != NULL) {
				/* Skipp . and .. */
				char *name = ent->d_name;
				if (*name == '.' && (!name[1] || (name[1] == '.' && !name[2])))
					continue;

				char *ret = strchr(name, '.');
				/* If the file contains not dot, or if its extension is not
				 * .clifm, or if it's just a hidden file named ".clifm",
				 * skip it */
				if (!ret || ret == name || strcmp(ret, ".clifm") != 0)
					continue;

				*ret = '\0';
				color_schemes[i] = savestring(name, strlen(name));
				i++;
			}

			closedir(dir_p);
			color_schemes[i] = (char *)NULL;
		}
	}

	if (!data_dir)
		goto END;

	char sys_colors_dir[PATH_MAX];
	snprintf(sys_colors_dir, sizeof(sys_colors_dir), "%s/%s/colors",
		data_dir, PNL); /* NOLINT */

	if (stat(sys_colors_dir, &attr) == -1)
		goto END;

	int total_tmp = schemes_total;
	schemes_total += (count_dir(sys_colors_dir, NO_CPOP) - 2);

	if (schemes_total <= total_tmp)
		goto END;

	if (!(dir_p = opendir(sys_colors_dir))) {
		_err('e', PRINT_PROMPT, "opendir: %s: %s\n", sys_colors_dir,
			strerror(errno));
		goto END;
	}

	color_schemes = (char **)xrealloc(color_schemes,
					((size_t)schemes_total + 2) * sizeof(char *));

	size_t i_tmp = i;

	while ((ent = readdir(dir_p)) != NULL) {
		/* Skipp . and .. */
		char *name = ent->d_name;
		if (*name == '.' && (!name[1] || (name[1] == '.' && !name[2])))
			continue;

		char *ret = strchr(name, '.');
		/* If the file contains not dot, or if its extension is not
		 * .clifm, or if it's just a hidden file named ".clifm", skip it */
		if (!ret || ret == name || strcmp(ret, ".clifm") != 0)
			continue;

		*ret = '\0';

		size_t j;
		int dup = 0;
		for (j = 0; j < i_tmp; j++) {
			if (*color_schemes[j] == *name && strcmp(name, color_schemes[j]) == 0) {
				dup = 1;
				break;
			}
		}

		if (dup == 1)
			continue;

		color_schemes[i] = savestring(name, strlen(name));
		i++;
	}

	closedir(dir_p);
	color_schemes[i] = (char *)NULL;

END:
	qsort(color_schemes, i, sizeof(char *), (QSFUNC *)compare_strings);
	return i;
}
#endif /* CLIFM_SUCKLESS */

static void
print_color_blocks(void)
{
	UNSET_LINE_WRAP;

	int pad = (term_cols - 24) / 2;
	printf("\x1b[%dC\x1b[0;40m   \x1b[0m\x1b[0;41m   \x1b[0m\x1b[0;42m   "
		"\x1b[0m\x1b[0;43m   \x1b[0m\x1b[0;44m   \x1b[0m\x1b[0;45m   "
		"\x1b[0m\x1b[0;46m   \x1b[0m\x1b[0;47m   \x1b[0m\n", pad);

	printf("\x1b[%dC\x1b[0m\x1b[0;100m   \x1b[0m\x1b[0;101m   "
		"\x1b[0m\x1b[0;102m   \x1b[0m\x1b[0;103m   \x1b[0m\x1b[0;104m   "
		"\x1b[0m\x1b[0;105m   \x1b[0m\x1b[0;106m   \x1b[0m\x1b[0;107m   "
		"\x1b[0m\n", pad);

	SET_LINE_WRAP;
}

/* List color codes for file types used by the program */
void
color_codes(void)
{
	if (conf.colorize == 0) {
		printf(_("%s: Currently running without colors\n"), PROGRAM_NAME);
		return;
	}

	if (ext_colors_n > 0)
		printf(_("%sFile type colors%s\n\n"), BOLD, df_c);

	printf(_(" %sfile name%s: di: Directory*\n"), di_c, df_c);
	printf(_(" %sfile name%s: ed: EMPTY directory\n"), ed_c, df_c);
	printf(_(" %sfile name%s: nd: Directory with no read/exec permission\n"),
	    nd_c, df_c);
	printf(_(" %sfile name%s: fi: Regular file\n"), fi_c, df_c);
	printf(_(" %sfile name%s: ef: Empty (zero-lenght) file\n"), ef_c, df_c);
	printf(_(" %sfile name%s: nf: File with no read permission\n"),
	    nf_c, df_c);
	printf(_(" %sfile name%s: ex: Executable file\n"), ex_c, df_c);
	printf(_(" %sfile name%s: ee: Empty executable file\n"), ee_c, df_c);
	printf(_(" %sfile name%s: ln: Symbolic link*\n"), ln_c, df_c);
	printf(_(" %sfile name%s: or: Broken symbolic link\n"), or_c, df_c);
	printf(_(" %sfile name%s: mh: Multi-hardlink\n"), mh_c, df_c);
	printf(_(" %sfile name%s: bd: Block special file\n"), bd_c, df_c);
	printf(_(" %sfile name%s: cd: Character special file\n"), cd_c, df_c);
	printf(_(" %sfile name%s: so: Socket file\n"), so_c, df_c);
	printf(_(" %sfile name%s: pi: Pipe or FIFO special file\n"), pi_c, df_c);
	printf(_(" %sfile name%s: su: SUID file\n"), su_c, df_c);
	printf(_(" %sfile name%s: sg: SGID file\n"), sg_c, df_c);
	printf(_(" %sfile name%s: ca: File with capabilities\n"), ca_c, df_c);
	printf(_(" %sfile name%s: st: Sticky and NOT other-writable "
		 "directory*\n"), st_c, df_c);
	printf(_(" %sfile name%s: tw: Sticky and other-writable directory*\n"),
		tw_c, df_c);
	printf(_(" %sfile name%s: ow: Other-writable and NOT sticky directory*\n"),
	    ow_c, df_c);
	printf(_(" %sfile name%s: no: Unknown file type\n"), no_c, df_c);
	printf(_(" %sfile name%s: uf: Unaccessible (non-stat'able) file\n"),
		uf_c, df_c);

	printf(_("\n*The slash followed by a number (/xx) after directories "
		 "or symbolic links to directories indicates the amount of "
		 "files contained by the corresponding directory, excluding "
		 "self (.) and parent (..) directories.\n"));
	printf(_("\nThe second field in this list is the code that is to be used "
		 "to modify the color of the corresponding file type in the "
		 "color scheme file (in the \"FiletypeColors\" line), "
		 "using the same ANSI style color format used by dircolors. "
		 "By default, %s uses only 8/16 colors, but you can use 256 "
		 "and RGB/true colors as well.\n\n"), PROGRAM_NAME);

	if (ext_colors_n > 0) {
		size_t i;
		printf(_("%sExtension colors%s\n\n"), BOLD, df_c);
		for (i = 0; i < ext_colors_n; i++) {
			printf(" \x1b[%sm*.%s%s\n", ext_colors[i].value,
				ext_colors[i].name, NC);
		}
		putchar('\n');
	}

	print_color_blocks();
}
