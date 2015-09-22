// We need commctrl v6 for LoadIconMetric()
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

#include <Windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <process.h>
#include <string>
#include <vector>

#include "resource.h"
#include "version.h"
#include "CustomMessages.h"
#include "MediaPlayerIF.h"
#include "MemoryCheck.h"

using namespace std;

static const WCHAR WINDOW_CLASS[] = L"LitePipe"; // Window class name.
static const INT   ICON_ID        = 100;         // System tray icon identifier.

// Application menu item positions.
//
// These reflect the IDC_CONTEXTMENU defined in LitePipeTestApp.rc
// and modified programatically in ShowContextMenu().
static const UINT MENU_PLAY    = 0;
static const UINT MENU_PAUSE   = 1;
static const UINT MENU_STOP    = 2;
static const UINT MENU_SEP_1   = 3;
static const UINT MENU_NETWORK = 4;
static const UINT MENU_SEP_2   = 5;
static const UINT MENU_UPDATE  = 6;
static const UINT MENU_ABOUT   = 7;
static const UINT MENU_EXIT    = 8;

// Global variables.
HINSTANCE  g_hInst            = NULL;  // Application instance.
HMENU      g_hSubMenu         = NULL;  // Application options menu.
BOOL       g_updatesAvailable = false; // Application updates availability.
CHAR      *g_updateLocation   = NULL;  // Update location URL.
INT        g_mediaOptions     = 0;     // Available media playback options flag.
HANDLE     g_mplayerThread    = NULL;  // Media Player thread ID.
InitArgs   g_mPlayerArgs;              // Media Player arguments.

// List of available subnets.
// Used to populate the 'Networks' popup menu.
std::vector<SubnetRecord*> *g_subnetList = NULL;

// Required forward declarations.
LRESULT CALLBACK  WndProc(HWND, UINT, WPARAM, LPARAM);

// Register application window class name and handler.
void RegisterWindowClass(PCWSTR className, WNDPROC wndProc)
{
    WNDCLASSEX wcex = {sizeof(wcex)};

    wcex.lpfnWndProc    = wndProc;
    wcex.hInstance      = g_hInst;
    wcex.lpszClassName  = className;

    RegisterClassEx(&wcex);
}

// Application main entry point.
INT APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE,
                      PWSTR     /*lpCmdLine*/,
                      INT       /*nCmdShow*/)
{
    g_hInst = hInstance;
    RegisterWindowClass(WINDOW_CLASS, WndProc);

    // Create the main (hidden) window.
    WCHAR windowName[100];
    LoadString(hInstance, IDS_APP_TITLE, windowName, ARRAYSIZE(windowName));
    HWND hwnd = CreateWindowEx( WS_EX_LTRREADING, WINDOW_CLASS, windowName,
                                0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL );

    if (hwnd)
    {
        // Main message loop:
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

#ifdef _DEBUG
    // Dump memory leak information on exit.
    if (! _CrtDumpMemoryLeaks())
    {
        printf_s("No Memory Leaks Detected\n");
    }
#endif /* _DEBUG */

    return 0;
}

// Add the application icon to the system tray.
BOOL AddNotificationIcon(HWND hwnd)
{
    NOTIFYICONDATA nid = {};

    nid.cbSize   = sizeof(NOTIFYICONDATA);
    nid.hWnd     = hwnd;

    // Add the icon, setting the icon, tooltip, and callback message.
    // the icon will be identified with ICON_ID + hwd to be Vista compliant.
    nid.uID              = ICON_ID;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_NOTIFY;

    LoadIconMetric(g_hInst,
                   MAKEINTRESOURCE(IDI_NOTIFICATIONICON),
                   LIM_SMALL,
                   &nid.
                   hIcon);

    LoadString(g_hInst, IDS_TOOLTIP, nid.szTip, ARRAYSIZE(nid.szTip));

    Shell_NotifyIcon(NIM_ADD, &nid);

    nid.uVersion = NOTIFYICON_VERSION_4;

    return Shell_NotifyIcon(NIM_SETVERSION, &nid);
}

// Remove the application icon from the system tray.
BOOL DeleteNotificationIcon(HWND hwnd)
{
    NOTIFYICONDATA nid = {};

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd   = hwnd;
    nid.uID    = ICON_ID;

    return Shell_NotifyIcon(NIM_DELETE, &nid);
}

// Display an informational balloon in the system tray.
BOOL ShowInfoBalloon(HWND hwnd, UINT title, UINT msg )
{
    NOTIFYICONDATA nid = {};

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd   = hwnd;
    nid.uID    = ICON_ID;

    // Show an informational icon.
    nid.uFlags = NIF_INFO;

    nid.dwInfoFlags = NIIF_WARNING;
    LoadString(g_hInst, title, nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle));
    LoadString(g_hInst, msg, nid.szInfo, ARRAYSIZE(nid.szInfo));

    return Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// Restore the application tooltip to the system tray icon.
BOOL RestoreTooltip(HWND hwnd)
{
    // After the balloon is dismissed, restore the application tooltip.
    NOTIFYICONDATA nid = {};

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd   = hwnd;
    nid.uID    = ICON_ID;
    nid.uFlags = NIF_SHOWTIP;

    return Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// Keep the menu playback options in sync with current state of the
// media player.
void UpdatePlaybackOptions()
{
    if ((g_mediaOptions & MEDIAPLAYER_PLAY_OPTION) != 0)
    {
        EnableMenuItem(g_hSubMenu, MENU_PLAY, MF_ENABLED|MF_BYPOSITION);
    }
    else
    {
        EnableMenuItem(g_hSubMenu, MENU_PLAY,
                       MF_DISABLED|MF_GRAYED|MF_BYPOSITION);
    }

    if ((g_mediaOptions & MEDIAPLAYER_PAUSE_OPTION) != 0)
    {
        EnableMenuItem(g_hSubMenu, MENU_PAUSE, MF_ENABLED|MF_BYPOSITION);
    }
    else
    {
        EnableMenuItem(g_hSubMenu, MENU_PAUSE,
                       MF_DISABLED|MF_GRAYED|MF_BYPOSITION);
    }

    if ((g_mediaOptions & MEDIAPLAYER_STOP_OPTION) != 0)
    {
        EnableMenuItem(g_hSubMenu, MENU_STOP, MF_ENABLED|MF_BYPOSITION);
    }
    else
    {
        EnableMenuItem(g_hSubMenu, MENU_STOP,
                       MF_DISABLED|MF_GRAYED|MF_BYPOSITION);
    }
}

BOOL InstallUpdate(HWND hwnd)
{
    // Display update dialogue.
    INT button = MessageBox(hwnd,
                            L"Install Updates ?",
                            L"Update",
                            MB_OKCANCEL);

    if (button == IDOK)
    {
        WCHAR  *wcstring;
        size_t  convertedChars;
        UINT    length = strlen(g_updateLocation);

        if (length <= 0)
        {
            return false;
        }

        // Convert update URI to a wide string.
        wcstring = new WCHAR[length+1];

        mbstowcs_s(&convertedChars, wcstring, length+1, g_updateLocation,
                   length);

        // Launch deafult browser to download update.
        HINSTANCE r = ShellExecute(NULL, L"open", wcstring,
                                   NULL, NULL, SW_SHOWNORMAL);

        delete wcstring;

        // A return code of > 32 equal success
        if ((INT)r > 32)
        {
            return true;
        }
        else
        {
            MessageBox(hwnd,
                       L"Cannot Retreive Update",
                       L"Update",
                       MB_OK);
        }
    }

    return false;
}

// Create a sub menu listing the available network adapters and their
// associated networks.
HMENU CreateNetworkAdapterPopup()
{
    HMENU pMenu;

    pMenu = CreatePopupMenu();

    if (pMenu != NULL)
    {
        // Release any existing subnet list resources.
        if (g_subnetList != NULL)
        {
            FreeSubnets(g_subnetList);
            g_subnetList = NULL;
        }

        // Get a list of available subnets from the media player..
        g_subnetList = GetSubnets();

        UINT index = 0;
        std::vector<SubnetRecord*>::iterator it;

        // Put each subnet in our popup menu.
        for (it=g_subnetList->begin(); it < g_subnetList->end(); it++)
        {
            WCHAR  *wcstring;
            size_t  convertedChars;
            size_t  itemSize;;
            UINT    uFlags = MF_STRING;

            // If this is the subnet we are currently using disable it's
            // selection.
            if ((*it)->isCurrent)
            {
                uFlags |= MF_GRAYED;
            }

            // Convert the item narrow string to a wide character string.
            itemSize = (*it)->menuString->length();
            wcstring = new WCHAR[itemSize+1];

            mbstowcs_s(&convertedChars, wcstring, itemSize+1,
                       (*it)->menuString->c_str() ,itemSize );

            // Add the menu item.
            if (! AppendMenu(pMenu, uFlags,
                             (UINT_PTR)(IDM_NETWORK_BASE+index), wcstring))
            {
                delete wcstring;
                return pMenu;
            }

            delete[] wcstring;
            index++;
        }
    }

    return pMenu;
}

// Display the applications options menu.
void ShowContextMenu(HWND hwnd, POINT pt)
{
    HMENU hMenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDC_CONTEXTMENU));
    if (hMenu)
    {
        g_hSubMenu = GetSubMenu(hMenu, 0);

        if (g_hSubMenu)
        {
            HMENU networkPopup = NULL;

            // Our window must be foreground before calling TrackPopupMenu
            // or the menu will not disappear when the user clicks away
            SetForegroundWindow(hwnd);

            // Insert a Network popup menu to allow selection of the required
            // subnet.
            //
            // This submenu is only available if the media player is
            // up and running. If, for example, the media player has been
            // shutdown due to the audio client being disconnected the
            // submenu is visible, but unavailable.
            //
            // This is done prior to any menu item configuration as the insert
            // operation causes menu item indices to be altered.
            UINT nFlags = MF_BYPOSITION | MF_POPUP | MF_STRING;

            if (g_mplayerThread != NULL)
            {
                networkPopup = CreateNetworkAdapterPopup();
            }
            else
            {
                nFlags |= MF_GRAYED;
            }

            if (!InsertMenu(g_hSubMenu,
                            MENU_NETWORK,
                            nFlags,
                            (UINT_PTR)networkPopup,
                            L"Network"))
            {
                networkPopup = NULL;
            }

            if (g_updatesAvailable)
            {
                // Ensure the update button is available.
                EnableMenuItem(g_hSubMenu, MENU_UPDATE,
                               MF_ENABLED|MF_BYPOSITION);
            }
            else
            {
                // Disable update button, until one becomes available.
                EnableMenuItem(g_hSubMenu, MENU_UPDATE,
                               MF_DISABLED|MF_GRAYED|MF_BYPOSITION);
            }

            if (g_mplayerThread != NULL)
            {
                // Bring the playback options into line with the current
                // pipeline state.
                //
                // This operation relies on the availability of the media
                // player.
                UpdatePlaybackOptions();
            }

            // respect menu drop alignment
            UINT uFlags = TPM_RIGHTBUTTON;
            if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0)
            {
                uFlags |= TPM_RIGHTALIGN;
            }
            else
            {
                uFlags |= TPM_LEFTALIGN;
            }

            TrackPopupMenuEx(g_hSubMenu, uFlags, pt.x, pt.y, hwnd, NULL);
            g_hSubMenu = NULL;
        }
        DestroyMenu(hMenu);
    }
}

// Application message handler.
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
        {
            // Add the system tray notification icon
            if (!AddNotificationIcon(hwnd))
            {
                MessageBox(hwnd, L"Error Adding Icon", NULL, MB_OK);

                return -1;
            }

            /* Register UPnP/OhMedia devices. */
            g_mPlayerArgs.hwnd   = hwnd;
            g_mPlayerArgs.subnet = InitArgs::NO_SUBNET;

            g_mplayerThread = CreateThread(NULL, 0, &InitAndRunMediaPlayer,
                                          (LPVOID)&g_mPlayerArgs, 0, NULL);

            break;
        }

        case WM_COMMAND:
        {
            int const wmId = LOWORD(wParam);

            // Parse the menu selections:
            switch (wmId)
            {
                case IDM_PLAY:
                {
                    PipeLinePlay();
                    break;
                }

                case IDM_PAUSE:
                {
                    PipeLinePause();
                    break;
                }

                case IDM_STOP:
                {
                    PipeLineStop();
                    break;
                }

                case IDM_ABOUT:
                {
                    WCHAR  *wcstring;
                    size_t  convertedChars;
                    UINT    length;
                    string  aboutString("LitePipeTestApp " CURRENT_VERSION
                                        "\n\nCopyright (c) OpenHome");

                    // Convert to a wide string.
                    length = strlen(aboutString.c_str());
                    wcstring = new WCHAR[length+1];

                    mbstowcs_s(&convertedChars, wcstring, length+1,
                               aboutString.c_str(), length);

                    MessageBox(hwnd,  wcstring, L"About", MB_OK);

                    delete wcstring;

                    break;
                }

                case IDM_UPDATE:
                {
                    if (! InstallUpdate(hwnd))
                    {
                        break;
                    }

                    // Fallthough on success to exit the application.
                }

                case IDM_EXIT:
                {
                    if (g_mplayerThread != NULL)
                    {
                        ExitMediaPlayer();

                        WaitForSingleObject(g_mplayerThread, INFINITE);
                        CloseHandle(g_mplayerThread);
                    }

                    DestroyWindow(hwnd);
                    break;
                }

                default:
                {
                    // Check our dynamic Network subnet selection popup.
                    if (wmId >= IDM_NETWORK_BASE)
                    {
                        UINT index = wmId - IDM_NETWORK_BASE;

                        try
                        {
                            SubnetRecord* subnetEntry = g_subnetList->at(index);

                            TIpAddress subnet = subnetEntry->subnet;

                            // Release subnet list resources.
                            FreeSubnets(g_subnetList);
                            g_subnetList = NULL;

                            // Restart the media player on the selected subnet.
                            ExitMediaPlayer();

                            WaitForSingleObject(g_mplayerThread, INFINITE);
                            CloseHandle(g_mplayerThread);

                            /* Re-Register UPnP/OhMedia devices. */
                            g_mPlayerArgs.hwnd   = hwnd;
                            g_mPlayerArgs.subnet = subnet;

                            g_mplayerThread =
                                CreateThread(NULL,
                                             0,
                                            &InitAndRunMediaPlayer,
                                             (LPVOID)&g_mPlayerArgs,
                                             0,
                                             NULL);
                        }
                        catch (const std::out_of_range& /*e*/)
                        {
                        }

                        break;
                    }

                    return DefWindowProc(hwnd, message, wParam, lParam);
                }
            }

            break;
        }

        case WM_APP_NOTIFY:
        {
            switch (LOWORD(lParam))
            {
                case NIN_BALLOONTIMEOUT:
                    RestoreTooltip(hwnd);
                    break;

                case NIN_BALLOONUSERCLICK:
                    RestoreTooltip(hwnd);
                    break;

                case WM_CONTEXTMENU:
                    {
                        POINT const pt = { LOWORD(wParam), HIWORD(wParam) };
                        ShowContextMenu(hwnd, pt);
                    }
                    break;
            }

            break;
        }

        case WM_APP_UPDATE_AVAILABLE:
        {
            g_updatesAvailable = true;

            // Alert the user to availability of an application update.
            ShowInfoBalloon(hwnd, IDS_UPDATE_TITLE, IDS_UPDATE_TEXT);

            // Enable the update option in the system tray menu.
            if (g_hSubMenu != NULL)
            {
                EnableMenuItem(g_hSubMenu, MENU_UPDATE,
                               MF_ENABLED|MF_BYPOSITION);
            }

            // Note the location of the update installer.
            delete[] g_updateLocation;

            g_updateLocation = (char *)lParam;

            break;
        }

        case WM_APP_AUDIO_DISCONNECTED:
        {
            // The audio endpoint has been disconnected.
            // Notify the user that a restart is required.
            ShowInfoBalloon(hwnd,
                              IDS_AUDIO_DISCONNECT_TITLE,
                              IDS_AUDIO_DISCONNECT_TEXT);

            // Close down the media player.
            ExitMediaPlayer();

            WaitForSingleObject(g_mplayerThread, INFINITE);
            CloseHandle(g_mplayerThread);
            g_mplayerThread = NULL;

            break;
        }

        case WM_APP_AUDIO_INIT_ERROR:
        {
            // The audio engine has failed to initialise.
            // Notify the user that a restart is required.
            ShowInfoBalloon(hwnd,
                              IDS_AUDIO_INIT_ERROR_TITLE,
                              IDS_AUDIO_INIT_ERROR_TEXT);

            // Close down the media player.
            ExitMediaPlayer();

            WaitForSingleObject(g_mplayerThread, INFINITE);
            CloseHandle(g_mplayerThread);
            g_mplayerThread = NULL;

            break;
        }

        case WM_APP_PLAYBACK_OPTIONS:
        {
            // The audio pipeline state has changed. Update the available
            // menu options in line with the current state.
            g_mediaOptions = LOWORD(lParam);

            // If the sub menu isn't currently being displayed there's
            // nothing to update for the moment.
            if (g_hSubMenu == NULL)
            {
                break;
            }

            // Update the playback options in the displayed sub-menu.
            UpdatePlaybackOptions();

            break;
        }

        case WM_DESTROY:
        {
            delete[] g_updateLocation;
            g_updateLocation = NULL;

            if (g_subnetList != NULL)
            {
                FreeSubnets(g_subnetList);
                g_subnetList = NULL;
            }

            DeleteNotificationIcon(hwnd);
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}
