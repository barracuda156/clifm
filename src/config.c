/* config.c -- functions to define, create, and set configuration files */

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

#include <errno.h>
#include <fcntl.h> /* open(2) */
#include <limits.h>
#include <stdio.h>
#include <readline/readline.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "aux.h"
#include "checks.h"
#include "colors.h"
#include "config.h"
#include "exec.h"
#include "init.h"
#include "listing.h"
#include "messages.h"
#include "misc.h"
#include "navigation.h"
#include "file_operations.h"
#include "autocmds.h"

/* Only for old log file split. Remove when needed */
#ifndef _NO_SPLIT_LOG
# include "history.h" // split_old_log_file()
#endif
////////////////////

#define DUMP_CONFIG_STR  0
#define DUMP_CONFIG_INT  1
#define DUMP_CONFIG_BOOL 2

#ifndef _NO_FZF
/* Determine input and output files to be used by the fuzzy finder (either
 * fzf or fnf)
 * Let's do this even if fzftab is not enabled at startup, because this feature
 * can be enabled in place editing the config file */
void
set_finder_paths(void)
{
	char *p = xargs.stealth_mode == 1 ? P_tmpdir : tmp_dir;
	snprintf(finder_in_file, sizeof(finder_in_file), "%s/%s.finder.in",
		p, PROGRAM_NAME);
	snprintf(finder_out_file, sizeof(finder_out_file), "%s/%s.finder.out",
		p, PROGRAM_NAME);
}
#endif /* _NO_FZF */

/* Regenerate the configuration file and create a back up of the old one */
static int
regen_config(void)
{
	int config_found = 1;
	struct stat attr;

	if (stat(config_file, &attr) == -1) {
		puts(_("No configuration file found"));
		config_found = 0;
	}

	if (config_found == 1) {
		time_t rawtime = time(NULL);
		struct tm t;
		if (!localtime_r(&rawtime, &t))
			return EXIT_FAILURE;

		char date[18] = "";
		strftime(date, sizeof(date), "%Y%m%d@%H:%M:%S", &t);

		char bk[PATH_MAX];
		snprintf(bk, sizeof(bk), "%s.%s", config_file, date);

		char *cmd[] = {"mv", config_file, bk, NULL};
		if (launch_execv(cmd, FOREGROUND, E_NOFLAG) != EXIT_SUCCESS)
			return EXIT_FAILURE;

		printf(_("Old configuration file stored as '%s'\n"), bk);
	}

	if (create_main_config_file(config_file) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	printf(_("New configuration file written to '%s'\n"), config_file);
	reload_config();
	return EXIT_SUCCESS;
}

static void
print_config_value(const char *option, void *cur_value, void *def_value,
		const int type)
{
	if (type == DUMP_CONFIG_STR) {
		char *cv = (char *)cur_value, *dv = (char *)def_value;
		if (!cv || (dv && strcmp(cv, dv) == 0))
			printf("  %s: \"%s\"\n", option, dv);
		else
			printf("%s>%s %s%s: \"%s\" [\"%s\"]%s\n", mi_c, df_c,
				BOLD, option, cv, dv, df_c);
	}

	else if (type == DUMP_CONFIG_BOOL) {
		int cv = *((int *)cur_value), dv = *((int *)def_value);
		if (cv == dv)
			printf("  %s: %s\n", option, cv == 1 ? "true" : "false");
		else
			printf("%s>%s %s%s: %s [%s]%s\n", mi_c, df_c, BOLD, option,
				cv == 1 ? "true" : "false", dv == 1 ? "true" : "false", df_c);
	}

	else { /* CONFIG_BOOL_INT */
		int cv = *((int *)cur_value), dv = *((int *)def_value);
		if (cv == dv)
			printf("  %s: %d\n", option, cv);
		else
			printf("%s>%s %s%s: %d [%d]%s\n", mi_c, df_c, BOLD, option,
				cv, dv, df_c);
	}
}

/* Return a mallo'ced pointer to a string representing the value for
 * TabCompletionMode in the config file. */
static char *
get_tab_comp_mode_str(void)
{
	char *s = (char *)xnmalloc(9, sizeof(char));

	switch (tabmode) {
	case FZF_TAB: xstrsncpy(s, "fzf", 4); break;
	case FNF_TAB: xstrsncpy(s, "fnf", 4); break;
	case SMENU_TAB: xstrsncpy(s, "smenu", 6); break;
	case STD_TAB: xstrsncpy(s, "standard", 9); break;
	default: free(s); s = (char *)NULL; break;
	}

	return s;
}

static void
get_start_path_and_ws_names(char **sp, char **ws)
{
	if (config_ok == 0 || !config_file)
		return;

	int fd;
	FILE *fp = open_fread(config_file, &fd);
	if (!fp)
		return;

	char line[PATH_MAX + 15];
	while (fgets(line, (int)sizeof(line), fp)) {
		if (*line == 'W' && strncmp(line, "WorkspaceNames=", 15) == 0
		&& *(line + 15)) {
			char *tmp = remove_quotes(line + 15);
			if (!tmp || !*tmp)
				continue;

			*ws = savestring(tmp, strlen(tmp));
		}

		if (*line == 'S' && strncmp(line, "StartingPath=", 13) == 0
		&& *(line + 13)) {
			char *tmp = remove_quotes(line + 13);
			if (!tmp || !*tmp)
				continue;

			*sp = savestring(tmp, strlen(tmp));
		}
	}

	fclose(fp);
}

/* Dump current value of config options (as defined in the config file),
 * highlighting those that differ from default values.
 * Note that values displayed here represent the CURRENT status of the
 * corresponding option, and not necessarily that of the config file:
 * some of these options can be changed on the fly via commands */
static int
dump_config(void)
{
	puts(_("The following is the list of options (as defined in the configuration "
		"file) and their current values. Whenever a current value differs "
		"from the default value, the entry is highlighted and the default "
		"value is displayed in brackets\n"));

	char *start_path = (char *)NULL, *ws_names = (char *)NULL;
	get_start_path_and_ws_names(&start_path, &ws_names);

	char *s = (char *)NULL;
	int n = 0;

	n = DEF_APPARENT_SIZE;
	print_config_value("ApparentSize", &conf.apparent_size, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_AUTOCD;
	print_config_value("Autocd", &conf.autocd, &n, DUMP_CONFIG_BOOL);

	n = DEF_AUTOLS;
	print_config_value("AutoLs", &conf.autols, &n, DUMP_CONFIG_BOOL);

	n = DEF_AUTO_OPEN;
	print_config_value("AutoOpen", &conf.auto_open, &n, DUMP_CONFIG_BOOL);

#ifndef _NO_SUGGESTIONS
	n = DEF_SUGGESTIONS;
	print_config_value("AutoSuggestions", &conf.suggestions, &n,
		DUMP_CONFIG_BOOL);
#endif

	n = DEF_CASE_SENS_DIRJUMP;
	print_config_value("CaseSensitiveDirjump", &conf.case_sens_dirjump,
		&n, DUMP_CONFIG_BOOL);

	n = DEF_CASE_SENS_LIST;
	print_config_value("CaseSensitiveList", &conf.case_sens_list, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_CASE_SENS_PATH_COMP;
	print_config_value("CaseSensitivePathComp", &conf.case_sens_path_comp,
		&n, DUMP_CONFIG_BOOL);

	n = DEF_CASE_SENS_SEARCH;
	print_config_value("CaseSensitiveSearch", &conf.case_sens_search, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_CD_ON_QUIT;
	print_config_value("CdOnQuit", &conf.cd_on_quit, &n, DUMP_CONFIG_BOOL);

	n = DEF_CLASSIFY;
	print_config_value("Classify", &conf.classify, &n, DUMP_CONFIG_BOOL);

	n = DEF_CLEAR_SCREEN;
	print_config_value("ClearScreen", &conf.clear_screen, &n, DUMP_CONFIG_BOOL);

	n = DEF_COLOR_LNK_AS_TARGET;
	print_config_value("ColorLinksAsTarget", &conf.color_lnk_as_target,
		&n, DUMP_CONFIG_BOOL);

	s = term_caps.color < 256 ? DEF_COLOR_SCHEME : DEF_COLOR_SCHEME_256;
	print_config_value("ColorScheme", cur_cscheme, s, DUMP_CONFIG_STR);

	n = DEF_CP_CMD;
	print_config_value("cpCmd", &conf.cp_cmd, &n, DUMP_CONFIG_INT);

	n = DEF_DESKTOP_NOTIFICATIONS;
	print_config_value("DesktopNotifications", &conf.desktop_notifications,
		&n, DUMP_CONFIG_BOOL);

	n = DEF_DIRHIST_MAP;
	print_config_value("DirhistMap", &conf.dirhist_map, &n, DUMP_CONFIG_BOOL);

	n = DEF_DISK_USAGE;
	print_config_value("DiskUsage", &conf.disk_usage, &n, DUMP_CONFIG_BOOL);

	n = DEF_EXT_CMD_OK;
	print_config_value("ExternalCommands", &conf.ext_cmd_ok, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_FILES_COUNTER;
	print_config_value("FilesCounter", &conf.files_counter, &n,
		DUMP_CONFIG_BOOL);

	s = "";
	print_config_value("Filter", filter.str, s, DUMP_CONFIG_STR);

	n = DEF_FULL_DIR_SIZE;
	print_config_value("FullDirSize", &conf.full_dir_size, &n, DUMP_CONFIG_BOOL);

#ifndef _NO_FZF
	n = DEF_FUZZY_MATCH;
	print_config_value("FuzzyMatching", &conf.fuzzy_match, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_FUZZY_MATCH_ALGO;
	print_config_value("FuzzyAlgorithm", &conf.fuzzy_match_algo, &n,
		DUMP_CONFIG_INT);

	n = DEF_FZF_PREVIEW;
	print_config_value("FzfPreview", &conf.fzf_preview, &n, DUMP_CONFIG_BOOL);
#endif

#ifndef _NO_ICONS
	n = DEF_ICONS;
	print_config_value("Icons", &conf.icons, &n, DUMP_CONFIG_BOOL);
#endif

	n = DEF_LIGHT_MODE;
	print_config_value("LightMode", &conf.light_mode, &n, DUMP_CONFIG_BOOL);

	n = DEF_LIST_DIRS_FIRST;
	print_config_value("ListDirsFirst", &conf.list_dirs_first, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_LISTING_MODE;
	print_config_value("ListingMode", &conf.listing_mode, &n, DUMP_CONFIG_INT);

	n = DEF_LOG_CMDS;
	print_config_value("LogCmds", &conf.log_cmds, &n, DUMP_CONFIG_BOOL);

	n = DEF_LOG_MSGS;
	print_config_value("LogMsgs", &conf.log_msgs, &n, DUMP_CONFIG_BOOL);

	n = DEF_LONG_VIEW;
	print_config_value("LongViewMode", &conf.long_view, &n, DUMP_CONFIG_BOOL);

	n = DEF_MAX_DIRHIST;
	print_config_value("MaxDirhist", &conf.max_dirhist, &n, DUMP_CONFIG_INT);

	n = DEF_MAX_NAME_LEN;
	print_config_value("MaxFilenameLen", &conf.max_name_len, &n,
		DUMP_CONFIG_INT);

	n = DEF_MAX_HIST;
	print_config_value("MaxHistory", &conf.max_hist, &n, DUMP_CONFIG_INT);

	n = DEF_MAX_JUMP_TOTAL_RANK;
	print_config_value("MaxJumpTotalRank", &conf.max_jump_total_rank,
		&n, DUMP_CONFIG_INT);

	n = DEF_MAX_LOG;
	print_config_value("MaxLog", &conf.max_log, &n, DUMP_CONFIG_INT);

	n = DEF_MAX_PATH;
	print_config_value("MaxPath", &conf.max_path, &n, DUMP_CONFIG_INT);

	n = DEF_MAX_PRINTSEL;
	print_config_value("MaxPrintSelfiles", &conf.max_printselfiles, &n,
		DUMP_CONFIG_INT);

	n = DEF_MIN_NAME_TRIM;
	print_config_value("MinFilenameTrim", &conf.min_name_trim, &n,
		DUMP_CONFIG_INT);

	n = DEF_MIN_JUMP_RANK;
	print_config_value("MinJumpRank", &conf.min_jump_rank, &n, DUMP_CONFIG_INT);

	n = DEF_MV_CMD;
	print_config_value("mvCmd", &conf.mv_cmd, &n, DUMP_CONFIG_INT);

	s = "";
	print_config_value("Opener", conf.opener, s, DUMP_CONFIG_STR);

	n = DEF_PAGER;
	print_config_value("Pager", &conf.pager, &n, conf.pager > 1
		? DUMP_CONFIG_INT : DUMP_CONFIG_BOOL);

	n = DEF_PRINTSEL;
	print_config_value("PrintSelfiles", &conf.print_selfiles, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_PRIVATE_WS_SETTINGS;
	print_config_value("PrivateWorkspaceSettings", &conf.private_ws_settings,
		&n, DUMP_CONFIG_BOOL);

	s = DEF_PROP_FIELDS;
	print_config_value("PropFields", prop_fields_str, s, DUMP_CONFIG_STR);

	n = DEF_PURGE_JUMPDB;
	print_config_value("PurgeJumpDB", &conf.purge_jumpdb, &n, DUMP_CONFIG_BOOL);

	n = DEF_RESTORE_LAST_PATH;
	print_config_value("RestoreLastPath", &conf.restore_last_path, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_RL_EDIT_MODE;
	print_config_value("RlEditMode", &rl_editing_mode, &n, DUMP_CONFIG_INT);

	n = DEF_RM_FORCE;
	print_config_value("rmForce", &conf.rm_force, &n, DUMP_CONFIG_BOOL);

	n = DEF_SEARCH_STRATEGY;
	print_config_value("SearchStrategy", &conf.search_strategy, &n,
		DUMP_CONFIG_INT);

	n = DEF_SHARE_SELBOX;
	print_config_value("ShareSelbox", &conf.share_selbox, &n, DUMP_CONFIG_BOOL);

	n = DEF_SHOW_HIDDEN;
	print_config_value("ShowHiddenFiles", &conf.show_hidden, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_SORT;
	print_config_value("Sort", &conf.sort, &n, DUMP_CONFIG_INT);

	n = DEF_SORT_REVERSE;
	print_config_value("SortReverse", &conf.sort_reverse, &n, DUMP_CONFIG_BOOL);

	n = DEF_SPLASH_SCREEN;
	print_config_value("SplashScreen", &conf.splash_screen, &n,
		DUMP_CONFIG_BOOL);

	s = "";
	print_config_value("StartingPath", start_path, s, DUMP_CONFIG_STR);
	free(start_path);

#ifndef _NO_SUGGESTIONS
	n = DEF_CMD_DESC_SUG;
	print_config_value("SuggestCmdDesc", &conf.cmd_desc_sug, &n,
		DUMP_CONFIG_BOOL);

	n = DEF_SUG_FILETYPE_COLOR;
	print_config_value("SuggestFiletypeColor", &conf.suggest_filetype_color,
		&n, DUMP_CONFIG_BOOL);

	s = DEF_SUG_STRATEGY;
	print_config_value("SuggestionStrategy", conf.suggestion_strategy,
		s, DUMP_CONFIG_STR);
#endif
#ifndef _NO_HIGHLIGHT
	n = DEF_HIGHLIGHT;
	print_config_value("SyntaxHighlighting", &conf.highlight, &n,
		DUMP_CONFIG_BOOL);
#endif

	char *ss = get_tab_comp_mode_str();
	print_config_value("TabCompletionMode", ss,
#ifndef _NO_FZF
		(bin_flags & FZF_BIN_OK) ? "fzf" : "standard",
#else
		"standard",
#endif
		DUMP_CONFIG_STR);
	free(ss);

	s = DEF_TERM_CMD;
	print_config_value("TerminalCmd", conf.term, s, DUMP_CONFIG_STR);

	s = "";
	print_config_value("TimeStyle", conf.time_str, s, DUMP_CONFIG_STR);

	n = DEF_TIPS;
	print_config_value("Tips", &conf.tips, &n, DUMP_CONFIG_BOOL);

#ifndef _NO_TRASH
	n = DEF_TRASRM;
	print_config_value("TrashAsRm", &conf.tr_as_rm, &n, DUMP_CONFIG_BOOL);
#endif

	n = DEF_TRIM_NAMES;
	print_config_value("TrimNames", &conf.trim_names, &n, DUMP_CONFIG_BOOL);

	n = DEF_UNICODE;
	print_config_value("Unicode", &conf.unicode, &n, DUMP_CONFIG_BOOL);

	n = DEF_WELCOME_MESSAGE;
	print_config_value("WelcomeMessage", &conf.welcome_message, &n,
		DUMP_CONFIG_BOOL);

	s = DEF_WELCOME_MESSAGE_STR;
	print_config_value("WelcomeMessageStr", conf.welcome_message_str,
		s, DUMP_CONFIG_STR);

	s = "";
	print_config_value("WorkspaceNames", ws_names, s, DUMP_CONFIG_STR);
	free(ws_names);

	return EXIT_SUCCESS;
}

/* Edit the config file, either via the mime function or via the first
 * passed argument (Ex: 'edit nano'). The 'reset' option regenerates
 * the configuration file and creates a back up of the old one */
int
edit_function(char **args)
{
	if (xargs.stealth_mode == 1) {
		printf("%s: %s\n", PROGRAM_NAME, STEALTH_DISABLED);
		return EXIT_SUCCESS;
	}

	if (*args[0] == 'e') {
		_err('n', PRINT_PROMPT, "%s: The 'edit' command is deprecated. "
			"Use 'config' instead\n", PROGRAM_NAME);
	}

	if (args[1] && IS_HELP(args[1])) {
		printf("%s\n", EDIT_USAGE);
		return EXIT_SUCCESS;
	}

	if (args[1] && *args[1] == 'd' && strcmp(args[1], "dump") == 0)
		return dump_config();

	if (args[1] && *args[1] == 'r' && strcmp(args[1], "reset") == 0)
		return regen_config();

	if (config_ok == 0) {
		xerror(_("%s: Cannot access the configuration file\n"), PROGRAM_NAME);
		return EXIT_FAILURE;
	}

	char *opening_app = args[1];
	if (args[1] && strcmp(args[1], "edit") == 0)
		opening_app = args[2];

	/* Get modification time of the config file before opening it */
	struct stat attr;

	/* If, for some reason (like someone erasing the file while the program
	 * is running) clifmrc doesn't exist, recreate the configuration file.
	 * Then run 'stat' again to reread the attributes of the file */
	if (stat(config_file, &attr) == -1) {
		create_main_config_file(config_file);
		stat(config_file, &attr);
	}

	time_t mtime_bfr = (time_t)attr.st_mtime;
	int ret = EXIT_SUCCESS;

	/* If there is an argument... */
	if (opening_app) {
		char *cmd[] = {opening_app, config_file, NULL};
		ret = launch_execv(cmd, FOREGROUND, E_NOFLAG);
	} else {
		/* If no application was passed as 2nd argument */
		open_in_foreground = 1;
		ret = open_file(config_file);
		open_in_foreground = 0;
	}

	if (ret != EXIT_SUCCESS)
		return ret;

	/* Get modification time after opening the config file */
	stat(config_file, &attr);
	/* If modification times differ, the file was modified after being opened */
	if (mtime_bfr != (time_t)attr.st_mtime) {
		/* Reload configuration only if the config file was modified */
		reload_config();

		if (conf.autols == 1) {
			free_dirlist();
			ret = list_dir();
		}
		print_reload_msg(_(CONFIG_FILE_UPDATED));
	}

	return ret;
}

/* Find the plugins-helper file and set CLIFM_PLUGINS_HELPER accordingly.
 * This envionment variable will be used by plugins. Returns zero on
 * success or one on error.
 * Try first the plugins directory.
 * If not there, try the data dir.
 * Else, try some standard locations. */
static int
setenv_plugins_helper(void)
{
	if (getenv("CLIFM_PLUGINS_HELPER"))
		return EXIT_SUCCESS;

	struct stat attr;
	if (plugins_dir && *plugins_dir) {
		char _path[PATH_MAX];
		snprintf(_path, sizeof(_path), "%s/plugins-helper", plugins_dir);

		if (stat(_path, &attr) != -1
		&& setenv("CLIFM_PLUGINS_HELPER", _path, 1) == 0)
			return EXIT_SUCCESS;
	}

	if (data_dir && *data_dir) {
		char _path[PATH_MAX];
		snprintf(_path, sizeof(_path), "%s/%s/plugins/plugins-helper",
			data_dir, PROGRAM_NAME);

		if (stat(_path, &attr) != -1
		&& setenv("CLIFM_PLUGINS_HELPER", _path, 1) == 0)
			return EXIT_SUCCESS;
	}

	char home_local[PATH_MAX];
	*home_local = '\0';
	if (user.home && *user.home) {
		snprintf(home_local, sizeof(home_local),
			"%s/.local/share/clifm/plugins/plugins-helper", user.home);
	}

	const char *const _paths[] = {
		home_local,
#if defined(__HAIKU__)
		"/boot/system/non-packaged/data/clifm/plugins/plugins-helper",
		"/boot/system/data/clifm/plugins/plugins-helper",
#elif defined(__TERMUX__)
		"/data/data/com.termux/files/usr/share/clifm/plugins/plugins-helper",
		"/data/data/com.termux/files/usr/local/share/clifm/plugins/plugins-helper",
#else
		"/usr/share/clifm/plugins/plugins-helper",
		"/usr/local/share/clifm/plugins/plugins-helper",
		"/opt/local/share/clifm/plugins/plugins-helper",
		"/opt/share/clifm/plugins/plugins-helper",
#endif /* __HAIKU__ */
		NULL};

	size_t i;
	for (i = 0; _paths[i]; i++) {
		if (stat(_paths[i], &attr) != -1
		&& setenv("CLIFM_PLUGINS_HELPER", _paths[i], 1) == 0)
			return EXIT_SUCCESS;
	}

	return EXIT_FAILURE;
}

/* Set a few environment variables, mostly useful to run custom scripts
 * via the actions function */
void
set_env(void)
{
	if (xargs.stealth_mode == 1)
		return;

	setenv("CLIFM", config_dir ? config_dir : "1", 1);
	setenv("CLIFM_PROFILE", alt_profile ? alt_profile : "default", 1);

	char t[NAME_MAX];
	snprintf(t, sizeof(t), "%d", (int)own_pid);
	setenv("CLIFM_PID", t, 1);
	snprintf(t, sizeof(t), "%s", VERSION);
	setenv("CLIFM_VERSION", t, 1);

	if (sel_file)
		setenv("CLIFM_SELFILE", sel_file, 1);

	setenv_plugins_helper();
}

/* Define the file for the Selection Box */
void
set_sel_file(void)
{
	if (xargs.sel_file == 1) /* Already set via --sel-file flag */
		return;

	if (sel_file) {
		free(sel_file);
		sel_file = (char *)NULL;
	}

	if (!config_dir)
		return;

	size_t len = 0;
	if (conf.share_selbox == 0) {
		/* Private selection box is stored in the profile directory */
		len = config_dir_len + 14;
		sel_file = (char *)xnmalloc(len, sizeof(char));
		snprintf(sel_file, len, "%s/selbox.clifm", config_dir);
	} else {
		/* Shared selection box is stored in the general config dir */
		len = config_dir_len + 23;
		sel_file = (char *)xnmalloc(len, sizeof(char));
		snprintf(sel_file, len, "%s/.config/%s/selbox.clifm", user.home,
			PROGRAM_NAME);
	}

	return;
}

static int
import_from_data_dir(const char *src_filename, char *dest)
{
	if (!data_dir || !src_filename || !dest
	|| !*data_dir || !*src_filename || !*dest)
		return EXIT_FAILURE;

	struct stat attr;
	char sys_file[PATH_MAX];
	snprintf(sys_file, sizeof(sys_file), "%s/%s/%s", data_dir, PROGRAM_NAME,
		src_filename);
	if (stat(sys_file, &attr) == -1)
		return EXIT_FAILURE;

	char *cmd[] = {"cp", sys_file, dest, NULL};
	int ret = launch_execv(cmd, FOREGROUND, E_NOFLAG);
	if (ret == EXIT_SUCCESS) {
		/* Make sure config files have always 600 permissions. */
		xchmod(dest, "0600", 1);
		return EXIT_SUCCESS;
	}

	return EXIT_FAILURE;
}

int
create_kbinds_file(void)
{
	if (config_ok == 0 || !kbinds_file)
		return EXIT_FAILURE;

	struct stat attr;
	/* If the file already exists, do nothing */
	if (stat(kbinds_file, &attr) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	/* If not, try to import it from DATADIR */
	if (import_from_data_dir("keybindings.clifm", kbinds_file) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	/* Else, create it */
	int fd = 0;
	FILE *fp = open_fwrite(kbinds_file, &fd);
	if (!fp) {
		_err('w', PRINT_PROMPT, "%s: '%s': %s\n", PROGRAM_NAME, kbinds_file,
			strerror(errno));
		return EXIT_FAILURE;
	}

	fprintf(fp, "# Keybindings file for %s\n\n\
# Emacs style key escapes are the simplest way of setting your \n\
# keybindings. For example, use \"action:\\C-t\" to bind the action name \n\
# 'action' to Ctrl-t \n\
# Note: available action names are defined below \n\n\
# Use the 'kbgen' plugin (compile it first: gcc -o kbgen kbgen.c) to \n\
# find out the escape code for the key or key sequence you want. Use \n\
# either octal, hexadecimal codes or symbols.\n\
# Ex: For Alt-/ (in rxvt terminals) 'kbgen' will print the following \n\
# lines:\n\
# Hex  | Oct | Symbol\n\
# ---- | ---- | ------\n\
# \\x1b | \\033 | ESC (\\e)\n\
# \\x2f | \\057 | /\n\
# In this case, the keybinding, if using symbols, is: \"\\e/:function\"\n\
# In case you prefer the hex codes it would be: \\x1b\\x2f:function.\n\
# GNU emacs escape sequences are also allowed (ex: \"\\M-a\", Alt-a\n\
# in most keyboards, or \"\\C-r\" for Ctrl-r).\n\
# Some codes, especially those involving keys like Ctrl or the arrow\n\
# keys, vary depending on the terminal emulator and the system settings.\n\
# These keybindings should be set up thus on a per terminal basis.\n\
# You can also consult the terminfo database via the infocmp command.\n\
# See terminfo(5) and infocmp(1).\n\
\n\
# Alt-j\n\
previous-dir:\\M-j\n\
# Shift-left (rxvt)\n\
previous-dir2:\\e[d\n\
# Shift-left (xterm)\n\
previous-dir3:\\e[2D\n\
# Shift-left (others)\n\
previous-dir4:\\e[1;2D\n\
\n\
# Alt-k\n\
next-dir:\\M-k\n\
# Shift-right (rxvt)\n\
next-dir2:\\e[c\n\
# Shift-right (xterm)\n\
next-dir3:\\e[2C\n\
# Shift-right (others)\n\
next-dir4:\\e[1;2C\n\
first-dir:\\C-\\M-j\n\
last-dir:\\C-\\M-k\n\
\n\
# Alt-u\n\
parent-dir:\\M-u\n\
# Shift-up (rxvt)\n\
parent-dir2:\\e[a\n\
# Shift-up (xterm)\n\
parent-dir3:\\e[2A\n\
# Shift-up (others)\n\
parent-dir4:\\e[1;2A\n\
\n\
# Alt-e\n\
home-dir:\\M-e\n\
# Home key (rxvt)\n\
#home-dir2:\\e[7~\n\
# Home key (xterm)\n\
#home-dir3:\\e[H\n\
# Home key (Emacs term)\n\
#home-dir4:\\e[1~\n\
\n\
# Alt-r\n\
root-dir:\\M-r\n\
# Alt-/ (rxvt)\n\
root-dir2:\\e/\n\
#root-dir3:\n\
\n\
pinned-dir:\\M-p\n\
workspace1:\\M-1\n\
workspace2:\\M-2\n\
workspace3:\\M-3\n\
workspace4:\\M-4\n\
\n\
# Help\n\
# F1-3\n\
show-manpage:\\eOP\n\
show-manpage2:\\e[11~\n\
show-cmds:\\eOQ\n\
show-cmds2:\\e[12~\n\
show-kbinds:\\eOR\n\
show-kbinds2:\\e[13~\n\n\
archive-sel:\\C-\\M-a\n\
bookmark-sel:\\C-\\M-b\n\
bookmarks:\\M-b\n\
clear-line:\\M-c\n\
clear-msgs:\\M-t\n\
create-file:\\M-n\n\
deselect-all:\\M-d\n\
export-sel:\\C-\\M-e\n\
dirs-first:\\M-g\n\
launch-view:\\M--\n\
lock:\\M-o\n\
mountpoints:\\M-m\n\
move-sel:\\C-\\M-n\n\
new-instance:\\C-x\n\
next-profile:\\C-\\M-p\n\
only-dirs:\\M-,\n\
open-sel:\\C-\\M-g\n\
paste-sel:\\C-\\M-v\n\
prepend-sudo:\\M-v\n\
previous-profile:\\C-\\M-o\n\
rename-sel:\\C-\\M-r\n\
remove-sel:\\C-\\M-d\n\
refresh-screen:\\C-r\n\
selbox:\\M-s\n\
select-all:\\M-a\n\
show-dirhist:\\M-h\n\
sort-previous:\\M-z\n\
sort-next:\\M-x\n\
toggle-hidden:\\M-i\n\
toggle-hidden2:\\M-.\n\
toggle-light:\\M-y\n\
toggle-long:\\M-l\n\
toggle-max-name-len:\\C-\\M-l\n\
toggle-disk-usage:\\C-\\M-i\n\
toggle-virtualdir-full-paths:\\M-w\n\
trash-sel:\\C-\\M-t\n\
untrash-all:\\C-\\M-u\n\n\
# F6-12\n\
open-mime:\\e[17~\n\
open-preview:\\e[18~\n\
#open-jump-db:\\e[18~\n\
edit-color-scheme:\\e[19~\n\
open-keybinds:\\e[20~\n\
open-config:\\e[21~\n\
open-bookmarks:\\e[23~\n\
quit:\\e[24~\n\
\n\
# Plugins\n\
# 1) Make sure your plugin is in the plugins directory (or use any of the\n\
# plugins in there)\n\
# 2) Link pluginx to your plugin using the 'actions edit' command. Ex:\n\
# \"plugin1=myplugin.sh\"\n\
# 3) Set a keybinding here for pluginx. Ex: \"plugin1:\\M-7\"\n\n\
\n\
# Bound to the xclip plugin\n\
plugin1:\\C-y\n\n\
#plugin2:\n\
#plugin3:\n\
#plugin4:\n",
	    PROGRAM_NAME);

	fclose(fp);
	return EXIT_SUCCESS;
}

static int
create_preview_file(void)
{
	char file[PATH_MAX];
	snprintf(file, sizeof(file), "%s/preview.clifm", config_dir);

	struct stat attr;
	if (stat(file, &attr) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__) \
&& !defined(__DragonFly__) && !defined(__APPLE__)
	if (import_from_data_dir("preview.clifm", file) == EXIT_SUCCESS)
		return EXIT_SUCCESS;
#endif /* !BSD */

	int fd = 0;
	FILE *fp = open_fwrite(file, &fd);
	if (!fp) {
		_err('e', PRINT_PROMPT, "%s: %s: %s\n", PROGRAM_NAME, file,
			strerror(errno));
		return EXIT_FAILURE;
	}

	fprintf(fp, "                  ######################################\n\
                  #   Configuration file for Shotgun   #\n\
                  #       CliFM's file previewer       #\n\
                  ######################################\n\
\n\
# Commented and blank lines are omitted\n\
\n\
# It is recommended to edit this file setting your preferred applications\n\
# first: the previewing process will be smoother and faster this way\n\
# You can even remove whatever applications you don't use\n\
\n\
# For syntax details consult the mimelist.clifm file\n\
\n\
# Uncomment this line to use pistol (or any other previewing program)\n\
#.*=pistol\n\
\n\
# Uncomment and edit this line to use Ranger's scope script:\n\
#.*=/home/USER/.config/ranger/scope.sh %%f 120 80 /tmp/clifm/ True\n\
\n\
# Directories\n\
inode/directory=exa -a --tree --level=1 --;lsd -A --tree --depth=1 --color=always;tree -a -L 1;%s\n\
\n\
# Web content\n\
^text/html$=w3m -dump;lynx -dump --;elinks -dump;pandoc -s -t markdown --;\n\
\n\
# Text\n\
^text/rtf=catdoc --;\n\
N:.*\\.json$=jq --color-output . ;python -m json.tool --;\n\
N:.*\\.md$=glow -s dark --;mdcat --;\n\
^text/.*=highlight -f --out-format=xterm256 --force --;bat --style=plain --color=always --;cat --;\n\
\n\
# Office documents\n\
N:.*\\.xlsx$=xlsx2csv --;file -b --;\n\
N:.*\\.(odt|ods|odp|sxw)$=odt2txt;pandoc -s -t markdown --;\n\
^application/(.*wordprocessingml.document|.*epub+zip|x-fictionbook+xml)=pandoc -s -t markdown --;\n\
^application/msword=catdoc --;file -b --;\n\
^application/ms-excel=xls2csv --;file -b --;\n\
\n\
# Archives\n\
N:.*\\.rar=unrar lt -p- --;\n\
application/zstd=file -b --;true\n\
application/(zip|gzip|x-7z-compressed|x-xz|x-bzip*|x-tar)=atool --list --;bsdtar --list --file;\n\
\n\
# PDF\n\
^application/pdf$=pdftotext -l 10 -nopgbrk -q -- %%f -;mutool draw -F txt -i --;exiftool;\n\
\n\
# Image, video, and audio\n\
^image/vnd.djvu=djvutxt;exiftool;\n\
^image/.*=exiftool;\n\
^video/.*=mediainfo;exiftool;\n\
^audio/.*=mediainfo;exiftool;\n\
\n\
# Torrent:\n\
application/x-bittorrent=transmission-show --;\n\
\n\
# Fallback\n\
.*=file -b --;true;\n",
#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__) \
&& !defined(__DragonFly__) && !defined(__APPLE__)
	"ls -Ap --color=always --indicator-style=none;"
#else
	"gls -Ap --color=always --indicator-style=none;ls -Ap;"
#endif /* !BSD */
	);

	fclose(fp);
	return EXIT_SUCCESS;
}

static int
create_actions_file(char *file)
{
	struct stat attr;
	/* If the file already exists, do nothing */
	if (stat(file, &attr) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	/* If not, try to import it from DATADIR */
	if (import_from_data_dir("actions.clifm", file) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	/* Else, create it */
	int fd = 0;
	FILE *fp = open_fwrite(file, &fd);
	if (!fp) {
		_err('e', PRINT_PROMPT, "%s: %s: %s\n", PROGRAM_NAME, file,
			strerror(errno));
		return EXIT_FAILURE;
	}

	fprintf(fp, "######################\n"
		"# Actions file for %s #\n"
		"######################\n\n"
		"# Define here your custom actions. Actions are "
		"custom command names\n"
		"# bound to an executable file located either in "
		"DATADIR/clifm/plugins\n"
		"# (usually /usr/local/share/clifm/plugins) or in "
		"$XDG_CONFIG_HOME/clifm/plugins (usually ~/.config/clifm/plugins).\n"
		"# Actions can be executed directly from "
		"%s command line, as if they\n"
		"# were any other command, and the associated "
		"file will be executed\n"
		"# instead. All parameters passed to the action "
		"command will be passed\n"
		"# to the corresponding plugin as well.\n\n"
		"+=finder.sh\n"
		"++=jumper.sh\n"
		"-=fzfnav.sh\n"
		"*=fzfsel.sh\n"
		"**=fzfdesel.sh\n"
		"//=rgfind.sh\n"
		"_=fzcd.sh\n"
		"bcp=batch_copy.sh\n"
		"bmi=bm_import.sh\n"
		"bn=batch_create.sh\n"
		"clip=clip.sh\n"
		"cr=cprm.sh\n"
		"da=disk_analyzer.sh\n"
		"dr=dragondrop.sh\n"
		"fdups=fdups.sh\n"
		"gg=pager.sh\n"
		"h=fzfhist.sh\n"
		"i=img_viewer.sh\n"
		"ih=ihelp.sh\n"
		"kbgen=kbgen\n"
		"kd=decrypt.sh\n"
		"ke=encrypt.sh\n"
		"music=music_player.sh\n"
		"ml=mime_list.sh\n"
		"ptot=pdf_viewer.sh\n"
		"plugin1=xclip.sh\n"
		"rrm=recur_rm.sh\n"
		"update=update.sh\n"
		"vid=vid_viewer.sh\n"
		"vt=virtualize.sh\n"
		"wall=wallpaper_setter.sh\n",
	    PROGRAM_NAME, PROGRAM_NAME);

	fclose(fp);
	return EXIT_SUCCESS;
}

static char *
define_tmp_rootdir(int *from_env)
{
	char *temp = (char *)NULL;
	*from_env = 1;

	if (xargs.secure_env != 1 && xargs.secure_env_full != 1) {
		temp = getenv("CLIFM_TMPDIR");
		if (!temp || !*temp)
			temp = getenv("TMPDIR");
	}

	if (!temp || !*temp) {
		*from_env = 0;
		temp = P_tmpdir;
	}

	char *p = temp;
	if (*temp != '/') {
		p = normalize_path(temp, strlen(temp));
		if (!p)
			p = temp;
	}

	size_t len = strlen(p);
	char *tmp_root_dir = savestring(p, len);
	if (p != temp)
		free(p);

	if (len > 1 && tmp_root_dir[len - 1] == '/')
		tmp_root_dir[len - 1] = '\0';

	return tmp_root_dir;
}

/* Define and create the root of our temporary directory. Checks are made in
 * this order: CLIFM_TMPDIR, TMPDIR, P_tmpdir, and /tmp */
static char *
create_tmp_rootdir(void)
{
	int from_env = 0;
	char *tmp_root_dir = define_tmp_rootdir(&from_env);
	if (!tmp_root_dir || !*tmp_root_dir)
		goto END;

	struct stat a;

	if (stat(tmp_root_dir, &a) != -1)
		return tmp_root_dir;

	int ret = EXIT_SUCCESS;
	char *cmd[] = {"mkdir", "-p", "--", tmp_root_dir, NULL};
	if ((ret = launch_execv(cmd, FOREGROUND, E_NOSTDERR)) == EXIT_SUCCESS)
		return tmp_root_dir;

	if (from_env == 0) /* Error creating P_tmpdir */
		goto END;

	_err('w', PRINT_PROMPT, _("%s: %s: %s.\n"
		"Cannot create temporary directory. Falling back to '%s'.\n"),
		PROGRAM_NAME, tmp_root_dir, strerror(ret), P_tmpdir);

	free(tmp_root_dir);
	tmp_root_dir = savestring(P_tmpdir, P_tmpdir_len);

	if (stat(tmp_root_dir, &a) != -1)
		return tmp_root_dir;

	char *cmd2[] = {"mkdir", "-p", "--", tmp_root_dir, NULL};
	if (launch_execv(cmd2, FOREGROUND, E_NOSTDERR) == EXIT_SUCCESS)
		return tmp_root_dir;

END: /* If everything fails, fallback to /tmp */
	free(tmp_root_dir);
	return savestring("/tmp", 4);
}

static void
define_selfile(const size_t tmp_rootdir_len)
{
	/* SEL_FILE should has been set before by set_sel_file(). If not set,
	 * we do not have access to the config dir. */
	if (sel_file)
		return;

	size_t len = 0;
	/* If the config directory isn't available, define an alternative
	 * selection file in TMP_ROOTDIR (if available). */
	if (conf.share_selbox == 0) {
		size_t prof_len = alt_profile ? strlen(alt_profile) : 7;
		/* 7 == lenght of "default" */

		len = tmp_rootdir_len + prof_len + 15;
		sel_file = (char *)xnmalloc(len, sizeof(char));
		snprintf(sel_file, len, "%s/selbox_%s.clifm", tmp_rootdir,
		    alt_profile ? alt_profile : "default");
	} else {
		len = tmp_rootdir_len + 14;
		sel_file = (char *)xnmalloc(len, sizeof(char));
		snprintf(sel_file, len, "%s/selbox.clifm", tmp_rootdir);
	}

	_err('w', PRINT_PROMPT, _("%s: %s: Using a temporary directory for "
		"the Selection Box. Selected files won't be persistent across "
		"reboots\n"), PROGRAM_NAME, tmp_dir);
}

void
create_tmp_files(void)
{
	if (xargs.stealth_mode == 1)
		return;

	free(tmp_rootdir); /* In case we come from reload_config() */
	tmp_rootdir = create_tmp_rootdir();

	size_t tmp_rootdir_len = strlen(tmp_rootdir);
	size_t pnl_len = strlen(PROGRAM_NAME);
	size_t user_len = user.name ? strlen(user.name) : 7; /* 7: len of "unknown" */

	size_t tmp_len = tmp_rootdir_len + pnl_len + user_len + 3;
	tmp_dir = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(tmp_dir, tmp_len, "%s/%s", tmp_rootdir, PROGRAM_NAME);

	struct stat attr;
	if (stat(tmp_dir, &attr) == -1)
		xmkdir(tmp_dir, S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX);

	/* Once TMP_ROOTDIR exists, create the user's directory to store
	 * the list of selected files: TMP_DIR/clifm/username/.selbox_PROFILE.
	 * We use here very restrictive permissions (700), since only the
	 * current user must be able to read and/or modify this list. */
	snprintf(tmp_dir, tmp_len, "%s/%s/%s", tmp_rootdir, PROGRAM_NAME,
		user.name ? user.name : "unknown");
	if (stat(tmp_dir, &attr) == -1
	&& xmkdir(tmp_dir, S_IRWXU) == EXIT_FAILURE) {
		selfile_ok = 0;
		_err('e', PRINT_PROMPT, _("%s: %s: %s\n"), PROGRAM_NAME,
			tmp_dir, strerror(errno));
	}

	/* If the directory exists, check if it is writable. */
	else if (access(tmp_dir, W_OK) == -1 && !sel_file) {
		selfile_ok = 0;
		_err('w', PRINT_PROMPT, "%s: %s: Directory not writable. Selected "
			"files will be lost after program exit\n",
			PROGRAM_NAME, tmp_dir);
	}

	define_selfile(tmp_rootdir_len);
}

static void
define_config_file_names(void)
{
	size_t pnl_len = strlen(PROGRAM_NAME);
	size_t tmp_len = 0;

	if (alt_config_dir) {
		config_dir_gral = savestring(alt_config_dir, strlen(alt_config_dir));
		free(alt_config_dir);
		alt_config_dir = (char *)NULL;
	} else {
		/* If $XDG_CONFIG_HOME is set, use it for the config file.
		 * Else, fall back to user.home: $HOME if not secure-env or pw_dir
		 * from a passwd struct) */
		char *xdg_config_home = getenv("XDG_CONFIG_HOME");
		if (xdg_config_home) {
			size_t len = strlen(xdg_config_home);
			tmp_len = len + pnl_len + 2;
			config_dir_gral = (char *)xnmalloc(tmp_len, sizeof(char));
			snprintf(config_dir_gral, tmp_len, "%s/%s", xdg_config_home,
				PROGRAM_NAME);
			xdg_config_home = (char *)NULL;
		} else {
			tmp_len = user.home_len + pnl_len + 10;
			config_dir_gral = (char *)xnmalloc(tmp_len, sizeof(char));
			snprintf(config_dir_gral, tmp_len, "%s/.config/%s", user.home,
				PROGRAM_NAME);
		}
	}

	size_t config_gral_len = strlen(config_dir_gral);

	/* alt_profile will not be NULL whenever the -P option is used
	 * to run clifm under an alternative profile */
	if (alt_profile) {
		tmp_len = config_gral_len + strlen(alt_profile) + 11;
		config_dir = (char *)xnmalloc(tmp_len, sizeof(char));
		snprintf(config_dir, tmp_len, "%s/profiles/%s",
			config_dir_gral, alt_profile);
	} else {
		tmp_len = config_gral_len + 18;
		config_dir = (char *)xnmalloc(tmp_len, sizeof(char));
		snprintf(config_dir, tmp_len, "%s/profiles/default", config_dir_gral);
	}

	config_dir_len = strlen(config_dir);

	tmp_len = config_dir_len + 6;
	tags_dir = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(tags_dir, tmp_len, "%s/tags", config_dir);

	if (alt_kbinds_file) {
		kbinds_file = savestring(alt_kbinds_file, strlen(alt_kbinds_file));
		free(alt_kbinds_file);
		alt_kbinds_file = (char *)NULL;
	} else {
		/* Keybindings per user, not per profile. */
		tmp_len = config_gral_len + 19;
		kbinds_file = (char *)xnmalloc(tmp_len, sizeof(char));
		snprintf(kbinds_file, tmp_len, "%s/keybindings.clifm", config_dir_gral);
	}

	tmp_len = config_gral_len + 8;
	colors_dir = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(colors_dir, tmp_len, "%s/colors", config_dir_gral);

	tmp_len = config_gral_len + 9;
	plugins_dir = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(plugins_dir, tmp_len, "%s/plugins", config_dir_gral);

	tmp_len = config_dir_len + 15;
	dirhist_file = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(dirhist_file, tmp_len, "%s/dirhist.clifm", config_dir);

	if (!alt_bm_file) {
		tmp_len = config_dir_len + 17;
		bm_file = (char *)xnmalloc(tmp_len, sizeof(char));
		snprintf(bm_file, tmp_len, "%s/bookmarks.clifm", config_dir);
	} else {
		bm_file = savestring(alt_bm_file, strlen(alt_bm_file));
		free(alt_bm_file);
		alt_bm_file = (char *)NULL;
	}

	tmp_len = config_dir_len + 15;
	msgs_log_file = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(msgs_log_file, tmp_len, "%s/msglogs.clifm", config_dir);

	cmds_log_file = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(cmds_log_file, tmp_len, "%s/cmdlogs.clifm", config_dir);

	hist_file = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(hist_file, tmp_len, "%s/history.clifm", config_dir);

	if (!alt_config_file) {
		tmp_len = config_dir_len + pnl_len + 4;
		config_file = (char *)xnmalloc(tmp_len, sizeof(char));
		snprintf(config_file, tmp_len, "%s/%src", config_dir, PROGRAM_NAME);
	} else {
		config_file = savestring(alt_config_file, strlen(alt_config_file));
		free(alt_config_file);
		alt_config_file = (char *)NULL;
	}

	tmp_len = config_dir_len + 15;
	profile_file = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(profile_file, tmp_len, "%s/profile.clifm", config_dir);

	tmp_len = config_dir_len + 16;
	mime_file = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(mime_file, tmp_len, "%s/mimelist.clifm", config_dir);

	tmp_len = config_dir_len + 15;
	actions_file = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(actions_file, tmp_len, "%s/actions.clifm", config_dir);

	tmp_len = config_dir_len + 12;
	remotes_file = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(remotes_file, tmp_len, "%s/nets.clifm", config_dir);

	return;
}

/* Import readline.clifm from data directory. */
static int
import_rl_file(void)
{
	if (!data_dir || !config_dir_gral)
		return EXIT_FAILURE;

	char dest[PATH_MAX];
	snprintf(dest, sizeof(dest), "%s/readline.clifm", config_dir_gral);
	struct stat attr;
	if (lstat(dest, &attr) == 0)
		return EXIT_SUCCESS;

	return import_from_data_dir("readline.clifm", dest);
}

int
create_main_config_file(char *file)
{
	/* First, try to import it from DATADIR */
	char src_filename[NAME_MAX];
	snprintf(src_filename, sizeof(src_filename), "%src", PROGRAM_NAME);
	if (import_from_data_dir(src_filename, file) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	/* If not found, create it */
	int fd;
	FILE *config_fp = open_fwrite(file, &fd);

	if (!config_fp) {
		xerror("%s: fopen: %s: %s\n", PROGRAM_NAME, file, strerror(errno));
		return EXIT_FAILURE;
	}

	/* Do not translate anything in the config file */
	fprintf(config_fp,

"\t\t###########################################\n\
\t\t#                  CLIFM                  #\n\
\t\t#      The command line file manager      #\n\
\t\t###########################################\n\n"

	    "# This is the configuration file for CliFM\n\n"

		"# Lines starting with '#' or ';' are commented (ignored)\n\
# Uncomment an option to override the default value\n\n"

	    "# Color schemes (or just themes) are stored in the colors directory.\n\
# Available themes: base16, default, dracula, dracula-vivid, gruvbox,\n\
# jellybeans-vivid, light, molokai, nocolor, nord, one-dark, solarized, zenburn\n\
# Visit %s to get some extra themes\n\
;ColorScheme=%s\n\n"

	    "# The amount of files contained by a directory is informed next\n\
# to the directory name. However, this feature might slow things down when,\n\
# for example, listing files on a remote server. The filescounter can be\n\
# disabled here, via the --no-files-counter option, or using the 'fc'\n\
# command while in the program itself.\n\
;FilesCounter=%s\n\n"

		"# How to list files: 0 = vertically (like ls(1) would), 1 = horizontally\n\
;ListingMode=%d\n\n"

		"# List files automatically after changing current directory\n\
;AutoLs=%s\n\n"

		"# Send errors, warnings, and notices to the notification daemon?\n\
;DesktopNotifications=%s\n\n"

	    "# If set to true, print a map of the current position in the directory\n\
# history list, showing previous, current, and next entries\n\
;DirhistMap=%s\n\n"

		"# Use a regex expression to filter file names when listing files.\n\
# Example: \"!.*~$\" to exclude backup files (ending with ~), or \"^\\.\" to list \n\
# only hidden files. File type filters are also supported. Example: \"=d\" to\n\
# list directories only, or \"!=l\" to exclude all symlinks.\n\
# Run 'help file-filters' for more information.\n\
;Filter=""\n\n"

	    "# Set the default copy command. Available options are:\n\
# 0 = 'cp -iRp', 1 = 'cp -Rp', 2 = 'advcp -giRp', 3 = 'advcp -gRp',\n\
# 4 = 'wcp', and 5 = 'rsync -avP'\n\
# 2-5 include a progress bar\n\
# Only 0 and 2 will prompt before overwrite\n\
;cpCmd=%d\n\n"

	    "# Set the default move command. Available options are:\n\
# 0 = 'mv -i', 1 = 'mv', 2 = 'advmv -gi', and 3 = 'advmv -g'\n\
# 2 and 3 include a progress bar\n\
# Only 0 and 2 will prompt before overwrite\n\
;mvCmd=%d\n\n"

		"# If set to true, the 'r' command will never prompt before removals\n\
# rm(1) is invoked with the -f flag.\n\
;rmForce=%s\n\n",

	    COLORS_REPO,
		DEF_COLOR_SCHEME,
		DEF_FILES_COUNTER == 1 ? "true" : "false",
		DEF_LISTING_MODE,
		DEF_AUTOLS == 1 ? "true" : "false",
		DEF_DESKTOP_NOTIFICATIONS == 1 ? "true" : "false",
		DEF_DIRHIST_MAP == 1 ? "true" : "false",
		DEF_CP_CMD,
		DEF_MV_CMD,
		DEF_RM_FORCE == 1 ? "true" : "false");

	fprintf(config_fp,
		"# Enable fuzzy matching for filename/path completions and suggestions\n\
;FuzzyMatching=%s\n\n"

		"# Fuzzy matching algorithm: 1 (faster, non-Unicode), 2 (slower, Unicode)\n\
;FuzzyAlgorithm=%d\n\n"

		"# TAB completion mode: 'standard', 'fzf', 'fnf', or 'smenu'. Defaults to\n\
# 'fzf' if the binary is found in PATH. Otherwise, the standard mode is used\n\
;TabCompletionMode=\n\n"

		"# File previews for TAB completion (fzf mode only). Possible values:\n\
# 'true', 'false', 'hidden' (enabled, but hidden; toggle it with Alt-p)\n\
;FzfPreview=%s\n\n"

	    "# MaxPath is only used for the /p option of the prompt: the current working\n\
# directory will be abbreviated to its basename (everything after last slash)\n\
# whenever the current path is longer than MaxPath.\n\
;MaxPath=%d\n\n"

	    ";WelcomeMessage=%s\n\
;WelcomeMessageStr=\"\"\n\n\
# Print %s's logo screen at startup\n\
;SplashScreen=%s\n\n\
;ShowHiddenFiles=%s\n\n\
# List file properties next to file names instead of just file names\n\
;LongViewMode=%s\n\
# Properties fields to be printed in long view mode\n\
# f = files counter for directories\n\
# d = inode number\n\
# p|n = permissions: either symbolic (p) or numeric/octal (n)\n\
# i = user/group IDs (numeric)\n\
# a|m|c = either last (a)ccess, (m)odification or status (c)hange time\n\
# s|S = size (either human readable (s) or bytes (S))\n\
# x = extended attributes (marked as '@')\n\
# A single dash (\'-\') disables all fields\n\
;PropFields=\"%s\"\n\
# Format used to print timestamps in long view (see strftime(3))\n\
;TimeStyle=\"\"\n\
# If you prefer rather relative times\n\
;TimeStyle=relative\n\
# Print files apparent size instead of actual device usage (Linux only)\n\
;ApparentSize=%s\n\
# If running in long view, print directories full size (including contents)\n\
;FullDirSize=%s\n\n\
# Log errors and warnings\n\
;LogMsgs=%s\n\
# Log commands entered in the command line\n\
;LogCmds=%s\n\n"

	    "# Minimum length at which a file name can be trimmed in long view mode.\n\
# If running in long mode, this setting overrides MaxFilenameLen whenever\n\
# this latter is smaller than MINFILENAMETRIM.\n\
;MinFilenameTrim=%d\n\n"

	    "# When a directory rank in the jump database is below MinJumpRank, it\n\
# is removed. If set to 0, directories are kept indefinitely\n\
;MinJumpRank=%d\n\n"

	    "# When the sum of all ranks in the jump database reaches MaxJumpTotalRank,\n\
# all ranks will be reduced using a dynamic factor so that the total sum falls\n\
# below MaxJumpTotalRank again. Those entries falling below MinJumpRank will\n\
# be deleted\n\
;MaxJumpTotalRank=%d\n\n"

		"# Automatically purge the jump database from non-existing directories at\n\
# startup. Note that this will remove paths pointing to unmounted removable\n\
# devices and remote file systems\n\
;PurgeJumpDB=%s\n\n"

	    "# Should CliFM be allowed to run external, shell commands?\n\
;ExternalCommands=%s\n\n"

	    "# Write the last visited directory to $XDG_CONFIG_HOME/clifm/.last to be\n\
# later accessed by the corresponding shell function at program exit.\n\
# To enable this feature consult the manpage.\n\
;CdOnQuit=%s\n\n",

		DEF_FUZZY_MATCH == 1 ? "true" : "false",
		DEF_FUZZY_MATCH_ALGO,
		DEF_FZF_PREVIEW == 1 ? "true" : "false",
		DEF_MAX_PATH,
		DEF_WELCOME_MESSAGE == 1 ? "true" : "false",
		PROGRAM_NAME,
		DEF_SPLASH_SCREEN == 1 ? "true" : "false",
		DEF_SHOW_HIDDEN == 1 ? "true" : "false",
		DEF_LONG_VIEW == 1 ? "true" : "false",
		DEF_PROP_FIELDS,
		DEF_APPARENT_SIZE == 1 ? "true" : "false",
		DEF_FULL_DIR_SIZE == 1 ? "true" : "false",
		DEF_LOG_MSGS == 1 ? "true" : "false",
		DEF_LOG_CMDS == 1 ? "true" : "false",
		DEF_MIN_NAME_TRIM,
		DEF_MIN_JUMP_RANK,
		DEF_MAX_JUMP_TOTAL_RANK,
		DEF_PURGE_JUMPDB == 1 ? "true" : "false",
		DEF_EXT_CMD_OK == 1 ? "true" : "false",
		DEF_CD_ON_QUIT == 1 ? "true" : "false"
		);

	fprintf(config_fp,
	    "# If set to true, a command name that is the name of a directory or a\n\
# file is executed as if it were the argument to the the 'cd' or the \n\
# 'open' commands respectivelly: 'cd DIR' works the same as just 'DIR'\n\
# and 'open FILE' works the same as just 'FILE'.\n\
;Autocd=%s\n\
;AutoOpen=%s\n\n"

	    "# If set to true, enable auto-suggestions.\n\
;AutoSuggestions=%s\n\n"

	    "# The following checks will be performed in the order specified\n\
# by SuggestionStrategy. Available checks:\n\
# a = Aliases names\n\
# b = Bookmarks names (deprecated since v1.9.9)\n\
# c = Path completion\n\
# e = ELN's\n\
# f = File names in current directory\n\
# h = Commands history\n\
# j = Jump database\n\
# Use a dash (-) to skip a check. Ex: 'eahfj-c' to skip the bookmarks check\n\
;SuggestionStrategy=%s\n\n"

	    "# If set to true, suggest file names using the corresponding\n\
# file type color (set via the color scheme file).\n\
;SuggestFiletypeColor=%s\n\n"

		"# Suggest a brief decription for internal commands\n\
;SuggestCmdDesc=%s\n\n"

";SyntaxHighlighting=%s\n\n"

		"# We have three search strategies: 0 = glob-only, 1 = regex-only,\n\
# and 2 = glob-regex\n\
;SearchStrategy=%d\n\n",

		DEF_AUTOCD == 1 ? "true" : "false",
		DEF_AUTO_OPEN == 1 ? "true" : "false",
		DEF_SUGGESTIONS == 1 ? "true" : "false",
		DEF_SUG_STRATEGY,
		DEF_SUG_FILETYPE_COLOR == 1 ? "true" : "false",
		DEF_CMD_DESC_SUG == 1 ? "true" : "false",
		DEF_HIGHLIGHT == 1 ? "true" : "false",
		DEF_SEARCH_STRATEGY
		);

	fprintf(config_fp,
	    "# In light mode, extra file type checks (except those provided by\n\
# the d_type field of the dirent structure (see readdir(3))\n\
# are disabled to speed up the listing process. Because of this, we cannot\n\
# know in advance if a file is readable by the current user, if it is executable,\n\
# SUID, SGID, if a symlink is broken, and so on. The file extension check is\n\
# ignored as well, so that the color per extension feature is disabled.\n\
;LightMode=%s\n\n"

	    "# If running with colors, append directory indicator\n\
# to directories. If running without colors (via the --no-colors option),\n\
# append file type indicator at the end of file names: '/' for directories,\n\
# '@' for symbolic links, '=' for sockets, '|' for FIFO/pipes, '*'\n\
# for for executable files, and '?' for unknown file types. Bear in mind\n\
# that when running in light mode the check for executable files won't be\n\
# performed, and thereby no indicator will be added to executable files.\n\
;Classify=%s\n\n"

		"# Color links as target file name\n\
;ColorLinksAsTarget=%s\n\n"

	    "# Should the Selection Box be shared among different profiles?\n\
;ShareSelbox=%s\n\n"

	    "# Choose the resource opener to open files with their default associated\n\
# application. If not set, Lira, CliFM's built-in opener, is used.\n\
;Opener=\n\n"

	    "# Only used when opening a directory via a new CliFM instance (with the 'x'\n\
# command), this option specifies the command to be used to launch a\n\
# terminal emulator to run CliFM on it.\n\
;TerminalCmd='%s'\n\n"

	    "# Choose sorting method: 0 = none, 1 = name, 2 = size, 3 = atime\n\
# 4 = btime (ctime if not available), 5 = ctime, 6 = mtime, 7 = version\n\
# (name if note available) 8 = extension, 9 = inode, 10 = owner, 11 = group\n\
# NOTE: the 'version' method is not available on FreeBSD\n\
;Sort=%d\n\
# By default, CliFM sorts files from less to more (ex: from 'a' to 'z' if\n\
# using the \"name\" method). To invert this ordering, set SortReverse to\n\
# true (you can also use the --sort-reverse option or the 'st' command)\n\
;SortReverse=%s\n\n"

	"# If set to true, settings changed in the current workspace (only via\n\
# the command line or keyboard shortcuts) are kept private to that workspace\n\
# and made persistent (for the current session only), even when switching\n\
# workspaces.\n\
;PrivateWorkspaceSettings=%s\n\n"

		"# A comma separated list of workspace names in the form NUM=NAME\n\
# Example: \"1=MAIN,2=EXTRA,3=GIT,4=WORK\" or \"1=α,2=β,3=γ,4=δ\"\n\
;WorkspaceNames=\"\"\n\n"

	    "# Print a usage tip at startup\n\
;Tips=%s\n\n\
;ListDirsFirst=%s\n\n\
# Enable case sensitive listing for files in the current directory\n\
;CaseSensitiveList=%s\n\n\
# Enable case sensitive lookup for the directory jumper function (via \n\
# the 'j' command)\n\
;CaseSensitiveDirJump=%s\n\n\
# Enable case sensitive completion for file names\n\
;CaseSensitivePathComp=%s\n\n\
# Enable case sensitive search\n\
;CaseSensitiveSearch=%s\n\n\
;Unicode=%s\n\n\
# Mas, the files list pager. Possible values are:\n\
# 0/false: Disable the pager\n\
# 1/true: Run the pager whenever the list of files does not fit on the screen\n\
# >1: Run the pager whenever the amount of files in the current directory is\n\
# greater than or equal to this value (say, 1000)\n\
;Pager=%s\n\n"

	"# Maximum file name length for listed files. If TrimNames is set to\n\
# true, names larger than MAXFILENAMELEN will be truncated at MAXFILENAMELEN\n\
# using a tilde.\n\
# Set it to -1 (or empty) to remove this limit.\n\
# When running in long mode, this setting is overriden by MinFilenameTrim\n\
# whenever MAXFILENAMELEN is smaller than MINFILENAMETRIM.\n\
;MaxFilenameLen=%d\n\n\
# Trim file names longer than MAXFILENAMELEN\n\
;TrimNames=%s\n\n",

		DEF_LIGHT_MODE == 1 ? "true" : "false",
		DEF_CLASSIFY == 1 ? "true" : "false",
		DEF_COLOR_LNK_AS_TARGET == 1 ? "true" : "false",
		DEF_SHARE_SELBOX == 1 ? "true" : "false",
		DEF_TERM_CMD,
		DEF_SORT,
		DEF_SORT_REVERSE == 1 ? "true" : "false",
		DEF_PRIVATE_WS_SETTINGS == 1 ? "true" : "false",
		DEF_TIPS == 1 ? "true" : "false",
		DEF_LIST_DIRS_FIRST == 1 ? "true" : "false",
		DEF_CASE_SENS_LIST == 1 ? "true" : "false",
		DEF_CASE_SENS_DIRJUMP == 1 ? "true" : "false",
		DEF_CASE_SENS_PATH_COMP == 1 ? "true" : "false",
		DEF_CASE_SENS_SEARCH == 1 ? "true" : "false",
		DEF_UNICODE == 1 ? "true" : "false",
		DEF_PAGER == 1 ? "true" : "false",
		DEF_MAX_NAME_LEN,
		DEF_TRIM_NAMES == 1 ? "true" : "false"
		);

	fprintf(config_fp,
	";MaxHistory=%d\n\
;MaxDirhist=%d\n\
;MaxLog=%d\n\
;Icons=%s\n\
;DiskUsage=%s\n\n"

		"# If set to true, always print the list of selected files. Since this\n\
# list could become quite extensive, you can limit the number of printed \n\
# entries using the MaxPrintSelfiles option (-1 = no limit, 0 = auto (never\n\
# print more than half terminal height), or any custom value)\n\
;PrintSelfiles=%s\n\
;MaxPrintSelfiles=%d\n\n"

	    "# If set to true, clear the screen before listing files\n\
;ClearScreen=%s\n\n"

	    "# If not specified, StartingPath defaults to the current working\n\
# directory. If set, it overrides RestoreLastPath\n\
;StartingPath=\n\n"

	    "# If set to true, start CliFM in the last visited directory (and in the\n\
# last used workspace). This option is overriden by StartingPath (if set).\n\
;RestoreLastPath=%s\n\n"

	    "# If set to true, the 'r' command executes 'trash' instead of 'rm' to\n\
# prevent accidental deletions.\n\
;TrashAsRm=%s\n\n"

	    "# Set readline editing mode: 0 for vi and 1 for emacs (default).\n\
;RlEditMode=%d\n\n",

		DEF_MAX_HIST,
		DEF_MAX_DIRHIST,
		DEF_MAX_LOG,
		DEF_ICONS == 1 ? "true" : "false",
		DEF_DISK_USAGE == 1 ? "true" : "false",
		DEF_PRINTSEL == 1 ? "true" : "false",
		DEF_MAX_PRINTSEL,
		DEF_CLEAR_SCREEN == 1 ? "true" : "false",
		DEF_RESTORE_LAST_PATH == 1 ? "true" : "false",
		DEF_TRASRM == 1 ? "true" : "false",
		DEF_RL_EDIT_MODE
		);

	fputs(

	    "# ALIASES\n\
#alias ls='ls --color=auto -A'\n\n"

	    "# PROMPT COMMANDS\n\
# Write below the commands you want to be executed before the prompt. Ex:\n\
#promptcmd /usr/share/clifm/plugins/git_status.sh\n\
#promptcmd date | awk '{print $1\", \"$2,$3\", \"$4}'\n\n"

		"# AUTOCOMMANDS\n\
# Control CliFM settings on a per directory basis. For more information\n\
# consult the manpage\n\
#autocmd /media/remotes/** lm=1,fc=0\n\
#autocmd @ws3 lv=1\n\
#autocmd ~/important !printf \"Keep your fingers outta here!\\n\" && read -n1\n\
#autocmd ~/Downloads !/usr/share/clifm/plugins/fzfnav.sh\n\n",
	    config_fp);

	fclose(config_fp);
	return EXIT_SUCCESS;
}

static int
create_def_color_scheme256(void)
{
	char cscheme_file[PATH_MAX];
	snprintf(cscheme_file, sizeof(cscheme_file), "%s/%s.clifm", colors_dir,
		DEF_COLOR_SCHEME_256);

	/* If the file already exists, do nothing */
	struct stat attr;
	if (stat(cscheme_file, &attr) != -1)
		return EXIT_SUCCESS;

	/* Try to import it from data dir */
	if (import_color_scheme(DEF_COLOR_SCHEME_256) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	return EXIT_FAILURE;
}

static void
create_def_color_scheme(void)
{
	if (!colors_dir || !*colors_dir)
		return;

	if (term_caps.color >= 256)
		create_def_color_scheme256();

	char cscheme_file[PATH_MAX];
	snprintf(cscheme_file, sizeof(cscheme_file), "%s/%s.clifm", colors_dir,
		DEF_COLOR_SCHEME);

	/* If the file already exists, do nothing */
	struct stat attr;
	if (stat(cscheme_file, &attr) != -1)
		return;

	/* Try to import it from data dir */
	if (import_color_scheme(DEF_COLOR_SCHEME) == EXIT_SUCCESS)
		return;

	/* If cannot be imported either, create it with default values */
	int fd;
	FILE *fp = open_fwrite(cscheme_file, &fd);
	if (!fp) {
		_err('w', PRINT_PROMPT, "%s: Error creating default color scheme "
			"file: %s\n", PROGRAM_NAME, strerror(errno));
		return;
	}

	fprintf(fp, "# Default color scheme for %s\n\n\
# FiletypeColors defines the color used for file types when listing files,\n\
# just as InterfaceColors defines colors for CliFM's interface and ExtColors\n\
# for file extensions. They all make use of the same format used by the\n\
# LS_COLORS environment variable. Thus, \"di=01;34\" means that (non-empty)\n\
# directories will be listed in bold blue.\n\
# Color codes are traditional ANSI escape sequences less the escape char and\n\
# the final 'm'. 8 bit, 256 colors, RGB, and hex (#rrggbb) colors are supported.\n\
# A detailed explanation of all these codes can be found in the manpage.\n\n"

			"FiletypeColors=\"%s\"\n\n"

			"InterfaceColors=\"%s\"\n\n"

			"# Same as FiletypeColors, but for file extensions. The format is always\n\
# *.EXT=COLOR (extensions are case insensitive)\n"
			"ExtColors=\"%s\"\n\n"

			"# Color shades used to colorize timestamps and file sizes. Consult the\n\
# manpage for more information\n"
			"DateShades=\"%s\"\n"
			"SizeShades=\"%s\"\n\n"

			"DirIconColor=\"00;33\"\n\n"
			"DividingLine=\"%s\"\n\n"

			"# If set to 'true', automatically print notifications at the left\n\
# of the prompt. If set to 'false', let the prompt string handle these notifications\n\
# itself via escape codes. See the manpage for more information\n"
			"Notifications=\"%s\"\n\n"
			"Prompt=\"%s\"\n\n"

			"# An alternative prompt to warn the user about invalid command names\n"
			"EnableWarningPrompt=\"%s\"\n\n"
			"WarningPrompt=\"%s\"\n\n"
			"FzfTabOptions=\"%s\"\n\n",

		PROGRAM_NAME,
		DEF_FILE_COLORS,
		DEF_IFACE_COLORS,
		DEF_EXT_COLORS,
		term_caps.color >= 256 ? DEF_DATE_SHADES_256 : DEF_DATE_SHADES_8,
		term_caps.color >= 256 ? DEF_SIZE_SHADES_256 : DEF_SIZE_SHADES_8,
		DEF_DIV_LINE,
		DEF_PROMPT_NOTIF == 1 ? "true" : "false",
		DEFAULT_PROMPT,
		DEF_WARNING_PROMPT == 1 ? "true" : "false",
		DEF_WPROMPT_STR,
		DEF_FZFTAB_OPTIONS);

	fclose(fp);
	return;
}

static int
create_remotes_file(void)
{
	if (!remotes_file || !*remotes_file)
		return EXIT_FAILURE;

	struct stat attr;

	/* If the file already exists, do nothing */
	if (stat(remotes_file, &attr) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	/* Let's try to copy the file from DATADIR */
	if (import_from_data_dir("nets.clifm", remotes_file) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	/* If not in DATADIR, let's create a minimal file here */
	int fd = 0;
	FILE *fp = open_fwrite(remotes_file, &fd);
	if (!fp) {
		_err('e', PRINT_PROMPT, "%s: '%s': %s\n", PROGRAM_NAME,
		    remotes_file, strerror(errno));
		return EXIT_FAILURE;
	}

	fprintf(fp, "#####################################\n"
		"# Remotes management file for %s #\n"
		"#####################################\n\n"
		"# Blank and commented lines are omitted\n\n"
		"# Example:\n"
		"# A name for this remote. It will be used by the 'net' command\n"
		"# and will be available for TAB completion\n"
		"# [work_smb]\n\n"
		"# Comment=My work samba server\n"
		"# Mountpoint=/home/user/.config/clifm/mounts/work_smb\n\n"
		"# Use %%m as a placeholder for Mountpoint\n"
		"# MountCmd=mount.cifs //WORK_IP/shared %%m -o OPTIONS\n"
		"# UnmountCmd=umount %%m\n\n"
		"# Automatically mount this remote at startup\n"
		"# AutoMount=true\n\n"
		"# Automatically unmount this remote at exit\n"
		"# AutoUnmount=true\n\n", PROGRAM_NAME);

	fclose(fp);
	return EXIT_SUCCESS;
}

static void
create_config_files(void)
{
	struct stat attr;

				/* ####################
				 * #    CONFIG DIR    #
				 * #################### */

	/* If the config directory doesn't exist, create it */
	/* Use the mkdir(1) to let it handle parent directories */
	if (stat(config_dir, &attr) == -1) {
		char *tmp_cmd[] = {"mkdir", "-p", config_dir, NULL};
		if (launch_execv(tmp_cmd, FOREGROUND, E_NOFLAG) != EXIT_SUCCESS) {
			config_ok = 0;
			_err('e', PRINT_PROMPT, _("%s: mkdir: '%s': Error creating "
				"configuration directory. Bookmarks, commands logs, and "
				"command history are disabled. Program messages won't be "
				"persistent. Using default options\n"),
			    PROGRAM_NAME, config_dir);
			return;
		}
	}

	/* If it exists, check it is writable */
	else if (access(config_dir, W_OK) == -1) {
		config_ok = 0;
		_err('e', PRINT_PROMPT, _("%s: '%s': Directory not writable. Bookmarks, "
			"commands logs, and commands history are disabled. Program messages "
			"won't be persistent. Using default options\n"),
		    PROGRAM_NAME, config_dir);
		return;
	}

				/* #####################
				 * #     TAGS DIR      #
				 * #####################*/

	if (stat(tags_dir, &attr) == -1 && xmkdir(tags_dir, S_IRWXU) == EXIT_FAILURE)
		_err('w', PRINT_PROMPT, _("%s: %s: Error creating tags directory. "
			"Tag function disabled\n"),	PROGRAM_NAME, tags_dir);

				/* #####################
				 * #    CONFIG FILE    #
				 * #####################*/

	if (stat(config_file, &attr) == -1)
		config_ok = create_main_config_file(config_file) == EXIT_SUCCESS ? 1 : 0;

	if (config_ok == 0)
		return;

				/* ######################
				 * #    PROFILE FILE    #
				 * ###################### */

	if (stat(profile_file, &attr) == -1) {
		int fd = 0;
		FILE *profile_fp = open_fwrite(profile_file, &fd);
		if (!profile_fp) {
			_err('e', PRINT_PROMPT, "%s: fopen: '%s': %s\n", PROGRAM_NAME,
			    profile_file, strerror(errno));
		} else {
			fprintf(profile_fp, _("# This is %s's profile file\n#\n"
				"# Write here the commands you want to be executed at startup\n"
				"# Ex:\n#echo \"%s, the command line file manager\"; read -r\n#\n"
				"# Uncommented, non-empty lines are executed line by line. If you\n"
				"# want a multi-line command, just write a script for it:\n"
				"#sh /path/to/my/script.sh\n"),
				PROGRAM_NAME_UPPERCASE, PROGRAM_NAME_UPPERCASE);
			fclose(profile_fp);
		}
	}

				/* #####################
				 * #    COLORS DIR     #
				 * ##################### */

	if (stat(colors_dir, &attr) == -1
	&& xmkdir(colors_dir, S_IRWXU) == EXIT_FAILURE)
		_err('w', PRINT_PROMPT, _("%s: mkdir: Error creating colors "
			"directory. Using the default color scheme\n"), PROGRAM_NAME);

	/* Generate the default color scheme file */
	create_def_color_scheme();

				/* #####################
				 * #      PLUGINS      #
				 * #####################*/

	if (stat(plugins_dir, &attr) == -1
	&& xmkdir(plugins_dir, S_IRWXU) == EXIT_FAILURE)
		_err('e', PRINT_PROMPT, _("%s: mkdir: Error creating plugins "
			"directory. The actions function is disabled\n"), PROGRAM_NAME);

	import_rl_file();
	create_actions_file(actions_file);
	create_mime_file(mime_file, 0);
	create_preview_file();
	create_remotes_file();
}

static int
create_mime_file_anew(char *file)
{
	int fd;
	FILE *fp = open_fwrite(file, &fd);
	if (!fp) {
		_err('e', PRINT_PROMPT, "%s: %s: %s\n", PROGRAM_NAME, file,
			strerror(errno));
		return EXIT_FAILURE;
	}

	fprintf(fp, "                  ###################################\n\
                  #   Configuration file for Lira   #\n\
                  #     CliFM's resource opener     #\n\
                  ###################################\n\
\n\
# Commented and blank lines are omitted\n\
\n\
# The below settings cover the most common filetypes\n\
# It is recommended to edit this file placing your prefered applications\n\
# at the beginning of the apps list to speed up the opening process\n\
\n\
# The file is read top to bottom and left to right; the first existent\n\
# application found will be used\n\
\n\
# Applications defined here are NOT desktop files, but commands (arguments\n\
# could be used as well). Bear in mind that these commands will be executed\n\
# directly without shell intervention, so that no shell goodies (like pipes,\n\
# conditions, loops, etc) are available. In case you need something more\n\
# complex than a single command (including shell capabilities) write your\n\
# own script and place the path to the script in place of the command.\n\
# For example: X:^text/.*:~/scripts/my_cool_script.sh\n\
\n\
# Use 'X' to specify a GUI environment and '!X' for non-GUI environments,\n\
# like the kernel built-in console or a remote SSH session.\n\
\n\
# Use 'N' to match file names instead of MIME types.\n\
\n\
# Regular expressions are allowed for both file types and file names.\n\
\n\
# Use the %%f placeholder to specify the position of the file name to be\n\
# opened in the command. Example:\n\
# 'mpv %%f --terminal=no' -> 'mpv FILE --terminal=no'\n\
# If %%f is not specified, the file name will be added to the end of the\n\
# command. Ex: 'mpv --terminal=no' -> 'mpv --terminal=no FILE'\n\
\n\
# Running the opening application in the background:\n\
# For GUI applications:\n\
#    APP %%f &\n\
# For terminal applications:\n\
#    TERM -e APP %%f &\n\
# Replace 'TERM' and 'APP' by the corresponding values. The -e option\n\
# might vary depending on the terminal emulator used (TERM)\n\
\n\
# Note on graphical applications: If the opening application is already running\n\
# the file might be opened in a tab, and CliFM won't wait for the file to be\n\
# closed (because the process already returned). To avoid this, instruct the\n\
# application to run a new instance: Ex: geany -i, gedit -s, kate -n,\n\
# pluma --new-window, and so on.\n\
\n\
# To silence STDERR and/or STDOUT use !E and !O respectivelly (they could\n\
# be used together). Examples:\n\
# Silence STDERR only and run in the foreground:\n\
#    mpv %%f !E\n\
# Silence both (STDERR and STDOUT) and run in the background:\n\
#    mpv %%f !EO &\n\
# or\n\
#    mpv %%f !E !O &\n\
\n\
# Environment variables could be used as well. Example:\n\
# X:text/plain=$EDITOR %%f &;$VISUAL;nano;vi\n\
\n\
# Use Ranger's rifle (or whatever opener you prefer) to open all files\n\
#.*=rifle\n\
\n\
###########################\n\
#  File names/extensions  #\n\
###########################\n\
\n\
# Match a full file name\n\
#X:N:some_filename=cmd\n\
\n\
# Match all file names starting with 'str'\n\
#X:N:^str.*=cmd\n\
\n\
# Match files with extension 'ext'\n\
#X:N:.*\\.ext$=cmd\n\
\n\
X:N:.*\\.djvu$=djview;zathura;xreader;evince;atril\n\
X:N:.*\\.epub$=mupdf;zathura;xreader;ebook-viewer;FBReader\n\
X:N:.*\\.mobi$=ebook-viewer;FBReader\n\
X:N:.*\\.(cbr|cbz)$=mcomix;xreader;YACReader;qcomicbook;zathura\n\
X:N:(.*\\.clifm$|clifmrc)=$EDITOR;$VISUAL;kak;micro;nvim;vim;vi;mg;emacs;ed;nano;mili;leafpad;mousepad;featherpad;gedit;kate;pluma\n\
!X:N:(.*\\.clifm$|clifmrc)=$EDITOR;$VISUAL;kak;micro;nvim;vim;vi;mg;emacs;ed;nano\n\
\n\n");

	fprintf(fp, "##################\n\
#   MIME types   #\n\
##################\n\
\n\
# Directories - only for the open-with (ow) command and the --open command\n\
# line switch\n\
# In graphical environments directories will be opened in a new window\n\
X:inode/directory=xterm -e clifm %%f &;xterm -e vifm %%f &;pcmanfm %%f &;thunar %%f &;xterm -e ncdu %%f &\n\
!X:inode/directory=vifm;ranger;nnn;ncdu\n\
\n\
# Web content\n\
X:^text/html$=$BROWSER;surf;vimprobable;vimprobable2;qutebrowser;dwb;jumanji;luakit;uzbl;uzbl-tabbed;uzbl-browser;uzbl-core;iceweasel;midori;opera;firefox;seamonkey;brave;chromium-browser;chromium;google-chrome;epiphany;konqueror;elinks;links2;links;lynx;w3m\n\
!X:^text/html$=$BROWSER;elinks;links2;links;lynx;w3m\n\
\n\
# Text\n\
#X:^text/x-(c|shellscript|perl|script.python|makefile|fortran|java-source|javascript|pascal)$=geany\n\
X:^text/rtf$=libreoffice;soffice;ooffice\n\
X:(^text/.*|application/json|inode/x-empty)=$EDITOR;$VISUAL;kak;micro;dte;nvim;vim;vi;mg;emacs;ed;nano;mili;leafpad;mousepad;featherpad;nedit;kate;gedit;pluma;io.elementary.code;liri-text;xed;atom;nota;gobby;kwrite;xedit\n\
!X:(^text/.*|application/json|inode/x-empty)=$EDITOR;$VISUAL;kak;micro;dte;nvim;vim;vi;mg;emacs;ed;nano\n\
\n\
# Office documents\n\
X:^application/.*(open|office)document.*=libreoffice;soffice;ooffice\n\
\n\
# Archives\n\
# Note: 'ad' is CliFM's built-in archives utility (based on atool). Remove it if you\n\
# prefer another application\n\
X:^application/(zip|gzip|zstd|x-7z-compressed|x-xz|x-bzip*|x-tar|x-iso9660-image)=ad;xarchiver %%f &;lxqt-archiver %%f &;ark %%f &\n\
!X:^application/(zip|gzip|zstd|x-7z-compressed|x-xz|x-bzip*|x-tar|x-iso9660-image)=ad\n\
\n\
# PDF\n\
X:.*/pdf$=mupdf;sioyek;llpp;lpdf;zathura;mupdf-x11;apvlv;xpdf;xreader;evince;atril;okular;epdfview;qpdfview\n\
\n\
# Images\n\
X:^image/gif$=animate;pqiv;sxiv -a;nsxiv -a\n\
X:^image/svg=display;inkscape\n\
X:^image/.*=sxiv;nsxiv;pqiv;gpicview;qview;qimgv;mirage;ristretto;eog;eom;xviewer;viewnior;nomacs;geeqie;gwenview;gthumb;gimp\n\
!X:^image/.*=fim;img2txt;cacaview;fbi;fbv\n\
\n\
# Video and audio\n\
X:^video/.*=ffplay;mplayer;mplayer2;mpv;vlc;gmplayer;smplayer;celluloid;qmplayer2;haruna;totem\n\
X:^audio/.*=ffplay -nodisp -autoexit;mplayer;mplayer2;mpv;vlc;gmplayer;smplayer;totem\n\
\n\
# Fonts\n\
X:^font/.*=fontforge;fontpreview\n\
\n\
# Torrent:\n\
X:application/x-bittorrent=rtorrent;transimission-gtk;transmission-qt;deluge-gtk;ktorrent\n\
\n\
# Fallback to another resource opener as last resource\n\
.*=xdg-open;mimeo;mimeopen -n;whippet -m;open;linopen;\n");
	fclose(fp);

	return EXIT_SUCCESS;
}

static void
print_mime_file_msg(char *file)
{
	int _free = 0;
	char *f = home_tilde(file, &_free);

	_err('n', PRINT_PROMPT, _("%sNOTE%s: %s created a new MIME list file (%s). "
		"It is recommended to edit this file (entering 'mm edit' or "
		"pressing F6) to add the programs you use and remove those "
		"you don't. This will make the process of opening files "
		"faster and smoother\n"), BOLD, NC, PROGRAM_NAME, f ? f : file);

	if (f != file)
		free(f);
}

int
create_mime_file(char *file, const int new_prof)
{
	if (!file || !*file)
		return EXIT_FAILURE;

	struct stat attr;
	if (stat(file, &attr) == EXIT_SUCCESS)
		return EXIT_SUCCESS;

	int ret = import_from_data_dir("mimelist.clifm", file);
	if (ret != EXIT_SUCCESS)
		ret = create_mime_file_anew(file);

	if (new_prof == 0 && ret == EXIT_SUCCESS)
		print_mime_file_msg(file);
	return ret;
}

int
create_bm_file(void)
{
	if (!bm_file)
		return EXIT_FAILURE;

	struct stat attr;
	if (stat(bm_file, &attr) != -1)
		return EXIT_SUCCESS;

	int fd = 0;
	FILE *fp = open_fwrite(bm_file, &fd);
	if (!fp) {
		_err('e', PRINT_PROMPT, "bookmarks: '%s': %s\n", bm_file,
			strerror(errno));
		return EXIT_FAILURE;
	}

	fprintf(fp, "### This is the bookmarks file for %s ###\n\n"
		"# Empty and commented lines are omitted.\n"
		"# Make your changes, save, and exit.\n"
		"# To remove a bookmark, delete the corresponding line, save, and exit\n"
		"# Changes are applied automatically at exit (to cancel just quit the editor).\n\n"
		"# The bookmarks syntax is: name:path\n"
		"# Example:\n"
		"clifm:%s\n",
		PROGRAM_NAME, config_dir ? config_dir : "/path/to/file");

	fclose(fp);
	return EXIT_SUCCESS;
}

#ifndef CLIFM_SUCKLESS
static char *
get_line_value(char *line)
{
	if (!line || *line < ' ') /* Skip non-printable chars */
		return (char *)NULL;

	return remove_quotes(line);
}

static int
set_fzf_preview_value(const char *line, int *var)
{
	char *p = strchr(line, '=');
	if (!p || !*(++p))
		return (-1);

	if (*p == 't' && strncmp(p, "true", 4) == 0) {
		*var = 1;
	} else if (*p == 'h' && strncmp(p, "hidden", 6) == 0) {
		*var = 2;
	} else {
		if (*p == 'f' && strncmp(p, "false", 5) == 0)
			*var = 0;
	}

	return EXIT_SUCCESS;
}

static void
set_pager_value(char *line, int *var)
{
	char *p = line;
	if (!p || *p < '0')
		return;

	if (*p >= '0' && *p <= '9') {
		size_t l = strlen(p);
		if (l > 0 && p[l - 1] == '\n')
			p[l - 1] = '\0';

		int n = xatoi(p);
		if (n == INT_MIN)
			return;

		*var = n;
		return;
	}

	if (*p == 't' && strncmp(p, "true", 4) == 0) {
		*var = 1;
	} else {
		if (*p == 'f' && strncmp(p, "false", 5) == 0)
			*var = 0;
	}
}

/* Get boolean value from LINE and set VAR accordingly */
static void
set_config_bool_value(char *line, int *var)
{
	char *p = line;
	if (!p || !*p || *p < 'f')
		return;

	if (*p == 't' && strncmp(p, "true", 4) == 0) {
		*var = 1;
	} else {
		if (*p == 'f' && strncmp(p, "false", 5) == 0)
			*var = 0;
	}
}

static void
_set_colorscheme(char *line)
{
	if (!line || *line < ' ')
		return;

	char *p = remove_quotes(line);
	if (!p)
		return;

	size_t l = strlen(p);
	if (p[l - 1] == '\n') {
		p[l - 1] = '\0';
		l--;
	}

	free(conf.usr_cscheme);
	conf.usr_cscheme = savestring(p, l);
}

void
set_div_line(char *line)
{
	if (!line || *line < ' ') {
		*div_line = *DEF_DIV_LINE;
		return;
	}

	char *tmp = remove_quotes(line);
	if (!tmp) {
		*div_line = '\0';
		return;
	}

	xstrsncpy(div_line, tmp, sizeof(div_line));
}

static inline int
_set_filter(const char *line)
{
	char *p = strchr(line, '=');
	if (!p || !*(++p))
		return (-1);

	char *q = remove_quotes(p);
	if (!q)
		return (-1);

	size_t l = strlen(q);
	if (q[l - 1] == '\n') {
		q[l - 1] = '\0';
		l--;
	}

	if (*q == '!') {
		filter.rev = 1;
		q++;
		l--;
	} else {
		filter.rev = 0;
	}

	set_filter_type(*q);
	free(filter.str);
	filter.str = savestring(q, l);

	return EXIT_SUCCESS;
}

static inline void
set_listing_mode(const char *line)
{
	char *p = strchr(line, '=');
	if (!p || !*p || !*(++p))
		goto END;

	int n = atoi(p);
	if (n == INT_MIN)
		goto END;

	switch (n) {
	case VERTLIST: conf.listing_mode = VERTLIST; return;
	case HORLIST: conf.listing_mode = HORLIST; return;
	default: break;
	}

END:
	conf.listing_mode = DEF_LISTING_MODE;
}

static void
set_search_strategy(const char *line)
{
	if (!line || *line < '0' || *line > '2')
		return;

	switch (*line) {
	case '0': conf.search_strategy = GLOB_ONLY; break;
	case '1': conf.search_strategy = REGEX_ONLY; break;
	case '2': conf.search_strategy = GLOB_REGEX; break;
	default: break;
	}
}

static inline int
set_max_filename_len(const char *line)
{
	char *p = strchr(line, '=');
	if (!p || !*p || !*(++p))
		return (conf.max_name_len = UNSET);

	int n = atoi(p);
	if (n <= 0)
		return (conf.max_name_len = UNSET);

	conf.max_name_len = n;
	return EXIT_SUCCESS;
}

static void
free_workspaces_names(void)
{
	if (workspaces) {
		int i = MAX_WS;
		while (--i >= 0) {
			free(workspaces[i].name);
			workspaces[i].name = (char *)NULL;
		}
	}

	return;
}

/* Get workspaces names from the WorkspacesNames line in the configuration
 * file and store them in the workspaces array */
void
set_workspace_names(char *line)
{
	char *e = (char *)NULL;
	char *t = remove_quotes(line);
	if (!t || !*t)
		return;

	char *p = (char *)NULL;
	while (*t) {
		p = strchr(t, ',');
		if (p)
			*p = '\0';
		else
			p = t;

		e = strchr(t, '=');
		if (!e || !*(e + 1))
			goto CONT;

		*e = '\0';
		if (!is_number(t))
			goto CONT;

		int a = atoi(t);
		if (a <= 0 || a > MAX_WS)
			goto CONT;

		free(workspaces[a - 1].name);
		workspaces[a - 1].name = savestring(e + 1, strlen(e + 1));

CONT:
		t = p + 1;
		continue;
	}

	return;
}

#ifndef _NO_SUGGESTIONS
static void
set_sug_strat(const char *line)
{
	char opt_str[SUG_STRATS + 3] = "";
	if (sscanf(line, "%9s\n", opt_str) == -1)
		return;

	char *tmp = remove_quotes(opt_str);
	if (!tmp)
		return;

	int fail = 0;
	size_t s;
	for (s = 0; tmp[s]; s++) {
		if (tmp[s] != 'a' && tmp[s] != 'b'
		&& tmp[s] != 'c' && tmp[s] != 'e'
		&& tmp[s] != 'f' && tmp[s] != 'h'
		&& tmp[s] != 'j' && tmp[s] != '-') {
			fail = 1;
			break;
		}
	}

	if (fail == 1 || s != SUG_STRATS)
		return;

	free(conf.suggestion_strategy);
	conf.suggestion_strategy = savestring(tmp, strnlen(tmp, sizeof(tmp)));
}
#endif /* _NO_SUGGESTIONS */

#ifndef _NO_FZF
static void
set_tabcomp_mode(const char *line)
{
	char opt_str[11] = "";
	if (sscanf(line, "%10s\n", opt_str) == -1)
		return;

	char *tmp = remove_quotes(opt_str);
	if (!tmp)
		return;

	if (strncmp(tmp, "standard", 8) == 0) {
		fzftab = 0; tabmode = STD_TAB;
	} else if (strncmp(tmp, "fzf", 3) == 0) {
		fzftab = 1; tabmode = FZF_TAB;
	} else if (strncmp(tmp, "fnf", 3) == 0) {
		fzftab = 1; tabmode = FNF_TAB;
	} else if (strncmp(tmp, "smenu", 5) == 0) {
		fzftab = 1; tabmode = SMENU_TAB;
	}
}
#endif /* _NO_FZF */

static void
set_starting_path(char *line)
{
	char *tmp = get_line_value(line);
	if (!tmp)
		return;

	/* If starting path is not NULL, and exists, and is a directory, and the
	 * user has appropriate permissions, set path to starting path. If any of
	 * these conditions is false, path will be set to default, that is, CWD */
	if (xchdir(tmp, SET_TITLE) == 0) {
		if (cur_ws < 0)
			cur_ws = 0;
		free(workspaces[cur_ws].path);
		workspaces[cur_ws].path = savestring(tmp, strlen(tmp));
		return;
	}

	_err('w', PRINT_PROMPT, _("%s: chdir: %s: %s. Using the "
		"current working directory as starting path\n"),
		PROGRAM_NAME, tmp, strerror(errno));
}

/* Read the main configuration file and set options accordingly */
static void
read_config(void)
{
	int fd;
	FILE *config_fp = open_fread(config_file, &fd);
	if (!config_fp) {
		_err('e', PRINT_PROMPT, _("%s: fopen: '%s': %s. Using default "
			"values.\n"), PROGRAM_NAME, config_file, strerror(errno));
		return;
	}

	free_workspaces_names();

	if (xargs.rl_vi_mode == 1)
		rl_vi_editing_mode(1, 0);

	int ret = -1;
	conf.max_name_len = DEF_MAX_NAME_LEN;
	*div_line = *DEF_DIV_LINE;
	char line[PATH_MAX + 15];

	*prop_fields_str = '\0';

	while (fgets(line, (int)sizeof(line), config_fp)) {
		if (*line < 'A' || *line > 'z')
			continue;

		if (xargs.apparent_size == UNSET && *line == 'A'
		&& strncmp(line, "ApparentSize=", 13) == 0) {
			set_config_bool_value(line + 13, &conf.apparent_size);
		}

		else if (*line == 'a' && strncmp(line, "autocmd ", 8) == 0) {
			parse_autocmd_line(line + 8);
		}

		else if (xargs.autocd == UNSET && *line == 'A'
		&& strncmp(line, "Autocd=", 7) == 0) {
			set_config_bool_value(line + 7, &conf.autocd);
		}

		else if (xargs.autols == UNSET && *line == 'A'
		&& strncmp(line, "AutoLs=", 7) == 0) {
			set_config_bool_value(line + 7, &conf.autols);
		}

		else if (xargs.auto_open == UNSET && *line == 'A'
		&& strncmp(line, "AutoOpen=", 9) == 0) {
			set_config_bool_value(line + 9, &conf.auto_open);
		}

#ifndef _NO_SUGGESTIONS
		else if (xargs.suggestions == UNSET && *line == 'A'
		&& strncmp(line, "AutoSuggestions=", 16) == 0) {
			set_config_bool_value(line + 16, &conf.suggestions);
		}
#endif

		else if (xargs.case_sens_dirjump == UNSET && *line == 'C'
		&& strncmp(line, "CaseSensitiveDirJump=", 21) == 0) {
			set_config_bool_value(line + 21, &conf.case_sens_dirjump);
		}

		else if (*line == 'C'
		&& strncmp(line, "CaseSensitiveSearch=", 20) == 0) {
			set_config_bool_value(line + 20, &conf.case_sens_search);
		}

		else if (xargs.case_sens_list == UNSET && *line == 'C'
		&& strncmp(line, "CaseSensitiveList=", 18) == 0) {
			set_config_bool_value(line + 18, &conf.case_sens_list);
		}

		else if (xargs.case_sens_path_comp == UNSET && *line == 'C'
		&& strncmp(line, "CaseSensitivePathComp=", 22) == 0) {
			set_config_bool_value(line + 22, &conf.case_sens_path_comp);
		}

		else if (xargs.cd_on_quit == UNSET && *line == 'C'
		&& strncmp(line, "CdOnQuit=", 9) == 0) {
			set_config_bool_value(line + 9, &conf.cd_on_quit);
		}

		else if (xargs.classify == UNSET && *line == 'C'
		&& strncmp(line, "Classify=", 9) == 0) {
			set_config_bool_value(line + 9, &conf.classify);
		}

		else if (xargs.clear_screen == UNSET && *line == 'C'
		&& strncmp(line, "ClearScreen=", 12) == 0) {
			set_config_bool_value(line + 12, &conf.clear_screen);
		}

		else if (*line == 'C'
		&& strncmp(line, "ColorLinksAsTarget=", 19) == 0) {
			set_config_bool_value(line + 19, &conf.color_lnk_as_target);
		}

		else if (!conf.usr_cscheme && *line == 'C'
		&& strncmp(line, "ColorScheme=", 12) == 0) {
			_set_colorscheme(line + 12);
		}

		else if (*line == 'c' && strncmp(line, "cpCmd=", 6) == 0) {
			int opt_num = 0;
			ret = sscanf(line + 6, "%d\n", &opt_num);
			if (ret == -1)
				continue;
			if (opt_num >= 0 && opt_num < CP_CMD_AVAILABLE)
				conf.cp_cmd = opt_num;
			else
				conf.cp_cmd = DEF_CP_CMD;
		}

		else if (xargs.desktop_notifications == UNSET && *line == 'D'
		&& strncmp(line, "DesktopNotifications=", 21) == 0) {
			set_config_bool_value(line + 21, &conf.desktop_notifications);
		}

		else if (xargs.dirmap == UNSET && *line == 'D'
		&& strncmp(line, "DirhistMap=", 11) == 0) {
			set_config_bool_value(line + 11, &conf.dirhist_map);
		}

		else if (xargs.disk_usage == UNSET && *line == 'D'
		&& strncmp(line, "DiskUsage=", 10) == 0) {
			set_config_bool_value(line + 10, &conf.disk_usage);
		}

		else if (*line == 'D' && strncmp(line, "DividingLine=", 13) == 0) {
			set_div_line(line + 13);
		}

		else if (xargs.ext == UNSET && *line == 'E'
		&& strncmp(line, "ExternalCommands=", 17) == 0) {
			set_config_bool_value(line + 17, &conf.ext_cmd_ok);
		}

		else if (xargs.files_counter == UNSET && *line == 'F'
		&& strncmp(line, "FilesCounter=", 13) == 0) {
			set_config_bool_value(line + 13, &conf.files_counter);
		}

		else if (!filter.str && *line == 'F'
		&& strncmp(line, "Filter=", 7) == 0) {
			if (_set_filter(line) == -1)
				continue;
		}

		else if (xargs.full_dir_size == UNSET && *line == 'F'
		&& strncmp(line, "FullDirSize=", 12) == 0) {
			set_config_bool_value(line + 12, &conf.full_dir_size);
		}

		else if (xargs.fuzzy_match_algo == UNSET && *line == 'F'
		&& strncmp(line, "FuzzyAlgorithm=", 15) == 0) {
			int opt_num = 0;
			ret = sscanf(line + 15, "%d\n", &opt_num);
			if (ret == -1 || opt_num < 1 || opt_num > FUZZY_ALGO_MAX)
				continue;
			conf.fuzzy_match_algo = opt_num;
		}

		else if (xargs.fuzzy_match == UNSET && *line == 'F'
		&& strncmp(line, "FuzzyMatching=", 14) == 0) {
			set_config_bool_value(line + 14, &conf.fuzzy_match);
		}

		else if (xargs.fzf_preview == UNSET && *line == 'F'
		&& strncmp(line, "FzfPreview=", 11) == 0) {
			if (set_fzf_preview_value(line, &conf.fzf_preview) == -1)
				continue;
		}

#ifndef _NO_ICONS
		else if (xargs.icons == UNSET && *line == 'I'
		&& strncmp(line, "Icons=", 6) == 0) {
			set_config_bool_value(line + 6, &conf.icons);
		}
#endif /* !_NO_ICONS */

		else if (xargs.light == UNSET && *line == 'L'
		&& strncmp(line, "LightMode=", 10) == 0) {
			set_config_bool_value(line + 10, &conf.light_mode);
		}

		else if (xargs.dirs_first == UNSET && *line == 'L'
		&& strncmp(line, "ListDirsFirst=", 14) == 0) {
			set_config_bool_value(line + 14, &conf.list_dirs_first);
		}

		else if (xargs.horizontal_list == UNSET && *line == 'L'
		&& strncmp(line, "ListingMode=", 12) == 0) {
			set_listing_mode(line);
		}

		else if (xargs.longview == UNSET && *line == 'L'
		&& strncmp(line, "LongViewMode=", 13) == 0) {
			set_config_bool_value(line + 13, &conf.long_view);
		}

		////////// DEPRECATED!!
		else if (*line == 'L' && strncmp(line, "Logs=", 5) == 0) {
			set_config_bool_value(line + 5, &conf.log_msgs);
		}
		//////////

		else if (*line == 'L' && strncmp(line, "LogMsgs=", 8) == 0) {
			set_config_bool_value(line + 8, &conf.log_msgs);
		}

		else if (*line == 'L' && strncmp(line, "LogCmds=", 8) == 0) {
			set_config_bool_value(line + 8, &conf.log_cmds);
		}

		else if (xargs.max_dirhist == UNSET && *line == 'M'
		&& strncmp(line, "MaxDirhist=", 11) == 0) {
			int opt_num = 0;
			ret = sscanf(line + 11, "%d\n", &opt_num);
			if (ret == -1)
				continue;
			if (opt_num >= 0)
				conf.max_dirhist = opt_num;
			else /* default */
				conf.max_dirhist = DEF_MAX_DIRHIST;
		}

		else if (*line == 'M' && strncmp(line, "MaxFilenameLen=", 15) == 0) {
			if (set_max_filename_len(line) == -1)
				continue;
		}

		else if (*line == 'M' && strncmp(line, "MaxHistory=", 11) == 0) {
			int opt_num = 0;
			sscanf(line + 11, "%d\n", &opt_num);
			if (opt_num <= 0)
				continue;
			conf.max_hist = opt_num;
		}

		else if (*line == 'M' && strncmp(line, "MaxJumpTotalRank=", 17) == 0) {
			int opt_num = 0;
			ret = sscanf(line + 17, "%d\n", &opt_num);
			if (ret == -1)
				continue;
			conf.max_jump_total_rank = opt_num;
		}

		else if (*line == 'M' && strncmp(line, "MaxLog=", 7) == 0) {
			int opt_num = 0;
			sscanf(line + 7, "%d\n", &opt_num);
			if (opt_num <= 0)
				continue;
			conf.max_log = opt_num;
		}

		else if (xargs.max_path == UNSET && *line == 'M'
		&& strncmp(line, "MaxPath=", 8) == 0) {
			int opt_num = 0;
			sscanf(line + 8, "%d\n", &opt_num);
			if (opt_num <= 0)
				continue;
			conf.max_path = opt_num;
		}

		else if (*line == 'M' && strncmp(line, "MaxPrintSelfiles=", 17) == 0) {
			int opt_num = 0;
			ret = sscanf(line + 17, "%d\n", &opt_num);
			if (ret == -1)
				continue;
			conf.max_printselfiles = opt_num;
		}

		else if (*line == 'M' && strncmp(line, "MinFilenameTrim=", 16) == 0) {
			int opt_num = 0;
			ret = sscanf(line + 16, "%d\n", &opt_num);
			if (ret == -1)
				continue;
			if (opt_num > 0)
				conf.min_name_trim = opt_num;
			else /* default */
				conf.min_name_trim = DEF_MIN_NAME_TRIM;
		}

		else if (*line == 'M' && strncmp(line, "MinJumpRank=", 12) == 0) {
			int opt_num = 0;
			ret = sscanf(line + 12, "%d\n", &opt_num);
			if (ret == -1)
				continue;
			conf.min_jump_rank = opt_num;
		}

		else if (*line == 'm' && strncmp(line, "mvCmd=", 6) == 0) {
			int opt_num = 0;
			ret = sscanf(line + 6, "%d\n", &opt_num);
			if (ret == -1)
				continue;
			if (opt_num >= 0 && opt_num < MV_CMD_AVAILABLE)
				conf.mv_cmd = opt_num;
			else
				conf.mv_cmd = DEF_MV_CMD;
		}

		else if (!conf.opener && *line == 'O'
		&& strncmp(line, "Opener=", 7) == 0) {
			char *tmp = get_line_value(line + 7);
			if (!tmp)
				continue;
			free(conf.opener);
			conf.opener = savestring(tmp, strlen(tmp));
		}

		else if (xargs.pager == UNSET && *line == 'P'
		&& strncmp(line, "Pager=", 6) == 0) {
			set_pager_value(line + 6, &conf.pager);
		}

		else if (xargs.printsel == UNSET && *line == 'P'
		&& strncmp(line, "PrintSelfiles=", 14) == 0) {
			set_config_bool_value(line + 14, &conf.print_selfiles);
		}

		else if (*line == 'P'
		&& strncmp(line, "PrivateWorkspaceSettings=", 25) == 0) {
			set_config_bool_value(line + 25, &conf.private_ws_settings);
		}

		else if (*line == 'P' && strncmp(line, "PropFields=", 11) == 0) {
			char *tmp = get_line_value(line + 11);
			if (tmp) {
				xstrsncpy(prop_fields_str, tmp, sizeof(prop_fields_str));
				set_prop_fields(prop_fields_str);
			}
		}

		else if (*line == 'P' && strncmp(line, "PurgeJumpDB=", 12) == 0) {
			set_config_bool_value(line + 12, &conf.purge_jumpdb);
		}

		else if (xargs.restore_last_path == UNSET && *line == 'R'
		&& strncmp(line, "RestoreLastPath=", 16) == 0) {
			set_config_bool_value(line + 16, &conf.restore_last_path);
		}

		else if (*line == 'R' && strncmp(line, "RlEditMode=0", 12) == 0)
			rl_vi_editing_mode(1, 0); /* Readline defaults to emacs */

		else if (*line == 'r' && strncmp(line, "rmForce=", 8) == 0) {
			set_config_bool_value(line + 8, &conf.rm_force);
		}

		else if (*line == 'S' && strncmp(line, "SearchStrategy=", 15) == 0) {
			set_search_strategy(line + 15);
		}

		else if (xargs.share_selbox == UNSET && *line == 'S'
		&& strncmp(line, "ShareSelbox=", 12) == 0) {
			set_config_bool_value(line + 12, &conf.share_selbox);
		}

		else if (xargs.hidden == UNSET && *line == 'S'
		&& strncmp(line, "ShowHiddenFiles=", 16) == 0) {
			set_config_bool_value(line + 16, &conf.show_hidden);
		}

		else if (xargs.sort == UNSET && *line == 'S'
		&& strncmp(line, "Sort=", 5) == 0) {
			int opt_num = 0;
			ret = sscanf(line + 5, "%d\n", &opt_num);
			if (ret == -1)
				continue;
			if (opt_num >= 0 && opt_num <= SORT_TYPES)
				conf.sort = opt_num;
			else /* default (sort by name) */
				conf.sort = DEF_SORT;
		}

		else if (xargs.sort_reverse == UNSET && *line == 'S'
		&& strncmp(line, "SortReverse=", 12) == 0) {
			set_config_bool_value(line + 12, &conf.sort_reverse);
		}

		else if (xargs.splash == UNSET && *line == 'S'
		&& strncmp(line, "SplashScreen=", 13) == 0) {
			set_config_bool_value(line + 13, &conf.splash_screen);
		}

		else if (xargs.path == UNSET && cur_ws == UNSET && *line == 'S'
		&& strncmp(line, "StartingPath=", 13) == 0) {
			set_starting_path(line + 13);
		}

#ifndef _NO_SUGGESTIONS
		else if (*line == 'S' && strncmp(line, "SuggestCmdDesc=", 15) == 0) {
			set_config_bool_value(line + 15, &conf.cmd_desc_sug);
		}

		else if (*line == 'S'
		&& strncmp(line, "SuggestFiletypeColor=", 21) == 0) {
			set_config_bool_value(line + 21, &conf.suggest_filetype_color);
		}

		else if (*line == 'S'
		&& strncmp(line, "SuggestionStrategy=", 19) == 0) {
			set_sug_strat(line + 19);
		}
#endif /* !_NO_SUGGESTIONS */

#ifndef _NO_HIGHLIGHT
		else if (xargs.highlight == UNSET && *line == 'S'
		&& strncmp(line, "SyntaxHighlighting=", 19) == 0) {
			set_config_bool_value(line + 19, &conf.highlight);
		}
#endif /* !_NO_HIGHLIGHT */

#ifndef _NO_FZF
		else if (xargs.fzftab == UNSET
		&& xargs.fnftab == UNSET && xargs.smenutab == UNSET
		&& *line == 'T' && strncmp(line, "TabCompletionMode=", 18) == 0) {
			set_tabcomp_mode(line + 18);
		}
#endif /* !_NO_FZF */

		else if (*line == 'T' && strncmp(line, "TerminalCmd=", 12) == 0) {
			char *opt = line + 12;
			if (!*opt)
				continue;

			char *tmp = remove_quotes(opt);
			if (!tmp)
				continue;

			free(conf.term);
			conf.term = savestring(tmp, strlen(tmp));
		}

		else if (*line == 'T' && strncmp(line, "TimeStyle=", 10) == 0) {
			char *tmp = get_line_value(line + 10);
			if (!tmp)
				continue;
			free(conf.time_str);
			conf.time_str = savestring(tmp, strlen(tmp));
			if (*tmp == 'r' && strcmp(tmp + 1, "elative") == 0)
				conf.relative_time = 1;
		}

		else if (xargs.tips == UNSET && *line == 'T'
		&& strncmp(line, "Tips=", 5) == 0) {
			set_config_bool_value(line + 5, &conf.tips);
		}

#ifndef _NO_TRASH
		else if (xargs.trasrm == UNSET && *line == 'T'
		&& strncmp(line, "TrashAsRm=", 10) == 0) {
			set_config_bool_value(line + 10, &conf.tr_as_rm);
		}
#endif

		else if (xargs.trim_names == UNSET && *line == 'T'
		&& strncmp(line, "TrimNames=", 10) == 0) {
			set_config_bool_value(line + 10, &conf.trim_names);
		}

		else if (*line == 'U' && strncmp(line, "Unicode=", 8) == 0) {
			set_config_bool_value(line + 8, &conf.unicode);
		}

		else if (xargs.welcome_message == UNSET && *line == 'W'
			&& strncmp(line, "WelcomeMessage=", 15) == 0) {
			set_config_bool_value(line + 15, &conf.welcome_message);
		}

		else if (*line == 'W' && strncmp(line, "WelcomeMessageStr=", 18) == 0) {
			char *tmp = get_line_value(line + 18);
			if (!tmp)
				continue;
			free(conf.welcome_message_str);
			conf.welcome_message_str = savestring(tmp, strlen(tmp));
		}

		else {
			if (*line == 'W' && strncmp(line, "WorkspaceNames=", 15) == 0)
				set_workspace_names(line + 15);
		}
	}

	fclose(config_fp);

	if (xargs.disk_usage_analyzer == 1) {
		conf.sort = STSIZE;
		conf.long_view = conf.full_dir_size = 1;
		conf.list_dirs_first = conf.welcome_message = 0;
	}

	if (filter.str && filter.type == FILTER_FILE_NAME) {
		regfree(&regex_exp);
		ret = regcomp(&regex_exp, filter.str, REG_NOSUB | REG_EXTENDED);
		if (ret != EXIT_SUCCESS) {
			_err('w', PRINT_PROMPT, _("%s: '%s': Invalid regular "
				"expression\n"), PROGRAM_NAME, filter.str);
			free(filter.str);
			filter.str = (char *)NULL;
			regfree(&regex_exp);
		}
	}

	return;
}
#endif /* CLIFM_SUCKLESS */

static void
check_colors(void)
{
	char *nc = getenv("NO_COLOR");
	char *cnc = getenv("CLIFM_NO_COLOR");
	char *cfc = getenv("CLIFM_FORCE_COLOR");

	/* See https://bixense.com/clicolors */
	char *cc = getenv("CLICOLOR");
	char *ccf = getenv("CLICOLOR_FORCE");

	if (term_caps.color == 0 || (nc && *nc) || (cnc && *cnc)
	|| (cc && *cc == '0' && !*(cc + 1))) {
		conf.colorize = 0;
	} else {
		if (conf.colorize == UNSET) {
			if (xargs.colorize == UNSET)
				conf.colorize = DEF_COLORS;
			else
				conf.colorize = xargs.colorize;
		}
	}

	if (xargs.colorize == UNSET
	&& ((cfc && *cfc) || (ccf && *ccf && *ccf != '0'))) {
		if (term_caps.color == 0)
			/* The user is forcing the use of colors even when the terminal
			 * reports no color capability. Let's assume a highly compatible
			 * value */
			term_caps.color = 8;
		conf.colorize = 1;
	}

	if (conf.colorize == 1) {
		unsetenv("CLIFM_COLORLESS");
		set_colors(conf.usr_cscheme ? conf.usr_cscheme
			: (term_caps.color >= 256
			? DEF_COLOR_SCHEME_256 : DEF_COLOR_SCHEME), 1);
		cur_color = tx_c;
		return;
	}

	if (xargs.stealth_mode != 1)
		setenv("CLIFM_COLORLESS", "1", 1);

	reset_filetype_colors();
	reset_iface_colors();
//	unset_suggestions_color();
	cur_color = tx_c;
}

#ifndef _NO_FZF
/* Just check if --height is set in FZF_DEFAULT_OPTS */
static int
get_fzf_win_height(void)
{
	char *p = getenv("FZF_DEFAULT_OPTS");
	if (!p || !*p)
		return 0;

	char *q = strstr(p, "--height");
	if (!q)
		return 0;

	return 1;
}
#endif /* !_NO_FZF */

#ifndef _NO_TRASH
static void
create_trash_dirs(void)
{
	struct stat a;
	if (stat(trash_files_dir, &a) != -1 && S_ISDIR(a.st_mode)
	&& stat(trash_info_dir, &a) != -1 && S_ISDIR(a.st_mode))
		return;

	if (xargs.stealth_mode == 1) {
		_err('w', PRINT_PROMPT, _("%s: %s: %s. Trash function disabled. "
			"If needed, create the directories manually and restart %s.\n"
			"Ex: mkdir -p ~/.local/share/Trash/{files,info}\n"),
			PROGRAM_NAME, trash_dir, strerror(errno), PROGRAM_NAME);
		trash_ok = 0;
		return;
	}

	char *cmd[] = {"mkdir", "-p", trash_files_dir, trash_info_dir, NULL};
	int ret = launch_execv(cmd, FOREGROUND, E_NOFLAG);

	if (ret != EXIT_SUCCESS) {
		trash_ok = 0;
		_err('w', PRINT_PROMPT, _("%s: mkdir: %s: Error creating the trash "
			"directory (or one of its subdirectories: files/ and info/).\n"
			"Try creating them manually and restart %s.\n"
			"Ex: mkdir -p ~/.local/share/Trash/{files,info}\n"),
			PROGRAM_NAME, trash_dir, PROGRAM_NAME);
		return;
	}
}

static void
set_trash_dirs(void)
{
	if (!user.home) {
		trash_ok = 0;
		return;
	}

	size_t len = user.home_len + 20;
	trash_dir = (char *)xnmalloc(len, sizeof(char));
	snprintf(trash_dir, len, "%s/.local/share/Trash", user.home);

	size_t trash_len = strlen(trash_dir);

	len = trash_len + 7;
	trash_files_dir = (char *)xnmalloc(len, sizeof(char));
	snprintf(trash_files_dir, len, "%s/files", trash_dir);

	len = trash_len + 6;
	trash_info_dir = (char *)xnmalloc(len, sizeof(char));
	snprintf(trash_info_dir, len, "%s/info", trash_dir);

	create_trash_dirs();
}
#endif /* _NO_TRASH */

/* Set up CliFM directories and config files. Load the user's
 * configuration from clifmrc */
void
init_config(void)
{
#ifndef _NO_TRASH
	set_trash_dirs();
#endif /* _NO_TRASH */
	if (xargs.stealth_mode == 1) {
		_err(ERR_NO_LOG, PRINT_PROMPT, _("%s: Running in stealth mode: "
			"persistent selection, bookmarks, jump database and directory "
			"history, just as plugins, logs and configuration files, are "
			"disabled.\n"), PROGRAM_NAME);
		config_ok = 0;
		check_colors();
		return;
	}

	if (home_ok == 0) {
		check_colors();
		return;
	}

	define_config_file_names();
	create_config_files();

///////// TEMPORAL CODE
#ifndef _NO_SPLIT_LOG
	split_old_log_file();
#endif
///////////////////////


#ifndef CLIFM_SUCKLESS
	cschemes_n = get_colorschemes();

	if (config_ok == 1)
		read_config();
#else
	xstrsncpy(div_line, DEF_DIV_LINE, sizeof(div_line));
#endif /* CLIFM_SUCKLESS */

	load_prompts();
	check_colors();

#ifndef _NO_FZF
	/* If FZF win height was not defined in the config file,
	 * check whether it is present in FZF_DEFAULT_OPTS */
	if (fzftab && fzf_height_set == 0)
		fzf_height_set = get_fzf_win_height();
#endif

	char *t = getenv("TERM");
	if (xargs.list_and_quit != 1 && t && *t == 'x'
	&& strncmp(t, "xterm", 5) == 0)
		/* If running Xterm, instruct it to send an escape code (27) for
		 * Meta (Alt) key sequences. Otherwise, Alt keybindings won't work */
		META_SENDS_ESC;
}

static void
reset_variables(void)
{
	/* Free everything */
	free(conf.time_str);
	conf.time_str = (char *)NULL;

	free(config_dir_gral);
	free(config_dir);
	config_dir = config_dir_gral = (char *)NULL;

#ifndef _NO_TRASH
	free(trash_dir);
	free(trash_files_dir);
	free(trash_info_dir);
	trash_dir = trash_files_dir = trash_info_dir = (char *)NULL;
#endif

	free(bm_file);
	free(msgs_log_file);
	free(cmds_log_file);
	bm_file = msgs_log_file = cmds_log_file = (char *)NULL;

	free(hist_file);
	free(dirhist_file);
	hist_file = dirhist_file = (char *)NULL;

	free(config_file);
	free(profile_file);
	config_file = profile_file = (char *)NULL;

	free(mime_file);
	free(plugins_dir);
	free(actions_file);
	free(kbinds_file);
	mime_file = plugins_dir = actions_file = kbinds_file = (char *)NULL;

	free(colors_dir);
	free(tmp_dir);
	free(sel_file);
	free(remotes_file);
	tmp_dir = colors_dir = sel_file = remotes_file = (char *)NULL;

#ifndef _NO_SUGGESTIONS
	free(suggestion_buf);
	free(conf.suggestion_strategy);
	suggestion_buf = conf.suggestion_strategy = (char *)NULL;
#endif

	free(conf.fzftab_options);
	free(tags_dir);
	free(conf.wprompt_str);
	conf.fzftab_options = tags_dir = conf.wprompt_str = (char *)NULL;
	free(conf.welcome_message_str);
	conf.welcome_message_str = (char *)NULL;

	free_autocmds();
	free_tags();
	free_remotes(0);

	if (filter.str && filter.env == 0) {
		regfree(&regex_exp);
		free(filter.str);
		filter.str = (char *)NULL;
		filter.rev = 0;
		filter.type = FILTER_NONE;
	}

	free(conf.opener);
	free(conf.encoded_prompt);
/*	free(right_prompt); */
	free(conf.term);
//	conf.opener = conf.encoded_prompt = right_prompt = conf.term = (char *)NULL;
	conf.opener = conf.encoded_prompt = conf.term = (char *)NULL;

	int i = (int)cschemes_n;
	while (--i >= 0)
		free(color_schemes[i]);
	free(color_schemes);
	color_schemes = (char **)NULL;
	cschemes_n = 0;
	free(conf.usr_cscheme);
	conf.usr_cscheme = (char *)NULL;
	cur_cscheme = (char *)NULL;

/*	free(user.shell);
	user.shell = (char *)NULL;
	free(user.shell_basename);
	user.shell_basename = (char *)NULL; */

	init_conf_struct();

	free_workspaces_filters();

	check_cap = UNSET;
	check_ext = UNSET;
	follow_symlinks = UNSET;
#ifndef _NO_FZF
	fzftab = UNSET;
#endif
	hist_status = UNSET;
	int_vars = UNSET;
	max_files = UNSET;
	print_removed_files = UNSET;
	prompt_offset = UNSET;
	prompt_notif = UNSET;

	dir_changed = 0;
	dequoted = 0;
	internal_cmd = 0;
	is_sel = 0;
	kbind_busy = 0;
	mime_match = 0;
	no_log = 0;
	print_msg = 0;
	recur_perm_error_flag = 0;
	sel_is_last = 0;
	shell_is_interactive = 0;
	shell_terminal = 0;
	conf.sort_reverse = 0;
	sort_switch = 0;

	config_ok = 1;
	home_ok = 1;
	selfile_ok = 1;
#ifndef _NO_TRASH
	trash_ok = 1;
#endif

	pmsg = NOMSG;
}

#ifndef _NO_FZF
static void
update_finder_binaries_status(void)
{
	char *p = (char *)NULL;
	if (!(bin_flags & FZF_BIN_OK)) {
		if ((p = get_cmd_path("fzf"))) {
			free(p);
			bin_flags |= FZF_BIN_OK;
		}
	}

	if (!(bin_flags & FNF_BIN_OK)) {
		if ((p = get_cmd_path("fnf"))) {
			free(p);
			bin_flags |= FNF_BIN_OK;
		}
	}

	if (!(bin_flags & SMENU_BIN_OK)) {
		if ((p = get_cmd_path("smenu"))) {
			free(p);
			bin_flags |= SMENU_BIN_OK;
		}
	}
}
#endif /* !_NO_FZF */

int
reload_config(void)
{
#ifndef _NO_FZF
	enum tab_mode tabmode_bk = tabmode;
#endif /* !_NO_FZF */

	reset_variables();

	/* Set up config files and options */
	init_config();

	/* If some option was not set, set it to the default value*/
	check_options();
	set_sel_file();
	create_tmp_files();

	/* If some option was set via command line, keep that value for any profile */
//	check_cmd_line_options();

#ifndef _NO_FZF
	if (tabmode_bk != tabmode)
		update_finder_binaries_status();
	check_completion_mode();
#endif /* !_NO_FZF */

	/* Free the aliases and prompt_cmds arrays to be allocated again */
	int i = dirhist_total_index;
	while (--i >= 0)
		free(old_pwd[i]);
	free(old_pwd);
	old_pwd = (char **)NULL;

	if (jump_db) {
		for (i = 0; jump_db[i].path; i++)
			free(jump_db[i].path);

		free(jump_db);
		jump_db = (struct jump_t *)NULL;
	}
	jump_n = 0;

	i = (int)aliases_n;
	while (--i >= 0) {
		free(aliases[i].name);
		free(aliases[i].cmd);
	}
	free(aliases);
	aliases = (struct alias_t *)NULL;
	aliases_n = 0;

	i = (int)prompt_cmds_n;
	while (--i >= 0)
		free(prompt_cmds[i]);

	dirhist_total_index = 0;
	prompt_cmds_n = 0;

	get_aliases();
	get_prompt_cmds();
	load_dirhist();
	load_jumpdb();
	load_tags();
	load_remotes();
	init_workspaces_opts();

	/* Set the current poistion of the dirhist index to the last entry */
	dirhist_cur_index = dirhist_total_index - 1;

	dir_changed = 1;
	set_env();
	return EXIT_SUCCESS;
}
