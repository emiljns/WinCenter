#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "resource.h"

constexpr int HOTKEY_CENTER = 1;
constexpr int HOTKEY_EXIT = 2;
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT MENU_EXIT = 1001;

RECT CenterRect(const RECT& rect, const RECT& workArea)
{
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    RECT centered{};
    centered.left = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    centered.top = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;
    centered.right = centered.left + width;
    centered.bottom = centered.top + height;
    return centered;
}

void CenterActiveWindow()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return;
    if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) return;
    if (IsIconic(hwnd)) return;

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) return;

    if (IsZoomed(hwnd))
    {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (!GetWindowPlacement(hwnd, &wp)) return;
        wp.rcNormalPosition = CenterRect(wp.rcNormalPosition, mi.rcWork);
        SetWindowPlacement(hwnd, &wp);
        return;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return;
    RECT centered = CenterRect(rect, mi.rcWork);

    SetWindowPos(hwnd, nullptr, centered.left, centered.top, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// global menu handle
HMENU g_hMenu = nullptr;

// hidden window procedure for tray messages
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd); // required for menu to stay up
            TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == MENU_EXIT)
        {
            PostQuitMessage(0);
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();

    // Register hotkeys
    if (!RegisterHotKey(nullptr, HOTKEY_CENTER, MOD_CONTROL | MOD_SHIFT, 'C'))
    {
        MessageBoxW(nullptr, L"Couldn't register Ctrl+Shift+C.", L"Center Window", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!RegisterHotKey(nullptr, HOTKEY_EXIT, MOD_CONTROL | MOD_ALT, 'Q'))
    {
        MessageBoxW(nullptr, L"Couldn't register Ctrl+Alt+Q.", L"Center Window", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Register hidden window class
    WNDCLASS wc{};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CenterWindowHidden";
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, L"", 0,
        0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    // Tray icon setup
    NOTIFYICONDATA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wcscpy_s(nid.szTip, L"Center Window Utility");
    Shell_NotifyIcon(NIM_ADD, &nid);

    // Popup menu
    g_hMenu = CreatePopupMenu();
    AppendMenu(g_hMenu, MF_STRING, MENU_EXIT, L"Exit");

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
        {
            switch (msg.wParam)
            {
            case HOTKEY_CENTER:
                CenterActiveWindow();
                break;
            case HOTKEY_EXIT:
                PostQuitMessage(0);
                break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);
    DestroyMenu(g_hMenu);
    UnregisterHotKey(nullptr, HOTKEY_CENTER);
    UnregisterHotKey(nullptr, HOTKEY_EXIT);
    return 0;
}
