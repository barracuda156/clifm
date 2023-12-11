/* prompt.c -- functions controlling the appearance and behavior of the prompt */

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

/* The decode_prompt function is taken from Bash (1.14.7), licensed under
 * GPL-2.0-or-later, and modified to fit our needs. */

#include "helpers.h"

#include <stdio.h>
#include <string.h>
#if !defined(__HAIKU__) && !defined(__OpenBSD__) && !defined(__ANDROID__)
# include <wordexp.h>
#endif /* !__HAIKU__ && !__OpenBSD__ && !__ANDROID__ */
#include <readline/readline.h>
#include <readline/history.h> /* history_expand() */
#include <errno.h>

#include "aux.h"
#include "colors.h" /* update_warning_prompt_text_color() */
#include "exec.h"
#include "file_operations.h"
#include "history.h"
#include "init.h"
#include "listing.h"
#include "messages.h"
#include "misc.h"
#include "navigation.h"
#include "prompt.h"
#include "sanitize.h"

#ifndef _NO_SUGGESTIONS
# include "suggestions.h"
#endif /* !_NO_SUGGESTIONS */

#define CTLESC '\001'
#define CTLNUL '\177'

#define ROOT_IND "\001\x1b[1;31m\002R\001\x1b[0m\002"
#define ROOT_IND_NO_COLOR "R"
#define ROOT_IND_SIZE 17
#define RDONLY_IND "RO\001\x1b[0m\002"
#define RDONLY_IND_SIZE (MAX_COLOR + 8 + 1)
#define STEALTH_IND "S\001\x1b[0m\002"
#define STEALTH_IND_SIZE (MAX_COLOR + 7 + 1)

#define EMERGENCY_PROMPT_MSG "Error decoding prompt line. Using an \
emergency prompt"
#define EMERGENCY_PROMPT "\001\x1b[0m\002> "

/* Flag macros to generate files statistic string for the prompt */
#define STATS_DIR      0
#define STATS_REG      1
#define STATS_EXE      2
#define STATS_HIDDEN   3
#define STATS_SUID     4
#define STATS_SGID     5
#define STATS_FIFO     6
#define STATS_SOCK     7
#define STATS_BLK      8
#define STATS_CHR      9
#define STATS_CAP      10
#define STATS_LNK      11
#define STATS_BROKEN_L 12 /* Broken link */
#define STATS_MULTI_L  13 /* Multi-link */
#define STATS_OTHER_W  14 /* Other writable */
#define STATS_STICKY   15
#define STATS_EXTENDED 16 /* Extended attributes (acl) */
#define STATS_UNKNOWN  17
#define STATS_UNSTAT   18
#ifdef SOLARIS_DOORS
# define STATS_DOOR    19
# define STATS_PORT    20
#endif /* SOLARIS_DOORS */

#define NOTIF_SEL     0
#define NOTIF_TRASH   1
#define NOTIF_WARNING 2
#define NOTIF_ERROR   3
#define NOTIF_NOTICE  4
#define NOTIF_ROOT    5

/* Size of the indicator for msgs, trash, and sel */
#define N_IND MAX_COLOR + 1 + sizeof(size_t) + 6 + 1 + 13
/* Color + 1 letter + plus unsigned integer + RL_NC size + nul char */

static inline char *
gen_time(const int c)
{
	char *temp = (char *)NULL;
	time_t rawtime = time(NULL);
	struct tm tm;

	if (!localtime_r(&rawtime, &tm)) {
		temp = savestring("?", 1);
	} else if (c == 't') {
		char time[9] = "";
		strftime(time, sizeof(time), "%H:%M:%S", &tm);
		temp = savestring(time, sizeof(time));
	} else if (c == 'T') {
		char time[9] = "";
		strftime(time, sizeof(time), "%I:%M:%S", &tm);
		temp = savestring(time, sizeof(time));
	} else if (c == 'A') {
		char time[6] = "";
		strftime(time, sizeof(time), "%H:%M", &tm);
		temp = savestring(time, sizeof(time));
	} else if (c == '@') {
		char time[12] = "";
		strftime(time, sizeof(time), "%I:%M:%S %p", &tm);
		temp = savestring(time, sizeof(time));
	} else { /* c == 'd' */
		char time[12] = "";
		strftime(time, sizeof(time), "%a %b %d", &tm);
		temp = savestring(time, sizeof(time));
	}

	return temp;
}

static inline char *
get_dir_basename(const char *_path)
{
	char *temp = (char *)NULL,
		 *ret = (char *)NULL;

	/* If not root dir (/), get last path component */
	if (!(*_path == '/' && !*(_path + 1)))
		ret = strrchr(_path, '/');

	if (!ret)
		temp = savestring(_path, strlen(_path));
	else
		temp = savestring(ret + 1, strlen(ret + 1));

	return temp;
}

static inline char *
reduce_path(const char *_path)
{
	char *temp = (char *)NULL;

	if (strlen(_path) > (size_t)conf.max_path) {
		char *ret = strrchr(_path, '/');
		if (!ret)
			temp = savestring(_path, strlen(_path));
		else
			temp = savestring(ret + 1, strlen(ret + 1));
	} else {
		temp = savestring(_path, strlen(_path));
	}

	return temp;
}

static inline char *
gen_pwd(const int c)
{
	char *temp = (char *)NULL, *tmp_path = (char *)NULL;
	int free_tmp_path = 0;

	if (user.home
	&& strncmp(workspaces[cur_ws].path, user.home, user.home_len) == 0)
		tmp_path = home_tilde(workspaces[cur_ws].path, &free_tmp_path);

	if (!tmp_path)
		tmp_path = workspaces[cur_ws].path;

	if (c == 'W')
		temp = get_dir_basename(tmp_path);
	else if (c == 'p')
		temp = reduce_path(tmp_path);
	else /* If c == 'w' */
		temp = savestring(tmp_path, strlen(tmp_path));

	if (free_tmp_path == 1)
		free(tmp_path);

	return temp;
}

static inline char *
gen_workspace(void)
{
		/* An int (or workspace name) + workspaces color + NUL byte */
	char s[NAME_MAX + sizeof(ws1_c) + 1];
	char *cl = (char *)NULL;

	if (conf.colorize == 1) {
		switch (cur_ws + 1) {
		case 1: cl = *ws1_c ? ws1_c : DEF_WS1_C; break;
		case 2: cl = *ws2_c ? ws2_c : DEF_WS2_C; break;
		case 3: cl = *ws3_c ? ws3_c : DEF_WS3_C; break;
		case 4: cl = *ws4_c ? ws4_c : DEF_WS4_C; break;
		case 5: cl = *ws5_c ? ws5_c : DEF_WS5_C; break;
		case 6: cl = *ws6_c ? ws6_c : DEF_WS6_C; break;
		case 7: cl = *ws7_c ? ws7_c : DEF_WS7_C; break;
		case 8: cl = *ws8_c ? ws8_c : DEF_WS8_C; break;
		default: break;
		}
	} else {
		cl = df_c;
	}

	if (workspaces[cur_ws].name)
		snprintf(s, sizeof(s), "%s%s", cl, workspaces[cur_ws].name);
	else
		snprintf(s, sizeof(s), "%s%d", cl, cur_ws + 1);

	/* Using strnlen() here avoids a Redhat hardened compilation warning. */
	return savestring(s, strnlen(s, sizeof(s)));
}

static inline char *
gen_exit_status(void)
{
	size_t code_len = (size_t)DIGINUM(exit_code);
	size_t len = code_len + 3 + (MAX_COLOR * 2);

	char *temp = (char *)xnmalloc(len, sizeof(char));
	snprintf(temp, len, "%s%d\001%s\002",
		(exit_code == 0) ? (conf.colorize == 1 ? xs_c : "")
		: (conf.colorize == 1 ? xf_c : ""), exit_code, df_c);

	return temp;
}

static inline char *
gen_escape_char(char **line, int *c)
{
	(*line)++;
	*c = 0;
	/* 27 (dec) == 033 (octal) == 0x1b (hex) == \e */
	char *temp = (char *)xnmalloc(2, sizeof(char));
	*temp = '\033';
	temp[1] = '\0';

	return temp;
}

static inline char *
gen_octal(char **line, int *c)
{
	char octal_string[4];
	int n;

	xstrsncpy(octal_string, *line, sizeof(octal_string));
	octal_string[3] = '\0';

	n = read_octal(octal_string);
	char *temp = (char *)xnmalloc(3, sizeof(char));

	if (n == CTLESC || n == CTLNUL) {
		*line += 3;
		temp[0] = CTLESC;
		temp[1] = (char)n;
		temp[2] = '\0';
	} else if (n == -1) {
		temp[0] = '\\';
		temp[1] = '\0';
	} else {
		*line += 3;
		temp[0] = (char)n;
		temp[1] = '\0';
	}

	*c = 0;

	return temp;
}

static inline char *
gen_profile(void)
{
	char *temp = (char *)NULL;

	if (!alt_profile)
		temp = savestring("default", 7);
	else
		temp = savestring(alt_profile, strlen(alt_profile));

	return temp;
}

static inline char *
gen_user_name(void)
{
	char *temp = (char *)NULL;

	if (!user.name)
		temp = savestring("?", 1);
	else
		temp = savestring(user.name, strlen(user.name));

	return temp;
}

static inline char *
gen_hostname(const int c)
{
	/* Using strnlen() here avoids a Redhat hardened compilation warning. */
	char *temp = savestring(hostname, strnlen(hostname, sizeof(hostname)));
	if (c != 'h')
		return temp;

	char *ret = strchr(temp, '.');
	if (ret)
		*ret = '\0';

	return temp;
}

static inline char *
gen_user_flag(void)
{
	char *temp = (char *)xnmalloc(2, sizeof(char));
	*temp = user.uid == 0 ? '#' : '$';
	temp[1] = '\0';

	return temp;
}

static inline char *
gen_mode(void)
{
	char *temp = (char *)xnmalloc(2, sizeof(char));

	if (conf.light_mode == 1) {
		*temp = 'L';
		temp[1] = '\0';
	} else {
		*temp = '\0';
	}

	return temp;
}

static inline char*
gen_misc(const int c)
{
	char *temp = (char *)xnmalloc(2, sizeof(char));

	if (c == 'n')
		*temp = '\n';
	else if (c == 'r')
		*temp = '\r';
	else
		*temp = '\a';

	temp[1] = '\0';

	return temp;
}

static inline char *
gen_non_print_sequence(const int c)
{
	char *temp = (char *)xnmalloc(2, sizeof(char));
	*temp = (c == '[') ? RL_PROMPT_START_IGNORE : RL_PROMPT_END_IGNORE;
	temp[1] = '\0';

	return temp;
}

static inline char *
gen_shell_name(void)
{
	if (user.shell && user.shell_basename)
		return savestring(user.shell_basename, strlen(user.shell_basename));

	return savestring("unknown", 7);
}

static inline void
add_string(char **tmp, const int c, char **line, char **res, size_t *len)
{
	if (!*tmp)
		return;

	if (c)
		(*line)++;

	*len += strlen(*tmp);

	size_t l = *len + 2	+ (wrong_cmd ? (MAX_COLOR + 6) : 0);
	if (!*res) {
		*res = (char *)xnmalloc(l + 1, sizeof(char));
		*(*res) = '\0';
	} else {
		*res = (char *)xnrealloc(*res, (l + 1), sizeof(char));
	}

	xstrncat(*res, strlen(*res), *tmp, l + 1);
	free(*tmp);
}

#if !defined(__HAIKU__) && !defined(__OpenBSD__) && !defined(__ANDROID__)
static inline void
reset_ifs(const char *value)
{
	if (value)
		setenv("IFS", value, 1);
	else
		unsetenv("IFS");
}

static inline void
substitute_cmd(char **line, char **res, size_t *len)
{
	int tmp = strcntchr(*line, ')');
	if (tmp == -1) return; /* No ending bracket */

	size_t tmp_len = strlen(*line) + 2;
	char *tmp_str = (char *)xnmalloc(tmp_len, sizeof(char));
	snprintf(tmp_str, tmp_len, "$%s", *line);

	tmp_str[tmp + 2] = '\0';
	*line += tmp + 1;

	const char *old_value = getenv("IFS");
	setenv("IFS", "", 1);

	wordexp_t wordbuf;
	if (wordexp(tmp_str, &wordbuf, 0) != EXIT_SUCCESS) {
		free(tmp_str);
		reset_ifs(old_value);
		return;
	}
	reset_ifs(old_value);
	free(tmp_str);

	if (wordbuf.we_wordc) {
		for (size_t j = 0; j < wordbuf.we_wordc; j++) {
			*len += strlen(wordbuf.we_wordv[j]);
			if (!*res) {
				*res = (char *)xnmalloc(*len + 2, sizeof(char));
				*(*res) = '\0';
			} else {
				*res = (char *)xnrealloc(*res, (*len + 2), sizeof(char));
			}
			xstrncat(*res, strlen(*res), wordbuf.we_wordv[j], *len + 2);
		}
	}

	wordfree(&wordbuf);
	return;
}
#endif /* !__HAIKU__ && !__OpenBSD__ && !__ANDROID__ */

static inline char *
gen_emergency_prompt(void)
{
	static int f = 0;
	if (f == 0) {
		f = 1;
		xerror("%s: %s\n", PROGRAM_NAME, EMERGENCY_PROMPT_MSG);
	}
	char *_prompt = savestring(EMERGENCY_PROMPT, sizeof(EMERGENCY_PROMPT) - 1);
	return _prompt;
}

static inline char *
gen_stats_str(const int flag)
{
	size_t val = 0;

	switch (flag) {
	case STATS_BLK: val = stats.block_dev; break;
	case STATS_BROKEN_L: val = stats.broken_link; break;
	case STATS_CAP: val = stats.caps; break;
	case STATS_CHR: val = stats.char_dev; break;
	case STATS_DIR: val = stats.dir; break;
#ifdef SOLARIS_DOORS
	case STATS_DOOR: val = stats.door; break;
	case STATS_PORT: val = stats.port; break;
#endif /* SOLARIS_DOORS */
	case STATS_EXE: val = stats.exec; break;
	case STATS_EXTENDED: val = stats.extended; break;
	case STATS_FIFO: val = stats.fifo; break;
	case STATS_HIDDEN: val = stats.hidden; break;
	case STATS_LNK: val = stats.link; break;
	case STATS_MULTI_L: val = stats.multi_link; break;
	case STATS_OTHER_W: val = stats.other_writable; break;
	case STATS_REG: val = stats.reg; break;
	case STATS_SUID: val = stats.suid; break;
	case STATS_SGID: val = stats.sgid; break;
	case STATS_SOCK: val = stats.socket; break;
	case STATS_STICKY: val = stats.sticky; break;
	case STATS_UNKNOWN: val = stats.unknown; break;
	case STATS_UNSTAT: val = stats.unstat; break;
	default: break;
	}

	char *p = (char *)NULL;
	if (val != 0) {
		p = (char *)xnmalloc(32, sizeof(char));
		snprintf(p, 32, "%zu", val);
	} else {
		p = (char *)xnmalloc(2, sizeof(char));
		*p = '-';
		p[1] = '\0';
	}

	return p;
}

static inline char *
gen_notification(const int flag)
{
	char *p;
	size_t len = 32;

	p = (char *)xnmalloc(len, sizeof(char));
	*p = '\0';

	switch (flag) {
	case NOTIF_ERROR:
		if (msgs.error > 0)
			snprintf(p, len, "E%zu", msgs.error);
		break;
	case NOTIF_NOTICE:
		if (msgs.notice > 0)
			snprintf(p, len, "N%zu", msgs.notice);
		break;
	case NOTIF_WARNING:
		if (msgs.warning > 0)
			snprintf(p, len, "W%zu", msgs.warning);
		break;
	case NOTIF_ROOT:
		if (user.uid == 0)
			{ *p = 'R'; p[1] = '\0'; }
		break;
	case NOTIF_SEL:
		if (sel_n > 0)
			snprintf(p, len, "*%zu", sel_n);
		break;
	case NOTIF_TRASH:
		if (trash_n > 2)
			snprintf(p, len, "T%zu", trash_n - 2);
		break;
	default: break;
	}

	return p;
}

static inline char *
gen_nesting_level(const int mode)
{
	char *p = (char *)NULL;

	if (mode == 'i') {
		p = (char *)xnmalloc(32, sizeof(char));
		snprintf(p, 32, "%d", nesting_level);
		return p;
	}

	/* I == full mode (nothing if first level) */
	if (nesting_level <= 1) {
		p = xnmalloc(1, sizeof(char));
		*p = '\0';
		return p;
	}

	size_t len = (MAX_COLOR * 2) + 32;
	p = (char *)xnmalloc(len, sizeof(char));
	snprintf(p, len, "(%d)", nesting_level);

	return p;
}

/* Decode the prompt string (encoded_prompt global variable) taken from
 * the configuration file. */
char *
decode_prompt(char *line)
{
	if (!line)
		return (char *)NULL;

	char *temp = (char *)NULL, *result = (char *)NULL;
	size_t result_len = 0;
	int c;

	while ((c = *line++)) {
		/* We have an escape char */
		if (c == '\\') {
			/* Now move on to the next char */
			c = *line;
			switch (c) {
			/* Files statistics */
			case 'B': temp = gen_stats_str(STATS_BLK); goto ADD_STRING;
			case 'C': temp = gen_stats_str(STATS_CHR); goto ADD_STRING;
			case 'D': temp = gen_stats_str(STATS_DIR); goto ADD_STRING;
			case 'E': temp = gen_stats_str(STATS_EXTENDED); goto ADD_STRING;
			case 'F': temp = gen_stats_str(STATS_FIFO); goto ADD_STRING;
			case 'G': temp = gen_stats_str(STATS_SGID); goto ADD_STRING;
			case 'K': temp = gen_stats_str(STATS_SOCK); goto ADD_STRING;
			case 'L': temp = gen_stats_str(STATS_LNK); goto ADD_STRING;
			case 'M': temp = gen_stats_str(STATS_MULTI_L); goto ADD_STRING;
			case 'o': temp = gen_stats_str(STATS_BROKEN_L); goto ADD_STRING;
			case 'O': temp = gen_stats_str(STATS_OTHER_W); goto ADD_STRING;
			case 'R': temp = gen_stats_str(STATS_REG); goto ADD_STRING;
			case 'U': temp = gen_stats_str(STATS_SUID); goto ADD_STRING;
			case 'x': temp = gen_stats_str(STATS_CAP); goto ADD_STRING;
			case 'X': temp = gen_stats_str(STATS_EXE); goto ADD_STRING;
			case '.': temp = gen_stats_str(STATS_HIDDEN); goto ADD_STRING;
			case '"': temp = gen_stats_str(STATS_STICKY); goto ADD_STRING;
			case '?': temp = gen_stats_str(STATS_UNKNOWN); goto ADD_STRING;
			case '!': temp = gen_stats_str(STATS_UNSTAT); goto ADD_STRING;
#ifdef SOLARIS_DOORS
			case '>': temp = gen_stats_str(STATS_DOOR); goto ADD_STRING;
			case '<': temp = gen_stats_str(STATS_PORT); goto ADD_STRING;
#endif /* SOLARIS_DOORS */

			case '*': temp = gen_notification(NOTIF_SEL); goto ADD_STRING;
			case '%': temp = gen_notification(NOTIF_TRASH); goto ADD_STRING;
			case '#': temp = gen_notification(NOTIF_ROOT); goto ADD_STRING;
			case ')': temp = gen_notification(NOTIF_WARNING); goto ADD_STRING;
			case '(': temp = gen_notification(NOTIF_ERROR); goto ADD_STRING;
			case '=': temp = gen_notification(NOTIF_NOTICE); goto ADD_STRING;

			case 'z': /* Exit status of last executed command */
				temp = gen_exit_status(); goto ADD_STRING;

			case 'e': /* Escape char */
				temp = gen_escape_char(&line, &c); goto ADD_STRING;

			case '0': /* fallthrough */ /* Octal char */
			case '1': /* fallthrough */
			case '2': /* fallthrough */
			case '3': /* fallthrough */
			case '4': /* fallthrough */
			case '5': /* fallthrough */
			case '6': /* fallthrough */
			case '7':
				temp = gen_octal(&line, &c); goto ADD_STRING;

			case 'c': /* Program name */
				temp = savestring(PROGRAM_NAME, strlen(PROGRAM_NAME));
				goto ADD_STRING;

			case 'P': /* Current profile name */
				temp = gen_profile(); goto ADD_STRING;

			case 't': /* fallthrough */ /* Time: 24-hour HH:MM:SS format */
			case 'T': /* fallthrough */ /* 12-hour HH:MM:SS format */
			case 'A': /* fallthrough */ /* 24-hour HH:MM format */
			case '@': /* fallthrough */ /* 12-hour HH:MM:SS am/pm format */
			case 'd': /* Date: abrev_weak_day, abrev_month_day month_num */
				temp = gen_time(c); goto ADD_STRING;

			case 'u': /* User name */
				temp = gen_user_name(); goto ADD_STRING;

			case 'h': /* fallthrough */ /* Hostname up to first '.' */
			case 'H': /* Full hostname */
				temp = gen_hostname(c); goto ADD_STRING;

			case 'i': /* fallthrough */ /* Nest level (number only) */
			case 'I': /* Nest level (full format) */
				temp = gen_nesting_level(c); goto ADD_STRING;

			case 's': /* Shell name (after last slash)*/
				if (!user.shell) { line++; break; }
				temp = gen_shell_name(); goto ADD_STRING;

			case 'S': /* Current workspace */
				temp = gen_workspace(); goto ADD_STRING;

			case 'l': /* Current mode */
				temp = gen_mode(); goto ADD_STRING;

			case 'p': /* fallthrough */ /* Abbreviated if longer than PathMax */
			case 'w': /* fallthrough */ /* Full PWD */
			case 'W': /* Short PWD */
				if (!workspaces[cur_ws].path) {	line++;	break; }
				temp = gen_pwd(c); goto ADD_STRING;

			case '$': /* '$' or '#' for normal and root user */
				temp = gen_user_flag();	goto ADD_STRING;

			case 'a': /* fallthrough */ /* Bell character */
			case 'r': /* fallthrough */ /* Carriage return */
			case 'n': /* fallthrough */ /* New line char */
				temp = gen_misc(c); goto ADD_STRING;

			case '[': /* fallthrough */ /* Begin a sequence of non-printing characters */
			case ']': /* End the sequence */
				temp = gen_non_print_sequence(c); goto ADD_STRING;

			case '\\': /* Literal backslash */
				temp = savestring("\\", 1); goto ADD_STRING;

			default:
				temp = savestring("\\ ", 2);
				temp[1] = (char)c;

ADD_STRING:
				add_string(&temp, c, &line, &result, &result_len);
				break;
			}
		}

		/* If not escape code, check for command substitution, and if not,
		 * just add whatever char is there. */
		else {
			/* Remove non-escaped quotes */
			if (c == '\'' || c == '"')
				continue;

#if !defined(__HAIKU__) && !defined(__OpenBSD__) && !defined(__ANDROID__)
			/* Command substitution */
			if (c == '$' && *line == '(') {
				substitute_cmd(&line, &result, &result_len);
				continue;
			}
#endif /* !__HAIKU__ && !__OpenBSD__ && !__ANDROID__ */

			size_t new_len = result_len + 2
				+ (wrong_cmd ? (MAX_COLOR + 6) : 0);
			result = (char *)xnrealloc(result, new_len, sizeof(char));
			result[result_len] = (char)c;
			result_len++;
			result[result_len] = '\0';
		}
	}

	/* Remove trailing new line char, if any */
	if (result && result_len > 0 && result[result_len - 1] == '\n')
		result[result_len - 1] = '\0';

	/* Emergency prompt, just in case something went wrong */
	if (!result)
		result = gen_emergency_prompt();

	return result;
}

/* Make sure CWD exists; if not, go up to the parent, and so on */
static inline void
check_cwd(void)
{
	int cwd_change = 0;

	while (xchdir(workspaces[cur_ws].path, SET_TITLE) != EXIT_SUCCESS) {
		char *ret = strrchr(workspaces[cur_ws].path, '/');
		if (ret && ret != workspaces[cur_ws].path) {
			*ret = '\0';
			cwd_change = 1;
		} else {
			break;
		}
	}

	if (cwd_change == 1 && conf.autols == 1)
		refresh_screen();
}

/* Remove all final slash(es) from path, if any */
static inline void
trim_final_slashes(void)
{
	size_t path_len = strlen(workspaces[cur_ws].path), i;

	for (i = path_len - 1; workspaces[cur_ws].path[i] && i > 0; i--) {
		if (workspaces[cur_ws].path[i] != '/')
			break;
		else
			workspaces[cur_ws].path[i] = '\0';
	}
}

static inline void
print_welcome_msg(void)
{
	static int wm = UNSET;

	if (wm == 0 || conf.welcome_message == 0)
		return;

	if (conf.welcome_message_str)
		printf("%s%s%s\n", wc_c, conf.welcome_message_str, df_c);
	else
		printf("%s%s\n%s", wc_c, DEF_WELCOME_MESSAGE_STR, df_c);

	printf("%s\n", _(HELP_MESSAGE));

	wm = 0;
}

static inline void
print_tips_func(void)
{
	if (conf.tips == 0)
		return;

	static int first_run = 1;
	if (first_run == 1) {
		print_tips(0);
		first_run = 0;
	}
}

static inline void
run_prompt_cmds(void)
{
	if (conf.ext_cmd_ok == 0 || prompt_cmds_n == 0)
		return;

	int tflags = flags;
	flags &= ~DELAYED_REFRESH;
	size_t i;
	for (i = 0; i < prompt_cmds_n; i++) {
		if (xargs.secure_cmds == 0
		|| sanitize_cmd(prompt_cmds[i], SNT_PROMPT) == EXIT_SUCCESS)
			launch_execl(prompt_cmds[i]);
	}
	flags = tflags;
}

#ifndef _NO_TRASH
static void
update_trash_indicator(void)
{
	static time_t trash_files_dir_mtime = 0;

	if (trash_ok == 0)
		return;

	struct stat a;
	if (stat(trash_files_dir, &a) == -1)
		return;

	if (trash_files_dir_mtime == a.st_mtime)
		return;

	trash_files_dir_mtime = a.st_mtime;

	filesn_t n = count_dir(trash_files_dir, NO_CPOP);
	if (n <= 2)
		trash_n = 0;
	else
		trash_n = (size_t)n;
}
#endif /* !_NO_TRASH */

static inline void
setenv_prompt(void)
{
	if (prompt_notif == 1)
		return;

	/* Set environment variables with CliFM state information
	 * (sel files, trash, stealth mode, messages) to be handled by
	 * the prompt itself. */
	char t[32];
	snprintf(t, sizeof(t), "%d", (int)sel_n);
	setenv("CLIFM_STAT_SEL", t, 1);
#ifndef _NO_TRASH
	snprintf(t, sizeof(t), "%d", trash_n > 2 ? (int)trash_n - 2 : 0);
	setenv("CLIFM_STAT_TRASH", t, 1);
#endif /* !_NO_TRASH */
	snprintf(t, sizeof(t), "%d", (int)msgs.error);
	setenv("CLIFM_STAT_ERROR_MSGS", t, 1);
	snprintf(t, sizeof(t), "%d", (int)msgs.warning);
	setenv("CLIFM_STAT_WARNING_MSGS", t, 1);
	snprintf(t, sizeof(t), "%d", (int)msgs.notice);
	setenv("CLIFM_STAT_NOTICE_MSGS", t, 1);

	snprintf(t, sizeof(t), "%d", cur_ws + 1);
	setenv("CLIFM_STAT_WS", t, 1);
	snprintf(t, sizeof(t), "%d", exit_code);
	setenv("CLIFM_STAT_EXIT", t, 1);
	setenv("CLIFM_STAT_ROOT", user.uid == 0 ? "1" : "0", 1);
	setenv("CLIFM_STAT_STEALTH", (xargs.stealth_mode == 1) ? "1" : "0", 1);
}

static inline size_t
set_prompt_length(const size_t decoded_prompt_len)
{
	size_t len = 0;

	if (prompt_notif == 1) {
		len = (size_t)(decoded_prompt_len
		+ (xargs.stealth_mode == 1 ? STEALTH_IND_SIZE : 0)
		+ (user.uid == 0 ? ROOT_IND_SIZE : 0)
		+ (conf.readonly == 1 ? RDONLY_IND_SIZE : 0)
		+ ((sel_n > 0) ? N_IND : 0)
		+ ((trash_n > 0) ? N_IND : 0)
		+ ((msgs.error > 0) ? N_IND : 0)
		+ ((msgs.warning > 0) ? N_IND : 0)
		+ ((msgs.notice > 0) ? N_IND : 0)
		+ 6 + sizeof(tx_c) + 1 + 2);
	} else {
		len = (size_t)(decoded_prompt_len + 6 + sizeof(tx_c) + 1);
	}

	return len;
}

static inline char *
construct_prompt(const char *decoded_prompt)
{
	/* Construct indicators: MSGS (ERR, WARN, and NOTICE), SEL, and TRASH */
	char err_ind[N_IND], warn_ind[N_IND],
		notice_ind[N_IND], trash_ind[N_IND], sel_ind[N_IND];
	*err_ind = *warn_ind = *notice_ind = *trash_ind = *sel_ind = '\0';

	if (prompt_notif == 1) {
		if (msgs.error > 0)
			snprintf(err_ind, N_IND, "%sE%zu%s", em_c, msgs.error, RL_NC);
		if (msgs.warning > 0)
			snprintf(warn_ind, N_IND, "%sW%zu%s", wm_c, msgs.warning, RL_NC);
		if (msgs.notice > 0)
			snprintf(notice_ind, N_IND, "%sN%zu%s", nm_c, msgs.notice, RL_NC);
		if (trash_n > 2)
			snprintf(trash_ind, N_IND, "%sT%zu%s",
				ti_c, (size_t)trash_n - 2, RL_NC);
		if (sel_n > 0)
			snprintf(sel_ind, N_IND, "%s*%zu%s", li_c, sel_n, RL_NC);
	}

	size_t prompt_len = set_prompt_length(strlen(decoded_prompt));
	char *the_prompt = (char *)xnmalloc(prompt_len, sizeof(char));

	if (prompt_notif == 1) {
		snprintf(the_prompt, prompt_len,
			"%s%s%s%s%s%s%s%s%s%s%s%s\001%s\002",
			(user.uid == 0) ? (conf.colorize == 1
				? ROOT_IND : ROOT_IND_NO_COLOR) : "",
			(conf.readonly == 1) ? ro_c : "",
			(conf.readonly == 1) ? RDONLY_IND : "",
			(msgs.error > 0) ? err_ind : "",
			(msgs.warning > 0) ? warn_ind : "",
			(msgs.notice > 0) ? notice_ind : "",
			(xargs.stealth_mode == 1) ? si_c : "",
			(xargs.stealth_mode == 1) ? STEALTH_IND : "",
			(trash_n > 0) ? trash_ind : "",
			(sel_n > 0) ? sel_ind : "",
			decoded_prompt, RL_NC, tx_c);
	} else {
		snprintf(the_prompt, prompt_len, "%s%s\001%s\002",
			decoded_prompt, RL_NC, tx_c);
	}

	return the_prompt;
}

static inline void
initialize_prompt_data(const int prompt_flag)
{
	check_cwd();
	trim_final_slashes();
	print_welcome_msg();
	print_tips_func();

	/* If autols is disabled, and since terminal dimensions are gathered
	 * in list_dir() via get_term_size(), let's get terminal dimensions
	 * here. We need them to print suggestions */
	if (conf.autols == 0 && conf.suggestions == 1)
		get_term_size();

	/* Set foreground color to default */
	fputs(df_c, stdout);
	fflush(stdout);

	if (prompt_flag != PROMPT_UPDATE)
	/* We're just updating the prompt string: no need to run prompt commands. */
		run_prompt_cmds();

#ifndef _NO_TRASH
	update_trash_indicator();
#endif /* !_NO_TRASH */
	get_sel_files();
	setenv_prompt();

	args_n = 0;
	curhistindex = current_hist_n;
#ifndef _NO_SUGGESTIONS
	if (wrong_cmd == 1) {
		rl_delete_text(0, rl_end);
		rl_point = rl_end = 0;
		recover_from_wrong_cmd();
	}
#endif /* !_NO_SUGGESTIONS */

	/* Print error messages */
	if (print_msg == 1 && msgs_n > 0) {
		fputs(messages[msgs_n - 1], stderr);
		print_msg = 0; /* Print messages only once */
	}
}

static inline void
log_and_record(char *input)
{
	if (conf.log_cmds == 1) {
		free(last_cmd);
		last_cmd = savestring(input, strlen(input));
		log_cmd();
	}

	if (record_cmd(input) == 1)
		add_to_cmdhist(input);
}
/*
void
print_right_prompt(void)
{
//	get_cursor_position(STDIN_FILENO, STDOUT_FILENO);
	get_cursor_position(0, 1);
	char *p = decode_prompt(right_prompt);
	printf("%*s", term_cols, p);
	printf("\x1b[%d;%dH", curline, curcol);
	free(p);
} */

/* Some commands take '!' as parameter modifier: quick search, 'filter',
 * and 'sel', in which case history expansion must not be performed.
 * Return 1 if we have one of these commands or 0 otherwise. */
static int
exclude_from_history(const char *s)
{
	if (*s == '/'
	|| (*s == 's' && (s[1] == ' ' || strncmp(s + 1, "el ", 3) == 0))
	|| (*s == 'f' && ((s[1] == 't' && s[2] == ' ')
	|| strncmp(s + 1, "ilter ", 6) == 0))
	|| (*s == 'd' && s[1] == 'h' && s[2] == ' ')
	|| (*s == 'b' && s[1] == ' '))
		return 1;

	return 0;
}

/* Replace history expressions ("!*") in the string INPUT by the corresponding
 * history entry. */
static int
expand_history(char **input)
{
	/* history_expansion_char defaults to '!' */
	char *hist_c = strchr(*input, history_expansion_char);
	if (!hist_c || (hist_c != *input && *(hist_c - 1) != ' ')
	|| exclude_from_history(*input) == 1)
		return EXIT_SUCCESS;

	char *exp_input = (char *)NULL;
	int ret = history_expand(*input, &exp_input);

	if (ret == -1) { /* Error in expansion */
		xerror("%s: %s\n", PROGRAM_NAME, exp_input);
		free(*input);
		free(exp_input);
		return EXIT_FAILURE;
	}

	if (ret == 0) { /* No expansion took place */
		free(exp_input);
		return EXIT_SUCCESS;
	}

	printf("%s\n", exp_input);
	free(*input);

	if (ret == 2) { /* Display but do not execute the expanded command (:p) */
		free(exp_input);
		return (-1);
	}

	/* (ret == 1) Display and execute */
	*input = exp_input;

	return EXIT_SUCCESS;
}

static char *
handle_empty_line(void)
{
	if (conf.autols == 1 && ((flags & DELAYED_REFRESH)
	|| xargs.refresh_on_empty_line == 1) && rl_pending_input == 0) {
		flags &= ~DELAYED_REFRESH;
		refresh_screen();
	} else {
		flags &= ~DELAYED_REFRESH;
	}

	rl_pending_input = 0;
	return (char *)NULL;
}

/* Print the prompt and return the string entered by the user, to be
 * parsed later by parse_input_str() */
char *
prompt(const int prompt_flag)
{
	initialize_prompt_data(prompt_flag);

	/* Generate the prompt string using the prompt line in the config
	 * file (stored in encoded_prompt at startup). */
	char *decoded_prompt = decode_prompt(conf.encoded_prompt);
	char *the_prompt = construct_prompt(decoded_prompt
		? decoded_prompt : EMERGENCY_PROMPT);
	free(decoded_prompt);

	if (prompt_flag == PROMPT_UPDATE || prompt_flag == PROMPT_UPDATE_RUN_CMDS) {
		rl_set_prompt(the_prompt);
		free(the_prompt);
		return (char *)NULL;
	}

	/* Tell my_rl_getc() (readline.c) to recalculate the length
	 * of the last prompt line, needed to calculate the finder's offset
	 * and the current cursor column. This length might vary if the
	 * prompt contains dynamic values. */
	prompt_offset = UNSET;

/*	if (right_prompt && *right_prompt)
		print_right_prompt(); */

	UNHIDE_CURSOR;

	/* Print the prompt and get user input */
	char *input = readline(the_prompt);
	free(the_prompt);

	if (!input || !*input || rl_end == 0) {
		free(input);
		return handle_empty_line();
	}

	flags &= ~DELAYED_REFRESH;

	if (expand_history(&input) != EXIT_SUCCESS)
		return (char *)NULL;

	log_and_record(input);

	return input;
}

static int
list_prompts(void)
{
	if (prompts_n == 0) {
		puts(_("prompt: No extra prompts found. Using the default prompt"));
		return EXIT_SUCCESS;
	}

	size_t i;
	for (i = 0; i < prompts_n; i++) {
		if (!prompts[i].name)
			continue;

		if (*cur_prompt_name == *prompts[i].name
		&& strcmp(cur_prompt_name, prompts[i].name) == 0)
			printf("%s>%s %s\n", mi_c, df_c, prompts[i].name);
		else
			printf("  %s\n", prompts[i].name);
	}

	return EXIT_SUCCESS;
}

static int
switch_prompt(const size_t n)
{
	if (prompts[n].regular) {
		free(conf.encoded_prompt);
		conf.encoded_prompt = savestring(prompts[n].regular, strlen(prompts[n].regular));
	}

	if (prompts[n].warning) {
		free(conf.wprompt_str);
		conf.wprompt_str = savestring(prompts[n].warning, strlen(prompts[n].warning));
	}

	prompt_notif = prompts[n].notifications;

	if (xargs.warning_prompt == 0)
		return EXIT_SUCCESS;

	conf.warning_prompt = prompts[n].warning_prompt_enabled;

	update_warning_prompt_text_color();

	return EXIT_SUCCESS;
}

static int
set_prompt(char *name)
{
	if (!name || !*name)
		return EXIT_FAILURE;

	if (prompts_n == 0) {
		xerror("%s\n", _("prompt: No extra prompts defined. Using the "
			"default prompt"));
		return EXIT_FAILURE;
	}

	char *p = dequote_str(name, 0);
	if (!p) {
		xerror(_("prompt: %s: Error dequoting string\n"), name);
		return EXIT_FAILURE;
	}

	int i = (int)prompts_n;
	while (--i >= 0) {
		if (*p != *prompts[i].name || strcmp(p, prompts[i].name) != 0)
			continue;
		free(p);
		xstrsncpy(cur_prompt_name, prompts[i].name, sizeof(cur_prompt_name));
		return switch_prompt((size_t)i);
	}

	xerror(_("prompt: %s: No such prompt\n"), p);
	free(p);
	return EXIT_FAILURE;
}

static int
set_default_prompt(void)
{
	free(conf.encoded_prompt);
	conf.encoded_prompt = savestring(DEFAULT_PROMPT, sizeof(DEFAULT_PROMPT) - 1);
	free(conf.wprompt_str);
	conf.wprompt_str = savestring(DEF_WPROMPT_STR, sizeof(DEF_WPROMPT_STR) - 1);
	*cur_prompt_name = '\0';
	prompt_notif = DEF_PROMPT_NOTIF;
	return EXIT_SUCCESS;
}

static int
edit_prompts_file(char *app)
{
	if (xargs.stealth_mode == 1) {
		printf("%s: prompt: %s\n", PROGRAM_NAME, STEALTH_DISABLED);
		return EXIT_SUCCESS;
	}

	if (!prompts_file || !*prompts_file) {
		xerror("%s\n", _("prompt: No prompts file found"));
		return EXIT_FAILURE;
	}

	struct stat a;
	if (stat(prompts_file, &a) == -1) {
		xerror("prompt: '%s': %s\n", prompts_file, strerror(errno));
		return EXIT_FAILURE;
	}

	time_t old_time = a.st_mtime;

	int ret = EXIT_FAILURE;
	if (app && *app) {
		char *cmd[] = {app, prompts_file, NULL};
		ret = launch_execv(cmd, FOREGROUND, E_NOFLAG);
	} else {
		open_in_foreground = 1;
		ret = open_file(prompts_file);
		open_in_foreground = 0;
	}

	if (ret != EXIT_SUCCESS)
		return ret;

	stat(prompts_file, &a);
	if (conf.autols == 1)
		reload_dirlist();
	if (old_time == a.st_mtime)
		return EXIT_SUCCESS;

	ret = load_prompts();
	print_reload_msg(_("File modified. Prompts reloaded\n"));
	if (*cur_prompt_name)
		set_prompt(cur_prompt_name);
	return ret;
}

int
prompt_function(char **args)
{
	if (!args[0] || !*args[0] || (*args[0] == 'l'
	&& strcmp(args[0], "list") == 0))
		return list_prompts();

	if (IS_HELP(args[0])) {
		puts(PROMPT_USAGE);
		return EXIT_SUCCESS;
	}

	if (*args[0] == 'u' && strcmp(args[0], "unset") == 0)
		return set_default_prompt();

	if (*args[0] == 'e' && strcmp(args[0], "edit") == 0)
		return edit_prompts_file(args[1]);

	if (*args[0] == 'r' && strcmp(args[0], "reload") == 0) {
		int ret = load_prompts();
		if (ret == EXIT_SUCCESS) {
			printf(_("%s: Prompts successfully reloaded\n"), PROGRAM_NAME);
			return EXIT_SUCCESS;
		}
		return ret;
	}

	if (*args[0] == 's' && strcmp(args[0], "set") == 0)
		return set_prompt(args[1]);

	return set_prompt(args[0]);
}
