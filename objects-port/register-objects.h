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
