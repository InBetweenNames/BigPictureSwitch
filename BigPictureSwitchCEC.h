#pragma once
#include "framework.h"

// Workaround to link libcec statically
#define DECLSPEC
#include "cec.h"
#undef DECLSPEC

#include <vector>
#include <memory>
#include <format>
#include <optional>
#include <variant>

    //libCEC
inline auto ICECAdapter_deleter = [](CEC::ICECAdapter* adapter) {
    if (adapter) {
        CECDestroy(adapter);
    }
    };

struct PortInfo
{
	uint16_t physAddr; // HDMI physical address
};

template<>
struct std::formatter<PortInfo, wchar_t> {
    // Parse format specifiers (we ignore any here).
    constexpr auto parse(wformat_parse_context& ctx) {
        return ctx.begin();
    }
    // Format the PortInfo as a string
    template<typename FormatContext>
    auto format(const PortInfo& port, FormatContext& ctx) const {

		// Extract each nibble from the physical address
        auto portLow = port.physAddr & 0xF;
        auto portMid = (port.physAddr >> 4) & 0xF;
		auto portMid2 = (port.physAddr >> 8) & 0xF;
		auto portHigh = (port.physAddr >> 12) & 0xF;

        return std::format_to(ctx.out(), L"HDMI-CEC adapter physical address: {}.{}.{}.{} (HDMI port {})", portHigh, portMid2, portMid, portLow, portHigh);
    }
};

struct BigPictureSwitchCEC {

    BigPictureSwitchCEC(std::optional<uint16_t> hdmiAddress);
	BigPictureSwitchCEC(const BigPictureSwitchCEC&) = delete;
    BigPictureSwitchCEC& operator=(const BigPictureSwitchCEC&) = delete;
    BigPictureSwitchCEC(BigPictureSwitchCEC&&) = delete;
	BigPictureSwitchCEC& operator=(BigPictureSwitchCEC&&) = delete;

	std::vector<CEC::cec_adapter_descriptor> EnumerateCECAdapters();

    // State change; bind this exact interface
    bool OpenAdapter(const CEC::cec_adapter_descriptor& adapter);

    const char* GetTVVendorString();
    std::wstring ToWString();
    PortInfo PortInfo();

    void SetPhysicalAddress(uint16_t physicalAddress);

	void WakeAndSwitch();
	void StandbyAndSwitch();

    void TurnOff();

    ~BigPictureSwitchCEC();


    CEC::ICECCallbacks m_callbacks; // Optional callbacks for CEC events
    CEC::libcec_configuration m_config;
    std::unique_ptr<CEC::ICECAdapter, decltype(ICECAdapter_deleter)> m_parser; // Pointer to the CEC parser instance

    CEC::cec_adapter_descriptor m_adapter; // The currently selected adapter

    uint16_t m_physical_address; // Physical addresses of the adapter on the bus
	bool m_opened = false; // Whether the adapter is currently opened
	std::wstring m_menuString; // Menu string for display
};