// BigPictureAudioSwitch2.cpp : Defines the entry point for the application.
//

 /* Exclude redundant APIs such as Cryptography, DDE, RPC, Shell, and Windows Sockets. */
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include "framework.h"
#include "BigPictureSwitch.h"
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <comdef.h>
#include <vector>
#include <string>
#include <propsys.h>
#include <initguid.h>
#include <mmreg.h>
#include <shlwapi.h>
#include <debugapi.h>
#include <iostream>
#include <sstream>
#include <processthreadsapi.h>
#include <algorithm>

#include <unordered_map>
#include <unordered_set>

#include <winuser.h>
#include <setupapi.h>
#include <devpkey.h>          // For DEVPKEY_Device_FriendlyName
#include <initguid.h>
#include <devguid.h>          // For GUID_DEVCLASS_DISPLAY
#include <cfgmgr32.h>         // For CM_Get_Device_IDW

#include <optional>
#include <format>

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

// Undocumented interface for setting default audio device
interface DECLSPEC_UUID("f8679f50-850a-41cf-9c72-430f290290c8") IPolicyConfig;
class DECLSPEC_UUID("870af99c-171d-4f9e-af0d-e63df40c2bc9") CPolicyConfigClient;

// ----------------------------------------------------------------------------
// class CPolicyConfigClient
// {870af99c-171d-4f9e-af0d-e63df40c2bc9}
//
// interface IPolicyConfig
// {f8679f50-850a-41cf-9c72-430f290290c8}
//
// Query interface:
// CComPtr<IPolicyConfig> PolicyConfig;
// PolicyConfig.CoCreateInstance(__uuidof(CPolicyConfigClient));
//
// @compatible: Windows 7 and Later
// ----------------------------------------------------------------------------
interface IPolicyConfig : public IUnknown
{
public:

        virtual HRESULT GetMixFormat(
                PCWSTR,
                WAVEFORMATEX**
        );

        virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(
                PCWSTR,
                INT,
                WAVEFORMATEX**
        );

        virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(
                PCWSTR
        );

        virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(
                PCWSTR,
                WAVEFORMATEX*,
                WAVEFORMATEX*
        );

        virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(
                PCWSTR,
                INT,
                PINT64,
                PINT64
        );

        virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(
                PCWSTR,
                PINT64
        );

        virtual HRESULT STDMETHODCALLTYPE GetShareMode(
                PCWSTR,
                struct DeviceShareMode*
        );

        virtual HRESULT STDMETHODCALLTYPE SetShareMode(
                PCWSTR,
                struct DeviceShareMode*
        );

        virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(
                PCWSTR,
                const PROPERTYKEY&,
                PROPVARIANT*
        );

        virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(
                PCWSTR,
                const PROPERTYKEY&,
                PROPVARIANT*
        );

        virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(
                __in PCWSTR wszDeviceId,
                __in ERole eRole
        );

        virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(
                PCWSTR,
                INT
        );
};

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

// Alternative macro for easier usage
#ifdef _DEBUG
#define DEBUG_LOG(fmt, ...) DebugLog(fmt, ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif


// Panic macro to log an error and exit the application
#define PANIC(msg, ...) do { \
    wchar_t buffer[512]; \
    swprintf_s(buffer, L"PANIC: " msg L" at %S:%d", ##__VA_ARGS__, __FILE__, __LINE__); \
    FatalAppExit(0, buffer); \
} while(0)




struct AudioDevice
{
    std::wstring id;
    std::wstring name;
};

struct DisplayConfiguration
{
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
};

struct AdapterPair
{
    LUID adapterId;
    UINT32 id;

    AdapterPair() = default;

    AdapterPair(const DISPLAYCONFIG_PATH_TARGET_INFO& target) : adapterId(target.adapterId), id(target.id)
    {
    }

    bool operator==(const AdapterPair& other) const
    {
        return adapterId.LowPart == other.adapterId.LowPart &&
            adapterId.HighPart == other.adapterId.HighPart &&
            id == other.id;
    }
};

template<>
struct std::hash<AdapterPair>
{
    std::size_t operator()(const AdapterPair& s) const noexcept
    {
        std::size_t h1 = std::hash<DWORD>{}(s.adapterId.LowPart);
        std::size_t h2 = std::hash<LONG>{}(s.adapterId.HighPart);
        std::size_t h3 = std::hash<UINT32>{}(s.id);

        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

enum struct DisplayTargetExStatus
{
    Inactive,
    Active,
    Primary,
    NotPresent
};;

template<>
struct std::formatter<DisplayTargetExStatus, wchar_t> {
    // Parse format specifiers (we ignore any here).
    constexpr auto parse(wformat_parse_context& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const DisplayTargetExStatus& p, FormatContext& ctx) const {
        // ctx.out() is an iterator to the output buffer

        switch (p)
        {
            default:
            case DisplayTargetExStatus::Inactive:
                return std::format_to(
                    ctx.out(),
                    L""
                );
            case DisplayTargetExStatus::Active:
                return std::format_to(
                    ctx.out(),
                    L"[Active]"
                );
            case DisplayTargetExStatus::Primary:
                return std::format_to(
                    ctx.out(),
                    L"[Primary]"
                );
            case DisplayTargetExStatus::NotPresent:
                return std::format_to(
                    ctx.out(),
                    L"[Not Present]"
                );
        }

    }
};

struct DisplayTargetEx
{
    AdapterPair id;
    std::wstring monitorFriendlyName;
    std::wstring adapterFriendlyName;

    // Used for comparison
    std::wstring monitorDevicePath;
	std::wstring adapterDevicePath;

};

template<>
struct std::formatter<DisplayTargetEx, wchar_t> {
    // Parse format specifiers (we ignore any here).
    constexpr auto parse(wformat_parse_context& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const DisplayTargetEx& p, FormatContext& ctx) const {
        // ctx.out() is an iterator to the output buffer
        return std::format_to(
            ctx.out(),
            L"{} ({})", 
			p.monitorFriendlyName.empty() ? L"Unknown Display" : p.monitorFriendlyName,
			p.adapterFriendlyName.empty() ? L"Unknown Adapter" : p.adapterFriendlyName
        );
    }
};

bool operator==(const DisplayTargetEx& lhs, const DisplayTargetEx& rhs)
{
	// We don't care about comparing status.  We do care about expected names as this is a sign the configuration has changed.
    return lhs.id == rhs.id
        && rhs.monitorDevicePath == lhs.monitorDevicePath
        && lhs.adapterDevicePath == rhs.adapterDevicePath;
}


// Forward declarations of functions included in this code module:
ATOM                RegisterWindow(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
BOOL                AddTrayIcon(HWND hWnd);
BOOL                RemoveTrayIcon();
void                ShowTrayMenu(HWND hWnd);
void                InitializeAudio();
void                CleanupAudio();
HRESULT             GetCurrentAudioDevice(std::wstring& ppwszDeviceId, std::wstring& ppwszDeviceName);
HRESULT             EnumerateAudioDevices(std::vector<AudioDevice>& devices);
HRESULT             SetDefaultAudioDevice(LPCWSTR deviceId);
BOOL                IsStartupEnabled();
BOOL                SaveStartupEnabled();
WCHAR* GetExecutablePath();
void CALLBACK       WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
BOOL                InitializeWindowEventHook();
void                CleanupWindowEventHook();
BOOL                SaveSettingsToRegistry();
BOOL                LoadSettingsFromRegistry();
void                SetSelectedAudioDevice(LPCWSTR deviceId);
LPCWSTR             GetSelectedAudioDevice();

std::vector<std::wstring> GetDisplayNamesFromPaths(const std::vector<DISPLAYCONFIG_PATH_INFO>& paths);
void                SetSelectedDisplayDevice(const DisplayTargetEx& path);
std::optional<DisplayTargetEx> GetDisplayTargetExForPath(const DISPLAYCONFIG_PATH_INFO& path);
std::optional<DISPLAYCONFIG_PATH_INFO> FindPathToDisplay(const DisplayTargetEx& target, DisplayConfiguration& dc);
DisplayConfiguration GetCurrentDisplayConfiguration();
std::vector<std::pair<DisplayTargetEx, DisplayTargetExStatus>> EnumerateConnectedDisplayTargets();
DisplayConfiguration QueryDisplayConfiguration(const UINT32 flags, DISPLAYCONFIG_TOPOLOGY_ID* currentTopology);


// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
NOTIFYICONDATA nid = {};                        // System tray icon data
HWND g_hWnd = nullptr;                          // Hidden window handle

// Audio-related globals
IMMDeviceEnumerator* g_pEnumerator = nullptr;
bool g_bStartupEnabled = false; // Flag to indicate if the application is set to run at startup
std::vector<AudioDevice> g_audioDevices; // Store enumerated audio devices
std::optional<std::wstring> g_selectedAudioDeviceId = {}; // Store the user's selected audio device
std::wstring g_origAudioDeviceId; // Store the main audio device name


// GUID for the PolicyConfig interface
IPolicyConfig* g_pPolicyConfig = nullptr; // Pointer to the PolicyConfig interface for setting default audio device

// Steam Big Picture Mode detection globals
HWINEVENTHOOK g_hWinEventHook = nullptr;
BOOL g_bSteamBigPictureModeRunning = FALSE;
std::optional<DisplayConfiguration> g_origDisplayConfig;
std::vector<std::pair<DisplayTargetEx, DisplayTargetExStatus>> g_displayTargets;
std::optional<DisplayTargetEx> g_selectedDisplayTarget;


std::wstring GetFriendlyNameFromInstanceId(const std::wstring& instanceId)
{
    // 1. Get a device info set for the DISPLAY class
    HDEVINFO devs = SetupDiGetClassDevs(
        &GUID_DEVCLASS_DISPLAY,
        nullptr,
        nullptr,
        DIGCF_PRESENT
    );
    if (devs == INVALID_HANDLE_VALUE)
        return L"";

    SP_DEVINFO_DATA devInfo = {};
	devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
    DWORD idx = 0;
    std::wstring friendly;

    // 2. Iterate all display devices
    while (SetupDiEnumDeviceInfo(devs, idx++, &devInfo))
    {
        // 3. Match instance ID
        WCHAR bufId[512];
        if (CM_Get_Device_IDW(devInfo.DevInst, bufId, _countof(bufId), 0) == CR_SUCCESS &&
            _wcsicmp(bufId, instanceId.c_str()) == 0)
        {
            // 4a. Try the new FriendlyName property
            DEVPROPTYPE propType;
            WCHAR bufName[512];
            DWORD cbSize = 0;
            BOOL status = SetupDiGetDevicePropertyW(
                devs,
                &devInfo,
                &DEVPKEY_Device_FriendlyName,
                &propType,
                (BYTE*)bufName,
                sizeof(bufName),
                &cbSize,
                0
            );

            if (status)
            {
                friendly = bufName;
            }
            else if (GetLastError() == ERROR_NOT_FOUND)
            {
                // If the property is not found, we can try to get the device description
                // using the DEVPKEY_Device_DeviceDesc property.
                status = SetupDiGetDevicePropertyW(
                    devs,
                    &devInfo,
                    &DEVPKEY_Device_DeviceDesc,
                    &propType,
                    (BYTE*)bufName,
                    sizeof(bufName),
                    &cbSize,
                    0
                );
                if (status)
                {
                    friendly = bufName;
                }
            }

            break;
        }
    }

    SetupDiDestroyDeviceInfoList(devs);
    return friendly;
}

// TODO:
// * Enumerate all displays
// * Allow user to select a display for Big Picture Mode
// * Save current active topology (+ selected display as inactive)
// * Set new toplgy for Big Picture Mode (all displays inactive EXCEPT selected display, which is active)
// * Restore topology on exit

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
    LoadStringW(hInstance, IDC_BIGPICTURESWITCH, szWindowClass, MAX_LOADSTRING);
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
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"Switch to Display Target:");
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

        // Add startup option with checkmark if enabled
        UINT startupFlags = MF_STRING;
        if (IsStartupEnabled())
        {
            startupFlags |= MF_CHECKED;
        }
        AppendMenuW(hMenu, startupFlags, IDM_STARTUP, L"Run on Startup");

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

//
//  FUNCTION: InitializeAudio()
//
//  PURPOSE: Initializes the audio device enumerator for COM audio APIs
//
void InitializeAudio()
{
    DebugLog(L"InitializeAudio: Initializing audio enumerator");
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&g_pEnumerator);

    if (FAILED(hr))
    {
        PANIC(L"InitializeAudio: CoCreateInstance for IMMDeviceEnumerator failed: 0x%08X", hr);
    }

    hr = CoCreateInstance(__uuidof(CPolicyConfigClient), NULL, CLSCTX_ALL, __uuidof(IPolicyConfig), (LPVOID*)&g_pPolicyConfig);

    if (FAILED(hr))
    {
        PANIC(L"InitializeAudio: CoCreateInstance for IPolicyConfig failed: 0x%08X", hr);
    }

}

//
//  FUNCTION: CleanupAudio()
//
//  PURPOSE: Releases audio COM objects
//
void CleanupAudio()
{
    DebugLog(L"CleanupAudio: Cleaning up audio resources");

    if (g_pEnumerator)
    {
        g_pEnumerator->Release();
        g_pEnumerator = nullptr;
    }

    if (g_pPolicyConfig)
    {
        g_pPolicyConfig->Release();
        g_pPolicyConfig = nullptr;
    }
}

//
//  FUNCTION: GetCurrentAudioDevice(LPWSTR*, LPWSTR*)
//
//  PURPOSE: Gets the current default audio playback device ID and friendly name
//
HRESULT GetCurrentAudioDevice(std::wstring& deviceId, std::wstring& deviceName)
{
    DebugLog(L"GetCurrentAudioDevice: Getting current audio device");
    if (!g_pEnumerator)
        return E_INVALIDARG;

    LPWSTR ppwszDeviceId = nullptr;

    IMMDevice* pDevice = nullptr;
    IPropertyStore* pProps = nullptr;
    PROPVARIANT varName;
    PropVariantInit(&varName);
    HRESULT hr = S_OK;

    // Get default audio endpoint
    hr = g_pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    if (SUCCEEDED(hr))
    {
        // Get device ID
        hr = pDevice->GetId(&ppwszDeviceId);
        if (SUCCEEDED(hr))
        {
            if (ppwszDeviceId) {
                deviceId = ppwszDeviceId;
            }
            // Get device friendly name
            hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
            if (SUCCEEDED(hr))
            {
                hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
                if (SUCCEEDED(hr))
                {
                    // Copy the friendly name
                    deviceName = varName.pwszVal;
                }
            }
        }
    }

    // Cleanup
    PropVariantClear(&varName);
    if (pProps)
        pProps->Release();
    if (pDevice)
        pDevice->Release();

    if (FAILED(hr))
    {
        if (ppwszDeviceId)
        {
            CoTaskMemFree(ppwszDeviceId);
        }
    }

    DebugLog(L"GetCurrentAudioDevice: value='%s', deviceName='%s', hr=0x%08X", deviceId.c_str(), deviceName.c_str(), hr);
    return hr;
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

HRESULT EnumerateAudioDevices(std::vector<AudioDevice>& devices)
{
    DebugLog(L"EnumerateAudioDevices: Enumerating audio devices");
    devices.clear();
    if (!g_pEnumerator)
        return E_FAIL;

    IMMDeviceCollection* pCollection = nullptr;
    // Include all device states, not just active ones
    HRESULT hr = g_pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_NOTPRESENT | DEVICE_STATE_UNPLUGGED, &pCollection);

    if (SUCCEEDED(hr))
    {
        UINT count;
        hr = pCollection->GetCount(&count);
        if (SUCCEEDED(hr))
        {
            for (UINT i = 0; i < count; i++)
            {
                IMMDevice* pDevice = nullptr;
                hr = pCollection->Item(i, &pDevice);
                if (SUCCEEDED(hr))
                {
                    LPWSTR pwszId = nullptr;
                    hr = pDevice->GetId(&pwszId);
                    if (SUCCEEDED(hr))
                    {
                        IPropertyStore* pProps = nullptr;
                        hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
                        if (SUCCEEDED(hr))
                        {
                            PROPVARIANT varName;
                            PropVariantInit(&varName);
                            hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
                            if (SUCCEEDED(hr))
                            {
                                std::wstring deviceName = varName.pwszVal;

                                // Get device state for display purposes
                                DWORD deviceState;
                                if (SUCCEEDED(pDevice->GetState(&deviceState)))
                                {
                                    // Add state indicator for inactive devices
                                    if (deviceState != DEVICE_STATE_ACTIVE)
                                    {
                                        switch (deviceState)
                                        {
                                        case DEVICE_STATE_DISABLED:
                                            deviceName += L" [Disabled]";
                                            break;
                                        case DEVICE_STATE_NOTPRESENT:
                                            deviceName += L" [Not Present]";
                                            break;
                                        case DEVICE_STATE_UNPLUGGED:
                                            deviceName += L" [Unplugged]";
                                            break;
                                        }
                                    }
                                }

                                devices.push_back({ pwszId, deviceName });
                                DebugLog(L"Audio device added: %s", pwszId);
                                PropVariantClear(&varName);
                            }
                            pProps->Release();
                        }
                        CoTaskMemFree(pwszId);
                    }
                    pDevice->Release();
                }
            }
        }
        pCollection->Release();
    }

    DebugLog(L"EnumerateAudioDevices: Found %d devices (including inactive)", (int)devices.size());
    return hr;
}


HRESULT SetDefaultAudioDevice(LPCWSTR deviceId)
{
    DebugLog(L"SetDefaultAudioDevice: Setting default device to '%s'", deviceId);

    HRESULT hr = g_pPolicyConfig->SetDefaultEndpoint(deviceId, eMultimedia);

    DebugLog(L"SetDefaultAudioDevice: hr=0x%08X", hr);
    return hr;
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

            if (auto pathToTarget = FindPathToDisplay(*g_selectedDisplayTarget, dc)) {

                // save old display configuration
                g_origDisplayConfig = GetCurrentDisplayConfiguration();

                // use the selected display device as a topology with exactly one active display
                auto res = SetDisplayConfig(
                    1, &*pathToTarget,  // only paths
                    static_cast<UINT32>(dc.modes.size()), dc.modes.data(),
                    //SDC_APPLY | SDC_TOPOLOGY_SUPPLIED | SDC_VIRTUAL_REFRESH_RATE_AWARE
                    SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_VIRTUAL_REFRESH_RATE_AWARE | SDC_ALLOW_CHANGES
                );

                if (res != ERROR_SUCCESS)
                {
                    PANIC(L"EnterBigPictureMode: SetDisplayConfig failed with error 0x%08X", res);
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
        
        g_bSteamBigPictureModeRunning = TRUE;
    }
}

void ExitBigPictureMode()
{
    if (g_bSteamBigPictureModeRunning)
    {
        DebugLog(L"Exiting Steam Big Picture Mode");

        if (g_origDisplayConfig)
        {

			// restore original display configuration exactly as it was before entering Big Picture Mode

            auto res = SetDisplayConfig(
                static_cast<UINT32>(g_origDisplayConfig->paths.size()), g_origDisplayConfig->paths.data(),
                static_cast<UINT32>(g_origDisplayConfig->modes.size()), g_origDisplayConfig->modes.data(),
                SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_VIRTUAL_MODE_AWARE | SDC_VIRTUAL_REFRESH_RATE_AWARE | SDC_ALLOW_CHANGES
            );

            if (res != ERROR_SUCCESS)
            {
                PANIC(L"ExitBigPictureMode: SetDisplayConfig failed with error 0x%08X", res);
            }
            else
            {
                DebugLog(L"ExitBigPictureMode: Successfully switched to selected display");
            }
        }
        
        // Restore original audio device
        if (!g_origAudioDeviceId.empty())
        {
            SetDefaultAudioDevice(g_origAudioDeviceId.c_str());
            DebugLog(L"ExitBigPictureMode: Restored audio to '%s'", g_origAudioDeviceId.c_str());
            g_origAudioDeviceId.clear();
        }
        
        // Restore original display configuration (all previously active displays)

        g_bSteamBigPictureModeRunning = FALSE;
        g_origDisplayConfig.reset();
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

void SetSelectedAudioDevice(LPCWSTR deviceId)
{
    DebugLog(L"SetSelectedAudioDevice: value='%s'", deviceId ? deviceId : L"(null)");
    if (deviceId)
    {
        if (deviceId == g_selectedAudioDeviceId)
        {
            //unset the device if it's already selected
            g_selectedAudioDeviceId.reset();
        }
        else {
            g_selectedAudioDeviceId = deviceId;
        }
    }
}

void SetSelectedDisplayDevice(const DisplayTargetEx& target)
{

    /*AdapterPair selectedTarget = AdapterPair(device.target.targetInfo);

	DebugLog(L"SetSelectedDisplayDevice: value='%s' (%ld.%u,%u)", device.monitorFriendlyName.c_str(), selectedTarget.adapterId.HighPart, selectedTarget.adapterId.LowPart, selectedTarget.id);*/

    if (g_selectedDisplayTarget && target == *g_selectedDisplayTarget)
    {
		DebugLog(L"SetSelectedDisplayDevice: Display device already selected, unsetting");
        g_selectedDisplayTarget.reset();
    }
    else {
		DebugLog(L"SetSelectedDisplayDevice: Setting selected display device to '%s'", target.monitorFriendlyName.c_str());

        g_selectedDisplayTarget = target;
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

        // TODO: Verify that the display target is still valid
    }


    dwSize = sizeof(value);
    result = RegQueryValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_MONITOR_PATH, NULL, &dwType, (BYTE*)value, &dwSize);
    if (result == ERROR_SUCCESS && dwType == REG_SZ)
    {
        monitorDevicePath = value;
        DebugLog(L"LoadSettingsFromRegistry: Loaded monitor path '%s'", value);

        // TODO: Verify that the display target is still valid
    }

    dwSize = sizeof(value);
    result = RegQueryValueExW(hKey, SELECTED_DISPLAY_DEVICE_TARGET_ADAPTER_PATH, NULL, &dwType, (BYTE*)value, &dwSize);
    if (result == ERROR_SUCCESS && dwType == REG_SZ)
    {
        adapterDevicePath = value;
        DebugLog(L"LoadSettingsFromRegistry: Loaded adapter path '%s'", value);

        // TODO: Verify that the display target is still valid
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
    result = RegQueryValueExW(hKey, SELECTED_DEVICE_VALUE_NAME, NULL, &dwType, (BYTE*)value, &dwSize);

    if (result == ERROR_SUCCESS)
    {
        g_selectedAudioDeviceId = value;
        DebugLog(L"LoadSettingsFromRegistry: result=%d, g_selectedAudioDeviceId='%s'", (result == ERROR_SUCCESS), g_selectedAudioDeviceId->c_str());
    }


	g_bStartupEnabled = IsStartupEnabled();


    RegCloseKey(hKey);

    return FALSE;
}

LPCWSTR GetSelectedAudioDevice()
{
    return g_selectedAudioDeviceId.has_value() ? g_selectedAudioDeviceId->c_str() : nullptr;
}

DisplayConfiguration QueryDisplayConfiguration(const UINT32 flags, DISPLAYCONFIG_TOPOLOGY_ID* currentTopology)
{
    LONG result = ERROR_SUCCESS;

	DisplayConfiguration displayConfig;

    do
    {
        // Determine how many target and mode structures to allocate
        UINT32 pathCount, modeCount;
        result = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);

        if (result != ERROR_SUCCESS)
        {
			PANIC(L"QueryActiveDisplayConfiguration: GetDisplayConfigBufferSizes failed with error 0x%08X", result);
        }

        // Allocate the target and mode arrays
        displayConfig.paths.resize(pathCount);
        displayConfig.modes.resize(modeCount);

        // Get all active paths and their modes
        result = QueryDisplayConfig(flags, &pathCount, displayConfig.paths.data(), &modeCount, displayConfig.modes.data(), currentTopology);

        // The function may have returned fewer paths/modes than estimated
        displayConfig.paths.resize(pathCount);
        displayConfig.modes.resize(modeCount);

        // It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
        // that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
    } while (result == ERROR_INSUFFICIENT_BUFFER);

    if (result != ERROR_SUCCESS)
    {
		PANIC(L"QueryActiveDisplayConfiguration: QueryDisplayConfig failed with error 0x%08X", result);
    }

	return displayConfig;
}

DisplayConfiguration GetCurrentDisplayConfiguration()
{
    DebugLog(L"GetCurrentDisplayConfiguration: Getting current display configuration");

    // Here, we want to ensure we capture the entire current display configuration, including virtual modes and refresh rates.
    //DISPLAYCONFIG_TOPOLOGY_ID currentTopology;

    auto ret = QueryDisplayConfiguration(QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE | QDC_VIRTUAL_REFRESH_RATE_AWARE, nullptr);

    return ret;

}

std::optional<DisplayTargetEx> GetDisplayTargetExForPath(const DISPLAYCONFIG_PATH_INFO& path)
{


    // Get the target display name
    DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
    targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    targetName.header.size = sizeof(targetName);
    targetName.header.adapterId = path.targetInfo.adapterId;
    targetName.header.id = path.targetInfo.id;
    LONG res = DisplayConfigGetDeviceInfo(&targetName.header);
    if (res == ERROR_SUCCESS)
    {

        std::wstring const monitorFriendlyName = targetName.monitorFriendlyDeviceName;
		std::wstring const monitorDevicePath = targetName.monitorDevicePath;
		

        // now get the adapter name too
        DISPLAYCONFIG_ADAPTER_NAME adapterName = {};
        adapterName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
        adapterName.header.size = sizeof(adapterName);
        adapterName.header.adapterId = path.targetInfo.adapterId;
        res = DisplayConfigGetDeviceInfo(&adapterName.header);
        if (res != ERROR_SUCCESS)
        {
            DebugLog(L"GetDisplayTargetExForPath: Could not get adapter name for display: %d", res);
            return std::nullopt;
        }

        DebugLog(L"GetDisplayTargetExForPath: Adapter name: %s", adapterName.adapterDevicePath);

        // e.g. "\\?\PCI#VEN_10DE&DEV_2684&SUBSYS_467519DA&REV_A1#4&...&0008#{...}"
        std::wstring id(adapterName.adapterDevicePath);
        std::wstring const adapterDevicePath = adapterName.adapterDevicePath;

        // 1) strip the leading '\\?\'
        if (id.find(L"\\\\?\\", 0) == 0)
        {
            id.erase(0, 4);
        }

        // 2) turn '#' into '\' so it becomes a proper instance-id
        std::replace(id.begin(), id.end(), L'#', L'\\');

        // 3) remove the trailing "\{…}" class‐GUID
        if (auto pos = id.find(L"\\{"); pos != std::wstring::npos)
            id.resize(pos);

        std::wstring const adapterFriendlyName = GetFriendlyNameFromInstanceId(id);
        DebugLog(L"GetDisplayTargetExForPath: Adapter friendly name: %s", adapterFriendlyName.c_str());

        DebugLog(L"GetDisplayTargetExForPath: Friendly name: %s", monitorFriendlyName.c_str());

        return DisplayTargetEx(AdapterPair(path.targetInfo), monitorFriendlyName, adapterFriendlyName, monitorDevicePath, adapterDevicePath);
    }
    else {
        DebugLog(L"GetDisplayTargetExForPath: Could not get friendly name of display: %d", res);
		return std::nullopt; // If we can't get the friendly name, return nullopt
    }

}

std::optional<DISPLAYCONFIG_PATH_INFO> FindPathToDisplay(const DisplayTargetEx& target, DisplayConfiguration& dc)
{
    // Find the path that matches the target display.

	// Find the path that best matches the target display.

	std::vector<DISPLAYCONFIG_PATH_INFO> candidatePaths;

    for (const auto& path : dc.paths)
    {

        auto pathTargetEx = GetDisplayTargetExForPath(path);

        if (pathTargetEx && *pathTargetEx == target)
        {
            DebugLog(L"FindPathToDisplay: Potential path found");
			// Now test if this could actually be activated.


            DISPLAYCONFIG_PATH_INFO testPath = path;
            testPath.flags |= DISPLAYCONFIG_PATH_ACTIVE; // Set the active flag to test if it can be activated

            // Use SDC_VALIDATE to check if this target can be activated
            LONG res = SetDisplayConfig(1, &testPath, static_cast<UINT32>(dc.modes.size()), dc.modes.data(), SDC_VALIDATE | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_VIRTUAL_REFRESH_RATE_AWARE | SDC_ALLOW_CHANGES);

            if (res == ERROR_SUCCESS)
            {
                DebugLog(L"FindPathToDisplay: Path (%ld.%u,%u) to (%ld.%u,%u) is activatable, using it", path.sourceInfo.adapterId.HighPart, path.sourceInfo.adapterId.LowPart, path.sourceInfo.id, path.targetInfo.adapterId.HighPart, path.targetInfo.adapterId.LowPart, path.targetInfo.id);
                return testPath;
            }

		}
    }
    
    return std::nullopt;
}


std::vector<std::pair<DisplayTargetEx,DisplayTargetExStatus>> EnumerateConnectedDisplayTargets()
{
    DebugLog(L"EnumerateConnectedDisplayTargets: Getting all connected displays");
    // Query all display paths and modes (virtual mode NOT supported, virtual refresh rate IS)
    auto dc = QueryDisplayConfiguration(QDC_ALL_PATHS | QDC_VIRTUAL_REFRESH_RATE_AWARE, nullptr);

    // Now, use SDC_VALIDATE | SDC_TOPOLOGY_SUPPLIED with setting the paths to active to filter all of the display paths for ones that could be activated.

    std::unordered_set<AdapterPair> connectedDisplays;
	std::vector<std::pair<DisplayTargetEx, DisplayTargetExStatus>> connectedTargets;

    for (auto& path : dc.paths)
    {
        if (connectedDisplays.contains(AdapterPair{ path.targetInfo }))
        {
            DebugLog(L"EnumerateConnectedDisplayTargets: Skipping already processed target: (%ld.%u,%u)", path.targetInfo.adapterId.HighPart, path.targetInfo.adapterId.LowPart, path.targetInfo.id);
            continue; // Skip if we've already processed this source
        }

        bool primary = false;
        bool active = path.flags & DISPLAYCONFIG_PATH_ACTIVE;

        // Check if the target is active
        if (active)
        {
            DebugLog(L"EnumerateConnectedDisplayTargets: Found active display target: (%ld.%u,%u) to (%ld.%u,%u)", path.sourceInfo.adapterId.HighPart, path.sourceInfo.adapterId.LowPart, path.sourceInfo.id, path.targetInfo.adapterId.HighPart, path.targetInfo.adapterId.LowPart, path.targetInfo.id);
            // This target is active, so we can consider it connected

            auto position = dc.modes[path.sourceInfo.modeInfoIdx].sourceMode.position;
            primary = position.x == 0 && position.y == 0;
        }
        else {
            // Test if it COULD be active
            DISPLAYCONFIG_PATH_INFO testPath = path;
            testPath.flags |= DISPLAYCONFIG_PATH_ACTIVE; // Set the active flag to test if it can be activated

            // Use SDC_VALIDATE to check if this target can be activated
            LONG res = SetDisplayConfig(1, &testPath, 0, nullptr, SDC_VALIDATE | SDC_TOPOLOGY_SUPPLIED | SDC_VIRTUAL_REFRESH_RATE_AWARE);

            if (res == ERROR_SUCCESS)
            {
                DebugLog(L"EnumerateConnectedDisplayTargets: Path (%ld.%u,%u) to (%ld.%u,%u) inactive, but could be activated", path.sourceInfo.adapterId.HighPart, path.sourceInfo.adapterId.LowPart, path.sourceInfo.id, path.targetInfo.adapterId.HighPart, path.targetInfo.adapterId.LowPart, path.targetInfo.id);
            }
            else {
				continue; // If it can't be activated, skip this path
            }
        }

		DisplayTargetExStatus status = DisplayTargetExStatus::Inactive;
        if (primary)
        {
            status = DisplayTargetExStatus::Primary;
        }
        else if (active)
        {
            status = DisplayTargetExStatus::Active;
        }
        


		std::optional<DisplayTargetEx> displayTarget = GetDisplayTargetExForPath( path );

        if (displayTarget)
        {
			connectedDisplays.insert(displayTarget->id); // Add to the set of connected display paths
            connectedTargets.push_back(std::make_pair(*displayTarget, status));
        }

    }


    for (const auto& target : connectedTargets)
    {
        DebugLog(L"EnumerateConnectedDisplayTargets: Connected display: (%ld.%u,%u) [%s]", target.first.id.adapterId.HighPart, target.first.id.adapterId.LowPart, target.first.id.id, target.first.monitorFriendlyName.c_str());
    }

    return connectedTargets;
}


std::vector<std::wstring> GetDisplayNamesFromPaths(const std::vector<DISPLAYCONFIG_PATH_INFO>& paths)
{

    std::vector<std::wstring> displayNames;

    for (const auto& path : paths)
    {

        // Get the target display name
        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(targetName);
        targetName.header.adapterId = path.targetInfo.adapterId;
        targetName.header.id = path.targetInfo.id;
        LONG res = DisplayConfigGetDeviceInfo(&targetName.header);
        if (res == ERROR_SUCCESS)
        {
            displayNames.push_back(targetName.monitorFriendlyDeviceName);

            // Check the status of the display too
            if (path.targetInfo.statusFlags & DISPLAYCONFIG_PATH_ACTIVE)
            {
                displayNames.back() += L" [Active]";
            }

            DebugLog(L"GetConnectedDisplayNames: Found display: %s", displayNames.back().c_str());
        }
        else {
            DebugLog(L"GetConnectedDisplayNames: Could not enumerate display: %d", res);
        }
        
    }
    return displayNames;
}