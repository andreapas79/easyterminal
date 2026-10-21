/*
 * Copyright © 2026 EasyTerminal contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "terminal-name-dialog.hh"
#include <glib/gi18n.h>

#include "terminal-intl.hh"

typedef struct {
  GtkEntry *entry;
  TerminalNameDialogCallback callback;
  gpointer user_data;
  GDestroyNotify destroy_notify;
} NameDialogData;

static void
name_dialog_data_free (NameDialogData *data)
{
  if (data->destroy_notify)
    data->destroy_notify (data->user_data);
  g_free (data);
}

static void
name_dialog_data_closure_notify (gpointer data,
                                 GClosure *closure)
{
  name_dialog_data_free ((NameDialogData*)data);
}

static void
name_dialog_response_cb (GtkDialog *dialog,
                         int response,
                         NameDialogData *data)
{
  if (response == GTK_RESPONSE_OK)
    data->callback (gtk_entry_get_text (data->entry), data->user_data);
  else
    data->callback (nullptr, data->user_data);

  gtk_widget_destroy (GTK_WIDGET (dialog));
}

void
terminal_name_dialog_show (GtkWindow                 *parent,
                           const char                *title,
                           const char                *label,
                           const char                *initial_text,
                           TerminalNameDialogCallback callback,
                           gpointer                   user_data,
                           GDestroyNotify             destroy_notify)
{
  GtkWidget *dialog = gtk_dialog_new_with_buttons (title,
                                                   parent,
                                                   GtkDialogFlags(GTK_DIALOG_MODAL |
                                                                  GTK_DIALOG_DESTROY_WITH_PARENT |
                                                                  GTK_DIALOG_USE_HEADER_BAR),
                                                   _("_Cancel"), GTK_RESPONSE_CANCEL,
                                                   _("_OK"), GTK_RESPONSE_OK,
                                                   nullptr);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);

  GtkWidget *label_widget = gtk_label_new_with_mnemonic (label);
  gtk_widget_set_halign (label_widget, GTK_ALIGN_START);
  gtk_box_pack_start (GTK_BOX (box), label_widget, FALSE, FALSE, 0);

  GtkWidget *entry = gtk_entry_new ();
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_entry_set_width_chars (GTK_ENTRY (entry), 32);
  gtk_entry_set_text (GTK_ENTRY (entry), initial_text ? initial_text : "");
  gtk_label_set_mnemonic_widget (GTK_LABEL (label_widget), entry);
  gtk_box_pack_start (GTK_BOX (box), entry, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                      box, TRUE, TRUE, 0);
  gtk_widget_show_all (box);

  NameDialogData *data = g_new0 (NameDialogData, 1);
  data->entry = GTK_ENTRY (entry);
  data->callback = callback;
  data->user_data = user_data;
  data->destroy_notify = destroy_notify;

  g_signal_connect_data (dialog, "response",
                         G_CALLBACK (name_dialog_response_cb),
                         data, name_dialog_data_closure_notify,
                         GConnectFlags(0));

  gtk_window_present (GTK_WINDOW (dialog));
  gtk_widget_grab_focus (entry);
  gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);
}
