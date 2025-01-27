/*
 * Copyright (C) 2010, 2011 Igalia S.L.
 *
 * Contact: Iago Toral Quiroga <itoral@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#if !defined (_GRILO_H_INSIDE_) && !defined (GRILO_COMPILATION)
#error "Only <grilo.h> can be included directly."
#endif

#ifndef _GRL_MEDIA_PLUGIN_H_
#define _GRL_MEDIA_PLUGIN_H_

#include <glib.h>
#include <glib-object.h>

#include "grl-definitions.h"

/* Info */

#define GRL_MEDIA_PLUGIN_NAME "name"
#define GRL_MEDIA_PLUGIN_DESCRIPTION "description"
#define GRL_MEDIA_PLUGIN_VERSION "version"
#define GRL_MEDIA_PLUGIN_LICENSE "license"
#define GRL_MEDIA_PLUGIN_AUTHOR "author"
#define GRL_MEDIA_PLUGIN_SITE "site"

/* Macros */

#define GRL_TYPE_MEDIA_PLUGIN                   \
  (grl_media_plugin_get_type ())

#define GRL_MEDIA_PLUGIN(obj)                           \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                   \
                               GRL_TYPE_MEDIA_PLUGIN,   \
                               GrlMediaPlugin))

#define GRL_IS_MEDIA_PLUGIN(obj)                        \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                   \
                               GRL_TYPE_MEDIA_PLUGIN))

#define GRL_MEDIA_PLUGIN_CLASS(klass)                   \
  (G_TYPE_CHECK_CLASS_CAST((klass),                     \
                           GRL_TYPE_MEDIA_PLUGIN,       \
                           GrlMediaPluginClass))

#define GRL_IS_MEDIA_PLUGIN_CLASS(klass)                \
  (G_TYPE_CHECK_CLASS_TYPE((klass),                     \
                           GRL_TYPE_MEDIA_PLUGIN))

#define GRL_MEDIA_PLUGIN_GET_CLASS(obj)                 \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                    \
                              GRL_TYPE_MEDIA_PLUGIN,    \
                              GrlMediaPluginClass))

/* GrlMediaPlugin object */

typedef struct _GrlMediaPlugin        GrlMediaPlugin;
typedef struct _GrlMediaPluginPrivate GrlMediaPluginPrivate;

struct _GrlMediaPlugin {

  GObject parent;

  /*< private >*/
  GrlMediaPluginPrivate *priv;

  gpointer _grl_reserved[GRL_PADDING];
};

/* GrlMediaPlugin class */

typedef struct _GrlMediaPluginClass GrlMediaPluginClass;

/**
 * GrlMediaPluginClass:
 * @parent_class: the parent class structure
 */
struct _GrlMediaPluginClass {

  GObjectClass parent_class;

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING];
};

/* Function prototypes */

G_BEGIN_DECLS

GType grl_media_plugin_get_type (void);

const gchar *grl_media_plugin_get_name (GrlMediaPlugin *plugin);

const gchar *grl_media_plugin_get_description (GrlMediaPlugin *plugin);

const gchar *grl_media_plugin_get_version (GrlMediaPlugin *plugin);

const gchar *grl_media_plugin_get_license (GrlMediaPlugin *plugin);

const gchar *grl_media_plugin_get_author (GrlMediaPlugin *plugin);

const gchar *grl_media_plugin_get_site (GrlMediaPlugin *plugin);

const gchar *grl_media_plugin_get_id (GrlMediaPlugin *plugin);

const gchar *grl_media_plugin_get_filename (GrlMediaPlugin *plugin);

gint grl_media_plugin_get_rank (GrlMediaPlugin *plugin);

GList *grl_media_plugin_get_info_keys (GrlMediaPlugin *plugin);

const gchar *grl_media_plugin_get_info (GrlMediaPlugin *plugin,
                                        const gchar *key);

G_END_DECLS

#endif /* _GRL_MEDIA_PLUGIN_H_ */
