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

#include "terminal-tab-group.hh"
#include "terminal-screen-container.hh"
#include "terminal-tab-label.hh"
#include "terminal-libgsystem.hh"

#define N_GROUP_COLORS (6)

struct _TerminalTabGroup {
  guint id;
  char *name;
  gboolean collapsed;
  int color;
  /* Raw pointers to member screens; cleared when they leave the group */
  TerminalScreen *last_active;
  TerminalScreen *representative; /* only meaningful while collapsed */
};

struct _TerminalTabGroups {
  GtkNotebook *notebook; /* not owned: the manager lives inside it */
  GList *groups;         /* owned TerminalTabGroup* */
  guint next_id;
  int fixup_depth;       /* >0 while we reorder pages ourselves */
};

static GQuark
group_quark (void)
{
  static GQuark quark = 0;
  if (G_UNLIKELY (quark == 0))
    quark = g_quark_from_static_string ("easyterminal-tab-group");
  return quark;
}

static void
install_css_once (void)
{
  static gboolean installed = FALSE;
  if (installed)
    return;
  installed = TRUE;

  /* Adwaita tones: green, orange, purple, red, brown, blue. Blue comes last
   * because it is also Adwaita's "current tab" underline. */
  static const char css[] =
    ".et-group-member { border-top: 3px solid transparent; }\n"
    ".et-group-first { border-left: 1px solid alpha(currentColor, 0.25); padding-left: 6px; }\n"
    ".et-group-last { border-right: 1px solid alpha(currentColor, 0.25); padding-right: 6px; }\n"
    ".et-group-c0 { border-top-color: #33d17a; }\n"
    ".et-group-c1 { border-top-color: #ff7800; }\n"
    ".et-group-c2 { border-top-color: #9141ac; }\n"
    ".et-group-c3 { border-top-color: #e01b24; }\n"
    ".et-group-c4 { border-top-color: #986a44; }\n"
    ".et-group-c5 { border-top-color: #3584e4; }\n"
    ".et-group-chip { border-radius: 6px; padding: 0 8px; color: #ffffff; font-weight: bold; }\n"
    ".et-group-chip.et-group-c0 { background-color: #26a269; }\n"
    ".et-group-chip.et-group-c1 { background-color: #e66100; }\n"
    ".et-group-chip.et-group-c2 { background-color: #9141ac; }\n"
    ".et-group-chip.et-group-c3 { background-color: #c01c28; }\n"
    ".et-group-chip.et-group-c4 { background-color: #865e3c; }\n"
    ".et-group-chip.et-group-c5 { background-color: #3584e4; }\n"
    ".et-group-handle { border-radius: 6px; min-width: 0; min-height: 0; padding: 0 2px; color: #ffffff; }\n"
    ".et-group-handle.et-group-c0 { background-color: #26a269; }\n"
    ".et-group-handle.et-group-c1 { background-color: #e66100; }\n"
    ".et-group-handle.et-group-c2 { background-color: #9141ac; }\n"
    ".et-group-handle.et-group-c3 { background-color: #c01c28; }\n"
    ".et-group-handle.et-group-c4 { background-color: #865e3c; }\n"
    ".et-group-handle.et-group-c5 { background-color: #3584e4; }\n"
    ".et-group-chip.et-group-chip-active { box-shadow: inset 0 -3px alpha(#ffffff, 0.8); }\n";

  GtkCssProvider *provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider, css, -1, nullptr);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
}

/* Page helpers */

static GtkWidget *
page_of (TerminalScreen *screen)
{
  return GTK_WIDGET (terminal_screen_container_get_from_screen (screen));
}

static int
n_pages (TerminalTabGroups *m)
{
  return gtk_notebook_get_n_pages (m->notebook);
}

static TerminalScreen *
screen_at (TerminalTabGroups *m,
           int page_num)
{
  if (page_num < 0 || page_num >= n_pages (m))
    return nullptr;

  GtkWidget *child = gtk_notebook_get_nth_page (m->notebook, page_num);
  return terminal_screen_container_get_screen (TERMINAL_SCREEN_CONTAINER (child));
}

static int
pos_of (TerminalTabGroups *m,
        TerminalScreen *screen)
{
  return gtk_notebook_page_num (m->notebook, page_of (screen));
}

static TerminalTabGroup *
group_of (TerminalScreen *screen)
{
  if (screen == nullptr)
    return nullptr;
  return (TerminalTabGroup*) g_object_get_qdata (G_OBJECT (screen), group_quark ());
}

static void
set_group_of (TerminalScreen *screen,
              TerminalTabGroup *group)
{
  g_object_set_qdata (G_OBJECT (screen), group_quark (), group);
}

static TerminalScreen *
current_screen (TerminalTabGroups *m)
{
  return screen_at (m, gtk_notebook_get_current_page (m->notebook));
}

/* Returns: (transfer container): members of @group in tab order */
static GList *
members_of (TerminalTabGroups *m,
            TerminalTabGroup *group)
{
  GList *list = nullptr;
  int n = n_pages (m);
  for (int i = 0; i < n; i++) {
    TerminalScreen *screen = screen_at (m, i);
    if (group_of (screen) == group)
      list = g_list_prepend (list, screen);
  }
  return g_list_reverse (list);
}

static int
last_member_pos (TerminalTabGroups *m,
                 TerminalTabGroup *group,
                 TerminalScreen *except)
{
  int last = -1;
  int n = n_pages (m);
  for (int i = 0; i < n; i++) {
    TerminalScreen *screen = screen_at (m, i);
    if (screen != except && group_of (screen) == group)
      last = i;
  }
  return last;
}

static void
reorder_page (TerminalTabGroups *m,
              TerminalScreen *screen,
              int position)
{
  m->fixup_depth++;
  gtk_notebook_reorder_child (m->notebook, page_of (screen), position);
  m->fixup_depth--;
}

/* Moves @screen right after the last member of @group (other than itself) */
static void
move_after_group (TerminalTabGroups *m,
                  TerminalScreen *screen,
                  TerminalTabGroup *group)
{
  int last = last_member_pos (m, group, screen);
  if (last < 0)
    return;

  int pos = pos_of (m, screen);
  /* Removing @screen first shifts the members after it one slot left */
  reorder_page (m, screen, pos < last ? last : last + 1);
}

static void
set_page_visible (TerminalScreen *screen,
                  gboolean visible)
{
  gtk_widget_set_visible (page_of (screen), visible);
}

static int
pick_color (TerminalTabGroups *m)
{
  gboolean used[N_GROUP_COLORS] = { FALSE, };
  for (GList *l = m->groups; l != nullptr; l = l->next)
    used[((TerminalTabGroup*)l->data)->color] = TRUE;

  for (int i = 0; i < N_GROUP_COLORS; i++)
    if (!used[i])
      return i;

  return g_list_length (m->groups) % N_GROUP_COLORS;
}

static void
destroy_group (TerminalTabGroups *m,
               TerminalTabGroup *group)
{
  m->groups = g_list_remove (m->groups, group);
  g_free (group->name);
  g_free (group);
}

/* Updates every tab label of the notebook to reflect the groups */
static void
refresh (TerminalTabGroups *m)
{
  if (gtk_widget_in_destruction (GTK_WIDGET (m->notebook)))
    return;

  TerminalScreen *current = current_screen (m);
  int n = n_pages (m);

  for (int i = 0; i < n; i++) {
    TerminalScreen *screen = screen_at (m, i);
    TerminalTabGroup *group = group_of (screen);
    GtkWidget *child = page_of (screen);
    TerminalTabLabel *label =
      TERMINAL_TAB_LABEL (gtk_notebook_get_tab_label (m->notebook, child));

    if (group == nullptr) {
      terminal_tab_label_set_chip (label, nullptr, -1, FALSE);
      terminal_tab_label_set_group_style (label, -1, FALSE, FALSE, nullptr);
      gtk_notebook_set_tab_reorderable (m->notebook, child, TRUE);
      continue;
    }

    if (group->collapsed && group->representative == screen) {
      terminal_tab_label_set_group_style (label, -1, FALSE, FALSE, nullptr);
      terminal_tab_label_set_chip (label, group->name, group->color,
                                   group_of (current) == group);
      /* The chip is not draggable: dragging it would pull one tab out */
      gtk_notebook_set_tab_reorderable (m->notebook, child, FALSE);
      continue;
    }

    terminal_tab_label_set_chip (label, nullptr, -1, FALSE);
    terminal_tab_label_set_group_style (label, group->color,
                                        group_of (screen_at (m, i - 1)) != group,
                                        group_of (screen_at (m, i + 1)) != group,
                                        group->name);
    gtk_notebook_set_tab_reorderable (m->notebook, child, TRUE);
  }
}

/* Clears @screen's membership; destroys the group if it became empty.
 * Does not move pages. */
static void
detach_member (TerminalTabGroups *m,
               TerminalScreen *screen,
               gboolean visuals)
{
  TerminalTabGroup *group = group_of (screen);
  if (group == nullptr)
    return;

  set_group_of (screen, nullptr);
  if (group->last_active == screen)
    group->last_active = nullptr;

  gs_free_list GList *rest = members_of (m, group);
  if (rest == nullptr) {
    destroy_group (m, group);
    return;
  }

  if (group->collapsed && group->representative == screen) {
    group->representative = (TerminalScreen*) rest->data;
    if (visuals)
      set_page_visible (group->representative, TRUE);
  }
}

static void
make_representative (TerminalTabGroups *m,
                     TerminalTabGroup *group,
                     TerminalScreen *screen)
{
  TerminalScreen *old = group->representative;
  if (old == screen)
    return;

  /* Show the new one first so GtkNotebook never runs out of visible pages */
  group->representative = screen;
  set_page_visible (screen, TRUE);
  if (old != nullptr)
    set_page_visible (old, FALSE);
}

/* Public API */

TerminalTabGroups *
terminal_tab_groups_new (GtkNotebook *notebook)
{
  install_css_once ();

  TerminalTabGroups *m = g_new0 (TerminalTabGroups, 1);
  m->notebook = notebook;
  m->next_id = 1;
  return m;
}

void
terminal_tab_groups_free (TerminalTabGroups *m)
{
  if (m == nullptr)
    return;

  while (m->groups != nullptr)
    destroy_group (m, (TerminalTabGroup*) m->groups->data);
  g_free (m);
}

TerminalTabGroup *
terminal_tab_groups_get_group_of (TerminalTabGroups *m,
                                  TerminalScreen *screen)
{
  TerminalTabGroup *group = group_of (screen);
  /* Only report groups of this window */
  return g_list_find (m->groups, group) ? group : nullptr;
}

TerminalTabGroup *
terminal_tab_groups_find (TerminalTabGroups *m,
                          guint id)
{
  for (GList *l = m->groups; l != nullptr; l = l->next)
    if (((TerminalTabGroup*)l->data)->id == id)
      return (TerminalTabGroup*) l->data;
  return nullptr;
}

GList *
terminal_tab_groups_list (TerminalTabGroups *m)
{
  GList *list = nullptr;
  int n = n_pages (m);
  for (int i = 0; i < n; i++) {
    TerminalTabGroup *group = group_of (screen_at (m, i));
    if (group != nullptr && g_list_find (list, group) == nullptr)
      list = g_list_prepend (list, group);
  }
  return g_list_reverse (list);
}

TerminalTabGroup *
terminal_tab_groups_get_chip_at (TerminalTabGroups *m,
                                 int page_num)
{
  TerminalScreen *screen = screen_at (m, page_num);
  TerminalTabGroup *group = group_of (screen);
  if (group != nullptr && group->collapsed && group->representative == screen)
    return group;
  return nullptr;
}

gboolean
terminal_tab_groups_is_navigable (TerminalTabGroups *m,
                                  TerminalScreen *screen)
{
  TerminalTabGroup *group = group_of (screen);
  return group == nullptr || !group->collapsed;
}

guint
terminal_tab_group_get_id (TerminalTabGroup *group)
{
  return group->id;
}

const char *
terminal_tab_group_get_name (TerminalTabGroup *group)
{
  return group->name;
}

gboolean
terminal_tab_group_get_collapsed (TerminalTabGroup *group)
{
  return group->collapsed;
}

TerminalTabGroup *
terminal_tab_groups_create (TerminalTabGroups *m,
                            TerminalScreen *screen,
                            const char *name)
{
  TerminalTabGroup *group = g_new0 (TerminalTabGroup, 1);
  group->id = m->next_id++;
  group->name = g_strdup (name);
  group->color = pick_color (m);
  m->groups = g_list_append (m->groups, group);

  /* The tab leaves its old group (if any) and the new group starts where the
   * tab is; keep the old group contiguous */
  TerminalTabGroup *old = group_of (screen);
  if (old != nullptr) {
    move_after_group (m, screen, old);
    detach_member (m, screen, TRUE);
  }

  set_group_of (screen, group);
  group->last_active = screen;
  refresh (m);
  return group;
}

void
terminal_tab_groups_join (TerminalTabGroups *m,
                          TerminalScreen *screen,
                          TerminalTabGroup *group)
{
  TerminalTabGroup *old = group_of (screen);
  if (old == group)
    return;

  if (old != nullptr)
    detach_member (m, screen, TRUE);

  move_after_group (m, screen, group);
  set_group_of (screen, group);

  if (group->collapsed) {
    if (screen == current_screen (m))
      make_representative (m, group, screen);
    else
      set_page_visible (screen, FALSE);
  }

  refresh (m);
}

void
terminal_tab_groups_leave (TerminalTabGroups *m,
                           TerminalScreen *screen)
{
  TerminalTabGroup *group = group_of (screen);
  if (group == nullptr)
    return;

  move_after_group (m, screen, group);
  detach_member (m, screen, TRUE);
  set_page_visible (screen, TRUE);
  refresh (m);
}

void
terminal_tab_groups_collapse (TerminalTabGroups *m,
                              TerminalTabGroup *group)
{
  if (group->collapsed)
    return;

  gs_free_list GList *members = members_of (m, group);
  if (members == nullptr)
    return;

  TerminalScreen *current = current_screen (m);
  TerminalScreen *rep;
  if (group_of (current) == group)
    rep = current; /* never hide the active terminal */
  else if (group->last_active != nullptr)
    rep = group->last_active;
  else
    rep = (TerminalScreen*) members->data;

  group->collapsed = TRUE;
  group->representative = rep;
  for (GList *l = members; l != nullptr; l = l->next)
    if (l->data != rep)
      set_page_visible ((TerminalScreen*) l->data, FALSE);

  refresh (m);
}

void
terminal_tab_groups_expand (TerminalTabGroups *m,
                            TerminalTabGroup *group)
{
  if (!group->collapsed)
    return;

  gs_free_list GList *members = members_of (m, group);
  for (GList *l = members; l != nullptr; l = l->next)
    set_page_visible ((TerminalScreen*) l->data, TRUE);

  group->collapsed = FALSE;
  TerminalScreen *target = group->last_active ? group->last_active : group->representative;
  group->representative = nullptr;
  refresh (m);

  if (target != nullptr)
    gtk_notebook_set_current_page (m->notebook, pos_of (m, target));
}

void
terminal_tab_groups_rename (TerminalTabGroups *m,
                            TerminalTabGroup *group,
                            const char *name)
{
  g_free (group->name);
  group->name = g_strdup (name);
  refresh (m);
}

void
terminal_tab_groups_ungroup (TerminalTabGroups *m,
                             TerminalTabGroup *group)
{
  gs_free_list GList *members = members_of (m, group);
  for (GList *l = members; l != nullptr; l = l->next) {
    set_page_visible ((TerminalScreen*) l->data, TRUE);
    set_group_of ((TerminalScreen*) l->data, nullptr);
  }

  destroy_group (m, group);
  refresh (m);
}

void
terminal_tab_groups_prepare_activate (TerminalTabGroups *m,
                                      TerminalScreen *screen)
{
  TerminalTabGroup *group = group_of (screen);
  if (group == nullptr || !group->collapsed)
    return;

  make_representative (m, group, screen);
  refresh (m);
}

/* Membership rule after a tab was moved (drag or Move Terminal Left/Right):
 * - landing between two tabs of the same expanded group joins that group;
 * - landing inside a collapsed group puts the tab right after it;
 * - a member that is no longer next to its group leaves it. */
static void
apply_membership_rule (TerminalTabGroups *m,
                       TerminalScreen *screen,
                       gboolean allow_move)
{
  int pos = pos_of (m, screen);
  TerminalTabGroup *own = group_of (screen);
  TerminalTabGroup *left = group_of (screen_at (m, pos - 1));
  TerminalTabGroup *right = group_of (screen_at (m, pos + 1));

  if (left != nullptr && left == right && left != own) {
    if (left->collapsed) {
      if (allow_move) {
        move_after_group (m, screen, left);
        apply_membership_rule (m, screen, FALSE);
      }
      return;
    }

    if (own != nullptr)
      detach_member (m, screen, TRUE);
    set_group_of (screen, left);
    return;
  }

  if (own == nullptr || own == left || own == right)
    return;

  /* Moved away from its group; a lone member takes its group along */
  gs_free_list GList *members = members_of (m, own);
  if (g_list_length (members) > 1)
    detach_member (m, screen, TRUE);
}

void
terminal_tab_groups_reorder (TerminalTabGroups *m,
                             TerminalScreen *screen,
                             int direction)
{
  g_return_if_fail (direction == 1 || direction == -1);

  int n = n_pages (m);
  TerminalTabGroup *own = group_of (screen);

  /* The block being moved: a collapsed group moves as a whole */
  gs_free_list GList *block = nullptr;
  if (own != nullptr && own->collapsed)
    block = members_of (m, own);
  else
    block = g_list_prepend (nullptr, screen);

  int first = pos_of (m, (TerminalScreen*) block->data);
  int last = pos_of (m, (TerminalScreen*) g_list_last (block)->data);
  int len = last - first + 1;

  /* Destination of the block's first tab */
  int dest;
  if (direction < 0) {
    if (first == 0) {
      dest = n - len; /* wrap around */
    } else {
      /* Jump over a whole collapsed group */
      TerminalTabGroup *neighbour = group_of (screen_at (m, first - 1));
      int step = 1;
      if (neighbour != nullptr && neighbour->collapsed && neighbour != own) {
        while (group_of (screen_at (m, first - 1 - step)) == neighbour)
          step++;
      }
      dest = first - step;
    }
  } else {
    if (last == n - 1) {
      dest = 0; /* wrap around */
    } else {
      TerminalTabGroup *neighbour = group_of (screen_at (m, last + 1));
      int step = 1;
      if (neighbour != nullptr && neighbour->collapsed && neighbour != own) {
        while (group_of (screen_at (m, last + 1 + step)) == neighbour)
          step++;
      }
      dest = first + step;
    }
  }

  /* Move the block tab by tab, keeping its internal order */
  if (dest < first) {
    int i = 0;
    for (GList *l = block; l != nullptr; l = l->next, i++)
      reorder_page (m, (TerminalScreen*) l->data, dest + i);
  } else {
    int i = len - 1;
    for (GList *l = g_list_last (block); l != nullptr; l = l->prev, i--)
      reorder_page (m, (TerminalScreen*) l->data, dest + i);
  }

  if (!(own != nullptr && own->collapsed))
    apply_membership_rule (m, screen, TRUE);

  refresh (m);
}

void
terminal_tab_groups_page_added (TerminalTabGroups *m,
                                TerminalScreen *screen)
{
  /* New and moved-in tabs start free; never split a group */
  set_group_of (screen, nullptr);

  int pos = pos_of (m, screen);
  TerminalTabGroup *left = group_of (screen_at (m, pos - 1));
  TerminalTabGroup *right = group_of (screen_at (m, pos + 1));
  if (left != nullptr && left == right)
    move_after_group (m, screen, left);

  refresh (m);
}

void
terminal_tab_groups_page_removed (TerminalTabGroups *m,
                                  TerminalScreen *screen)
{
  gboolean visuals = !gtk_widget_in_destruction (GTK_WIDGET (m->notebook));

  /* The page's container is discarded: a tab moved to another window gets
   * a new one there, always visible */
  detach_member (m, screen, visuals);

  if (visuals)
    refresh (m);
}

void
terminal_tab_groups_page_reordered (TerminalTabGroups *m,
                                    TerminalScreen *screen)
{
  if (m->fixup_depth > 0)
    return;

  /* A tab dragged with the mouse */
  apply_membership_rule (m, screen, TRUE);
  refresh (m);
}

void
terminal_tab_groups_page_switched (TerminalTabGroups *m,
                                   TerminalScreen *screen)
{
  TerminalTabGroup *group = group_of (screen);
  if (group != nullptr)
    group->last_active = screen;

  refresh (m);
}
