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

/* EasyTerminal: named, collapsible tab groups (per window).
 *
 * The groups of a window are owned by a TerminalTabGroups manager that lives
 * in the window's TerminalNotebook. Members of a group are always contiguous
 * in the notebook.
 *
 * GTK3's GtkNotebook ties a tab's visibility to its page's visibility
 * (gtknotebook.c, gtk_notebook_size_request), so a tab cannot be hidden on
 * its own. A collapsed group therefore keeps exactly one member page visible,
 * the "representative", whose tab label turns into the group chip; the other
 * member pages are hidden. The representative is the active tab when it
 * belongs to the group, so collapsing never hides the active terminal.
 */

#pragma once

#include <gtk/gtk.h>

#include "terminal-screen.hh"

G_BEGIN_DECLS

typedef struct _TerminalTabGroup  TerminalTabGroup;
typedef struct _TerminalTabGroups TerminalTabGroups;

TerminalTabGroups *terminal_tab_groups_new  (GtkNotebook *notebook);
void               terminal_tab_groups_free (TerminalTabGroups *groups);

/* Queries */
TerminalTabGroup *terminal_tab_groups_get_group_of (TerminalTabGroups *groups,
                                                    TerminalScreen *screen);
TerminalTabGroup *terminal_tab_groups_find         (TerminalTabGroups *groups,
                                                    guint id);
/* Returns: (transfer container): the groups in tab bar order */
GList            *terminal_tab_groups_list         (TerminalTabGroups *groups);
/* The collapsed group whose chip is the tab of page @page_num, or nullptr */
TerminalTabGroup *terminal_tab_groups_get_chip_at  (TerminalTabGroups *groups,
                                                    int page_num);
/* FALSE for every tab of a collapsed group: keyboard tab switching skips them */
gboolean          terminal_tab_groups_is_navigable (TerminalTabGroups *groups,
                                                    TerminalScreen *screen);

guint       terminal_tab_group_get_id        (TerminalTabGroup *group);
const char *terminal_tab_group_get_name      (TerminalTabGroup *group);
gboolean    terminal_tab_group_get_collapsed (TerminalTabGroup *group);

/* Operations (all keep the tab bar consistent) */
TerminalTabGroup *terminal_tab_groups_create (TerminalTabGroups *groups,
                                              TerminalScreen *screen,
                                              const char *name);
void terminal_tab_groups_join     (TerminalTabGroups *groups,
                                   TerminalScreen *screen,
                                   TerminalTabGroup *group);
void terminal_tab_groups_leave    (TerminalTabGroups *groups,
                                   TerminalScreen *screen);
void terminal_tab_groups_collapse (TerminalTabGroups *groups,
                                   TerminalTabGroup *group);
void terminal_tab_groups_expand   (TerminalTabGroups *groups,
                                   TerminalTabGroup *group);
void terminal_tab_groups_rename   (TerminalTabGroups *groups,
                                   TerminalTabGroup *group,
                                   const char *name);
void terminal_tab_groups_ungroup  (TerminalTabGroups *groups,
                                   TerminalTabGroup *group);

/* Makes the page of @screen visible so it can become the current page; for a
 * member of a collapsed group it becomes the group's representative. */
void terminal_tab_groups_prepare_activate (TerminalTabGroups *groups,
                                           TerminalScreen *screen);

/* Move Terminal Left/Right: moves @screen by @direction (±1), treating a
 * collapsed group as a single tab, and updates membership. */
void terminal_tab_groups_reorder (TerminalTabGroups *groups,
                                  TerminalScreen *screen,
                                  int direction);

/* Notebook hooks */
void terminal_tab_groups_page_added     (TerminalTabGroups *groups,
                                         TerminalScreen *screen);
void terminal_tab_groups_page_removed   (TerminalTabGroups *groups,
                                         TerminalScreen *screen);
void terminal_tab_groups_page_reordered (TerminalTabGroups *groups,
                                         TerminalScreen *screen);
void terminal_tab_groups_page_switched  (TerminalTabGroups *groups,
                                         TerminalScreen *screen);

G_END_DECLS
