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

#ifndef _GRL_MEDIA_SOURCE_H_
#define _GRL_MEDIA_SOURCE_H_

#include <grl-media-plugin.h>
#include <grl-metadata-source.h>
#include <grl-data.h>
#include <grl-media-box.h>
#include <grl-definitions.h>

#include <glib.h>
#include <glib-object.h>

/* Macros */

#define GRL_TYPE_MEDIA_SOURCE                   \
  (grl_media_source_get_type ())

#define GRL_MEDIA_SOURCE(obj)                           \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                   \
                               GRL_TYPE_MEDIA_SOURCE,   \
                               GrlMediaSource))

#define GRL_IS_MEDIA_SOURCE(obj)                        \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                   \
                               GRL_TYPE_MEDIA_SOURCE))

#define GRL_MEDIA_SOURCE_CLASS(klass)                   \
  (G_TYPE_CHECK_CLASS_CAST((klass),                     \
                           GRL_TYPE_MEDIA_SOURCE,       \
                           GrlMediaSourceClass))

#define GRL_IS_MEDIA_SOURCE_CLASS(klass)                \
  (G_TYPE_CHECK_CLASS_TYPE((klass),                     \
                           GRL_TYPE_MEDIA_SOURCE))

#define GRL_MEDIA_SOURCE_GET_CLASS(obj)                         \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                            \
                              GRL_TYPE_MEDIA_SOURCE,            \
                              GrlMediaSourceClass))
/**
 * GrlMediaSourceChangeType:
 * @GRL_CONTENT_CHANGED: content has changed. It is used when any property of
 * #GrlMedia has changed, or in case of #GrlMediaBox, if several children have
 * been added and removed.
 * @GRL_CONTENT_ADDED: new content has been added.
 * @GRL_CONTENT_REMOVED: content has been removed
 *
 * Specifies which kind of change has happened in the plugin
 */
typedef enum {
  GRL_CONTENT_CHANGED,
  GRL_CONTENT_ADDED,
  GRL_CONTENT_REMOVED
} GrlMediaSourceChangeType;

/* GrlMediaSource object */

typedef struct _GrlMediaSource        GrlMediaSource;
typedef struct _GrlMediaSourcePrivate GrlMediaSourcePrivate;

struct _GrlMediaSource {

  GrlMetadataSource parent;

  /*< private >*/
  GrlMediaSourcePrivate *priv;

  gpointer _grl_reserved[GRL_PADDING];
};

/* Callbacks for GrlMediaSource class */

/**
 * GrlMediaSourceResultCb:
 * @source: a media source
 * @operation_id: operation identifier
 * @media: (transfer full): a data transfer object
 * @remaining: the number of remaining #GrlMedia to process, or
 * GRL_SOURCE_REMAINING_UNKNOWN if it is unknown
 * @user_data: user data passed to the used method
 * @error: (type uint): possible #GError generated at processing
 *
 * Prototype for the callback passed to the media sources' methods
 */
typedef void (*GrlMediaSourceResultCb) (GrlMediaSource *source,
                                        guint operation_id,
                                        GrlMedia *media,
                                        guint remaining,
                                        gpointer user_data,
                                        const GError *error);

/**
 * GrlMediaSourceMetadataCb:
 * @source: a media source
 * @operation_id: operation identifier
 * @media: (transfer full): a data transfer object
 * @user_data: user data passed to grl_media_source_metadata()
 * @error: (type uint): possible #GError generated at processing
 *
 * Prototype for the callback passed to grl_media_source_metadata()
 */
typedef void (*GrlMediaSourceMetadataCb) (GrlMediaSource *source,
                                          guint operation_id,
                                          GrlMedia *media,
                                          gpointer user_data,
                                          const GError *error);

/**
 * GrlMediaSourceStoreCb:
 * @source: a media source
 * @parent: The #GrlMediaBox who parents the @media
 * @media: (transfer full): a data transfer object
 * @user_data: user data passed to grl_media_source_store()
 * @error: (type uint): possible #GError generated at processing
 *
 * Prototype for the callback passed to grl_media_source_store()
 */
typedef void (*GrlMediaSourceStoreCb) (GrlMediaSource *source,
                                       GrlMediaBox *parent,
                                       GrlMedia *media,
                                       gpointer user_data,
                                       const GError *error);

/**
 * GrlMediaSourceRemoveCb:
 * @source: a media source
 * @media: (transfer full): a data transfer object
 * @user_data: user data passed to grl_media_source_remove()
 * @error: (type uint): possible #GError generated at processing
 *
 * Prototype for the callback passed to grl_media_source_remove()
 */
typedef void (*GrlMediaSourceRemoveCb) (GrlMediaSource *source,
                                        GrlMedia *media,
                                        gpointer user_data,
                                        const GError *error);

/* Types for MediaSourceClass */

/**
 * GrlMediaSourceBrowseSpec:
 * @source: a media source
 * @browse_id: operation identifier
 * @container: a container of data transfer objects
 * @keys: the #GList of #GrlKeyID<!-- -->s to request
 * @skip: the number if elements to skip in the browse operation
 * @count: the number of elements to retrieve in the browse operation
 * @flags: the resolution mode
 * @callback: the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Data transport structure used internally by the plugins which support
 * browse vmethod.
 */
typedef struct {
  GrlMediaSource *source;
  guint browse_id;
  GrlMedia *container;
  GList *keys;
  guint skip;
  guint count;
  GrlMetadataResolutionFlags flags;
  GrlMediaSourceResultCb callback;
  gpointer user_data;

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING];
} GrlMediaSourceBrowseSpec;

/**
 * GrlMediaSourceSearchSpec:
 * @source: a media source
 * @search_id: operation identifier
 * @text: the text to search
 * @keys: the #GList of #GrlKeyID<!-- -->s to request
 * @skip: the number if elements to skip in the browse operation
 * @count: the number of elements to retrieve in the browse operation
 * @flags: the resolution mode
 * @callback: the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Data transport structure used internally by the plugins which support
 * search vmethod.
 */
typedef struct {
  GrlMediaSource *source;
  guint search_id;
  gchar *text;
  GList *keys;
  guint skip;
  guint count;
  GrlMetadataResolutionFlags flags;
  GrlMediaSourceResultCb callback;
  gpointer user_data;

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING];
} GrlMediaSourceSearchSpec;

/**
 * GrlMediaSourceQuerySpec:
 * @source: a media source
 * @query_id: operation identifier
 * @query: the query to process
 * @keys: the #GList of #GrlKeyID<!-- -->s to request
 * @skip: the number if elements to skip in the browse operation
 * @count: the number of elements to retrieve in the browse operation
 * @flags: the resolution mode
 * @callback: the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Data transport structure used internally by the plugins which support
 * query vmethod.
 */
typedef struct {
  GrlMediaSource *source;
  guint query_id;
  gchar *query;
  GList *keys;
  guint skip;
  guint count;
  GrlMetadataResolutionFlags flags;
  GrlMediaSourceResultCb callback;
  gpointer user_data;

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING];
} GrlMediaSourceQuerySpec;

/**
 * GrlMediaSourceMetadataSpec:
 * @source: a media source
 * @metadata_id: operation identifier
 * @media: a data transfer object
 * @keys: the #GList of #GrlKeyID<!-- -->s to request
 * @flags: the resolution mode
 * @callback: the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Data transport structure used internally by the plugins which support
 * metadata vmethod.
 */
typedef struct {
  GrlMediaSource *source;
  guint metadata_id;
  GrlMedia *media;
  GList *keys;
  GrlMetadataResolutionFlags flags;
  GrlMediaSourceMetadataCb callback;
  gpointer user_data;

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING];
} GrlMediaSourceMetadataSpec;

/**
 * GrlMediaSourceStoreSpec:
 * @source: a media source
 * @parent: a parent to store the data transfer objects
 * @media: a data transfer object
 * @callback: the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Data transport structure used internally by the plugins which support
 * store vmethod.
 */
typedef struct {
  GrlMediaSource *source;
  GrlMediaBox *parent;
  GrlMedia *media;
  GrlMediaSourceStoreCb callback;
  gpointer user_data;

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING];
} GrlMediaSourceStoreSpec;

/**
 * GrlMediaSourceRemoveSpec:
 * @source: a media source
 * @media_id: media identifier to remove
 * @media: a data transfer object
 * @callback: the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Data transport structure used internally by the plugins which support
 * store vmethod.
 */
typedef struct {
  GrlMediaSource *source;
  gchar *media_id;
  GrlMedia *media;
  GrlMediaSourceRemoveCb callback;
  gpointer user_data;

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING];
} GrlMediaSourceRemoveSpec;

/**
 * GrlMediaSourceMediaFromUriSpec:
 * @source: a media source
 * @media_from_uri_id: operation identifier
 * @uri: A URI that can be used to identify a media resource
 * @keys: Metadata keys to resolve
 * @flags: Operation flags
 * @callback: the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Data transport structure used internally by the plugins which support
 * media_from_uri vmethod.
 */
typedef struct {
  GrlMediaSource *source;
  guint media_from_uri_id;
  gchar *uri;
  GList *keys;
  GrlMetadataResolutionFlags flags;
  GrlMediaSourceMetadataCb callback;
  gpointer user_data;

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING - 1];
} GrlMediaSourceMediaFromUriSpec;


/* GrlMediaSource class */

typedef struct _GrlMediaSourceClass GrlMediaSourceClass;

/**
 * GrlMediaSourceClass:
 * @parent_class: the parent class structure
 * @browse: browse through a list of media
 * @search: search for media
 * @query: query for a specific media
 * @cancel: cancel the current operation
 * @metadata: request for specific metadata
 * @store: store a media in a container
 * @remove: remove a media from a container
 * @test_media_from_uri: tests if this source can create #GrlMedia
 * instances from a given URI.
 * @media_from_uri: Creates a #GrlMedia instance representing the media
 * exposed by a certain URI.
 * @notify_change_start: start emitting signals about changes in content
 * @notify_change_stop: stop emitting signals about changes in content
 *
 * Grilo MediaSource class. Override the vmethods to implement the
 * source functionality.
 */
struct _GrlMediaSourceClass {

  GrlMetadataSourceClass parent_class;

  void (*browse) (GrlMediaSource *source, GrlMediaSourceBrowseSpec *bs);

  void (*search) (GrlMediaSource *source, GrlMediaSourceSearchSpec *ss);

  void (*query) (GrlMediaSource *source, GrlMediaSourceQuerySpec *qs);

  void (*cancel) (GrlMediaSource *source, guint operation_id);

  void (*metadata) (GrlMediaSource *source, GrlMediaSourceMetadataSpec *ms);

  void (*store) (GrlMediaSource *source, GrlMediaSourceStoreSpec *ss);

  void (*remove) (GrlMediaSource *source, GrlMediaSourceRemoveSpec *ss);

  gboolean (*test_media_from_uri) (GrlMediaSource *source,
				   const gchar *uri);

  void (*media_from_uri) (GrlMediaSource *source,
			  GrlMediaSourceMediaFromUriSpec *mfss);

  gboolean (*notify_change_start) (GrlMediaSource *source,
                                    GError **error);

  gboolean (*notify_change_stop) (GrlMediaSource *source,
                                  GError **error);

  /*< private >*/
  gpointer _grl_reserved[GRL_PADDING - 1];
};

G_BEGIN_DECLS

GType grl_media_source_get_type (void);

guint grl_media_source_browse (GrlMediaSource *source,
                               GrlMedia *container,
                               const GList *keys,
                               guint skip,
                               guint count,
                               GrlMetadataResolutionFlags flags,
                               GrlMediaSourceResultCb callback,
                               gpointer user_data);

GList *grl_media_source_browse_sync (GrlMediaSource *source,
                                     GrlMedia *container,
                                     const GList *keys,
                                     guint skip,
                                     guint count,
                                     GrlMetadataResolutionFlags flags,
                                     GError **error);

guint grl_media_source_search (GrlMediaSource *source,
                               const gchar *text,
                               const GList *keys,
                               guint skip,
                               guint count,
                               GrlMetadataResolutionFlags flags,
                               GrlMediaSourceResultCb callback,
                               gpointer user_data);

GList *grl_media_source_search_sync (GrlMediaSource *source,
                                     const gchar *text,
                                     const GList *keys,
                                     guint skip,
                                     guint count,
                                     GrlMetadataResolutionFlags flags,
                                     GError **error);

guint grl_media_source_query (GrlMediaSource *source,
                              const gchar *query,
                              const GList *keys,
                              guint skip,
                              guint count,
                              GrlMetadataResolutionFlags flags,
                              GrlMediaSourceResultCb callback,
                              gpointer user_data);

GList *grl_media_source_query_sync (GrlMediaSource *source,
                                    const gchar *query,
                                    const GList *keys,
                                    guint skip,
                                    guint count,
                                    GrlMetadataResolutionFlags flags,
                                    GError **error);

guint grl_media_source_metadata (GrlMediaSource *source,
                                 GrlMedia *media,
                                 const GList *keys,
                                 GrlMetadataResolutionFlags flags,
                                 GrlMediaSourceMetadataCb callback,
                                 gpointer user_data);

GrlMedia *grl_media_source_metadata_sync (GrlMediaSource *source,
                                          GrlMedia *media,
                                          const GList *keys,
                                          GrlMetadataResolutionFlags flags,
                                          GError **error);

void grl_media_source_store (GrlMediaSource *source,
                             GrlMediaBox *parent,
                             GrlMedia *media,
                             GrlMediaSourceStoreCb callback,
                             gpointer user_data);

void grl_media_source_store_sync (GrlMediaSource *source,
                                  GrlMediaBox *parent,
                                  GrlMedia *media,
                                  GError **error);

void grl_media_source_remove (GrlMediaSource *source,
                              GrlMedia *media,
                              GrlMediaSourceRemoveCb callback,
                              gpointer user_data);

void grl_media_source_remove_sync (GrlMediaSource *source,
                                   GrlMedia *media,
                                   GError **error);


G_GNUC_DEPRECATED void grl_media_source_cancel (GrlMediaSource *source, guint operation_id);

G_GNUC_DEPRECATED void grl_media_source_set_operation_data (GrlMediaSource *source,
                                                            guint operation_id,
                                                            gpointer data);

G_GNUC_DEPRECATED gpointer grl_media_source_get_operation_data (GrlMediaSource *source,
                                                                guint operation_id);

void grl_media_source_set_auto_split_threshold (GrlMediaSource *source,
                                                guint threshold);

guint grl_media_source_get_auto_split_threshold (GrlMediaSource *source);

gboolean grl_media_source_test_media_from_uri (GrlMediaSource *source,
					       const gchar *uri);

guint grl_media_source_get_media_from_uri (GrlMediaSource *source,
                                           const gchar *uri,
                                           const GList *keys,
                                           GrlMetadataResolutionFlags flags,
                                           GrlMediaSourceMetadataCb callback,
                                           gpointer user_data);

GrlMedia *grl_media_source_get_media_from_uri_sync (GrlMediaSource *source,
                                                    const gchar *uri,
                                                    const GList *keys,
                                                    GrlMetadataResolutionFlags flags,
                                                    GError **error);

gboolean grl_media_source_notify_change_start (GrlMediaSource *source,
                                               GError **error);

gboolean grl_media_source_notify_change_stop (GrlMediaSource *source,
                                              GError **error);

void grl_media_source_notify_change_list (GrlMediaSource *source,
                                          GPtrArray *changed_medias,
                                          GrlMediaSourceChangeType change_type,
                                          gboolean location_unknown);

void grl_media_source_notify_change (GrlMediaSource *source,
                                     GrlMedia *media,
                                     GrlMediaSourceChangeType change_type,
                                     gboolean location_unknown);

G_END_DECLS

#endif /* _GRL_MEDIA_SOURCE_H_ */
