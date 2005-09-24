/********************************************************************\
 * dialog-options.c -- GNOME option handling                        *
 * Copyright (C) 1998-2000 Linas Vepstas                            *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
\********************************************************************/

#include "config.h"

#include <gnome.h>
#include <g-wrap-wct.h>

#include "dialog-options.h"
#include "dialog-utils.h"
#include "engine-helpers.h"
#include "glib-helpers.h"
#include "global-options.h"
#include "gnc-account-sel.h"
#include "gnc-tree-view-account.h"
#include "gnc-commodity-edit.h"
#include "gnc-general-select.h"
#include "gnc-currency-edit.h"
#include "gnc-date-edit.h"
#include "gnc-engine-util.h"
#include "gnc-engine.h"
#include "gnc-gconf-utils.h"
#include "gnc-gui-query.h"
#include "gnc-ui.h"
#include "guile-util.h"
#include "messages.h"
#include "option-util.h"
#include "gdk/gdkfont.h"
#include "guile-mappings.h"
#include "gnc-date-format.h"
#include "misc-gnome-utils.h"


/* This static indicates the debugging module that this .o belongs to.  */
static short module = MOD_GUI;

/*
 * Point where preferences switch control method from a set of
 * notebook tabs to a list.
 */
#define MAX_TAB_COUNT 4

static GHashTable *optionTable = NULL;

struct gnc_option_win
{
  GtkWidget  * dialog;
  GtkWidget  * notebook;
  GtkWidget  * page_list;

  gboolean toplevel;

  GtkTooltips * tips;

  GNCOptionWinCallback apply_cb;
  gpointer             apply_cb_data;
  
  GNCOptionWinCallback help_cb;
  gpointer             help_cb_data;

  GNCOptionWinCallback close_cb;
  gpointer             close_cb_data;

  /* Hold onto this for a complete reset */
  GNCOptionDB *		option_db;
};

typedef enum {
  GNC_RD_WID_AB_BUTTON_POS = 0,
  GNC_RD_WID_AB_WIDGET_POS,
  GNC_RD_WID_REL_BUTTON_POS,
  GNC_RD_WID_REL_WIDGET_POS} GNCRdPositions;


static GNCOptionWinCallback global_help_cb = NULL;
gpointer global_help_cb_data = NULL;

void gnc_options_dialog_response_cb(GtkDialog *dialog, gint response, GNCOptionWin *window);
static void gnc_options_dialog_reset_cb(GtkWidget * w, gpointer data);
void gnc_options_dialog_list_select_cb(GtkWidget * list, GtkWidget * item, gpointer data);


static void
gnc_options_dialog_changed_internal (GtkWidget *widget, gboolean sensitive)
{
  GtkDialog *dialog;

  while (widget && !GTK_IS_DIALOG(widget))
    widget = gtk_widget_get_parent(widget);
  if (widget == NULL)
    return;

  dialog = GTK_DIALOG(widget);
  gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_OK, sensitive);
  gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_APPLY, sensitive);
}

void
gnc_options_dialog_changed (GNCOptionWin *win)
{
  if (!win) return;

  gnc_options_dialog_changed_internal (win->dialog, TRUE);
}

void 
gnc_option_changed_widget_cb(GtkWidget *widget, GNCOption *option)
{
  gnc_option_set_changed (option, TRUE);
  gnc_option_call_option_widget_changed_proc(option);
  gnc_options_dialog_changed_internal (widget, TRUE);
}

void 
gnc_option_changed_option_cb(GtkWidget *dummy, GNCOption *option)
{
  GtkWidget *widget;

  widget = gnc_option_get_widget (option);
  gnc_option_changed_widget_cb(widget, option);
}

static void
gnc_date_option_set_select_method(GNCOption *option, gboolean use_absolute,
                                  gboolean set_buttons)
{
  GList* widget_list;
  GtkWidget *ab_button, *rel_button, *rel_widget, *ab_widget;
  GtkWidget *widget;

  widget = gnc_option_get_widget (option);

  widget_list = gtk_container_children(GTK_CONTAINER(widget));
  ab_button = g_list_nth_data(widget_list, GNC_RD_WID_AB_BUTTON_POS);
  ab_widget = g_list_nth_data(widget_list, GNC_RD_WID_AB_WIDGET_POS);
  rel_button = g_list_nth_data(widget_list, GNC_RD_WID_REL_BUTTON_POS);
  rel_widget = g_list_nth_data(widget_list, GNC_RD_WID_REL_WIDGET_POS);

  if(use_absolute)
  {
    gtk_widget_set_sensitive(ab_widget, TRUE);
    gtk_widget_set_sensitive(rel_widget, FALSE);
    if(set_buttons)
    {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ab_button), TRUE);
    }
  }
  else
  {
    gtk_widget_set_sensitive(rel_widget, TRUE);
    gtk_widget_set_sensitive(ab_widget, FALSE);
    if (set_buttons)
    {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rel_button), TRUE);
    }
  }
}

static void 
gnc_rd_option_ab_set_cb(GtkWidget *widget, gpointer *raw_option)
{
  GNCOption *option = (GNCOption *) raw_option;
  gnc_date_option_set_select_method(option, TRUE, FALSE);
  gnc_option_changed_option_cb(widget, option);
}

static void
gnc_rd_option_rel_set_cb(GtkWidget *widget, gpointer *raw_option)
{
  GNCOption *option = (GNCOption *) raw_option;
  gnc_date_option_set_select_method(option, FALSE, FALSE);
  gnc_option_changed_option_cb(widget, option);
  return;
}

/********************************************************************\
 * gnc_option_set_ui_value_internal                                 *
 *   sets the GUI representation of an option with either its       *
 *   current guile value, or its default value                      *
 *                                                                  *
 * Args: option      - option structure containing option           *
 *       use_default - if true, use the default value, otherwise    *
 *                     use the current value                        *
 * Return: nothing                                                  *
\********************************************************************/

static void
gnc_option_set_ui_value_internal (GNCOption *option, gboolean use_default)
{
  gboolean bad_value = FALSE;
  GtkWidget *widget;
  char *type;
  SCM getter;
  SCM value;
  GNCOptionDef_t *option_def;

  widget = gnc_option_get_widget (option);
  if (!widget)
    return;

  type = gnc_option_type(option);

  if (use_default)
    getter = gnc_option_default_getter(option);
  else
    getter = gnc_option_getter(option);

  value = scm_call_0(getter);

  option_def = gnc_options_ui_get_option (type);
  if (option_def && option_def->set_value)
  {
    bad_value = option_def->set_value (option, use_default, widget, value);
  }
  else
  {
    PERR("Unknown type. Ignoring.\n");
  }

  if (bad_value)
  {
    PERR("bad value\n");
  }

  free(type);
}


/********************************************************************\
 * gnc_option_get_ui_value_internal                                 *
 *   returns the SCM representation of the GUI option value         *
 *                                                                  *
 * Args: option - option structure containing option                *
 * Return: SCM handle to GUI option value                           *
\********************************************************************/
static SCM
gnc_option_get_ui_value_internal (GNCOption *option)
{
  SCM result = SCM_UNDEFINED;
  GtkWidget *widget;
  char *type;
  GNCOptionDef_t *option_def;

  widget = gnc_option_get_widget (option);
  if (!widget)
    return result;

  type = gnc_option_type(option);

  option_def = gnc_options_ui_get_option (type);
  if (option_def && option_def->get_value)
  {
    result = option_def->get_value (option, widget);
  }
  else
  {
    PERR("Unknown type for refresh. Ignoring.\n");
  }

  free(type);

  return result;
}


/********************************************************************\
 * gnc_option_set_selectable_internal                               *
 *   Change the selectable state of the widget that represents a    *
 *   GUI option.                                                    *
 *                                                                  *
 * Args: option      - option to change widget state for            *
 *       selectable  - if false, update the widget so that it       *
 *                     cannot be selected by the user.  If true,    *
 *                     update the widget so that it can be selected.*
 * Return: nothing                                                  *
\********************************************************************/
static void
gnc_option_set_selectable_internal (GNCOption *option, gboolean selectable)
{
  GtkWidget *widget;

  widget = gnc_option_get_widget (option);
  if (!widget)
    return;

  gtk_widget_set_sensitive (widget, selectable);
}

static void 
gnc_option_default_cb(GtkWidget *widget, GNCOption *option)
{
  gnc_option_set_ui_value (option, TRUE);
  gnc_option_set_changed (option, TRUE);
  gnc_options_dialog_changed_internal (widget, TRUE);
}

static void
gnc_option_multichoice_cb(GtkWidget *w, gint index, gpointer data)
{
  GNCOption *option = data;
  GtkWidget *widget;
  GtkWidget *omenu;
  gpointer _current;
  gint current;

  widget = gnc_option_get_widget (option);

  _current = gtk_object_get_data(GTK_OBJECT(widget), "gnc_multichoice_index");
  current = GPOINTER_TO_INT(_current);

  if (current == index)
    return;

  gtk_option_menu_set_history(GTK_OPTION_MENU(widget), index);
  gtk_object_set_data(GTK_OBJECT(widget), "gnc_multichoice_index",
                      GINT_TO_POINTER(index));
  omenu = gtk_object_get_data(GTK_OBJECT(w), "gnc_option_menu");
  gnc_option_changed_widget_cb(omenu, option);
}

static void
gnc_option_radiobutton_cb(GtkWidget *w, gpointer data)
{
  GNCOption *option = data;
  GtkWidget *widget;
  gpointer _current, _new_value;
  gint current, new_value;

  widget = gnc_option_get_widget (option);

  _current = gtk_object_get_data(GTK_OBJECT(widget), "gnc_radiobutton_index");
  current = GPOINTER_TO_INT (_current);

  _new_value = gtk_object_get_data (GTK_OBJECT(w), "gnc_radiobutton_index");
  new_value = GPOINTER_TO_INT (_new_value);

  if (current == new_value)
    return;

  gtk_object_set_data (GTK_OBJECT(widget), "gnc_radiobutton_index",
		       GINT_TO_POINTER(new_value));
  gnc_option_changed_widget_cb(widget, option);
}

static void
gnc_option_rd_combo_cb(GtkWidget *w, gint index, gpointer data)
{
  GtkWidget *widget, *omenu;
  GList *children;
  GNCOption *option = data;
  gpointer _current;
  gint current;

  children =
    gtk_container_children(GTK_CONTAINER(gnc_option_get_widget (option)));

  widget = g_list_nth_data(children, GNC_RD_WID_REL_WIDGET_POS);
				     
  _current = gtk_object_get_data(GTK_OBJECT(widget),
                                 "gnc_multichoice_index");
  current = GPOINTER_TO_INT(_current);

  if (current == index)
    return;

  gtk_option_menu_set_history(GTK_OPTION_MENU(widget), index);
  gtk_object_set_data(GTK_OBJECT(widget), "gnc_multichoice_index",
                      GINT_TO_POINTER(index));
  omenu = gtk_object_get_data(GTK_OBJECT(w), "gnc_option_menu");
  gnc_option_changed_widget_cb(omenu, option);
}

static GtkWidget *
gnc_option_create_date_widget (GNCOption *option)
{
  GtkWidget * box = NULL;
  GtkWidget *rel_button= NULL, *ab_button=NULL;
  GtkWidget *rel_widget=NULL, *ab_widget=NULL;
  GtkWidget *entry;
  gboolean show_time, use24;
  GNCOptionInfo *info;
  char *type;
  char **raw_strings;
  char **raw;
  int num_values;

  type = gnc_option_date_option_get_subtype(option);
  show_time = gnc_option_show_time(option);
  use24 = gnc_gconf_get_bool(GCONF_GENERAL, "24hour_time", FALSE);

  if (safe_strcmp(type, "relative") != 0)
  {
    ab_widget = gnc_date_edit_new(time(NULL), show_time, use24);
    entry = GNC_DATE_EDIT(ab_widget)->date_entry;
    g_signal_connect(G_OBJECT(entry), "changed",
		     G_CALLBACK(gnc_option_changed_option_cb), option);
    if (show_time)
    {
      entry = GNC_DATE_EDIT(ab_widget)->time_entry;
      g_signal_connect(G_OBJECT(entry), "changed",
		       G_CALLBACK(gnc_option_changed_option_cb), option);
    }
  }
    
  if (safe_strcmp(type, "absolute") != 0)
  { 
    int i;
    num_values = gnc_option_num_permissible_values(option);
    
    g_return_val_if_fail(num_values >= 0, NULL);
    
    info = g_new0(GNCOptionInfo, num_values);
    raw_strings = g_new0(char *, num_values * 2);
    raw = raw_strings;
    
    for (i = 0; i < num_values; i++)
    {
      *raw = gnc_option_permissible_value_name(option, i);
      if (*raw != NULL)
	info[i].name = _(*raw);
      else
	info[i].name = "";
      
      raw++;
      
      *raw = gnc_option_permissible_value_description(option, i);
      if (*raw != NULL)
        info[i].tip = _(*raw);
      else
        info[i].tip = "";
      if(safe_strcmp(type, "both") == 0)
      {
        info[i].callback = gnc_option_rd_combo_cb;
      }
      else 
      {
        info[i].callback = gnc_option_multichoice_cb;
      }
      info[i].user_data = option;

      raw++;
    }

    rel_widget = gnc_build_option_menu(info, num_values);
  
    for (i = 0; i < num_values * 2; i++)
      if (raw_strings[i] != NULL)
        free(raw_strings[i]);

    g_free(raw_strings);
    g_free(info);
  }

  if(safe_strcmp(type, "absolute") == 0)
  {
    free(type);
    gnc_option_set_widget (option, ab_widget);
    return ab_widget;
  }
  else if (safe_strcmp(type, "relative") == 0)
  {
    gnc_option_set_widget (option, rel_widget);
    free(type);

    return rel_widget;
  }
  else if (safe_strcmp(type, "both") == 0)
  {
    box = gtk_hbox_new(FALSE, 5);

    ab_button = gtk_radio_button_new(NULL);
    g_signal_connect(G_OBJECT(ab_button), "toggled",
		     G_CALLBACK(gnc_rd_option_ab_set_cb), option);

    rel_button = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(ab_button));
    g_signal_connect(G_OBJECT(rel_button), "toggled",
		     G_CALLBACK(gnc_rd_option_rel_set_cb), option);

    gtk_box_pack_start(GTK_BOX(box), ab_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), ab_widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), rel_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), rel_widget, FALSE, FALSE, 0);

    free(type);

    gnc_option_set_widget (option, box);

    return box;
  }
  else /* can't happen */
  {
    return NULL;
  }
}


static GtkWidget *
gnc_option_create_multichoice_widget(GNCOption *option)
{
  GtkWidget *widget;
  GNCOptionInfo *info;
  int num_values;
  char **raw_strings;
  char **raw;
  int i;

  num_values = gnc_option_num_permissible_values(option);

  g_return_val_if_fail(num_values >= 0, NULL);

  info = g_new0(GNCOptionInfo, num_values);
  raw_strings = g_new0(char *, num_values * 2);
  raw = raw_strings;

  for (i = 0; i < num_values; i++)
  {
    *raw = gnc_option_permissible_value_name(option, i);
    if (*raw != NULL)
      info[i].name = _(*raw);
    else
      info[i].name = "";

    raw++;

    *raw = gnc_option_permissible_value_description(option, i);
    if (*raw != NULL)
      info[i].tip = _(*raw);
    else
      info[i].tip = "";

    info[i].callback = gnc_option_multichoice_cb;
    info[i].user_data = option;
    raw++;
  }

  widget = gnc_build_option_menu(info, num_values);

  for (i = 0; i < num_values * 2; i++)
    if (raw_strings[i] != NULL)
      free(raw_strings[i]);
  
  g_free(raw_strings);
  g_free(info);

  return widget;
}

static void
radiobutton_destroy_cb (GtkObject *obj, gpointer data)
{
  GtkTooltips *tips = data;

  g_object_unref (tips);
}

static GtkWidget *
gnc_option_create_radiobutton_widget(char *name, GNCOption *option)
{
  GtkTooltips *tooltips;
  GtkWidget *frame, *box;
  GtkWidget *widget = NULL;
  int num_values;
  char *label;
  char *tip;
  int i;

  num_values = gnc_option_num_permissible_values(option);

  g_return_val_if_fail(num_values >= 0, NULL);

  /* Create our button frame */
  frame = gtk_frame_new (name);

  /* Create the button box */
  box = gtk_hbox_new (FALSE, 5);
  gtk_container_add (GTK_CONTAINER (frame), box);

  /* Create the tooltips */
  tooltips = gtk_tooltips_new ();
  g_object_ref (tooltips);
  gtk_object_sink (GTK_OBJECT (tooltips));

  /* Iterate over the options and create a radio button for each one */
  for (i = 0; i < num_values; i++)
  {
    label = gnc_option_permissible_value_name(option, i);
    tip = gnc_option_permissible_value_description(option, i);

    widget =
      gtk_radio_button_new_with_label_from_widget (widget ?
						   GTK_RADIO_BUTTON (widget) :
						   NULL,
						   label ? _(label) : "");
    gtk_object_set_data (GTK_OBJECT (widget), "gnc_radiobutton_index",
			 GINT_TO_POINTER (i));
    gtk_tooltips_set_tip(tooltips, widget, tip ? _(tip) : "", NULL);
    g_signal_connect(G_OBJECT(widget), "toggled",
		     G_CALLBACK(gnc_option_radiobutton_cb), option);
    gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

    if (label)
      free (label);
    if (tip)
      free (tip);
  }

  g_signal_connect (G_OBJECT (frame), "destroy",
		    G_CALLBACK (radiobutton_destroy_cb), tooltips);

  return frame;
}

static void
gnc_option_account_cb(GtkTreeSelection *selection, gpointer data)
{
  GNCOption *option = data;

  gnc_option_changed_widget_cb(GTK_WIDGET(selection), option);
}

static void
gnc_option_account_select_all_cb(GtkWidget *widget, gpointer data)
{
  GNCOption *option = data;
  GncTreeViewAccount *tree_view;
  GtkTreeSelection *selection;

  tree_view = GNC_TREE_VIEW_ACCOUNT(gnc_option_get_widget (option));
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
  gtk_tree_selection_select_all(selection);
  gnc_option_changed_widget_cb(widget, option);
}

static void
gnc_option_account_clear_all_cb(GtkWidget *widget, gpointer data)
{
  GNCOption *option = data;
  GncTreeViewAccount *tree_view;
  GtkTreeSelection *selection;

  tree_view = GNC_TREE_VIEW_ACCOUNT(gnc_option_get_widget (option));
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
  gtk_tree_selection_unselect_all(selection);
  gnc_option_changed_widget_cb(widget, option);
}


static GtkWidget *
gnc_option_create_account_widget(GNCOption *option, char *name)
{
  gboolean multiple_selection;
  GtkWidget *scroll_win;
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *tree;
  GtkWidget *vbox;
  GtkWidget *bbox;
  GList *acct_type_list;
  GtkTreeSelection *selection;

  multiple_selection = gnc_option_multiple_selection(option);
  acct_type_list = gnc_option_get_account_type_list(option);

  frame = gtk_frame_new(name);

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), vbox);

  tree = GTK_WIDGET(gnc_tree_view_account_new (FALSE));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(tree), FALSE);
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(tree));
  if (multiple_selection)
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
  else
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

  if (acct_type_list) {
    GList *node;
    AccountViewInfo avi;
    int i;

    gnc_tree_view_account_get_view_info (GNC_TREE_VIEW_ACCOUNT (tree), &avi);

    for (i = 0; i < NUM_ACCOUNT_TYPES; i++)
      avi.include_type[i] = FALSE;

    for (node = acct_type_list; node; node = node->next) {
      GNCAccountType type = GPOINTER_TO_INT (node->data);
      avi.include_type[type] = TRUE;
    }

    gnc_tree_view_account_set_view_info (GNC_TREE_VIEW_ACCOUNT (tree), &avi);
    g_list_free (acct_type_list);    
  }

  scroll_win = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_win),
                                 GTK_POLICY_AUTOMATIC, 
                                 GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start(GTK_BOX(vbox), scroll_win, FALSE, FALSE, 0);
  gtk_container_border_width(GTK_CONTAINER(scroll_win), 5);
  gtk_container_add(GTK_CONTAINER(scroll_win), tree);

  bbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_SPREAD);
  gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 10);

  if (multiple_selection)
  {
    button = gtk_button_new_with_label(_("Select All"));
    gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);

    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(gnc_option_account_select_all_cb), option);

    button = gtk_button_new_with_label(_("Clear All"));
    gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);

    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(gnc_option_account_clear_all_cb), option);
  }

  button = gtk_button_new_with_label(_("Select Default"));
  gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);

  g_signal_connect(G_OBJECT(button), "clicked",
		   G_CALLBACK(gnc_option_default_cb), option);

  gnc_option_set_widget (option, tree);

  return frame;
}

static void
gnc_option_list_select_cb(GtkCList *clist, gint row, gint column,
                          GdkEventButton *event, gpointer data)
{
  GNCOption *option = data;

  gtk_clist_set_row_data(clist, row, GINT_TO_POINTER(TRUE));
  gnc_option_changed_widget_cb(GTK_WIDGET(clist), option);
}

static void
gnc_option_list_unselect_cb(GtkCList *clist, gint row, gint column,
                            GdkEventButton *event, gpointer data)
{
  GNCOption *option = data;

  gtk_clist_set_row_data(clist, row, GINT_TO_POINTER(FALSE));
  gnc_option_changed_widget_cb(GTK_WIDGET(clist), option);
}

static void
gnc_option_list_select_all_cb(GtkWidget *widget, gpointer data)
{
  GNCOption *option = data;

  gtk_clist_select_all(GTK_CLIST(gnc_option_get_widget (option)));
  gnc_option_changed_widget_cb(widget, option);
}

static void
gnc_option_list_clear_all_cb(GtkWidget *widget, gpointer data)
{
  GNCOption *option = data;

  gtk_clist_unselect_all(GTK_CLIST(gnc_option_get_widget (option)));
  gnc_option_changed_widget_cb(widget, option);
}

static GtkWidget *
gnc_option_create_list_widget(GNCOption *option, char *name)
{
  GtkWidget *scroll_win;
  GtkWidget *top_hbox;
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *clist;
  GtkWidget *hbox;
  GtkWidget *bbox;
  gint num_values;
  gint width;
  gint i;

  top_hbox = gtk_hbox_new(FALSE, 0);

  frame = gtk_frame_new(name);
  gtk_box_pack_start(GTK_BOX(top_hbox), frame, FALSE, FALSE, 0);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), hbox);

  clist = gtk_clist_new(1);
  gtk_clist_column_titles_hide(GTK_CLIST(clist));
  gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_MULTIPLE);

  num_values = gnc_option_num_permissible_values(option);
  for (i = 0; i < num_values; i++)
  {
    gchar *text[1];
    gchar *string;

    string = gnc_option_permissible_value_name(option, i);
    if (string != NULL)
    {
      text[0] = _(string);
      gtk_clist_append(GTK_CLIST(clist), text);
      gtk_clist_set_row_data(GTK_CLIST(clist), i, GINT_TO_POINTER(FALSE));
      free(string);
    }
    else
    {
      PERR("bad value name\n");
    }
  }

  scroll_win = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_win),
                                 GTK_POLICY_NEVER, 
                                 GTK_POLICY_AUTOMATIC);

  width = gtk_clist_columns_autosize(GTK_CLIST(clist));
  gtk_widget_set_usize(scroll_win, width + 50, 0);

  gtk_box_pack_start(GTK_BOX(hbox), scroll_win, FALSE, FALSE, 0);
  gtk_container_border_width(GTK_CONTAINER(scroll_win), 5);
  gtk_container_add(GTK_CONTAINER(scroll_win), clist);

  bbox = gtk_vbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_SPREAD);
  gtk_box_pack_start(GTK_BOX(hbox), bbox, FALSE, FALSE, 10);

  button = gtk_button_new_with_label(_("Select All"));
  gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);

  g_signal_connect(G_OBJECT(button), "clicked",
		   G_CALLBACK(gnc_option_list_select_all_cb), option);

  button = gtk_button_new_with_label(_("Clear All"));
  gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);

  g_signal_connect(G_OBJECT(button), "clicked",
		   G_CALLBACK(gnc_option_list_clear_all_cb), option);

  button = gtk_button_new_with_label(_("Select Default"));
  gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);

  g_signal_connect(G_OBJECT(button), "clicked",
		   G_CALLBACK(gnc_option_default_cb), option);

  gnc_option_set_widget (option, clist);

  return top_hbox;
}

static void
gnc_option_color_changed_cb(GnomeColorPicker *picker, guint arg1, guint arg2,
                            guint arg3, guint arg4, GNCOption *option)
{
  gnc_option_changed_widget_cb(GTK_WIDGET(picker), option);
}

static void
gnc_option_font_changed_cb(GnomeFontPicker *picker, gchar *font_name,
                           GNCOption *option)
{
  gnc_option_changed_widget_cb(GTK_WIDGET(picker), option);
}

static void
gnc_option_set_ui_widget(GNCOption *option,
                         GtkBox *page_box,
                         GtkTooltips *tooltips)
{
  GtkWidget *enclosing = NULL;
  GtkWidget *value = NULL;
  gboolean packed = FALSE;
  char *raw_name, *raw_documentation;
  char *name, *documentation;
  char *type;
  GNCOptionDef_t *option_def;

  ENTER("option %p(%s), box %p, tips %p",
	option, gnc_option_name(option), page_box, tooltips);
  type = gnc_option_type(option);
  if (type == NULL) {
    LEAVE("bad type");
    return;
  }

  raw_name = gnc_option_name(option);
  if (raw_name != NULL)
    name = _(raw_name);
  else
    name = NULL;

  raw_documentation = gnc_option_documentation(option);
  if (raw_documentation != NULL)
    documentation = _(raw_documentation);
  else
    documentation = NULL;

  option_def = gnc_options_ui_get_option (type);
  if (option_def && option_def->set_widget)
  {
    value = option_def->set_widget (option, page_box,
				    tooltips, name, documentation,
				    /* Return values */
				    &enclosing, &packed);
  }
  else
  {
    PERR("Unknown type. Ignoring.\n");
  }

  if (!packed && (enclosing != NULL))
    gtk_box_pack_start(page_box, enclosing, FALSE, FALSE, 0);

  if (value != NULL)
    gtk_tooltips_set_tip(tooltips, value, documentation, NULL);

  if (raw_name != NULL)
    free(raw_name);
  if (raw_documentation != NULL)
    free(raw_documentation);
  free(type);
  LEAVE(" ");
}

static void
gnc_options_dialog_add_option(GtkWidget *page,
                              GNCOption *option,
                              GtkTooltips *tooltips)
{
  gnc_option_set_ui_widget(option, GTK_BOX(page), tooltips);
}

static gint
gnc_options_dialog_append_page(GNCOptionWin * propertybox,
                               GNCOptionSection *section)
{
  GNCOption *option;
  GtkWidget *page_label;
  GtkWidget *options_box;
  GtkWidget *page_content_box;
  GtkWidget* notebook_page;
  GtkWidget *reset_button;
  GtkWidget *listitem = NULL;
  GtkWidget *buttonbox;
  gint num_options;
  const char *name;
  gint i, page_count, name_offset;
  gboolean advanced;

  name = gnc_option_section_name(section);
  if (!name)
    return -1;

  if (strncmp(name, "__", 2) == 0)
    return -1;
  advanced = (strncmp(name, "_+", 2) == 0);
  name_offset = (advanced) ? 2 : 0;
  page_label = gtk_label_new(_(name) + name_offset);
  gtk_widget_show(page_label);

  /* Build this options page */
  page_content_box = gtk_vbox_new(FALSE, 2);

  /* Build space for the content - the options box */
  options_box = gtk_vbox_new(FALSE, 5);
  gtk_container_set_border_width(GTK_CONTAINER(options_box), 0);
  gtk_box_pack_start(GTK_BOX(page_content_box), options_box, TRUE, TRUE, 0);

  /* Create all the options */
  num_options = gnc_option_section_num_options(section);
  for (i = 0; i < num_options; i++)
  {
    option = gnc_get_option_section_option(section, i);
    gnc_options_dialog_add_option(options_box, option,
                                  propertybox->tips);
  }

  /* Add a button box at the bottom of the page */
  buttonbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout (GTK_BUTTON_BOX (buttonbox),
			     GTK_BUTTONBOX_EDGE);
  gtk_container_set_border_width(GTK_CONTAINER (buttonbox), 5);
  gtk_box_pack_end(GTK_BOX(page_content_box), buttonbox, FALSE, FALSE, 0);

  /* Install the lone reset button */
  reset_button = gtk_button_new_with_label (_("Defaults"));
  g_signal_connect(G_OBJECT(reset_button), "clicked",
		   G_CALLBACK(gnc_options_dialog_reset_cb), propertybox);
  gtk_object_set_data(GTK_OBJECT(reset_button), "section", section);
  gtk_box_pack_end(GTK_BOX(buttonbox), reset_button, FALSE, FALSE, 0);
  gtk_widget_show_all(page_content_box);
  gtk_notebook_append_page(GTK_NOTEBOOK(propertybox->notebook), 
                           page_content_box, page_label);

  /* Switch to selection from a list if the page count threshhold is reached */
  page_count = gtk_notebook_page_num(GTK_NOTEBOOK(propertybox->notebook),
				     page_content_box);

  if (propertybox->page_list) {
    /* Build the matching list item for selecting from large page sets */
    listitem = gtk_list_item_new_with_label(_(name) + name_offset);
    gtk_widget_show(listitem);
    gtk_container_add(GTK_CONTAINER(propertybox->page_list), listitem);

    if (page_count > MAX_TAB_COUNT - 1) { /* Convert 1-based -> 0-based */
      gtk_widget_show(propertybox->page_list);
      gtk_notebook_set_show_tabs(GTK_NOTEBOOK(propertybox->notebook), FALSE);
      gtk_notebook_set_show_border(GTK_NOTEBOOK(propertybox->notebook), FALSE);
    }

    /* Tweak "advanced" pages for later handling. */
    if (advanced) {
      notebook_page =
	gtk_notebook_get_nth_page(GTK_NOTEBOOK(propertybox->notebook),
				  page_count);

      gtk_object_set_data(GTK_OBJECT(notebook_page), "listitem", listitem);
      gtk_object_set_data(GTK_OBJECT(notebook_page), "advanced",
			  GINT_TO_POINTER(advanced));
    }
  }

  return(page_count);
}


/********************************************************************\
 * gnc_build_options_dialog_contents                                *
 *   builds an options dialog given a property box and an options   *
 *   database                                                       *
 *                                                                  *
 * Args: propertybox - gnome property box to use                    *
 *       odb         - option database to use                       *
 * Return: nothing                                                  *
\********************************************************************/
void
gnc_build_options_dialog_contents(GNCOptionWin *propertybox,
                                  GNCOptionDB  *odb)
{
  GNCOptionSection *section;
  gchar *default_section_name;
  gint default_page = -1;
  gint num_sections;
  gint page;
  gint i;
  guint j;

  g_return_if_fail (propertybox != NULL);
  g_return_if_fail (odb != NULL);

  gnc_option_db_set_ui_callbacks (odb,
                                  gnc_option_get_ui_value_internal,
                                  gnc_option_set_ui_value_internal,
                                  gnc_option_set_selectable_internal);

  propertybox->tips = gtk_tooltips_new();
  propertybox->option_db = odb;

  g_object_ref (propertybox->tips);
  gtk_object_sink (GTK_OBJECT (propertybox->tips));

  num_sections = gnc_option_db_num_sections(odb);
  default_section_name = gnc_option_db_get_default_section(odb);

  for (i = 0; i < num_sections; i++)
  {
    const char *section_name;

    section = gnc_option_db_get_section(odb, i);
    page = gnc_options_dialog_append_page(propertybox, section);

    section_name = gnc_option_section_name(section);
    if (safe_strcmp(section_name, default_section_name) == 0)
      default_page = page;
  }

  if (default_section_name != NULL)
    free(default_section_name);

  /* call each option widget changed callbacks once at this point,
   * now that all options widgets exist.
   */
  for (i = 0; i < num_sections; i++)
  {
    section = gnc_option_db_get_section(odb, i);

    for (j = 0; j < gnc_option_section_num_options(section); j++)
    {
      gnc_option_call_option_widget_changed_proc(
              gnc_get_option_section_option(section, j) );
    }
  }

  gtk_notebook_popup_enable(GTK_NOTEBOOK(propertybox->notebook));
  if (default_page >= 0) {
    gtk_notebook_set_current_page(GTK_NOTEBOOK(propertybox->notebook), default_page);
    gtk_list_select_item(GTK_LIST(propertybox->page_list), default_page);
  } else {
    /* GTKList doesn't default to selecting the first item. */
    gtk_list_select_item(GTK_LIST(propertybox->page_list), 0);
  }
  gnc_options_dialog_changed_internal(propertybox->dialog, FALSE);
  gtk_widget_show(propertybox->dialog);
}


GtkWidget *
gnc_options_dialog_widget(GNCOptionWin * win)
{
  return win->dialog;
}

GtkWidget *
gnc_options_dialog_notebook(GNCOptionWin * win)
{
  return win->notebook;
}

void
gnc_options_dialog_response_cb(GtkDialog *dialog, gint response, GNCOptionWin *window)
{
  switch (response) {
   case GTK_RESPONSE_HELP:
    if(window->help_cb)
      (window->help_cb)(window, window->help_cb_data);
    break;

   case GTK_RESPONSE_OK:
   case GTK_RESPONSE_APPLY:
    if (window->apply_cb)
      window->apply_cb (window, window->apply_cb_data);
    gnc_options_dialog_changed_internal (window->dialog, FALSE);
    if (response == GTK_RESPONSE_APPLY)
      break;
    /* fall through */

   default:
    if (window->close_cb) {
      (window->close_cb)(window, window->close_cb_data);
    } else {
      gtk_widget_hide(window->dialog);
    }
  }
}

static void
gnc_options_dialog_reset_cb(GtkWidget * w, gpointer data)
{
  GNCOptionWin *win = data;
  GNCOptionSection *section;
  gpointer val;

  val = gtk_object_get_data(GTK_OBJECT(w), "section");
  g_return_if_fail (val);
  g_return_if_fail (win);

  section = (GNCOptionSection*)val;
  gnc_option_db_section_reset_widgets (section);
  gnc_options_dialog_changed_internal (win->dialog, TRUE);
}

void
gnc_options_dialog_list_select_cb(GtkWidget * list, GtkWidget * item,
				  gpointer data)
{
  GNCOptionWin * win = data;
  gint index;

  g_return_if_fail (list);
  g_return_if_fail (win);

  index = gtk_list_child_position(GTK_LIST(list), item);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(win->notebook), index);
}

void
gnc_options_register_stocks (void)
{
#if 0
	static gboolean done = FALSE;
	
	GtkStockItem items[] = {
		{ GTK_STOCK_APPLY		,"gnc_option_apply_button",	0, 0, NULL },
		{ GTK_STOCK_HELP		,"gnc_options_dialog_help",	0, 0, NULL },
		{ GTK_STOCK_OK			,"gnc_options_dialog_ok",	0, 0, NULL },
		{ GTK_STOCK_CANCEL		,"gnc_options_dialog_cancel",	0, 0, NULL },
	};

	if (done) 
	{
		return;
	}
	done = TRUE;

	gtk_stock_add (items, G_N_ELEMENTS (items));
#endif
}


GNCOptionWin *
gnc_options_dialog_new(gchar *title)
{
  GNCOptionWin * retval;
  GladeXML *xml;
  GtkWidget * hbox;

  retval = g_new0(GNCOptionWin, 1);
  xml = gnc_glade_xml_new ("preferences.glade", "Gnucash Preferences");
  retval->dialog = glade_xml_get_widget (xml, "Gnucash Preferences");
  retval->page_list = glade_xml_get_widget (xml, "page_list");

  glade_xml_signal_autoconnect_full( xml,
                                     gnc_glade_autoconnect_full_func,
                                     retval );

  if (title)
    gtk_window_set_title(GTK_WINDOW(retval->dialog), title);

  /* glade doesn't suport a notebook with zero pages */
  hbox = glade_xml_get_widget (xml, "notebook placeholder");
  retval->notebook = gtk_notebook_new();
  gtk_widget_show(retval->notebook);
  gtk_box_pack_start(GTK_BOX(hbox), retval->notebook, TRUE, TRUE, 5);

  return retval;
}

GNCOptionWin *
gnc_options_dialog_new_w_dialog(gchar *title, GtkWidget *dialog)
{
  GNCOptionWin * retval;

  retval = g_new0(GNCOptionWin, 1);
  retval->dialog = dialog;
  return retval;
}

void 
gnc_options_dialog_set_apply_cb(GNCOptionWin * win, GNCOptionWinCallback cb,
                                gpointer data)
{
  win->apply_cb = cb;
  win->apply_cb_data = data;
}

void 
gnc_options_dialog_set_help_cb(GNCOptionWin * win, GNCOptionWinCallback cb,
                               gpointer data)
{
  win->help_cb = cb;
  win->help_cb_data = data;
}

void 
gnc_options_dialog_set_close_cb(GNCOptionWin * win, GNCOptionWinCallback cb,
                                gpointer data)
{
  win->close_cb = cb;
  win->close_cb_data = data;
}

void
gnc_options_dialog_set_global_help_cb(GNCOptionWinCallback thunk,
                                      gpointer cb_data)
{
  global_help_cb = thunk;
  global_help_cb_data = cb_data;
}

void
gnc_options_dialog_destroy(GNCOptionWin * win)
{
  if (!win) return;

  gtk_widget_destroy(win->dialog);

  if(win->tips) {
    g_object_unref (win->tips);
  }

  win->dialog = NULL;
  win->notebook = NULL;
  win->apply_cb = NULL;
  win->help_cb = NULL;
  win->tips = NULL;

  g_free(win);
}


/* Global options dialog... this should house all of the config
 * options like where the docs reside, and whatever else is deemed
 * necessary */

static void
gnc_option_show_advanced_pages(GNCOptionWin * propertybox)
{
  GtkWidget *page, *listitem;
  gboolean advanced, show_advanced;
  gint i;

  show_advanced = gnc_lookup_boolean_option("General",
					    "Show Advanced Settings", FALSE);
  i = 0;
  while (TRUE) {
    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(propertybox->notebook), i++);
    if (page == NULL)
      break;
    advanced =
      GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(page), "advanced"));
    if (advanced) {
      listitem = gtk_object_get_data(GTK_OBJECT(page), "listitem");
      if (show_advanced) {
	gtk_widget_show(page);
	gtk_widget_show(listitem);
      } else {
	gtk_widget_hide(page);
	gtk_widget_hide(listitem);
      }
    }
  };
}

static void
gnc_options_dialog_apply_cb(GNCOptionWin *propertybox,
                            gpointer user_data)
{
  GNCOptionDB *global_options = user_data;

  gnc_option_db_commit(global_options);
  gnc_option_show_advanced_pages(propertybox);
}

static void
gnc_options_dialog_help_cb(GNCOptionWin *propertybox,
			   gpointer user_data)
{
  if (global_help_cb)
    global_help_cb (propertybox, global_help_cb_data);
}

static void
gnc_options_dialog_close_cb(GNCOptionWin *propertybox,
                            gpointer user_data)
{
  GNCOptionWin **options_dialog = user_data;

  gnc_options_dialog_destroy(propertybox);

  *options_dialog = NULL;
}


void
gnc_show_options_dialog(void)
{
  static GNCOptionWin *options_dialog = NULL;
  GNCOptionDB *global_options;

  global_options = gnc_get_global_options();

  if (gnc_option_db_num_sections(global_options) == 0)
  {
    gnc_warning_dialog(NULL, _("No options!"));
    return;
  }

  if (gnc_option_db_dirty(global_options))
  {
    if (options_dialog != NULL)
      gnc_options_dialog_destroy(options_dialog);

    options_dialog = NULL;
  }

  if (options_dialog == NULL)
  {
    options_dialog = gnc_options_dialog_new(_("GnuCash Preferences"));

    gnc_build_options_dialog_contents(options_dialog, global_options);
    gnc_option_db_clean(global_options);

    gnc_options_dialog_set_apply_cb(options_dialog, 
                                    gnc_options_dialog_apply_cb,
                                    global_options);

    gnc_options_dialog_set_help_cb(options_dialog, 
                                   gnc_options_dialog_help_cb,
                                   global_options);

    gnc_options_dialog_set_close_cb (options_dialog,
                                     gnc_options_dialog_close_cb,
                                     &options_dialog);
  }

  gtk_window_present(GTK_WINDOW(options_dialog->dialog));
}

/*****************************************************************/
/* Option Registration                                           */

/* SET WIDGET */

static GtkWidget *
gnc_option_set_ui_widget_boolean (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;

  *enclosing = gtk_hbox_new(FALSE, 5);
  value = gtk_check_button_new_with_label(name);

  gnc_option_set_widget (option, value);
  gnc_option_set_ui_value(option, FALSE);

  g_signal_connect(G_OBJECT(value), "toggled",
		   G_CALLBACK(gnc_option_changed_widget_cb), option);

  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);

  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_string (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  gchar *colon_name;

  colon_name = g_strconcat(name, ":", NULL);
  label = gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  *enclosing = gtk_hbox_new(FALSE, 5);
  value = gtk_entry_new();

  gnc_option_set_widget (option, value);
  gnc_option_set_ui_value(option, FALSE);

  g_signal_connect(G_OBJECT(value), "changed",
		   G_CALLBACK(gnc_option_changed_widget_cb), option);

  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_text (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *frame;
  GtkWidget *scroll;
  GtkTextBuffer* text_buffer;

  frame = gtk_frame_new(name);

  scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				 GTK_POLICY_NEVER, 
				 GTK_POLICY_AUTOMATIC);
  gtk_container_border_width(GTK_CONTAINER(scroll), 2);

  gtk_container_add(GTK_CONTAINER(frame), scroll);

  *enclosing = gtk_hbox_new(FALSE, 10);
  value = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(value), GTK_WRAP_WORD);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(value), TRUE);
  gtk_container_add (GTK_CONTAINER (scroll), value);

  gnc_option_set_widget (option, value);
  gnc_option_set_ui_value(option, FALSE);

  text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(value));
  g_signal_connect(G_OBJECT(text_buffer), "changed",
		   G_CALLBACK(gnc_option_changed_option_cb), option);

  gtk_box_pack_start(GTK_BOX(*enclosing), frame, TRUE, TRUE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_currency (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  gchar *colon_name;

  colon_name = g_strconcat(name, ":", NULL);
  label = gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  *enclosing = gtk_hbox_new(FALSE, 5);
  value = gnc_currency_edit_new();

  gnc_option_set_widget (option, value);
  gnc_option_set_ui_value(option, FALSE);

  if (documentation != NULL)
    gtk_tooltips_set_tip(tooltips, GTK_COMBO(value)->entry,
			 documentation, NULL);

  g_signal_connect(G_OBJECT(GTK_COMBO(value)->entry), "changed",
		   G_CALLBACK(gnc_option_changed_widget_cb), option);

  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_commodity (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  gchar *colon_name;

  colon_name = g_strconcat(name, ":", NULL);
  label = gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  *enclosing = gtk_hbox_new(FALSE, 5);
  value = gnc_general_select_new(GNC_GENERAL_SELECT_TYPE_SELECT,
				 gnc_commodity_edit_get_string,
				 gnc_commodity_edit_new_select,
				 NULL);

  gnc_option_set_widget (option, value);
  gnc_option_set_ui_value(option, FALSE);

  if (documentation != NULL)
    gtk_tooltips_set_tip(tooltips, GNC_GENERAL_SELECT(value)->entry,
			 documentation, NULL);

  g_signal_connect(G_OBJECT(GNC_GENERAL_SELECT(value)->entry), "changed",
		   G_CALLBACK(gnc_option_changed_widget_cb), option);

  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_multichoice (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  gchar *colon_name;

  colon_name = g_strconcat(name, ":", NULL);
  label= gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  *enclosing = gtk_hbox_new(FALSE, 5);

  value = gnc_option_create_multichoice_widget(option);
  gnc_option_set_widget (option, value);

  gnc_option_set_ui_value(option, FALSE);
  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_date (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  gchar *colon_name;

  colon_name = g_strconcat(name, ":", NULL);
  label= gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  *enclosing = gtk_hbox_new(FALSE, 5);

  value = gnc_option_create_date_widget(option);

  gnc_option_set_widget (option, value);

  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);

  gtk_box_pack_start(page_box, *enclosing, FALSE, FALSE, 5);
  *packed = TRUE;  
  gnc_option_set_ui_value(option, FALSE);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_account_list (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkTreeSelection *selection;

  *enclosing = gnc_option_create_account_widget(option, name);
  value = gnc_option_get_widget (option);

  gtk_tooltips_set_tip(tooltips, *enclosing, documentation, NULL);

  gtk_box_pack_start(page_box, *enclosing, FALSE, FALSE, 5);
  *packed = TRUE;

  //gtk_widget_realize(value);

  gnc_option_set_ui_value(option, FALSE);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(value));
  g_signal_connect(G_OBJECT(value), "changed",
		   G_CALLBACK(gnc_option_account_cb), option);

  //  gtk_clist_set_row_height(GTK_CLIST(value), 0);
  //  gtk_widget_set_usize(value, 0, GTK_CLIST(value)->row_height * 10);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_account_sel (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  GList *acct_type_list;
  char * colon_name;

  colon_name = g_strconcat(name, ":", NULL);
  label = gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  acct_type_list = gnc_option_get_account_type_list(option);
  value = gnc_account_sel_new();
  gnc_account_sel_set_acct_filters(GNC_ACCOUNT_SEL(value), acct_type_list);

  g_signal_connect(G_OBJECT(gnc_account_sel_gtk_entry(GNC_ACCOUNT_SEL(value))),
		   "changed",
		   G_CALLBACK(gnc_option_changed_widget_cb), option);

  gnc_option_set_widget (option, value);
  gnc_option_set_ui_value(option, TRUE);

  *enclosing = gtk_hbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_list (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  gint num_lines;

  *enclosing = gnc_option_create_list_widget(option, name);
  value = gnc_option_get_widget (option);

  gtk_tooltips_set_tip(tooltips, *enclosing, documentation, NULL);

  gtk_box_pack_start(page_box, *enclosing, FALSE, FALSE, 5);
  *packed = TRUE;

  //gtk_widget_realize(value);

  gnc_option_set_ui_value(option, FALSE);

  g_signal_connect(G_OBJECT(value), "select_row",
		   G_CALLBACK(gnc_option_list_select_cb), option);
  g_signal_connect(G_OBJECT(value), "unselect_row",
		   G_CALLBACK(gnc_option_list_unselect_cb), option);

  num_lines = gnc_option_num_permissible_values(option);
  num_lines = MIN(num_lines, 9) + 1;

  gtk_clist_set_row_height(GTK_CLIST(value), 0);
  gtk_widget_set_usize(value, 0, GTK_CLIST(value)->row_height * num_lines);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_number_range (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  gchar *colon_name;
  GtkAdjustment *adj;
  gdouble lower_bound = G_MINDOUBLE;
  gdouble upper_bound = G_MAXDOUBLE;
  gdouble step_size = 1.0;
  int num_decimals = 0;

  colon_name = g_strconcat(name, ":", NULL);
  label = gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  *enclosing = gtk_hbox_new(FALSE, 5);

  gnc_option_get_range_info(option, &lower_bound, &upper_bound,
			    &num_decimals, &step_size);
  adj = GTK_ADJUSTMENT(gtk_adjustment_new(lower_bound, lower_bound,
					  upper_bound, step_size,
					  step_size * 5.0,
					  step_size * 5.0));
  value = gtk_spin_button_new(adj, step_size, num_decimals);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(value), TRUE);

  {
    GtkStyle *style;
    gdouble biggest;
    gint num_digits;

    biggest = ABS(lower_bound);
    biggest = MAX(biggest, ABS(upper_bound));

    num_digits = 0;
    while (biggest >= 1)
    {
      num_digits++;
      biggest = biggest / 10;
    }

    if (num_digits == 0)
      num_digits = 1;

    num_digits += num_decimals + 1;

    style = gtk_widget_get_style(value);
    if (style != NULL)
    {
      gchar *string;
      gint width;

      string = g_strnfill(num_digits, '8');
      
      width = gdk_text_measure(gdk_font_from_description (style->font_desc), 
                 string, num_digits);

      /* sync with gtkspinbutton.c. why doesn't it do this itself? */
      width += 11 + (2 * style->xthickness);

      g_free(string);

      gtk_widget_set_usize(value, width, 0);
    }
  }

  gnc_option_set_widget (option, value);
  gnc_option_set_ui_value(option, FALSE);

  g_signal_connect(G_OBJECT(value), "changed",
		   G_CALLBACK(gnc_option_changed_widget_cb), option);
  
  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_color (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  gchar *colon_name;
  gboolean use_alpha;

  colon_name = g_strconcat(name, ":", NULL);
  label = gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  *enclosing = gtk_hbox_new(FALSE, 5);

  use_alpha = gnc_option_use_alpha(option);

  value = gnome_color_picker_new();
  gnome_color_picker_set_title(GNOME_COLOR_PICKER(value), name);
  gnome_color_picker_set_use_alpha(GNOME_COLOR_PICKER(value), use_alpha);

  gnc_option_set_widget (option, value);
  gnc_option_set_ui_value(option, FALSE);

  g_signal_connect(G_OBJECT(value), "color-set",
		   G_CALLBACK(gnc_option_color_changed_cb), option);

  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_font (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  gchar *colon_name;

  colon_name = g_strconcat(name, ":", NULL);
  label = gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  *enclosing = gtk_hbox_new(FALSE, 5);
  value = gnome_font_picker_new();
  gnome_font_picker_set_mode(GNOME_FONT_PICKER(value),
			     GNOME_FONT_PICKER_MODE_FONT_INFO);

  gnc_option_set_widget (option, value);

  gnc_option_set_ui_value(option, FALSE);

  g_signal_connect(G_OBJECT(value), "font-set",
		   G_CALLBACK(gnc_option_font_changed_cb), option);

  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_pixmap (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;
  GtkWidget *label;
  GtkWidget *entry;
  gchar *colon_name;

  ENTER("option %p(%s), name %s", option, gnc_option_name(option), name);
  colon_name = g_strconcat(name, ":", NULL);
  label = gtk_label_new(colon_name);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  g_free(colon_name);

  *enclosing = gtk_hbox_new(FALSE, 5);
  value = gnome_pixmap_entry_new(NULL, _("Select pixmap"),
				 FALSE);
  gnome_pixmap_entry_set_preview(GNOME_PIXMAP_ENTRY(value), FALSE);

  entry = gnome_pixmap_entry_gtk_entry (GNOME_PIXMAP_ENTRY(value));
  g_signal_connect(G_OBJECT (entry), "changed",
		   G_CALLBACK(gnc_option_changed_widget_cb), option);
    
  gnc_option_set_widget (option, value);
  gnc_option_set_ui_value(option, FALSE);

  gtk_box_pack_start(GTK_BOX(*enclosing), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);

  gtk_widget_show(value);
  gtk_widget_show(label);
  gtk_widget_show(*enclosing);
  LEAVE("new widget = %p", value);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_radiobutton (GNCOption *option, GtkBox *page_box,
				  GtkTooltips *tooltips,
				  char *name, char *documentation,
				  /* Return values */
				  GtkWidget **enclosing, gboolean *packed)
{
  GtkWidget *value;

  *enclosing = gtk_hbox_new(FALSE, 5);

  value = gnc_option_create_radiobutton_widget(name, option);
  gnc_option_set_widget (option, value);

  gnc_option_set_ui_value(option, FALSE);
  gtk_box_pack_start(GTK_BOX(*enclosing), value, FALSE, FALSE, 0);
  gtk_widget_show_all(*enclosing);
  return value;
}

static GtkWidget *
gnc_option_set_ui_widget_dateformat (GNCOption *option, GtkBox *page_box,
				     GtkTooltips *tooltips,
				     char *name, char *documentation,
				     /* Return values */
				     GtkWidget **enclosing, gboolean *packed)
{
  *enclosing = gnc_date_format_new_with_label(name);
  gnc_option_set_widget (option, *enclosing);

  gnc_option_set_ui_value(option, FALSE);
  g_signal_connect(G_OBJECT(*enclosing), "format_changed",
		   G_CALLBACK(gnc_option_changed_option_cb), option);
  gtk_widget_show_all(*enclosing);
  return *enclosing;
}

/* SET VALUE */

static gboolean
gnc_option_set_ui_value_boolean (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  if (SCM_BOOLP(value))
  {
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(widget),
				SCM_NFALSEP(value));
    return FALSE;
  }
  else
    return TRUE;
}

static gboolean
gnc_option_set_ui_value_string (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  if (SCM_STRINGP(value))
  {
    const gchar *string = SCM_STRING_CHARS(value);
    gtk_entry_set_text(GTK_ENTRY(widget), string);
    return FALSE;
  }
  else
    return TRUE;
}

static gboolean
gnc_option_set_ui_value_text (GNCOption *option, gboolean use_default,
			      GObject *object, SCM value)
{
  GtkTextBuffer *buffer;

  if (GTK_IS_TEXT_BUFFER(object))
    buffer = GTK_TEXT_BUFFER(object);
  else
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(object));

  if (SCM_STRINGP(value))
  {
    const gchar *string = SCM_STRING_CHARS(value);
    gtk_text_buffer_set_text (buffer, string, strlen (string));
    return FALSE;
  }
  else
    return TRUE;
}

static gboolean
gnc_option_set_ui_value_currency (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  gnc_commodity *commodity;

  commodity = gnc_scm_to_commodity (value);
  if (commodity)
  {
    gnc_currency_edit_set_currency(GNC_CURRENCY_EDIT(widget), commodity);
    return FALSE;
  }
  else
    return TRUE;
}

static gboolean
gnc_option_set_ui_value_commodity (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  gnc_commodity *commodity;

  commodity = gnc_scm_to_commodity (value);
  if (commodity)
  {
    gnc_general_select_set_selected(GNC_GENERAL_SELECT (widget), commodity);
    return FALSE;
  }
  else
    return TRUE;
}

static gboolean
gnc_option_set_ui_value_multichoice (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  int index;

  index = gnc_option_permissible_value_index(option, value);
  if (index < 0)
    return TRUE;
  else
  {
    gtk_option_menu_set_history(GTK_OPTION_MENU(widget), index);
    gtk_object_set_data(GTK_OBJECT(widget), "gnc_multichoice_index",
			GINT_TO_POINTER(index));
    return FALSE;
  }
}

static gboolean
gnc_option_set_ui_value_date (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  int index;
  char *date_option_type;
  char *symbol_str;
  gboolean bad_value = FALSE;

  date_option_type = gnc_option_date_option_get_subtype(option);

  if (SCM_CONSP(value))
  {
    symbol_str = gnc_date_option_value_get_type (value);
    if (symbol_str)
    {
      if (safe_strcmp(symbol_str, "relative") == 0)
      {
	SCM relative = gnc_date_option_value_get_relative (value);

	index = gnc_option_permissible_value_index(option, relative);
	if (safe_strcmp(date_option_type, "relative") == 0)
	{
	  gtk_object_set_data(GTK_OBJECT(widget),
			      "gnc_multichoice_index",
			      GINT_TO_POINTER(index));
	  gtk_option_menu_set_history(GTK_OPTION_MENU(widget), index);
	}
	else if (safe_strcmp(date_option_type, "both") == 0)
	{
	  GList *widget_list;
	  GtkWidget *rel_date_widget;

	  widget_list = gtk_container_children(GTK_CONTAINER(widget));
	  rel_date_widget = g_list_nth_data(widget_list,
					    GNC_RD_WID_REL_WIDGET_POS);
	  gnc_date_option_set_select_method(option, FALSE, TRUE);
	  gtk_object_set_data(GTK_OBJECT(rel_date_widget),
			      "gnc_multichoice_index",
			      GINT_TO_POINTER(index));
	  gtk_option_menu_set_history(GTK_OPTION_MENU(rel_date_widget),
				      index);
	}
	else
	{
	  bad_value = TRUE;
	}
      }
      else if (safe_strcmp(symbol_str, "absolute") == 0)
      { 
	Timespec ts;

	ts = gnc_date_option_value_get_absolute (value);

	if (safe_strcmp(date_option_type, "absolute") == 0)
        {
	  gnc_date_edit_set_time(GNC_DATE_EDIT(widget), ts.tv_sec);
	}
	else if (safe_strcmp(date_option_type, "both") == 0)
        {
	  GList *widget_list;
	  GtkWidget *ab_widget;

	  widget_list = gtk_container_children(GTK_CONTAINER(widget));
	  ab_widget = g_list_nth_data(widget_list,
				      GNC_RD_WID_AB_WIDGET_POS);
	  gnc_date_option_set_select_method(option, TRUE, TRUE);
	  gnc_date_edit_set_time(GNC_DATE_EDIT(ab_widget), ts.tv_sec);
	}
	else
        {
	  bad_value = TRUE;
	}
      }
      else
      {
	bad_value = TRUE;
      }

      if (symbol_str)
	free(symbol_str);
    }
  }
  else
  {
    bad_value = TRUE;
  }

  if (date_option_type)
    free(date_option_type);

  return bad_value;
}

static gboolean
gnc_option_set_ui_value_account_list (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  GList *list;

  list = gnc_scm_list_to_glist(value);

  gnc_tree_view_account_set_selected_accounts (GNC_TREE_VIEW_ACCOUNT(widget),
					       list, TRUE);

  g_list_free(list);
  return FALSE;
}

static gboolean
gnc_option_set_ui_value_account_sel (GNCOption *option, gboolean use_default,
				     GtkWidget *widget, SCM value)
{
  Account *acc = NULL;

  if (value != SCM_BOOL_F) {
    if (!gw_wcp_p(value))
      scm_misc_error("gnc_optoin_set_ui_value_account_sel",
		     "Option Value not a gw:wcp.", value);
      
    acc = gw_wcp_get_ptr(value);
  }
	
  gnc_account_sel_set_account (GNC_ACCOUNT_SEL(widget), acc);

  return FALSE;
}

static gboolean
gnc_option_set_ui_value_list (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  gint num_rows, row;

  gtk_clist_unselect_all(GTK_CLIST(widget));

  num_rows = gnc_option_num_permissible_values(option);
  for (row = 0; row < num_rows; row++)
    gtk_clist_set_row_data(GTK_CLIST(widget), row, GINT_TO_POINTER(FALSE));

  while (SCM_LISTP(value) && !SCM_NULLP(value))
  {
    SCM item;

    item = SCM_CAR(value);
    value = SCM_CDR(value);

    row = gnc_option_permissible_value_index(option, item);
    if (row < 0)
    {
      return TRUE;
    }

    gtk_clist_select_row(GTK_CLIST(widget), row, 0);
    gtk_clist_set_row_data(GTK_CLIST(widget), row, GINT_TO_POINTER(TRUE));
  }

  if (!SCM_LISTP(value) || !SCM_NULLP(value))
    return TRUE;

  return FALSE;
}

static gboolean
gnc_option_set_ui_value_number_range (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  GtkSpinButton *spinner;
  gdouble d_value;;

  spinner = GTK_SPIN_BUTTON(widget);

  if (SCM_NUMBERP(value))
  {
    d_value = scm_num2dbl(value, __FUNCTION__);
    gtk_spin_button_set_value(spinner, d_value);
    return FALSE;
  }
  else
    return TRUE;
}

static gboolean
gnc_option_set_ui_value_color (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  gdouble red, green, blue, alpha;

  if (gnc_option_get_color_info(option, use_default,
				&red, &green, &blue, &alpha))
  {
    GnomeColorPicker *picker;

    picker = GNOME_COLOR_PICKER(widget);

    gnome_color_picker_set_d(picker, red, green, blue, alpha);
    return FALSE;
  }
  else
    return TRUE;
}

static gboolean
gnc_option_set_ui_value_font (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  if (SCM_STRINGP(value))
  {
    const gchar *string = SCM_STRING_CHARS(value);
    if ((string != NULL) && (*string != '\0'))
    {
      GnomeFontPicker *picker = GNOME_FONT_PICKER(widget);
      gnome_font_picker_set_font_name(picker, string);
    }
    return FALSE;
  }
  else
    return TRUE;
}

static gboolean
gnc_option_set_ui_value_pixmap (GNCOption *option, gboolean use_default,
				 GtkWidget *widget, SCM value)
{
  ENTER("option %p(%s)", option, gnc_option_name(option));
  if (SCM_STRINGP(value))
  {
    const gchar *string = SCM_STRING_CHARS(value);

    if (string && *string)
    {
      GtkEntry *entry;
      DEBUG("string = %s", string);
      entry = GTK_ENTRY(gnome_pixmap_entry_gtk_entry(GNOME_PIXMAP_ENTRY(widget)));
      gtk_entry_set_text(entry, string);
    }
    LEAVE("FALSE");
    return FALSE;
  }

  LEAVE("TRUE");
  return TRUE;
}

static gboolean
gnc_option_set_ui_value_radiobutton (GNCOption *option, gboolean use_default,
				     GtkWidget *widget, SCM value)
{
  int index;

  index = gnc_option_permissible_value_index(option, value);
  if (index < 0)
    return TRUE;
  else
  {
    GtkWidget *box, *button;
    GList *list;
    int i;
    gpointer val;

    list = gtk_container_children (GTK_CONTAINER (widget));
    box = list->data;

    list = gtk_container_children (GTK_CONTAINER (box));
    for (i = 0; i < index && list; i++)
      list = list->next;
    g_return_val_if_fail (list, TRUE);

    button = list->data;
    val = gtk_object_get_data (GTK_OBJECT (button), "gnc_radiobutton_index");
    g_return_val_if_fail (GPOINTER_TO_INT (val) == index, TRUE);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
    //    gtk_object_set_data(GTK_OBJECT(widget), "gnc_radiobutton_index",
    //			GINT_TO_POINTER(index));
    return FALSE;
  }
}

static gboolean
gnc_option_set_ui_value_dateformat (GNCOption *option, gboolean use_default,
				    GtkWidget *widget, SCM value)
{
  GNCDateFormat * gdf = GNC_DATE_FORMAT(widget);
  QofDateFormat format;
  GNCDateMonthFormat months;
  gboolean years;
  char *custom;

  if (gnc_dateformat_option_value_parse(value, &format, &months, &years, &custom))
    return TRUE;

  gnc_date_format_set_format(gdf, format);
  gnc_date_format_set_months(gdf, months);
  gnc_date_format_set_years(gdf, years);
  gnc_date_format_set_custom(gdf, custom);
  gnc_date_format_refresh(gdf);

  if (custom)
    free(custom);

  return FALSE;
}

/* GET VALUE */

static SCM
gnc_option_get_ui_value_boolean (GNCOption *option, GtkWidget *widget)
{
  gboolean active;

  active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  return SCM_BOOL(active);
}

static SCM
gnc_option_get_ui_value_string (GNCOption *option, GtkWidget *widget)
{
  char * string;
  SCM result;

  string = gtk_editable_get_chars(GTK_EDITABLE(widget), 0, -1);
  result = scm_makfrom0str(string);
  g_free(string);
  return result;
}

static SCM
gnc_option_get_ui_value_text (GNCOption *option, GtkWidget *widget)
{
  char * string;
  SCM result;

  string = xxxgtk_textview_get_text (GTK_TEXT_VIEW(widget));
  result = scm_makfrom0str(string);
  g_free(string);
  return result;
}

static SCM
gnc_option_get_ui_value_currency (GNCOption *option, GtkWidget *widget)
{
  gnc_commodity *commodity;

  commodity =
    gnc_currency_edit_get_currency(GNC_CURRENCY_EDIT(widget));

  return (gnc_commodity_to_scm (commodity));
}

static SCM
gnc_option_get_ui_value_commodity (GNCOption *option, GtkWidget *widget)
{
  gnc_commodity *commodity;

  commodity =
    gnc_general_select_get_selected(GNC_GENERAL_SELECT(widget));

  return (gnc_commodity_to_scm(commodity));
}

static SCM
gnc_option_get_ui_value_multichoice (GNCOption *option, GtkWidget *widget)
{
  gpointer _index;
  int index;

  _index = gtk_object_get_data(GTK_OBJECT(widget), "gnc_multichoice_index");
  index = GPOINTER_TO_INT(_index);

  return (gnc_option_permissible_value(option, index));
}

static SCM
gnc_option_get_ui_value_date (GNCOption *option, GtkWidget *widget)
{
  int index;
  SCM type, val,result = SCM_UNDEFINED;
  char *subtype = gnc_option_date_option_get_subtype(option);

  if(safe_strcmp(subtype, "relative") == 0)
  {
    index = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(widget),
						"gnc_multichoice_index"));
    type = scm_str2symbol("relative");
    val = gnc_option_permissible_value(option, index);
    result = scm_cons(type, val);
  }
  else if (safe_strcmp(subtype, "absolute") == 0)
  { 		      
    Timespec ts;

    ts.tv_sec  = gnc_date_edit_get_date(GNC_DATE_EDIT(widget));
    ts.tv_nsec = 0;

    result = scm_cons(scm_str2symbol("absolute"), gnc_timespec2timepair(ts));
  }
  else if (safe_strcmp(subtype, "both") == 0)
  {
    Timespec ts;
    int index;
    SCM val;
    GList *widget_list;
    GtkWidget *ab_button, *rel_widget, *ab_widget;

    widget_list = gtk_container_children(GTK_CONTAINER(widget));
    ab_button = g_list_nth_data(widget_list,  GNC_RD_WID_AB_BUTTON_POS);
    ab_widget = g_list_nth_data(widget_list,  GNC_RD_WID_AB_WIDGET_POS);
    rel_widget = g_list_nth_data(widget_list, GNC_RD_WID_REL_WIDGET_POS);

    /* if it's an absolute date */
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ab_button)))
    {
      ts.tv_sec = gnc_date_edit_get_date(GNC_DATE_EDIT(ab_widget));
      ts.tv_nsec = 0;
      result = scm_cons(scm_str2symbol("absolute"), gnc_timespec2timepair(ts));
    }
    else 
    {
      index = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(rel_widget),
						  "gnc_multichoice_index"));
      val = gnc_option_permissible_value(option, index);
      result = scm_cons(scm_str2symbol("relative"), val);
    }
  }
  g_free(subtype);
  return result;
}

static SCM
gnc_option_get_ui_value_account_list (GNCOption *option, GtkWidget *widget)
{
  GncTreeViewAccount *tree;
  GList *list;
  SCM result;

  tree = GNC_TREE_VIEW_ACCOUNT(widget);
  list = gnc_tree_view_account_get_selected_accounts (tree);

  /* handover list */
  result = gnc_glist_to_scm_list(list, scm_c_eval_string("<gnc:Account*>"));
  g_list_free(list);
  return result;
}

static SCM
gnc_option_get_ui_value_account_sel (GNCOption *option, GtkWidget *widget)
{
  GNCAccountSel *gas;
  Account* acc;

  gas = GNC_ACCOUNT_SEL(widget);
  acc = gnc_account_sel_get_account (gas);

  if (!acc)
    return SCM_BOOL_F;

  return gw_wcp_assimilate_ptr(acc, scm_c_eval_string("<gnc:Account*>"));
}

static SCM
gnc_option_get_ui_value_list (GNCOption *option, GtkWidget *widget)
{
  SCM result;
  gboolean selected;
  GtkCList *clist;
  gint num_rows;
  gint row;

  clist = GTK_CLIST(widget);
  num_rows = gnc_option_num_permissible_values(option);
  result = scm_c_eval_string("'()");

  for (row = 0; row < num_rows; row++)
  {
    selected = GPOINTER_TO_INT(gtk_clist_get_row_data(clist, row));
    if (selected)
      result = scm_cons(gnc_option_permissible_value(option, row), result);
  }

  return (scm_reverse(result));
}

static SCM
gnc_option_get_ui_value_number_range (GNCOption *option, GtkWidget *widget)
{
  GtkSpinButton *spinner;
  gdouble value;

  spinner = GTK_SPIN_BUTTON(widget);

  value = gtk_spin_button_get_value_as_float(spinner);

  return (scm_make_real(value));
}

static SCM
gnc_option_get_ui_value_color (GNCOption *option, GtkWidget *widget)
{
  SCM result;
  GnomeColorPicker *picker;
  gdouble red, green, blue, alpha;
  gdouble scale;

  picker = GNOME_COLOR_PICKER(widget);

  gnome_color_picker_get_d(picker, &red, &green, &blue, &alpha);

  scale = gnc_option_color_range(option);

  result = SCM_EOL;
  result = scm_cons(scm_make_real(alpha * scale), result);
  result = scm_cons(scm_make_real(blue * scale), result);
  result = scm_cons(scm_make_real(green * scale), result);
  result = scm_cons(scm_make_real(red * scale), result);
  return result;
}

static SCM
gnc_option_get_ui_value_font (GNCOption *option, GtkWidget *widget)
{
  GnomeFontPicker *picker = GNOME_FONT_PICKER(widget);
  const gchar * string;

  string = gnome_font_picker_get_font_name(picker);
  return (scm_makfrom0str(string));
}

static SCM
gnc_option_get_ui_value_pixmap (GNCOption *option, GtkWidget *widget)
{
  GnomePixmapEntry * p = GNOME_PIXMAP_ENTRY(widget);
  char             * string = gnome_pixmap_entry_get_filename(p);

  return (scm_makfrom0str(string ? string : ""));
}

static SCM
gnc_option_get_ui_value_radiobutton (GNCOption *option, GtkWidget *widget)
{
  gpointer _index;
  int index;

  _index = gtk_object_get_data(GTK_OBJECT(widget), "gnc_radiobutton_index");
  index = GPOINTER_TO_INT(_index);

  return (gnc_option_permissible_value(option, index));
}

static SCM
gnc_option_get_ui_value_dateformat (GNCOption *option, GtkWidget *widget)
{
  GNCDateFormat *gdf = GNC_DATE_FORMAT(widget);
  QofDateFormat format;
  GNCDateMonthFormat months;
  gboolean years;
  const char* custom;

  format = gnc_date_format_get_format(gdf);
  months = gnc_date_format_get_months(gdf);
  years = gnc_date_format_get_years(gdf);
  custom = gnc_date_format_get_custom(gdf);

  return (gnc_dateformat_option_set_value(format, months, years, custom));
}

/* INITIALIZATION */

static void gnc_options_initialize_options (void)
{
  static GNCOptionDef_t options[] = {
    { "boolean", gnc_option_set_ui_widget_boolean,
      gnc_option_set_ui_value_boolean, gnc_option_get_ui_value_boolean },
    { "string", gnc_option_set_ui_widget_string,
      gnc_option_set_ui_value_string, gnc_option_get_ui_value_string },
    { "text", gnc_option_set_ui_widget_text,
      (GNCOptionUISetValue)gnc_option_set_ui_value_text,
      gnc_option_get_ui_value_text },
    { "currency", gnc_option_set_ui_widget_currency,
      gnc_option_set_ui_value_currency, gnc_option_get_ui_value_currency },
    { "commodity", gnc_option_set_ui_widget_commodity,
      gnc_option_set_ui_value_commodity, gnc_option_get_ui_value_commodity },
    { "multichoice", gnc_option_set_ui_widget_multichoice,
      gnc_option_set_ui_value_multichoice, gnc_option_get_ui_value_multichoice },
    { "date", gnc_option_set_ui_widget_date,
      gnc_option_set_ui_value_date, gnc_option_get_ui_value_date },
    { "account-list", gnc_option_set_ui_widget_account_list,
      gnc_option_set_ui_value_account_list, gnc_option_get_ui_value_account_list },
    { "account-sel", gnc_option_set_ui_widget_account_sel,
      gnc_option_set_ui_value_account_sel, gnc_option_get_ui_value_account_sel },
    { "list", gnc_option_set_ui_widget_list,
      gnc_option_set_ui_value_list, gnc_option_get_ui_value_list },
    { "number-range", gnc_option_set_ui_widget_number_range,
      gnc_option_set_ui_value_number_range, gnc_option_get_ui_value_number_range },
    { "color", gnc_option_set_ui_widget_color,
      gnc_option_set_ui_value_color, gnc_option_get_ui_value_color },
    { "font", gnc_option_set_ui_widget_font,
      gnc_option_set_ui_value_font, gnc_option_get_ui_value_font },
    { "pixmap", gnc_option_set_ui_widget_pixmap,
      gnc_option_set_ui_value_pixmap, gnc_option_get_ui_value_pixmap },
    { "radiobutton", gnc_option_set_ui_widget_radiobutton,
      gnc_option_set_ui_value_radiobutton, gnc_option_get_ui_value_radiobutton },
    { "dateformat", gnc_option_set_ui_widget_dateformat,
      gnc_option_set_ui_value_dateformat, gnc_option_get_ui_value_dateformat },
    { NULL, NULL, NULL, NULL }
  };
  int i;

  for (i = 0; options[i].option_name; i++)
    gnc_options_ui_register_option (&(options[i]));
}

/* Register a new option type in the UI */
void gnc_options_ui_register_option (GNCOptionDef_t *option)
{
  g_return_if_fail (optionTable);
  g_return_if_fail (option);

  g_hash_table_insert (optionTable, (gpointer)(option->option_name), option);
}

GNCOptionDef_t * gnc_options_ui_get_option (const char *option_name)
{
  g_return_val_if_fail (optionTable, NULL);
  g_return_val_if_fail (option_name, NULL);

  return g_hash_table_lookup (optionTable, option_name);
}

void gnc_options_ui_initialize (void)
{

  //  gnc_options_register_stocks ();
  g_return_if_fail (optionTable == NULL);
  optionTable = g_hash_table_new (g_str_hash, g_str_equal);

  /* add known types */
  gnc_options_initialize_options ();
}

struct scm_cb
{
  SCM	apply_cb;
  SCM	close_cb;
};

static void
scm_apply_cb (GNCOptionWin *win, gpointer data)
{
  struct scm_cb *cbdata = data;

  if (gnc_option_db_get_changed (win->option_db)) {
    gnc_option_db_commit (win->option_db);
    if (cbdata->apply_cb != SCM_BOOL_F) {
      scm_call_0 (cbdata->apply_cb);
    }
  }
}

static void
scm_close_cb (GNCOptionWin *win, gpointer data)
{
  struct scm_cb *cbdata = data;

  if (cbdata->close_cb != SCM_BOOL_F) {
    scm_call_0 (cbdata->close_cb);
    scm_gc_unprotect_object (cbdata->close_cb);
  }

  if (cbdata->apply_cb != SCM_BOOL_F)
    scm_gc_unprotect_object (cbdata->apply_cb);

  g_free (cbdata);
}

/* Both apply_cb and close_cb should be scheme functions with 0 arguments.
 * References to these functions will be held until the close_cb is called
 */
void
gnc_options_dialog_set_scm_callbacks (GNCOptionWin *win, SCM apply_cb,
				      SCM close_cb)
{
  struct scm_cb *cbdata;

  cbdata = g_new0 (struct scm_cb, 1);
  cbdata->apply_cb = apply_cb;
  cbdata->close_cb = close_cb;

  if (apply_cb != SCM_BOOL_F)
    scm_gc_protect_object (cbdata->apply_cb);

  if (close_cb != SCM_BOOL_F)
    scm_gc_protect_object (cbdata->close_cb);

  gnc_options_dialog_set_apply_cb (win, scm_apply_cb, cbdata);
  gnc_options_dialog_set_close_cb (win, scm_close_cb, cbdata);
}
