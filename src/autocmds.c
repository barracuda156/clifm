/* autocmds.c -- run cmds on a per directory basis */

/*
 * This file is part of CliFM
 *
 * Copyright (C) 2016-2024, L. Abramovich <leo.clifm@outlook.com>
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
#include <string.h>
#include <readline/tilde.h>

#include "aux.h"
#include "colors.h"
#include "sanitize.h"
#include "spawn.h"

/* The opts struct contains option values previous to any autocommand call */
void
reset_opts(void)
{
	opts.color_scheme = cur_cscheme;
	opts.files_counter = conf.files_counter;
	opts.light_mode = conf.light_mode;
	opts.max_files = max_files;
	opts.long_view = conf.long_view;
	opts.show_hidden = conf.show_hidden;
	opts.max_name_len = conf.max_name_len;
	opts.only_dirs = conf.only_dirs;
	opts.pager = conf.pager;
	opts.sort = conf.sort;
	opts.sort_reverse = conf.sort_reverse;
}

/* Run autocommands for the current directory */
int
check_autocmds(void)
{
	if (!autocmds || autocmds_n == 0)
		return FUNC_SUCCESS;

	size_t i;
	int found = 0;

	for (i = 0; i < autocmds_n; i++) {
		if (!autocmds[i].pattern)
			continue;

		int rev = 0;
		char *p = autocmds[i].pattern;
		if (*p == '!') {
			++p;
			rev = 1;
		}

		/* Check workspaces (@wsN)*/
		if (*autocmds[i].pattern == '@' && autocmds[i].pattern[1] == 'w'
		&& autocmds[i].pattern[2] == 's' && autocmds[i].pattern[3]
		/* char '1' - 48 (or '0') == int 1 */
		&& autocmds[i].pattern[3] - 48 == cur_ws + 1) {
			found = 1;
			goto RUN_AUTOCMD;
		}

		/* Double asterisk: match everything starting with PATTERN
		 * (less double asterisk itself and ending slash)*/
		size_t plen = strlen(p), n = 0;
		if (!rev && plen >= 3 && p[plen - 1] == '*' && p[plen - 2] == '*') {
			n = 2;
			if (p[plen - 3] == '/')
				n++;

			if (*p == '~') {
				p[plen - n] = '\0';
				char *path = tilde_expand(p);
				if (!path)
					continue;

				p[plen - n] = (n == 2 ? '*' : '/');
				const size_t tlen = strlen(path);
				const int ret = strncmp(path, workspaces[cur_ws].path, tlen);
				free(path);

				if (ret == 0) {
					found = 1;
					goto RUN_AUTOCMD;
				}

			} else { /* We have an absolute path */
				/* If (plen - n) == 0 we have "/\**", that is, match everything:
				 * no need to perform any check. */
				if (plen - n == 0 || strncmp(autocmds[i].pattern,
				workspaces[cur_ws].path, plen - n) == 0) {
					found = 1;
					goto RUN_AUTOCMD;
				}
			}
		}

		/* Glob expression or plain text for PATTERN */
		glob_t g;
		const int ret = glob(p, GLOB_NOSORT | GLOB_NOCHECK | GLOB_TILDE
		| GLOB_BRACE, NULL, &g);

		if (ret != FUNC_SUCCESS) {
			globfree(&g);
			continue;
		}

		size_t j;
		for (j = 0; j < g.gl_pathc; j++) {
			if (*workspaces[cur_ws].path == *g.gl_pathv[j]
			&& strcmp(workspaces[cur_ws].path, g.gl_pathv[j]) == 0) {
				found = 1;
				break;
			}
		}
		globfree(&g);

		if (rev == 0) {
			if (found == 0)
				continue;
		} else {
			if (found == 1)
				continue;
		}

RUN_AUTOCMD:
		if (autocmd_set == 0) {
			/* Backup current options, only if there was no autocmd for
			 * this directory */
			opts.light_mode = conf.light_mode;
			opts.files_counter = conf.files_counter;
			opts.long_view = conf.long_view;
			opts.max_files = max_files;
			opts.show_hidden = conf.show_hidden;
			opts.sort = conf.sort;
			opts.sort_reverse = conf.sort_reverse;
			opts.max_name_len = conf.max_name_len;
			opts.pager = conf.pager;
			opts.only_dirs = conf.only_dirs;
			if (autocmds[i].color_scheme && cur_cscheme)
				opts.color_scheme = cur_cscheme;
			else
				opts.color_scheme = (char *)NULL;
			autocmd_set = 1;
		}

		/* Set options for current directory */
		if (autocmds[i].light_mode != -1)
			conf.light_mode = autocmds[i].light_mode;
		if (autocmds[i].files_counter != -1)
			conf.files_counter = autocmds[i].files_counter;
		if (autocmds[i].long_view != -1)
			conf.long_view = autocmds[i].long_view;
		if (autocmds[i].show_hidden != -1)
			conf.show_hidden = autocmds[i].show_hidden;
		if (autocmds[i].only_dirs != -1)
			conf.only_dirs = autocmds[i].only_dirs;
		if (autocmds[i].pager != -1)
			conf.pager = autocmds[i].pager;
		if (autocmds[i].sort != -1)
			conf.sort = autocmds[i].sort;
		if (autocmds[i].sort_reverse != -1)
			conf.sort_reverse = autocmds[i].sort_reverse;
		if (autocmds[i].max_name_len != -1)
			conf.max_name_len = autocmds[i].max_name_len;
		if (autocmds[i].max_files != -2)
			max_files = autocmds[i].max_files;
		if (autocmds[i].color_scheme)
			set_colors(autocmds[i].color_scheme, 0);
		if (autocmds[i].cmd) {
			if (xargs.secure_cmds == 0
			|| sanitize_cmd(autocmds[i].cmd, SNT_AUTOCMD) == FUNC_SUCCESS)
				launch_execl(autocmds[i].cmd);
		}

		break;
	}

	return found;
}

/* Revert back to options previous to autocommand */
void
revert_autocmd_opts(void)
{
	conf.light_mode = opts.light_mode;
	conf.files_counter = opts.files_counter;
	conf.long_view = opts.long_view;
	max_files = opts.max_files;
	conf.show_hidden = opts.show_hidden;
	conf.max_name_len = opts.max_name_len;
	conf.pager = opts.pager;
	conf.sort = opts.sort;
	conf.only_dirs = opts.only_dirs;
	conf.sort_reverse = opts.sort_reverse;
	if (opts.color_scheme && opts.color_scheme != cur_cscheme)
		set_colors(opts.color_scheme, 0);
	autocmd_set = 0;
}

/* Store each autocommand option in the corresponding field of the
 * autocmds struct */
static void
set_autocmd_opt(char *opt)
{
	if (!opt || !*opt)
		return;

	if (*opt == '!' && *(++opt)) {
		free(autocmds[autocmds_n].cmd);
		autocmds[autocmds_n].cmd = savestring(opt, strlen(opt));
		return;
	}

	char *p = strchr(opt, '=');
	if (!p || !*(++p))
		return;

	*(p - 1) = '\0';
	if (*opt == 'c' && opt[1] == 's') {
		int i = (int)cschemes_n;
		while (--i >= 0) {
			if (*color_schemes[i] == *p && strcmp(color_schemes[i], p) == 0) {
				autocmds[autocmds_n].color_scheme = color_schemes[i];
				break;
			}
		}
	} else {
		int a = atoi(p);
		if (a == INT_MIN)
			a = 0;
		if (*opt == 'f' && opt[1] == 'c')
			autocmds[autocmds_n].files_counter = a;
		else if (*opt == 'h' && opt[1] == 'f')
			autocmds[autocmds_n].show_hidden = a;
		else if (*opt == 'l' && opt[1] == 'm')
			autocmds[autocmds_n].light_mode = a;
		else if (*opt == 'l' && opt[1] == 'v')
			autocmds[autocmds_n].long_view = a;
		else if (*opt == 'm' && opt[1] == 'f')
			autocmds[autocmds_n].max_files = a;
		else if (*opt == 'm' && opt[1] == 'n')
			autocmds[autocmds_n].max_name_len = a;
		else if (*opt == 'o' && opt[1] == 'd')
			autocmds[autocmds_n].only_dirs = a;
		else if (*opt == 'p' && opt[1] == 'g')
			autocmds[autocmds_n].pager = a;
		else if (*opt == 's' && opt[1] == 't')
			autocmds[autocmds_n].sort = a;
		else {
			if (*opt == 's' && opt[1] == 'r')
				autocmds[autocmds_n].sort_reverse = a;
		}
	}
}

static void
init_autocmd_opts(void)
{
	autocmds[autocmds_n].cmd = (char *)NULL;
	autocmds[autocmds_n].color_scheme = cur_cscheme;
	autocmds[autocmds_n].files_counter = conf.files_counter;
	autocmds[autocmds_n].light_mode = conf.light_mode;
	autocmds[autocmds_n].long_view = conf.long_view;
	autocmds[autocmds_n].max_files = max_files;
	autocmds[autocmds_n].max_name_len = conf.max_name_len;
	autocmds[autocmds_n].only_dirs = conf.only_dirs;
	autocmds[autocmds_n].pager = conf.pager;
	autocmds[autocmds_n].show_hidden = conf.show_hidden;
	autocmds[autocmds_n].sort = conf.sort;
	autocmds[autocmds_n].sort_reverse = conf.sort_reverse;
}

/* Take an autocmd line (from the config file) and store parameters
 * in a struct. */
void
parse_autocmd_line(char *cmd, const size_t buflen)
{
	if (!cmd || !*cmd)
		return;

	const size_t clen = strnlen(cmd, buflen);
	if (clen > 0 && cmd[clen - 1] == '\n')
		cmd[clen - 1] = '\0';

	char *p = strchr(cmd, ' ');
	if (!p || !*(p + 1))
		return;

	*p = '\0';

	autocmds = xnrealloc(autocmds, autocmds_n + 1, sizeof(struct autocmds_t));
	autocmds[autocmds_n].pattern = savestring(cmd, strnlen(cmd, buflen));

	init_autocmd_opts();

	++p;
	char *q = p;
	while (1) {
		char *val = (char *)NULL;
		if (*p == ',') {
			*(p) = '\0';
			p++;
			val = q;
			q = p;
		} else if (!*p) {
			val = q;
		} else {
			++p;
			continue;
		}

		set_autocmd_opt(val);

		if (!*p)
			break;
	}

	autocmds_n++;
}
