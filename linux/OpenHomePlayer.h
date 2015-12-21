#pragma once

extern const gchar *g_appName;

gboolean updateUI(gpointer mediaOptions);
#ifdef USE_UNITY
gboolean networkAdaptersAvailable();
#endif // USE_UNITY
void updatesAvailable(gpointer data);

