#include "BigPictureSwitch.h"
#include "BigPictureSwitchDisplay.h"

#include <Ntddvdeo.h>
#include <ioapiset.h>

#include <unordered_set>
#include <algorithm>

std::wstring GetFriendlyNameFromInstanceId(const std::wstring& instanceId)
{
    // 1. Get a device info set for the DISPLAY class
    HDEVINFO devs = SetupDiGetClassDevsW(
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

        DEVPROPTYPE propType;
        DWORD size = sizeof(bufId);

        HRESULT hr = SetupDiGetDevicePropertyW(
            devs,                      
            &devInfo,                  
            &DEVPKEY_Device_InstanceId,
            &propType,
            reinterpret_cast<PBYTE>(bufId),
            size,
            &size,
            0                         
        );

        if (SUCCEEDED(hr) && propType == DEVPROP_TYPE_STRING &&
            _wcsicmp(bufId, instanceId.c_str()) == 0)
        {
            // 4a. Try the new FriendlyName property
            DWORD cbSize = 0;
            BOOL status = SetupDiGetDevicePropertyW(
                devs,
                &devInfo,
                &DEVPKEY_Device_FriendlyName,
                &propType,
                (BYTE*)bufId,
                sizeof(bufId),
                &cbSize,
                0
            );

            if (status)
            {
                friendly = bufId;
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
                    (BYTE*)bufId,
                    sizeof(bufId),
                    &cbSize,
                    0
                );
                if (status)
                {
                    friendly = bufId;
                }
            }

            break;
        }
    }

    SetupDiDestroyDeviceInfoList(devs);
    return friendly;
}


LONG SetDisplayConfigWrapper(_In_ UINT32 numPathArrayElements,
    _In_reads_opt_(numPathArrayElements) DISPLAYCONFIG_PATH_INFO* pathArray,
    _In_ UINT32 numModeInfoArrayElements,
    _In_reads_opt_(numModeInfoArrayElements) DISPLAYCONFIG_MODE_INFO* modeInfoArray,
    _In_ UINT32 flags)
{
    LONG res = ERROR_GEN_FAILURE;
    int attempts = 0;

    do {

        res = SetDisplayConfig(
            numPathArrayElements, pathArray,
            numModeInfoArrayElements, modeInfoArray,
            flags
        );

        if (res != ERROR_SUCCESS)
        {
            DebugLog(L"SetDesktopDisplayConfiguration: SetDisplayConfig failed with error 0x%08X attempt %d", res, attempts);
            Sleep(1000);

        }

        attempts++;
    } while (res != ERROR_SUCCESS && attempts < 5);

    return res;
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
    DISPLAYCONFIG_TOPOLOGY_ID currentTopology;

    // NOTE: we use QDC_DISPLAY_CURRENT because Steam itself can mess around with the primary desktop.
    // If we used QDC_ACTIVE_PATHS, and Steam switched the primary desktop to the other display, then we would very likely
    // capture that configuration here (depending if this code runs first or if Steam's own SetDisplayConfig runs first).
    // So to mitigate this, we just get the most current working configuration from the database itself.  Since Steam does not
    // save its display configuration to the database, we're safe here.

    auto ret = QueryDisplayConfiguration(QDC_DATABASE_CURRENT | QDC_VIRTUAL_MODE_AWARE | QDC_VIRTUAL_REFRESH_RATE_AWARE, &currentTopology);

    DebugLog(L"GetCurrentDisplayConfiguration: currentTopology = %d", currentTopology);

    return ret;

}

// NOTE: there is a better way of doing this where we cache this information and just look it up later
// and there is a better way of enumerating display targets too using the SetupDiEnumDeviceInfo with MONITOR instances
// but this is good enough for now.

// NOTE: we could autodetect HDMI port for CEC by parsing the EDID ourselves, but maybe later.  It's better to make the user specify it for now
// as it supports more configurations.

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


std::vector<std::pair<DisplayTargetEx, DisplayTargetExStatus>> EnumerateConnectedDisplayTargets()
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



        std::optional<DisplayTargetEx> displayTarget = GetDisplayTargetExForPath(path);

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