/*
 * Copyright (C) 2009 Ubiq Technologies Pty Ltd, <graham.gower@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>

#include <libopkg/pkg.h>
#include <gtk/gtk.h>

struct pkg_list {
	struct pkg_list *next;
	pkg_t *pkg;

	/* gtk+ stuff */
	GtkTreeModel *model;
	GtkTreeIter iter;
};

static struct pkg_list *head = NULL;

unsigned int pkg_list_len = 0;

static void
pkg_list_add(pkg_t *pkg, GtkTreeModel *model, GtkTreeIter iter)
{
	struct pkg_list *pl;

	pl = malloc(sizeof(struct pkg_list));
	if (pl == NULL) {
		perror("malloc");
		return;
	}

	pl->next = head;
	pl->pkg = pkg;
	pl->model = model;
	pl->iter = iter;

	head = pl;

	pkg_list_len++;
}

static pkg_t *
pkg_list_del(pkg_t *pkg)
{
	struct pkg_list *pl, *prev = NULL;

	for (pl=head; pl; pl=pl->next) {
		if (pl->pkg == pkg)
			break;
		prev = pl;
	}

	if (pl == NULL)
		return NULL;

	if (pl == head)
		head = head->next;
	else
		prev->next = pl->next;

	free(pl);

	pkg_list_len--;

	return pkg;
}

/*
 * Remove the package if its in the list, add it if its not.
 */
void
pkg_list_toggle(pkg_t *pkg, GtkTreeModel *model, GtkTreeIter iter)
{
	if (pkg_list_del(pkg))
		return;

	pkg_list_add(pkg, model, iter);
}

void
pkg_list_foreach(void (*func)(pkg_t *, GtkTreeModel *, GtkTreeIter *))
{
	struct pkg_list *pl;

	for (pl=head; pl; pl=pl->next)
		func(pl->pkg, pl->model, &pl->iter);
}

void
pkg_list_free(void)
{
	struct pkg_list *pl, *tmp;

	pl = head;
	while (pl) {
		tmp = pl;
		pl = pl->next;
		free(tmp);
	}

	pkg_list_len = 0;
	head = NULL;
}

