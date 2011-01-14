/*
 * Copyright (C) 2010 Igalia S.L.
 *
 * Contact: Iago Toral Quiroga <itoral@igalia.com>
 *
 * Authors: Juan A. Suarez Romero <jasuarez@igalia.com>
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

#include <glib.h>
#include <glib-object.h>

#include <grl-definitions.h>

#ifndef _GRL_CONFIG_H_
#define _GRL_CONFIG_H_

G_BEGIN_DECLS

#define GRL_TYPE_CONFIG                         \
  (grl_config_get_type())

#define GRL_CONFIG(obj)                         \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),           \
                               GRL_TYPE_CONFIG, \
                               GrlConfig))

#define GRL_CONFIG_CLASS(klass)                 \
  (G_TYPE_CHECK_CLASS_CAST ((klass),            \
                            GRL_TYPE_CONFIG,    \
                            GrlConfigClass))

#define GRL_IS_CONFIG(obj)                              \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                   \
                               GRL_TYPE_CONFIG))

#define GRL_IS_CONFIG_CLASS(klass)              \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),            \
                            GRL_TYPE_CONFIG))

#define GRL_CONFIG_GET_CLASS(obj)               \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),            \
                              GRL_TYPE_CONFIG,  \
                              GrlConfigClass))

#define GRL_CONFIG_KEY_PLUGIN      "target-plugin"
#define GRL_CONFIG_KEY_SOURCE      "target-source"
#define GRL_CONFIG_KEY_APIKEY      "api-key"
#define GRL_CONFIG_KEY_APITOKEN    "api-token"
#define GRL_CONFIG_KEY_APISECRET   "api-secret"

typedef struct _GrlConfig        GrlConfig;
typedef struct _GrlConfigPrivate GrlConfigPrivate;
typedef struct _GrlConfigClass   GrlConfigClass;

/**
 * GrlConfigClass:
 * @parent_class: the parent class structure
 *
 * Grilo Config Class
 */
struct _GrlConfigClass
{
  GObjectClass parent_class;

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING];
};

struct _GrlConfig
{
  GObject parent;

  /*< private >*/
  GrlConfigPrivate *priv;

  gpointer _grl_reserved[GRL_PADDING_SMALL];
};

void grl_config_set_plugin (GrlConfig *config, const gchar *plugin);

void grl_config_set_source (GrlConfig *config, const gchar *source);

void grl_config_set_api_key (GrlConfig *config, const gchar *key);

void grl_config_set_api_token (GrlConfig *config, const gchar *token);

void grl_config_set_api_secret (GrlConfig *config, const gchar *secret);

const gchar *grl_config_get_plugin (GrlConfig *config);

const gchar *grl_config_get_api_key (GrlConfig *config);

const gchar *grl_config_get_api_token (GrlConfig *config);

const gchar *grl_config_get_api_secret (GrlConfig *config);

GType grl_config_get_type (void) G_GNUC_CONST;
GrlConfig *grl_config_new (const gchar *plugin, const gchar *source);

void grl_config_set (GrlConfig *config, const gchar *param, const GValue *value);

void grl_config_set_string (GrlConfig *config,
			    const gchar *param,
			    const gchar *value);

void grl_config_set_int (GrlConfig *config, const gchar *param, gint value);

void grl_config_set_float (GrlConfig *config, const gchar *param, gfloat value);

void grl_config_set_boolean (GrlConfig *config, const gchar *param, gboolean value);

const GValue *grl_config_get (GrlConfig *config, const gchar *param);

const gchar *grl_config_get_string (GrlConfig *config, const gchar *param);

gint grl_config_get_int (GrlConfig *config, const gchar *param);

gfloat grl_config_get_float (GrlConfig *config, const gchar *param);

gboolean grl_config_get_boolean (GrlConfig *config, const gchar *param);


G_END_DECLS

#endif /* _GRL_CONFIG_H_ */
