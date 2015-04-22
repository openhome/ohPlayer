// We need commctrl v6 for LoadIconMetric()
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

#include "resource.h"
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
#define ICON_ID 1

HINSTANCE g_hInst            = NULL;
HMENU     g_hSubMenu         = NULL;
BOOL      g_updatesAvailable = false;
HANDLE    _MplayerThread     = NULL;

UINT const WMAPP_NOTIFYCALLBACK = WM_APP + 1;

UINT_PTR const UPDATE_TIMER_ID  = 1;

wchar_t const szWindowClass[] = L"LitePipe";

// Forward declarations of functions included in this code module:
void              RegisterWindowClass(PCWSTR pszClassName, WNDPROC lpfnWndProc);
LRESULT CALLBACK  WndProc(HWND, UINT, WPARAM, LPARAM);
void              ShowContextMenu(HWND hwnd, POINT pt);
BOOL              AddNotificationIcon(HWND hwnd);
BOOL              DeleteNotificationIcon(HWND hwnd);
BOOL              ShowUpdateBalloon(HWND hwnd);
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
        // Pretend to get an update notification, in 10 seconds ....
        SetTimer(hwnd, UPDATE_TIMER_ID, 10000, NULL);

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
    NOTIFYICONDATA nid = {sizeof(nid)};

    nid.hWnd = hwnd;

    // Add the icon, setting the icon, tooltip, and callback message.
    // the icon will be identified with ICON_ID + hwd to be Vista compliant.
    nid.uID    = ICON_ID;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;

    nid.uCallbackMessage = WMAPP_NOTIFYCALLBACK;
    LoadIconMetric(g_hInst, MAKEINTRESOURCE(IDI_NOTIFICATIONICON),
                   LIM_SMALL, &nid.hIcon);
    LoadString(g_hInst, IDS_TOOLTIP, nid.szTip, ARRAYSIZE(nid.szTip));
    Shell_NotifyIcon(NIM_ADD, &nid);

    nid.uVersion = NOTIFYICON_VERSION_4;
    return Shell_NotifyIcon(NIM_SETVERSION, &nid);
}

BOOL DeleteNotificationIcon(HWND hwnd)
{
    NOTIFYICONDATA nid = {sizeof(nid)};
    nid.hWnd           = hwnd;
    nid.uID            = ICON_ID;

    return Shell_NotifyIcon(NIM_DELETE, &nid);
}

BOOL ShowUpdateBalloon(HWND hwnd)
{
    // Display an "update available" message.
    NOTIFYICONDATA nid = {sizeof(nid)};
    nid.hWnd   = hwnd;
    nid.uID    = ICON_ID;

    // Show an informational icon.
    nid.uFlags = NIF_INFO;

    // Respect quiet time since this balloon did not come from a direct user
    // action.
    nid.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    LoadString(g_hInst, IDS_UPDATE_TITLE, nid.szInfoTitle,
               ARRAYSIZE(nid.szInfoTitle));
    LoadString(g_hInst, IDS_UPDATE_TEXT, nid.szInfo, ARRAYSIZE(nid.szInfo));

    return Shell_NotifyIcon(NIM_MODIFY, &nid);
}

BOOL RestoreTooltip(HWND hwnd)
{
    // After the balloon is dismissed, restore the tooltip.
    NOTIFYICONDATA nid = {sizeof(nid)};
    nid.hWnd   = hwnd;
    nid.uID    = ICON_ID;
    nid.uFlags = NIF_SHOWTIP;

    return Shell_NotifyIcon(NIM_MODIFY, &nid);
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
                EnableMenuItem(g_hSubMenu, 4, MF_ENABLED|MF_BYPOSITION);
            }
            else
            {
                // Disable update button, until our dummy update timer fires.
                EnableMenuItem(g_hSubMenu, 4,
                               MF_DISABLED|MF_GRAYED|MF_BYPOSITION);
            }

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
            _MplayerThread = CreateThread(NULL, 0, &InitAndRunMediaPlayer,
                                          NULL, 0, NULL);

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
                    MessageBox(hwnd,  L"PLAY", L"TBD", MB_OK);
                    break;
                }

                case IDM_PAUSE:
                {
                    PipeLinePause();
                    MessageBox(hwnd,  L"PAUSE", L"TBD", MB_OK);
                    break;
                }

                case IDM_STOP:
                {
                    PipeLineStop();
                    MessageBox(hwnd,  L"STOP", L"TBD", MB_OK);
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

                    WaitForSingleObject(_MplayerThread, INFINITE);
                    CloseHandle(_MplayerThread);

                    DestroyWindow(hwnd);
                    break;
                }

                default:
                    return DefWindowProc(hwnd, message, wParam, lParam);
            }

            break;
        }

        case WMAPP_NOTIFYCALLBACK:
        {
            switch (LOWORD(lParam))
            {
                case NIN_BALLOONTIMEOUT:
                    RestoreTooltip(hwnd);
                    break;

                case NIN_BALLOONUSERCLICK:
                    RestoreTooltip(hwnd);
                    ShowUpdateUI(hwnd);
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

        case WM_TIMER:
        {
            if (wParam == UPDATE_TIMER_ID)
            {
                g_updatesAvailable = true;
                if (g_hSubMenu != NULL)
                {
                    EnableMenuItem(g_hSubMenu, 4, MF_ENABLED|MF_BYPOSITION);
                }

                ShowUpdateBalloon(hwnd);
                KillTimer(hwnd, UPDATE_TIMER_ID);
            }

            break;
        }

        case WM_DESTROY:
        {
            DeleteNotificationIcon(hwnd);
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}
