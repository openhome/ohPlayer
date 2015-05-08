// We need commctrl v6 for LoadIconMetric()
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

#include "resource.h"
#include "CustomMessages.h"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <strsafe.h>
#include <process.h>

#include "MediaPlayerIF.h"

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifdef _DEBUG
   #ifndef DBG_NEW
      #define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
      #define new DBG_NEW
   #endif
#endif  // _DEBUG

// System tray icon identifier.
#define ICON_ID 100

// Application menu item positions
#define MENU_PLAY       0
#define MENU_PAUSE      1
#define MENU_STOP       2
#define MENU_UPDATE     4

HINSTANCE  g_hInst            = NULL;
HMENU      g_hSubMenu         = NULL;
BOOL       g_updatesAvailable = false;
int        g_mediaOptions     = 0;
CHAR      *g_updateLocation   = NULL;
HANDLE     g_mplayerThread    = NULL;

wchar_t const szWindowClass[] = L"LitePipe";

// Forward declarations of functions included in this code module:
void              RegisterWindowClass(PCWSTR pszClassName, WNDPROC lpfnWndProc);
LRESULT CALLBACK  WndProc(HWND, UINT, WPARAM, LPARAM);
void              ShowContextMenu(HWND hwnd, POINT pt);
BOOL              AddNotificationIcon(HWND hwnd);
BOOL              DeleteNotificationIcon(HWND hwnd);
BOOL              ShowInfoBalloon(HWND hwnd, UINT title, UINT msg);
BOOL              RestoreTooltip(HWND hwnd);

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
    g_hInst = hInstance;
    RegisterWindowClass(szWindowClass, WndProc);

    //_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );

    // Create the main (hidden) window.
    WCHAR szTitle[100];
    LoadString(hInstance, IDS_APP_TITLE, szTitle, ARRAYSIZE(szTitle));
    HWND hwnd = CreateWindowEx( 0, szWindowClass, szTitle, 0, 0, 0, 0, 0,
                                HWND_MESSAGE, NULL, NULL, NULL );

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

    if (! _CrtDumpMemoryLeaks())
    {
        printf_s("No Memory Leaks Detected\n");
    }

    return 0;
}

void RegisterWindowClass(PCWSTR pszClassName, WNDPROC lpfnWndProc)
{
    WNDCLASSEX wcex = {sizeof(wcex)};

    wcex.lpfnWndProc    = lpfnWndProc;
    wcex.hInstance      = g_hInst;
    wcex.lpszClassName  = pszClassName;

    RegisterClassEx(&wcex);
}

BOOL AddNotificationIcon(HWND hwnd)
{
    NOTIFYICONDATA nid = {};

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;

    // Add the icon, setting the icon, tooltip, and callback message.
    // the icon will be identified with ICON_ID + hwd to be Vista compliant.
    nid.uID    = ICON_ID;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;

    nid.uCallbackMessage = WM_APP_NOTIFY;
    LoadIconMetric(g_hInst, MAKEINTRESOURCE(IDI_NOTIFICATIONICON),
                   LIM_SMALL, &nid.hIcon);
    LoadString(g_hInst, IDS_TOOLTIP, nid.szTip, ARRAYSIZE(nid.szTip));
    Shell_NotifyIcon(NIM_ADD, &nid);

    nid.uVersion = NOTIFYICON_VERSION_4;
    return Shell_NotifyIcon(NIM_SETVERSION, &nid);
}

BOOL DeleteNotificationIcon(HWND hwnd)
{
    NOTIFYICONDATA nid = {};

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd           = hwnd;
    nid.uID            = ICON_ID;

    return Shell_NotifyIcon(NIM_DELETE, &nid);
}

BOOL ShowInfoBalloon(HWND hwnd, UINT title, UINT msg )
{
    // Display an "update available" message.
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

BOOL RestoreTooltip(HWND hwnd)
{
    // After the balloon is dismissed, restore the tooltip.
    NOTIFYICONDATA nid = {};

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd   = hwnd;
    nid.uID    = ICON_ID;
    nid.uFlags = NIF_SHOWTIP;

    return Shell_NotifyIcon(NIM_MODIFY, &nid);
}

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

void ShowUpdateUI(HWND hwnd)
{
    // TBD. Update UI.
    MessageBox(hwnd,
               L"Installing Updates",
               L"Update", MB_OK);
}

void ShowContextMenu(HWND hwnd, POINT pt)
{
    HMENU hMenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDC_CONTEXTMENU));
    if (hMenu)
    {
        g_hSubMenu = GetSubMenu(hMenu, 0);

        if (g_hSubMenu)
        {
            // Our window must be foreground before calling TrackPopupMenu
            // or the menu will not disappear when the user clicks away
            SetForegroundWindow(hwnd);

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

            // Bring the playback options into line with the current
            // pipeline state.
            UpdatePlaybackOptions();

            TrackPopupMenuEx(g_hSubMenu, uFlags, pt.x, pt.y, hwnd, NULL);
            g_hSubMenu = NULL;
        }
        DestroyMenu(hMenu);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
        {
            // Add the notification icon
            if (!AddNotificationIcon(hwnd))
            {
                MessageBox(hwnd, L"Error Adding Icon", NULL, MB_OK);

                return -1;
            }

            /* Register UPnP/OhMedia devices. */
            g_mplayerThread = CreateThread(NULL, 0, &InitAndRunMediaPlayer,
                                          (LPVOID)hwnd, 0, NULL);

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

                case IDM_UPDATE:
                {
                    ShowUpdateUI(hwnd);
                    break;
                }

                case IDM_ABOUT:
                {
                    MessageBox(hwnd,  L"LitePipe v1.0", L"About", MB_OK);
                    break;
                }

                case IDM_EXIT:
                {
                    ExitMediaPlayer();

                    WaitForSingleObject(g_mplayerThread, INFINITE);
                    CloseHandle(g_mplayerThread);

                    DestroyWindow(hwnd);
                    break;
                }

                default:
                    return DefWindowProc(hwnd, message, wParam, lParam);
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
            delete g_updateLocation;

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
            delete g_updateLocation;

            DeleteNotificationIcon(hwnd);
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}
