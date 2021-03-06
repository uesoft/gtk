/*
 * Copyright (c) 2008-2009  Christian Hammond
 * Copyright (c) 2008-2009  David Trowbridge
 * Copyright (c) 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <stdlib.h>

#include "window.h"
#include "prop-list.h"
#include "css-editor.h"
#include "css-node-tree.h"
#include "object-hierarchy.h"
#include "object-tree.h"
#include "selector.h"
#include "size-groups.h"
#include "data-list.h"
#include "signals-list.h"
#include "actions.h"
#include "menu.h"
#include "misc-info.h"
#include "gestures.h"
#include "magnifier.h"
#include "recorder.h"

#include "gtklabel.h"
#include "gtkbutton.h"
#include "gtkstack.h"
#include "gtktreeviewcolumn.h"
#include "gtkmodulesprivate.h"
#include "gtkwindowprivate.h"
#include "gtkwindowgroup.h"
#include "gtkprivate.h"
#include "gdk-private.h"
#include "gskrendererprivate.h"

G_DEFINE_TYPE (GtkInspectorWindow, gtk_inspector_window, GTK_TYPE_WINDOW)

static gboolean
set_selected_object (GtkInspectorWindow *iw,
                     GObject            *selected)
{
  GList *l;
  const char *title;

  if (!gtk_inspector_prop_list_set_object (GTK_INSPECTOR_PROP_LIST (iw->prop_list), selected))
    return FALSE;

  title = (const char *)g_object_get_data (selected, "gtk-inspector-object-title");
  gtk_label_set_label (GTK_LABEL (iw->object_title), title);

  gtk_inspector_prop_list_set_object (GTK_INSPECTOR_PROP_LIST (iw->child_prop_list), selected);
  gtk_inspector_signals_list_set_object (GTK_INSPECTOR_SIGNALS_LIST (iw->signals_list), selected);
  gtk_inspector_object_hierarchy_set_object (GTK_INSPECTOR_OBJECT_HIERARCHY (iw->object_hierarchy), selected);
  gtk_inspector_selector_set_object (GTK_INSPECTOR_SELECTOR (iw->selector), selected);
  gtk_inspector_misc_info_set_object (GTK_INSPECTOR_MISC_INFO (iw->misc_info), selected);
  gtk_inspector_css_node_tree_set_object (GTK_INSPECTOR_CSS_NODE_TREE (iw->widget_css_node_tree), selected);
  gtk_inspector_size_groups_set_object (GTK_INSPECTOR_SIZE_GROUPS (iw->size_groups), selected);
  gtk_inspector_data_list_set_object (GTK_INSPECTOR_DATA_LIST (iw->data_list), selected);
  gtk_inspector_actions_set_object (GTK_INSPECTOR_ACTIONS (iw->actions), selected);
  gtk_inspector_menu_set_object (GTK_INSPECTOR_MENU (iw->menu), selected);
  gtk_inspector_gestures_set_object (GTK_INSPECTOR_GESTURES (iw->gestures), selected);
  gtk_inspector_magnifier_set_object (GTK_INSPECTOR_MAGNIFIER (iw->magnifier), selected);

  for (l = iw->extra_pages; l != NULL; l = l->next)
    g_object_set (l->data, "object", selected, NULL);

  return TRUE;
}

static void
on_object_activated (GtkInspectorObjectTree *wt,
                     GObject                *selected,
                     const gchar            *name,
                     GtkInspectorWindow     *iw)
{
  const gchar *tab;

  if (!set_selected_object (iw, selected))
    return;

  tab = g_object_get_data (G_OBJECT (wt), "next-tab");
  if (tab)
    gtk_stack_set_visible_child_name (GTK_STACK (iw->object_details), tab);

  gtk_stack_set_visible_child_name (GTK_STACK (iw->object_stack), "object-details");
  gtk_stack_set_visible_child_name (GTK_STACK (iw->object_buttons), "details");
}

static void
on_object_selected (GtkInspectorObjectTree *wt,
                    GObject                *selected,
                    GtkInspectorWindow     *iw)
{
  gtk_widget_set_sensitive (iw->object_details_button, selected != NULL);
  if (GTK_IS_WIDGET (selected))
    gtk_inspector_flash_widget (iw, GTK_WIDGET (selected));
}

static void
close_object_details (GtkWidget *button, GtkInspectorWindow *iw)
{
  gtk_stack_set_visible_child_name (GTK_STACK (iw->object_stack), "object-tree");
  gtk_stack_set_visible_child_name (GTK_STACK (iw->object_buttons), "list");
}

static void
open_object_details (GtkWidget *button, GtkInspectorWindow *iw)
{
  GObject *selected;

  selected = gtk_inspector_object_tree_get_selected (GTK_INSPECTOR_OBJECT_TREE (iw->object_tree));
 
  if (!set_selected_object (iw, selected))
    return;

  gtk_stack_set_visible_child_name (GTK_STACK (iw->object_stack), "object-details");
  gtk_stack_set_visible_child_name (GTK_STACK (iw->object_buttons), "details");
}

static gboolean
translate_visible_child_name (GBinding     *binding,
                              const GValue *from,
                              GValue       *to,
                              gpointer      user_data)
{
  GtkInspectorWindow *iw = user_data;
  const char *name;

  name = g_value_get_string (from);

  if (gtk_stack_get_child_by_name (GTK_STACK (iw->object_start_stack), name))
    g_value_set_string (to, name);
  else
    g_value_set_string (to, "empty");

  return TRUE;
}

static void
gtk_inspector_window_init (GtkInspectorWindow *iw)
{
  GIOExtensionPoint *extension_point;
  GList *l, *extensions;

  gtk_widget_init_template (GTK_WIDGET (iw));

  g_object_bind_property_full (iw->object_details, "visible-child-name",
                               iw->object_start_stack, "visible-child-name",
                               G_BINDING_SYNC_CREATE,
                               translate_visible_child_name,
                               NULL,
                               iw,
                               NULL);

  gtk_window_group_add_window (gtk_window_group_new (), GTK_WINDOW (iw));

  extension_point = g_io_extension_point_lookup ("gtk-inspector-page");
  extensions = g_io_extension_point_get_extensions (extension_point);

  for (l = extensions; l != NULL; l = l->next)
    {
      GIOExtension *extension = l->data;
      GType type;
      GtkWidget *widget;
      const char *name;
      char *title;
      GtkWidget *button;
      gboolean use_picker;

      type = g_io_extension_get_type (extension);

      widget = g_object_new (type, NULL);

      iw->extra_pages = g_list_prepend (iw->extra_pages, widget);

      name = g_io_extension_get_name (extension);
      g_object_get (widget, "title", &title, NULL);

      if (g_object_class_find_property (G_OBJECT_GET_CLASS (widget), "use-picker"))
        g_object_get (widget, "use-picker", &use_picker, NULL);
      else
        use_picker = FALSE;

      if (use_picker)
        {
          button = gtk_button_new_from_icon_name ("find-location-symbolic");
          gtk_widget_set_focus_on_click (button, FALSE);
          gtk_widget_set_halign (button, GTK_ALIGN_START);
          gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
          g_signal_connect (button, "clicked",
                            G_CALLBACK (gtk_inspector_on_inspect), iw);
        }
      else
        button = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

      gtk_stack_add_titled (GTK_STACK (iw->top_stack), widget, name, title);
      gtk_stack_add_named (GTK_STACK (iw->button_stack), button, name);
      gtk_widget_show (widget);
      gtk_widget_show (button);

      g_free (title);
    }
}

static void
gtk_inspector_window_constructed (GObject *object)
{
  GtkInspectorWindow *iw = GTK_INSPECTOR_WINDOW (object);

  G_OBJECT_CLASS (gtk_inspector_window_parent_class)->constructed (object);

  g_object_set_data (G_OBJECT (gdk_display_get_default ()), "-gtk-inspector", iw);

  gtk_inspector_object_tree_scan (GTK_INSPECTOR_OBJECT_TREE (iw->object_tree), NULL);
}

static void
object_details_changed (GtkWidget          *combo,
                        GParamSpec         *pspec,
                        GtkInspectorWindow *iw)
{
  gtk_stack_set_visible_child_name (GTK_STACK (iw->object_center_stack), "title");
}

static void
gtk_inspector_window_realize (GtkWidget *widget)
{
  GskRenderer *renderer;

  GTK_WIDGET_CLASS (gtk_inspector_window_parent_class)->realize (widget);

  renderer = gtk_window_get_renderer (GTK_WINDOW (widget));
  gsk_renderer_set_debug_flags (renderer, 0);
}

static void
gtk_inspector_window_class_init (GtkInspectorWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = gtk_inspector_window_constructed;
  widget_class->realize = gtk_inspector_window_realize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gtk/libgtk/inspector/window.ui");

  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, top_stack);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, button_stack);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, object_stack);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, object_tree);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, object_details);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, object_start_stack);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, object_center_stack);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, object_buttons);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, object_details_button);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, select_object);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, prop_list);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, child_prop_list);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, signals_list);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, widget_css_node_tree);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, widget_recorder);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, object_hierarchy);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, object_title);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, selector);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, size_groups);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, data_list);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, actions);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, menu);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, misc_info);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, gestures);
  gtk_widget_class_bind_template_child (widget_class, GtkInspectorWindow, magnifier);

  gtk_widget_class_bind_template_callback (widget_class, gtk_inspector_on_inspect);
  gtk_widget_class_bind_template_callback (widget_class, on_object_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_object_selected);
  gtk_widget_class_bind_template_callback (widget_class, open_object_details);
  gtk_widget_class_bind_template_callback (widget_class, close_object_details);
  gtk_widget_class_bind_template_callback (widget_class, object_details_changed);
}

static GdkDisplay *
get_inspector_display (void)
{
  static GdkDisplay *display = NULL;

  if (display == NULL)
    {
      const gchar *name;

      name = g_getenv ("GTK_INSPECTOR_DISPLAY");
      display = gdk_display_open (name);

      if (display)
        g_debug ("Using display %s for GtkInspector", name);
      else
        g_message ("Failed to open display %s", name);
    }

  if (!display)
    {
      display = gdk_display_open (NULL);
      if (display)
        g_debug ("Using default display for GtkInspector");
      else
        g_message ("Failed to separate connection to default display");
    }


  if (display)
    {
      const gchar *name;

      name = g_getenv ("GTK_INSPECTOR_RENDERER");

      g_object_set_data_full (G_OBJECT (display), "gsk-renderer",
                              g_strdup (name), g_free);

      gdk_display_set_debug_flags (display, 0);
      gtk_set_display_debug_flags (display, 0);
    }

  if (!display)
    display = gdk_display_get_default ();

  return display;
}

GtkWidget *
gtk_inspector_window_new (void)
{
  return GTK_WIDGET (g_object_new (GTK_TYPE_INSPECTOR_WINDOW,
                                   "display", get_inspector_display (),
                                   NULL));
}

void
gtk_inspector_window_rescan (GtkWidget *widget)
{
  GtkInspectorWindow *iw = GTK_INSPECTOR_WINDOW (widget);

  gtk_inspector_object_tree_scan (GTK_INSPECTOR_OBJECT_TREE (iw->object_tree), NULL);
}

static GtkInspectorWindow *
gtk_inspector_window_get_for_display (GdkDisplay *display)
{
  return g_object_get_data (G_OBJECT (display), "-gtk-inspector");
}

void
gtk_inspector_record_render (GtkWidget            *widget,
                             GskRenderer          *renderer,
                             GdkSurface           *surface,
                             const cairo_region_t *region,
                             GdkDrawingContext    *context,
                             GskRenderNode        *node)
{
  GtkInspectorWindow *iw;

  iw = gtk_inspector_window_get_for_display (gtk_widget_get_display (widget));
  if (iw == NULL)
    return;

  /* sanity check for single-display GDK backends */
  if (GTK_WIDGET (iw) == widget)
    return;

  gtk_inspector_recorder_record_render (GTK_INSPECTOR_RECORDER (iw->widget_recorder),
                                        widget,
                                        renderer,
                                        surface,
                                        region,
                                        context,
                                        node);
}

gboolean
gtk_inspector_is_recording (GtkWidget *widget)
{
  GtkInspectorWindow *iw;

  iw = gtk_inspector_window_get_for_display (gtk_widget_get_display (widget));
  if (iw == NULL)
    return FALSE;

  /* sanity check for single-display GDK backends */
  if (GTK_WIDGET (iw) == widget)
    return FALSE;

  return gtk_inspector_recorder_is_recording (GTK_INSPECTOR_RECORDER (iw->widget_recorder));
}

// vim: set et sw=2 ts=2:
