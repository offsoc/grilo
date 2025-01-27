
/*
 * Shows how to get content in an efficient way.
 * Search 'rock' content in all searchable sources.
 */

#include <grilo.h>
#include <string.h>
#include <stdlib.h>

#define GRL_LOG_DOMAIN_DEFAULT  example_log_domain
GRL_LOG_DOMAIN_STATIC(example_log_domain);

const gchar *target_source_id = NULL;

static void
metadata_cb (GrlMediaSource *source,
             guint metadata_id,
	     GrlMedia *media,
	     gpointer user_data,
	     const GError *error)
{
  if (error)
    g_error ("Metadata operation failed. Reason: %s", error->message);

  const gchar *url = grl_media_get_url (media);
  g_debug ("\tURL: %s", url);
  g_object_unref (media);
  exit (0);
}

static void
search_cb (GrlMediaSource *source,
	   guint browse_id,
	   GrlMedia *media,
	   guint remaining,
	   gpointer user_data,
	   const GError *error)
{
  if (error)
    g_error ("Search operation failed. Reason: %s", error->message);

  if (!media) {
    g_error ("No media items found matching the text \"rock\"!");
    return;
  }

  g_debug ("Got matching media from %s. Details:", target_source_id);
  const gchar *title = grl_media_get_title (media);
  g_debug ("\tTitle: %s", title);
  const gchar *url = grl_media_get_url (media);
  if (url) {
    g_debug ("\tURL: %s:", url);
    g_object_unref (media);
    exit (0);
  } else {
    g_debug ("URL no available, trying with slow keys now");
    GList *keys = grl_metadata_key_list_new (GRL_METADATA_KEY_URL, NULL);
    grl_media_source_metadata (source,
			       media,
			       keys,
			       GRL_RESOLVE_IDLE_RELAY,
			       metadata_cb,
			       NULL);
    g_list_free (keys);
  }
}

static void
source_added_cb (GrlPluginRegistry *registry, gpointer user_data)
{
  GrlMetadataSource *source = GRL_METADATA_SOURCE (user_data);
  const gchar *source_id = grl_metadata_source_get_id (source);

  /* We are looking for one source in particular */
  if (strcmp (source_id, target_source_id))
    return;

  GList *keys = grl_metadata_key_list_new (GRL_METADATA_KEY_TITLE,
					   GRL_METADATA_KEY_URL,
					   NULL);

  /* The source must be searchable */
  if (!(grl_metadata_source_supported_operations (source) & GRL_OP_SEARCH))
    g_error ("Source %s is not searchable!", source_id);

  /* Retrieve the first media from the source matching the text "rock" */
  g_debug ("Searching \"rock\" in \"%s\"", source_id);
  grl_media_source_search (GRL_MEDIA_SOURCE (source),
			   "rock",
			   keys,
			   0, 1,
			   GRL_RESOLVE_IDLE_RELAY | GRL_RESOLVE_FAST_ONLY,
			   search_cb,
			   NULL);
  g_list_free (keys);
}

static void
configure_plugins (void)
{
  GrlConfig *config;
  GrlPluginRegistry *registry;

  /* Let's configure only the Youtube plugin (only requires an API key) */
  config = grl_config_new ("grl-youtube", NULL);
  grl_config_set_api_key (config,
                          "AI39si4EfscPllSfUy1IwexMf__kntTL_G5dfSr2iUEVN45RHG"
                          "q92Aq0lX25OlnOkG6KTN-4soVAkAf67fWYXuHfVADZYr7S1A");
  registry = grl_plugin_registry_get_default ();
  grl_plugin_registry_add_config (registry, config, NULL);
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

  if (argc != 2) {
    g_print ("Please specify id of the source to search " \
	     "(example: grl-youtube)\n");
    exit (1);
  } else {
    target_source_id = argv[1];
  }

  GRL_LOG_DOMAIN_INIT (example_log_domain, "example");
  configure_plugins ();
  load_plugins ();
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  return 0;
}
