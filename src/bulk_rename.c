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

/* bulk_rename.c -- bulk rename files */

/* The run_action function is taken from NNN's run_selected_plugin function
 * (https://github.com/jarun/nnn/blob/master/src/nnn.c), licensed under BSD-2-Clause.
 * All changes are licensed under GPL-2.0-or-later. */

#include "helpers.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#if defined(MAC_OS_X_RENAMEAT_SYS_STDIO_H)
# include <sys/stdio.h> /* renameat(2) */
#endif /* MAC_OS_X_RENAMEAT_SYS_STDIO_H */

#include "aux.h" /* press_any_key_to_continue(), abbreviate_file_name(), open_fread() */
#include "checks.h" /* is_file_in_cwd() */
#include "file_operations.h" /* open_file() */
#include "init.h" /* get_sel_files() */
#include "listing.h" /* reload_dirlist() */
#include "messages.h" /* BULK_RENAME_USAGE */
#include "misc.h" /* xerror(), print_reload_msg() */
#include "readline.h" /* rl_get_y_or_n() */
#include "spawn.h" /* launch_execv() */

#define BULK_RENAME_TMP_FILE_HEADER "# Clifm - Rename files in bulk\n\
# Edit filenames, save, and quit the editor (you will be\n\
# asked for confirmation).\n\
# Quit the editor without saving to cancel the operation.\n\n"

#define IS_BR_COMMENT(l) (*(l) == '#' && (l)[1] == ' ')

/* Error opening tmp file FILE. Err accordingly. */
static int
err_open_tmp_file(const char *file, const int fd)
{
	xerror("br: open: '%s': %s\n", file, strerror(errno));
	if (unlinkat(fd, file, 0) == -1)
		xerror("br: unlink: '%s': %s\n", file, strerror(errno));
	close(fd);

	return FUNC_FAILURE;
}

/* Rename OLDPATH as NEWPATH.
 * NEWPATH is checked for existence before renaming, in which case the user
 * is asked for confirmation. */
static int
rename_file(char *oldpath, char *newpath)
{
	/* Some renameat(2) implementations (DragonFly) do not like NEWPATH to
	 * end with a slash (in case of renaming directories). */
	size_t nlen = strlen(newpath);
	if (nlen > 1 && newpath[nlen - 1] == '/') {
		nlen--;
		newpath[nlen] = '\0';
	}

	char *npath = normalize_path(newpath, nlen);
	if (!npath || !*npath) {
		free(npath);
		xerror(_("br: '%s': Error normalizing path\n"), newpath);
		return FUNC_FAILURE;
	}

	struct stat a;
	if (lstat(npath, &a) == 0) {
		xerror("br: '%s': %s\n", newpath, strerror(EEXIST));
		if (rl_get_y_or_n(_("Overwrite this file?"),
		conf.default_answer.overwrite) == 0) {
			free(npath);
			return EEXIST;
		}
	}

	if (renameat(XAT_FDCWD, oldpath, XAT_FDCWD, npath) == 0) {
		free(npath);
		return 0;
	}

	if (errno != EXDEV) {
		xerror(_("br: Cannot rename '%s' to '%s': %s\n"), oldpath,
			newpath, strerror(errno));
		free(npath);
		return errno;
	}

	char *cmd[] = {"mv", "--", oldpath, npath, NULL};
	const int ret = launch_execv(cmd, FOREGROUND, E_NOFLAG);
	free(npath);
	return ret;
}

/* Write filenames in ARGS into the temporary file TMPFILE, whose file
 * descriptor is FD. Update WRITTEN to the number of actually written
 * filenames, and make ATTR hold the stat attributes of the temporary
 * file after writing into it all filenames. */
static int
write_files_to_tmp(char ***args, const char *tmpfile, const int fd,
	struct stat *attr, size_t *written)
{
	size_t i;
	FILE *fp = fdopen(fd, "w");
	if (!fp)
		return err_open_tmp_file(tmpfile, fd);

	fprintf(fp, BULK_RENAME_TMP_FILE_HEADER);

	/* Copy all files to be renamed into the bulk file */
	for (i = 1; (*args)[i]; i++) {
		/* Dequote filename, if necessary */
		if (strchr((*args)[i], '\\')) {
			char *deq_file = unescape_str((*args)[i], 0);
			if (!deq_file) {
				xerror(_("br: '%s': Error unescaping filename\n"), (*args)[i]);
				press_any_key_to_continue(0);
				continue;
			}

			xstrsncpy((*args)[i], deq_file, strlen(deq_file) + 1);
			free(deq_file);
		}

		/* Resolve "./" and "../" */
		if (*(*args)[i] == '.' && ((*args)[i][1] == '/' || ((*args)[i][1] == '.'
		&& (*args)[i][2] == '/') ) ) {
			char *p = normalize_path((*args)[i], strlen((*args)[i]));
			if (!p) {
				xerror(_("br: '%s': Error normalizing path\n"), (*args)[i]);
				press_any_key_to_continue(0);
				continue;
			}
			free((*args)[i]);
			(*args)[i] = p;
		}

		struct stat a;
		if (lstat((*args)[i], &a) == -1) {
			/* Last parameter may be opening app (:APP) */
			if (i != args_n || *(*args)[i] != ':') {
				xerror("br: '%s': %s\n", (*args)[i], strerror(errno));
				press_any_key_to_continue(0);
			}
			continue;
		}

		fprintf(fp, "%s\n", (*args)[i]);
		(*written)++;
	}

	if (*written == 0 || fstat(fd, attr) == -1) {
		if (unlinkat(fd, tmpfile, 0) == -1)
			xerror("br: unlink: '%s': %s\n", tmpfile, strerror(errno));
		fclose(fp);
		return FUNC_FAILURE;
	}

	fclose(fp);
	return FUNC_SUCCESS;
}

static size_t
print_and_count_modified_names(char **args, FILE *fp)
{
	size_t modified = 0;
	size_t i = 1;
	char line[PATH_MAX + 1];

	/* Print what would be done */
	while (fgets(line, (int)sizeof(line), fp)) {
		if (!*line || *line == '\n' || IS_BR_COMMENT(line))
			continue;

		const size_t len = strlen(line);
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';

		if (args[i] && strcmp(args[i], line) != 0) {
			char *a = abbreviate_file_name(args[i]);
			char *b = abbreviate_file_name(line);

			printf("%s %s%s%s %s\n", a ? a : args[i], mi_c, SET_MSG_PTR,
				df_c, b ? b : line);

			if (a && a != args[i])
				free(a);
			if (b && b != line)
				free(b);

			modified++;
		}

		i++;
	}

	fseek(fp, 0L, SEEK_SET);

	if (modified == 0)
		puts(_("br: Nothing to do"));

	return modified;
}

/* Open FILE via APP (default associated application for text files if
 * omitted). */
static int
open_tmpfile(char *app, char *file)
{
	char *application = (char *)NULL;
	struct stat a;

	if (app && *app == ':' && app[1] && lstat(app, &a) == -1)
		application = app + 1;

	if (application) {
		char *cmd[] = {application, file, NULL};
		const int ret = launch_execv(cmd, FOREGROUND, E_NOFLAG);

		if (ret != FUNC_SUCCESS)
			unlink(file);

		return ret;
	}

	open_in_foreground = 1;
	const int exit_status = open_file(file);
	open_in_foreground = 0;

	if (exit_status != FUNC_SUCCESS) {
		xerror("br: %s\n", errno != 0
			? strerror(errno) : _("Error opening temporary file"));

		if (unlink(file) == -1)
			xerror("br: unlink: '%s': %s\n", file, strerror(errno));

		return exit_status;
	}

	return FUNC_SUCCESS;
}

static int
check_line_mismatch(FILE *fp, const size_t total)
{
	size_t modified = 0;
	char line[PATH_MAX + 1];

	while (fgets(line, (int)sizeof(line), fp)) {
		if (!*line || *line == '\n' || IS_BR_COMMENT(line))
			continue;

		modified++;
	}

	fseek(fp, 0L, SEEK_SET);

	if (total != modified) {
		xerror("%s\n", _("br: Line mismatch in temporary file"));
		return FUNC_FAILURE;
	}

	return FUNC_SUCCESS;
}

static int
rename_bulk_files(char **args, FILE *fp, int *is_cwd, size_t *renamed,
	const size_t modified)
{
	size_t i = 1;
	int exit_status = FUNC_SUCCESS;
	char line[PATH_MAX + 1]; *line = '\0';

	while (fgets(line, (int)sizeof(line), fp)) {
		if (!*line || *line == '\n' || IS_BR_COMMENT(line))
			continue;

		if (!args[i])
			goto CONT;

		const size_t len = strlen(line);
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';

		if (strcmp(args[i], line) == 0)
			goto CONT;

		const int ret = rename_file(args[i], line);
		if (ret != 0) {
			if (ret != EEXIST)
				exit_status = ret;
			goto CONT;
		}

		if (*is_cwd == 0 && (is_file_in_cwd(args[i])
		|| is_file_in_cwd(line)))
			*is_cwd = 1;

		(*renamed)++;

CONT:
		i++;
	}

	if (conf.autols == 1 && exit_status != 0 && modified > 1)
		press_any_key_to_continue(0);

	return exit_status;
}

/* Return zero if no name is duplicated in the file pointed to by FP.
 * Otherwise, the number of duplicate files is returned. */
static int
check_dups(FILE *fp)
{
	char line[PATH_MAX + 1];
	*line = '\0';

	size_t n = 0, i = 0;
	int dups = 0;

	while (fgets(line, (int)sizeof(line), fp))
		n++;

	fseek(fp, 0L, SEEK_SET);
	char **fnames = xnmalloc(n + 1, sizeof(char *));

	while (fgets(line, (int)sizeof(line), fp)) {
		if (!*line || *line == '\n' || IS_BR_COMMENT(line))
			continue;

		size_t len = strlen(line);
		if (line[len - 1] == '\n') {
			len--;
			line[len] = '\0';
		}

		fnames[i] = savestring(line, len);
		i++;
	}

	fnames[i] = (char *)NULL;
	fseek(fp, 0L, SEEK_SET);

	for (i = 0; fnames[i]; i++) {
		for (n = i + 1; fnames[n]; n++) {
			if (strcmp(fnames[i], fnames[n]) == 0) {
				dups++;
				xerror(_("br: '%s' is duplicated\n"), fnames[i]);
			}
		}
	}

	for (i = 0; fnames[i]; i++)
		free(fnames[i]);
	free(fnames);

	if (dups > 0 && rl_get_y_or_n(_("Continue?"),
	conf.default_answer.overwrite) == 0) {
		return dups;
	}

	return 0;
}

/* Rename a bulk of files (ARGS) at once.
 * Takes files to be renamed as arguments, and returns zero on success or one
 * on error.
 *
 * RENAMED is updated to the number of renamed files.
 * If reload_list is set to 1, the list of files is reload and a summary
 * message is displayed.
 *
 * The procedure is simple: filenames to be renamed are copied into a
 * temporary file, which is opened via the mime function and shown to
 * the user to modify it. Once the filenames have been modified and saved,
 * modifications are printed on the screen and the user is asked wehther to
 * perform the actual bulk renaming or not. */
int
bulk_rename(char **args, size_t *renamed, const size_t reload_list)
{
	*renamed = 0;

	if (virtual_dir == 1) {
		xerror(_("%s: br: Feature not allowed in virtual "
			"directories\n"), PROGRAM_NAME);
		return FUNC_SUCCESS;
	}

	if (!args || !args[1] || IS_HELP(args[1])) {
		puts(_(BULK_RENAME_USAGE));
		return FUNC_SUCCESS;
	}

	int exit_status = FUNC_SUCCESS;
	struct stat attra; /* Original temp file. */
	struct stat attrb; /* Edited temp file. */

	char tmpfile[PATH_MAX + 1];
	snprintf(tmpfile, sizeof(tmpfile), "%s/%s",
		xargs.stealth_mode == 1 ? P_tmpdir : tmp_dir, TMP_FILENAME);

	int fd = mkstemp(tmpfile);
	if (fd == -1) {
		xerror("br: mkstemp: '%s': %s\n", tmpfile, strerror(errno));
		return FUNC_FAILURE;
	}

	/* Write files to be renamed into a tmp file and store stat info in attra. */
	size_t written = 0;
	int ret = write_files_to_tmp(&args, tmpfile, fd, &attra, &written);
	if (ret != FUNC_SUCCESS)
		return ret;

	/* Open the tmp file with the associated text editor. */
	if ((ret = open_tmpfile(args[args_n], tmpfile)) != FUNC_SUCCESS)
		return ret;

	FILE *fp;
	if (!(fp = open_fread(tmpfile, &fd)))
		return err_open_tmp_file(tmpfile, fd);

	/* Compare the new modification time to the stored one: if they
	 * match, nothing was modified. */
	if (fstat(fd, &attrb) == -1) {
		xerror("br: '%s': %s\n", tmpfile, strerror(errno));
		goto ERROR;
	}

	if (attra.st_mtime == attrb.st_mtime) {
		puts(_("br: Nothing to do"));
		goto ERROR;
	}

	/* Modification time after edition. */
	time_t mtime_bk = attrb.st_mtime;

	/* Make sure there are as many lines in the tmp file as files
	 * to be renamed. */
	if (check_line_mismatch(fp, written) != FUNC_SUCCESS) {
		exit_status = FUNC_FAILURE;
		goto ERROR;
	}

	/* Check duplicate names. */
	if (check_dups(fp) != FUNC_SUCCESS)
		goto ERROR;

	size_t modified = print_and_count_modified_names(args, fp);
	if (modified == 0)
		goto ERROR;

	/* Ask the user for confirmation. */
	if (rl_get_y_or_n(_("Continue?"), conf.default_answer.bulk_rename) == 0)
		goto ERROR;

	/* Make sure the tmp file we're about to read is the same we originally
	 * created, and that it was not modified since the user edited it
	 * (via open_tmp_file()). */
	if (fstat(fd, &attrb) == -1 || !S_ISREG(attrb.st_mode)
	|| attra.st_ino != attrb.st_ino || attra.st_dev != attrb.st_dev
	|| mtime_bk != attrb.st_mtime) {
		exit_status = FUNC_FAILURE;
		xerror("%s\n", _("br: Temporary file changed on disk! Aborting."));
		goto ERROR;
	}

	int is_cwd = 0;

	if ((ret = rename_bulk_files(args, fp, &is_cwd,
	renamed, modified)) != FUNC_SUCCESS)
		exit_status = ret;

	/* Clean stuff, report, and exit. */
	if (unlinkat(fd, tmpfile, 0) == -1) {
		exit_status = errno;
		err('w', PRINT_PROMPT, "br: unlink: '%s': %s\n",
			tmpfile, strerror(errno));
	}

	fclose(fp);

	if (sel_n > 0 && cwd_has_sel_files())
		/* Just in case a selected file in the current dir was renamed. */
		get_sel_files();

	if (reload_list == 0)
		return exit_status;

	if (*renamed > 0 && is_cwd == 1 && conf.autols == 1)
		reload_dirlist();

	print_reload_msg(SET_SUCCESS_PTR, xs_cb,
		_("%zu file(s) renamed\n"), *renamed);

	return exit_status;

ERROR:
	if (unlinkat(fd, tmpfile, 0) == -1) {
		xerror("br: unlink: '%s': %s\n", tmpfile, strerror(errno));
		exit_status = errno;
	}

	fclose(fp);
	return exit_status;
}
