/*
 * Copyright (C) 2010-2012 Igalia S.L.
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

/**
 * SECTION:grl-media-source
 * @short_description: Abstract class for media providers
 * @see_also: #GrlMediaPlugin, #GrlMetadataSource, #GrlMedia
 *
 * GrlMediaSource is the abstract base class needed to construct a
 * source of media data.
 *
 * The media sources fetch media data descriptors and store them
 * in data transfer objects represented as #GrlMedia.
 *
 * There are several methods to retrieve the media, such as searching
 * a text expression, crafting a specific query, etc. And most of those
 * methods are asynchronous.
 *
 * Examples of media sources are #GrlYoutubeSource, #GrlJamendoSource,
 * etc.
 */

#include "grl-media-source.h"
#include "grl-metadata-source-priv.h"
#include "grl-operation.h"
#include "grl-operation-priv.h"
#include "grl-sync-priv.h"
#include "data/grl-media.h"
#include "data/grl-media-box.h"
#include "grl-error.h"
#include "grl-log.h"

#include "grl-marshal.h"
#include "grl-type-builtins.h"

#include <string.h>

#define GRL_LOG_DOMAIN_DEFAULT  media_source_log_domain
GRL_LOG_DOMAIN(media_source_log_domain);

#define GRL_MEDIA_SOURCE_GET_PRIVATE(object)            \
  (G_TYPE_INSTANCE_GET_PRIVATE((object),                \
                               GRL_TYPE_MEDIA_SOURCE,   \
                               GrlMediaSourcePrivate))

enum {
  PROP_0,
  PROP_AUTO_SPLIT_THRESHOLD
};

struct _GrlMediaSourcePrivate {
  guint auto_split_threshold;
};

struct SortedResult {
  GrlMedia *media;
  guint remaining;
};

struct FullResolutionCtlCb {
  GrlMediaSourceResultCb user_callback;
  gpointer user_data;
  GList *keys;
  GrlMetadataResolutionFlags flags;
  gboolean chained;
  GList *next_index;
  GList *waiting_list;
};

struct FullResolutionDoneCb {
  GHashTable *pending_callbacks;
  gboolean cancelled;
  GrlMediaSource *source;
  guint browse_id;
  guint remaining;
  struct FullResolutionCtlCb *ctl_info;
};

struct AutoSplitCtl {
  gboolean chunk_first;
  guint chunk_requested;
  guint chunk_consumed;
  guint threshold;
  guint count;
};

struct BrowseRelayCb {
  GrlMediaSourceResultCb user_callback;
  gpointer user_data;
  gboolean use_idle;
  GrlMediaSourceBrowseSpec *bspec;
  GrlMediaSourceSearchSpec *sspec;
  GrlMediaSourceQuerySpec *qspec;
  gboolean chained;
  struct AutoSplitCtl *auto_split;
};

struct BrowseRelayIdle {
  GrlMediaSourceResultCb user_callback;
  gpointer user_data;
  GrlMediaSource *source;
  guint browse_id;
  GrlMedia *media;
  guint remaining;
  GError *error;
  gboolean chained;
};

struct MetadataFullResolutionCtlCb {
  GrlMediaSourceMetadataCb user_callback;
  gpointer user_data;
  GList *keys;
  GrlMetadataResolutionFlags flags;
  guint metadata_id;
};

struct MetadataFullResolutionDoneCb {
  GrlMediaSourceMetadataCb user_callback;
  gpointer user_data;
  GHashTable *pending_callbacks;
  gboolean cancelled;
  GrlMediaSource *source;
  struct MetadataFullResolutionCtlCb *ctl_info;;
};

struct MetadataRelayCb {
  GrlMediaSourceMetadataCb user_callback;
  gpointer user_data;
  GrlMediaSourceMetadataSpec *spec;
};

struct MediaFromUriRelayCb {
  GrlMediaSourceMetadataCb user_callback;
  gpointer user_data;
  GrlMediaSourceMediaFromUriSpec *spec;
};

static void grl_media_source_finalize (GObject *object);

static void grl_media_source_get_property (GObject *plugin,
                                           guint prop_id,
                                           GValue *value,
                                           GParamSpec *pspec);
static void grl_media_source_set_property (GObject *object,
                                           guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec);

static GrlSupportedOps
grl_media_source_supported_operations (GrlMetadataSource *metadata_source);

/* ================ GrlMediaSource GObject ================ */

enum {
  SIG_CONTENT_CHANGED,
  SIG_LAST
};
static gint registry_signals[SIG_LAST];

G_DEFINE_ABSTRACT_TYPE (GrlMediaSource,
                        grl_media_source,
                        GRL_TYPE_METADATA_SOURCE);

static void
grl_media_source_class_init (GrlMediaSourceClass *media_source_class)
{
  GObjectClass *gobject_class;
  GrlMetadataSourceClass *metadata_source_class;

  gobject_class = G_OBJECT_CLASS (media_source_class);
  metadata_source_class = GRL_METADATA_SOURCE_CLASS (media_source_class);

  gobject_class->finalize = grl_media_source_finalize;
  gobject_class->set_property = grl_media_source_set_property;
  gobject_class->get_property = grl_media_source_get_property;

  metadata_source_class->supported_operations =
    grl_media_source_supported_operations;

  g_type_class_add_private (media_source_class,
                            sizeof (GrlMediaSourcePrivate));

  /**
   * GrlMediaSource:auto-split-threshold
   *
   * Transparently split queries with count requests
   * bigger than a certain threshold into smaller queries.
   */
  g_object_class_install_property (gobject_class,
				   PROP_AUTO_SPLIT_THRESHOLD,
				   g_param_spec_uint ("auto-split-threshold",
						      "Auto-split threshold",
						      "Threshold to use auto-split of queries",
						      0, G_MAXUINT, 0,
						      G_PARAM_READWRITE |
						      G_PARAM_STATIC_STRINGS));
  /**
   * GrlMediaSource::content-changed:
   * @source: source that has changed
   * @changed_medias: a #GPtrArray with the medias that changed or a common
   * ancestor of them of type #GrlMediaBox.
   * @change_type: the kind of change that ocurred
   * @location_unknown: @TRUE if the change happened in @media itself or in one
   * of its direct children (when @media is a #GrlMediaBox). @FALSE otherwise
   *
   * Signals that the content in the source has changed. @changed_medias is the
   * list of elements that have changed. Usually these medias are of type
   * #GrlMediaBox, meaning that the content of that box has changed.
   *
   * If @location_unknown is @TRUE it means the source cannot establish where the
   * change happened: could be either in the box, in any child, or in any other
   * descendant of the box in the hierarchy.
   *
   * Both @change_type and @location_unknown are applied to all elements in the
   * list.
   *
   * For the cases where the source can only signal that a change happened, but
   * not where, it would use a list with the the root box (@NULL id) and set
   * location_unknown as @TRUE.
   *
   * Since: 0.1.9
   */
  registry_signals[SIG_CONTENT_CHANGED] =
    g_signal_new("content-changed",
                 G_TYPE_FROM_CLASS (gobject_class),
                 G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                 0,
                 NULL,
                 NULL,
                 grl_marshal_VOID__BOXED_ENUM_BOOLEAN,
                 G_TYPE_NONE,
                 3,
                 G_TYPE_PTR_ARRAY,
                 GRL_TYPE_MEDIA_SOURCE_CHANGE_TYPE,
                 G_TYPE_BOOLEAN);
}

static void
grl_media_source_init (GrlMediaSource *source)
{
  source->priv = GRL_MEDIA_SOURCE_GET_PRIVATE (source);
}

static void
grl_media_source_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  GrlMediaSource *source;

  source = GRL_MEDIA_SOURCE (object);

  switch (prop_id) {
  case PROP_AUTO_SPLIT_THRESHOLD:
    g_value_set_uint (value, source->priv->auto_split_threshold);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (source, prop_id, pspec);
    break;
  }
}

static void
grl_media_source_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  GrlMediaSource *source;

  source = GRL_MEDIA_SOURCE (object);

  switch (prop_id) {
  case PROP_AUTO_SPLIT_THRESHOLD:
    source->priv->auto_split_threshold = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (source, prop_id, pspec);
    break;
  }
}

static void
grl_media_source_finalize (GObject *object)
{
  GRL_DEBUG ("grl_media_source_finalize");
  G_OBJECT_CLASS (grl_media_source_parent_class)->finalize (object);
}

/* ================ Utitilies ================ */

static void
free_browse_operation_spec (GrlMediaSourceBrowseSpec *spec)
{
  GRL_DEBUG ("free_browse_operation_spec");
  g_object_unref (spec->source);
  g_object_unref (spec->container);
  g_list_free (spec->keys);
  g_free (spec);
}

static void
free_search_operation_spec (GrlMediaSourceSearchSpec *spec)
{
  GRL_DEBUG ("free_search_operation_spec");
  g_object_unref (spec->source);
  g_free (spec->text);
  g_list_free (spec->keys);
  g_free (spec);
}

static void
free_query_operation_spec (GrlMediaSourceQuerySpec *spec)
{
  GRL_DEBUG ("free_query_operation_spec");
  g_object_unref (spec->source);
  g_free (spec->query);
  g_list_free (spec->keys);
  g_free (spec);
}

static gboolean
browse_idle (gpointer user_data)
{
  GRL_DEBUG ("browse_idle");
  GrlMediaSourceBrowseSpec *bs = (GrlMediaSourceBrowseSpec *) user_data;
  /* Check if operation was cancelled even before the idle kicked in */
  if (!grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (bs->source),
                                                   bs->browse_id)) {
    GRL_MEDIA_SOURCE_GET_CLASS (bs->source)->browse (bs->source, bs);
  } else {
    GError *error;
    GRL_DEBUG ("  operation was cancelled");
    error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                         "Operation was cancelled");
    /* Note: at this point, bs->callback should not be the user-provided
     * callback, but rather browse_result_relay_cb() */
    bs->callback (bs->source, bs->browse_id, NULL, 0, bs->user_data, error);
    g_error_free (error);
  }
  return FALSE;
}

static gboolean
search_idle (gpointer user_data)
{
  GRL_DEBUG ("search_idle");
  GrlMediaSourceSearchSpec *ss = (GrlMediaSourceSearchSpec *) user_data;
  /* Check if operation was cancelled even before the idle kicked in */
  if (!grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (ss->source),
                                                   ss->search_id)) {
    GRL_MEDIA_SOURCE_GET_CLASS (ss->source)->search (ss->source, ss);
  } else {
    GError *error;
    GRL_DEBUG ("  operation was cancelled");
    error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                         "Operation was cancelled");
    ss->callback (ss->source, ss->search_id, NULL, 0, ss->user_data, error);
    g_error_free (error);
  }
  return FALSE;
}

static gboolean
query_idle (gpointer user_data)
{
  GRL_DEBUG ("query_idle");
  GrlMediaSourceQuerySpec *qs = (GrlMediaSourceQuerySpec *) user_data;
  if (!grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (qs->source),
                                                   qs->query_id)) {
    GRL_MEDIA_SOURCE_GET_CLASS (qs->source)->query (qs->source, qs);
  } else {
    GError *error;
    GRL_DEBUG ("  operation was cancelled");
    error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                         "Operation was cancelled");
    qs->callback (qs->source, qs->query_id, NULL, 0, qs->user_data, error);
    g_error_free (error);
  }
  return FALSE;
}

static gboolean
metadata_idle (gpointer user_data)
{
  GRL_DEBUG ("metadata_idle");
  GrlMediaSourceMetadataSpec *ms = (GrlMediaSourceMetadataSpec *) user_data;
  if (!grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (ms->source),
                                                   ms->metadata_id)) {
    GRL_MEDIA_SOURCE_GET_CLASS (ms->source)->metadata (ms->source, ms);
  } else {
    GError *error;
    GRL_DEBUG ("  operation was cancelled");
    error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                         "Operation was cancelled");
    ms->callback (ms->source, ms->metadata_id, ms->media, ms->user_data, error);
    g_error_free (error);
  }
  return FALSE;
}

static void
store_idle_destroy (gpointer user_data)
{
  GrlMediaSourceStoreSpec *ss = (GrlMediaSourceStoreSpec *) user_data;
  g_object_unref (ss->source);
  if (ss->parent)
    g_object_unref (ss->parent);
  g_object_unref (ss->media);
  g_free (ss);
}

static gboolean
store_idle (gpointer user_data)
{
  GRL_DEBUG ("store_idle");
  GrlMediaSourceStoreSpec *ss = (GrlMediaSourceStoreSpec *) user_data;
  GRL_MEDIA_SOURCE_GET_CLASS (ss->source)->store (ss->source, ss);
  return FALSE;
}

static void
remove_idle_destroy (gpointer user_data)
{
  GrlMediaSourceRemoveSpec *rs = (GrlMediaSourceRemoveSpec *) user_data;
  g_object_unref (rs->source);
  g_free (rs->media_id);
  g_free (rs);
}

static gboolean
remove_idle (gpointer user_data)
{
  GRL_DEBUG ("remove_idle");
  GrlMediaSourceRemoveSpec *rs = (GrlMediaSourceRemoveSpec *) user_data;
  GRL_MEDIA_SOURCE_GET_CLASS (rs->source)->remove (rs->source, rs);
  return FALSE;
}

static void
media_from_uri_relay_cb (GrlMediaSource *source,
                         guint media_from_uri_id,
			 GrlMedia *media,
			 gpointer user_data,
			 const GError *error)
{
  gboolean should_free_error = FALSE;
  GError *_error = (GError *) error;
  GRL_DEBUG ("media_from_uri_relay_cb");

  struct MediaFromUriRelayCb *mfsrc;

  mfsrc = (struct MediaFromUriRelayCb *) user_data;
  if (media) {
    grl_media_set_source (media,
                          grl_metadata_source_get_id (GRL_METADATA_SOURCE (source)));
  }

  if (grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (source),
                                                  mfsrc->spec->media_from_uri_id)) {
    /* if the plugin already set an error, we don't care because we're
     * cancelled */
    _error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                          "Operation was cancelled");
    /* yet, we should free the error we just created (if we didn't create it,
     * the plugin owns it) */
    should_free_error = TRUE;

    /* As it was cancelled, there shouldn't be a media; so free it */
    if (media) {
      g_object_unref (media);
      media = NULL;
    }
  }

  mfsrc->user_callback (source, mfsrc->spec->media_from_uri_id,
                        media, mfsrc->user_data, _error);

  if (should_free_error && _error) {
    g_error_free (_error);
  }

  g_object_unref (mfsrc->spec->source);
  g_free (mfsrc->spec->uri);
  g_list_free (mfsrc->spec->keys);
  g_free (mfsrc->spec);
  g_free (mfsrc);
}

static gboolean
media_from_uri_idle (gpointer user_data)
{
  GRL_DEBUG ("media_from_uri_idle");
  GrlMediaSourceMediaFromUriSpec *mfus =
    (GrlMediaSourceMediaFromUriSpec *) user_data;
  if (!grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (mfus->source),
                                                   mfus->media_from_uri_id)) {
    GRL_MEDIA_SOURCE_GET_CLASS (mfus->source)->media_from_uri (mfus->source,
                                                               mfus);
  } else {
    GError *error;
    GRL_DEBUG ("  operation was cancelled");
    error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                         "Operation was cancelled");
    mfus->callback (mfus->source, mfus->media_from_uri_id, NULL, mfus->user_data, error);
    g_error_free (error);
  }
  return FALSE;
}

static gboolean
browse_result_relay_idle (gpointer user_data)
{
  GRL_DEBUG ("browse_result_relay_idle");

  struct BrowseRelayIdle *bri = (struct BrowseRelayIdle *) user_data;
  gboolean cancelled = FALSE;

  /* Check if operation was cancelled (could be cancelled between the relay
     callback and this idle loop iteration). Remember that we do
     emit the last result (remaining == 0) in any case. */
  if (grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (bri->source),
                                                  bri->browse_id)) {
    if (bri->media) {
      g_object_unref (bri->media);
      bri->media = NULL;
    }
    cancelled = TRUE;
  }
  if (!cancelled || bri->remaining == 0) {
    if (cancelled) {
      /* Last callback call for a cancelled operation, the cancelled error
       * takes precedence, because the caller shouldn't care about other errors
       * if it called _cancel(). */
      if (bri->error)
        g_error_free (bri->error);
      bri->error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                           "Operation was cancelled");
    }
    bri->user_callback (bri->source,
			bri->browse_id,
			bri->media,
			bri->remaining,
			bri->user_data,
			bri->error);
  } else {
    GRL_DEBUG ("operation was cancelled, skipping idle result!");
  }

  if (bri->remaining == 0 && !bri->chained) {
    /* This is the last post-processing callback, so we can remove
       the operation state data here */
    grl_metadata_source_set_operation_finished (GRL_METADATA_SOURCE (bri->source),
                                                bri->browse_id);
  }

  /* We copy the error if we do idle relay or might have created one above in
   * some cancellation cases, we have to free it here */
  if (bri->error) {
    g_error_free (bri->error);
  }

  g_free (bri);

  return FALSE;
}

static void
auto_split_run_next_chunk (struct BrowseRelayCb *brc, guint remaining)
{
  struct AutoSplitCtl *as_info = brc->auto_split;
  guint *skip = NULL;
  guint *count = NULL;
  GSourceFunc operation = NULL;
  gpointer spec = NULL;

  /* Identify the operation we are handling */
  if (brc->bspec) {
    spec = brc->bspec;
    skip = &brc->bspec->skip;
    count = &brc->bspec->count;
    operation = browse_idle;
  } else if (brc->sspec) {
    spec = brc->sspec;
    skip = &brc->sspec->skip;
    count = &brc->sspec->count;
    operation = search_idle;
  } else if (brc->qspec) {
    spec = brc->qspec;
    skip = &brc->qspec->skip;
    count = &brc->qspec->count;
    operation = query_idle;
  }

  /* Go for next chunk */
  *skip += as_info->chunk_requested;
  as_info->chunk_first = TRUE;
  as_info->chunk_consumed = 0;
  if (remaining < as_info->threshold) {
    as_info->chunk_requested = remaining;
  }
  *count = as_info->chunk_requested;
  GRL_DEBUG ("auto-split: requesting next chunk (skip=%u, count=%u)",
             *skip, *count);
  g_idle_add (operation, spec);
}

static void
browse_result_relay_cb (GrlMediaSource *source,
			guint browse_id,
			GrlMedia *media,
			guint remaining,
			gpointer user_data,
			const GError *error)
{
  struct BrowseRelayCb *brc;
  guint plugin_remaining = remaining;

  GRL_DEBUG ("browse_result_relay_cb, op:%u, source:%s, remaining:%u",
             browse_id,
             grl_metadata_source_get_name (GRL_METADATA_SOURCE (source)),
             remaining);

  brc = (struct BrowseRelayCb *) user_data;

  /* --- operation cancel management --- */

  /* Check if operation is still valid , otherwise do not emit the result
     but make sure to free the operation data when remaining is 0 */
  if (!grl_metadata_source_operation_is_ongoing (GRL_METADATA_SOURCE (source),
                                                 browse_id)) {
    GRL_DEBUG ("operation is cancelled or already finished, skipping result!");
    if (media) {
      g_object_unref (media);
      media = NULL;
    }
    if (brc->auto_split) {
      /* Stop auto-split, of course */
      g_free (brc->auto_split);
      brc->auto_split = NULL;
    }
    if (remaining > 0) {
      return;
    }
    if (grl_metadata_source_operation_is_completed (GRL_METADATA_SOURCE (source),
                                                    browse_id)) {
      /* If the operation was cancelled, we ignore all results until
	 we get the last one, which we let through so all chained callbacks
	 have the chance to free their resources. If the operation is already
	 completed (includes finished) however, we already let the last
	 result through and doing it again would cause a crash */
      GRL_WARNING ("Source '%s' emitted 'remaining=0' more than once for "
                   "operation %d",
                   grl_metadata_source_get_name (GRL_METADATA_SOURCE (source)),
                   browse_id);
      return;
    }
    /* If we reached this point the operation is cancelled but not completed
       and this is the last result (remaining == 0) */
  }

  /* --- auto split management  --- */

  if (brc->auto_split) {
    struct AutoSplitCtl *as_info = brc->auto_split;
    /* Adjust remaining count if the plugin was not able to
       provide as many results as we requested */
    if (as_info->chunk_first) {
      if (plugin_remaining < as_info->chunk_requested - 1) {
	as_info->count = plugin_remaining + 1;
      }
      as_info->chunk_first = FALSE;
    }

    as_info->count--;
    as_info->chunk_consumed++;

    /* When auto split, if less results than what a chunk should give,
     * that means we've reached the end of the results. */
    if ((plugin_remaining == 0) &&
        (as_info->chunk_consumed < as_info->chunk_requested))
      remaining = 0;
    else
      remaining = as_info->count;
  }

  /* --- relay operation  --- */

  /* This is to prevent crash when plugins emit remaining=0 more than once */
  if (remaining == 0) {
    grl_metadata_source_set_operation_completed (GRL_METADATA_SOURCE (source),
                                                 browse_id);
  }

  if (media) {
    grl_media_set_source (media,
                          grl_metadata_source_get_id (GRL_METADATA_SOURCE (source)));
  }

  /* TODO: this should be TRUE if GRL_RESOLVE_FULL was requested too,
     after all GRL_RESOLVE_FULL already forces the idle loop before emission */
  if (brc->use_idle) {
    struct BrowseRelayIdle *bri = g_new (struct BrowseRelayIdle, 1);
    bri->source = source;
    bri->browse_id = browse_id;
    bri->media = media;
    bri->remaining = remaining;
    bri->error = (GError *) (error ? g_error_copy (error) : NULL);
    bri->user_callback = brc->user_callback;
    bri->user_data = brc->user_data;
    bri->chained = brc->chained;
    g_idle_add (browse_result_relay_idle, bri);
  } else {
    gboolean should_free_error = FALSE;
    GError *_error = (GError *)error;
    if (remaining == 0 &&
        grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (source),
                                                    browse_id)) {
      /* last callback call for a cancelled operation */
      /* if the plugin already set an error, we don't care because we're
       * cancelled */
      _error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                           "Operation was cancelled");
      /* Yet, we should free the error we just created (if we didn't create it,
       * the plugin owns it) */
      should_free_error = TRUE;
    }
    brc->user_callback (source,
			browse_id,
			media,
			remaining,
			brc->user_data,
			_error);
    if (should_free_error && _error)
      g_error_free (_error);

    if (remaining == 0 && !brc->chained) {
      /* This is the last post-processing callback, so we can remove
	 the operation state data here */
      grl_metadata_source_set_operation_finished (GRL_METADATA_SOURCE (source),
                                                  browse_id);
    }
  }

  /* --- auto split management --- */

  if (brc->auto_split) {
    if (plugin_remaining == 0 && remaining > 0) {
      auto_split_run_next_chunk (brc, remaining);
    }
  }

  /* --- free relay information  --- */

  /* Free callback data when we processed the last result */
  if (remaining == 0) {
    GRL_DEBUG ("Got remaining '0' for operation %d (%s)",
               browse_id,
               grl_metadata_source_get_name (GRL_METADATA_SOURCE (source)));
    if (brc->bspec) {
      free_browse_operation_spec (brc->bspec);
    } else if (brc->sspec) {
      free_search_operation_spec (brc->sspec);
    } else if (brc->sspec) {
      free_query_operation_spec (brc->qspec);
    }
    g_free (brc->auto_split);
    g_free (brc);
  }
}

static void
multiple_result_async_cb (GrlMediaSource *source,
                          guint op_id,
                          GrlMedia *media,
                          guint remaining,
                          gpointer user_data,
                          const GError *error)
{
  GrlDataSync *ds = (GrlDataSync *) user_data;

  GRL_DEBUG ("multiple_result_async_cb");

  if (error) {
    ds->error = g_error_copy (error);

    /* Free previous results */
    g_list_free_full (ds->data, g_object_unref);

    ds->data = NULL;
    ds->complete = TRUE;
    return;
  }

  if (media) {
    ds->data = g_list_prepend (ds->data, media);
  }

  if (remaining == 0) {
    ds->data = g_list_reverse (ds->data);
    ds->complete = TRUE;
  }
}

static void
metadata_result_relay_cb (GrlMediaSource *source,
                          guint metadata_id,
			  GrlMedia *media,
			  gpointer user_data,
			  const GError *error)
{
  gboolean should_free_error = FALSE;
  GError *_error = (GError *)error;
  GRL_DEBUG ("metadata_result_relay_cb");

  struct MetadataRelayCb *mrc;

  mrc = (struct MetadataRelayCb *) user_data;
  if (media) {
    grl_media_set_source (media,
                          grl_metadata_source_get_id (GRL_METADATA_SOURCE (source)));
  }

  if (grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (source),
                                                  mrc->spec->metadata_id)) {
    /* if the plugin already set an error, we don't care because we're
     * cancelled */
    _error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                          "Operation was cancelled");
    /* yet, we should free the error we just created (if we didn't create it,
     * the plugin owns it) */
    should_free_error = TRUE;
  }

  mrc->user_callback (source, mrc->spec->metadata_id, media, mrc->user_data, error);

  if (should_free_error && _error)
    g_error_free (_error);

  g_object_unref (mrc->spec->source);
  if (mrc->spec->media) {
    /* Can be NULL if getting metadata for root category */
    g_object_unref (mrc->spec->media);
  }
  g_list_free (mrc->spec->keys);
  g_free (mrc->spec);
  g_free (mrc);
}

static void
metadata_result_async_cb (GrlMediaSource *source,
                          guint operation_id,
                          GrlMedia *media,
                          gpointer user_data,
                          const GError *error)
{
  GrlDataSync *ds = (GrlDataSync *) user_data;

  GRL_DEBUG ("metadata_result_async_cb");

  if (error) {
    ds->error = g_error_copy (error);
  }

  ds->data = media;
  ds->complete = TRUE;
}

static void
store_async_cb (GrlMediaSource *source,
                GrlMediaBox *parent,
                GrlMedia *media,
                gpointer user_data,
                const GError *error)
{
  GrlDataSync *ds = (GrlDataSync *) user_data;

  GRL_DEBUG ("store_async_cb");

  if (error) {
    ds->error = g_error_copy (error);
  }

  ds->complete = TRUE;
}

static void
remove_async_cb (GrlMediaSource *source,
                 GrlMedia *media,
                 gpointer user_data,
                 const GError *error)
{
  GrlDataSync *ds = (GrlDataSync *) user_data;

  GRL_DEBUG ("remove_async_cb");

  if (error) {
    ds->error = g_error_copy (error);
  }

  ds->complete = TRUE;
}

static gint
compare_sorted_results (gconstpointer a, gconstpointer b)
{
  struct SortedResult *r1 = (struct SortedResult *) a;
  struct SortedResult *r2 = (struct SortedResult *) b;
  return r1->remaining < r2->remaining;
}

static void
full_resolution_add_to_waiting_list (GList **waiting_list,
				     GrlMedia *media,
				     guint index)
{
  struct SortedResult *result;
  result = g_new (struct SortedResult, 1);
  result->media = media;
  result->remaining = index;
  *waiting_list = g_list_insert_sorted (*waiting_list,
					result,
					compare_sorted_results);
}

static gboolean
full_resolution_check_waiting_list (GList **waiting_list,
				    guint index,
				    struct FullResolutionDoneCb *done_cb,
				    guint *last_index)
{
  struct FullResolutionCtlCb *ctl_info;
  gboolean emitted = FALSE;

  ctl_info = done_cb->ctl_info;
  if (!ctl_info->next_index)
    return emitted;

  while (*waiting_list) {
    struct SortedResult *r = (struct SortedResult *) (*waiting_list)->data;
    guint index = GPOINTER_TO_UINT (ctl_info->next_index->data);
    if (r->remaining == index) {
      emitted = TRUE;
      *last_index = index;
      ctl_info->user_callback (done_cb->source,
			       done_cb->browse_id,
			       r->media,
			       r->remaining,
			       ctl_info->user_data,
			       NULL);
      /* Move to next index and next item in waiting list */
      ctl_info->next_index =
	g_list_delete_link (ctl_info->next_index, ctl_info->next_index);
      g_free ((*waiting_list)->data);
      *waiting_list = g_list_delete_link (*waiting_list, *waiting_list);
    } else {
      break;
    }
  }

  return emitted;
}

static void
cancel_resolve (gpointer source, gpointer operation_id, gpointer user_data)
{
  grl_operation_cancel (GPOINTER_TO_UINT (operation_id));
}

static void
full_resolution_done_cb (GrlMetadataSource *source,
                         guint resolve_id,
			 GrlMedia *media,
			 gpointer user_data,
			 const GError *error)
{
  GRL_DEBUG ("full_resolution_done_cb");

  struct FullResolutionCtlCb *ctl_info;
  struct FullResolutionDoneCb *cb_info =
    (struct FullResolutionDoneCb *) user_data;

  if (resolve_id > 0) {
    g_hash_table_remove (cb_info->pending_callbacks, source);
  }

  /* We we have a valid source this error comes from the resoluton operation.
     In that case we just did not manage to resolve extra metadata, but
     the result itself as provided by the control callback is valid so we
     just log the error and emit the result as valid. If we do not have a
     source though, it means the error was provided by the control callback
     and in that case we have to emit it */
  if (error && source) {
    if (!g_error_matches (error, GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED)) {
      GRL_WARNING ("Failed to fully resolve some metadata: %s", error->message);
    }
    error = NULL;
  }

  /* Check if pending resolutions must be cancelled */
  if (!cb_info->cancelled &&
      grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (cb_info->source),
                                                  cb_info->browse_id)) {
    cb_info->cancelled = TRUE;
    g_hash_table_foreach (cb_info->pending_callbacks, cancel_resolve, NULL);
  }

  /* If we are done with this result, invoke the user's callback */
  if (g_hash_table_size (cb_info->pending_callbacks) == 0) {
    g_hash_table_unref (cb_info->pending_callbacks);
    ctl_info = cb_info->ctl_info;

    /* Ignore elements coming after finishing the operation (out-of-order elements) */
    if (grl_metadata_source_operation_is_finished (GRL_METADATA_SOURCE (cb_info->source),
                                                                        cb_info->browse_id)) {
      GRL_DEBUG ("operation was finished, skipping full resolutuion done "
                 "result!");
      if (media) {
        g_object_unref (media);
      }
      return;
    }

    /* Check if operation was cancelled before emitting
       (we execute in the idle loop) */
    if (cb_info->cancelled) {
      GRL_DEBUG ("operation was cancelled, skipping full resolution done "
                 "result!");
      if (media) {
	g_object_unref (media);
	media = NULL;
      }
    }

    if (!cb_info->cancelled || cb_info->remaining == 0) {
      /* We can emit the result, but we have to do it in the right order:
	 we cannot guarantee that all the elements are fully resolved in
	 the same order that was requested. Only exception is the operation
	 was cancelled and this is the one with remaining == 0*/
      if (GPOINTER_TO_UINT (ctl_info->next_index->data) == cb_info->remaining
	  || cb_info->cancelled) {
        GError *_error = (GError *)error;
        gboolean should_free_error = FALSE;
	/* Notice we pass NULL as error on purpose
	   since the result is valid even if the full-resolution failed */
	guint remaining = cb_info->remaining;
        if (cb_info->cancelled && remaining==0
            && !g_error_matches (_error, GRL_CORE_ERROR,
                                 GRL_CORE_ERROR_OPERATION_CANCELLED)) {
          /* We are cancelled and this is the last callback, cancelled error need to
           * be set */
          _error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                                "Operation was cancelled");
          should_free_error = TRUE;
        }
	GRL_DEBUG ("  Result is in sort order, emitting (%d)", remaining);
	ctl_info->user_callback (cb_info->source,
				 cb_info->browse_id,
				 media,
				 cb_info->remaining,
				 ctl_info->user_data,
				 _error);
        if (should_free_error && _error)
          g_error_free (_error);

	ctl_info->next_index = g_list_delete_link (ctl_info->next_index,
						   ctl_info->next_index);
	/* Now that we have emitted the next result, check if we
	   had results waiting for this one to be emitted */
	if (remaining != 0) {
	  full_resolution_check_waiting_list (&ctl_info->waiting_list,
					      cb_info->remaining,
					      cb_info,
					      &remaining);
	}
	if (remaining == 0) {
	  if (!ctl_info->chained) {
	    /* We are the last post-processing callback, finish operation */
	    grl_metadata_source_set_operation_finished (GRL_METADATA_SOURCE (cb_info->source),
                                                        cb_info->browse_id);
	  }
	  /* We are done, free the control information now */
	  g_list_free (ctl_info->keys);
	  g_free (ctl_info);	}
      } else {
	full_resolution_add_to_waiting_list (&ctl_info->waiting_list,
					     media,
					     cb_info->remaining);
      }
    }
    g_free (cb_info);
  }
}

static void
full_resolution_ctl_cb (GrlMediaSource *source,
			guint browse_id,
			GrlMedia *media,
			guint remaining,
			gpointer user_data,
			const GError *error)
{
  struct FullResolutionCtlCb *ctl_info =
    (struct FullResolutionCtlCb *) user_data;

  GRL_DEBUG ("full_resolution_ctl_cb");

  /* No need to check if the operation is cancelled, that was
     already checked in the relay callback and this is called
     from there synchronously */

  /* We cannot guarantee that full resolution callbacks will
     keep the emission order, so we have to make sure we emit
     in the same order we receive results here. We use the
     remaining associated to each result to get that order. */
  ctl_info->next_index = g_list_append (ctl_info->next_index,
					GUINT_TO_POINTER (remaining));

  struct FullResolutionDoneCb *done_info =
    g_new (struct FullResolutionDoneCb, 1);

  done_info->source = source;
  done_info->browse_id = browse_id;
  done_info->remaining = remaining;
  done_info->ctl_info = ctl_info;
  done_info->pending_callbacks = g_hash_table_new (g_direct_hash,
                                                   g_direct_equal);
  done_info->cancelled = FALSE;

  if (error || !media) {
    /* No need to start full resolution here, but we cannot emit right away
       either (we have to ensure the order) and that's done in the
       full_resolution_done_cb, so we fake the resolution to get into that
       callback */
    full_resolution_done_cb (NULL, 0, media, done_info, error);
  } else {
    GList *sources, *iter;
    /* Start full-resolution: save all the data we need to emit the result
       when fully resolved */

    sources =
        grl_metadata_source_get_additional_sources (GRL_METADATA_SOURCE (source),
                                                    media, ctl_info->keys,
                                                    NULL, FALSE);

    /* Use suggested sources to fill in missing metadata, the "done"
       callback will be used to emit the resulting object when all metadata has
       been gathered */
    for (iter = sources; iter; iter = g_list_next (iter)) {
      GrlMetadataSource *_source = (GrlMetadataSource *)iter->data;
      GRL_DEBUG ("Using '%s' to resolve extra metadata now",
                 grl_metadata_source_get_name (_source));

      if (grl_metadata_source_supported_operations (_source) & GRL_OP_RESOLVE) {
        guint resolve_id = grl_metadata_source_resolve (_source,
                                                        /* all keys are asked, metadata sources
                                                         * should check what's already in media */
                                                        ctl_info->keys,
                                                        media,
                                                        ctl_info->flags,
                                                        full_resolution_done_cb,
                                                        done_info);
        g_hash_table_insert (done_info->pending_callbacks,
                             _source,
                             GUINT_TO_POINTER (resolve_id));
      }
    }
    g_list_free (sources);

    if (g_hash_table_size (done_info->pending_callbacks) == 0) {
      full_resolution_done_cb (NULL, 0, media, done_info, NULL);
    }
  }
}

static void
metadata_full_resolution_done_cb (GrlMetadataSource *source,
                                  guint resolve_id,
				  GrlMedia *media,
				  gpointer user_data,
				  const GError *error)
{
  GRL_DEBUG ("metadata_full_resolution_done_cb");

  struct MetadataFullResolutionDoneCb *cb_info =
    (struct MetadataFullResolutionDoneCb *) user_data;

  if (resolve_id > 0) {
    g_hash_table_remove (cb_info->pending_callbacks, source);
  }

  if (error &&
      !g_error_matches (error, GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED)) {
    GRL_WARNING ("Failed to fully resolve some metadata: %s", error->message);
  }

  /* Check if pending resolutions must be cancelled */
  if (!cb_info->cancelled &&
      grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (cb_info->source),
                                                  cb_info->ctl_info->metadata_id)) {
    cb_info->cancelled = TRUE;
    g_hash_table_foreach (cb_info->pending_callbacks, cancel_resolve, NULL);
  }

  if (g_hash_table_size (cb_info->pending_callbacks) == 0) {
    GError *_error = NULL;
    g_hash_table_unref (cb_info->pending_callbacks);

    if (grl_metadata_source_operation_is_cancelled (GRL_METADATA_SOURCE (cb_info->source),
                                                    cb_info->ctl_info->metadata_id)) {
      /* if the plugin already set an error, we don't care because we're
       * cancelled */
      _error = g_error_new (GRL_CORE_ERROR, GRL_CORE_ERROR_OPERATION_CANCELLED,
                           "Operation was cancelled");
      /* Yet, we should free the error we just created (if we didn't create it,
       * the plugin owns it) */
    }
    cb_info->user_callback (cb_info->source,
                            cb_info->ctl_info->metadata_id,
			    media,
			    cb_info->user_data,
			    _error);

    if (_error) {
      g_error_free (_error);
    }

    grl_metadata_source_set_operation_finished (GRL_METADATA_SOURCE (cb_info->source),
                                                cb_info->ctl_info->metadata_id);

    g_list_free (cb_info->ctl_info->keys);
    g_free (cb_info->ctl_info);
    g_free (cb_info);
  }
}

static void
metadata_full_resolution_ctl_cb (GrlMediaSource *source,
                                 guint metadata_id,
				 GrlMedia *media,
				 gpointer user_data,
				 const GError *error)
{
  GList *sources, *iter;
  struct MetadataFullResolutionCtlCb *ctl_info =
    (struct MetadataFullResolutionCtlCb *) user_data;

  GRL_DEBUG ("metadata_full_resolution_ctl_cb");

  /* If we got an error, invoke the user callback right away and bail out */
  if (error) {
    if (error->code == GRL_CORE_ERROR_OPERATION_CANCELLED) {
      GRL_DEBUG ("Operation cancelled");
    } else {
      GRL_WARNING ("Operation failed: %s", error->message);
    }
    ctl_info->user_callback (source,
                             ctl_info->metadata_id,
			     media,
			     ctl_info->user_data,
			     error);
    return;
  }

  /* Save all the data we need to emit the result */
  struct MetadataFullResolutionDoneCb *done_info =
    g_new (struct MetadataFullResolutionDoneCb, 1);
  done_info->user_callback = ctl_info->user_callback;
  done_info->user_data = ctl_info->user_data;
  done_info->source = source;
  done_info->ctl_info = ctl_info;
  done_info->pending_callbacks = g_hash_table_new (g_direct_hash,
                                                   g_direct_equal);
  done_info->cancelled = FALSE;

  sources =
      grl_metadata_source_get_additional_sources (GRL_METADATA_SOURCE (source),
                                                  media, ctl_info->keys,
                                                  NULL, FALSE);

  /* Use suggested sources to fill in missing metadata, the "done"
     callback will be used to emit the resulting object when all metadata has
     been gathered */
  for (iter = sources; iter; iter = g_list_next (iter)) {
    GrlMetadataSource *_source = (GrlMetadataSource *)iter->data;
    GRL_DEBUG ("Using '%s' to resolve extra metadata now",
               grl_metadata_source_get_name (_source));

    if (grl_metadata_source_supported_operations (_source) & GRL_OP_RESOLVE) {
      guint resolve_id = grl_metadata_source_resolve (_source,
                                                      /* all keys are asked, metadata sources
                                                       * should check what's already in media */
                                                      ctl_info->keys,
                                                      media,
                                                      ctl_info->flags,
                                                      metadata_full_resolution_done_cb,
                                                      done_info);
      g_hash_table_insert (done_info->pending_callbacks,
                           _source,
                           GUINT_TO_POINTER (resolve_id));
    }
  }
  g_list_free (sources);

  if (g_hash_table_size (done_info->pending_callbacks) == 0) {
    g_hash_table_unref (done_info->pending_callbacks);
    ctl_info->user_callback (source,
                             ctl_info->metadata_id,
			     media,
			     ctl_info->user_data,
			     NULL);

    grl_metadata_source_set_operation_finished (GRL_METADATA_SOURCE (source),
                                                ctl_info->metadata_id);

    g_free (done_info);
  }
}

/* ================ API ================ */

/**
 * grl_media_source_browse:
 * @source: a media source
 * @container: (allow-none): a container of data transfer objects
 * @keys: (element-type GObject.ParamSpec): the #GList of
 * #GrlKeyID<!-- -->s to request
 * @skip: the number if elements to skip in the browse operation
 * @count: the number of elements to retrieve in the browse operation
 * @flags: the resolution mode
 * @callback: (scope notified): the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Browse from @skip, a @count number of media elements through an available list.
 *
 * This method is asynchronous.
 *
 * Returns: the operation identifier
 *
 * Since: 0.1.4
 */
guint
grl_media_source_browse (GrlMediaSource *source,
                         GrlMedia *container,
                         const GList *keys,
                         guint skip,
                         guint count,
                         GrlMetadataResolutionFlags flags,
                         GrlMediaSourceResultCb callback,
                         gpointer user_data)
{
  GrlMediaSourceResultCb _callback;
  gpointer _user_data ;
  GList *_keys;
  GrlMediaSourceBrowseSpec *bs;
  guint browse_id;
  struct BrowseRelayCb *brc;
  gboolean relay_chained = FALSE;
  gboolean full_chained = FALSE;

  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), 0);
  g_return_val_if_fail (callback != NULL, 0);
  g_return_val_if_fail (count > 0, 0);
  g_return_val_if_fail (grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (source)) &
			GRL_OP_BROWSE, 0);

  /* By default assume we will use the parameters specified by the user */
  _keys = g_list_copy ((GList *) keys);
  _callback = callback;
  _user_data = user_data;

  if (flags & GRL_RESOLVE_FAST_ONLY) {
    GRL_DEBUG ("requested fast keys only");
    grl_metadata_source_filter_slow (GRL_METADATA_SOURCE (source),
                                     &_keys,
                                     FALSE);
  }

  /* Setup full resolution mode if requested */
  if (flags & GRL_RESOLVE_FULL) {
    struct FullResolutionCtlCb *c;
    GRL_DEBUG ("requested full resolution");
    _keys =
        grl_metadata_source_expand_operation_keys (GRL_METADATA_SOURCE (source),
                                                   NULL, _keys);

    c = g_new0 (struct FullResolutionCtlCb, 1);
    c->user_callback = _callback;
    c->user_data = _user_data;
    c->keys = g_list_copy (_keys);
    c->flags = flags;
    c->chained = full_chained;

    _callback = full_resolution_ctl_cb;
    _user_data = c;

    relay_chained = TRUE;
  }

  browse_id = grl_operation_generate_id ();

  /* Always hook an own relay callback so we can do some
     post-processing before handing out the results
     to the user */
  brc = g_new0 (struct BrowseRelayCb, 1);
  brc->chained = relay_chained;
  brc->user_callback = _callback;
  brc->user_data = _user_data;
  brc->use_idle = flags & GRL_RESOLVE_IDLE_RELAY;
  _callback = browse_result_relay_cb;
  _user_data = brc;

  bs = g_new0 (GrlMediaSourceBrowseSpec, 1);
  bs->source = g_object_ref (source);
  bs->browse_id = browse_id;
  bs->keys = _keys;
  bs->skip = skip;
  bs->count = count;
  bs->flags = flags;
  bs->callback = _callback;
  bs->user_data = _user_data;
  if (!container) {
    /* Special case: NULL container ==> NULL id */
    bs->container = grl_media_box_new ();
    grl_media_set_id (bs->container, NULL);
  } else {
    bs->container = g_object_ref (container);
  }

  /* Save a reference to the operaton spec in the relay-cb's
     user_data so that we can free the spec there when we get
     the last result */
  brc->bspec = bs;

  /* Setup auto-split management if requested */
  if (source->priv->auto_split_threshold > 0 &&
      count > source->priv->auto_split_threshold) {
    GRL_DEBUG ("auto-split: enabled");
    struct AutoSplitCtl *as_ctl = g_new0 (struct AutoSplitCtl, 1);
    as_ctl->count = count;
    as_ctl->threshold = source->priv->auto_split_threshold;
    as_ctl->chunk_requested = as_ctl->threshold;
    as_ctl->chunk_first = TRUE;
    bs->count = as_ctl->chunk_requested;
    brc->auto_split = as_ctl;
    GRL_DEBUG ("auto-split: requesting first chunk (skip=%u, count=%u)",
               bs->skip, bs->count);
  }

  grl_metadata_source_set_operation_ongoing (GRL_METADATA_SOURCE (source),
                                             browse_id);
  g_idle_add_full (brc->use_idle? G_PRIORITY_DEFAULT_IDLE: G_PRIORITY_HIGH_IDLE,
                   browse_idle,
                   bs,
                   NULL);

  return browse_id;
}

/**
 * grl_media_source_browse_sync:
 * @source: a media source
 * @container: (allow-none): a container of data transfer objects
 * @keys: (element-type GObject.ParamSpec): the #GList of
 * #GrlKeyID<!-- -->s to request
 * @skip: the number if elements to skip in the browse operation
 * @count: the number of elements to retrieve in the browse operation
 * @flags: the resolution mode
 * @error: a #GError, or @NULL
 *
 * Browse from @skip, a @count number of media elements through an available
 * list.
 *
 * This method is synchronous.
 *
 * Returns: (element-type Grl.Media) (transfer full): a #GList with #GrlMedia
 * elements. After use g_object_unref() every element and g_list_free() the
 * list.
 *
 * Since: 0.1.6
 */
GList *
grl_media_source_browse_sync (GrlMediaSource *source,
                              GrlMedia *container,
                              const GList *keys,
                              guint skip,
                              guint count,
                              GrlMetadataResolutionFlags flags,
                              GError **error)
{
  GrlDataSync *ds;
  GList *result;

  ds = g_slice_new0 (GrlDataSync);

  grl_media_source_browse (source,
                           container,
                           keys,
                           skip,
                           count,
                           flags,
                           multiple_result_async_cb,
                           ds);

  grl_wait_for_async_operation_complete (ds);

  if (ds->error) {
    if (error) {
      *error = ds->error;
    } else {
      g_error_free (ds->error);
    }
  }

  result = (GList *) ds->data;
  g_slice_free (GrlDataSync, ds);

  return result;
}

/**
 * grl_media_source_search:
 * @source: a media source
 * @text: the text to search
 * @keys: (element-type GObject.ParamSpec): the #GList of
 * #GrlKeyID<!-- -->s to request
 * @skip: the number if elements to skip in the search operation
 * @count: the number of elements to retrieve in the search operation
 * @flags: the resolution mode
 * @callback: (scope notified): the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Search for the @text string in a media source for data identified with
 * that string.
 *
 * If @text is @NULL then no text filter will be applied, and thus, no media
 * items from @source will be filtered. If @source does not support NULL-text
 * search operations it should notiy the client by setting
 * @GRL_CORE_ERROR_SEARCH_NULL_UNSUPPORTED in @callback's error parameter.
 *
 * This method is asynchronous.
 *
 * Returns: the operation identifier
 *
 * Since: 0.1.1
 */
guint
grl_media_source_search (GrlMediaSource *source,
                         const gchar *text,
                         const GList *keys,
                         guint skip,
                         guint count,
                         GrlMetadataResolutionFlags flags,
                         GrlMediaSourceResultCb callback,
                         gpointer user_data)
{
  GrlMediaSourceResultCb _callback;
  gpointer _user_data ;
  GList *_keys;
  GrlMediaSourceSearchSpec *ss;
  guint search_id;
  struct BrowseRelayCb *brc;
  gboolean relay_chained = FALSE;
  gboolean full_chained = FALSE;

  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), 0);
  g_return_val_if_fail (callback != NULL, 0);
  g_return_val_if_fail (count > 0, 0);
  g_return_val_if_fail (grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (source)) &
			GRL_OP_SEARCH, 0);

  /* By default assume we will use the parameters specified by the user */
  _callback = callback;
  _user_data = user_data;
  _keys = g_list_copy ((GList *) keys);

  if (flags & GRL_RESOLVE_FAST_ONLY) {
    GRL_DEBUG ("requested fast keys only");
    grl_metadata_source_filter_slow (GRL_METADATA_SOURCE (source), &_keys, FALSE);
  }

  if (flags & GRL_RESOLVE_FULL) {
    struct FullResolutionCtlCb *c;
    GRL_DEBUG ("requested full search");
    _keys =
        grl_metadata_source_expand_operation_keys (GRL_METADATA_SOURCE (source),
                                                   NULL, _keys);

    c = g_new0 (struct FullResolutionCtlCb, 1);
    c->user_callback = callback;
    c->user_data = user_data;
    c->keys = g_list_copy (_keys);
    c->flags = flags;
    c->chained = full_chained;

    _callback = full_resolution_ctl_cb;
    _user_data = c;

    relay_chained = TRUE;
  }

  search_id = grl_operation_generate_id ();

  brc = g_new0 (struct BrowseRelayCb, 1);
  brc->chained = relay_chained;
  brc->user_callback = _callback;
  brc->user_data = _user_data;
  brc->use_idle = flags & GRL_RESOLVE_IDLE_RELAY;
  _callback = browse_result_relay_cb;
  _user_data = brc;

  ss = g_new0 (GrlMediaSourceSearchSpec, 1);
  ss->source = g_object_ref (source);
  ss->search_id = search_id;
  ss->text = g_strdup (text);
  ss->keys = _keys;
  ss->skip = skip;
  ss->count = count;
  ss->flags = flags;
  ss->callback = _callback;
  ss->user_data = _user_data;

  /* Save a reference to the operaton spec in the relay-cb's
     user_data so that we can free the spec there when we get
     the last result */
  brc->sspec = ss;

  /* Setup auto-split management if requested */
  if (source->priv->auto_split_threshold > 0 &&
      count > source->priv->auto_split_threshold) {
    GRL_DEBUG ("auto-split: enabled");
    struct AutoSplitCtl *as_ctl = g_new0 (struct AutoSplitCtl, 1);
    as_ctl->count = count;
    as_ctl->threshold = source->priv->auto_split_threshold;
    as_ctl->chunk_requested = as_ctl->threshold;
    as_ctl->chunk_first = TRUE;
    ss->count = as_ctl->chunk_requested;
    brc->auto_split = as_ctl;
    GRL_DEBUG ("auto-split: requesting first chunk (skip=%u, count=%u)",
               ss->skip, ss->count);
  }

  grl_metadata_source_set_operation_ongoing (GRL_METADATA_SOURCE (source),
                                             search_id);
  g_idle_add_full (brc->use_idle? G_PRIORITY_DEFAULT_IDLE: G_PRIORITY_HIGH_IDLE,
                   search_idle,
                   ss,
                   NULL);

  return search_id;
}

/**
 * grl_media_source_search_sync:
 * @source: a media source
 * @text: the text to search
 * @keys: (element-type GObject.ParamSpec): the #GList of
 * #GrlKeyID<!-- -->s to request
 * @skip: the number if elements to skip in the search operation
 * @count: the number of elements to retrieve in the search operation
 * @flags: the resolution mode
 * @error: a #GError, or @NULL
 *
 * Search for the @text string in a media source for data identified with
 * that string.
 *
 * If @text is @NULL then no text filter will be applied, and thus, no media
 * items from @source will be filtered. If @source does not support NULL-text
 * search operations it should notiy the client by setting
 * @GRL_CORE_ERROR_SEARCH_NULL_UNSUPPORTED in the error parameter.
 *
 * This method is synchronous.
 *
 * Returns: (element-type Grl.Media) (transfer full): a #GList with #GrlMedia
 * elements. After use g_object_unref() every element and g_list_free() the
 * list.
 *
 * Since: 0.1.6
 */
GList *
grl_media_source_search_sync (GrlMediaSource *source,
                              const gchar *text,
                              const GList *keys,
                              guint skip,
                              guint count,
                              GrlMetadataResolutionFlags flags,
                              GError **error)
{
  GrlDataSync *ds;
  GList *result;

  ds = g_slice_new0 (GrlDataSync);

  grl_media_source_search (source,
                           text,
                           keys,
                           skip,
                           count,
                           flags,
                           multiple_result_async_cb,
                           ds);

  grl_wait_for_async_operation_complete (ds);

  if (ds->error) {
    if (error) {
      *error = ds->error;
    } else {
      g_error_free (ds->error);
    }
  }

  result = (GList *) ds->data;
  g_slice_free (GrlDataSync, ds);

  return result;
}

/**
 * grl_media_source_query:
 * @source: a media source
 * @query: the query to process
 * @keys: (element-type GObject.ParamSpec): the #GList of
 * #GrlKeyID<!-- -->s to request
 * @skip: the number if elements to skip in the query operation
 * @count: the number of elements to retrieve in the query operation
 * @flags: the resolution mode
 * @callback: (scope notified): the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Execute a specialized query (specific for each provider) on a media
 * repository.
 *
 * It is different from grl_media_source_search() semantically, because
 * the query implies a carefully crafted string, rather than a simple
 * string to search.
 *
 * This method is asynchronous.
 *
 * Returns: the operation identifier
 *
 * Since: 0.1.1
 */
guint
grl_media_source_query (GrlMediaSource *source,
                        const gchar *query,
                        const GList *keys,
                        guint skip,
                        guint count,
                        GrlMetadataResolutionFlags flags,
                        GrlMediaSourceResultCb callback,
                        gpointer user_data)
{
  GrlMediaSourceResultCb _callback;
  gpointer _user_data ;
  GList *_keys;
  GrlMediaSourceQuerySpec *qs;
  guint query_id;
  struct BrowseRelayCb *brc;
  gboolean relay_chained = FALSE;
  gboolean full_chained = FALSE;

  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), 0);
  g_return_val_if_fail (query != NULL, 0);
  g_return_val_if_fail (callback != NULL, 0);
  g_return_val_if_fail (count > 0, 0);
  g_return_val_if_fail (grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (source)) &
			GRL_OP_QUERY, 0);

  /* By default assume we will use the parameters specified by the user */
  _callback = callback;
  _user_data = user_data;
  _keys = g_list_copy ((GList *) keys);

  if (flags & GRL_RESOLVE_FAST_ONLY) {
    GRL_DEBUG ("requested fast keys only");
    grl_metadata_source_filter_slow (GRL_METADATA_SOURCE (source),
                                     &_keys,
                                     FALSE);
  }

  if (flags & GRL_RESOLVE_FULL) {
    struct FullResolutionCtlCb *c;
    GRL_DEBUG ("requested full search");
    _keys =
        grl_metadata_source_expand_operation_keys (GRL_METADATA_SOURCE (source),
                                                   NULL, _keys);

    c = g_new0 (struct FullResolutionCtlCb, 1);
    c->user_callback = callback;
    c->user_data = user_data;
    c->keys = g_list_copy (_keys);
    c->flags = flags;
    c->chained = full_chained;

    _callback = full_resolution_ctl_cb;
    _user_data = c;

    relay_chained = TRUE;
  }

  query_id = grl_operation_generate_id ();

  brc = g_new0 (struct BrowseRelayCb, 1);
  brc->chained = relay_chained;
  brc->user_callback = _callback;
  brc->user_data = _user_data;
  brc->use_idle = flags & GRL_RESOLVE_IDLE_RELAY;
  _callback = browse_result_relay_cb;
  _user_data = brc;

  qs = g_new0 (GrlMediaSourceQuerySpec, 1);
  qs->source = g_object_ref (source);
  qs->query_id = query_id;
  qs->query = g_strdup (query);
  qs->keys = _keys;
  qs->skip = skip;
  qs->count = count;
  qs->flags = flags;
  qs->callback = _callback;
  qs->user_data = _user_data;

  /* Save a reference to the operaton spec in the relay-cb's
     user_data so that we can free the spec there when we get
     the last result */
  brc->qspec = qs;

  /* Setup auto-split management if requested */
  if (source->priv->auto_split_threshold > 0 &&
      count > source->priv->auto_split_threshold) {
    GRL_DEBUG ("auto-split: enabled");
    struct AutoSplitCtl *as_ctl = g_new0 (struct AutoSplitCtl, 1);
    as_ctl->count = count;
    as_ctl->threshold = source->priv->auto_split_threshold;
    as_ctl->chunk_requested = as_ctl->threshold;
    as_ctl->chunk_first = TRUE;
    qs->count = as_ctl->chunk_requested;
    brc->auto_split = as_ctl;
    GRL_DEBUG ("auto-split: requesting first chunk (skip=%u, count=%u)",
               qs->skip, qs->count);
  }

  grl_metadata_source_set_operation_ongoing (GRL_METADATA_SOURCE (source),
                                             query_id);
  g_idle_add_full (brc->use_idle? G_PRIORITY_DEFAULT_IDLE: G_PRIORITY_HIGH_IDLE,
                   query_idle,
                   qs,
                   NULL);

  return query_id;
}

/**
 * grl_media_source_query_sync:
 * @source: a media source
 * @query: the query to process
 * @keys: (element-type GObject.ParamSpec): the #GList of
 * #GrlKeyID<!-- -->s to request
 * @skip: the number if elements to skip in the query operation
 * @count: the number of elements to retrieve in the query operation
 * @flags: the resolution mode
 * @error: a #GError, or @NULL
 *
 * Execute a specialized query (specific for each provider) on a media
 * repository.
 *
 * This method is synchronous.
 *
 * Returns: (element-type Grl.Media) (transfer full): a #GList with #GrlMedia
 * elements. After use g_object_unref() every element and g_list_free() the
 * list.
 *
 * Since: 0.1.6
 */
GList *
grl_media_source_query_sync (GrlMediaSource *source,
                             const gchar *query,
                             const GList *keys,
                             guint skip,
                             guint count,
                             GrlMetadataResolutionFlags flags,
                             GError **error)
{
  GrlDataSync *ds;
  GList *result;

  ds = g_slice_new0 (GrlDataSync);

  grl_media_source_query (source,
                          query,
                          keys,
                          skip,
                          count,
                          flags,
                          multiple_result_async_cb,
                          ds);

  grl_wait_for_async_operation_complete (ds);

  if (ds->error) {
    if (error) {
      *error = ds->error;
    } else {
      g_error_free (ds->error);
    }
  }

  result = (GList *) ds->data;
  g_slice_free (GrlDataSync, ds);

  return result;
}

/**
 * grl_media_source_metadata:
 * @source: a media source
 * @media: (allow-none): a data transfer object
 * @keys: (element-type GObject.ParamSpec): the #GList of
 * #GrlKeyID<!-- -->s to request
 * @flags: the resolution mode
 * @callback: (scope notified): the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * This method is intended to fetch the requested keys of metadata of
 * a given @media to the media source.
 *
 * This method is asynchronous.
 *
 * Returns: the operation identifier
 *
 * Since: 0.1.6
 */
guint
grl_media_source_metadata (GrlMediaSource *source,
                           GrlMedia *media,
                           const GList *keys,
                           GrlMetadataResolutionFlags flags,
                           GrlMediaSourceMetadataCb callback,
                           gpointer user_data)
{
  GrlMediaSourceMetadataCb _callback;
  gpointer _user_data ;
  GList *_keys;
  GrlMediaSourceMetadataSpec *ms;
  struct MetadataRelayCb *mrc;
  guint metadata_id;

  GRL_DEBUG ("grl_media_source_metadata");

  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), 0);
  g_return_val_if_fail (keys != NULL, 0);
  g_return_val_if_fail (callback != NULL, 0);
  g_return_val_if_fail (grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (source)) &
                        GRL_OP_METADATA, 0);

  /* By default assume we will use the parameters specified by the user */
  _callback = callback;
  _user_data = user_data;
  _keys = g_list_copy ((GList *) keys);

  if (flags & GRL_RESOLVE_FAST_ONLY) {
    grl_metadata_source_filter_slow (GRL_METADATA_SOURCE (source),
                                     &_keys, FALSE);
  }

  metadata_id = grl_operation_generate_id ();

  if (flags & GRL_RESOLVE_FULL) {
    struct MetadataFullResolutionCtlCb *c;
    GRL_DEBUG ("requested full metadata");
    _keys =
        grl_metadata_source_expand_operation_keys (GRL_METADATA_SOURCE (source),
                                                   media, _keys);

      c = g_new0 (struct MetadataFullResolutionCtlCb, 1);
      c->user_callback = callback;
      c->user_data = user_data;
      c->keys = g_list_copy (_keys);
      c->flags = flags;
      c->metadata_id = metadata_id;

      _callback = metadata_full_resolution_ctl_cb;
      _user_data = c;
  }

  /* Always hook an own relay callback so we can do some
     post-processing before handing out the results
     to the user */

  mrc = g_new0 (struct MetadataRelayCb, 1);
  mrc->user_callback = _callback;
  mrc->user_data = _user_data;
  _callback = metadata_result_relay_cb;
  _user_data = mrc;

  ms = g_new0 (GrlMediaSourceMetadataSpec, 1);
  ms->source = g_object_ref (source);
  ms->metadata_id = metadata_id;
  ms->keys = _keys; /* It is already a copy */
  ms->flags = flags;
  ms->callback = _callback;
  ms->user_data = _user_data;
  if (!media) {
    /* Special case, NULL media ==> root container */
    ms->media = grl_media_box_new ();
    grl_media_set_id (ms->media, NULL);
  } else {
    ms->media = media;
  }
  g_object_ref (ms->media);

  /* Save a reference to the operaton spec in the relay-cb's
     user_data so that we can free the spec there */
  mrc->spec = ms;

  grl_metadata_source_set_operation_ongoing (GRL_METADATA_SOURCE (source),
                                             metadata_id);
  g_idle_add_full (flags & GRL_RESOLVE_IDLE_RELAY?
                   G_PRIORITY_DEFAULT_IDLE: G_PRIORITY_HIGH_IDLE,
                   metadata_idle,
                   ms,
                   NULL);

  return metadata_id;
}

/**
 * grl_media_source_metadata_sync:
 * @source: a media source
 * @media: (allow-none): a data transfer object
 * @keys: (element-type GObject.ParamSpec): the #GList of
 * #GrlKeyID<!-- -->s to request
 * @flags: the resolution mode
 * @error: a #GError, or @NULL
 *
 * This method is intended to fetch the requested keys of metadata of
 * a given @media to the media source.
 *
 * This method is synchronous.
 *
 * Returns: (transfer full): a filled #GrlMedia
 *
 * Since: 0.1.6
 */
GrlMedia *
grl_media_source_metadata_sync (GrlMediaSource *source,
                                GrlMedia *media,
                                const GList *keys,
                                GrlMetadataResolutionFlags flags,
                                GError **error)
{
  GrlDataSync *ds;

  ds = g_slice_new0 (GrlDataSync);

  grl_media_source_metadata (source,
                             media,
                             keys,
                             flags,
                             metadata_result_async_cb,
                             ds);

  grl_wait_for_async_operation_complete (ds);

  if (ds->error) {
    if (error) {
      *error = ds->error;
    } else {
      g_error_free (ds->error);
    }
  }

  g_slice_free (GrlDataSync, ds);

  return media;
}

static GrlSupportedOps
grl_media_source_supported_operations (GrlMetadataSource *metadata_source)
{
  GrlSupportedOps caps;
  GrlMediaSource *source;
  GrlMediaSourceClass *media_source_class;
  GrlMetadataSourceClass *metadata_source_class;

  metadata_source_class =
    GRL_METADATA_SOURCE_CLASS (grl_media_source_parent_class);
  source = GRL_MEDIA_SOURCE (metadata_source);
  media_source_class = GRL_MEDIA_SOURCE_GET_CLASS (source);

  caps = metadata_source_class->supported_operations (metadata_source);
  if (media_source_class->browse)
    caps |= GRL_OP_BROWSE;
  if (media_source_class->search)
    caps |= GRL_OP_SEARCH;
  if (media_source_class->query)
    caps |= GRL_OP_QUERY;
  if (media_source_class->metadata)
    caps |= GRL_OP_METADATA;
  if (media_source_class->store)  /* We do not assume GRL_OP_STORE_PARENT */
    caps |= GRL_OP_STORE;
  if (media_source_class->remove)
    caps |= GRL_OP_REMOVE;
  if (media_source_class->test_media_from_uri &&
      media_source_class->media_from_uri)
    caps |= GRL_OP_MEDIA_FROM_URI;
  if (media_source_class->notify_change_start &&
      media_source_class->notify_change_stop)
    caps |= GRL_OP_NOTIFY_CHANGE;

  return caps;
}

/**
 * grl_media_source_cancel:
 * @source: a media source
 * @operation_id: the identifier of the running operation, as returned by the
 * function that started it
 *
 * Cancel a running method.
 *
 * The derived class must implement the cancel vmethod in order to honour the
 * request correctly. Otherwise, the operation will not be interrupted.
 *
 * In all cases, if this function is called on an ongoing operation, the
 * corresponding callback will be called with the
 * @GRL_CORE_ERROR_OPERATION_CANCELLED error set, and no more action will be
 * taken for that operation after the said callback with error has been called.
 *
 * Since: 0.1.1
 * Deprecated: 0.1.14: Use grl_operation_cancel() instead
 */
void
grl_media_source_cancel (GrlMediaSource *source, guint operation_id)
{
  GRL_DEBUG ("grl_media_source_cancel");

  g_return_if_fail (GRL_IS_MEDIA_SOURCE (source));

  GRL_WARNING ("grl_media_source_cancel() is deprecated. "
               "Use grl_operation_cancel() instead");

  grl_operation_cancel (operation_id);
}

/**
 * grl_media_source_set_operation_data:
 * @source: a media source
 * @operation_id: the identifier of a running operation
 * @data: the data to attach
 *
 * Attach a pointer to the specific operation.
 *
 * Since: 0.1.1
 * Deprecated: 0.1.14: Use grl_operation_set_data() instead
 */
void
grl_media_source_set_operation_data (GrlMediaSource *source,
                                     guint operation_id,
                                     gpointer data)
{
  GRL_DEBUG ("grl_media_source_set_operation_data");

  g_return_if_fail (GRL_IS_MEDIA_SOURCE (source));

  GRL_WARNING ("grl_media_source_set_operation_data() is deprecated. "
               "Use grl_operation_set_data() instead");

  grl_operation_set_data (operation_id, data);
}

/**
 * grl_media_source_get_operation_data:
 * @source: a media source
 * @operation_id: the identifier of a running operation
 *
 * Obtains the previously attached data
 *
 * Returns: (transfer none): The previously attached data.
 *
 * Since: 0.1.1
 * Deprecated: 0.1.14: Use grl_operation_get_data() instead
 */
gpointer
grl_media_source_get_operation_data (GrlMediaSource *source,
                                     guint operation_id)
{
  GRL_DEBUG ("grl_media_source_get_operation_data");

  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), NULL);

  GRL_WARNING ("grl_metadata_source_get_operation_data() is deprecated. "
               "Use grl_operation_get_data() instead");

  return grl_operation_get_data (operation_id);
}

/**
 * grl_media_source_get_auto_split_threshold:
 * @source: a media source
 *
 * TBD
 *
 * Returns: the assigned threshold
 *
 * Since: 0.1.1
 */
guint
grl_media_source_get_auto_split_threshold (GrlMediaSource *source)
{
  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), 0);
  return source->priv->auto_split_threshold;
}

/**
 * grl_media_source_set_auto_split_threshold:
 * @source: a media source
 * @threshold: the threshold to request
 *
 * TBD
 *
 * Since: 0.1.1
 */
void
grl_media_source_set_auto_split_threshold (GrlMediaSource *source,
                                           guint threshold)
{
  g_return_if_fail (GRL_IS_MEDIA_SOURCE (source));
  source->priv->auto_split_threshold = threshold;
}

/**
 * grl_media_source_store:
 * @source: a media source
 * @parent: (allow-none): a parent to store the data transfer objects
 * @media: a data transfer object
 * @callback: (scope notified): the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Store the @media into the @parent container
 *
 * This method is asynchronous.
 *
 * Since: 0.1.4
 */
void
grl_media_source_store (GrlMediaSource *source,
                        GrlMediaBox *parent,
                        GrlMedia *media,
                        GrlMediaSourceStoreCb callback,
                        gpointer user_data)
{
  GRL_DEBUG ("grl_media_source_store");

  const gchar *title;
  const gchar *url;
  GError *error = NULL;

  g_return_if_fail (GRL_IS_MEDIA_SOURCE (source));
  g_return_if_fail (!parent || GRL_IS_MEDIA_BOX (parent));
  g_return_if_fail (GRL_IS_MEDIA (media));
  g_return_if_fail (callback != NULL);
  g_return_if_fail ((!parent &&
                     grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (source)) &
		     GRL_OP_STORE) ||
		    (parent &&
                     grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (source)) &
		     GRL_OP_STORE_PARENT));

  /* First, check that we have the minimum information we need */
  title = grl_media_get_title (media);
  url = grl_media_get_url (media);

  if (!title) {
    error = g_error_new (GRL_CORE_ERROR,
			 GRL_CORE_ERROR_STORE_FAILED,
			 "Media has no title, cannot store");
  } else if (!url && !GRL_IS_MEDIA_BOX (media)) {
    error = g_error_new (GRL_CORE_ERROR,
			 GRL_CORE_ERROR_STORE_FAILED,
			 "Media has no URL, cannot store");
  }

  /* If we have the info, ask the plugin to store the media */
  if (!error) {
    GrlMediaSourceStoreSpec *ss = g_new0 (GrlMediaSourceStoreSpec, 1);
    ss->source = g_object_ref (source);
    ss->parent = parent ? g_object_ref (parent) : NULL;
    ss->media = g_object_ref (media);
    ss->callback = callback;
    ss->user_data = user_data;

    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
		     store_idle,
		     ss,
		     store_idle_destroy);
  } else {
    callback (source, parent, media, user_data, error);
    g_error_free (error);
  }
}

/**
 * grl_media_source_store_sync:
 * @source: a media source
 * @parent: (allow-none): a #GrlMediaBox to store the data transfer objects
 * @media: a #GrlMedia data transfer object
 * @error: a #GError, or @NULL
 *
 * Store the @media into the @parent container.
 *
 * This method is synchronous.
 *
 * Since: 0.1.6
 */
void
grl_media_source_store_sync (GrlMediaSource *source,
                             GrlMediaBox *parent,
                             GrlMedia *media,
                             GError **error)
{
  GrlDataSync *ds;

  ds = g_slice_new0 (GrlDataSync);

  grl_media_source_store (source,
                          parent,
                          media,
                          store_async_cb,
                          ds);

  grl_wait_for_async_operation_complete (ds);

  if (ds->error) {
    if (error) {
      *error = ds->error;
    } else {
      g_error_free (ds->error);
    }
  }

  g_slice_free (GrlDataSync, ds);
}

/**
 * grl_media_source_remove:
 * @source: a media source
 * @media: a data transfer object
 * @callback: (scope notified): the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Remove a @media from the @source repository.
 *
 * This method is asynchronous.
 *
 * Since: 0.1.4
 */
void
grl_media_source_remove (GrlMediaSource *source,
                         GrlMedia *media,
                         GrlMediaSourceRemoveCb callback,
                         gpointer user_data)
{
  GRL_DEBUG ("grl_media_source_remove");

  const gchar *id;
  GError *error = NULL;

  g_return_if_fail (GRL_IS_MEDIA_SOURCE (source));
  g_return_if_fail (GRL_IS_MEDIA (media));
  g_return_if_fail (callback != NULL);
  g_return_if_fail (grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (source)) &
		    GRL_OP_REMOVE);

  /* First, check that we have the minimum information we need */
  id = grl_media_get_id (media);
  if (!id) {
    error = g_error_new (GRL_CORE_ERROR,
			 GRL_CORE_ERROR_REMOVE_FAILED,
			 "Media has no id, cannot remove");
  }

  if (!error) {
    GrlMediaSourceRemoveSpec *rs = g_new0 (GrlMediaSourceRemoveSpec, 1);
    rs->source = g_object_ref (source);
    rs->media_id = g_strdup (id);
    rs->media = g_object_ref (media);
    rs->callback = callback;
    rs->user_data = user_data;

    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
		     remove_idle,
		     rs,
		     remove_idle_destroy);
  } else {
    callback (source, media, user_data, error);
    g_error_free (error);
  }
}

/**
 * grl_media_source_remove_sync:
 * @source: a media source
 * @media: a data transfer object
 * @error: a #GError, or @NULL
 *
 * Remove a @media from the @source repository.
 *
 * This method is synchronous.
 *
 * Since: 0.1.6
 */
void
grl_media_source_remove_sync (GrlMediaSource *source,
                              GrlMedia *media,
                              GError **error)
{
  GrlDataSync *ds;

  ds = g_slice_new0 (GrlDataSync);

  grl_media_source_remove (source,
                           media,
                           remove_async_cb,
                           ds);

  grl_wait_for_async_operation_complete (ds);

  if (ds->error) {
    if (error) {
      *error = ds->error;
    } else {
      g_error_free (ds->error);
    }
  }

  g_slice_free (GrlDataSync, ds);
}

/**
 * grl_media_source_test_media_from_uri:
 * @source: a media source
 * @uri: A URI that can be used to identify a media resource
 *
 * Tests whether @source can instantiate a #GrlMedia object representing
 * the media resource exposed at @uri.
 *
 * Returns: %TRUE if it can, %FALSE otherwise.
 *
 * This method is synchronous.
 *
 * Since: 0.1.7
 */
gboolean
grl_media_source_test_media_from_uri (GrlMediaSource *source,
				       const gchar *uri)
{
  GRL_DEBUG ("grl_media_source_test_media_from_uri");

  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), FALSE);
  g_return_val_if_fail (uri != NULL, FALSE);

  if (GRL_MEDIA_SOURCE_GET_CLASS (source)->test_media_from_uri) {
    return GRL_MEDIA_SOURCE_GET_CLASS (source)->test_media_from_uri (source,
								     uri);
  } else {
    return FALSE;
  }
}

/**
 * grl_media_source_get_media_from_uri:
 * @source: a media source
 * @uri: A URI that can be used to identify a media resource
 * @keys: (element-type GrlKeyID): A list of keys to resolve
 * @flags: the resolution mode
 * @callback: (scope notified): the user defined callback
 * @user_data: the user data to pass in the callback
 *
 * Creates an instance of #GrlMedia representing the media resource
 * exposed at @uri.
 *
 * It is recommended to call grl_media_source_test_media_from_uri() before
 * invoking this to check whether the target source can theoretically do the
 * resolution.
 *
 * This method is asynchronous.
 *
 * Returns: the operation identifier
 *
 * Since: 0.1.14
 */
guint
grl_media_source_get_media_from_uri (GrlMediaSource *source,
				     const gchar *uri,
				     const GList *keys,
				     GrlMetadataResolutionFlags flags,
				     GrlMediaSourceMetadataCb callback,
				     gpointer user_data)
{
  GRL_DEBUG ("grl_media_source_get_media_from_uri");

  GList *_keys;
  GrlMediaSourceMediaFromUriSpec *mfus;
  struct MediaFromUriRelayCb *mfsrc;
  guint media_from_uri_id;

  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), 0);
  g_return_val_if_fail (uri != NULL, 0);
  g_return_val_if_fail (keys != NULL, 0);
  g_return_val_if_fail (callback != NULL, 0);
  g_return_val_if_fail (grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (source)) &
                        GRL_OP_MEDIA_FROM_URI, 0);

  _keys = g_list_copy ((GList *) keys);
  if (flags & GRL_RESOLVE_FAST_ONLY) {
    grl_metadata_source_filter_slow (GRL_METADATA_SOURCE (source),
                                     &_keys, FALSE);
  }

  media_from_uri_id = grl_operation_generate_id ();

  /* We cannot prepare for full resolution yet because we don't
     have a GrlMedia t operate with.
     TODO: full resolution could be added in the relay calback
     when we get the GrlMedia object */

  /* Always hook an own relay callback so we can do some
     post-processing before handing out the results
     to the user */

  mfsrc = g_new0 (struct MediaFromUriRelayCb, 1);
  mfsrc->user_callback = callback;
  mfsrc->user_data = user_data;

  mfus = g_new0 (GrlMediaSourceMediaFromUriSpec, 1);
  mfus->source = g_object_ref (source);
  mfus->media_from_uri_id = media_from_uri_id;
  mfus->uri = g_strdup (uri);
  mfus->keys = _keys;
  mfus->flags = flags;
  mfus->callback = media_from_uri_relay_cb;
  mfus->user_data = mfsrc;

  /* Save a reference to the operaton spec in the relay-cb's
     user_data so that we can free the spec there */
  mfsrc->spec = mfus;

  grl_metadata_source_set_operation_ongoing (GRL_METADATA_SOURCE (source),
                                             media_from_uri_id);
  g_idle_add_full (flags & GRL_RESOLVE_IDLE_RELAY?
                   G_PRIORITY_DEFAULT_IDLE: G_PRIORITY_HIGH_IDLE,
                   media_from_uri_idle,
                   mfus,
                   NULL);

  return media_from_uri_id;
}

/**
 * grl_media_source_get_media_from_uri_sync:
 * @source: a media source
 * @uri: A URI that can be used to identify a media resource
 * @keys: (element-type GrlKeyID): A list of keys to resolve
 * @flags: the resolution mode
 * @error: a #GError, or @NULL
 *
 * Creates an instance of #GrlMedia representing the media resource
 * exposed at @uri.
 *
 * It is recommended to call grl_media_source_test_media_from_uri() before
 * invoking this to check whether the target source can theoretically do the
 * resolution.
 *
 * This method is synchronous.
 *
 * Returns: (transfer full): a filled #GrlMedia
 *
 * Since: 0.1.8
 */
GrlMedia *
grl_media_source_get_media_from_uri_sync (GrlMediaSource *source,
                                          const gchar *uri,
                                          const GList *keys,
                                          GrlMetadataResolutionFlags flags,
                                          GError **error)
{
  GrlDataSync *ds;
  GrlMedia *result;

  ds = g_slice_new0 (GrlDataSync);

  grl_media_source_get_media_from_uri (source,
                                       uri,
                                       keys,
                                       flags,
                                       metadata_result_async_cb,
                                       ds);

  grl_wait_for_async_operation_complete (ds);

  if (ds->error) {
    if (error) {
      *error = ds->error;
    } else {
      g_error_free (ds->error);
    }
  }

  result = (GrlMedia *) ds->data;
  g_slice_free (GrlDataSync, ds);

  return result;
}

/**
 * grl_media_source_notify_change_start:
 * @source: a media source
 * @error: a #GError, or @NULL
 *
 * Starts emitting ::content-changed signals when @source discovers changes in
 * the content. This instructs @source to setup the machinery needed to be aware
 * of changes in the content.
 *
 * Returns: @TRUE if initialization has succeed.
 *
 * Since: 0.1.9
 */
gboolean
grl_media_source_notify_change_start (GrlMediaSource *source,
                                      GError **error)
{
  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), FALSE);
  g_return_val_if_fail (grl_media_source_supported_operations (GRL_METADATA_SOURCE (source)) &
                        GRL_OP_NOTIFY_CHANGE, FALSE);

  return GRL_MEDIA_SOURCE_GET_CLASS (source)->notify_change_start (source,
                                                                   error);
}

/**
 * grl_media_source_notify_change_stop:
 * @source: a media source
 * @error: a #GError, or @NULL
 *
 * This will drop emission of ::content-changed signals from @source. When this
 * is done @source should stop the machinery required for it to track changes in
 * the content.
 *
 * Returns: @TRUE if stop has succeed.
 *
 * Since: 0.1.9
 */
gboolean
grl_media_source_notify_change_stop (GrlMediaSource *source,
                                     GError **error)
{
  g_return_val_if_fail (GRL_IS_MEDIA_SOURCE (source), FALSE);
  g_return_val_if_fail (grl_media_source_supported_operations (GRL_METADATA_SOURCE (source)) &
                        GRL_OP_NOTIFY_CHANGE, FALSE);

  return GRL_MEDIA_SOURCE_GET_CLASS (source)->notify_change_stop (source,
                                                                  error);
}

/**
 * grl_media_source_notify_change_list:
 * @source: a media source
 * @changed_medias: (element-type Grl.Media) (transfer full):: the list of
 * medias that have changed
 * @change_type: the type of change
 * @location_unknown: if change has happpened in @media or any descendant
 *
 * Emits "content-changed" signal to notify subscribers that a change ocurred
 * in @source.
 *
 * The function will take ownership of @changed medias and it should not be
 * manipulated in any way by the caller after invoking this function. If that is
 * needed, the caller must ref the array in advance.
 *
 * See GrlMediaSource::content-changed signal.
 *
 * <note>
 *  <para>
 *    This function is intended to be used only by plugins.
 *  </para>
 * </note>
 *
 * Since: 0.1.14
 */
void grl_media_source_notify_change_list (GrlMediaSource *source,
                                          GPtrArray *changed_medias,
                                          GrlMediaSourceChangeType change_type,
                                          gboolean location_unknown)
{
  const gchar *source_id;

  g_return_if_fail (GRL_IS_MEDIA_SOURCE (source));
  g_return_if_fail (changed_medias);

  /* Set the source */
  source_id = grl_metadata_source_get_id (GRL_METADATA_SOURCE (source));
  g_ptr_array_foreach (changed_medias,
                       (GFunc) grl_media_set_source,
                       (gpointer) source_id);

  /* Add hook to free content when freeing the array */
  g_ptr_array_set_free_func (changed_medias, (GDestroyNotify) g_object_unref);

  g_signal_emit (source,
                 registry_signals[SIG_CONTENT_CHANGED],
                 0,
                 changed_medias,
                 change_type,
                 location_unknown);

  g_ptr_array_unref (changed_medias);
}

/**
 * grl_media_source_notify_change:
 * @source: a media source
 * @media: (allow-none): the media which has changed, or @NULL to use the root box.
 * @change_type: the type of change
 * @location_unknown: if change has happened in @media or any descendant
 *
 * Emits "content-changed" signal to notify subscribers that a change ocurred
 * in @source.
 *
 * See #grl_media_source_notify_change_list() function.
 *
 * <note>
 *  <para>
 *    This function is intended to be used only by plugins.
 *  </para>
 * </note>
 *
 * Since: 0.1.9
 */
void grl_media_source_notify_change (GrlMediaSource *source,
                                     GrlMedia *media,
                                     GrlMediaSourceChangeType change_type,
                                     gboolean location_unknown)
{
  GPtrArray *ptr_array;

  g_return_if_fail (GRL_IS_MEDIA_SOURCE (source));

  if (!media) {
    media = grl_media_box_new ();
  } else {
    g_object_ref (media);
  }

  ptr_array = g_ptr_array_sized_new (1);
  g_ptr_array_add (ptr_array, media);

  grl_media_source_notify_change_list (source, ptr_array,
                                       change_type, location_unknown);
}
