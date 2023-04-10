/* helpers.h -- main header file */

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

#ifndef HELPERS_H
#define HELPERS_H

#if (defined(__linux__) || defined(__CYGWIN__)) && !defined(_BE_POSIX)
# define _GNU_SOURCE
#elif defined(__sun)
# define BSD_COMP
#else
# define _POSIX_C_SOURCE 200809L
# define _DEFAULT_SOURCE
# if defined(__linux__) || defined(__CYGWIN__)
#  define _XOPEN_SOURCE /* wcwidth() */
# endif
# if defined(__FreeBSD__) || defined(__DragonFly__)
#  define _XOPEN_SOURCE
#  define __XSI_VISIBLE 700
#  define __BSD_VISIBLE 1
# endif
# ifdef __NetBSD__
#  define _XOPEN_SOURCE
#  define _NETBSD_SOURCE
#  define __BSD_VISIBLE 1
# endif
# ifdef __OpenBSD__
#  define _BSD_SOURCE
# endif
# ifdef __APPLE__
#  define _DARWIN_C_SOURCE
# endif
#endif

/* Setting GLOB_BRACE to ZERO disables support for GLOB_BRACE if not
 * available on current platform */
#if !defined(__TINYC__) && !defined(GLOB_BRACE)
# define GLOB_BRACE 0
#endif

#if defined(__CYGWIN__) && defined(_BE_POSIX) && !defined(GLOB_TILDE)
# define GLOB_TILDE 0
#endif

/* Support large files on ARM and 32-bit machines */
#if defined(__arm__) || defined(__i386__)
# define _FILE_OFFSET_BITS 64
# define _TIME_BITS 64 /* Address Y2038 problem for 32 bits machines */
#endif

/* _NO_LIRA implies _NO_MAGIC */
#if defined(_NO_LIRA) && !defined(_NO_MAGIC)
# define _NO_MAGIC
#endif

#if (defined(__linux__) || defined(__CYGWIN__) || defined(__HAIKU__)) \
&& !defined(_BE_POSIX)
/* du(1) can report sizes in bytes, apparent sizes, and take custom block sizes */
# define HAVE_GNU_DU
#endif

#ifndef _NO_GETTEXT
# include <libintl.h>
#endif
#include <regex.h>
#include <stdlib.h>
#include <sys/stat.h> /* S_BLKSIZE */

#if defined(__linux__)
# include <linux/version.h>
# include <linux/limits.h>
# include <sys/inotify.h>
# include <sys/types.h>
# define LINUX_INOTIFY
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) \
|| defined(__DragonFly__)
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>
# include <sys/param.h>
# include <sys/syslimits.h>
# define BSD_KQUEUE
#elif defined(__APPLE__)
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>
# define BSD_KQUEUE
#elif defined(__sun) || defined(__CYGWIN__)
# include <sys/types.h>
# include <sys/time.h>
#elif defined(__HAIKU__)
# include <stdint.h> /* uint8_t */
#endif /* __linux__ */

#ifndef __BEGIN_DECLS
# ifdef __cpluplus
#  define __BEGIN_DECLS extern "C" {
# else
#  define __BEGIN_DECLS
# endif
#endif

#ifndef __END_DECLS
# ifdef __cpluplus
#  define __END_DECLS }
# else
#  define __END_DECLS
# endif
#endif

#include "strings.h"
#include "settings.h"

#define _PROGRAM_NAME "CliFM"
#define PROGRAM_NAME "clifm"
#define PNL "clifm" /* Program name lowercase */
#define PROGRAM_DESC "The command line file manager"
#define VERSION "1.11.7"
#define AUTHOR "L. Abramovich"
#define CONTACT "https://github.com/leo-arch/clifm"
#define DATE "Apr 10, 2023"
#define LICENSE "GPL2+"
#define COLORS_REPO "https://github.com/leo-arch/clifm-colors"

#if defined(__TINYC__)
void *__dso_handle;
#endif /* __TINYC__ */

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#ifndef PATH_MAX
# ifdef __linux__
#  define PATH_MAX 4096
# else
#  define PATH_MAX 1024
# endif /* __linux */
#endif /* PATH_MAX */

#ifndef HOST_NAME_MAX
# if defined(__ANDROID__)
#  define HOST_NAME_MAX 255
# else
#  define HOST_NAME_MAX 64
# endif /* __ANDROID__ */
#endif /* !HOST_NAME_MAX */

#ifndef NAME_MAX
# define NAME_MAX 255
#endif

/* Used by FILE_SIZE and FILE_SIZE_PTR macros to calculate file sizes */
#ifndef S_BLKSIZE
# define S_BLKSIZE 512 /* Not defined in Termux */
#endif

/*#define CMD_LEN_MAX (PATH_MAX + ((NAME_MAX + 1) << 1)) */
#ifndef ARG_MAX
# ifdef __linux__
#  define ARG_MAX 128 * 1024
# else
#  define ARG_MAX 512 * 1024
# endif /* __linux__ */
#endif /* ARG_MAX */

/* _GNU_SOURCE is only defined if __linux__ is defined and _BE_POSIX is not defined */
#ifdef _GNU_SOURCE
# if (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 28))
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#   define _STATX
#  endif /* LINUX_VERSION (4.11) */
# endif /* __GLIBC__ >= 2.28 */
# if (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 3))
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 0)
#   define _LINUX_XATTR
#  endif /* LINUX_VERSION (2.4) */
# endif /* __GLIBC__ */
#endif /* _GNU_SOURCE */

/* Because capability.h is deprecated in BSD */
#if __linux__
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
#  define _LINUX_CAP
# endif /* LINUX_VERSION (2.6.24)*/
#endif /* __linux__ */

// NOT SUPPORTED ON TERMUX. NOT SURE ABOUT CYGWIN. CHECK.
#if defined(__linux__) && !defined(_BE_POSIX) && !defined(__TERMUX__) \
&& !defined(__CYGWIN__)
# define LINUX_FILE_ATTRS
#endif

/* Event handling */
#if defined(LINUX_INOTIFY)
# define NUM_EVENT_SLOTS 32 /* Make room for 32 events */
# define EVENT_SIZE (sizeof(struct inotify_event))
# define EVENT_BUF_LEN (EVENT_SIZE * NUM_EVENT_SLOTS)
extern int inotify_fd, inotify_wd;
extern unsigned int INOTIFY_MASK;
#elif defined(BSD_KQUEUE)
# define NUM_EVENT_SLOTS 10
# define NUM_EVENT_FDS   10
extern int kq, event_fd;
extern struct kevent events_to_monitor[];
extern unsigned int KQUEUE_FFLAGS;
extern struct timespec timeout;
#endif /* LINUX_INOTIFY */
extern int watch;

/* The following flags are used via an integer (FLAGS). If an integer has
 * 4 bytes, then we can use a total of 32 flags (0-31)
 * 4 * 8 == 32 bits == (1 << 31)
 * NOTE: setting (1 << 31) gives a negative value: DON'T USE
 * NOTE 2: What if int size isn't 4 bytes or more, but 2 (16 bits)? In this
 * case, if we want to support old 16 bit machines, we shouldn't use more than
 * 16 bits per flag, that is (1 << 15) */

/* Internal flags */
#define GUI                 (1 << 0)
#define IS_USRVAR_DEF       (1 << 1)

/* Used by the refresh on resize feature */
#define DELAYED_REFRESH     (1 << 2)
#define PATH_PROGRAMS_ALREADY_LOADED (1 << 3)

#define FIRST_WORD_IS_ELN   (1 << 4)
#define IN_BOOKMARKS_SCREEN (1 << 5)
#define STATE_COMPLETING    (1 << 6)
/* Instead of a completion for the current word, a BAEJ suggestion points to
 * a possible completion as follows: WORD > COMPLETION */
#define BAEJ_SUGGESTION     (1 << 7)
#define STATE_SUGGESTING    (1 << 8)
#define IN_SELBOX_SCREEN    (1 << 9)
#define MULTI_SEL           (1 << 10)
#define PREVIEWER           (1 << 11)
#define KITTY_TERM          (1 << 12)
#define NO_FIX_RL_POINT     (1 << 13)
#define FAILED_ALIAS        (1 << 14)

/* Flags for third party binaries */
#define FZF_BIN_OK     (1 << 0)
#define FZY_BIN_OK     (1 << 1)
#define SMENU_BIN_OK   (1 << 2)
#define GNU_DU_BIN_DU  (1 << 3)
/* 'gdu' is the GNU version of 'du' used by BSD systems */
#define GNU_DU_BIN_GDU (1 << 4)

/* File ownership flags (used by check_file_access() in checks.c) */
#define R_USR (1 << 1)
#define X_USR (1 << 2)
#define R_GRP (1 << 3)
#define X_GRP (1 << 4)
#define R_OTH (1 << 5)
#define X_OTH (1 << 6)

/* Flag to control the search function behavior */
#define NO_GLOB_CHAR (1 << 0)

/* Search strategy */
#define GLOB_ONLY  0
#define REGEX_ONLY 1
#define GLOB_REGEX 2

#define GLOB_CHARS "*?[{"
#define GLOB_REGEX_CHARS "*?[{|^+$."

/* Used by log_msg() to know wether to tell prompt() to print messages or not */
#define PRINT_PROMPT   1
#define NOPRINT_PROMPT 0

/* Terminal columns taken by the last line of the default prompt */
#define FALLBACK_PROMPT_OFFSET 6

/* A few macros for the _err function */
#define ERR_NO_LOG   -1 /* _err prints but doesn't log */
#define ERR_NO_STORE -2 /* _err prints and logs, but doesn't store the msg into the messages array */

/* Macros for xchdir (for setting term title or not) */
#define SET_TITLE 1
#define NO_TITLE  0

/* Macros for cd_function */
#define CD_PRINT_ERROR    1
#define CD_NO_PRINT_ERROR 0

/* Macros for the count_dir function. CPOP tells the function to only
 * check if a given directory is populated (it has at least 3 files) */
#define CPOP    1
#define NO_CPOP 0

#define BACKGROUND 1
#define FOREGROUND 0

/* A few fixed colors */
#define _RED    (conf.colorize == 1 ? "\x1b[1;31m" : "")
#define _BGREEN (conf.colorize == 1 ? "\x1b[1;32m" : "")
#define D_CYAN  (conf.colorize == 1 ? "\x1b[0;36m" : "")
#define BOLD    (conf.colorize == 1 ? "\x1b[1m" : "")
/* NC: Reset color attributes to terminal defaults */
#define NC      (conf.colorize == 1 ? "\x1b[0m" : "")

/* Format to use for suggestions when running colorless */
#define SUG_NO_COLOR "\x1b[0m" /* No color */

/* Colors for the prompt: */
/* \001 and \002 tell readline that color codes between them are
 * non-printing chars. This is specially useful for the prompt, i.e.,
 * when passing color codes to readline */
#define RL_NC "\001\x1b[0m\002"

#define UNSET -1
/* MinJumpRank takes -1 as a valid value. So, let's use -2 to mark it unset */
#define JUMP_UNSET -2

/* Macros for the cp and mv cmds */
#define CP_CP            0 /* cp -iRp */
#define CP_CP_FORCE      1 /* cp -Rp */
#define CP_ADVCP         2 /* advcp -giRp */
#define CP_ADVCP_FORCE   3 /* advcp -gRp */
#define CP_WCP           4 /* wcp */
#define CP_RSYNC         5 /* rsync -avP */
#define CP_CMD_AVAILABLE 6

#define MV_MV            0 /* mv -i */
#define MV_MV_FORCE      1 /* mv */
#define MV_ADVMV         2 /* advmv -gi */
#define MV_ADVMV_FORCE   3 /* advmv -g */
#define MV_CMD_AVAILABLE 4

/* Macros for listing_mode */
#define VERTLIST 0 /* ls-like listing mode */
#define HORLIST  1

/* Sort macros */
#define SNONE      0
#define SNAME      1
#define STSIZE     2
#define SATIME     3
#define SBTIME     4
#define SCTIME     5
#define SMTIME     6
#define SVER       7
#define SEXT       8
#define SINO       9
#define SOWN       10
#define SGRP       11
#define SORT_TYPES 11

/* Macros for the colors_list function */
#define NO_ELN        0
#define NO_NEWLINE    0
#define NO_PAD        0
#define PRINT_NEWLINE 1

/* A few key macros used by the auto-suggestions system */
#define _ESC   27
#define _TAB   9
#define BS     8
#define DELETE 127
#define ENTER  13

/* Macros to specify suggestions type */
#define NO_SUG         0
#define HIST_SUG       1
#define FILE_SUG       2
#define CMD_SUG        3
#define INT_CMD        4
#define COMP_SUG       5
#define BOOKMARK_SUG   6
#define ALIAS_SUG      7
#define ELN_SUG        8
#define FIRST_WORD     9
#define JCMD_SUG       10
#define JCMD_SUG_NOACD 11 /* No auto-cd */
#define VAR_SUG	       12
#define SEL_SUG	       13
#define BACKDIR_SUG	   14
#define TAGT_SUG       15 /* t:TAG (expand tag TAG) */
#define TAGC_SUG       16 /* :TAG (param to tag command) */
#define TAGS_SUG       17 /* TAG  (param to tag command) */
#define BM_NAME_SUG    18 /* Bookmark names */
#define SORT_SUG       19
#define PROMPT_SUG     20
#define USER_SUG       21
#define WS_NUM_SUG     22 /* Workspace number */
#define WS_NAME_SUG    23 /* Workspace name */
#define FASTBACK_SUG   24
#define FUZZY_FILENAME 25
#define CMD_DESC_SUG   26
#define NET_SUG        27
#define CSCHEME_SUG    28
#define INT_HELP_SUG   29
#define PROFILE_SUG    30
#define BM_PREFIX_SUG  31 /* Bookmark name (b:NAME) */
#define DIRHIST_SUG    32

/* 46 == \x1b[00;38;02;000;000;000;00;48;02;000;000;000m\0 (24bit, RGB
 * true color format including foreground and background colors, the SGR
 * (Select Graphic Rendition) parameter, and, of course, the terminating
 * null byte. */
#define MAX_COLOR 46

/* Macros to control file descriptors in exec functions */
#define E_NOFLAG   0
#define E_NOSTDIN  (1 << 1)
#define E_NOSTDOUT (1 << 2)
#define E_NOSTDERR (1 << 3)
#define E_MUTE     (E_NOSTDOUT | E_NOSTDERR)

/* Macros for the backdir (bd) function */
#define BD_TAB    1
#define BD_NO_TAB 0

/* Macros for the clear_suggestion function */
#define CS_FREEBUF 1
#define CS_KEEPBUF 0

/* Macros for the xmagic function */
#define MIME_TYPE 1
#define TEXT_DESC 0

/* Macros for the dirjump function */
#define SUG_JUMP    0
#define NO_SUG_JUMP 1

/* Macros for the media_menu function */
#define MEDIA_LIST 	0
#define MEDIA_MOUNT	1

/* Macros for the rl_highlight function */
#define SET_COLOR    1
#define INFORM_COLOR 0

/* Are we trimming a file name with a extension? */
#define TRIM_NO_EXT 1
#define TRIM_EXT    2

#define __MB_LEN_MAX 16

/* OpenBSD recommends the use of ten trailing X's. See mkstemp(3) */
#if defined(__OpenBSD__)
# define TMP_FILENAME ".tempXXXXXXXXXX"
#else
# define TMP_FILENAME ".tempXXXXXX"
#endif /* __OpenBSD__ */

#ifndef P_tmpdir
# define P_tmpdir "/tmp"
#endif

#if defined(__HAIKU__) || defined(__sun)
# define DT_UNKNOWN 0
# define DT_FIFO    1
# define DT_CHR     2
# define DT_DIR     4
# define DT_BLK     6
# define DT_REG     8
# define DT_LNK     10
# define DT_SOCK    12
#endif /* __HAIKU__ || __sun */

#define DT_NONE     14

/* Macros for the get_sys_shell function */
#define SHELL_NONE 0
#define SHELL_BASH 1
#define SHELL_DASH 2
#define SHELL_FISH 3
#define SHELL_KSH  4
#define SHELL_TCSH 5
#define SHELL_ZSH  6

#define BELL_NONE    0
#define BELL_AUDIBLE 1
#define BELL_VISIBLE 2
#define BELL_FLASH   3

#define SECURE_ENV_FULL   1
#define SECURE_ENV_IMPORT 0

/* Macros for the sanitization function */
/* Commands send to the system shell and taken from an untrusted source,
 * mostly config files, need to be sanitized first */
#define SNT_MIME      0
#define SNT_PROMPT    1
#define SNT_PROFILE   2
#define SNT_AUTOCMD   3
#define SNT_NET       4
#define SNT_GRAL      5
#define SNT_DISPLAY   6 /* Sanitize DISPLAY environment variable */
#define SNT_MISC      7 /* Used to sanitize a few environment variables */
#define SNT_NONE      8 /* Trusted command: do not sanitize*/
#define SNT_BLACKLIST 9

/* Macros for the TYPE field of the filter_t struct */
#define FILTER_NONE      0
#define FILTER_FILE_NAME 1 /* Regex */
#define FILTER_FILE_TYPE 2 /* =x */
#define FILTER_MIME_TYPE 3 /* @query */

/* Macros for properties string fields in long view */
#if defined(_LINUX_XATTR)
# define PROP_FIELDS_SIZE 7 /* Seven available fields */
#else
# define PROP_FIELDS_SIZE 6 /* Six available fields */
#endif /* _LINUX_XATTR */

#define PERM_SYMBOLIC 1
#define PERM_NUMERIC  2

#define PROP_TIME_ACCESS 1
#define PROP_TIME_MOD    2
#define PROP_TIME_CHANGE 3

#define PROP_SIZE_BYTES  1
#define PROP_SIZE_HUMAN  2

/* Macros for fzf_preview_border_type */
#define FZF_BORDER_BOLD    0
#define FZF_BORDER_BOTTOM  1
#define FZF_BORDER_DOUBLE  2
#define FZF_BORDER_HORIZ   3
#define FZF_BORDER_LEFT    4
#define FZF_BORDER_NONE    5
#define FZF_BORDER_ROUNDED 6
#define FZF_BORDER_SHARP   7
#define FZF_BORDER_TOP     8
#define FZF_BORDER_VERT    9

/* Flags to skip fuzzy matching based on what we're comparing */
#define FUZZY_FILES_ASCII 0
#define FUZZY_FILES_UTF8  1
#define FUZZY_BM_NAMES    2
#define FUZZY_HISTORY     3
#define FUZZY_ALGO_MAX    2 /* We have two fuzzy algorithms */

#define JUMP_ENTRY_PURGED -1

#define MAX_TIME_STR 256

/* Function macros */
#define itoa xitoa /* itoa does not exist in some OS's */
#define atoi xatoi /* xatoi is just a secure atoi */

#ifndef _NO_GETTEXT
# define _(String) gettext(String)
#else
# define _(String) String
#endif /* !_GETTEXT */

#define strlen(s) xstrnlen(s)

#if (defined(__linux__) || defined(__CYGWIN__)) && defined(_BE_POSIX)
# define strcasestr xstrcasestr
#endif /* (__linux || __CYGWIN__) && _BE_POSIX */

#define ENTRY_N 64

/* Macros to calculate file sizes */
#define FILE_SIZE_PTR (conf.apparent_size == 1 ? attr->st_size : attr->st_blocks * S_BLKSIZE)
#define FILE_SIZE (conf.apparent_size == 1 ? attr.st_size : attr.st_blocks * S_BLKSIZE)

#define UNUSED(x) (void)x /* Just silence the compiler's warning */
#define TOUPPER(ch) (((ch) >= 'a' && (ch) <= 'z') ? ((ch) - 'a' + 'A') : (ch))

/* UINT_MAX is 4294967295 == 10 digits */
#define DIGINUM(n) (((n) < 10) ? 1 \
		: ((n) < 100)        ? 2 \
		: ((n) < 1000)       ? 3 \
		: ((n) < 10000)      ? 4 \
		: ((n) < 100000)     ? 5 \
		: ((n) < 1000000)    ? 6 \
		: ((n) < 10000000)   ? 7 \
		: ((n) < 100000000)  ? 8 \
		: ((n) < 1000000000) ? 9 \
				      : 10)
#define IS_DIGIT(n) ((unsigned int)(n) >= '0' && (unsigned int)(n) <= '9')
#define IS_ALPHA(n) ((unsigned int)(n) >= 'a' && (unsigned int)(n) <= 'z')

#define SELFORPARENT(n) (*(n) == '.' && (!(n)[1] || ((n)[1] == '.' && !(n)[2])))

#define FILE_URI_PREFIX_LEN 7
#define IS_FILE_URI(f) ((f)[4] == ':' \
				&& (f)[FILE_URI_PREFIX_LEN] \
				&& strncmp((f), "file://", FILE_URI_PREFIX_LEN) == 0)

#define IS_HELP(s) (*(s) == '-' && (((s)[1] == 'h' && !(s)[2]) \
				|| strcmp((s), "--help") == 0))

/* TERMINAL ESCAPE CODES */
#define CLEAR \
	if (term_caps.home == 1 && term_caps.clear == 1) { \
		if (term_caps.del_scrollback == 1) \
			fputs("\x1b[H\x1b[2J\x1b[3J", stdout); \
		else \
			fputs("\x1b[H\x1b[J", stdout); \
	}

#define MOVE_CURSOR_DOWN(n)      printf("\x1b[%dB", (n))  /* CUD */

/* ######## Escape sequences used by the suggestions system */
#define MOVE_CURSOR_UP(n)        printf("\x1b[%dA", (n))  /* CUU */
#define MOVE_CURSOR_RIGHT(n)     printf("\x1b[%dC", (n))  /* CUF */
#define MOVE_CURSOR_LEFT(n)      printf("\x1b[%dD", (n))  /* CUB */
#define ERASE_TO_RIGHT           fputs("\x1b[0K", stdout) /* EL0 */
#define ERASE_TO_LEFT            fputs("\x1b[1K", stdout) /* EL1 */
#define ERASE_TO_RIGHT_AND_BELOW fputs("\x1b[J", stdout)  /* ED0 */

#define	SUGGEST_BAEJ(offset,color) printf("\x1b[%dC%s>\x1b[0m ", (offset), (color))
/* ######## */

/* Sequences used by the pad_filename function (listing.c):
 * MOVE_CURSOR_RIGHT() */

/* Sequences used by the pager (listing.c):
 * MOVE_CURSOR_DOWN(n)
 * ERASE_TO_RIGHT */

#define META_SENDS_ESC  fputs("\x1b[?1036h", stdout)
#define HIDE_CURSOR     fputs(term_caps.hide_cursor == 1 ? "\x1b[?25l" : "", stdout) /* DECTCEM */
#define UNHIDE_CURSOR   fputs(term_caps.hide_cursor == 1 ? "\x1b[?25h" : "", stdout)

#define SET_RVIDEO      fputs("\x1b[?5h", stderr) /* DECSCNM: Enable reverse video */
#define UNSET_RVIDEO    fputs("\x1b[?5l", stderr)
#define SET_LINE_WRAP   fputs("\x1b[?7h", stderr) /* DECAWM */
#define UNSET_LINE_WRAP fputs("\x1b[?7l", stderr)
#define RING_BELL       fputs("\007", stderr)

/*
// See https://bestasciitable.com
#define __CTRL(n) ((n) & ~(1 << 6)) // Just unset (set to zero) the seventh bit
#define ___CTRL(n) ((n) & 0x3f) // Same as __CTRL
#define _SHIFT(n) ((n) & ~(1 << 5)) // Unset the sixth bit

// As defined by readline. Explanation:
// 0x1ff == 0011111
// So, this bitwise AND operation: 'n & 0x1f', means: set to zero whatever
// bits in N that are zero in 0x1f, that is, the sixth and seventh bits
#define _CTRL(n)  ((n) & 0x1f)
// As defined by readline: set to one the eight bit in N
#define _META(n)  ((n) | 0x80)
// Ex: 'A' == 01000001
//     ('A' | 0x80) == 11000001 == 193 == Á
 */

				/** #########################
				 *  #    GLOBAL VARIABLES   #
				 *  ######################### */

/* User settings (mostly from the config file) */
struct config_t {
	int apparent_size;
	int auto_open;
	int autocd;
	int autols;
	int case_sens_dirjump;
	int case_sens_path_comp;
	int case_sens_search;
	int case_sens_list; // files list
	int cd_on_quit;
	int classify;
	int clear_screen;
	int cmd_desc_sug;
	int colorize;

	int color_lnk_as_target;

	int columned;
	int cp_cmd;
	int desktop_notifications;
	int dirhist_map;
	int disk_usage;
	int ext_cmd_ok;
	int files_counter;
	int full_dir_size;
	int fuzzy_match;
	int fuzzy_match_algo;
	int fzf_preview;
	int highlight;
#ifndef _NO_ICONS
	int icons;
#else
	int pad1; /* Keep the struct alignment */
#endif /* !_NO_ICONS */
	int light_mode;
	int list_dirs_first;
	int listing_mode;
	int log_cmds;
	int logs_enabled;
	int long_view;
	int max_dirhist;
	int max_hist;
	int max_jump_total_rank;
	int max_log;
	int max_name_len;

	int max_name_len_bk;

	int max_path;
	int max_printselfiles;
	int min_jump_rank;
	int min_name_trim;
	int mv_cmd;
	int no_eln;
	int only_dirs;
	int pager;
	int purge_jumpdb;
	int print_selfiles;
	int private_ws_settings;
	int relative_time;
	int restore_last_path;
	int rm_force;
	int search_strategy;
	int share_selbox;
	int show_hidden;
	int sort;
	int sort_reverse;
	int splash_screen;
	int suggest_filetype_color;
	int suggestions;
	int tips;

	int trim_names;

#ifndef _NO_TRASH
	int tr_as_rm;
#else
	int pad2; /* Keep the struct alignment */
#endif /* !_NO_TRASH */
	int unicode;
	int warning_prompt;
	int welcome_message;
	int pad3;

	char *opener;
	char *encoded_prompt;
	char *term;
	char *time_str;
	char *welcome_message_str;
	char *wprompt_str;
#ifndef _NO_SUGGESTIONS
	char *suggestion_strategy;
#else
	char *pad4; /* Keep the struct alignment */
#endif /* !_NO_SUGGESTIONS */
	char *usr_cscheme;
	char *fzftab_options;
};

extern struct config_t conf;

/* Store information about the current files filter */
struct filter_t {
	char *str;
	int rev;
	int type;
	int env;
	int pad;
};

extern struct filter_t filter;

/* Struct to store information about the current user */
struct user_t {
	char *home;
	char *name;
	char *shell;
	size_t home_len;
	uid_t uid;
	gid_t gid;     /* Primary user group */
	gid_t *groups; /* Secondary groups ID's */
	int ngroups;   /* Number of secondary groups */
	int pad;
};

extern struct user_t user;

/* Struct to store user defined variables */
struct usrvar_t {
	char *name;
	char *value;
};

extern struct usrvar_t *usr_var;

/* Struct to store user defined actions */
struct actions_t {
	char *name;
	char *value;
};

extern struct actions_t *usr_actions;

/* Workspaces information */
struct ws_t {
	char *path;
	char *name;
	int num;
	int pad;
};

extern struct ws_t *workspaces;

/* Struct to store user defined keybindings */
struct kbinds_t {
	char *function;
	char *key;
};

extern struct kbinds_t *kbinds;

/* Struct to store the dirjump database values */
struct jump_t {
	char *path;
	int keep;
	int rank;
	size_t len;
	size_t visits;
	time_t first_visit;
	time_t last_visit;
};

extern struct jump_t *jump_db;

/* Struct to store bookmarks */
struct bookmarks_t {
	char *shortcut;
	char *name;
	char *path;
};

extern struct bookmarks_t *bookmarks;

struct alias_t {
	char *name;
	char *cmd;
};

extern struct alias_t *aliases;

/* Struct to store files information */
struct fileinfo {
	char *color;
	char *ext_color;
	char *ext_name;
#ifndef _NO_ICONS
	char *icon;
	char *icon_color;
#endif
	char *name;
	int dir;
	int eln_n;
	int exec;
	int filesn; /* Number of files in subdir. Is a signed integer enough? */
	int ruser;  /* User read permission for dir */
	int symlink;
	int sel;
	int xattr;
	size_t len;
	mode_t mode; /* Store st_mode (for long view mode) */
	mode_t type; /* Store d_type value */
	ino_t inode;
	off_t size;
	uid_t uid;
	gid_t gid;
	nlink_t linkn;
	time_t ltime; /* For long view mode */
	time_t time;
	dev_t rdev; /* To calculate major and minor devs in long view */
};

extern struct fileinfo *file_info;

struct devino_t {
	dev_t dev;
	ino_t ino;
	char mark;
	char pad1;
	char pad2;
	char pad3;
	int pad4;
};

extern struct devino_t *sel_devino;

struct autocmds_t {
	char *pattern;
	char *color_scheme;
	char *cmd;
	int long_view;
	int light_mode;
	int files_counter;
	int max_files;
	int max_name_len;
	int show_hidden;
	int sort;
	int sort_reverse;
	int pager;
	int only_dirs;
};

extern struct autocmds_t *autocmds;

struct opts_t {
	struct filter_t filter;
	char *color_scheme;
	int files_counter;
	int light_mode;
	int list_dirs_first;
	int long_view;
	int max_files;
	int max_name_len;
	int only_dirs;
	int pager;
	int show_hidden;
	int sort;
	int sort_reverse;
	int pad;
};

extern struct opts_t opts;
extern struct opts_t workspace_opts[MAX_WS];

/* Struct to specify which parameters have been set from the command
 * line, to avoid overriding them with init_config(). While no command
 * line parameter will be overriden, the user still can modifiy on the
 * fly (editing the config file) any option not specified in the command
 * line */
struct param_t {
	int apparent_size;
	int auto_open;
	int autocd;
	int autojump;
	int autols;
	int bell_style;
	int bm_file;
	int case_sens_dirjump;
	int case_sens_path_comp;
	int case_sens_list;
	int clear_screen;
	int colorize;
	int columns;
	int config;
	int cwd_in_title;
	int desktop_notifications;
	int dirmap;
	int disk_usage;
	int cd_on_quit;
	int check_cap;
	int check_ext;
	int classify;
	int color_scheme;
	int disk_usage_analyzer;
	int eln_use_workspace_color;
	int ext;
	int dirs_first;
	int files_counter;
	int follow_symlinks;
	int full_dir_size;
	int fuzzy_match;
	int fuzzy_match_algo;
	int fzf_preview;
#ifndef _NO_FZF
	int fzftab;
	int fzytab;
	int smenutab;
#endif
	int hidden;
#ifndef _NO_HIGHLIGHT
	int highlight;
#endif
	int history;
	int horizontal_list;
#ifndef _NO_ICONS
	int icons;
#endif
	int icons_use_file_color;
	int int_vars;
	int list_and_quit;
	int light;
	int logs;
	int longview;
	int max_dirhist;
	int max_files;
	int max_path;
	int mount_cmd;
	int no_dirjump;
	int noeln;
	int only_dirs;
	int open;
	int pager;
	int path;
	int preview;
	int printsel;
	int refresh_on_empty_line;
	int refresh_on_resize;
	int restore_last_path;
	int rl_vi_mode;
	int secure_cmds;
	int secure_env;
	int secure_env_full;
	int sel_file;
	int share_selbox;
	int si; /* Sizes in powers of 1000 instead of 1024 */
	int sort;
	int sort_reverse;
	int splash;
	int stealth_mode;
#ifndef _NO_SUGGESTIONS
	int suggestions;
#endif
	int tips;
#ifndef _NO_TRASH
	int trasrm;
#endif
	int trim_names;
	int virtual_dir_full_paths;
	int vt100;
	int welcome_message;
	int warning_prompt;
};

extern struct param_t xargs;

/* Struct to store remotes information */
struct remote_t {
	char *desc;
	char *name;
	char *mount_cmd;
	char *mountpoint;
	char *unmount_cmd;
	int auto_mount;
	int auto_unmount;
	int mounted;
	int padding;
};

extern struct remote_t *remotes;

/* Store information about the current suggestion */
struct suggestions_t {
	int filetype;
    int printed;
	int type;
    int offset;
	char *color;
	size_t full_line_len;
	size_t nlines;
};

extern struct suggestions_t suggestion;

/* Hold information about selected files */
struct sel_t {
	char *name;
	off_t size;
};

extern struct sel_t *sel_elements;

/* File statistics for the current directory. Used by the 'stats' command */
struct stats_t {
	size_t dir;
	size_t reg;
	size_t exec;
	size_t hidden;
	size_t suid;
	size_t sgid;
	size_t fifo;
	size_t socket;
	size_t block_dev;
	size_t char_dev;
	size_t caps;
	size_t link;
	size_t broken_link;
	size_t multi_link;
	size_t other_writable;
	size_t sticky;
	size_t extended;
	size_t unknown;
	size_t unstat; /* Non-statable file */
};

extern struct stats_t stats;

struct sort_t {
	const char *name;
	int num;
	int pad;
};

extern struct sort_t _sorts[];

/* Prompts and prompt settings */
struct prompts_t {
	char *name;
	char *regular;
	char *warning;
	int notifications;
	int warning_prompt_enabled;
};

extern struct prompts_t *prompts;

/* System messages */
struct msgs_t {
	size_t error;
	size_t warning;
	size_t notice;
};
extern struct msgs_t msgs;

/* A few termcap values to know whether we can use some terminal features */
struct termcaps_t {
	int color;
	int suggestions;
	int pager;
	int hide_cursor;
	int home;  // Move cursor to line 1, column 1
	int clear; // ED (erase display)
	int del_scrollback; // E3
	int req_cur_pos; // CPR (cursor position request)
};
extern struct termcaps_t term_caps;

/* Data to be displayed in the properties string in long mode */
struct props_t {
	int counter; /* Files counter */
	int perm; /* File permissions; either NUMERIC or SYMBOLIC */
	int ids; /* User and group IDs */
	int time; /* Time: either ACCESS, MOD, or CHANGE */
	int size; /* File size: either HUMAN or BYTES */
	int inode; /* File inode number */
	int xattr; /* Extended attributes */
	int len; /* Approx len of the entire properties string taking into account
	the above values */
};
extern struct props_t prop_fields;

struct cmdslist_t {
	char *name;
	size_t len;
};

extern const struct cmdslist_t param_str[];
extern const struct cmdslist_t internal_cmds[];

extern size_t internal_cmds_n;

struct history_t {
	char *cmd;
	size_t len;
	time_t date;
};
extern struct history_t *history;

#define SHADE_TYPE_UNSET     0
#define SHADE_TYPE_8COLORS   1
#define SHADE_TYPE_256COLORS 2
#define SHADE_TYPE_TRUECOLOR 3

#define NUM_SHADES 6
// Structs to hold color info for size and date fields in file properties
struct rgb_t {
	uint8_t attr;
	uint8_t R;
	uint8_t G;
	uint8_t B;
};

struct shades_t {
	uint8_t type;
	struct rgb_t shades[NUM_SHADES];
};
extern struct shades_t date_shades;
extern struct shades_t size_shades;

struct paths_t {
	char *path;
	time_t mtime;
};
extern struct paths_t *paths;

enum tab_mode {
	STD_TAB =   0,
	FZF_TAB =   1,
	FZY_TAB =   2,
	SMENU_TAB = 3
};

extern enum tab_mode tabmode;

/* A list of possible program messages. Each value tells the prompt what
 * to do with error messages: either to print an E, W, or N char at the
 * beginning of the prompt, or nothing (nomsg) */
enum prog_msg {
	NOMSG =   0,
	ERROR =   1,
	WARNING = 2,
	NOTICE =  4
};

/* pmsg holds the current program message type */
extern enum prog_msg pmsg;

/* Enumeration for the dirjump function options */
enum jump {
	NONE = 	  0,
	JPARENT = 1,
	JCHILD =  2,
	JORDER =  4,
	JLIST =	  8
};

enum comp_type {
	TCMP_BOOKMARK =	  0,
	TCMP_CMD =		  1,
	TCMP_CSCHEME = 	  2,
	TCMP_DESEL = 	  3,
	TCMP_ELN =		  4,
	TCMP_HIST =       5,
	TCMP_JUMP =       6,
	TCMP_NET =	      7,
	TCMP_NONE =       8,
	TCMP_OPENWITH =   9,
	TCMP_PATH =       10,
	TCMP_PROF =       11,
	TCMP_RANGES = 	  12,
	TCMP_SEL =	      13,
	TCMP_SORT =       14,
	TCMP_TRASHDEL =	  15,
	TCMP_UNTRASH =    16,
	TCMP_BACKDIR =    17,
	TCMP_ENVIRON =    18,
	TCMP_TAGS_T =     19, /* T keyword: 't:TAG' */
	TCMP_TAGS_C =     20, /* Colon: 'tag file :TAG' */
	TCMP_TAGS_S =     21, /* Simple completion: 'tag rm TAG' */
	TCMP_TAGS_F =     22, /* Tagged files completion: 't:FULL_TAG_NAME' */
	TCMP_TAGS_U =     23, /* Tagged files for the untag function */
	TCMP_ALIAS =      24,
	TCMP_PROMPTS =    25,
	TCMP_USERS =      26,
	TCMP_GLOB =       27,
	TCMP_FILE_TYPES_OPTS = 28,
	TCMP_FILE_TYPES_FILES = 29,
	TCMP_WORKSPACES = 30,
	TCMP_BM_PATHS =   31, /* 'b:FULLNAME' keyword expansion */
	TCMP_BM_PREFIX =  32, /* 'b:' keyword expansion */
	TCMP_CMD_DESC =   33,
	TCMP_OWNERSHIP =  34,
	TCMP_DIRHIST =    35,
	TCMP_MIME_LIST =  36,
	TCMP_MIME_FILES = TCMP_FILE_TYPES_FILES /* Same behavior */
};

extern enum comp_type cur_comp_type;

extern struct termios orig_termios;

/* Bit flag holders */
extern int
	flags,
	bin_flags,
	search_flags;

/* Internal state flags */
extern int
	argc_bk, /* A copy of argc taken from main() */
	autocmd_set,
	autojump,
	bell,
	bg_proc,
	check_cap,
	check_ext,
	cmdhist_flag,
	config_ok,
	cur_ws,
	curcol,
	dequoted,
	dir_changed, /* flag to know is dir was changed: used by autocmds */
	dir_out, /* Autocommands: A .cfm.out file was found in CWD*/
	dirhist_cur_index,
	dirhist_total_index,
	exit_code,
	follow_symlinks,
	fzftab,
	fzf_height_set,
	fzf_open_with,
	fzf_preview_border_type,
	hist_status,
	home_ok,
	int_vars,
	internal_cmd,
	is_sel,
	jump_total_rank,
	kbind_busy,
	max_files,
	mime_match,
	no_log,
	open_in_foreground, /* Overrides mimelist file value: used by mime_open */
	prev_ws,
	print_msg,
	print_removed_files,
	prompt_offset,
	prompt_notif,
	recur_perm_error_flag,
	rl_nohist,
	rl_notab,
	sel_is_last,
	selfile_ok,
	shell,
	shell_is_interactive,
	shell_terminal,
	sort_switch,
	switch_cscheme,
#ifndef _NO_TRASH
	trash_ok,
#endif
	wrong_cmd,
	xrename; /* We're running a secondary prompt for the rename function */

extern unsigned short term_cols, term_lines;

extern size_t
	actions_n,
	aliases_n,
	args_n,
	autocmds_n,
	bm_n,
	cdpath_n,
	config_dir_len,
	cschemes_n,
	current_hist_n,
	curhistindex,
	ext_colors_n,
	files, /* Amount of files in the current dir. Is size_t enough? */
	jump_n,
	kbinds_n,
	longest,
	msgs_n,
	nwords,
	P_tmpdir_len,
	path_n,
	path_progsn,
	prompt_cmds_n,
	prompts_n,
	remotes_n,
	sel_n,
	tab_offset,
	tags_n,
	trash_n,
	usrvar_n,
	zombies;

extern struct termios shell_tmodes;
extern pid_t own_pid;
extern time_t props_now;

extern char
	cur_prompt_name[NAME_MAX + 1],
	div_line[NAME_MAX + 1],
	hostname[HOST_NAME_MAX + 1],
#ifndef _NO_FZF
	finder_in_file[PATH_MAX + 1],
	finder_out_file[PATH_MAX + 1],
#endif /* _NO_FZF */
	_fmatch[PATH_MAX + 1], /* First regular match if fuzzy matching is enabled */
	prop_fields_str[PROP_FIELDS_SIZE + 1],
	invalid_time_str[MAX_TIME_STR],

	*actions_file,
	*alt_config_dir,
	*alt_bm_file,
	*alt_config_file,
	*alt_kbinds_file,
	*alt_preview_file,
	*alt_profile,
	*bin_name,
	*bm_file,
	*colors_dir,
	*config_dir,
	*config_dir_gral,
	*config_file,
	*cur_color,
	*cur_tag,
	*data_dir,
	*cur_cscheme,
	*dirhist_file,
	*file_cmd_path,
	*hist_file,
	*kbinds_file,
	*jump_suggestion,
	*last_cmd,
	*log_file,
	*mime_file,
	*pinned_dir,
	*plugins_dir,
	*profile_file,
	*prompts_file,
	*quote_chars,
	*rl_callback_handler_input,
	*remotes_file,
/*	*right_prompt, */
	*sel_file,
	*smenutab_options_env,
	*stdin_tmp_dir,
#ifndef _NO_SUGGESTIONS
	*suggestion_buf,
#endif
	*tags_dir,
	*tmp_dir,
#ifndef _NO_TRASH
	*trash_dir,
	*trash_files_dir,
	*trash_info_dir,
#endif

	**argv_bk,
	**bin_commands,
	**cdpaths,
	**color_schemes,
	**ext_colors,
	**messages,
	**old_pwd,
	**profile_names,
	**prompt_cmds,
	**tags;

extern regex_t regex_exp;
extern char **environ;

/* To store all the 39 color variables we use, with 46 bytes each, we need
 * a total of 1,8Kb. It's not much but it could be less if we'd use
 * dynamically allocated arrays for them (which, on the other side,
 * would make the whole thing slower and more tedious) */

/* Colors */
extern char
	/* File types */
	bd_c[MAX_COLOR], /* Block device */
	ca_c[MAX_COLOR], /* Cap file */
	cd_c[MAX_COLOR], /* Char device */
	ed_c[MAX_COLOR], /* Empty dir */
	ee_c[MAX_COLOR], /* Empty executable */
	ex_c[MAX_COLOR], /* Executable */
	ef_c[MAX_COLOR], /* Empty reg file */
	fi_c[MAX_COLOR], /* Reg file */
	di_c[MAX_COLOR], /* Directory */
	ln_c[MAX_COLOR], /* Symlink */
	mh_c[MAX_COLOR], /* Multi-hardlink file */
	nd_c[MAX_COLOR], /* No read directory */
	ne_c[MAX_COLOR], /* No read empty dir */
	nf_c[MAX_COLOR], /* No read file */
	no_c[MAX_COLOR], /* Unknown */
	or_c[MAX_COLOR], /* Broken symlink */
	ow_c[MAX_COLOR], /* Other writable */
	pi_c[MAX_COLOR], /* FIFO, pipe */
	sg_c[MAX_COLOR], /* SGID file */
	so_c[MAX_COLOR], /* Socket */
	st_c[MAX_COLOR], /* Sticky (not ow)*/
	su_c[MAX_COLOR], /* SUID file */
	tw_c[MAX_COLOR], /* Sticky other writable */
	uf_c[MAX_COLOR], /* Non-'stat'able file */

	/* Interface */
	bm_c[MAX_COLOR], /* Bookmarked directory */
	fc_c[MAX_COLOR], /* Files counter */
	df_c[MAX_COLOR], /* Default color */
	dl_c[MAX_COLOR], /* Dividing line */
	el_c[MAX_COLOR], /* ELN color */
	mi_c[MAX_COLOR], /* Misc indicators */
	ts_c[MAX_COLOR], /* TAB completion suffix */
	tt_c[MAX_COLOR], /* Tilde for trimmed files */
	wc_c[MAX_COLOR], /* Welcome message */
	wp_c[MAX_COLOR], /* Warning prompt */

	/* Suggestions */
	sb_c[MAX_COLOR], /* Auto-suggestions: shell builtins */
	sc_c[MAX_COLOR], /* Auto-suggestions: external commands and aliases */
	sd_c[MAX_COLOR], /* Auto-suggestions: internal commands description */
	sf_c[MAX_COLOR], /* Auto-suggestions: file names */
	sh_c[MAX_COLOR], /* Auto-suggestions: history */
	sp_c[MAX_COLOR], /* Auto-suggestions: BAEJ suggestions pointer */
	sx_c[MAX_COLOR], /* Auto-suggestions: internal commands and parameters */
	sz_c[MAX_COLOR], /* Auto-suggestions: file names (fuzzy) */

#ifndef _NO_ICONS
    dir_ico_c[MAX_COLOR], /* Directories icon color */
#endif

	/* Syntax highlighting */
	hb_c[MAX_COLOR], /* Brackets () [] {} */
	hc_c[MAX_COLOR], /* Comments */
	hd_c[MAX_COLOR], /* Paths (slashes) */
	he_c[MAX_COLOR], /* Expansion operators: * ~ */
	hn_c[MAX_COLOR], /* Numbers */
	hp_c[MAX_COLOR], /* Parameters: - */
	hq_c[MAX_COLOR], /* Quoted strings */
	hr_c[MAX_COLOR], /* Redirection > */
	hs_c[MAX_COLOR], /* Process separators | & ; */
	hv_c[MAX_COLOR], /* Variables $ */
	hw_c[MAX_COLOR], /* Wrong, non-existent command name */

	dr_c[MAX_COLOR], /* Read */
	dw_c[MAX_COLOR], /* Write */
	dxd_c[MAX_COLOR], /* Execute (dirs) */
	dxr_c[MAX_COLOR], /* Execute (reg files) */
	dg_c[MAX_COLOR], /* UID, GID */
	dd_c[MAX_COLOR], /* Date */
	dz_c[MAX_COLOR], /* Size (dirs) > */
	do_c[MAX_COLOR], /* Octal representation > */
	dp_c[MAX_COLOR], /* Special files (SUID, SGID, etc) */
	dn_c[MAX_COLOR], /* dash (none) */

    /* Colors used in the prompt, so that \001 and \002 needs to
	 * be added. This is why MAX_COLOR + 2 */
	/* Workspaces */
	ws1_c[MAX_COLOR + 2],
	ws2_c[MAX_COLOR + 2],
	ws3_c[MAX_COLOR + 2],
	ws4_c[MAX_COLOR + 2],
	ws5_c[MAX_COLOR + 2],
	ws6_c[MAX_COLOR + 2],
	ws7_c[MAX_COLOR + 2],
	ws8_c[MAX_COLOR + 2],

	em_c[MAX_COLOR + 2], /* Error msg */
	li_c[MAX_COLOR + 2], /* Sel indicator */
	li_cb[MAX_COLOR],    /* Sel indicator (for the files list) */
	nm_c[MAX_COLOR + 2], /* Notice msg */
	ti_c[MAX_COLOR + 2], /* Trash indicator */
	tx_c[MAX_COLOR + 2], /* Text */
	si_c[MAX_COLOR + 2], /* Stealth indicator */
	wm_c[MAX_COLOR + 2], /* Warning msg */
	xs_c[MAX_COLOR + 2], /* Exit code: success */
	xf_c[MAX_COLOR + 2], /* Exit code: failure */
	tmp_color[MAX_COLOR + 2]; /* A temp buffer to store color codes */

#endif /* HELPERS_H */
