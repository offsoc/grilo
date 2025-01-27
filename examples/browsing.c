
/*
 * Browsing in Grilo.
 * Shows the first 5 elements of each browsable source
 */

#include <grilo.h>

#define GRL_LOG_DOMAIN_DEFAULT  example_log_domain
GRL_LOG_DOMAIN_STATIC(example_log_domain);

/* This callback is invoked for each result that matches our
   browse operation. The arguments are:
   1) The source we obtained the content from.
   2) The operation identifier this result relates to.
   3) A media object representing content that matched the browse operation.
   4) Estimation of the number of remaining media objects that will be sent
   after this one as part of the same resultset (0 means that the browse
   operation is finished).
   5) User data passed to the grl_media_source_browse method.
   6) A GError if an error happened, NULL otherwise */
static void
browse_cb (GrlMediaSource *source,
	   guint browse_id,
	   GrlMedia *media,
	   guint remaining,
	   gpointer user_data,
	   const GError *error)
{
  /* First we check if the operation failed for some reason */
  if (error) {
    g_error ("Browse operation failed. Reason: %s", error->message);
  }

  /* Check if we got a valid media object as some plugins may call the callback
     with a NULL media under certain circumstances (for example when they
     cannot estimate the number of remaining results and they find suddenly they
     don't have any more results to send) */
  if (media) {
    /* Get the metadata we are interested in */
    const gchar *title = grl_media_get_title (media);

    /* If the media is a container (box) that means we could
       browse it again (that is, we could use it as the second parameter
       of the grl_media_source_browse method) */
    if (GRL_IS_MEDIA_BOX (media)) {
      guint childcount = grl_media_box_get_childcount (GRL_MEDIA_BOX (media));
      g_debug ("\t Got '%s' (container with %d elements)", title, childcount);
    } else {
      guint seconds = grl_media_get_duration (media);
      const gchar *url = grl_media_get_url (media);
      g_debug ("\t Got '%s' (media - length: %d seconds)", title, seconds);
      g_debug ("\t\t URL: %s", url);
    }
    g_object_unref (media);
  }

  /* Check if this was the last result */
  if (remaining == 0) {
    g_debug ("Browse operation finished!");
  }
}

static void
source_added_cb (GrlPluginRegistry *registry, gpointer user_data)
{
  static gboolean first = TRUE;
  GrlMetadataSource *source = GRL_METADATA_SOURCE (user_data);
  GList * keys = grl_metadata_key_list_new (GRL_METADATA_KEY_TITLE,
					    GRL_METADATA_KEY_DURATION,
					    GRL_METADATA_KEY_URL,
					    GRL_METADATA_KEY_CHILDCOUNT,
					    NULL);
  g_debug ("Detected new source available: '%s'",
	   grl_metadata_source_get_name (source));

  /* We will just issue a browse operation on the first browseble
     source we find */
  if (first &&
      grl_metadata_source_supported_operations (source) & GRL_OP_BROWSE) {
    first = FALSE;
    g_debug ("Browsing source: %s", grl_metadata_source_get_name (source));
    /* Here is how you can browse a source, you have to provide:
       1) The source you want to browse contents from.
       2) The container object you want to browse (NULL for the root container)
       3) A list of metadata keys we are interested in.
       4) Flags to control certain aspects of the browse operation.
       5) A callback that the framework will invoke for each available result
       6) User data for the callback
       It returns an operation identifier that you can use to match results
       with the corresponding request (we ignore it here) */
    grl_media_source_browse (GRL_MEDIA_SOURCE (source),
			     NULL,
			     keys,
			     0, 5,
			     GRL_RESOLVE_IDLE_RELAY,
			     browse_cb,
			     NULL);
  }

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
