
/*
 * Searching in Grilo.
 * Search all media in Jamendo with the word 'rock'.
 */

#include <grilo.h>
#include <string.h>

#define GRL_LOG_DOMAIN_DEFAULT  example_log_domain
GRL_LOG_DOMAIN_STATIC(example_log_domain);

static void
search_cb (GrlMediaSource *source,
	   guint browse_id,
	   GrlMedia *media,
	   guint remaining,
	   gpointer user_data,
	   const GError *error)
{
  if (error) {
    g_error ("Search operation failed. Reason: %s", error->message);
  }

  if (media) {
    const gchar *title = grl_media_get_title (media);
    if (GRL_IS_MEDIA_BOX (media)) {
      guint childcount = grl_media_box_get_childcount (GRL_MEDIA_BOX (media));
      g_debug ("\t Got '%s' (container with %d elements)", title, childcount);
    } else {
      guint seconds = grl_media_get_duration (media);
      const gchar *url = grl_media_get_url (media);
      g_debug ("\t Got '%s' (media - length: %d seconds)", title, seconds);
      g_debug ("\t\t URL: %s", url);
    }
  }

  if (remaining == 0) {
    g_debug ("Search operation finished!");
  } else {
    g_debug ("\t%d results remaining!", remaining);
  }

  g_object_unref (media);
}

static void
source_added_cb (GrlPluginRegistry *registry, gpointer user_data)
{
  const gchar *id;
  GrlMetadataSource *source = GRL_METADATA_SOURCE (user_data);
  GList * keys = grl_metadata_key_list_new (GRL_METADATA_KEY_TITLE,
					    GRL_METADATA_KEY_DURATION,
					    GRL_METADATA_KEY_CHILDCOUNT,
					    NULL);

  /* Not interested if not searchable */
  if (!(grl_metadata_source_supported_operations (source) & GRL_OP_SEARCH))
    return;

  g_debug ("Detected new searchable source available: '%s'",
	   grl_metadata_source_get_name (source));

  /* Only interested in Jamendo */
  id = grl_metadata_source_get_id (source);
  if (strcmp (id, "grl-jamendo"))
    return;

  g_debug ("Searching \"rock\" in Jamendo");
  grl_media_source_search (GRL_MEDIA_SOURCE (source),
			   "rock",
			   keys,
			   0, 5,
			   GRL_RESOLVE_IDLE_RELAY,
			   search_cb,
			   NULL);

  g_list_free (keys);
}

static void
load_plugins (void)
{
  GrlPluginRegistry *registry;
  GError *error = NULL;

  registry = grl_plugin_registry_get_default ();
  g_signal_connect (registry, "source-added",
		    G_CALLBACK (source_added_cb), NULL);
  if (!grl_plugin_registry_load_all (registry, &error)) {
    g_error ("Failed to load plugins: %s", error->message);
  }
}

gint
main (int argc, gchar *argv[])
{
  GMainLoop *loop;
  grl_init (&argc, &argv);
  GRL_LOG_DOMAIN_INIT (example_log_domain, "example");
  load_plugins ();
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  return 0;
}
