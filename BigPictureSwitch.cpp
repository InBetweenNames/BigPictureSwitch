// BigPictureAudioSwitch2.cpp : Defines the entry point for the application.

#pragma warning(disable: 4100) // Disable unreferenced formal parameter warnings

#include "framework.h"
#include "BigPictureSwitch.h"


#include <unordered_set>
#include <optional>
#include <format>
#include <algorithm>
#include <vector>
#include <string>
#include <memory>


#include "BigPictureSwitchAudio.h"
#include "BigPictureSwitchDisplay.h"
#include "BigPictureSwitchCEC.h"



#define MAX_LOADSTRING 100
#define WM_TRAYICON (WM_USER + 1)

// Registry target for Windows startup
#define STARTUP_REGISTRY_PATH L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"
#define APP_REGISTRY_NAME L"BigPictureSwitch"

// Registry target for application settings
#define SETTINGS_REGISTRY_PATH L"SOFTWARE\\BigPictureSwitch"
#define SELECTED_DEVICE_VALUE_NAME L"SelectedAudioDevice"
#define SELECTED_DISPLAY_DEVICE_TARGET L"SelectedDisplayDeviceTarget"
#define SELECTED_DISPLAY_DEVICE_TARGET_MONITOR_FRIENDLY_NAME L"SelectedDisplayDeviceTargetMonitorFriendlyName"
#define SELECTED_DISPLAY_DEVICE_TARGET_MONITOR_PATH L"SelectedDisplayDeviceTargetMonitorPath"
#define SELECTED_DISPLAY_DEVICE_TARGET_ADAPTER_FRIENDLY_NAME L"SelectedDisplayDeviceTargetAdapterFriendlyName"
#define SELECTED_DISPLAY_DEVICE_TARGET_ADAPTER_PATH L"SelectedDisplayDeviceTargetAdapterPath"
#define SELECTED_DISPLAY_EXCLUDE L"SelectedDisplayExcludeFromDesktop"


// Simple debug logging function
void DebugLog(const wchar_t* format, ...)
{
#ifdef _DEBUG
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    vswprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), format, args);
    va_end(args);

    OutputDebugStringW(buffer);
    OutputDebugStringW(L"\n");
#endif
}




// Forward declarations of functions included in this code module:
ATOM                RegisterWindow(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
BOOL                AddTrayIcon(HWND hWnd);
BOOL                RemoveTrayIcon();
void                ShowTrayMenu(HWND hWnd);
BOOL                IsStartupEnabled();
BOOL                SaveStartupEnabled();
WCHAR* GetExecutablePath();
void CALLBACK       WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
BOOL                InitializeWindowEventHook();
void                CleanupWindowEventHook();
BOOL                SaveSettingsToRegistry();
BOOL                LoadSettingsFromRegistry();




// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
NOTIFYICONDATA nid = {};                        // System tray icon data
HWND g_hWnd = nullptr;                          // Hidden window handle
bool g_bStartupEnabled = false;                 // Flag to indicate if the application is set to run at startup

// Steam Big Picture Mode detection globals
HWINEVENTHOOK g_hWinEventHook = nullptr;
bool g_bSteamBigPictureModeRunning = false;
bool g_bExcludeSelectedDisplayFromDesktop = false; // Flag to indicate if the selected display should be excluded from the desktop



int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    DebugLog(L"wWinMain: Application started");

    // Enter Efficiency Mode
    {
        PROCESS_POWER_THROTTLING_STATE PowerThrottling;
        PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        PowerThrottling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        BOOL setResult = SetProcessInformation(
            GetCurrentProcess(),
            ProcessPowerThrottling,
            &PowerThrottling,
            sizeof(PowerThrottling)
        );

        if (!setResult) {
            DWORD error = GetLastError();
            DebugLog(L"wWinMain: SetProcessInformation (Power Throttling) FAILED with error %d", error);

        }

        BOOL setResult2 = SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);

        if (!setResult2) {
            DWORD error = GetLastError();
            DebugLog(L"wWinMain: SetPriorityClass (IDLE_PRIORITY_CLASS) FAILED with error %d", error);

        }

        if (setResult && setResult2) {
            DebugLog(L"wWinMain: Successfully entered Efficiency Mode (Power Throttling and IDLE_PRIORITY_CLASS)");
        }
        else {
            DebugLog(L"wWinMain: Failed to enter Efficiency Mode");
        }
    }

    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        PANIC(L"wWinMain: CoInitializeEx failed: 0x%08X", hr);
    }

    // Initialize global strings
    DebugLog(L"wWinMain: Loading global strings");
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDS_APP_TITLE, szWindowClass, MAX_LOADSTRING);
    RegisterWindow(hInstance);

    // Perform application initialization:
    DebugLog(L"wWinMain: Initializing instance");
    if (!InitInstance(hInstance, SW_HIDE))  // Hide the window
    {
        CoUninitialize();
        PANIC(L"wWinMain: InitInstance failed");

    }

    // Initialize audio system
    DebugLog(L"wWinMain: Initializing audio system");
    InitializeAudio();

    // Load the previously selected audio device
    DebugLog(L"wWinMain: Loading previously selected audio device");
    LoadSettingsFromRegistry();

    // Restore the excluded display configuration if applicable
    if (g_bExcludeSelectedDisplayFromDesktop && g_selectedDisplayTarget) {
        auto dc = GetCurrentDisplayConfiguration();
        SetDesktopDisplayConfiguration(dc);
    }

    // Initialize window event hook for Steam Big Picture Mode detection
    DebugLog(L"wWinMain: Initializing window event hook");
    if (!InitializeWindowEventHook())
    {
        PANIC(L"wWinMain: InitializeWindowEventHook failed");
    }

    MSG msg;

    // Main message loop:
    DebugLog(L"wWinMain: Entering main message loop");
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    DebugLog(L"wWinMain: Cleaning up");
    CleanupWindowEventHook();
    CleanupAudio();
    CoUninitialize();

    DebugLog(L"wWinMain: Exiting with code %d", (int)msg.wParam);
    return (int)msg.wParam;
}

//
//  FUNCTION: RegisterWindow()
//
//  PURPOSE: Registers the window class.
//
ATOM RegisterWindow(HINSTANCE hInstance)
{
    DebugLog(L"RegisterWindow: Registering window class");
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;  // No menu for hidden window
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_ICON));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates hidden window for message handling
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    DebugLog(L"InitInstance: Creating hidden window");
    hInst = hInstance; // Store instance handle in our global variable

    // Create a hidden window for message handling
    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        PANIC(L"InitInstance: CreateWindowW failed");
    }

    g_hWnd = hWnd;

    // Don't show the window - keep it hidden
    // ShowWindow(hWnd, nCmdShow);
    // UpdateWindow(hWnd);

    // Add the system tray icon
    if (!AddTrayIcon(hWnd))
    {
        PANIC(L"InitInstance: AddTrayIcon failed");
    }

    DebugLog(L"InitInstance: Success");
    return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the hidden window and system tray.
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    //DebugLog(L"WndProc: message=0x%04X, wParam=0x%08X, lParam=0x%08X", message, wParam, lParam);
    switch (message)
    {
    case WM_COMMAND:
    {
        auto wmId = LOWORD(wParam);

        if (wmId >= IDM_AUDIODEVICE_BASE && wmId < IDM_AUDIODEVICE_MAX)
        {
            size_t deviceIndex = wmId - IDM_AUDIODEVICE_BASE;
            if (deviceIndex >= 0 && deviceIndex < g_audioDevices.size())
            {
                SetSelectedAudioDevice(g_audioDevices[deviceIndex].id.c_str());
                SaveSettingsToRegistry();
            }
            return 0;
        }
        if (wmId >= IDM_DISPLAYDEVICE_BASE && wmId < IDM_DISPLAYDEVICE_MAX)
        {
            size_t deviceIndex = wmId - IDM_DISPLAYDEVICE_BASE;
            if (deviceIndex >= 0 && deviceIndex < g_displayTargets.size())
            {
                SetSelectedDisplayDevice(g_displayTargets[deviceIndex].first);
                SaveSettingsToRegistry();
            }
            return 0;
        }
        if (wmId == IDM_DISPLAYDEVICE_DISCONNECTED)
        {
			// Handle the disconnected display target case -- simply remove the selected display target
			g_selectedDisplayTarget.reset();
            return 0;
		}

        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_ABOUT:
            // Open the web browser to the GitHub page
            ShellExecuteW(
                nullptr,               // no owner window
                L"open",               // “open” the file/URL
                L"https://github.com/InBetweenNames/BigPictureSwitch",  
                nullptr,               // no extra parameters
                nullptr,               // default working directory
                SW_SHOWNORMAL          // show window normally
            );

			break;
        case IDM_EXCLUDE_DISPLAY_FROM_DESKTOP:
        {
            g_bExcludeSelectedDisplayFromDesktop = !g_bExcludeSelectedDisplayFromDesktop;
            // FIXME: do the change immediately
            // This should work both ways because we use QDC_DATABASE_CURRENT
            auto dc = GetCurrentDisplayConfiguration();
            SetDesktopDisplayConfiguration(dc);
            SaveSettingsToRegistry();
            break;
		}
        case IDM_STARTUP:
        {
			g_bStartupEnabled = !g_bStartupEnabled;
            SaveSettingsToRegistry();
            break;
        }
        case IDM_EXIT:
            RemoveTrayIcon();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;
    case WM_TRAYICON:
    {
        switch (LOWORD(lParam))
        {
        case WM_RBUTTONUP:
        case WM_LBUTTONUP:
            ShowTrayMenu(hWnd);
            break;
            break;
        }
    }
    break;
    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

//
//  FUNCTION: AddTrayIcon(HWND)
//
//  PURPOSE: Adds the application icon to the system tray
//
BOOL AddTrayIcon(HWND hWnd)
{
    DebugLog(L"AddTrayIcon: Adding tray icon");
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON));
    wcscpy_s(nid.szTip, szTitle);

    BOOL result = Shell_NotifyIcon(NIM_ADD, &nid);
    DebugLog(L"AddTrayIcon: Shell_NotifyIcon returned %d", result);
    return result;
}

//
//  FUNCTION: RemoveTrayIcon()
//
//  PURPOSE: Removes the application icon from the system tray
//
BOOL RemoveTrayIcon()
{
    DebugLog(L"RemoveTrayIcon: Removing tray icon");
    BOOL result = Shell_NotifyIcon(NIM_DELETE, &nid);
    DebugLog(L"RemoveTrayIcon: Shell_NotifyIcon returned %d", result);
    return result;
}

//
//  FUNCTION: ShowTrayMenu(HWND)
//
//  PURPOSE: Shows the context menu when the tray icon is clicked
//
void ShowTrayMenu(HWND hWnd)
{
    DebugLog(L"ShowTrayMenu: Showing tray menu");
    HMENU hMenu = CreatePopupMenu();
    if (hMenu)
    {
        // Enumerate audio devices and store them in the global vector
        EnumerateAudioDevices(g_audioDevices);

        // Add audio devices to the menu
        if (!g_audioDevices.empty())
        {
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"Switch to Audio Device:");
            for (size_t i = 0; i < g_audioDevices.size(); ++i)
            {
                UINT deviceFlags = MF_STRING;
                if (g_audioDevices[i].id == g_selectedAudioDeviceId)
                {
                    deviceFlags |= MF_CHECKED;
                }
                AppendMenuW(hMenu, deviceFlags, IDM_AUDIODEVICE_BASE + i, g_audioDevices[i].name.c_str());
            }

            // Add a separator
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        }


        // Show all connected displays and their status

		g_displayTargets = EnumerateConnectedDisplayTargets();
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"Switch to Display:");
        bool found = false;
        for (size_t i = 0; i < g_displayTargets.size(); ++i)
        {
            // Add each display name to the menu
            UINT flags = MF_STRING;
            if (g_displayTargets[i].first == g_selectedDisplayTarget)
            {
                flags |= MF_CHECKED; // Checkmark for the selected display
                found = true;
			}
            AppendMenuW(hMenu, flags, IDM_DISPLAYDEVICE_BASE + i, std::format(L"{} {}", g_displayTargets[i].first, g_displayTargets[i].second).c_str());
		}

        // SPECIAL CASE: Display target is selected, but is invalid (e.g. monitor disconnected)
        if (!found && g_selectedDisplayTarget.has_value())
        {
            AppendMenuW(hMenu, MF_STRING | MF_CHECKED, IDM_DISPLAYDEVICE_DISCONNECTED, std::format(L"{} {}", *g_selectedDisplayTarget, DisplayTargetExStatus::NotPresent).c_str());
		}

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

		UINT flags = MF_STRING;
        if (g_bExcludeSelectedDisplayFromDesktop)
			flags |= MF_CHECKED;
        AppendMenuW(hMenu, flags, IDM_EXCLUDE_DISPLAY_FROM_DESKTOP, L"Exclude selected display from desktop");

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        // Add startup option with checkmark if enabled
        UINT startupFlags = MF_STRING;
        if (IsStartupEnabled())
        {
            startupFlags |= MF_CHECKED;
        }
		AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"Display control (HDMI CEC):");
        //EnumerateConnectedCECDevices();
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);


        AppendMenuW(hMenu, startupFlags, IDM_STARTUP, L"Run on Startup");

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, IDM_ABOUT, L"About");

        // Add another separator and the exit option
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

        // Get cursor position for menu placement
        POINT pt;
        GetCursorPos(&pt);

        // Set foreground window to ensure menu works properly
        SetForegroundWindow(hWnd);

        // Show the context menu
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, nullptr);

        // Destroy the menu once it's no longer needed
        DestroyMenu(hMenu);
    }
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

WCHAR* GetExecutablePath()
{
    static WCHAR szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    return szPath;
}

BOOL IsStartupEnabled()
{
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REGISTRY_PATH, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return FALSE;
    }

    DWORD dwType = REG_SZ;
    DWORD dwSize = 0;
    result = RegQueryValueExW(hKey, APP_REGISTRY_NAME, NULL, &dwType, NULL, &dwSize);
    RegCloseKey(hKey);

    return (result == ERROR_SUCCESS);
}

BOOL SaveStartupEnabled()
{
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REGISTRY_PATH, 0, KEY_WRITE, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return FALSE;
    }

    if (g_bStartupEnabled)
    {
        WCHAR* szPath = GetExecutablePath();
        size_t dwSize = (wcslen(szPath) + 1) * sizeof(WCHAR);
        result = RegSetValueExW(hKey, APP_REGISTRY_NAME, 0, REG_SZ, (BYTE*)szPath, (DWORD)dwSize);
    }
    else
    {
        result = RegDeleteValueW(hKey, APP_REGISTRY_NAME);
    }

    RegCloseKey(hKey);

	DebugLog(L"SaveStartupEnabled: g_bStartupEnabled=%d, result=0x%08X", g_bStartupEnabled, result);
   

    return (result == ERROR_SUCCESS);
}


void EnterBigPictureMode(HWND steamBigPictureModeHwnd)
{
    if (!g_bSteamBigPictureModeRunning)
    {
        DebugLog(L"Entering Steam Big Picture Mode");

        if (g_selectedDisplayTarget)
        {
            // we must find a path that we can enable to get to our target display.
            // It may not exist.  If it doesn't exist, do not switch to it.


            DisplayConfiguration dc = QueryDisplayConfiguration(QDC_ALL_PATHS | QDC_VIRTUAL_REFRESH_RATE_AWARE, nullptr);

            if (auto pathToTarget = FindPathToDisplay(*g_selectedDisplayTarget, dc); pathToTarget) {

                // save old display configuration
                g_origDisplayConfig = GetCurrentDisplayConfiguration();

                // use the selected display device as a topology with exactly one active display
                auto res = SetDisplayConfigWrapper(1, &*pathToTarget,  // only paths
                    static_cast<UINT32>(dc.modes.size()), dc.modes.data(),
                    //SDC_APPLY | SDC_TOPOLOGY_SUPPLIED | SDC_VIRTUAL_REFRESH_RATE_AWARE
                    SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_VIRTUAL_REFRESH_RATE_AWARE | SDC_ALLOW_CHANGES | SDC_SAVE_TO_DATABASE);
              

                if (res != ERROR_SUCCESS)
                {
                    // Make a message box to inform the user
                    std::wstring errorMessage = std::format(L"Failed to switch display configuration: SetDisplayConfig returned {}", res);
                    MessageBoxW(g_hWnd, errorMessage.c_str(), L"Error", MB_OK | MB_ICONERROR);
                }
                else
                {
                    DebugLog(L"EnterBigPictureMode: Successfully switched to external display");
                }


                // testing

                // Get the current screen dimensions
                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);

                // Force window to resize and reposition
                SetWindowPos(steamBigPictureModeHwnd, HWND_TOP, 0, 0, screenWidth, screenHeight,
                    SWP_NOZORDER | SWP_SHOWWINDOW);

                DebugLog(L"Resized window to: %dx%d", screenWidth, screenHeight);
            }
            else
            {
                DebugLog(L"EnterBigPictureMode: Could not find path to selected display target");
			}
        }
        else
        {
			DebugLog(L"EnterBigPictureMode: Display switching is disabled");
        }
        

        // Switch to the selected audio device if one is configured (do this after the display configuration change)
        if (g_selectedAudioDeviceId)
        {
            std::wstring mainDeviceName;
            if (SUCCEEDED(GetCurrentAudioDevice(g_origAudioDeviceId, mainDeviceName)))
            {
                SetDefaultAudioDevice(g_selectedAudioDeviceId->c_str());
                DebugLog(L"EnterBigPictureMode: Switched audio from '%s' to '%s'",
                    g_origAudioDeviceId.c_str(), g_selectedAudioDeviceId->c_str());
            }
        }
        else {
            DebugLog(L"EnterBigPictureMode: Audio switching is disabled");
        }
        
        g_bSteamBigPictureModeRunning = true;
    }
}

void SetDesktopDisplayConfiguration(DisplayConfiguration& dc)
{
    // restore original display configuration exactly as it was before entering Big Picture Mode

    if (g_bExcludeSelectedDisplayFromDesktop && g_selectedDisplayTarget)
    {
        // Need to remove the selected display from the g_origDisplayConfig
        std::erase_if(dc.paths, [&](const DISPLAYCONFIG_PATH_INFO& path) {
            return GetDisplayTargetExForPath(path) == g_selectedDisplayTarget;
            });

    }

    auto res = SetDisplayConfigWrapper(static_cast<UINT32>(dc.paths.size()), dc.paths.data(),
        static_cast<UINT32>(dc.modes.size()), dc.modes.data(), SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_VIRTUAL_MODE_AWARE | SDC_VIRTUAL_REFRESH_RATE_AWARE | SDC_ALLOW_CHANGES | SDC_SAVE_TO_DATABASE);


    if (res != ERROR_SUCCESS)
    {
        std::wstring errorMessage = std::format(L"Failed to switch display configuration: SetDisplayConfig returned {}", res);
        MessageBoxW(g_hWnd, errorMessage.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }
    else
    {
        DebugLog(L"SetDesktopDisplayConfiguration: Successfully switched to selected display");
    }
}

void ExitBigPictureMode()
{
    if (g_bSteamBigPictureModeRunning)
    {
        DebugLog(L"Exiting Steam Big Picture Mode");

        if (g_origDisplayConfig)
        {

			// restore original display configuration as it was before entering Big Picture Mode (most recent saved database config)

            SetDesktopDisplayConfiguration(*g_origDisplayConfig);
            g_origDisplayConfig.reset();
        }
        
        // Restore original audio device
        if (!g_origAudioDeviceId.empty())
        {
            SetDefaultAudioDevice(g_origAudioDeviceId.c_str());
            DebugLog(L"ExitBigPictureMode: Restored audio to '%s'", g_origAudioDeviceId.c_str());
            g_origAudioDeviceId.clear();
        }
        

        g_bSteamBigPictureModeRunning = false;
    }

}

bool WindowMatches(WCHAR* szClassName, WCHAR* szWindowTitle)
{
    return wcscmp(szClassName, L"SDL_app") == 0 && wcscmp(szWindowTitle, L"Steam Big Picture Mode") == 0;
}

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    //DebugLog(L"WinEventProc: event=0x%08X, hwnd=0x%p, idObject=%ld, idChild=%ld", event, hwnd, idObject, idChild);
    // We only care about window-level events
    if (idObject != OBJID_WINDOW || !hwnd)
        return;

    WCHAR szClassName[256] = { 0 };
    WCHAR szWindowTitle[256] = { 0 };

    // Get the window class name
    if (GetClassNameW(hwnd, szClassName, _countof(szClassName)) == 0)
        return;

    // Get the window title
    if (GetWindowTextW(hwnd, szWindowTitle, _countof(szWindowTitle)) == 0)
        return;

    // Check if this is Steam Big Picture Mode
    if (WindowMatches(szClassName, szWindowTitle))
    {
        if (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW)
        {
            EnterBigPictureMode(hwnd);
        }
        else if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_HIDE)
        {
            ExitBigPictureMode();
        }
    }
}

BOOL InitializeWindowEventHook()
{
    DebugLog(L"InitializeWindowEventHook: Setting up event hooks");
    // Set up event hook to listen for window creation/destruction events
    g_hWinEventHook = SetWinEventHook(
        EVENT_OBJECT_CREATE,        // eventMin
        EVENT_OBJECT_DESTROY,       // eventMax  
        NULL,                       // hmodWinEventProc
        WinEventProc,              // lpfnWinEventProc
        0,                         // idProcess (0 = all processes)
        0,                         // idThread (0 = all threads)
        WINEVENT_OUTOFCONTEXT      // dwFlags
    );

    if (g_hWinEventHook == NULL)
        return FALSE;

    // Also check for show/hide events
    HWINEVENTHOOK hShowHideHook = SetWinEventHook(
        EVENT_OBJECT_SHOW,          // eventMin
        EVENT_OBJECT_HIDE,          // eventMax
        NULL,                       // hmodWinEventProc
        WinEventProc,              // lpfnWinEventProc
        0,                         // idProcess (0 = all processes)
        0,                         // idThread (0 = all threads)
        WINEVENT_OUTOFCONTEXT      // dwFlags
    );

    DebugLog(L"InitializeWindowEventHook: g_hWinEventHook=0x%p", g_hWinEventHook);
    DebugLog(L"InitializeWindowEventHook: hShowHideHook=0x%p", hShowHideHook);
    return TRUE;
}

void CleanupWindowEventHook()
{
    DebugLog(L"CleanupWindowEventHook: Cleaning up event hooks");
    if (g_hWinEventHook)
    {
        UnhookWinEvent(g_hWinEventHook);
        g_hWinEventHook = nullptr;
    }
}




BOOL SaveSettingsToRegistry()
{
    DebugLog(L"SaveSettingsToRegistry: value='%s'", g_selectedAudioDeviceId ? g_selectedAudioDeviceId->c_str() : L"(null)");


    HKEY hKey;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_REGISTRY_PATH, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (result != ERROR_SUCCESS)
    {
        return FALSE;
    }

    if (g_selectedAudioDeviceId)
    {

        size_t dwSize = (g_selectedAudioDeviceId->size() + 1) * sizeof(WCHAR);
        result = RegSetValueExW(hKey, SELECTED_DEVICE_VALUE_NAME, 0, REG_SZ, (const BYTE*)g_selectedAudioDeviceId->c_str(), (DWORD)dwSize);

        DebugLog(L"SaveSettingsToRegistry: SELECTED_DEVICE result=%d, g_selectedAudioDeviceId='%s'", (result == ERROR_SUCCESS), g_selectedAudioDeviceId->c_str());

    }
    else {
        result = RegDeleteValueW(hKey, SELECTED_DEVICE_VALUE_NAME);
        DebugLog(L"SaveSettingsToRegistry: RegDeleteValueW result=%d", (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND));
    }


    if (g_selectedDisplayTarget)
    {
		// Save the selected display target information.
		// ONLY save the g_selectedDisplayTarget->id.  Do it as REG_DWORD or REG_QWORD values.

		static_assert(std::has_unique_object_representations_v<decltype(g_selectedDisplayTarget->id)>, "AdapterPair must have unique object representations for registry storage");
        static_assert(std::is_standard_layout_v<decltype(g_selectedDisplayTarget->id)>, "AdapterPair must be standard layout for registry storage");

        result = RegSetValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET, 0, REG_BINARY,
			(const BYTE*)&g_selectedDisplayTarget->id, sizeof(g_selectedDisplayTarget->id));
		DebugLog(L"SaveSettingsToRegistry: SELECTED_DISPLAY_DEVICE_TARGET result=%d", (result == ERROR_SUCCESS));

		DWORD dwSize = static_cast<DWORD>((g_selectedDisplayTarget->monitorFriendlyName.size() + 1) * sizeof(WCHAR));

        result = RegSetValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_MONITOR_FRIENDLY_NAME, 0, REG_SZ,
            (const BYTE*)g_selectedDisplayTarget->monitorFriendlyName.c_str(), dwSize);
        DebugLog(L"SaveSettingsToRegistry: SELECTED_DISPLAY_DEVICE_TARGET_MONITOR_FRIENDLY_NAME result=%d", (result == ERROR_SUCCESS));

        dwSize = static_cast<DWORD>((g_selectedDisplayTarget->monitorDevicePath.size() + 1) * sizeof(WCHAR));

        result = RegSetValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_MONITOR_PATH, 0, REG_SZ,
            (const BYTE*)g_selectedDisplayTarget->monitorDevicePath.c_str(), dwSize);
        DebugLog(L"SaveSettingsToRegistry: SELECTED_DISPLAY_DEVICE_TARGET_MONITOR_PATH result=%d", (result == ERROR_SUCCESS));

        dwSize = static_cast<DWORD>((g_selectedDisplayTarget->adapterFriendlyName.size() + 1) * sizeof(WCHAR));

        result = RegSetValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_ADAPTER_FRIENDLY_NAME, 0, REG_SZ,
            (const BYTE*)g_selectedDisplayTarget->adapterFriendlyName.c_str(), dwSize);
        DebugLog(L"SaveSettingsToRegistry: SELECTED_DISPLAY_DEVICE_TARGET_ADAPTER_FRIENDLY_NAME result=%d", (result == ERROR_SUCCESS));

        dwSize = static_cast<DWORD>((g_selectedDisplayTarget->adapterDevicePath.size() + 1) * sizeof(WCHAR));

        result = RegSetValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_ADAPTER_PATH, 0, REG_SZ,
            (const BYTE*)g_selectedDisplayTarget->adapterDevicePath.c_str(), dwSize);
        DebugLog(L"SaveSettingsToRegistry: SELECTED_DISPLAY_DEVICE_TARGET_ADAPTER_PATH result=%d", (result == ERROR_SUCCESS));

    }
	else {
        result = RegDeleteValueW(hKey, SELECTED_DISPLAY_DEVICE_TARGET);
        DebugLog(L"SaveSettingsToRegistry: RegDeleteValueW result=%d", (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND));
    }
    DWORD exclude = g_bExcludeSelectedDisplayFromDesktop ? 1 : 0;
    result = RegSetValueExW(hKey, SELECTED_DISPLAY_EXCLUDE, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&exclude), sizeof(exclude));
    DebugLog(L"SaveSettingsToRegistry: SELECTED_DISPLAY_EXCLUDE result=%d", (result == ERROR_SUCCESS));

    RegCloseKey(hKey);

    SaveStartupEnabled();

    return (result == ERROR_SUCCESS);
}

BOOL LoadSettingsFromRegistry()
{
    DebugLog(L"LoadSettingsFromRegistry: Loading selected audio device from registry");
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_REGISTRY_PATH, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return FALSE;
    }

    WCHAR value[512];
    DWORD dwType = REG_SZ;
    DWORD dwSize = sizeof(value);
    result = RegQueryValueExW(hKey, SELECTED_DEVICE_VALUE_NAME, NULL, &dwType, (BYTE*)value, &dwSize);

    if (result == ERROR_SUCCESS)
    {
        g_selectedAudioDeviceId = value;
        DebugLog(L"LoadSettingsFromRegistry: result=%d, g_selectedAudioDeviceId='%s'", (result == ERROR_SUCCESS), g_selectedAudioDeviceId->c_str());
    }



    // Load the selected display target from the registry
    std::optional<AdapterPair> selectedDisplayTarget;
    std::optional<std::wstring> monitorFriendlyName;
    std::optional<std::wstring> monitorDevicePath;
    std::optional<std::wstring> adapterFriendlyName;
    std::optional<std::wstring> adapterDevicePath;



    static_assert(std::has_unique_object_representations_v<decltype(g_selectedDisplayTarget->id)>, "AdapterPair must have unique object representations for registry storage");
    static_assert(std::is_standard_layout_v<decltype(g_selectedDisplayTarget->id)>, "AdapterPair must be standard layout for registry storage");

	char buf[sizeof(AdapterPair)];

    dwSize = sizeof(AdapterPair);

    result = RegQueryValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET, NULL, &dwType, (BYTE*)&buf, &dwSize);
    if (result == ERROR_SUCCESS && dwType == REG_BINARY && dwSize == sizeof(AdapterPair))
    {
        selectedDisplayTarget = AdapterPair();
        memcpy(&selectedDisplayTarget, &buf, sizeof(AdapterPair));
    }


    // Now load the friendly name for the display target
    dwSize = sizeof(value);
    result = RegQueryValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_MONITOR_FRIENDLY_NAME, NULL, &dwType, (BYTE*)value, &dwSize);
    if (result == ERROR_SUCCESS && dwType == REG_SZ)
    {
        monitorFriendlyName = value;
        DebugLog(L"LoadSettingsFromRegistry: Loaded monitor friendly name '%s'", value);

    }


    dwSize = sizeof(value);
    result = RegQueryValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_MONITOR_PATH, NULL, &dwType, (BYTE*)value, &dwSize);
    if (result == ERROR_SUCCESS && dwType == REG_SZ)
    {
        monitorDevicePath = value;
        DebugLog(L"LoadSettingsFromRegistry: Loaded monitor path '%s'", value);

    }

    dwSize = sizeof(value);
    result = RegQueryValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_ADAPTER_PATH, NULL, &dwType, (BYTE*)value, &dwSize);
    if (result == ERROR_SUCCESS && dwType == REG_SZ)
    {
        adapterDevicePath = value;
        DebugLog(L"LoadSettingsFromRegistry: Loaded adapter path '%s'", value);

    }

    dwSize = sizeof(value);
    result = RegQueryValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_ADAPTER_FRIENDLY_NAME, NULL, &dwType, (BYTE*)value, &dwSize);
    if (result == ERROR_SUCCESS && dwType == REG_SZ)
    {
        adapterFriendlyName = value;
        DebugLog(L"LoadSettingsFromRegistry: Loaded adapter friendly name '%s'", value);

    }

    if (selectedDisplayTarget && monitorFriendlyName && adapterFriendlyName && monitorDevicePath && adapterDevicePath)
    {
		DebugLog(L"LoadSettingsFromRegistry: Successfully loaded display target information");

        // Create the DisplayTargetEx object
        g_selectedDisplayTarget = DisplayTargetEx(
            *selectedDisplayTarget,
            *monitorFriendlyName,
            *adapterFriendlyName,
            *monitorDevicePath,
            *adapterDevicePath);
    }
    else {
		DebugLog(L"Missing display target information in registry, resetting g_selectedDisplayTarget");

        g_selectedDisplayTarget.reset();
    }

    dwSize = sizeof(value);
    result = RegQueryValueExW(hKey, SELECTED_DEVICE_VALUE_NAME, 0, &dwType, (BYTE*)value, &dwSize);

    if (result == ERROR_SUCCESS)
    {
        g_selectedAudioDeviceId = value;
        DebugLog(L"LoadSettingsFromRegistry: result=%d, g_selectedAudioDeviceId='%s'", (result == ERROR_SUCCESS), g_selectedAudioDeviceId->c_str());
    }

    DWORD exclude = 0;
    dwSize = sizeof(DWORD);
    result = RegQueryValueExW(hKey, SELECTED_DISPLAY_EXCLUDE, 0, &dwType, reinterpret_cast<BYTE*>(&exclude), &dwSize);

    if (result == ERROR_SUCCESS)
    {
        g_bExcludeSelectedDisplayFromDesktop = exclude != 0;
    }
    DebugLog(L"LoadSettingsFromRegistry: result=%d, g_bExcludeSelectedDisplayFromDesktop = %d", exclude);


	g_bStartupEnabled = IsStartupEnabled();


    RegCloseKey(hKey);

    return FALSE;
}

