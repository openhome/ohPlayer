#include <gtk/gtk.h>

#include "mediaPlayerIF.h"
#include "gtk-main.h"

// Media Player thread entry point.
void InitAndRunMediaPlayer()
{
    int i;

    for (i = 0; i < 10; i++)
    {
      usleep(100000); /* 0.1 s */
      gdk_threads_add_idle((GSourceFunc)updateUI, NULL);
    }

    /* Make sure this thread is joined properly */
    gdk_threads_add_idle((GSourceFunc)terminate, g_thread_self());
}
