/* GTK - The GIMP Toolkit
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gtkcolorplaneprivate.h"

#include "gtkaccessible.h"
#include "gtkadjustment.h"
#include "gtkcolorutils.h"
#include "gtkgesturedrag.h"
#include "gtkgesturelongpress.h"
#include "gtkintl.h"
#include "gtksnapshot.h"
#include "gtkprivate.h"

struct _GtkColorPlanePrivate
{
  GtkAdjustment *h_adj;
  GtkAdjustment *s_adj;
  GtkAdjustment *v_adj;

  GdkTexture *texture;

  GtkGesture *drag_gesture;
  GtkGesture *long_press_gesture;
};

enum {
  PROP_0,
  PROP_H_ADJUSTMENT,
  PROP_S_ADJUSTMENT,
  PROP_V_ADJUSTMENT
};

G_DEFINE_TYPE_WITH_PRIVATE (GtkColorPlane, gtk_color_plane, GTK_TYPE_WIDGET)

static void
sv_to_xy (GtkColorPlane *plane,
          gint          *x,
          gint          *y)
{
  gdouble s, v;
  gint width, height;

  width = gtk_widget_get_width (GTK_WIDGET (plane));
  height = gtk_widget_get_height (GTK_WIDGET (plane));

  s = gtk_adjustment_get_value (plane->priv->s_adj);
  v = gtk_adjustment_get_value (plane->priv->v_adj);

  *x = CLAMP (width * v, 0, width - 1);
  *y = CLAMP (height * (1 - s), 0, height - 1);
}

static void
plane_snapshot (GtkWidget   *widget,
                GtkSnapshot *snapshot)
{
  GtkColorPlane *plane = GTK_COLOR_PLANE (widget);
  gint x, y;
  gint width, height;
  cairo_t *cr;

  sv_to_xy (plane, &x, &y);
  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  gtk_snapshot_append_texture (snapshot,
                               plane->priv->texture,
                               &GRAPHENE_RECT_INIT (0, 0, width, height),
                               "ColorPlane");
  cr = gtk_snapshot_append_cairo (snapshot,
                                  &GRAPHENE_RECT_INIT (0, 0, width, height),
                                  "ColorPlane");

  cairo_move_to (cr, 0,     y + 0.5);
  cairo_line_to (cr, width, y + 0.5);

  cairo_move_to (cr, x + 0.5, 0);
  cairo_line_to (cr, x + 0.5, height);

  if (gtk_widget_has_visible_focus (widget))
    {
      cairo_set_line_width (cr, 3.0);
      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.6);
      cairo_stroke_preserve (cr);

      cairo_set_line_width (cr, 1.0);
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.8);
      cairo_stroke (cr);
    }
  else
    {
      cairo_set_line_width (cr, 1.0);
      cairo_set_source_rgba (cr, 0.8, 0.8, 0.8, 0.8);
      cairo_stroke (cr);
    }

  cairo_destroy (cr);
}

static void
create_texture (GtkColorPlane *plane)
{
  GtkWidget *widget = GTK_WIDGET (plane);
  GBytes *bytes;
  gint width, height, stride;
  guint red, green, blue;
  guint32 *data, *p;
  gdouble h, s, v;
  gdouble r, g, b;
  gdouble sf, vf;
  gint x, y;

  if (!gtk_widget_get_realized (widget))
    return;

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  g_clear_object (&plane->priv->texture);

  stride = width * 4;

  data = g_malloc (height * stride);

  if (width > 1 && height > 1)
    {
      h = gtk_adjustment_get_value (plane->priv->h_adj);
      sf = 1.0 / (height - 1);
      vf = 1.0 / (width - 1);
      for (y = 0; y < height; y++)
        {
          s = CLAMP (1.0 - y * sf, 0.0, 1.0);
          p = data + y * (stride / 4);
          for (x = 0; x < width; x++)
            {
              v = x * vf;
              gtk_hsv_to_rgb (h, s, v, &r, &g, &b);
              red = CLAMP (r * 255, 0, 255);
              green = CLAMP (g * 255, 0, 255);
              blue = CLAMP (b * 255, 0, 255);
              p[x] = (0xff << 24) | (red << 16) | (green << 8) | blue;
            }
        }
    }
  else
    {
      memset (data, 0, height * stride);
    }

  bytes = g_bytes_new_take (data, height * stride);
  plane->priv->texture = gdk_memory_texture_new (width, height,
                                                 GDK_MEMORY_DEFAULT,
                                                 bytes,
                                                 stride);
  g_bytes_unref (bytes);
}

static void
plane_size_allocate (GtkWidget           *widget,
                     const GtkAllocation *allocation,
                     int                   baseline,
                     GtkAllocation        *out_clip)
{
  GtkColorPlane *plane = GTK_COLOR_PLANE (widget);

  create_texture (plane);
}

static void
plane_realize (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (gtk_color_plane_parent_class)->realize (widget);

  create_texture (GTK_COLOR_PLANE (widget));
}

static void
plane_unrealize (GtkWidget *widget)
{
  GtkColorPlane *plane = GTK_COLOR_PLANE (widget);
  GtkColorPlanePrivate *priv = gtk_color_plane_get_instance_private (plane);

  g_clear_object (&priv->texture);

  GTK_WIDGET_CLASS (gtk_color_plane_parent_class)->unrealize (widget);
}

static void
set_cross_cursor (GtkWidget *widget,
                  gboolean   enabled)
{
  if (enabled)
    gtk_widget_set_cursor_from_name (widget, "crosshair");
  else
    gtk_widget_set_cursor (widget, NULL);
}

static void
h_changed (GtkColorPlane *plane)
{
  create_texture (plane);
  gtk_widget_queue_draw (GTK_WIDGET (plane));
}

static void
sv_changed (GtkColorPlane *plane)
{
  gtk_widget_queue_draw (GTK_WIDGET (plane));
}

static void
update_color (GtkColorPlane *plane,
              gint           x,
              gint           y)
{
  GtkWidget *widget = GTK_WIDGET (plane);
  gdouble s, v;

  s = CLAMP (1 - y * (1.0 / gtk_widget_get_height (widget)), 0, 1);
  v = CLAMP (x * (1.0 / gtk_widget_get_width (widget)), 0, 1);
  gtk_adjustment_set_value (plane->priv->s_adj, s);
  gtk_adjustment_set_value (plane->priv->v_adj, v);

  gtk_widget_queue_draw (widget);
}

static void
hold_action (GtkGestureLongPress *gesture,
             gdouble              x,
             gdouble              y,
             GtkColorPlane       *plane)
{
  gboolean handled;

  g_signal_emit_by_name (plane, "popup-menu", &handled);
}

static void
sv_move (GtkColorPlane *plane,
         gdouble        ds,
         gdouble        dv)
{
  gdouble s, v;

  s = gtk_adjustment_get_value (plane->priv->s_adj);
  v = gtk_adjustment_get_value (plane->priv->v_adj);

  if (s + ds > 1)
    {
      if (s < 1)
        s = 1;
      else
        goto error;
    }
  else if (s + ds < 0)
    {
      if (s > 0)
        s = 0;
      else
        goto error;
    }
  else
    {
      s += ds;
    }

  if (v + dv > 1)
    {
      if (v < 1)
        v = 1;
      else
        goto error;
    }
  else if (v + dv < 0)
    {
      if (v > 0)
        v = 0;
      else
        goto error;
    }
  else
    {
      v += dv;
    }

  gtk_adjustment_set_value (plane->priv->s_adj, s);
  gtk_adjustment_set_value (plane->priv->v_adj, v);
  return;

error:
  gtk_widget_error_bell (GTK_WIDGET (plane));
}

static gboolean
plane_key_press (GtkWidget   *widget,
                 GdkEventKey *event)
{
  GtkColorPlane *plane = GTK_COLOR_PLANE (widget);
  gdouble step;
  guint keyval, state;

  if (!gdk_event_get_keyval ((GdkEvent *) event, &keyval) ||
      !gdk_event_get_state ((GdkEvent *) event, &state))
    return GDK_EVENT_PROPAGATE;

  if ((state & GDK_MOD1_MASK) != 0)
    step = 0.1;
  else
    step = 0.01;

  if (keyval == GDK_KEY_Up ||
      keyval == GDK_KEY_KP_Up)
    sv_move (plane, step, 0);
  else if (keyval == GDK_KEY_Down ||
           keyval == GDK_KEY_KP_Down)
    sv_move (plane, -step, 0);
  else if (keyval == GDK_KEY_Left ||
           keyval == GDK_KEY_KP_Left)
    sv_move (plane, 0, -step);
  else if (keyval == GDK_KEY_Right ||
           keyval == GDK_KEY_KP_Right)
    sv_move (plane, 0, step);
  else
    return GTK_WIDGET_CLASS (gtk_color_plane_parent_class)->key_press_event (widget, event);

  return TRUE;
}

static void
plane_drag_gesture_begin (GtkGestureDrag *gesture,
                          gdouble         start_x,
                          gdouble         start_y,
                          GtkColorPlane  *plane)
{
  guint button;

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

  if (button == GDK_BUTTON_SECONDARY)
    {
      gboolean handled;

      g_signal_emit_by_name (plane, "popup-menu", &handled);
    }

  if (button != GDK_BUTTON_PRIMARY)
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
      return;
    }

  set_cross_cursor (GTK_WIDGET (plane), TRUE);
  update_color (plane, start_x, start_y);
  gtk_widget_grab_focus (GTK_WIDGET (plane));
  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
plane_drag_gesture_update (GtkGestureDrag *gesture,
                           gdouble         offset_x,
                           gdouble         offset_y,
                           GtkColorPlane  *plane)
{
  gdouble start_x, start_y;

  gtk_gesture_drag_get_start_point (GTK_GESTURE_DRAG (gesture),
                                    &start_x, &start_y);
  update_color (plane, start_x + offset_x, start_y + offset_y);
}

static void
plane_drag_gesture_end (GtkGestureDrag *gesture,
                        gdouble         offset_x,
                        gdouble         offset_y,
                        GtkColorPlane  *plane)
{
  set_cross_cursor (GTK_WIDGET (plane), FALSE);
}

static void
gtk_color_plane_init (GtkColorPlane *plane)
{
  AtkObject *atk_obj;

  plane->priv = gtk_color_plane_get_instance_private (plane);

  gtk_widget_set_has_surface (GTK_WIDGET (plane), FALSE);
  gtk_widget_set_can_focus (GTK_WIDGET (plane), TRUE);

  atk_obj = gtk_widget_get_accessible (GTK_WIDGET (plane));
  if (GTK_IS_ACCESSIBLE (atk_obj))
    {
      atk_object_set_name (atk_obj, _("Color Plane"));
      atk_object_set_role (atk_obj, ATK_ROLE_COLOR_CHOOSER);
    }

  plane->priv->drag_gesture = gtk_gesture_drag_new (GTK_WIDGET (plane));
  g_signal_connect (plane->priv->drag_gesture, "drag-begin",
		    G_CALLBACK (plane_drag_gesture_begin), plane);
  g_signal_connect (plane->priv->drag_gesture, "drag-update",
		    G_CALLBACK (plane_drag_gesture_update), plane);
  g_signal_connect (plane->priv->drag_gesture, "drag-end",
		    G_CALLBACK (plane_drag_gesture_end), plane);
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (plane->priv->drag_gesture), 0);

  plane->priv->long_press_gesture = gtk_gesture_long_press_new (GTK_WIDGET (plane));
  g_signal_connect (plane->priv->long_press_gesture, "pressed",
                    G_CALLBACK (hold_action), plane);
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (plane->priv->long_press_gesture),
                                     TRUE);
}

static void
plane_finalize (GObject *object)
{
  GtkColorPlane *plane = GTK_COLOR_PLANE (object);

  g_clear_object (&plane->priv->texture);

  g_clear_object (&plane->priv->h_adj);
  g_clear_object (&plane->priv->s_adj);
  g_clear_object (&plane->priv->v_adj);

  g_clear_object (&plane->priv->drag_gesture);
  g_clear_object (&plane->priv->long_press_gesture);

  G_OBJECT_CLASS (gtk_color_plane_parent_class)->finalize (object);
}

static void
plane_set_property (GObject      *object,
		    guint         prop_id,
		    const GValue *value,
		    GParamSpec   *pspec)
{
  GtkColorPlane *plane = GTK_COLOR_PLANE (object);
  GtkAdjustment *adjustment;

  /* Construct only properties can only be set once, these are created
   * only in order to be properly buildable from gtkcoloreditor.ui
   */
  switch (prop_id)
    {
    case PROP_H_ADJUSTMENT:
      adjustment = g_value_get_object (value);
      if (adjustment)
	{
	  plane->priv->h_adj = g_object_ref_sink (adjustment);
	  g_signal_connect_swapped (adjustment, "value-changed", G_CALLBACK (h_changed), plane);
	}
      break;
    case PROP_S_ADJUSTMENT:
      adjustment = g_value_get_object (value);
      if (adjustment)
	{
	  plane->priv->s_adj = g_object_ref_sink (adjustment);
	  g_signal_connect_swapped (adjustment, "value-changed", G_CALLBACK (sv_changed), plane);
	}
      break;
    case PROP_V_ADJUSTMENT:
      adjustment = g_value_get_object (value);
      if (adjustment)
	{
	  plane->priv->v_adj = g_object_ref_sink (adjustment);
	  g_signal_connect_swapped (adjustment, "value-changed", G_CALLBACK (sv_changed), plane);
	}
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_color_plane_class_init (GtkColorPlaneClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  object_class->finalize = plane_finalize;
  object_class->set_property = plane_set_property;

  widget_class->snapshot = plane_snapshot;
  widget_class->size_allocate = plane_size_allocate;
  widget_class->realize = plane_realize;
  widget_class->unrealize = plane_unrealize;
  widget_class->key_press_event = plane_key_press;

  g_object_class_install_property (object_class,
                                   PROP_H_ADJUSTMENT,
                                   g_param_spec_object ("h-adjustment",
                                                        "Hue Adjustment",
                                                        "Hue Adjustment",
							GTK_TYPE_ADJUSTMENT,
							GTK_PARAM_WRITABLE |
							G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_S_ADJUSTMENT,
                                   g_param_spec_object ("s-adjustment",
                                                        "Saturation Adjustment",
                                                        "Saturation Adjustment",
							GTK_TYPE_ADJUSTMENT,
							GTK_PARAM_WRITABLE |
							G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_V_ADJUSTMENT,
                                   g_param_spec_object ("v-adjustment",
                                                        "Value Adjustment",
                                                        "Value Adjustment",
							GTK_TYPE_ADJUSTMENT,
							GTK_PARAM_WRITABLE |
							G_PARAM_CONSTRUCT_ONLY));
}

GtkWidget *
gtk_color_plane_new (GtkAdjustment *h_adj,
                     GtkAdjustment *s_adj,
                     GtkAdjustment *v_adj)
{
  return g_object_new (GTK_TYPE_COLOR_PLANE,
                       "h-adjustment", h_adj,
                       "s-adjustment", s_adj,
                       "v-adjustment", v_adj,
                       NULL);
}
