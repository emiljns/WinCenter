#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <algorithm>

#include "resource.h"

// ============================================================
// Constants
// ============================================================

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

constexpr int MIN_CLIENT_W = 400;
constexpr int MIN_CLIENT_H = 300;

// ============================================================
// Globals
// ============================================================

HMENU g_hMenu = nullptr;
HWND  g_trayWnd = nullptr;

// Last foreground window we could actually resize (kept by the hook).
HWND g_lastForeground = nullptr;

// Single-use snapshot for the currently open tray menu.
HWND g_target = nullptr;

using AdjustProc = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);

static AdjustProc pAdjust = reinterpret_cast<AdjustProc>(
    GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));

// ============================================================
// Helpers
// ============================================================

// Single source of truth for "can we work with this window?"
bool IsResizableTarget(HWND hwnd)
{
    if (!hwnd || hwnd == g_trayWnd) return false;
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
    if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) return false;

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    if (!(style & WS_THICKFRAME)) return false;
    if (exStyle & WS_EX_TOOLWINDOW) return false;

    return true;
}

void CALLBACK ForegroundProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG, LONG, DWORD, DWORD)
{
    if (IsResizableTarget(hwnd))
        g_lastForeground = hwnd;
}

void CleanupHotkeys()
{
    UnregisterHotKey(nullptr, HOTKEY_1280x720);
    UnregisterHotKey(nullptr, HOTKEY_1600x900);
    UnregisterHotKey(nullptr, HOTKEY_1920x1080);
    UnregisterHotKey(nullptr, HOTKEY_70PCT);
    UnregisterHotKey(nullptr, HOTKEY_CENTER);
    UnregisterHotKey(nullptr, HOTKEY_EXIT);
}

// ============================================================
// Center Window
// ============================================================

void CenterWindow(HWND hwnd)
{
    if (!IsResizableTarget(hwnd)) return;
    if (IsIconic(hwnd)) return;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return;

    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - width) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - height) / 2;

    SetWindowPos(hwnd, nullptr, x, y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ============================================================
// Resize Client Area
// ============================================================

void ResizeClient(HWND hwnd, int clientWidth, int clientHeight)
{
    if (!IsResizableTarget(hwnd)) return;
    if (IsIconic(hwnd)) return;

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    DWORD    exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED)
        ShowWindow(hwnd, SW_RESTORE);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return;

    int monitorWidth = mi.rcWork.right - mi.rcWork.left;
    int monitorHeight = mi.rcWork.bottom - mi.rcWork.top;

    clientWidth = std::max(MIN_CLIENT_W, clientWidth);
    clientHeight = std::max(MIN_CLIENT_H, clientHeight);

    // Add the frame first...
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

    // ...then clamp the FINAL window size so the frame never spills offscreen.
    int windowWidth = std::min(static_cast<int>(rc.right - rc.left), monitorWidth);
    int windowHeight = std::min(static_cast<int>(rc.bottom - rc.top), monitorHeight);

    int x = mi.rcWork.left + (monitorWidth - windowWidth) / 2;
    int y = mi.rcWork.top + (monitorHeight - windowHeight) / 2;

    SetWindowPos(hwnd, nullptr, x, y, windowWidth, windowHeight,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void ResizePercent(HWND hwnd, int percent)
{
    if (!IsResizableTarget(hwnd)) return;

    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return;

    int newWidth = (rc.right - rc.left) * percent / 100;
    int newHeight = (rc.bottom - rc.top) * percent / 100;

    ResizeClient(hwnd, newWidth, newHeight);
}

// ============================================================
// Tray Window Procedure
// ============================================================

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            // Snapshot BEFORE we steal foreground for the menu.
            g_target = g_lastForeground;

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
        // Exit never needs a target, so handle it first.
        if (LOWORD(wParam) == MENU_EXIT)
        {
            DestroyWindow(hwnd);
            break;
        }

        HWND active = g_target;
        g_target = nullptr; // snapshot is single-use

        if (!IsResizableTarget(active))
            break;

        switch (LOWORD(wParam))
        {
        case MENU_1280x720:  ResizeClient(active, 1280, 720);  break;
        case MENU_1600x900:  ResizeClient(active, 1600, 900);  break;
        case MENU_1920x1080: ResizeClient(active, 1920, 1080); break;
        case MENU_70PCT:     ResizePercent(active, 70);        break;
        case MENU_CENTER:    CenterWindow(active);             break;
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

// ============================================================
// Entry Point
// ============================================================

#pragma warning(suppress: 28251)
int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();

    // ---- Hotkeys ----
    constexpr UINT MODS = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;

    if (!RegisterHotKey(nullptr, HOTKEY_1280x720, MODS, '1') ||
        !RegisterHotKey(nullptr, HOTKEY_1600x900, MODS, '2') ||
        !RegisterHotKey(nullptr, HOTKEY_1920x1080, MODS, '3') ||
        !RegisterHotKey(nullptr, HOTKEY_70PCT, MODS, '4') ||
        !RegisterHotKey(nullptr, HOTKEY_CENTER, MODS, 'C') ||
        !RegisterHotKey(nullptr, HOTKEY_EXIT, MODS, 'Q'))
    {
        MessageBoxW(nullptr, L"Failed to register one or more hotkeys.",
            L"Error", MB_OK | MB_ICONERROR);
        CleanupHotkeys();
        return 1;
    }

    // ---- Hidden window ----
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

    g_trayWnd = hwnd;

    // ---- Foreground tracking ----
    HWINEVENTHOOK hook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, ForegroundProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    // Seed with the same filter the hook uses.
    HWND fg = GetForegroundWindow();
    if (IsResizableTarget(fg))
        g_lastForeground = fg;

    // ---- Tray icon ----
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
        if (hook) UnhookWinEvent(hook);
        DestroyWindow(hwnd);
        CleanupHotkeys();
        return 1;
    }

    // ---- Menu ----
    g_hMenu = CreatePopupMenu();
    AppendMenuW(g_hMenu, MF_STRING, MENU_1280x720, L"Resize 1280x720 (Ctrl+Alt+1)");
    AppendMenuW(g_hMenu, MF_STRING, MENU_1600x900, L"Resize 1600x900 (Ctrl+Alt+2)");
    AppendMenuW(g_hMenu, MF_STRING, MENU_1920x1080, L"Resize 1920x1080 (Ctrl+Alt+3)");
    AppendMenuW(g_hMenu, MF_STRING, MENU_70PCT, L"Resize 70% (Ctrl+Alt+4)");
    AppendMenuW(g_hMenu, MF_STRING, MENU_CENTER, L"Center Window (Ctrl+Alt+C)");
    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_hMenu, MF_STRING, MENU_EXIT, L"Exit (Ctrl+Alt+Q)");

    // ---- Message loop ----
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
        {
            HWND active = GetForegroundWindow();

            switch (msg.wParam)
            {
            case HOTKEY_1280x720:  ResizeClient(active, 1280, 720);  break;
            case HOTKEY_1600x900:  ResizeClient(active, 1600, 900);  break;
            case HOTKEY_1920x1080: ResizeClient(active, 1920, 1080); break;
            case HOTKEY_70PCT:     ResizePercent(active, 70);        break;
            case HOTKEY_CENTER:    CenterWindow(active);             break;
            case HOTKEY_EXIT:      DestroyWindow(hwnd);              break;
            }
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ---- Cleanup ----
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (hook) UnhookWinEvent(hook);
    if (g_hMenu) DestroyMenu(g_hMenu);
    CleanupHotkeys();
    UnregisterClassW(L"CenterWindowHidden", hInstance);

    return 0;
}