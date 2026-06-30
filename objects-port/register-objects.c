/* Dia -- GTK4 + libadwaita port
 *
 * Registers the object types compiled into libdia-objects. The standard set
 * exposes pointer symbols (_box_type, …); the extra sets (flowchart/network/ER)
 * expose the type structs directly (their per-set dia_plugin_init files are not
 * linked, to avoid a clash), so we register &fc_box_type etc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include "object.h"
#include "register-objects.h"

/* standard */
extern DiaObjectType *_box_type;
extern DiaObjectType *_ellipse_type;
extern DiaObjectType *_line_type;
extern DiaObjectType *_textobj_type;
extern DiaObjectType *_polygon_type;
extern DiaObjectType *_beziergon_type;
extern DiaObjectType *_zigzagline_type;
extern DiaObjectType *_polyline_type;
extern DiaObjectType *_bezierline_type;
extern DiaObjectType *_arc_type;
extern DiaObjectType *_image_type;
extern DiaObjectType *_outline_type;

/* flowchart / network / ER */
extern DiaObjectType fc_box_type, diamond_type, fc_ellipse_type, pgram_type;
extern DiaObjectType basestation_type, bus_type, radiocell_type, wanlink_type;
extern DiaObjectType attribute_type, entity_type, participation_type, relationship_type;

void
dia_port_register_objects (void)
{
  object_register_type (_box_type);
  object_register_type (_ellipse_type);
  object_register_type (_line_type);
  object_register_type (_textobj_type);
  object_register_type (_polygon_type);
  object_register_type (_beziergon_type);
  object_register_type (_zigzagline_type);
  object_register_type (_polyline_type);
  object_register_type (_bezierline_type);
  object_register_type (_arc_type);
  object_register_type (_image_type);
  object_register_type (_outline_type);

  object_register_type (&fc_box_type);
  object_register_type (&diamond_type);
  object_register_type (&fc_ellipse_type);
  object_register_type (&pgram_type);

  object_register_type (&basestation_type);
  object_register_type (&bus_type);
  object_register_type (&radiocell_type);
  object_register_type (&wanlink_type);

  object_register_type (&attribute_type);
  object_register_type (&entity_type);
  object_register_type (&participation_type);
  object_register_type (&relationship_type);
}
