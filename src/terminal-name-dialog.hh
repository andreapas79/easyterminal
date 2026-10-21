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

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Called once with the text the user confirmed (possibly empty), or with
 * nullptr if the dialog was cancelled or closed. */
typedef void (* TerminalNameDialogCallback) (const char *text,
                                             gpointer    user_data);

/* Shows a small modal dialog with a single text entry (EasyTerminal:
 * used by "Rename Tab…", "Move to New Group…" and "Rename Group…").
 * @destroy_notify is called on @user_data after @callback. */
void terminal_name_dialog_show (GtkWindow                 *parent,
                                const char                *title,
                                const char                *label,
                                const char                *initial_text,
                                TerminalNameDialogCallback callback,
                                gpointer                   user_data,
                                GDestroyNotify             destroy_notify);

G_END_DECLS
