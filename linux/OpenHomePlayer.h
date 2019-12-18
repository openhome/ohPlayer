#pragma once

#include <glib.h>

extern const gchar *g_appName;

gboolean updateUI(gpointer mediaOptions);
#ifdef USE_GTK
gboolean networkAdaptersAvailable();
#endif // USE_GTK
void     updatesAvailable(gpointer data);

