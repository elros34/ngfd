/*
 * ngfd - Non-graphic feedback daemon
 *
 * Copyright (C) 2010 Nokia Corporation.
 * Contact: Xun Chen <xun.chen@nokia.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <glib.h>
#include <glib-unix.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include <ngf/log.h>
#include "core-internal.h"

#define LOG_CAT "core: "

#define EVENT_RELOAD_TIME_LIMIT_US (2 * G_USEC_PER_SEC)

typedef struct _AppData
{
    GMainLoop *loop;
    NCore     *core;
    gint       default_loglevel;
    guint      sigusr1_source;
    guint      sigusr2_source;
    guint      sigint_source;
    guint      sigterm_source;
    gint64     last_event_reload;
} AppData;

static gboolean
parse_cmdline (int argc, char **argv, AppData *app)
{
    int opt, opt_index;
    int level = app->default_loglevel;

    static struct option long_opts[] = {
        { "verbose", 0, 0, 'v' },
        { 0, 0, 0, 0 }
    };

    while ((opt = getopt_long (argc, argv, "v", long_opts, &opt_index)) != -1) {
        switch (opt) {
            case 'v':
                if (level)
                    level--;
                break;

            default:
                break;
        }
    }

    app->default_loglevel = level;
    n_log_set_level (level);

    return TRUE;
}

static void
quit (AppData *app)
{
    if (app && app->loop)
        g_main_loop_quit (app->loop);
}

static gboolean
handle_sigint (gpointer userdata)
{
    AppData *app = userdata;

    quit (app);
    app->sigint_source = 0;

    return FALSE;
}

static gboolean
handle_sigterm (gpointer userdata)
{
    AppData *app = userdata;

    quit (app);
    app->sigterm_source = 0;

    return FALSE;
}

static gboolean
handle_sigusr1 (gpointer userdata)
{
    AppData *app = userdata;

    if (n_log_get_target () == N_LOG_TARGET_SYSLOG) {
        n_log_set_target (N_LOG_TARGET_STDOUT);
        n_log_set_level  (app->default_loglevel);
    }
    else {
        n_log_set_target (N_LOG_TARGET_SYSLOG);
        n_log_set_level  (N_LOG_LEVEL_ENTER);
    }

    return TRUE;
}

/* Reload all event definitions */
static gboolean
handle_sigusr2 (gpointer userdata)
{
    AppData *app = userdata;

    if (app->last_event_reload + EVENT_RELOAD_TIME_LIMIT_US < g_get_real_time ()) {
        N_INFO ("daemon: event reload requested.");
        n_core_reload_events (app->core);
        app->last_event_reload = g_get_real_time ();
    }

    return TRUE;
}

static void
install_signal_handlers (AppData *app)
{
    app->sigusr1_source = g_unix_signal_add (SIGUSR1,
                                             handle_sigusr1,
                                             app);
    app->sigusr2_source = g_unix_signal_add (SIGUSR2,
                                             handle_sigusr2,
                                             app);
    app->sigterm_source = g_unix_signal_add (SIGTERM,
                                             handle_sigterm,
                                             app);
    app->sigint_source  = g_unix_signal_add (SIGINT,
                                             handle_sigint,
                                             app);
}

static void
remove_signal_handlers (AppData *app)
{
    if (app->sigusr1_source)
        g_source_remove (app->sigusr1_source), app->sigusr1_source = 0;

    if (app->sigusr2_source)
        g_source_remove (app->sigusr2_source), app->sigusr2_source = 0;

    if (app->sigterm_source)
        g_source_remove (app->sigterm_source), app->sigterm_source = 0;

    if (app->sigint_source)
        g_source_remove (app->sigint_source), app->sigint_source = 0;
}

int
main (int argc, char *argv[])
{
    AppData app;

    memset (&app, 0, sizeof (app));
    app.default_loglevel = N_LOG_LEVEL_NONE;
    n_log_initialize (app.default_loglevel);

    if (!parse_cmdline (argc, argv, &app))
        return 1;

    N_DEBUG ("daemon: Starting.");
    app.loop = g_main_loop_new (NULL, 0);
    app.core = n_core_new (&argc, argv);

    if (!n_core_initialize (app.core)) {
        N_ERROR ("daemon: Initialization failed.");
        return 2;
    }

    install_signal_handlers (&app);

    N_DEBUG ("daemon: Startup complete.");
    g_main_loop_run   (app.loop);
    N_DEBUG ("daemon: Shutdown initiated.");
    remove_signal_handlers (&app);
    n_core_shutdown   (app.core);
    n_core_free       (app.core);
    g_main_loop_unref (app.loop);
    N_DEBUG ("daemon: Terminated.");

    return 0;
}
