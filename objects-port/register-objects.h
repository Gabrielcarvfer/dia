/* Dia -- GTK4 + libadwaita port
 *
 * Shared registration of the statically-linked object types (objects-port), so
 * both the skeleton app and the port's object tests register the same set.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

/* Register the standard + flowchart/network/ER object types with libdia.
 * Call once, after libdia_init(). */
void dia_port_register_objects (void);

/* Load and register every .shape file under the bundled shapes/ tree, grouped
 * by top-level subdirectory (category). Call once, after register. */
void dia_port_load_shapes (void);

/* Sorted list of shape-category names (caller g_list_free()s the list, not the
 * strings). NULL before dia_port_load_shapes(). */
GList *dia_port_shape_categories (void);

/* The registered shape type-names in a category (owned; do not free). */
GPtrArray *dia_port_shapes_in_category (const char *category);
