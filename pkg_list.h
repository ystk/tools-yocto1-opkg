/*
 * Copyright (C) 2009 Ubiq Technologies Pty Ltd, <graham.gower@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PKG_LIST_H
#define PKG_LIST_H

void pkg_list_toggle(pkg_t *pkg, GtkTreeModel *model, GtkTreeIter iter);
void pkg_list_foreach(void (*func)(pkg_t *, GtkTreeModel *, GtkTreeIter *));
void pkg_list_free(void);

extern unsigned int pkg_list_len;

#endif /* PKG_LIST_H */
