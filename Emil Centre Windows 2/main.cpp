#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include "resource.h"

constexpr int HOTKEY_1280x720 = 1;
constexpr int HOTKEY_1600x900 = 2;
constexpr int HOTKEY_1920x1080 = 3;
constexpr int HOTKEY_70PCT = 4;
constexpr int HOTKEY_CENTER = 5;
constexpr int HOTKEY_EXIT = 99;

constexpr UINT WM_TRAYICON = WM_APP + 1;

constexpr UINT MENU_1280x720 = 1001;
constexpr UINT MENU_1600x900 = 1002;
constexpr UINT MENU_1920x1080 = 1003;
constexpr UINT MENU_70PCT = 1004;
constexpr UINT MENU_CENTER = 1005;
constexpr UINT MENU_EXIT = 1006;

HMENU g_hMenu = nullptr;

// Cache AdjustWindowRectExForDpi once
using AdjustProc = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
static AdjustProc pAdjust = reinterpret_cast<AdjustProc>(
    GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));

void CleanupHotkeys()
{
    UnregisterHotKey(nullptr, HOTKEY_1280x720);
    UnregisterHotKey(nullptr, HOTKEY_1600x900);
    UnregisterHotKey(nullptr, HOTKEY_1920x1080);
    UnregisterHotKey(nullptr, HOTKEY_70PCT);
    UnregisterHotKey(nullptr, HOTKEY_CENTER);
    UnregisterHotKey(nullptr, HOTKEY_EXIT);
}

void CenterActiveWindow(HWND hwnd = nullptr)
{
    if (!hwnd) hwnd = GetForegroundWindow();
    if (!hwnd) return;
    if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) return;
    if (IsIconic(hwnd)) return;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc))
        return;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return;

    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - width) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - height) / 2;

    SetWindowPos(hwnd, nullptr, x, y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void ResizeClient(HWND hwnd, int clientWidth, int clientHeight)
{
    if (!hwnd) return;
    if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) return;
    if (IsIconic(hwnd)) return;

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!(style & WS_THICKFRAME))
        return;

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED)
    {
        ShowWindow(hwnd, SW_RESTORE);
    }

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return;

    int monitorWidth = mi.rcWork.right - mi.rcWork.left;
    int monitorHeight = mi.rcWork.bottom - mi.rcWork.top;

    clientWidth = std::max(400, std::min(clientWidth, monitorWidth));
    clientHeight = std::max(300, std::min(clientHeight, monitorHeight));

    DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    RECT rc{ 0, 0, clientWidth, clientHeight };

    if (pAdjust)
    {
        UINT dpi = GetDpiForWindow(hwnd);
        if (dpi == 0) dpi = 96;
        pAdjust(&rc, static_cast<DWORD>(style), FALSE, exStyle, dpi);
    }
    else
    {
        AdjustWindowRectEx(&rc, static_cast<DWORD>(style), FALSE, exStyle);
    }

    int windowWidth = rc.right - rc.left;
    int windowHeight = rc.bottom - rc.top;

    int x = mi.rcWork.left + (monitorWidth - windowWidth) / 2;
    int y = mi.rcWork.top + (monitorHeight - windowHeight) / 2;

    SetWindowPos(hwnd, nullptr, x, y, windowWidth, windowHeight,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void ResizePercent(HWND hwnd, int percent)
{
    if (!hwnd) return;

    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return;

    int currentWidth = rc.right - rc.left;
    int currentHeight = rc.bottom - rc.top;

    int newWidth = currentWidth * percent / 100;
    int newHeight = currentHeight * percent / 100;

    ResizeClient(hwnd, newWidth, newHeight);
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenuEx(g_hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                pt.x, pt.y, hwnd, nullptr);
            PostMessageW(hwnd, WM_NULL, 0, 0);
        }
        break;

    case WM_COMMAND:
    {
        HWND active = GetForegroundWindow();
        switch (LOWORD(wParam))
        {
        case MENU_1280x720: ResizeClient(active, 1280, 720); break;
        case MENU_1600x900: ResizeClient(active, 1600, 900); break;
        case MENU_1920x1080: ResizeClient(active, 1920, 1080); break;
        case MENU_70PCT:    ResizePercent(active, 70); break;
        case MENU_CENTER:   CenterActiveWindow(active); break; // Reuses fetched window handle
        case MENU_EXIT:     DestroyWindow(hwnd); break;
        }
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();

    // Register Hotkeys
    if (!RegisterHotKey(nullptr, HOTKEY_1280x720, MOD_CONTROL | MOD_ALT, '1') ||
        !RegisterHotKey(nullptr, HOTKEY_1600x900, MOD_CONTROL | MOD_ALT, '2') ||
        !RegisterHotKey(nullptr, HOTKEY_1920x1080, MOD_CONTROL | MOD_ALT, '3') ||
        !RegisterHotKey(nullptr, HOTKEY_70PCT, MOD_CONTROL | MOD_ALT, '4') ||
        !RegisterHotKey(nullptr, HOTKEY_CENTER, MOD_CONTROL | MOD_ALT, 'C') ||
        !RegisterHotKey(nullptr, HOTKEY_EXIT, MOD_CONTROL | MOD_ALT, 'Q'))
    {
        MessageBoxW(nullptr, L"Failed to register one or more hotkeys.", L"Error", MB_OK | MB_ICONERROR);
        CleanupHotkeys();
        return 1;
    }

    // Register Window Class
    WNDCLASSW wc{};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CenterWindowHidden";

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        CleanupHotkeys();
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0,
        0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd)
    {
        CleanupHotkeys();
        return 1;
    }

    // Setup Tray Icon (LoadIconW returns a shared icon resource managed by Windows)
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = hIcon;
    wcscpy_s(nid.szTip, L"Resize Utility");

    if (!Shell_NotifyIconW(NIM_ADD, &nid))
    {
        DestroyWindow(hwnd);
        CleanupHotkeys();
        return 1;
    }

    // Create Pop-up Tray Menu
    g_hMenu = CreatePopupMenu();
    AppendMenuW(g_hMenu, MF_STRING, MENU_1280x720, L"Resize 1280x720 (Ctrl+Alt+1)");
    AppendMenuW(g_hMenu, MF_STRING, MENU_1600x900, L"Resize 1600x900 (Ctrl+Alt+2)");
    AppendMenuW(g_hMenu, MF_STRING, MENU_1920x1080, L"Resize 1920x1080 (Ctrl+Alt+3)");
    AppendMenuW(g_hMenu, MF_STRING, MENU_70PCT, L"Resize 70% (Ctrl+Alt+4)");
    AppendMenuW(g_hMenu, MF_STRING, MENU_CENTER, L"Center Window (Ctrl+Alt+C)");
    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_hMenu, MF_STRING, MENU_EXIT, L"Exit (Ctrl+Alt+Q)");

    // Message Loop
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
        {
            HWND active = GetForegroundWindow();
            switch (msg.wParam)
            {
            case HOTKEY_1280x720: ResizeClient(active, 1280, 720); break;
            case HOTKEY_1600x900: ResizeClient(active, 1600, 900); break;
            case HOTKEY_1920x1080: ResizeClient(active, 1920, 1080); break;
            case HOTKEY_70PCT:    ResizePercent(active, 70); break;
            case HOTKEY_CENTER:   CenterActiveWindow(active); break;
            case HOTKEY_EXIT:     DestroyWindow(hwnd); break;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup Resources
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (g_hMenu) DestroyMenu(g_hMenu);

    CleanupHotkeys();
    UnregisterClassW(L"CenterWindowHidden", hInstance);

    return 0;
}