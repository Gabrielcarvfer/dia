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

#include <glib/gi18n-lib.h>

#include "object.h"
#include "group.h"
#include "properties.h"
#include "register-objects.h"

/* --- UML shared enum data -----------------------------------------------
 * The UML set's registration file objects/UML/uml.c is excluded (its
 * dia_plugin_init would clash with custom.c's), but it also defines these two
 * PropEnumData arrays referenced by several UML objects (association.c,
 * umloperation.c, umlattribute.c). Provide them here. Keep in sync with uml.h
 * (DiaUmlVisibility / DiaUmlInheritanceType). */
PropEnumData _uml_visibilities[] = {
  { N_("Public"), 0 /* DIA_UML_PUBLIC */ },
  { N_("Private"), 1 /* DIA_UML_PRIVATE */ },
  { N_("Protected"), 2 /* DIA_UML_PROTECTED */ },
  { N_("Implementation"), 3 /* DIA_UML_IMPLEMENTATION */ },
  { NULL, 0 }
};

PropEnumData _uml_inheritances[] = {
  { N_("Abstract"), 0 /* DIA_UML_ABSTRACT */ },
  { N_("Polymorphic (virtual)"), 1 /* DIA_UML_POLYMORPHIC */ },
  { N_("Leaf (final)"), 2 /* DIA_UML_LEAF */ },
  { NULL, 0 }
};

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

/* UML relationship (connection) objects */
extern DiaObjectType association_type, dependency_type, generalization_type;
extern DiaObjectType realizes_type, implements_type, constraint_type;
extern DiaObjectType message_type, uml_transition_type;

/* UML element objects */
extern DiaObjectType actor_type, usecase_type, node_type, component_type;
extern DiaObjectType compfeat_type, note_type, smallpackage_type, largepackage_type;
extern DiaObjectType branch_type, fork_type, lifeline_type, state_type;
extern DiaObjectType state_term_type, activity_type, objet_type, umlobject_type;
extern DiaObjectType classicon_type;

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

  /* UML relationship (connection) objects */
  object_register_type (&association_type);
  object_register_type (&dependency_type);
  object_register_type (&generalization_type);
  object_register_type (&realizes_type);
  object_register_type (&implements_type);
  object_register_type (&constraint_type);
  object_register_type (&message_type);
  object_register_type (&uml_transition_type);

  /* UML element objects */
  object_register_type (&actor_type);
  object_register_type (&usecase_type);
  object_register_type (&node_type);
  object_register_type (&component_type);
  object_register_type (&compfeat_type);
  object_register_type (&note_type);
  object_register_type (&smallpackage_type);
  object_register_type (&largepackage_type);
  object_register_type (&branch_type);
  object_register_type (&fork_type);
  object_register_type (&lifeline_type);
  object_register_type (&state_type);
  object_register_type (&state_term_type);
  object_register_type (&activity_type);
  object_register_type (&objet_type);
  object_register_type (&umlobject_type);
  object_register_type (&classicon_type);

  object_register_type (&group_type);   /* so groups can be saved/loaded */
}


/* --- custom shapes (.shape files) -------------------------------------- */

/* custom.c exports this (no header); loads one .shape file into a new type. */
extern gboolean custom_object_load (char *filename, DiaObjectType **otype);

/* Loading all ~780 .shape files up front costs ~10s at startup, so we load
 * lazily per category: at startup we only SCAN the directory tree (cheap),
 * recording each category's .shape file paths; the shapes in a category are
 * custom_object_load()ed + registered the first time the category is opened. */
static GHashTable *shape_files = NULL;   /* category -> GPtrArray<char* path> */
static GHashTable *shape_types = NULL;   /* category -> GPtrArray<char* name> */

static void
scan_shapes_in (const char *dir, const char *category)
{
  GDir *dp = g_dir_open (dir, 0, NULL);
  const char *name;

  if (!dp) {
    return;
  }
  while ((name = g_dir_read_name (dp)) != NULL) {
    char *path = g_build_filename (dir, name, NULL);

    if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
      scan_shapes_in (path, category);     /* recurse; keep the top category */
      g_free (path);
    } else if (g_str_has_suffix (name, ".shape")) {
      GPtrArray *arr = g_hash_table_lookup (shape_files, category);

      if (!arr) {
        arr = g_ptr_array_new_with_free_func (g_free);
        g_hash_table_insert (shape_files, g_strdup (category), arr);
      }
      g_ptr_array_add (arr, path);   /* takes ownership of path */
    } else {
      g_free (path);
    }
  }
  g_dir_close (dp);
}

void
dia_port_load_shapes (void)
{
  GDir *dp;
  const char *name;

  if (shape_files) {
    return;   /* scan once */
  }
  shape_files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                       (GDestroyNotify) g_ptr_array_unref);
  shape_types = g_hash_table_new (g_str_hash, g_str_equal);

  dp = g_dir_open (DIA_SHAPES_DIR, 0, NULL);
  if (!dp) {
    return;
  }
  /* each top-level subdirectory of shapes/ is a category (a "sheet"). */
  while ((name = g_dir_read_name (dp)) != NULL) {
    char *path = g_build_filename (DIA_SHAPES_DIR, name, NULL);

    if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
      scan_shapes_in (path, name);
    }
    g_free (path);
  }
  g_dir_close (dp);
}

GList *
dia_port_shape_categories (void)
{
  if (!shape_files) {
    return NULL;
  }
  return g_list_sort (g_hash_table_get_keys (shape_files),
                      (GCompareFunc) g_strcmp0);
}

GPtrArray *
dia_port_shapes_in_category (const char *category)
{
  GPtrArray *types, *paths;

  if (!shape_types) {
    return NULL;
  }
  types = g_hash_table_lookup (shape_types, category);
  if (types) {
    return types;   /* already loaded */
  }
  /* first access: load + register every shape in this category */
  types = g_ptr_array_new ();
  g_hash_table_insert (shape_types, g_strdup (category), types);
  paths = g_hash_table_lookup (shape_files, category);
  for (guint i = 0; paths && i < paths->len; i++) {
    DiaObjectType *ot = NULL;

    if (custom_object_load (g_ptr_array_index (paths, i), &ot) && ot != NULL) {
      object_register_type (ot);
      g_ptr_array_add (types, ot->name);
    }
  }
  return types;
}
