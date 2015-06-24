#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libnotify/notify.h>
#include <vector>

#include "CustomMessages.h"
#include "MediaPlayerIF.h"
#include "version.h"

#ifdef DEBUG
// Mtrace allocation tracing.
#include <mcheck.h>
#endif

#ifdef USE_NVWA
// New/Delete tracing.
#include "debug_new.h"
#endif

// Type definitions

enum class NotificationClassifiction
{
    INFO,
    WARNING
};

// Global variables

// System Tray Menu Items
static GtkWidget *g_mi_play     = NULL;
static GtkWidget *g_mi_pause    = NULL;
static GtkWidget *g_mi_stop     = NULL;
static GtkWidget *g_mi_networks = NULL;
static GtkWidget *g_mi_update   = NULL;
static GtkWidget *g_mi_about    = NULL;
static GtkWidget *g_mi_exit     = NULL;
static GtkWidget *g_mi_sep      = NULL;
static GtkWidget *g_mi_sep1     = NULL;

static gboolean  g_updatesAvailable = false; // App updates availability.
static gchar    *g_updateLocation   = NULL;  // Update location URL.
static guint     g_mediaOptions     = 0;     // Available media playback options
static GThread  *g_mplayerThread    = NULL;  // Media Player thread
static InitArgs  g_mPlayerArgs;              // Media Player arguments.

const gchar     *g_appName          = "LitePipeTestApp";

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

static void tray_icon_on_click(GtkStatusIcon *status_icon,
                               gpointer       user_data)
{
    g_debug("Clicked on tray icon\n");
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
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file("OpenHome-48x48.png", NULL);

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

static void updateSelectionHandler()
{
    // TBD Update UI
    GtkWidget *dialog;
    dialog = gtk_message_dialog_new(NULL,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "Installing Updates");
    gtk_window_set_title(GTK_WINDOW(dialog), "Update");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void exitSelectionHandler()
{
    ExitMediaPlayer();

    gtk_main_quit();
}

static void NetworkSelectionHandler(GtkMenuItem * /*menuitem*/, gpointer args)
{
    TIpAddress subnet = GPOINTER_TO_UINT(args);

    // Restart the media player on the selected subnet.
    ExitMediaPlayer();

    // Wait on the Media Player thread to complete.
    g_thread_join(g_mplayerThread);

    /* Re-Register UPnP/OhMedia devices. */
    g_mPlayerArgs.subnet = subnet;

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
        GtkWidget *subnet;

        subnet = gtk_menu_item_new_with_label((*it)->menuString->c_str());
        gtk_menu_shell_append (GTK_MENU_SHELL(submenu), subnet);
        gtk_widget_show (subnet);

        // Attach menuitem handler.
        g_signal_connect(G_OBJECT(subnet),
                         "activate",
                         G_CALLBACK(NetworkSelectionHandler),
                         GUINT_TO_POINTER((*it)->subnet));

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

// Display the context menu
static void tray_icon_on_menu(GtkStatusIcon *status_icon,
                              guint          button,
                              guint          activate_time,
                              gpointer       user_data)
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

    // Populate the 'Networks' submenu.
    CreateNetworkAdapterSubmenu(g_mi_networks);

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

    gtk_menu_popup(menu,
                   NULL,
                   NULL,
                   NULL,
                   NULL,
                   button,
                   activate_time);
}

static void create_tray_icon()
{
    GtkStatusIcon *tray_icon;

    tray_icon = gtk_status_icon_new_from_file("OpenHome-48x48.png");

    gtk_status_icon_set_tooltip_text(tray_icon, g_appName);

    g_signal_connect(G_OBJECT(tray_icon), "activate",
                     G_CALLBACK(tray_icon_on_click), NULL);

    g_signal_connect(G_OBJECT(tray_icon),
                     "popup-menu",
                     G_CALLBACK(tray_icon_on_menu), NULL);

    gtk_status_icon_set_visible(tray_icon, TRUE);
}

gboolean updateUI(gpointer mediaOptions)
{
    // The audio pipeline state has changed. Update the available
    // menu options in line with the current state.
    g_mediaOptions = GPOINTER_TO_UINT(mediaOptions);

    // Update the playback options in the UI
    UpdatePlaybackOptions();

    return false;
}

gboolean updatesAvailable(gpointer data)
{
    g_updatesAvailable = true;

    // Alert the user to availability of an application update.
    displayNotification("Update",
                        "There are updates available for this application",
                        NotificationClassifiction::INFO);

    // Enable the update option in the system tray menu.
    if (g_mi_update != NULL)
    {
        gtk_widget_set_sensitive(g_mi_update,true);
    }

    // Free up any previously stored location.
    delete[] g_updateLocation;

    // Note the location of the update installer.
    g_updateLocation = (char *)data;

    return false;
}

int main(int argc, char **argv)
{
#ifdef DEBUG
    // Enable malloc tracing.
    mtrace();
#endif

    gtk_init(&argc, &argv);
    notify_init(g_appName);

    create_tray_icon();

    // Start MediaPlayer thread.
    g_mPlayerArgs.subnet = InitArgs::NO_SUBNET;

    g_mplayerThread = g_thread_new("MediaPlayerIF",
                                   (GThreadFunc)InitAndRunMediaPlayer,
                                   (gpointer)&g_mPlayerArgs);

    gtk_main();

    // Wait on the Media Player thread to complete.
    g_thread_join(g_mplayerThread);

    // Free up resources.
    delete[] g_updateLocation;
    g_updateLocation = NULL;

    notify_uninit();

    return 0;
}
