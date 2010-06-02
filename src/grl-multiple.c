/*
 * Copyright (C) 2010 Igalia S.L.
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

#include "grl-multiple.h"
#include "grl-plugin-registry.h"
#include "grl-error.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "grl-multiple"

struct MultipleSearchData {
  GHashTable *table;
  guint remaining;
  GList *search_ids;
  GList *sources;
  guint search_id;
  gboolean cancelled;
  GrlMediaSourceResultCb user_callback;
  gpointer user_data;
};

struct CallbackData {
  GrlMediaSourceResultCb user_callback;
  gpointer user_data;
};

/* ================= Globals ================= */

static GHashTable *pending_operations = NULL;
static gint multiple_search_id = 1;

/* ================ Utitilies ================ */

static void
free_multiple_search_data (struct MultipleSearchData *msd)
{
  g_hash_table_unref (msd->table);
  g_list_free (msd->search_ids);
  g_free (msd);
}

static gboolean
handle_no_searchable_sources_idle (gpointer user_data)
{
  GError *error;
  struct CallbackData *callback_data = (struct CallbackData *) user_data;

  error = g_error_new (GRL_ERROR, GRL_ERROR_SEARCH_FAILED, 
                       "No searchable sources available");
  callback_data->user_callback (NULL, 0, NULL, 0, callback_data->user_data, error);

  g_error_free (error);
  g_free (callback_data);

  return FALSE;
}

static void
handle_no_searchable_sources (GrlMediaSourceResultCb callback, gpointer user_data)
{
  struct CallbackData *callback_data = g_new0 (struct CallbackData, 1);
  callback_data->user_callback = callback;
  callback_data->user_data = user_data;
  g_idle_add (handle_no_searchable_sources_idle, callback_data);
}

static void
multiple_search_cb (GrlMediaSource *source,
		    guint search_id,
		    GrlMedia *media,
		    guint remaining,
		    gpointer user_data,
		    const GError *error)
{
  g_debug ("multiple_search_cb");

  struct MultipleSearchData *msd = (struct MultipleSearchData *) user_data;
  guint source_remaining;
  guint diff;
  gboolean emit;

  g_debug ("multiple:remaining == %u, source:remaining = %u (%s)",
	     msd->remaining, remaining,
	     grl_metadata_source_get_name (GRL_METADATA_SOURCE (source)));

  /* --- Update remaining count --- */

  source_remaining = GPOINTER_TO_INT (g_hash_table_lookup (msd->table,
							   (gpointer) source));
  if (remaining != -1) {
    diff = source_remaining - remaining;
    g_hash_table_insert (msd->table, source, GINT_TO_POINTER (remaining - 1));
  } else {
    diff = 0;
    g_hash_table_insert (msd->table, source, GINT_TO_POINTER (source_remaining - 1));
  }

  msd->remaining -= diff;

  /* --- Manage NULL results --- */

  if (remaining == 0 && media == NULL && msd->remaining > 0) {
    /* A source emitted a NULL result to finish its search operation
       we don't want to relay this to the client (unless this is the
       last one in the multiple search) */
    emit = FALSE;
  } else {
    emit = TRUE;
  }

  /* --- Cancelation management --- */

  if (msd->cancelled) {
    g_debug ("operation is cancelled or already finished, skipping result!");
    if (media) {
      g_object_unref (media);
      media = NULL;
    }
    if (msd->remaining > 0) {
      /* Do not emit if operation was cancelled, only let the last 
         result through */
      emit = FALSE;
    }
  }

  /* --- Result emission --- */
  if (emit) {
    msd->user_callback (source,
  		        msd->search_id,
 		        media,
		        msd->remaining,
		        msd->user_data,
		        NULL);
  }

  if (msd->remaining == 0) {
    g_debug ("Multiple operation finished (%u)", msd->search_id);
    free_multiple_search_data (msd);
  } else {
    /* Next remaining value expected is one unit less */
    msd->remaining--;
  }
}

/* ================ API ================ */

guint
grl_multiple_search (const gchar *text,
		     const GList *keys,
		     guint count,
		     GrlMetadataResolutionFlags flags,
		     GrlMediaSourceResultCb callback,
		     gpointer user_data)
{
  GrlPluginRegistry *registry;
  GrlMediaPlugin **sources;
  guint individual_count, first_count, source_count;
  guint search_id, n;
  struct MultipleSearchData *msd;

  g_debug ("grl_multiple_search");

  g_return_val_if_fail (count > 0, 0);
  g_return_val_if_fail (text != NULL, 0);
  g_return_val_if_fail (callback != NULL, 0);

  if (!pending_operations) {
    pending_operations = g_hash_table_new (g_direct_hash, g_direct_equal);
  }

  registry = grl_plugin_registry_get_instance ();
  sources = grl_plugin_registry_get_sources_by_operations (registry,
							   GRL_OP_SEARCH,
							   TRUE);

  /* No searchable sources? */
  if (sources[0] == NULL) {
    g_free (sources);
    handle_no_searchable_sources (callback, user_data);
    return 0;
  }

  /* Prepare operation */
  for (n = 0; sources[n] != NULL; n++);
  individual_count = count / n;
  first_count = individual_count + count % n;
  source_count = n;

  msd = g_new0 (struct MultipleSearchData, 1);
  msd->table = g_hash_table_new (g_direct_hash, g_direct_equal);
  msd->remaining = count - 1;
  msd->user_callback = callback;
  msd->user_data = user_data;
  msd->search_id = multiple_search_id++;

  /* Execute multiple search */
  for (n = 0; sources[n]; n++) {
    GrlMediaSource *source;
    guint c, id;

    source = GRL_MEDIA_SOURCE (sources[n]);

    if (n == 0) {
      c = first_count;
    } else {
      c = individual_count;
    }

    if (c > 0) {
      g_hash_table_insert (msd->table, source, GINT_TO_POINTER (c - 1));
      id = grl_media_source_search (source,
				    text,
				    keys,
				    0, c,
				    flags,
				    multiple_search_cb,
				    msd);
      g_debug ("Operation %u: Searching %u items in %s",
	       id, c, grl_metadata_source_get_name (GRL_METADATA_SOURCE (source)));

      msd->search_ids = g_list_prepend (msd->search_ids, GINT_TO_POINTER (id));
      msd->sources = g_list_prepend (msd->sources, source);
    }
  }

  g_hash_table_insert (pending_operations, GINT_TO_POINTER (msd->search_id),
                       msd);

  g_free (sources);

  return msd->search_id;
}

void
grl_multiple_cancel (guint search_id)
{
  g_debug ("grl_multiple_cancel");
  
  struct MultipleSearchData *msd;
  GList *sources, *ids;

  if (!pending_operations) {
    return;
  }

  msd = (struct MultipleSearchData *)
    g_hash_table_lookup (pending_operations, GINT_TO_POINTER (search_id));
  if (!msd) {
    g_debug ("Tried to cancel invalid or already cancelled "\
	     "multiple operation. Skipping...");
    return;
  }

  sources = msd->sources;
  ids = msd->search_ids;
  while (sources) {
    grl_media_source_cancel (GRL_MEDIA_SOURCE (sources->data),
                             GPOINTER_TO_INT (ids->data));
    sources = g_list_next (sources);
    ids = g_list_next (ids);
  }

  msd->cancelled = TRUE;
}
