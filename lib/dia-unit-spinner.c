/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dia-unit-spinner.h"

/**
 * DiaUnitSpinner:
 *
 * A Spinner that allows a 'favoured' unit to display in. External access
 * to the value still happens in cm, but display is in the favored unit.
 * Internally, the value is kept in the favored unit to a) allow proper
 * limits, and b) avoid rounding problems while editing.
 *
 * Since: dawn-of-time
 */
struct _DiaUnitSpinner {
  GtkWidget parent_instance;

  GtkSpinButton *spin;
  DiaUnit        unit_num;
};

G_DEFINE_FINAL_TYPE (DiaUnitSpinner, dia_unit_spinner, GTK_TYPE_WIDGET)


static void
dia_unit_spinner_dispose (GObject *object)
{
  DiaUnitSpinner *self = DIA_UNIT_SPINNER (object);

  g_clear_pointer ((GtkWidget **) &self->spin, gtk_widget_unparent);

  G_OBJECT_CLASS (dia_unit_spinner_parent_class)->dispose (object);
}


static void
dia_unit_spinner_class_init (DiaUnitSpinnerClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = dia_unit_spinner_dispose;

  /* Single child filling the whole widget. */
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}


/*
  Callback functions for the "input" and "output" signals emitted by the
  embedded GtkSpinButton. All the normal work is done by the spin button, we
  simply modify how the text in its entry is treated.
*/


static int
dia_unit_spinner_input (GtkSpinButton  *spin,
                        double         *value,
                        DiaUnitSpinner *self)
{
  double val, factor = 1.0;
  char *extra = NULL;

  val = g_strtod (gtk_editable_get_text (GTK_EDITABLE (spin)), &extra);

  /* get rid of extra white space after number */
  while (*extra && g_ascii_isspace (*extra)) {
    extra++;
  }

  if (*extra) {
    for (int i = 0; i < DIA_LAST_UNIT; i++) {
      if (!g_ascii_strcasecmp (dia_unit_get_symbol (i), extra)) {
        factor = dia_unit_get_factor (i) /
                  dia_unit_get_factor (self->unit_num);
        break;
      }
    }
  }

  /* convert to preferred units */
  val *= factor;

  /* Store value in the location provided by the signal emitter. */
  *value = val;

  /* Return true, so that the default input function is not invoked. */
  return TRUE;
}


static gboolean
dia_unit_spinner_output (GtkSpinButton  *spin,
                         DiaUnitSpinner *self)
{
  char buf[256];
  GtkAdjustment *adjustment = gtk_spin_button_get_adjustment (spin);

  g_snprintf (buf,
              sizeof(buf),
              "%0.*f %s",
              gtk_spin_button_get_digits(spin),
              gtk_adjustment_get_value(adjustment),
              dia_unit_get_symbol (self->unit_num));
  gtk_editable_set_text (GTK_EDITABLE (spin), buf);

  /* Return true, so that the default output function is not invoked. */
  return TRUE;
}


static void
dia_unit_spinner_init (DiaUnitSpinner *self)
{
  self->unit_num = DIA_UNIT_CENTIMETER;

  self->spin = GTK_SPIN_BUTTON (gtk_spin_button_new (NULL, 0.0, 0));
  gtk_widget_set_parent (GTK_WIDGET (self->spin), GTK_WIDGET (self));

  g_signal_connect (self->spin, "output",
                    G_CALLBACK (dia_unit_spinner_output),
                    self);
  g_signal_connect (self->spin, "input",
                    G_CALLBACK (dia_unit_spinner_input),
                    self);
}


GtkWidget *
dia_unit_spinner_new (GtkAdjustment *adjustment, DiaUnit adj_unit)
{
  DiaUnitSpinner *self;

  if (adjustment) {
    g_return_val_if_fail (GTK_IS_ADJUSTMENT (adjustment), NULL);
  }

  self = g_object_new (DIA_TYPE_UNIT_SPINNER, NULL);
  self->unit_num = adj_unit;

  gtk_spin_button_set_activates_default (self->spin, TRUE);
  gtk_spin_button_configure (self->spin,
                             adjustment, 0.0, dia_unit_get_digits (adj_unit));

  return GTK_WIDGET (self);
}


/**
 * dia_unit_spinner_set_value:
 * @self: the DiaUnitSpinner
 * @val: value in %DIA_UNIT_CENTIMETER
 *
 * Set the value (in cm).
 *
 * Since: dawn-of-time
 */
void
dia_unit_spinner_set_value (DiaUnitSpinner *self, double val)
{
  GtkSpinButton *sbutton = self->spin;

  gtk_spin_button_set_value (sbutton,
                             val /
                             (dia_unit_get_factor (self->unit_num) /
                              (dia_unit_get_factor (DIA_UNIT_CENTIMETER))));
}


/**
 * dia_unit_spinner_get_value:
 * @self: the DiaUnitSpinner
 *
 * Get the value (in cm)
 *
 * Returns: The value in %DIA_UNIT_CENTIMETER
 *
 * Since: dawn-of-time
 */
double
dia_unit_spinner_get_value (DiaUnitSpinner *self)
{
  GtkSpinButton *sbutton = self->spin;

  return gtk_spin_button_get_value (sbutton) *
                    (dia_unit_get_factor (self->unit_num) /
                     dia_unit_get_factor (DIA_UNIT_CENTIMETER));
}


/**
 * dia_unit_spinner_set_upper:
 * @self: the DiaUnitSpinner
 * @val: value in %DIA_UNIT_CENTIMETER
 *
 * Must manipulate the limit values through this to also consider unit.
 *
 * Given value is in centimeter.
 *
 * Since: dawn-of-time
 */
void
dia_unit_spinner_set_upper (DiaUnitSpinner *self, double val)
{
  val /= (dia_unit_get_factor (self->unit_num) /
          dia_unit_get_factor (DIA_UNIT_CENTIMETER));

  gtk_adjustment_set_upper (
    gtk_spin_button_get_adjustment (self->spin), val);
}

