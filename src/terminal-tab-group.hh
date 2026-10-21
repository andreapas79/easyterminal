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
 * in the window's TerminalNotebook. A group is a "header" page followed by
 * its member pages, always contiguous:
 *
 *   [header: pill "name"] [member] [member] ...
 *
 * The header page holds no terminal and never becomes the current page; its
 * tab is the group's pill (click: collapse/expand, right-click: group menu).
 * GtkNotebook (GTK3) ties a tab's visibility to its page's, so collapsing
 * hides the member pages and only the pill stays. The one exception is the
 * active terminal: collapsing never hides it, it stays next to the pill
 * until another tab is selected.
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
gboolean          terminal_tab_groups_is_header    (GtkWidget *page);
TerminalTabGroup *terminal_tab_groups_get_group_of (TerminalTabGroups *groups,
                                                    TerminalScreen *screen);
TerminalTabGroup *terminal_tab_groups_find         (TerminalTabGroups *groups,
                                                    guint id);
/* Returns: (transfer container): the groups in tab bar order */
GList            *terminal_tab_groups_list         (TerminalTabGroups *groups);
/* The group whose header (pill) is page @page_num, or nullptr */
TerminalTabGroup *terminal_tab_groups_get_header_at (TerminalTabGroups *groups,
                                                     int page_num);
/* FALSE for the tabs of a collapsed group: keyboard tab switching skips them */
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

/* Makes the page of @screen visible so it can become the current page */
void terminal_tab_groups_prepare_activate (TerminalTabGroups *groups,
                                           TerminalScreen *screen);

/* Move Terminal Left/Right: moves @screen by @direction (±1), treating a
 * collapsed group as a single tab, and updates membership. */
void terminal_tab_groups_reorder (TerminalTabGroups *groups,
                                  TerminalScreen *screen,
                                  int direction);

/* Notebook hooks (screen pages only; header pages are filtered out) */
void terminal_tab_groups_page_added     (TerminalTabGroups *groups,
                                         TerminalScreen *screen);
void terminal_tab_groups_page_removed   (TerminalTabGroups *groups,
                                         TerminalScreen *screen);
void terminal_tab_groups_page_reordered (TerminalTabGroups *groups,
                                         GtkWidget *page);
void terminal_tab_groups_page_switched  (TerminalTabGroups *groups,
                                         TerminalScreen *screen);

G_END_DECLS
