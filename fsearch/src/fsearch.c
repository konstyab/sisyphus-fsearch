/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <linux/limits.h>
#include <string.h>
#include <stdint.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include "fsearch.h"
#include "fsearch_config.h"
#include "list_model.h"
#include "resources.h"
#include "clipboard.h"
#include "utils.h"
#include "database.h"
#include "database_search.h"
#include "ui_utils.h"
#include "preferences_ui.h"
#include "fsearch_window.h"
#include "debug.h"

struct _FsearchApplication
{
    GtkApplication parent;
    Database *db;
    DatabaseSearch *search;
    FsearchConfig *config;
    FsearchThreadPool *pool;

    ListModel *list_model;
    gint sb_context_id;
    bool initialized;

    GMutex mutex;
};

enum {
    DATABASE_UPDATE,
    DATABASE_UPDATED,
    LAST_SIGNAL
};

static guint signals [LAST_SIGNAL];

G_DEFINE_TYPE (FsearchApplication, fsearch_application, GTK_TYPE_APPLICATION)

Database *
fsearch_application_get_db (FsearchApplication *fsearch)
{
    g_assert (FSEARCH_IS_APPLICATION (fsearch));
    return fsearch->db;
}

FsearchThreadPool *
fsearch_application_get_thread_pool (FsearchApplication *fsearch)
{
    g_assert (FSEARCH_IS_APPLICATION (fsearch));
    return fsearch->pool;
}

FsearchConfig *
fsearch_application_get_config (FsearchApplication *fsearch)
{
    g_assert (FSEARCH_IS_APPLICATION (fsearch));
    return fsearch->config;
}

gboolean
update_db_cb (gpointer user_data)
{
    char *text = user_data;
    if (!text) {
        return FALSE;
    }

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    GList *windows = gtk_application_get_windows (GTK_APPLICATION (app));

    for (; windows; windows = windows->next) {
        GtkWindow *window = windows->data;

        if (FSEARCH_WINDOW_IS_WINDOW (window))
        {
            fsearch_application_window_update_database_label ((FsearchApplicationWindow *) window, text);
        }
    }

    free (text);
    text = NULL;

    return FALSE;
}

void
build_location_callback (const char *text)
{
    if (text) {
        g_idle_add (update_db_cb, g_strdup (text));
    }
}

static bool
make_location_dir (void)
{
    gchar config_dir[PATH_MAX] = "";
    build_config_dir (config_dir, sizeof (config_dir));
    gchar location_dir[PATH_MAX] = "";
    snprintf (location_dir, sizeof (location_dir), "%s/%s", config_dir, "database");
    return !g_mkdir_with_parents (location_dir, 0700);
}

static void
fsearch_application_init (FsearchApplication *app)
{
    make_config_dir ();
    make_location_dir ();
    app->config = calloc (1, sizeof (FsearchConfig));
    if (!load_config (app->config)) {
        if (!load_default_config (app->config)) {
        }
    }
    app->db = NULL;
    app->search = NULL;
    app->initialized = false;
    app->sb_context_id = -1;
    g_mutex_init (&app->mutex);
}

static void
fsearch_application_shutdown (GApplication *app)
{
    g_assert (FSEARCH_IS_APPLICATION (app));
    FsearchApplication *fsearch = FSEARCH_APPLICATION (app);

    GtkWindow *window = NULL;
    GList *windows = gtk_application_get_windows (GTK_APPLICATION (app));

    for (; windows; windows = windows->next) {
        window = windows->data;

        if (FSEARCH_WINDOW_IS_WINDOW (window)) {
            fsearch_application_window_prepare_shutdown (window);
        }
    }

    if (fsearch->db) {
        db_save_locations (fsearch->db);
        db_clear (fsearch->db);
    }
    if (fsearch->pool) {
        fsearch_thread_pool_free (fsearch->pool);
    }
    save_config (fsearch->config);
    config_free (fsearch->config);
    g_mutex_clear (&fsearch->mutex);
    G_APPLICATION_CLASS (fsearch_application_parent_class)->shutdown (app);
}

static void
fsearch_application_finalize (GObject *object)
{
    G_OBJECT_CLASS (fsearch_application_parent_class)->finalize (object);
}

static gboolean
updated_database_signal_emit_cb (gpointer user_data)
{
    FsearchApplication *self = FSEARCH_APPLICATION (user_data);
    //update_windows_after_db_update (self);
    g_signal_emit (self, signals [DATABASE_UPDATED], 0);
    return G_SOURCE_REMOVE;
}

static gboolean
update_database_signal_emit_cb (gpointer user_data)
{
    FsearchApplication *self = FSEARCH_APPLICATION (user_data);
    g_signal_emit (self, signals [DATABASE_UPDATE], 0);
    return G_SOURCE_REMOVE;
}

static void
prepare_windows_for_db_update (FsearchApplication *app)
{
    GtkWindow *window = NULL;
    GList *windows = gtk_application_get_windows (GTK_APPLICATION (app));

    for (; windows; windows = windows->next) {
        window = windows->data;

        if (FSEARCH_WINDOW_IS_WINDOW (window)) {
            fsearch_application_window_remove_model (window);
        }
    }
    return;
}

#ifdef DEBUG
static struct timeval tm1;
#endif

static inline void start()
{
#ifdef DEBUG
    gettimeofday(&tm1, NULL);
#endif
}

static inline void stop()
{
#ifdef DEBUG
    struct timeval tm2;
    gettimeofday(&tm2, NULL);

    unsigned long long t = 1000 * (tm2.tv_sec - tm1.tv_sec) + (tm2.tv_usec - tm1.tv_usec) / 1000;
    trace ("%llu ms\n", t);
#endif
}

static gpointer
load_database (gpointer user_data)
{

    g_assert (user_data != NULL);
    g_assert (FSEARCH_IS_APPLICATION (user_data));
    FsearchApplication *app = FSEARCH_APPLICATION (user_data);
    app->initialized = false;
    g_idle_add (update_database_signal_emit_cb, app);

    if (!app->db) {
        // create new database
        start ();
        app->db = db_database_new ();

        bool loaded = false;
        bool build_new = false;
        for (GList *l = app->config->locations; l != NULL; l = l->next) {
            if (app->config->update_database_on_launch) {
                if (db_location_build_new (app->db, l->data, build_location_callback)) {
                    loaded = true;
                    build_new = true;
                }
            }
            else {
                if (!db_location_load (app->db, l->data)) {
                    if (db_location_build_new (app->db, l->data, build_location_callback)) {
                        loaded = true;
                        build_new = true;
                    }
                }
                else {
                    loaded = true;
                }
            }
        }
        if (loaded) {
            if (build_new) {
                db_build_initial_entries_list (app->db);
            }
            else {
                db_update_entries_list (app->db);
            }
        }
        trace ("loaded db in:");
        stop ();
    }
    else {
        trace ("update\n");
        start ();
        db_clear (app->db);
        if (app->config->locations) {
            for (GList *l = app->config->locations; l != NULL; l = l->next) {
                db_location_build_new (app->db, l->data, build_location_callback);
            }
            db_build_initial_entries_list (app->db);
        }
        trace ("loaded db in:");
        stop ();
    }
    uint32_t num_items = db_get_num_entries (app->db);
    app->initialized = true;

    gchar sb_text[100] = "";
    snprintf (sb_text, sizeof (sb_text), "Database loaded (%'d Items)", num_items);

    g_idle_add (updated_database_signal_emit_cb, app);

    return NULL;
}

static GThread *
load_database_thread (FsearchApplication *app)
{
    return g_thread_new("update_database", load_database, app);
}

static void
about_activated (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       app)
{
    g_assert (FSEARCH_IS_APPLICATION (app));
    GList *windows = gtk_application_get_windows (GTK_APPLICATION (app));

    for (; windows; windows = windows->next)
    {
        GtkWindow *window = windows->data;
        gtk_show_about_dialog (GTK_WINDOW (window),
                               "program-name", PACKAGE_NAME,
                               "logo-icon-name", "system-search",
                               "license-type", GTK_LICENSE_GPL_2_0,
                               "copyright", "Christian Boxdörfer",
                               "website", PACKAGE_URL,
                               "version", PACKAGE_VERSION,
                               "comments", _("A search utility focusing on performance and advanced features"),
                               NULL);
        break;
    }
}

static void
quit_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       app)
{
    g_application_quit (G_APPLICATION (app));
}

static void
preferences_activated (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       app)
{
    g_assert (FSEARCH_IS_APPLICATION (app));
    GList *windows = gtk_application_get_windows (GTK_APPLICATION (app));

    for (; windows; windows = windows->next) {
        GtkWindow *window = windows->data;
        preferences_ui_launch (FSEARCH_APPLICATION (app)->config, window);
        break;
    }
}

void
update_database (void)
{
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    prepare_windows_for_db_update (app);
    g_thread_new("update_database", load_database, app);
    return;
}

static void
update_database_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       app)
{
    update_database ();
}

static void
new_window_activated (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       app)
{
    GtkWindow *window = GTK_WINDOW (fsearch_application_window_new (FSEARCH_APPLICATION (app)));
    gtk_window_present (window);
}

static GActionEntry app_entries[] =
{
    { "new_window", new_window_activated, NULL, NULL, NULL },
    { "about", about_activated, NULL, NULL, NULL },
    { "update_database", update_database_activated, NULL, NULL, NULL },
    { "preferences", preferences_activated, NULL, NULL, NULL },
    { "quit", quit_activated, NULL, NULL, NULL }
};

static void
fsearch_application_startup (GApplication* app)
{
    g_assert (FSEARCH_IS_APPLICATION (app));
    G_APPLICATION_CLASS (fsearch_application_parent_class)->startup (app);

    FsearchApplication *fsearch = FSEARCH_APPLICATION (app);
    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme",
                 fsearch->config->enable_dark_theme,
                 NULL );

    g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   app_entries, G_N_ELEMENTS (app_entries),
                                   app);

    static const gchar *toggle_focus[] = { "Tab", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.toggle_focus", toggle_focus);
    static const gchar *search[] = { "<control>f", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.focus_search", search);
    static const gchar *new_window[] = { "<control>n", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), "app.new_window", new_window);
    static const gchar *hide_window[] = { "Escape", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.hide_window", hide_window);
    static const gchar *show_menubar[] = { "<control>m", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.show_menubar", show_menubar);
    static const gchar *match_case[] = { "<control>i", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.match_case", match_case);
    static const gchar *search_mode[] = { "<control>r", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.search_mode", search_mode);
    static const gchar *search_in_path[] = { "<control>u", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.search_in_path", search_in_path);
    static const gchar *quit[] = { "<control>q", NULL };
    gtk_application_set_accels_for_action (GTK_APPLICATION (app), "app.quit", quit);
    FSEARCH_APPLICATION (app)->pool = fsearch_thread_pool_init ();
}

static void
fsearch_application_activate (GApplication *app)
{
    g_assert (FSEARCH_IS_APPLICATION (app));

    GtkWindow *window = NULL;
    GList *windows = gtk_application_get_windows (GTK_APPLICATION (app));

    for (; windows; windows = windows->next) {
        window = windows->data;

        if (FSEARCH_WINDOW_IS_WINDOW (window)) {
            GtkWidget *entry = GTK_WIDGET (fsearch_application_window_get_search_entry ((FsearchApplicationWindow *) window));
            if (entry) {
            gtk_widget_grab_focus (entry);
            }
            gtk_window_present (window);
            return;
        }
    }
    window = GTK_WINDOW (fsearch_application_window_new (FSEARCH_APPLICATION (app)));
    gtk_window_present (window);
    load_database_thread (FSEARCH_APPLICATION (app));
}

static void
fsearch_application_class_init (FsearchApplicationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GApplicationClass *g_app_class = G_APPLICATION_CLASS (klass);

    object_class->finalize = fsearch_application_finalize;

    g_app_class->activate = fsearch_application_activate;
    g_app_class->startup = fsearch_application_startup;
    g_app_class->shutdown = fsearch_application_shutdown;

    signals [DATABASE_UPDATE] =
        g_signal_new ("database-update",
                G_TYPE_FROM_CLASS (klass),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL, NULL,
                G_TYPE_NONE,
                0);

    signals [DATABASE_UPDATED] =
        g_signal_new ("database-updated",
                G_TYPE_FROM_CLASS (klass),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL, NULL,
                G_TYPE_NONE,
                0);
}


FsearchApplication *
fsearch_application_new (void)
{
    return g_object_new (FSEARCH_APPLICATION_TYPE,
                         "application-id",
                         "org.fsearch.fsearch",
                         "flags",
                         G_APPLICATION_HANDLES_OPEN,
                         NULL);
}
