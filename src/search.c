/* search.c -- functions for the search system */

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

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <glob.h>

/* We need rl_line_buffer in case of no matches and no metacharacter */
#include <readline/readline.h>

#include "aux.h"
#include "checks.h"
#include "colors.h"
#include "exec.h"
#include "messages.h"
#include "misc.h"
#include "navigation.h"
#include "sort.h"

static int
exec_find(char *_path, char *method, char *pattern)
{
	if (follow_symlinks == 1) {
		char *cmd[] = {"find", "-L", _path, method, pattern, NULL};
		return launch_execve(cmd, FOREGROUND, E_NOSTDERR);
	}

	char *cmd[] = {"find", _path, method, pattern, NULL};
	return launch_execve(cmd, FOREGROUND, E_NOSTDERR);
}

static int
run_find(char *search_path, char *arg)
{
	char *_path = (search_path && *search_path) ? search_path : ".";
#if defined(_BE_POSIX)
	/* POSIX find(1) only supports -name */
	char *method = "-name";
#elif defined(__OpenBSD__)
	/* No -regex nor -iregex option in OpenBSD's find(1) */
	char *method = conf.case_sens_search == 1 ? "-name" : "-iname";
#else
	char *method = conf.search_strategy == REGEX_ONLY
		? (conf.case_sens_search == 1 ? "-regex" : "-iregex")
		: (conf.case_sens_search == 1 ? "-name" : "-iname");
#endif /* _BE_POSIX */

	int glob_char = check_glob_char(arg + 1, GLOB_REGEX);
	if (glob_char == 1)
		return exec_find(_path, method, arg + 1);

	int ret = EXIT_SUCCESS;
	char *ss = (char *)xnmalloc(strlen(arg + 1) + 5, sizeof(char));
#if !defined(_BE_POSIX) && !defined(__OpenBSD__)
	if (conf.search_strategy == REGEX_ONLY)
		sprintf(ss, ".*%s.*", arg + 1);
	else
		sprintf(ss, "*%s*", arg + 1);
#else
	sprintf(ss, "*%s*", arg + 1);
#endif /* !_BE_POSIX && !__OpenBSD__ */

	ret = exec_find(_path, method, ss);
	free(ss);

	return ret;
}

/* List matching file names in the specified directory */
int
search_glob(char **args, const int invert)
{
	if (!args || !args[0])
		return EXIT_FAILURE;

	char *search_str = (char *)NULL, *search_path = (char *)NULL;
	mode_t file_type = 0;
	struct stat attr;

	/* If there are two arguments, the one starting with '-' is the
	 * file type and the other is the path */
	if (args[1] && args[2]) {
		if (*args[1] == '-') {
			file_type = (mode_t)args[1][1];
			search_path = args[2];
		} else if (*args[2] == '-') {
			file_type = (mode_t)args[2][1];
			search_path = args[1];
		} else {
			search_path = args[1];
		}
	} else {
		/* If just one argument, '-' indicates file type. Else, we have a path */
		if (args[1]) {
			if (*args[1] == '-')
				file_type = (mode_t)args[1][1];
			else
				search_path = args[1];
		}
	}

	/* If no arguments, search_path will be NULL and file_type zero */
	if (file_type != 0) {
		/* Convert file type into a macro that can be decoded by stat(). If
		 * file type is specified, matches will be checked against this value */
		switch (file_type) {
		case 'b': file_type = invert == 1 ? DT_BLK : S_IFBLK; break;
		case 'c': file_type = invert == 1 ? DT_CHR : S_IFCHR; break;
		case 'd': file_type = invert == 1 ? DT_DIR : S_IFDIR; break;
		case 'f': file_type = invert == 1 ? DT_REG : S_IFREG; break;
		case 'l': file_type = invert == 1 ? DT_LNK : S_IFLNK; break;
		case 'p': file_type = invert == 1 ? DT_FIFO : S_IFIFO; break;
		case 's': file_type = invert == 1 ? DT_SOCK : S_IFSOCK; break;
		case 'x': run_find(search_path, args[0]); return EXIT_SUCCESS;
		default:
			fprintf(stderr, _("search: '%c': Unrecognized file "
				"type\n"), (char)file_type);
			return 2; /* Return 2 to avoid trying the regex approach */
		}
	}

	/* If we have a path ("/str /path"), chdir into it, since glob()
	 * works on CWD */
	if (search_path && *search_path) {
		/* Deescape the search path, if necessary */
		if (strchr(search_path, '\\')) {
			char *deq_dir = dequote_str(search_path, 0);
			if (!deq_dir) {
				xerror(_("search: %s: Error dequoting file name\n"), args[1]);
				return EXIT_FAILURE;
			}

			strcpy(search_path, deq_dir);
			free(deq_dir);
		}

		size_t path_len = strlen(search_path);
		if (path_len > 1 && search_path[path_len - 1] == '/')
			search_path[path_len - 1] = '\0';

		/* If search is current directory */
		if ((*search_path == '.' && !search_path[1]) ||
		    (search_path[1] == workspaces[cur_ws].path[1]
		    && strcmp(search_path, workspaces[cur_ws].path) == 0)) {
				search_path = (char *)NULL;
		} else {
			if (xchdir(search_path, NO_TITLE) == -1) {
				xerror("search: %s: %s\n", search_path, strerror(errno));
				return EXIT_FAILURE;
			}
		}
	}

	int i;
	char *tmp = args[0];

	if (invert == 1)
		tmp++;

	search_flags &= ~NO_GLOB_CHAR;
	/* Search for globbing char */
	int glob_char_found = check_glob_char(tmp, GLOB_REGEX);

	/* If search string is just "STR" (no glob chars), change it to either "*STR*"
	 * (if search strategy is GLOB_ONLY), or ".*STR.*" */
	if (glob_char_found == 0) {
		search_flags |= NO_GLOB_CHAR;
		size_t search_str_len = strlen(args[0]);

		args[0] = (char *)xrealloc(args[0], (search_str_len + 5) * sizeof(char));
		tmp = args[0];
		if (invert == 1)
			++tmp;

		size_t slen = strlen(tmp);
		char *s = savestring(tmp, slen);

		if (conf.search_strategy != GLOB_ONLY) {
			*(tmp + 1) = '.';
			*(tmp + 2) = '*';
			strcpy(tmp + 3, s + 1);
			slen += 2;
			tmp[slen] = '.';
			tmp[slen + 1] = '*';
			tmp[slen + 2] = '\0';
			free(s);
//			search_flags |= NO_GLOB_CHAR;
			/* Let's return here to perform a regex search (if called from
			 * search_function(), in search.c) */
			return EXIT_FAILURE;
		} else {
			*(tmp + 1) = '*';
			strcpy(tmp + 2, s + 1);
			slen++;
			tmp[slen] = '*';
			tmp[slen + 1] = '\0';
			free(s);
			search_str = tmp + 1;
		}
	} else {
		search_str = tmp + 1;
	}

	/* Get matches, if any */
	glob_t globbed_files;
	int ret = glob(search_str, GLOB_BRACE, NULL, &globbed_files);
	if (ret != 0) {
/*		puts(_("Glob: No matches found. Trying regex...")); */
		globfree(&globbed_files);

		if (search_path) {
			/* Go back to the directory we came from */
			if (xchdir(workspaces[cur_ws].path, NO_TITLE) == -1)
				xerror("search: %s: %s\n", workspaces[cur_ws].path, strerror(errno));
		}

		return EXIT_FAILURE;
	}

	size_t g = 0;
	char **gfiles = (char **)NULL;

	/* glob(3) doesn't sort directories first. Let's do it ourselves */
	if (conf.list_dirs_first == 1) {
		int *dirs = (int *)xnmalloc(globbed_files.gl_pathc + 1, sizeof(int));
		gfiles = (char **)xnmalloc(globbed_files.gl_pathc + 1, sizeof(char *));
		for (i = 0; globbed_files.gl_pathv[i]; i++) {
			if (stat(globbed_files.gl_pathv[i], &attr) != -1
			&& S_ISDIR(attr.st_mode))
				dirs[i] = 1;
			else
				dirs[i] = 0;
		}

		for (i = 0; globbed_files.gl_pathv[i]; i++) {
			if (dirs[i] == 1) {
				gfiles[g] = globbed_files.gl_pathv[i];
				g++;
			}
		}

		for (i = 0; globbed_files.gl_pathv[i]; i++) {
			if (dirs[i] == 0) {
				gfiles[g] = globbed_files.gl_pathv[i];
				g++;
			}
		}

		free(dirs);
		gfiles[g] = (char *)NULL;
	} else {
		gfiles = globbed_files.gl_pathv;
		g = globbed_files.gl_pathc;
	}

	/* We have matches */
	int sfiles = 0, found = 0;
	size_t flongest = 0;

	/* We need to store pointers to matching file names in array of pointers,
	 * just as the file name length (to construct the columned output), and,
	 * if searching in CWD, its index (ELN) in the dirlist array as well */
	char **pfiles = (char **)NULL;
	int *eln = (int *)0;
	size_t *files_len = (size_t *)0;
	struct dirent **ent = (struct dirent **)NULL;
	int longest_eln = -1;

	if (invert == 1) {
		if (!search_path) {
			int k;
			pfiles = (char **)xnmalloc(files + 1, sizeof(char *));
			eln = (int *)xnmalloc(files + 1, sizeof(int));
			files_len = (size_t *)xnmalloc(files + 1, sizeof(size_t));

			for (k = 0; file_info[k].name; k++) {
				int l, f = 0;

				for (l = 0; gfiles[l]; l++) {
					if (*gfiles[l] == *file_info[k].name
					&& strcmp(gfiles[l], file_info[k].name) == 0) {
						f = 1;
						break;
					}
				}

				if (f == 1 || (file_type && file_info[k].type != file_type))
					continue;

				eln[found] = (int)(k + 1);
				files_len[found] = wc_xstrlen(file_info[k].name)
					+ (size_t)file_info[k].eln_n + 1;

				if (files_len[found] > flongest) {
					flongest = files_len[found];
					longest_eln = k + 1;
				}

				pfiles[found] = file_info[k].name;
				found++;
			}
		} else {
			sfiles = scandir(search_path, &ent, skip_files, xalphasort);
			if (sfiles == -1)
				goto SCANDIR_ERROR;

			pfiles = (char **)xnmalloc((size_t)sfiles + 1, sizeof(char *));
			eln = (int *)xnmalloc((size_t)sfiles + 1, sizeof(int));
			files_len = (size_t *)xnmalloc((size_t)sfiles + 1, sizeof(size_t));

			int k, l;
			for (k = 0; k < sfiles; k++) {
				int f = 0;
				for (l = 0; gfiles[l]; l++) {
					if (*ent[k]->d_name == *gfiles[l]
					&& strcmp(ent[k]->d_name, gfiles[l]) == 0) {
						f = 1;
						break;
					}
				}

				if (f == 1)
					continue;

#if !defined(_DIRENT_HAVE_D_TYPE)
				mode_t type;
				if (lstat(ent[k]->d_name, &attr) == -1)
					continue;
				type = get_dt(attr.st_mode);
				if (file_type && type != file_type)
#else
				if (file_type && ent[k]->d_type != file_type)
#endif
					continue;

				eln[found] = -1;
				files_len[found] = wc_xstrlen(ent[k]->d_name);

				if (files_len[found] > flongest)
					flongest = files_len[found];

				pfiles[found] = ent[k]->d_name;
				found++;
			}
		}
	}

	else { /* No invert search */
		pfiles = (char **)xnmalloc(g + 1, sizeof(char *));
		eln = (int *)xnmalloc(g + 1, sizeof(int));
		files_len = (size_t *)xnmalloc(g + 1, sizeof(size_t));

		for (i = 0; gfiles[i]; i++) {
			if (*gfiles[i] == '.' && (!gfiles[i][1]
			|| (gfiles[i][1] == '.'	&& !gfiles[i][2])))
				continue;

			if (file_type != 0) {
				/* Simply skip all files not matching file_type */
				if (lstat(gfiles[i], &attr) == -1)
					continue;
				if ((attr.st_mode & S_IFMT) != file_type)
					continue;
			}

			pfiles[found] = gfiles[i];

			/* Get the longest file name in the list */
			/* If not in CWD, we only need to know the file's length (no ELN) */
			if (search_path) {
				/* This will be passed to colors_list(): -1 means no ELN */
				eln[found] = -1;
				files_len[found] = wc_xstrlen(pfiles[found]);

				if (files_len[found] > flongest)
					flongest = files_len[found];

				found++;
				continue;
			}

			/* If no search_path */
			/* If searching in CWD, take into account the file's ELN
			 * when calculating its legnth */
			size_t j, f = 0;
			for (j = 0; file_info[j].name; j++) {
				if (*pfiles[found] != *file_info[j].name
				|| strcmp(pfiles[found], file_info[j].name) != 0)
					continue;

				f = 1;
				eln[found] = (int)(j + 1);
				files_len[found] = wc_xstrlen(file_info[j].name)
					+ (size_t)file_info[j].eln_n + 1;

				if (files_len[found] > flongest) {
					flongest = files_len[found];
					longest_eln = (int)(j + 1);
				}
			}

			if (f == 0) {
				eln[found] = -1;
				files_len[found] = 0;
			}

			found++;
		}
	}

SCANDIR_ERROR:
	if (found == 0)
		goto END;

	int eln_pad = 0;
	if (!search_path) {
#ifndef _NO_ICONS
		if (conf.icons == 1)
			flongest += 3;
#endif

		int largest = 0;
		i = found;
		while (--i >= 0) {
			if (eln[i] > largest)
				largest = eln[i];
		}
		eln_pad = DIGINUM(largest);

		if (longest_eln > -1)
			flongest += (size_t)(eln_pad - DIGINUM(longest_eln));
	}

	/* Print the results using colors and columns */
	int columns_n = 0, last_column = 0;

	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	unsigned short tcols = w.ws_col;

	if (flongest == 0 || flongest > tcols)
		columns_n = 1;
	else
		columns_n = (int)(tcols / (flongest + 1));

	if (columns_n > found)
		columns_n = found;

	size_t t = tab_offset;
	tab_offset = 0;

	for (i = 0; i < found; i++) {
		if (!pfiles[i])
			continue;

		if ((i + 1) % columns_n == 0)
			last_column = 1;
		else
			last_column = 0;

		char ind_chr = file_info[eln[i] - 1].sel == 1 ? SELFILE_CHR : ' ';
		char *ind_chr_color = file_info[eln[i] - 1].sel == 1 ? li_cb : "";

		if (!search_path) {
			printf("%s%*d%s%s%c%s%s%s%s%c", el_c, eln_pad, eln[i], df_c,
				ind_chr_color, ind_chr, df_c,
#ifndef _NO_ICONS
				conf.icons == 1 ? file_info[eln[i] - 1].icon_color : "",
				conf.icons == 1 ? file_info[eln[i] - 1].icon : "",
				df_c, conf.icons == 1 ? ' ' : 0);
#else
				"", "", df_c, 0);
#endif
		}

		int name_pad = (last_column == 1 || i == (found - 1)) ? NO_PAD :
		    (int)(flongest - files_len[i] - ( search_path ? 0
		    : (size_t)(eln_pad - DIGINUM(eln[i])) ) + 1);

		if (name_pad < 0)
			name_pad = 0;

		colors_list(pfiles[i], NO_ELN, name_pad,
		    (last_column == 1 || i == found - 1) ? 1 : NO_NEWLINE);
	}
	tab_offset = t;

	print_reload_msg(_("Matches found: %d%s\n"), found,
		conf.search_strategy != GLOB_ONLY ? " (glob)" : "");

END:
	/* Free stuff */
	if (invert == 1 && search_path) {
		i = sfiles;
		while (--i >= 0)
			free(ent[i]);
		free(ent);
	}

	free(eln);
	free(files_len);
	free(pfiles);
	if (conf.list_dirs_first == 1)
		free(gfiles);
	globfree(&globbed_files);

	/* If needed, go back to the directory we came from */
	if (search_path && xchdir(workspaces[cur_ws].path, NO_TITLE) == -1) {
		xerror("search: %s: %s\n", workspaces[cur_ws].path, strerror(errno));
		return EXIT_FAILURE;
	}

	return found == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* List matching (or not marching, if inverse is set to 1) file names
 * in the specified directory */
int
search_regex(char **args, const int invert, const int case_sens)
{
	if (!args || !args[0])
		return EXIT_FAILURE;

	char *search_str = (char *)NULL, *search_path = (char *)NULL;
	mode_t file_type = 0;

	/* If there are two arguments, the one starting with '-' is the
	 * file type and the other is the path */
	if (args[1] && args[2]) {
		if (*args[1] == '-') {
			file_type = (mode_t) * (args[1] + 1);
			search_path = args[2];
		} else if (*args[2] == '-') {
			file_type = (mode_t) * (args[2] + 1);
			search_path = args[1];
		} else {
			search_path = args[1];
		}
	} else {
		/* If just one argument, '-' indicates file type. Else, we have a path */
		if (args[1]) {
			if (*args[1] == '-')
				file_type = (mode_t) * (args[1] + 1);
			else
				search_path = args[1];
		}
	}

	/* If no arguments, search_path will be NULL and file_type zero */
	if (file_type != 0) {
		/* If file type is specified, matches will be checked against this value */
		switch (file_type) {
		case 'b': file_type = DT_BLK; break;
		case 'c': file_type = DT_CHR; break;
		case 'd': file_type = DT_DIR; break;
		case 'f': file_type = DT_REG; break;
		case 'l': file_type = DT_LNK; break;
		case 'p': file_type = DT_FIFO; break;
		case 's': file_type = DT_SOCK; break;
		case 'x': run_find(search_path, args[0]); return EXIT_SUCCESS;
		default:
			fprintf(stderr, _("search: '%c': Unrecognized file "
				"type\n"), (char)file_type);
			return EXIT_FAILURE;
		}
	}

	struct dirent **reg_dirlist = (struct dirent **)NULL;
	int tmp_files = 0;

	/* If we have a path ("/str /path"), chdir into it, since
	 * regex() works on CWD */
	if (search_path && *search_path) {
		/* Deescape the search path, if necessary */
		if (strchr(search_path, '\\')) {
			char *deq_dir = dequote_str(search_path, 0);

			if (!deq_dir) {
				xerror(_("search: %s: Error dequoting file name\n"), args[1]);
				return EXIT_FAILURE;
			}

			strcpy(search_path, deq_dir);
			free(deq_dir);
		}

		size_t path_len = strlen(search_path);
		if (path_len > 1 && search_path[path_len - 1] == '/')
			search_path[path_len - 1] = '\0';

		if ((*search_path == '.' && !search_path[1])
		|| (search_path[1] == workspaces[cur_ws].path[1]
		&& strcmp(search_path, workspaces[cur_ws].path) == 0))
			search_path = (char *)NULL;

		if (search_path && *search_path) {
			if (xchdir(search_path, NO_TITLE) == -1) {
				xerror("search: %s: %s\n", search_path, strerror(errno));
				return EXIT_FAILURE;
			}

			tmp_files = scandir(".", &reg_dirlist, skip_files, xalphasort);
			if (tmp_files == -1) {
				xerror("search: %s: %s\n", search_path, strerror(errno));

				if (xchdir(workspaces[cur_ws].path, NO_TITLE) == -1) {
					xerror("search: %s: %s\n",
						workspaces[cur_ws].path, strerror(errno));
				}

				return EXIT_FAILURE;
			}
		}
	}

	size_t i;

	/* Search for regex expression */
	int regex_found = check_regex(args[0] + 1);

	/* If search string is just "STR" (no regex chars), change it
	 * to ".*STR.*" */
	if (regex_found == EXIT_FAILURE) {
		size_t search_str_len = strlen(args[0]);

		args[0] = (char *)xrealloc(args[0], (search_str_len + 5) * sizeof(char));
		char *tmp_str = (char *)xnmalloc(search_str_len + 1, sizeof(char));
		strcpy(tmp_str, args[0] + (invert ? 2 : 1));
		*args[0] = '.';
		*(args[0] + 1) = '*';
		*(args[0] + 2) = '\0';
		strcat(args[0], tmp_str);
		free(tmp_str);
		*(args[0] + search_str_len + 1) = '.';
		*(args[0] + search_str_len + 2) = '*';
		*(args[0] + search_str_len + 3) = '\0';
		search_str = args[0];
	} else {
		search_str = args[0] + (invert == 1 ? 2 : 1);
	}

	/* Get matches, if any, using regular expressions */
	regex_t regex_files;
	int reg_flags = case_sens ? (REG_NOSUB | REG_EXTENDED)
				: (REG_NOSUB | REG_EXTENDED | REG_ICASE);
	int ret = regcomp(&regex_files, search_str, reg_flags);

	if (ret != EXIT_SUCCESS) {
		fprintf(stderr, _("'%s': Invalid regular expression\n"), search_str);
		regfree(&regex_files);

		if (search_path) {
			for (i = 0; i < (size_t)tmp_files; i++)
				free(reg_dirlist[i]);
			free(reg_dirlist);

			if (xchdir(workspaces[cur_ws].path, NO_TITLE) == -1) {
				xerror("search: %s: %s\n", workspaces[cur_ws].path, strerror(errno));
			}
		}

		return EXIT_FAILURE;
	}

	size_t found = 0;
	int *regex_index = (int *)xnmalloc((search_path ? (size_t)tmp_files
					: files) + 1, sizeof(int));

	for (i = 0; i < (search_path ? (size_t)tmp_files : files); i++) {
		if (regexec(&regex_files, (search_path ? reg_dirlist[i]->d_name
		: file_info[i].name), 0, NULL, 0) == EXIT_SUCCESS) {
			if (invert == 0) {
				regex_index[found] = (int)i;
				found++;
			}
		} else {
			if (invert == 1) {
				regex_index[found] = (int)i;
				found++;
			}
		}
	}

	regfree(&regex_files);

	if (found == 0) {
		int exit_status = EXIT_FAILURE;
		char *s = (conf.autocd == 1 && !args[1] && (regex_found == EXIT_FAILURE
				|| (search_flags & NO_GLOB_CHAR)) && rl_line_buffer)
				? strrchr(rl_line_buffer, '/') : (char *)NULL;

		if (s && s != rl_line_buffer) {
			/* Input string looks like a path: let's err like it was */
			fprintf(stderr, _("cd: %s: %s\n"), rl_line_buffer, strerror(ENOENT));
			exit_status = ENOENT;
		} else if (search_flags & NO_GLOB_CHAR) {
			fputs(_("search: No matches found\n"), stderr);
		} else {
			fputs(_("No matches found\n"), stderr);
		}
		search_flags &= ~NO_GLOB_CHAR;

		free(regex_index);

		if (search_path) {
			int j = tmp_files;
			while (--j >= 0)
				free(reg_dirlist[j]);
			free(reg_dirlist);

			if (xchdir(workspaces[cur_ws].path, NO_TITLE) == -1)
				xerror("search: %s: %s\n", workspaces[cur_ws].path, strerror(errno));
		}

		return exit_status;
	}

	/* We have matches */
	size_t flongest = 0, type_ok = 0;

	size_t *files_len = (size_t *)xnmalloc(found + 1, sizeof(size_t));
	int *match_type = (int *)xnmalloc(found + 1, sizeof(int));
	int longest_eln = -1;

	/* Get the longest file name in the list */
	int j = (int)found;
	while (--j >= 0) {
		/* Simply skip all files not matching file_type */
		if (file_type != 0) {
			match_type[j] = 0;

			if (search_path) {
#if !defined(_DIRENT_HAVE_D_TYPE)
				mode_t type;
				struct stat attr;
				if (lstat(reg_dirlist[regex_index[j]]->d_name, &attr) == -1)
					continue;
				switch (attr.st_mode & S_IFMT) {
				case S_IFBLK: type = DT_BLK; break;
				case S_IFCHR: type = DT_CHR; break;
				case S_IFDIR: type = DT_DIR; break;
				case S_IFIFO: type = DT_FIFO; break;
				case S_IFLNK: type = DT_LNK; break;
				case S_IFREG: type = DT_REG; break;
				case S_IFSOCK: type = DT_SOCK; break;
				default: type = DT_UNKNOWN; break;
				}
				if (type != file_type)
#else
				if (reg_dirlist[regex_index[j]]->d_type != file_type)
#endif
					continue;
			} else {
				if (file_info[regex_index[j]].type != file_type)
					continue;
			}
		}

		/* Amount of non-filtered files */
		type_ok++;
		/* Index of each non-filtered file */
		match_type[j] = 1;

		/* If not searching in CWD, we only need to know the file's
		 * length (no ELN) */
		if (search_path) {
			files_len[j] = wc_xstrlen(reg_dirlist[regex_index[j]]->d_name);

			if (files_len[j] > flongest)
				flongest = files_len[j];
		} else {
			/* If searching in CWD, take into account the file's ELN
			 * when calculating its length */
			files_len[j] = wc_xstrlen(file_info[regex_index[j]].name)
					+ (size_t)DIGINUM(regex_index[j] + 1) + 1;

			if (files_len[j] > flongest) {
				flongest = files_len[j];
				longest_eln = regex_index[j] + 1;
			}
		}
	}

	if (type_ok != 0) {
		int last_column = 0;
		size_t total_cols = 0;

		struct winsize w;
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
		unsigned short terminal_cols = w.ws_col;

		int eln_pad = 0;
		if (!search_path) {
#ifndef _NO_ICONS
			if (conf.icons == 1)
				flongest += 3;
#endif

			int largest = 0;
			j = (int)found;
			while (--j >= 0) {
				if (match_type[j] == 0)
					continue;
				if (regex_index[j] + 1 > largest)
					largest = regex_index[j] + 1;
			}
			eln_pad = DIGINUM(largest);

			if (longest_eln > -1)
				flongest += (size_t)(eln_pad - DIGINUM(longest_eln));
		}

		if (flongest == 0 || flongest > terminal_cols)
			total_cols = 1;
		else
			total_cols = (size_t)terminal_cols / (flongest + 1);

		if (total_cols > type_ok)
			total_cols = type_ok;

		/* cur_col: Current columns number */
		size_t cur_col = 0, counter = 0;
		size_t t = tab_offset;
		tab_offset = 0;
		for (i = 0; i < found; i++) {
			if (match_type[i] == 0)
				continue;
			/* Print the results using colors and columns */
			cur_col++;

			/* If the current file is in the last column or is the last
			 * listed file, we need to print no pad and a newline char.
			 * Else, print the corresponding pad, to equate the longest
			 * file length, and no newline char */
			if (cur_col == total_cols) {
				last_column = 1;
				cur_col = 0;
			} else {
				last_column = 0;
			}

			/* Counter: Current amount of non-filtered files: if
			 * COUNTER equals TYPE_OK (total amount of non-filtered
			 * files), we have the last file to be printed */
			counter++;

			char ind_chr = file_info[regex_index[i]].sel == 1 ? SELFILE_CHR : ' ';
			char *ind_chr_color = file_info[regex_index[i]].sel == 1 ? li_cb : "";

			if (!search_path) {
				printf("%s%*d%s%s%c%s%s%s%s%c", el_c, eln_pad,
					regex_index[i] + 1, df_c, ind_chr_color, ind_chr, df_c,
#ifndef _NO_ICONS
					conf.icons == 1 ? file_info[regex_index[i]].icon_color : "",
					conf.icons == 1 ? file_info[regex_index[i]].icon : "",
					df_c, conf.icons == 1 ? ' ' : 0);
#else
					"", "", df_c, 0);
#endif
			}

			int name_pad = (last_column == 1 || counter == type_ok) ? NO_PAD :
				(int)(flongest - files_len[i] - ( search_path ? 0
				: (size_t)(eln_pad - DIGINUM(regex_index[i] + 1)) ) + 1);

			if (name_pad < 0)
				name_pad = 0;

			colors_list(search_path ? reg_dirlist[regex_index[i]]->d_name
				: file_info[regex_index[i]].name,
				NO_ELN, name_pad,
				(last_column == 1 || counter == type_ok) ? PRINT_NEWLINE
				: NO_NEWLINE);

/*			colors_list(search_path ? reg_dirlist[regex_index[i]]->d_name
					: file_info[regex_index[i]].name,
					search_path ? NO_ELN : regex_index[i] + 1,
					(last_column || counter == type_ok) ? NO_PAD
					: (int)(flongest - files_len[i]) + 1,
					(last_column || counter == type_ok) ? PRINT_NEWLINE
					: NO_NEWLINE); */
		}
		tab_offset = t;

		print_reload_msg(_("Matches found: %zu\n"), counter);
	} else {
		fputs(_("No matches found\n"), stderr);
	}

	/* Free stuff */
	free(files_len);
	free(match_type);
	free(regex_index);

	/* If needed, go back to the directory we came from */
	if (search_path) {
		j = tmp_files;
		while (--j >= 0)
			free(reg_dirlist[j]);
		free(reg_dirlist);

		if (xchdir(workspaces[cur_ws].path, NO_TITLE) == -1) {
			xerror("search: %s: %s\n", workspaces[cur_ws].path, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	return type_ok != 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* We have three search strategies:
 * 1 - Only glob
 * 2 - Only regex
 * 3 - Glob-regex */
int
search_function(char **args)
{
	if (args[1] && IS_HELP(args[1])) {
		puts(SEARCH_USAGE);
		return EXIT_SUCCESS;
	}

	int invert = args[0][1] == '!' ? 1 : 0;

	if (conf.search_strategy != REGEX_ONLY) {
		int ret = search_glob(args, invert);
		if (ret != EXIT_FAILURE)
			return (ret == 2 ? 1 : ret);

		if (conf.search_strategy == GLOB_ONLY) {
			char *s = (conf.autocd == 1 && !args[1]
				&& (search_flags & NO_GLOB_CHAR)
				&& rl_line_buffer) ? strrchr(rl_line_buffer, '/')
				: (char *)NULL;

			if (s && s != rl_line_buffer) {
				/* Input string looks like a path: let's err like it was */
				fprintf(stderr, _("cd: %s: %s\n"),
					rl_line_buffer, strerror(ENOENT));
				return ENOENT;
			} else {
				fputs(_("search: No matches found\n"), stderr);
				return EXIT_FAILURE;
			}
		}

		if (!(search_flags & NO_GLOB_CHAR))
			fputs(_("Glob: No matches found. Trying regex...\n"), stderr);
	}

	return search_regex(args, invert, conf.case_sens_search == 1 ? 1 : 0);
}
