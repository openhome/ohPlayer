#include <stdio.h>
#include <stdlib.h>
#ifdef USE_GTK
#include <gtk/gtk.h>
#include <libnotify/notify.h>
#include <libappindicator/app-indicator.h>
#else // USE_GTK
#include <syslog.h>
#include <glib.h>
#endif // USE_GTK
#include <gio/gio.h>
#include <vector>

#include "CustomMessages.h"
#include "MediaPlayerIF.h"
#include "version.h"

#ifdef DEBUG
// Mtrace allocation tracing.
#include <mcheck.h>
#endif // DEBUG

#ifdef USE_NVWA
// New/Delete tracing.
#include "debug_new.h"
#endif // USE_NVWA

// Type definitions

enum class NotificationClassifiction
{
    INFO,
    WARNING
};

// Global variables

// System Tray Menu Items
#ifdef USE_GTK
static GtkWidget *g_mi_play     = NULL;
static GtkWidget *g_mi_pause    = NULL;
static GtkWidget *g_mi_stop     = NULL;
static GtkWidget *g_mi_networks = NULL;
static GtkWidget *g_mi_update   = NULL;
static GtkWidget *g_mi_about    = NULL;
static GtkWidget *g_mi_exit     = NULL;
static GtkWidget *g_mi_sep      = NULL;
static GtkWidget *g_mi_sep1     = NULL;
#else // USE_GTK
static GMainLoop *loop = NULL;
#endif // USE_GTK

static guint     g_mediaOptions     = 0;     // Available media playback options
static gboolean  g_updatesAvailable = false; // App updates availability.
static gchar    *g_updateLocation   = NULL;  // Update location URL.
static GThread  *g_mplayerThread    = NULL;  // Media Player thread
static InitArgs  g_mPlayerArgs;              // Media Player arguments.

#ifdef USE_GTK
static const gchar *g_light_icon_path = "/usr/share/openhome-player";
#ifdef USE_UNITY
static const gchar *g_light_icon_name = "OpenHome-Light-48x48";
#else // USE_UNITY
static const gchar *g_light_icon_name = "OpenHome-48x48";
#endif // USE_UNITY

static const gchar *g_icon_path =
                        "/usr/share/openhome-player/OpenHome-48x48.png";
#endif // USE_GTK

const gchar        *g_appName   = "OpenHomePlayer";

#ifdef USE_GTK
static void displayNotification(const gchar *summary,
                                const gchar *body,
                                NotificationClassifiction nClass)
{
    NotifyNotification *notification;
    GError             *error = NULL;
    const gchar        *icon;

    switch (nClass)
    {
        case NotificationClassifiction::INFO:
            icon = "dialog-information";
            break;
        case NotificationClassifiction::WARNING:
            icon = "dialog-warning";
            break;
    }

    // Create a notification with the appropriate icon.
    notification = notify_notification_new (summary, body, icon);

    // Set the notification duration to 3 seconds.
    notify_notification_set_timeout(notification, 3 * 1000);

    // Show the notification.
    if (!notify_notification_show (notification, &error)) {
        g_debug("Notification Show Failed: %s", error->message);
        g_error_free(error);
    }

    g_object_unref(G_OBJECT(notification));
}

// Context Menu Handlers.
static void playSelectionHandler()
{
    PipeLinePlay();
}

static void pauseSelectionHandler()
{
    PipeLinePause();
}

static void stopSelectionHandler()
{
    PipeLineStop();
}

static void aboutSelectionHandler()
{
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(g_icon_path, NULL);

    GtkWidget *dialog = gtk_about_dialog_new();

    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), g_appName);
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), CURRENT_VERSION);
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog),
      "(c) OpenHome");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
     "An example OpenHome Media Player Application.");
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog),
      "http://www.openhome.org");

    gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), pixbuf);
    g_object_unref(pixbuf);
    pixbuf = NULL;

    gtk_dialog_run(GTK_DIALOG (dialog));
    gtk_widget_destroy(dialog);
}

static void exitSelectionHandler()
{
    ExitMediaPlayer();

    gtk_main_quit();
}

static void updateSelectionHandler()
{
    // Show update dialogue.
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new(NULL,
                                    GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Install Updates ?");
    gtk_window_set_title(GTK_WINDOW(dialog), "Update");

    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    // Check result
    switch (result)
    {
        case GTK_RESPONSE_OK:
        {
            GError   *error   = NULL;
            gboolean  success = false;

            // Launch default browser to download update for installation.
            success = g_app_info_launch_default_for_uri(g_updateLocation,
                                                        NULL,
                                                       &error);

            if (success)
            {
                // Exit the application to allow update.
                exitSelectionHandler();
            }
            else
            {
                // Show error if update could not be retrieved
                dialog = gtk_message_dialog_new(NULL,
                        GTK_DIALOG_MODAL,
                        GTK_MESSAGE_INFO,
                        GTK_BUTTONS_OK,
                        "Cannot Retrieve Update");
                gtk_window_set_title(GTK_WINDOW(dialog), "Update");
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);

                // Free up any error
                g_error_free(error);
            }

            break;
        }
        default:
        {
            break;
        }
    }
}

static void NetworkSelectionHandler(GtkMenuItem * /*menuitem*/, gpointer args)
{
    TIpAddress subnet = {
        kFamilyV4,                // iFamily
        GPOINTER_TO_UINT(args),   // iV4
        { 255 }                   // iV6
    };

    // Restart the media player on the selected subnet.
    ExitMediaPlayer();

    // Wait on the Media Player thread to complete.
    g_thread_join(g_mplayerThread);

    /* Re-Register UPnP/OhMedia devices. */
    g_mPlayerArgs.restarted = true;
    g_mPlayerArgs.subnet    = subnet;

    g_mplayerThread = g_thread_new("MediaPlayerIF",
                                   (GThreadFunc)InitAndRunMediaPlayer,
                                   (gpointer)&g_mPlayerArgs);
}

// Keep the menu playback options in sync with current state of the
// media player.
static void UpdatePlaybackOptions()
{
    if (g_mi_play == NULL || g_mi_pause == NULL || g_mi_stop == NULL)
    {
        return;
    }

    if ((g_mediaOptions & MEDIAPLAYER_PLAY_OPTION) != 0)
    {
        gtk_widget_set_sensitive(g_mi_play,true);
    }
    else
    {
        gtk_widget_set_sensitive(g_mi_play,false);
    }

    if ((g_mediaOptions & MEDIAPLAYER_PAUSE_OPTION) != 0)
    {
        gtk_widget_set_sensitive(g_mi_pause,true);
    }
    else
    {
        gtk_widget_set_sensitive(g_mi_pause,false);
    }

    if ((g_mediaOptions & MEDIAPLAYER_STOP_OPTION) != 0)
    {
        gtk_widget_set_sensitive(g_mi_stop,true);
    }
    else
    {
        gtk_widget_set_sensitive(g_mi_stop,false);
    }
}

// Create a sub menu listing the available network adapters and their
// associated networks.
static void CreateNetworkAdapterSubmenu(GtkWidget *networkMenuItem)
{
    GtkWidget                  *submenu    = gtk_menu_new();
    std::vector<SubnetRecord*> *subnetList = NULL;

    // Get a list of available subnets from the media player..
    subnetList = GetSubnets();

    std::vector<SubnetRecord*>::iterator it;

    // Put each subnet in our submenu.
    for (it=subnetList->begin(); it < subnetList->end(); it++)
    {
        // Explicitly disable iPv6 (for now)
        if ((*it)->subnet.iFamily != kFamilyV6) {
            continue;
        }

        GtkWidget *subnet;

        subnet = gtk_menu_item_new_with_label((*it)->menuString->c_str());
        gtk_menu_shell_append (GTK_MENU_SHELL(submenu), subnet);
        gtk_widget_show (subnet);

        // Attach menuitem handler.
        g_signal_connect(G_OBJECT(subnet),
                         "activate",
                         G_CALLBACK(NetworkSelectionHandler),
                         GUINT_TO_POINTER((*it)->subnet.iV4));

        // If this is the subnet we are currently using disable it's
        // selection.
        if ((*it)->isCurrent)
        {
            gtk_widget_set_sensitive(subnet,FALSE);
        }
    }

    // Release any existing subnet list resources.
    FreeSubnets(subnetList);

    gtk_menu_item_set_submenu (GTK_MENU_ITEM(networkMenuItem), submenu);
}

// Create the context menu
static GtkMenu* tray_icon_on_menu()
{
    GtkMenu   *menu    = (GtkMenu*)gtk_menu_new();

    g_mi_play     = gtk_menu_item_new_with_label("Play");
    g_mi_pause    = gtk_menu_item_new_with_label("Pause");
    g_mi_stop     = gtk_menu_item_new_with_label("Stop");
    g_mi_networks = gtk_menu_item_new_with_label("Networks");
    g_mi_update   = gtk_menu_item_new_with_label("Update");
    g_mi_about    = gtk_menu_item_new_with_label("About");
    g_mi_exit     = gtk_menu_item_new_with_label("Exit");
    g_mi_sep      = gtk_separator_menu_item_new();
    g_mi_sep1     = gtk_separator_menu_item_new();

    gtk_menu_shell_append (GTK_MENU_SHELL(menu), g_mi_play);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), g_mi_pause);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), g_mi_stop);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), g_mi_sep);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), g_mi_networks);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), g_mi_sep1);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), g_mi_update);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), g_mi_about);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), g_mi_exit);

    gtk_widget_show (g_mi_play);
    gtk_widget_show (g_mi_pause);
    gtk_widget_show (g_mi_stop);
    gtk_widget_show (g_mi_networks);
    gtk_widget_show (g_mi_update);
    gtk_widget_show (g_mi_about);
    gtk_widget_show (g_mi_exit);
    gtk_widget_show (g_mi_sep);
    gtk_widget_show (g_mi_sep1);

    // Register menu item handlers.
    g_signal_connect(G_OBJECT(g_mi_play), "activate",
                              G_CALLBACK(playSelectionHandler), NULL);
    g_signal_connect(G_OBJECT(g_mi_pause), "activate",
                              G_CALLBACK(pauseSelectionHandler), NULL);
    g_signal_connect(G_OBJECT(g_mi_stop), "activate",
                              G_CALLBACK(stopSelectionHandler), NULL);
    g_signal_connect(G_OBJECT(g_mi_about), "activate",
                              G_CALLBACK(aboutSelectionHandler), NULL);
    g_signal_connect(G_OBJECT(g_mi_update), "activate",
                              G_CALLBACK(updateSelectionHandler), NULL);
    g_signal_connect(G_OBJECT(g_mi_exit), "activate",
                              G_CALLBACK(exitSelectionHandler), NULL);

    // Update the playback options in the UI
    UpdatePlaybackOptions();

    // Disable the 'Update' menu item until updates become available.
    if (g_updatesAvailable)
    {
        gtk_widget_set_sensitive(g_mi_update,true);
    }
    else
    {
        gtk_widget_set_sensitive(g_mi_update,false);
    }

    return menu;
}

static void create_tray_icon(AppIndicator **indicator)
{
    // Create the application indicator
    *indicator = app_indicator_new(g_appName,
                                   g_light_icon_name,
                                   APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

    app_indicator_set_icon_theme_path(*indicator, g_light_icon_path);

    app_indicator_set_status (*indicator, APP_INDICATOR_STATUS_ACTIVE);

    // Create the application context menu.
    GtkMenu *menu = tray_icon_on_menu();

    // Attach the menu to the indicator.
    app_indicator_set_menu(*indicator, menu);
}

gboolean networkAdaptersAvailable()
{
    // Add the available adapters to the networks submenu.
    CreateNetworkAdapterSubmenu(g_mi_networks);

    return false;
}
#endif // USE_GTK

gboolean updateUI(gpointer mediaOptions)
{
    // The audio pipeline state has changed. Update the available
    // menu options in line with the current state.
    g_mediaOptions = GPOINTER_TO_UINT(mediaOptions);

#ifdef USE_GTK
    // Update the playback options in the UI
    UpdatePlaybackOptions();
#else // USE_GTK
    g_debug("The playback options have changed");
#endif  // USE_GTK

    return false;
}

gboolean updatesAvailable(gpointer data)
{
    g_updatesAvailable = true;

#ifdef USE_GTK
    // Alert the user to availability of an application update.
    displayNotification("Update",
                        "There are updates available for this application",
                        NotificationClassifiction::INFO);

    // Enable the update option in the system tray menu.
    if (g_mi_update != NULL)
    {
        gtk_widget_set_sensitive(g_mi_update,true);
    }
#else // USE_GTK
    syslog(LOG_INFO, "There are updates available for this application");
    syslog(LOG_INFO, "Download and Install %s", (gchar *)data);
#endif // USE_GTK

    // Free up any previously stored location.
    delete[] g_updateLocation;

    // Note the location of the update installer.
    g_updateLocation = (char *)data;

    return false;
}

int main(int argc, char **argv)
{
    const gchar* usage = "openhome-player [subnet address]";

    // Verify command line options.
    if (argc > 2)
    {
        fprintf(stderr, "%s\n", usage);
        exit(1);
    }

    g_mPlayerArgs.restarted = false;
    g_mPlayerArgs.subnet    = InitArgs::NO_SUBNET;

    // Validate the format of any supplied subnet.
    if (argc == 2)
    {
        guint byte1, byte2, byte3, byte4 = 0;

        if (sscanf(argv[1], "%u.%u.%u.%u", &byte1, &byte2, &byte3, &byte4) == 4)
        {
            if ((byte1 > 0xFF) || (byte2 > 0xFF) ||
                (byte3 > 0xFF) || (byte4 > 0xFF))
            {
                fprintf(stderr, "%s\n", "ERROR: Malformed Subnet Address\n");
                exit(1);
            }

            OpenHome::TUint subnetIPv4 = 0;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            subnetIPv4 = ((byte1 & 0xFF) |
                         ((byte2 & 0xFF) << 8)|
                         ((byte3 & 0xFF) << 16)|
                         ((byte4 & 0xFF) << 24));
#else // __ORDER_LITTLE_ENDIAN__
            subnetIPv4 = ((byte4 & 0xFF) |
                         ((byte3 & 0xFF) << 8)|
                         ((byte2 & 0xFF) << 16)|
                         ((byte1 & 0xFF) << 24));
#endif //__ORDER_LITTLE_ENDIAN__


            g_mPlayerArgs.subnet = { 
                kFamilyV4,     // iFamily
                subnetIPv4,    // iV4
                { 255 }        // iV6
            };

        }
        else
        {
            fprintf(stderr, "%s\n", "ERROR: Malformed Subnet Address\n");
            exit(1);
        }
    }

#ifdef DEBUG
    // Enable malloc tracing.
    mtrace();
#endif // DEBUG

#ifdef USE_GTK
    gtk_init(&argc, &argv);
    notify_init(g_appName);

    AppIndicator *indicator;
    create_tray_icon(&indicator);
#else // USE_GTK
    // For headless builds open syslog for update availability logging
    openlog(g_appName, LOG_PERROR, LOG_LOCAL0);
#endif // USE_GTK

    // Start MediaPlayer thread.
    g_mplayerThread = g_thread_new("MediaPlayerIF",
                                   (GThreadFunc)InitAndRunMediaPlayer,
                                   (gpointer)&g_mPlayerArgs);
#ifdef USE_GTK
    gtk_main();
#else // USE_GTK
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
#endif // USE_GTK

    // Wait on the Media Player thread to complete.
    g_thread_join(g_mplayerThread);

    // Free up resources.
    delete[] g_updateLocation;
    g_updateLocation = NULL;

#ifdef USE_GTK
    notify_uninit();
#else // USE_GTK
    closelog();
#endif //USE_GTK

    return 0;
}
