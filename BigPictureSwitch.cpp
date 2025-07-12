// BigPictureAudioSwitch2.cpp : Defines the entry point for the application.
//

 /* Exclude redundant APIs such as Cryptography, DDE, RPC, Shell, and Windows Sockets. */
#define WIN32_LEAN_AND_MEAN

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

#include <unordered_map>
#include <unordered_set>

#include <winuser.h>


#include <optional>

#define MAX_LOADSTRING 100
#define WM_TRAYICON (WM_USER + 1)

// Registry path for Windows startup
#define STARTUP_REGISTRY_PATH L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"
#define APP_REGISTRY_NAME L"BigPictureAudioSwitch2"

// Registry path for application settings
#define SETTINGS_REGISTRY_PATH L"SOFTWARE\\BigPictureAudioSwitch2"
#define SELECTED_DEVICE_VALUE_NAME L"SelectedAudioDevice"
#define SWITCH_DISPLAY_VALUE_NAME L"SwitchDisplay"

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

struct DisplayPathEx
{
    DISPLAYCONFIG_PATH_INFO path;
    std::wstring monitorFriendlyName;

    DisplayPathEx(DISPLAYCONFIG_PATH_INFO& path) : path(path)
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
            this->monitorFriendlyName = targetName.monitorFriendlyDeviceName;

            // Check the status of the display too
            if (path.targetInfo.statusFlags & DISPLAYCONFIG_PATH_ACTIVE)
            {
                this->monitorFriendlyName += L" [Active]";
            }

            DebugLog(L"DisplayPathEx: Friendly name: %s", this->monitorFriendlyName.c_str());
        }
        else {
            DebugLog(L"DisplayPathEx: Could not get friendly name of display: %d", res);
            this->monitorFriendlyName = L"Unknown display";
        }
    }
};

bool operator==(const DisplayPathEx& lhs, const DisplayPathEx& rhs)
{
	// Ignore the monitorFriendlyName for comparison
    static_assert(std::has_unique_object_representations_v<decltype(rhs.path)>, "DISPLAYCONFIG_PATH_INFO has padding bytes");
    static_assert(std::is_standard_layout_v<decltype(rhs.path)>, "DISPLAYCONFIG_PATH_INFO is not standard layout");

    return memcmp(&lhs.path, &rhs.path, sizeof(rhs.path)) == 0;

  
}


struct AdapterPair
{
    LUID adapterId;
    UINT32 id;

    AdapterPair(DISPLAYCONFIG_PATH_TARGET_INFO& target) : adapterId(target.adapterId), id(target.id)
    {
        DebugLog(L"AdapterPair created: AdapterId=%08X%08X, Id=%u", adapterId.HighPart, adapterId.LowPart, id);
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

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
NOTIFYICONDATA nid = {};                        // System tray icon data
HWND g_hWnd = nullptr;                          // Hidden window handle

// Audio-related globals
IMMDeviceEnumerator* g_pEnumerator = nullptr;
bool g_bSwitchDisplay = false; // Flag to indicate if display switching is enabled
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
std::vector<DisplayPathEx> g_displayDevices;
std::optional<DisplayPathEx> g_selectedDisplayDevice;

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

std::vector<DisplayPathEx> EnumerateConnectedDisplayConfigurations();
std::vector<std::wstring> GetDisplayNamesFromPaths(const std::vector<DISPLAYCONFIG_PATH_INFO>& paths);
DisplayConfiguration GetCurrentDisplayConfiguration();
void                SetSelectedDisplayDevice(const DisplayPathEx& path);

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
        int wmId = LOWORD(wParam);

        if (wmId >= IDM_AUDIODEVICE_BASE && wmId < IDM_AUDIODEVICE_MAX)
        {
            int deviceIndex = wmId - IDM_AUDIODEVICE_BASE;
            if (deviceIndex >= 0 && deviceIndex < g_audioDevices.size())
            {
                SetSelectedAudioDevice(g_audioDevices[deviceIndex].id.c_str());
                SaveSettingsToRegistry();
            }
            return 0;
        }
        if (wmId >= IDM_DISPLAYDEVICE_BASE && wmId < IDM_DISPLAYDEVICE_MAX)
        {
            int deviceIndex = wmId - IDM_DISPLAYDEVICE_BASE;
            if (deviceIndex >= 0 && deviceIndex < g_displayDevices.size())
            {
                SetSelectedDisplayDevice(g_displayDevices[deviceIndex]);
                SaveSettingsToRegistry();
            }
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
        case IDM_SWITCH_DISPLAY:
			g_bSwitchDisplay = !g_bSwitchDisplay;
            SaveSettingsToRegistry();
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

		g_displayDevices = EnumerateConnectedDisplayConfigurations();
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"Switch to Display Device:");
        for (size_t i = 0; i < g_displayDevices.size(); ++i)
        {
            // Add each display name to the menu
            UINT flags = MF_STRING;
            if (g_displayDevices[i] == g_selectedDisplayDevice)
            {
                flags |= MF_CHECKED; // Checkmark for the selected display
			}
            AppendMenuW(hMenu, flags, IDM_DISPLAYDEVICE_BASE + i, g_displayDevices[i].monitorFriendlyName.c_str());
		}

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        // Add startup option with checkmark if enabled
        UINT startupFlags = MF_STRING;
        if (IsStartupEnabled())
        {
            startupFlags |= MF_CHECKED;
        }
        AppendMenuW(hMenu, startupFlags, IDM_STARTUP, L"Run on Startup");

		AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        // Add an option to enable switching to the external display

		UINT switchDisplayFlags = MF_STRING;
        if (g_bSwitchDisplay)
        {
            switchDisplayFlags |= MF_CHECKED;
		}
		AppendMenuW(hMenu, switchDisplayFlags, IDM_SWITCH_DISPLAY, L"Switch to External Display");

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

        if (g_selectedDisplayDevice)
        {
            // save old display configuration
            g_origDisplayConfig = GetCurrentDisplayConfiguration();

			// use the selected display device as a topology with exactly one active display
            // skip any modesetting.

			DISPLAYCONFIG_PATH_INFO path = g_selectedDisplayDevice->path;
			path.flags |= DISPLAYCONFIG_PATH_ACTIVE;

            // FIXME: should I also make the current display config inactive?

            auto res = SetDisplayConfig(
                1, &path,  // only paths
                0, nullptr,  // No target modes
                SDC_APPLY | SDC_TOPOLOGY_SUPPLIED
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

        if (g_selectedDisplayDevice)
        {

			// restore original display configuration exactly as it was before entering Big Picture Mode

            // TODO: add an option to disable the display?

            auto res = SetDisplayConfig(
                g_origDisplayConfig->paths.size(), g_origDisplayConfig->paths.data(),  
                g_origDisplayConfig->modes.size(), g_origDisplayConfig->modes.data(),  
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

void SetSelectedDisplayDevice(const DisplayPathEx& path)
{

    /*AdapterPair selectedTarget = AdapterPair(device.path.targetInfo);

	DebugLog(L"SetSelectedDisplayDevice: value='%s' (%ld.%u,%u)", device.monitorFriendlyName.c_str(), selectedTarget.adapterId.HighPart, selectedTarget.adapterId.LowPart, selectedTarget.id);*/

    if (g_selectedDisplayDevice && path == *g_selectedDisplayDevice)
    {
		DebugLog(L"SetSelectedDisplayDevice: Display device already selected, unsetting");
        g_selectedDisplayDevice.reset();
    }
    else {
		DebugLog(L"SetSelectedDisplayDevice: Setting selected display device to '%s'", path.monitorFriendlyName.c_str());

        g_selectedDisplayDevice = path;
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

        size_t dwSize = g_selectedAudioDeviceId->size() * sizeof(WCHAR);
        result = RegSetValueExW(hKey, SELECTED_DEVICE_VALUE_NAME, 0, REG_SZ, (const BYTE*)g_selectedAudioDeviceId->c_str(), (DWORD)dwSize);

        DebugLog(L"SaveSettingsToRegistry: SELECTED_DEVICE result=%d, g_selectedAudioDeviceId='%s'", (result == ERROR_SUCCESS), g_selectedAudioDeviceId->c_str());

    }
    else {
        result = RegDeleteValueW(hKey, SELECTED_DEVICE_VALUE_NAME);
        DebugLog(L"SaveSettingsToRegistry: RegDeleteValueW result=%d", (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND));
    }

	DWORD switchDisplay = g_bSwitchDisplay ? 1 : 0;

	result = RegSetValueExW(hKey, SWITCH_DISPLAY_VALUE_NAME, 0, REG_DWORD, (BYTE*)(&switchDisplay), sizeof(DWORD));

	DebugLog(L"SaveSettingsToRegistry: SWITCH_DISPLAY result=%d, g_bSwitchDisplay=%d", (result == ERROR_SUCCESS), g_bSwitchDisplay);


    if (g_selectedDisplayDevice)
    {
		// Save the selected display device information
        // Only to save the source adapterId,id and target adapterId,id -- NOT the full path info.
    }

 //   if (g_selectedDisplayDevice)
 //   {
 //       // Save the selected display device information
 //       // FIXME
 //       size_t dwSize = g_selectedDisplayDevice->monitorFriendlyName.size() * sizeof(WCHAR);
 //       result = RegSetValueExW(hKey, SELECTED_DISPLAY_DEVICE_NAME, 0, REG_SZ, (const BYTE*)g_selectedDisplayDevice->monitorFriendlyName.c_str(), (DWORD)dwSize);
 //       DebugLog(L"SaveSettingsToRegistry: SELECTED_DISPLAY_DEVICE_NAME result=%d, g_selectedDisplayDevice='%s'", (result == ERROR_SUCCESS), g_selectedDisplayDevice->monitorFriendlyName.c_str());
 //   }
 //   else {
 //       result = RegDeleteValueW(hKey, SELECTED_DISPLAY_DEVICE_NAME);
 //       DebugLog(L"SaveSettingsToRegistry: RegDeleteValueW for SELECTED_DISPLAY_DEVICE_NAME result=%d", (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND));
	//}

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

    dwType = REG_DWORD;
    dwSize = sizeof(DWORD);

	DWORD switchDisplay = 0; // Default to false

	result = RegQueryValueExW(hKey, SWITCH_DISPLAY_VALUE_NAME, NULL, &dwType, (BYTE*)&switchDisplay, &dwSize);
    if (result == ERROR_SUCCESS)
    {
		g_bSwitchDisplay = (switchDisplay != 0);
		DebugLog(L"LoadSettingsFromRegistry: result=%d, g_bSwitchDisplay=%d", result, g_bSwitchDisplay);
    }
    else {
		g_bSwitchDisplay = false; // Default to false if not set
		DebugLog(L"LoadSettingsFromRegistry: result=%d, g_bSwitchDisplay not found, defaulting to FALSE", result);
    }

    // TODO: move autoruns logic here

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
        // Determine how many path and mode structures to allocate
        UINT32 pathCount, modeCount;
        result = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);

        if (result != ERROR_SUCCESS)
        {
			PANIC(L"QueryActiveDisplayConfiguration: GetDisplayConfigBufferSizes failed with error 0x%08X", result);
        }

        // Allocate the path and mode arrays
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

    auto ret = QueryDisplayConfiguration(QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE | QDC_VIRTUAL_REFRESH_RATE_AWARE, nullptr);

    return ret;

}

std::vector<DisplayPathEx> EnumerateConnectedDisplayConfigurations()
{
    DebugLog(L"EnumerateConnectedDisplayConfigurations: Getting all connected displays");
    // Query all display paths and modes
    auto dc = QueryDisplayConfiguration(QDC_ALL_PATHS | QDC_VIRTUAL_REFRESH_RATE_AWARE, nullptr);

	// Now, use SDC_VALIDATE | SDC_TOPOLOGY_SUPPLIED with setting the paths to active to filter all of the display paths for ones that could be activated.

	std::vector<DisplayPathEx> connectedPaths;
	std::unordered_set<AdapterPair> connectedDisplays;

    for (auto& path : dc.paths)
    {
        if (connectedDisplays.contains(AdapterPair{ path.targetInfo }))
        {
            DebugLog(L"EnumerateConnectedDisplayConfigurations: Skipping already processed target: (%ld.%u,%u)", path.targetInfo.adapterId.HighPart, path.targetInfo.adapterId.LowPart, path.targetInfo.id);
            continue; // Skip if we've already processed this source
		}

        // Check if the path is active
        if (path.flags & DISPLAYCONFIG_PATH_ACTIVE)
        {
            DebugLog(L"EnumerateConnectedDisplayConfigurations: Found active display path: (%ld.%u,%u) to (%ld.%u,%u)", path.sourceInfo.adapterId.HighPart, path.sourceInfo.adapterId.LowPart, path.sourceInfo.id, path.targetInfo.adapterId.HighPart, path.targetInfo.adapterId.LowPart, path.targetInfo.id);
			// This path is active, so we can consider it connected
            connectedPaths.emplace_back(path);
			connectedDisplays.insert(AdapterPair(path.targetInfo)); // Add to the set of connected display paths
            continue;
        }


        // Test if it could be activated.

		DISPLAYCONFIG_PATH_INFO testPath = path;
		testPath.flags |= DISPLAYCONFIG_PATH_ACTIVE; // Set the active flag to test if it can be activated

		// Use SDC_VALIDATE to check if this path can be activated
		LONG res = SetDisplayConfig(1, &testPath, 0, nullptr, SDC_VALIDATE | SDC_TOPOLOGY_SUPPLIED | SDC_VIRTUAL_REFRESH_RATE_AWARE);

        if (res == ERROR_SUCCESS)
        {
            DebugLog(L"EnumerateConnectedDisplayConfigurations: Path (%ld.%u,%u) to (%ld.%u,%u) can be activated - adding to path list", path.sourceInfo.adapterId.HighPart, path.sourceInfo.adapterId.LowPart, path.sourceInfo.id, path.targetInfo.adapterId.HighPart, path.targetInfo.adapterId.LowPart, path.targetInfo.id);
            connectedPaths.emplace_back(path); // Add it to the connected paths
			connectedDisplays.insert(AdapterPair(path.targetInfo)); // Add to the set of connected display paths
        }
        else
        {
            DebugLog(L"EnumerateConnectedDisplayConfigurations: Path (%ld.%u,%u) to (%ld.%u,%u) cannot be activated, error 0x%08X", path.sourceInfo.adapterId.HighPart, path.sourceInfo.adapterId.LowPart, path.sourceInfo.id, path.targetInfo.adapterId.HighPart, path.targetInfo.adapterId.LowPart, path.targetInfo.id);
		}
 
    }

    for (const auto& path : connectedPaths)
    {
        DebugLog(L"EnumerateConnectedDisplayConfigurations: Connected display path: (%ld.%u,%u) to (%ld.%u,%u)", path.path.sourceInfo.adapterId.HighPart, path.path.sourceInfo.adapterId.LowPart, path.path.sourceInfo.id, path.path.targetInfo.adapterId.HighPart, path.path.targetInfo.adapterId.LowPart, path.path.targetInfo.id);
	}

    for (const auto& path : connectedDisplays)
    {
        DebugLog(L"EnumerateConnectedDisplayConfigurations: Connected display: (%ld.%u,%u)", path.adapterId.HighPart, path.adapterId.LowPart, path.id);
	}

	return connectedPaths;
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