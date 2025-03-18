/* sort.h */

/*
 * This file is part of Clifm
 *
 * Copyright (C) 2016-2025, L. Abramovich <leo.clifm@outlook.com>
 * All rights reserved.

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

#ifndef SORT_H
#define SORT_H

/* Needed by qsort(3) to use compare_string() as comparison function */
typedef int QSFUNC(const void *, const void *);

__BEGIN_DECLS

int  alphasort_insensitive(const struct dirent **a, const struct dirent **b);
int  compare_strings(char **s1, char **s2);
int  entrycmp(const void *a, const void *b);
char *num_to_sort_name(const int n, const int abbrev);
void print_sort_method(void);
int  skip_files(const struct dirent *ent);
int  sort_function(char **arg);
int  xalphasort(const struct dirent **a, const struct dirent **b);

__END_DECLS

#endif /* SORT_H */
