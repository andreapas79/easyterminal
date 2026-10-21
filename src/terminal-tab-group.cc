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

#include <glib/gi18n.h>

#include "terminal-tab-group.hh"
#include "terminal-intl.hh"
#include "terminal-screen-container.hh"
#include "terminal-tab-label.hh"
#include "terminal-libgsystem.hh"

#define N_GROUP_COLORS (6)

/* Band colour on member tabs, pill background. No orange: it is the
 * current-tab accent of Pop!_OS. */
static const struct {
  const char *band;
  const char *pill;
} group_colors[N_GROUP_COLORS] = {
  { "#33d17a", "#26a269" }, /* green */
  { "#c061cb", "#9141ac" }, /* purple */
  { "#e01b24", "#c01c28" }, /* red */
  { "#3584e4", "#1c71d8" }, /* blue */
  { "#986a44", "#865e3c" }, /* brown */
  { "#2190a4", "#1a7a8c" }, /* teal */
};

struct _TerminalTabGroup {
  guint id;
  char *name;
  gboolean collapsed;
  int color;
  GtkWidget *header;   /* the header page (owned by the notebook) */
  GtkWidget *pill;     /* its tab label */
  /* Raw pointer to a member screen; cleared when it leaves the group */
  TerminalScreen *last_active;
};

struct _TerminalTabGroups {
  GtkNotebook *notebook; /* not owned: the manager lives inside it */
  GList *groups;         /* owned TerminalTabGroup* */
  guint next_id;
  int fixup_depth;       /* >0 while we move pages ourselves */
};

static GQuark
group_quark (void)
{
  static GQuark quark = 0;
  if (G_UNLIKELY (quark == 0))
    quark = g_quark_from_static_string ("easyterminal-tab-group");
  return quark;
}

static GQuark
header_quark (void)
{
  static GQuark quark = 0;
  if (G_UNLIKELY (quark == 0))
    quark = g_quark_from_static_string ("easyterminal-tab-group-header");
  return quark;
}

/* Styling
 *
 * The group band must span the whole tab, but the tab label sits inside
 * the theme's tab padding; the label is stretched over that padding with
 * negative margins read from the theme. The current-tab underline of the
 * theme is replaced by one as wide as the tab's content, in the theme's own
 * colour (sampled by rendering a checked tab). */

static GtkStyleContext *
tab_style_context_new (GtkStateFlags state)
{
  GtkWidgetPath *path = gtk_widget_path_new ();
  gtk_widget_path_append_type (path, GTK_TYPE_NOTEBOOK);
  gtk_widget_path_iter_set_object_name (path, -1, "notebook");
  gtk_widget_path_append_type (path, G_TYPE_NONE);
  gtk_widget_path_iter_set_object_name (path, -1, "header");
  gtk_widget_path_iter_add_class (path, -1, "top");
  gtk_widget_path_append_type (path, G_TYPE_NONE);
  gtk_widget_path_iter_set_object_name (path, -1, "tabs");
  gtk_widget_path_append_type (path, G_TYPE_NONE);
  gtk_widget_path_iter_set_object_name (path, -1, "tab");

  GtkStyleContext *context = gtk_style_context_new ();
  gtk_style_context_set_screen (context, gdk_screen_get_default ());
  gtk_style_context_set_path (context, path);
  gtk_style_context_set_state (context, state);
  gtk_widget_path_unref (path);
  return context;
}

static gboolean
sample_pixel (GtkStyleContext *context,
              int width,
              int height,
              int x,
              int y,
              guint8 rgb[3])
{
  cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create (surface);
  gtk_render_background (context, cr, 0, 0, width, height);
  cairo_destroy (cr);
  cairo_surface_flush (surface);

  const guint8 *data = cairo_image_surface_get_data (surface);
  int stride = cairo_image_surface_get_stride (surface);
  guint32 pixel = *(const guint32*)(data + y * stride + x * 4);
  guint a = pixel >> 24;
  gboolean ok = a > 0;
  if (ok) {
    /* premultiplied ARGB */
    rgb[0] = MIN (255u, (((pixel >> 16) & 0xff) * 255u) / a);
    rgb[1] = MIN (255u, (((pixel >> 8) & 0xff) * 255u) / a);
    rgb[2] = MIN (255u, ((pixel & 0xff) * 255u) / a);
  }
  cairo_surface_destroy (surface);
  return ok;
}

/* The colour of the theme's current-tab underline, if the theme draws one
 * at the bottom of a checked top tab */
static gboolean
theme_current_tab_color (char out[8])
{
  gs_unref_object GtkStyleContext *checked = tab_style_context_new (GTK_STATE_FLAG_CHECKED);
  gs_unref_object GtkStyleContext *normal = tab_style_context_new (GTK_STATE_FLAG_NORMAL);
  const int w = 64, h = 40;
  guint8 c[3] = { 0, }, n[3] = { 0, };

  gboolean has_c = sample_pixel (checked, w, h, w / 2, h - 2, c);
  gboolean has_n = sample_pixel (normal, w, h, w / 2, h - 2, n);
  if (!has_c)
    return FALSE;
  if (has_n && ABS (c[0] - n[0]) + ABS (c[1] - n[1]) + ABS (c[2] - n[2]) < 24)
    return FALSE; /* no distinct underline */

  g_snprintf (out, 8, "#%02x%02x%02x", c[0], c[1], c[2]);
  return TRUE;
}

static char *
build_css (void)
{
  gs_unref_object GtkStyleContext *context = tab_style_context_new (GTK_STATE_FLAG_NORMAL);
  GtkBorder padding, border;
  gtk_style_context_get_padding (context, GTK_STATE_FLAG_NORMAL, &padding);
  gtk_style_context_get_border (context, GTK_STATE_FLAG_NORMAL, &border);
  int top = padding.top + border.top;
  int bottom = padding.bottom + border.bottom;
  int left = padding.left + border.left;
  int right = padding.right + border.right;

  GString *css = g_string_new (nullptr);

  /* Group pill (the header page's tab) */
  g_string_append (css,
                   ".et-group-pill { border-radius: 6px; padding: 0 8px; color: #ffffff; font-weight: bold; }\n"
                   ".et-group-pill.et-group-pill-active { box-shadow: inset 0 -3px alpha(#ffffff, 0.8); }\n");
  for (int i = 0; i < N_GROUP_COLORS; i++)
    g_string_append_printf (css, ".et-group-pill.et-group-c%d { background-color: %s; }\n",
                            i, group_colors[i].pill);

  /* Group band across the top of member tabs, over the theme's padding */
  g_string_append_printf (css,
                          "notebook.et-notebook > header.top tabs tab .et-group-member {"
                          " margin: -%dpx -%dpx 0 -%dpx; padding: %dpx %dpx 0 %dpx; }\n",
                          top, right, left, top, right, left);
  for (int i = 0; i < N_GROUP_COLORS; i++)
    g_string_append_printf (css,
                            "notebook.et-notebook > header tabs tab .et-group-member.et-group-c%d"
                            " { box-shadow: inset 0 3px %s; }\n",
                            i, group_colors[i].band);

  /* Current-tab underline as wide as the tab content */
  char accent[8];
  if (theme_current_tab_color (accent)) {
    g_string_append_printf (css,
                            "notebook.et-notebook > header.top tabs tab:checked { box-shadow: none; }\n"
                            "notebook.et-notebook > header.top tabs tab .et-tab-current {"
                            " box-shadow: inset 0 -3px %s; margin-bottom: -%dpx; padding-bottom: %dpx; }\n",
                            accent, bottom, bottom);
  }

  return g_string_free (css, FALSE);
}

static GtkCssProvider *css_provider = nullptr;

static void
reload_css (void)
{
  gs_free char *css = build_css ();
  gtk_css_provider_load_from_data (css_provider, css, -1, nullptr);
}

static void
theme_changed_cb (GtkSettings *settings,
                  GParamSpec *pspec,
                  gpointer user_data)
{
  reload_css ();
}

static void
install_css_once (void)
{
  if (css_provider != nullptr)
    return;

  css_provider = gtk_css_provider_new ();
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (css_provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  reload_css ();

  GtkSettings *settings = gtk_settings_get_default ();
  g_signal_connect (settings, "notify::gtk-theme-name", G_CALLBACK (theme_changed_cb), nullptr);
  g_signal_connect (settings, "notify::gtk-application-prefer-dark-theme", G_CALLBACK (theme_changed_cb), nullptr);
}

/* Page helpers */

gboolean
terminal_tab_groups_is_header (GtkWidget *page)
{
  return page != nullptr && g_object_get_qdata (G_OBJECT (page), header_quark ()) != nullptr;
}

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

static GtkWidget *
page_at (TerminalTabGroups *m,
         int page_num)
{
  if (page_num < 0 || page_num >= n_pages (m))
    return nullptr;
  return gtk_notebook_get_nth_page (m->notebook, page_num);
}

/* nullptr for header pages */
static TerminalScreen *
screen_of_page (GtkWidget *page)
{
  if (page == nullptr || terminal_tab_groups_is_header (page))
    return nullptr;
  return terminal_screen_container_get_screen (TERMINAL_SCREEN_CONTAINER (page));
}

static int
pos_of_page (TerminalTabGroups *m,
             GtkWidget *page)
{
  return gtk_notebook_page_num (m->notebook, page);
}

static int
pos_of (TerminalTabGroups *m,
        TerminalScreen *screen)
{
  return pos_of_page (m, page_of (screen));
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

/* The group a page belongs to: a header belongs to its group */
static TerminalTabGroup *
group_of_page (GtkWidget *page)
{
  if (page == nullptr)
    return nullptr;
  if (terminal_tab_groups_is_header (page))
    return (TerminalTabGroup*) g_object_get_qdata (G_OBJECT (page), header_quark ());
  return group_of (screen_of_page (page));
}

/* The group "around" position @pos when looking at the page on its left
 * (a header opens its group) or on its right (a header there starts a new
 * block, so it does not count) */
static TerminalTabGroup *
group_on_left (TerminalTabGroups *m,
               int pos)
{
  return group_of_page (page_at (m, pos - 1));
}

static TerminalTabGroup *
group_on_right (TerminalTabGroups *m,
                int pos)
{
  GtkWidget *page = page_at (m, pos + 1);
  if (terminal_tab_groups_is_header (page))
    return nullptr;
  return group_of_page (page);
}

static TerminalScreen *
current_screen (TerminalTabGroups *m)
{
  return screen_of_page (page_at (m, gtk_notebook_get_current_page (m->notebook)));
}

/* Returns: (transfer container): members of @group in tab order */
static GList *
members_of (TerminalTabGroups *m,
            TerminalTabGroup *group)
{
  GList *list = nullptr;
  int n = n_pages (m);
  for (int i = 0; i < n; i++) {
    TerminalScreen *screen = screen_of_page (page_at (m, i));
    if (screen != nullptr && group_of (screen) == group)
      list = g_list_prepend (list, screen);
  }
  return g_list_reverse (list);
}

/* Position of the last page of @group's block (header or member), not
 * counting @except */
static int
block_end (TerminalTabGroups *m,
           TerminalTabGroup *group,
           TerminalScreen *except)
{
  int last = -1;
  int n = n_pages (m);
  for (int i = 0; i < n; i++) {
    GtkWidget *page = page_at (m, i);
    if (screen_of_page (page) != nullptr && screen_of_page (page) == except)
      continue;
    if (group_of_page (page) == group)
      last = i;
  }
  return last;
}

static void
reorder_page (TerminalTabGroups *m,
              GtkWidget *page,
              int position)
{
  m->fixup_depth++;
  gtk_notebook_reorder_child (m->notebook, page, position);
  m->fixup_depth--;
}

/* Moves @page right after position @after (which is computed before the
 * move) */
static void
move_page_after (TerminalTabGroups *m,
                 GtkWidget *page,
                 int after)
{
  int pos = pos_of_page (m, page);
  /* Removing @page first shifts the pages after it one slot left */
  reorder_page (m, page, pos <= after ? after : after + 1);
}

/* Moves @screen right after the end of @group's block */
static void
move_after_group (TerminalTabGroups *m,
                  TerminalScreen *screen,
                  TerminalTabGroup *group)
{
  int end = block_end (m, group, screen);
  if (end >= 0)
    move_page_after (m, page_of (screen), end);
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

/* Keeps every header right before its group's first member */
static void
normalize_headers (TerminalTabGroups *m)
{
  for (GList *l = m->groups; l != nullptr; l = l->next) {
    TerminalTabGroup *group = (TerminalTabGroup*) l->data;
    gs_free_list GList *members = members_of (m, group);
    if (members == nullptr)
      continue;

    int first = pos_of (m, (TerminalScreen*) members->data);
    int header = pos_of_page (m, group->header);
    if (header == first - 1)
      continue;
    /* Put the header at the first member's slot, pushing it right */
    reorder_page (m, group->header, header < first ? first - 1 : first);
  }
}

static void
destroy_group (TerminalTabGroups *m,
               TerminalTabGroup *group,
               gboolean remove_header)
{
  m->groups = g_list_remove (m->groups, group);

  if (remove_header && group->header != nullptr) {
    m->fixup_depth++;
    gtk_container_remove (GTK_CONTAINER (m->notebook), group->header);
    m->fixup_depth--;
  }

  g_free (group->name);
  g_free (group);
}

static void
update_pill (TerminalTabGroups *m,
             TerminalTabGroup *group)
{
  GtkStyleContext *context = gtk_widget_get_style_context (group->pill);
  for (int i = 0; i < N_GROUP_COLORS; i++) {
    char name[16];
    g_snprintf (name, sizeof (name), "et-group-c%d", i);
    if (i == group->color)
      gtk_style_context_add_class (context, name);
    else
      gtk_style_context_remove_class (context, name);
  }

  if (group->collapsed) {
    gs_free char *text = g_strdup_printf ("%s ▸", group->name);
    gtk_label_set_text (GTK_LABEL (group->pill), text);
    gtk_widget_set_tooltip_text (group->pill, _("Click to expand the group"));
  } else {
    gtk_label_set_text (GTK_LABEL (group->pill), group->name);
    gtk_widget_set_tooltip_text (group->pill, _("Click to collapse the group"));
  }

  TerminalScreen *current = current_screen (m);
  if (group->collapsed && current != nullptr && group_of (current) == group)
    gtk_style_context_add_class (context, "et-group-pill-active");
  else
    gtk_style_context_remove_class (context, "et-group-pill-active");
}

/* Updates every tab of the notebook to reflect the groups */
static void
refresh (TerminalTabGroups *m)
{
  if (gtk_widget_in_destruction (GTK_WIDGET (m->notebook)))
    return;

  for (GList *l = m->groups; l != nullptr; l = l->next)
    update_pill (m, (TerminalTabGroup*) l->data);

  TerminalScreen *current = current_screen (m);
  int n = n_pages (m);
  for (int i = 0; i < n; i++) {
    TerminalScreen *screen = screen_of_page (page_at (m, i));
    if (screen == nullptr)
      continue;

    TerminalTabGroup *group = group_of (screen);
    TerminalTabLabel *label =
      TERMINAL_TAB_LABEL (gtk_notebook_get_tab_label (m->notebook, page_of (screen)));
    terminal_tab_label_set_group_style (label, group ? group->color : -1);
    terminal_tab_label_set_current (label, screen == current);
  }
}

/* Clears @screen's membership; destroys the group if it became empty.
 * Does not move pages. */
static void
detach_member (TerminalTabGroups *m,
               TerminalScreen *screen,
               gboolean remove_header)
{
  TerminalTabGroup *group = group_of (screen);
  if (group == nullptr)
    return;

  set_group_of (screen, nullptr);
  if (group->last_active == screen)
    group->last_active = nullptr;

  gs_free_list GList *rest = members_of (m, group);
  if (rest == nullptr)
    destroy_group (m, group, remove_header);
}

/* In a collapsed group only the current tab may stay visible */
static void
hide_collapsed_members (TerminalTabGroups *m)
{
  TerminalScreen *current = current_screen (m);
  for (GList *l = m->groups; l != nullptr; l = l->next) {
    TerminalTabGroup *group = (TerminalTabGroup*) l->data;
    if (!group->collapsed)
      continue;

    gs_free_list GList *members = members_of (m, group);
    for (GList *k = members; k != nullptr; k = k->next)
      if (k->data != current)
        set_page_visible ((TerminalScreen*) k->data, FALSE);
  }
}

/* Public API */

TerminalTabGroups *
terminal_tab_groups_new (GtkNotebook *notebook)
{
  install_css_once ();
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (notebook)), "et-notebook");

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

  /* The notebook is gone: its pages (headers too) were already destroyed */
  while (m->groups != nullptr)
    destroy_group (m, (TerminalTabGroup*) m->groups->data, FALSE);
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
    GtkWidget *page = page_at (m, i);
    if (terminal_tab_groups_is_header (page))
      list = g_list_prepend (list, group_of_page (page));
  }
  return g_list_reverse (list);
}

TerminalTabGroup *
terminal_tab_groups_get_header_at (TerminalTabGroups *m,
                                   int page_num)
{
  GtkWidget *page = page_at (m, page_num);
  return terminal_tab_groups_is_header (page) ? group_of_page (page) : nullptr;
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
  /* The tab leaves its old group (if any), keeping that one contiguous */
  TerminalTabGroup *old = group_of (screen);
  if (old != nullptr) {
    move_after_group (m, screen, old);
    detach_member (m, screen, TRUE);
  }

  TerminalTabGroup *group = g_new0 (TerminalTabGroup, 1);
  group->id = m->next_id++;
  group->name = g_strdup (name);
  group->color = pick_color (m);
  group->last_active = screen;
  m->groups = g_list_append (m->groups, group);

  /* The header page: no terminal, its tab is the pill */
  group->header = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  g_object_set_qdata (G_OBJECT (group->header), header_quark (), group);
  gtk_widget_show (group->header);

  group->pill = gtk_label_new (nullptr);
  gtk_label_set_single_line_mode (GTK_LABEL (group->pill), TRUE);
  gtk_style_context_add_class (gtk_widget_get_style_context (group->pill), "et-group-pill");
  gtk_widget_show (group->pill);

  set_group_of (screen, group);

  m->fixup_depth++;
  gtk_notebook_insert_page (m->notebook, group->header, group->pill, pos_of (m, screen));
  gtk_container_child_set (GTK_CONTAINER (m->notebook), group->header,
                           "tab-expand", FALSE,
                           "tab-fill", TRUE,
                           nullptr);
  gtk_notebook_set_tab_reorderable (m->notebook, group->header, FALSE);
  m->fixup_depth--;

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
  if (screen == current_screen (m))
    group->last_active = screen;
  normalize_headers (m);

  /* Joining a collapsed group: hidden, unless it is the active tab */
  hide_collapsed_members (m);
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
  normalize_headers (m);
  refresh (m);
}

void
terminal_tab_groups_collapse (TerminalTabGroups *m,
                              TerminalTabGroup *group)
{
  if (group->collapsed)
    return;

  group->collapsed = TRUE;
  /* The active terminal is never hidden: if it is in the group it stays
   * next to the pill until another tab is selected */
  hide_collapsed_members (m);
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
  refresh (m);

  TerminalScreen *target = group->last_active;
  if (target == nullptr && members != nullptr)
    target = (TerminalScreen*) members->data;
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

  destroy_group (m, group, TRUE);
  refresh (m);
}

void
terminal_tab_groups_prepare_activate (TerminalTabGroups *m,
                                      TerminalScreen *screen)
{
  /* Shown next to its collapsed group's pill; hidden again when another
   * tab is selected (terminal_tab_groups_page_switched) */
  set_page_visible (screen, TRUE);
}

/* Membership rule after a tab was moved (drag or Move Terminal Left/Right):
 * - landing inside an expanded group joins it;
 * - landing inside a collapsed group puts the tab right after it;
 * - a member that is no longer inside its group leaves it. */
static void
apply_membership_rule (TerminalTabGroups *m,
                       TerminalScreen *screen,
                       gboolean allow_move)
{
  int pos = pos_of (m, screen);
  TerminalTabGroup *own = group_of (screen);
  TerminalTabGroup *left = group_on_left (m, pos);
  TerminalTabGroup *right = group_on_right (m, pos);

  /* Inside a block: right after its header, or between two of its tabs */
  TerminalTabGroup *inside = nullptr;
  if (left != nullptr && terminal_tab_groups_is_header (page_at (m, pos - 1)))
    inside = left;
  else if (left != nullptr && left == right)
    inside = left;

  if (inside != nullptr && inside != own) {
    if (inside->collapsed) {
      if (allow_move) {
        move_after_group (m, screen, inside);
        apply_membership_rule (m, screen, FALSE);
      }
      return;
    }

    if (own != nullptr)
      detach_member (m, screen, TRUE);
    set_group_of (screen, inside);
    if (screen == current_screen (m))
      inside->last_active = screen;
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

  /* A collapsed group, or a group with this tab only, moves as a whole
   * (header included); otherwise the tab moves alone */
  gboolean move_block = FALSE;
  if (own != nullptr) {
    gs_free_list GList *members = members_of (m, own);
    move_block = own->collapsed || g_list_length (members) == 1;
  }

  int first = move_block ? pos_of_page (m, own->header) : pos_of (m, screen);
  int last = move_block ? block_end (m, own, nullptr) : first;
  int len = last - first + 1;

  /* The neighbouring unit: a collapsed group (header + members) counts as
   * one; a header of an expanded group is stepped over so the tab lands
   * inside that group */
  int dest;
  if (direction < 0) {
    if (first == 0) {
      dest = n - len; /* wrap around */
    } else {
      TerminalTabGroup *neighbour = group_of_page (page_at (m, first - 1));
      int step = 1;
      if (neighbour != nullptr && neighbour != own && neighbour->collapsed)
        step = first - pos_of_page (m, neighbour->header);
      dest = first - step;
    }
  } else {
    if (last == n - 1) {
      dest = 0; /* wrap around */
    } else {
      GtkWidget *next = page_at (m, last + 1);
      TerminalTabGroup *neighbour = group_of_page (next);
      int step = 1;
      if (neighbour != nullptr && neighbour != own && terminal_tab_groups_is_header (next)) {
        if (neighbour->collapsed)
          step = block_end (m, neighbour, nullptr) - last;
        else
          step = 1; /* lands right after the header: joins that group */
      }
      dest = first + step;
    }
  }

  /* Collect the pages first, then move them keeping their order */
  gs_free_list GList *pages = nullptr;
  for (int i = first; i <= last; i++)
    pages = g_list_append (pages, page_at (m, i));

  if (dest < first) {
    int i = 0;
    for (GList *l = pages; l != nullptr; l = l->next, i++)
      reorder_page (m, (GtkWidget*) l->data, dest + i);
  } else {
    int i = len - 1;
    for (GList *l = g_list_last (pages); l != nullptr; l = l->prev, i--)
      reorder_page (m, (GtkWidget*) l->data, dest + i);
  }

  if (!move_block)
    apply_membership_rule (m, screen, TRUE);

  normalize_headers (m);
  refresh (m);
}

void
terminal_tab_groups_page_added (TerminalTabGroups *m,
                                TerminalScreen *screen)
{
  if (m->fixup_depth > 0)
    return;

  /* New and moved-in tabs start free; never split a group */
  set_group_of (screen, nullptr);

  int pos = pos_of (m, screen);
  TerminalTabGroup *left = group_on_left (m, pos);
  if (left != nullptr &&
      (terminal_tab_groups_is_header (page_at (m, pos - 1)) || left == group_on_right (m, pos)))
    move_after_group (m, screen, left);

  refresh (m);
}

void
terminal_tab_groups_page_removed (TerminalTabGroups *m,
                                  TerminalScreen *screen)
{
  /* While the window is being destroyed GTK removes every page itself */
  gboolean alive = !gtk_widget_in_destruction (GTK_WIDGET (m->notebook));

  detach_member (m, screen, alive);

  if (alive)
    refresh (m);
}

void
terminal_tab_groups_page_reordered (TerminalTabGroups *m,
                                    GtkWidget *page)
{
  if (m->fixup_depth > 0)
    return;

  /* A tab dragged with the mouse */
  TerminalScreen *screen = screen_of_page (page);
  if (screen != nullptr)
    apply_membership_rule (m, screen, TRUE);

  normalize_headers (m);
  refresh (m);
}

void
terminal_tab_groups_page_switched (TerminalTabGroups *m,
                                   TerminalScreen *screen)
{
  TerminalTabGroup *group = group_of (screen);
  if (group != nullptr)
    group->last_active = screen;

  hide_collapsed_members (m);
  refresh (m);
}
