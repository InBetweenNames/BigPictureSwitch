#pragma once
#include "framework.h"

#include <format>
#include <vector>
#include <optional>
#include <string>

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

inline bool operator==(const DisplayTargetEx& lhs, const DisplayTargetEx& rhs)
{
    // We don't care about comparing status.  We do care about expected names as this is a sign the configuration has changed.
    // We also don't care about comparing the raw adapter ID as this can change on e.g. driver updates.
    return rhs.monitorDevicePath == lhs.monitorDevicePath
        && lhs.adapterDevicePath == rhs.adapterDevicePath;
}

// Display-related functions
std::vector<std::wstring> GetDisplayNamesFromPaths(const std::vector<DISPLAYCONFIG_PATH_INFO>& paths);
void                SetSelectedDisplayDevice(const DisplayTargetEx& path);
std::optional<DisplayTargetEx> GetDisplayTargetExForPath(const DISPLAYCONFIG_PATH_INFO& path);
std::optional<DISPLAYCONFIG_PATH_INFO> FindPathToDisplay(const DisplayTargetEx& target, DisplayConfiguration& dc);
DisplayConfiguration GetCurrentDisplayConfiguration();
std::vector<std::pair<DisplayTargetEx, DisplayTargetExStatus>> EnumerateConnectedDisplayTargets();
DisplayConfiguration QueryDisplayConfiguration(const UINT32 flags, DISPLAYCONFIG_TOPOLOGY_ID* currentTopology);
void SetDesktopDisplayConfiguration(DisplayConfiguration& dc);
LONG SetDisplayConfigWrapper(_In_ UINT32 numPathArrayElements,
    _In_reads_opt_(numPathArrayElements) DISPLAYCONFIG_PATH_INFO* pathArray,
    _In_ UINT32 numModeInfoArrayElements,
    _In_reads_opt_(numModeInfoArrayElements) DISPLAYCONFIG_MODE_INFO* modeInfoArray,
    _In_ UINT32 flags);



// Display-related globals
inline std::optional<DisplayConfiguration> g_origDisplayConfig;
inline std::vector<std::pair<DisplayTargetEx, DisplayTargetExStatus>> g_displayTargets;
inline std::optional<DisplayTargetEx> g_selectedDisplayTarget;
