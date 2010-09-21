/*
 * Copyright (C) 2009 Ubiq Technologies Pty Ltd, <graham.gower@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include <libopkg/opkg.h>
#include <libopkg/pkg_parse.h>

#include "pkg_list.h"

static GtkWidget *window;
static GtkListStore *store;
static GtkWidget *pbar, *statusbar;
static guint status_ctx;
static GtkTextBuffer *msg_buf;
static GtkWidget *err_label;

static int n_actions;

enum
{
	COL_ACTION = 0,
	COL_TICK,
	COL_PKGNAME,
	COL_VERS,
	COL_DESC,
	NUM_COLS
};


/*
 * printf, but in a GtkDialog popup.
 */
void
popupf(const char *title, const char *fmt, ...)
{
	char *str;
	va_list ap;
	GtkWidget *dialog, *label;

	va_start(ap, fmt);
	str = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_STOCK_OK,
			GTK_RESPONSE_ACCEPT,
			NULL);
	g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy),
			dialog);

	label = gtk_label_new(str);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), label,
			TRUE, TRUE, 0);
	gtk_widget_show_all(dialog);

	g_free(str);
}

static gboolean
callback_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{

	pkg_list_free();
	opkg_free();
	gtk_main_quit();
	return FALSE;
}


static void
add_package_to_store(pkg_t *pkg, void *user_data)
{
	char *v;
	GtkTreeIter iter;
	GtkListStore *store = user_data;
	gboolean installed;

	/* If you want to install these, use the command line opkg client. */
	if (strstr(pkg->name, "-dbg") || strstr(pkg->name, "-dev"))
		return;

	v = pkg_version_str_alloc(pkg);
	installed = (pkg->state_status == SS_INSTALLED ? TRUE : FALSE);

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter,
			COL_ACTION, "",
			COL_TICK, installed,
			COL_PKGNAME, pkg->name,
			COL_VERS, v,
			COL_DESC, pkg->description,
			-1);

	free(v);
}

void
populate_store(GtkListStore *store)
{
	opkg_list_packages(add_package_to_store, store);
}

gboolean
view_selection_func(GtkTreeSelection *selection, GtkTreeModel *model,
	GtkTreePath *path, gboolean path_currently_selected, gpointer user_data)
{
	GtkTreeIter iter;
	pkg_t *pkg;
	gchar *name, *version, *action, *desc, *sb_str;
	GtkListStore *store = GTK_LIST_STORE(model);

	if (path_currently_selected) {
		gtk_statusbar_pop(GTK_STATUSBAR(statusbar), status_ctx);
		return TRUE;
	}

	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		popupf("%s: gtk_tree_model_get_iter failed. Wtf?\n",
				__FUNCTION__);
		return FALSE;
	}

	gtk_tree_model_get(model, &iter, COL_ACTION, &action, -1);
	gtk_tree_model_get(model, &iter, COL_PKGNAME, &name, -1);
	gtk_tree_model_get(model, &iter, COL_VERS, &version, -1);
	gtk_tree_model_get(model, &iter, COL_DESC, &desc, -1);

	sb_str = g_strdup_printf("%s: %s", version, desc);
	gtk_statusbar_pop(GTK_STATUSBAR(statusbar), status_ctx);
	gtk_statusbar_push(GTK_STATUSBAR(statusbar), status_ctx, sb_str);
	g_free(desc);
	g_free(sb_str);

/*
	printf("%s: %s %s: %d: %s\n", __FUNCTION__, name, version,
			path_currently_selected, action);
*/

	pkg = pkg_hash_fetch_by_name_version(name, version);
	if (pkg == NULL) {
		popupf("%s: Internal error, can't find pkg %s %s\n",
				__FUNCTION__, name, version);
		g_free(name);
		g_free(version);
		g_free(action);
		return FALSE;
	}

	g_free(name);
	g_free(version);

	if (pkg->state_status == SS_INSTALLED) {
		if (action[0] == '-')
			gtk_list_store_set(store, &iter, COL_ACTION, "", -1);
		else
			gtk_list_store_set(store, &iter, COL_ACTION, "-", -1);
	} else {
		if (action[0] == '+')
			gtk_list_store_set(store, &iter, COL_ACTION, "", -1);
		else
			gtk_list_store_set(store, &iter, COL_ACTION, "+", -1);
	}

	g_free(action);

	pkg_list_toggle(pkg, model, iter);

	return TRUE;
}

GtkWidget *
create_view_and_model(void)
{
	GtkCellRenderer *renderer;
	GtkTreeModel *model;
	GtkWidget *view;
	GtkTreeSelection *sel;

	view = gtk_tree_view_new();

	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
	gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_set_select_function(sel, view_selection_func,
			NULL, NULL);

	store = gtk_list_store_new(NUM_COLS,
			G_TYPE_STRING, G_TYPE_BOOLEAN,
			G_TYPE_STRING, G_TYPE_STRING,
			G_TYPE_STRING);
	populate_store(store);

	model = GTK_TREE_MODEL(store);

	/* Column #1, +/- */
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
		-1, "Action", renderer, "text", COL_ACTION, NULL);

	/* Column #1, checkbox */
	renderer = gtk_cell_renderer_toggle_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
		-1, "Installed", renderer, "active", COL_TICK, NULL);

	/* Column #2, package name */
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
		-1, "Package", renderer, "text", COL_PKGNAME, NULL);

	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(view));
	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);

	gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);
	g_object_unref(model);

	return view;
}


static void
callback_progress(const opkg_progress_data_t *prog, void *user_data)
{
	gdouble fraction;

	fraction = ((gboolean)prog->percentage/100.0f) + n_actions;
	fraction /= pkg_list_len ? pkg_list_len : 1;

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pbar), fraction);

	while (gtk_events_pending())
		gtk_main_iteration();

}

static void
rm_action(pkg_t *pkg, GtkTreeModel *model, GtkTreeIter *iter)
{
	gtk_list_store_set(GTK_LIST_STORE(model), iter, COL_ACTION, "", -1);
}

static void
do_action(pkg_t *pkg, GtkTreeModel *model, GtkTreeIter *iter)
{
	GtkListStore *store = GTK_LIST_STORE(model);

	if (pkg->state_status == SS_INSTALLED) {
		if (!opkg_remove_package(pkg->name, callback_progress, NULL))
			gtk_list_store_set(store, iter, COL_TICK, 0, -1);
	} else {
		if (!opkg_install_package(pkg->name, callback_progress, NULL))
			gtk_list_store_set(store, iter, COL_TICK, 1, -1);
	}

	n_actions++;
	rm_action(pkg, model, iter);
}

static gboolean
button_callback_apply(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	n_actions = 0;
	pkg_list_foreach(do_action);
	pkg_list_free();

	return FALSE;
}

static gboolean
button_callback_update(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	/* All the pkg_t pointers will be invalid, so discard them. */
	pkg_list_foreach(rm_action);
	pkg_list_free();

	n_actions = 0;

	if (opkg_update_package_lists(callback_progress, NULL)) {
		popupf("update failure", "Failed to update list of pacakges"
			" from repository. See error log for more details.");
	}

	/* Refresh the visible package list. */
	gtk_list_store_clear(store);
	populate_store(store);

	return FALSE;
}

static gboolean
button_callback_upgrade(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	/* All the pkg_t pointers will be invalid, so discard them. */
	pkg_list_foreach(rm_action);
	pkg_list_free();

	n_actions = 0;

	if (opkg_upgrade_all(callback_progress, NULL)) {
		popupf("upgrade failure", "One or more packages failed during"
			" upgrade. See error log for more details.");
	}

	/* Refresh the visible package list. */
	gtk_list_store_clear(store);
	populate_store(store);

	return FALSE;
}


static GtkWidget *
create_toolbar()
{
	GtkWidget *toolbar;
	GtkToolItem *item;

	toolbar = gtk_toolbar_new();

	item = gtk_tool_button_new_from_stock(GTK_STOCK_APPLY);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), item, -1);
	g_signal_connect(G_OBJECT(item), "clicked",
			G_CALLBACK(button_callback_apply), NULL);

	item = gtk_tool_button_new_from_stock(GTK_STOCK_REFRESH);
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(item), "update");
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), item, -1);
	g_signal_connect(G_OBJECT(item), "clicked",
			G_CALLBACK(button_callback_update), NULL);

	item = gtk_tool_button_new_from_stock(GTK_STOCK_NETWORK);
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(item), "upgrade");
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), item, -1);
	g_signal_connect(G_OBJECT(item), "clicked",
			G_CALLBACK(button_callback_upgrade), NULL);

	return toolbar;
}

static GtkWidget *
create_status_bar()
{
	GtkWidget *box;

	pbar = gtk_progress_bar_new();
	statusbar = gtk_statusbar_new();
	status_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar),
			"context-is-a-dumb-idea");

	box = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), pbar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), statusbar, FALSE, FALSE, 0);

	return box;
}

static void
switch_notebook_page(GtkNotebook *nb, GtkNotebookPage *page,
		guint page_num, gpointer user_data)
{
	if (page_num == 1)
		gtk_label_set_markup(GTK_LABEL(err_label), "Errors");
}

static void
vmessage(int level, const char *fmt, va_list ap)
{
	char *str;
	GtkTextIter iter;

	str = g_strdup_vprintf(fmt, ap);

	gtk_text_buffer_get_end_iter(msg_buf, &iter);

	if (level == ERROR) {
		GtkTextTag *tag;
		tag = gtk_text_buffer_create_tag(msg_buf, NULL,
				"background", "red",
				"foreground", "black", NULL);
		gtk_text_buffer_insert_with_tags(msg_buf, &iter, str, -1,
				tag, NULL);
		gtk_label_set_markup(GTK_LABEL(err_label),
				"<span background='red' foreground='black'>"
				"Errors</span>");
	} else {
		gtk_text_buffer_insert(msg_buf, &iter, str, -1);
	}

	free(str);
}
	
int
main(int argc, char **argv)
{
	GtkWidget *pkg_view, *msg_view;
	GtkWidget *pkg_window, *msg_window;
	GtkWidget *window_frame;
	GtkWidget *toolbar;
	GtkWidget *status;
	GtkWidget *notebook;

	gtk_init(&argc, &argv);

	msg_view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(msg_view), FALSE);
	msg_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(msg_view));


	conf->pfm = PFM_SOURCE;
	conf->verbosity = NOTICE;
	conf->autoremove = 1;	/* i think this is generally what users want */

	if (opkg_new()) {
		fprintf(stderr, "Failed to initialise libopkg, bailing.\n");
		return -1;
	}

	conf->opkg_vmessage = vmessage;


	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(window, "delete_event", G_CALLBACK(callback_delete),
			NULL);

	toolbar = create_toolbar();
	status = create_status_bar();

	pkg_window = gtk_scrolled_window_new(NULL, NULL);
	pkg_view = create_view_and_model();
	gtk_container_add(GTK_CONTAINER(pkg_window), pkg_view);

	msg_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(msg_window), msg_view);


	window_frame = gtk_vbox_new(FALSE, 0);
	notebook = gtk_notebook_new();

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), pkg_window,
			gtk_label_new("Packages"));
	err_label = gtk_label_new("Errors");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), msg_window,
			err_label);

	g_signal_connect(notebook, "switch-page",
			G_CALLBACK(switch_notebook_page), NULL);

	gtk_box_pack_start(GTK_BOX(window_frame), toolbar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(window_frame), notebook, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(window_frame), status, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(window), window_frame);


	gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
	gtk_widget_show_all(window);

	gtk_main();
	return 0;
}
