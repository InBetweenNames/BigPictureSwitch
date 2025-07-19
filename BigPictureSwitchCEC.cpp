#include "framework.h"

#include <vector>
#include <optional>
#include <memory>

#include "BigPictureSwitch.h"

// Workaround to link libcec statically
#define DECLSPEC
#include "cec.h"
#undef DECLSPEC

//libCEC
auto g_parser_deleter = [](CEC::ICECAdapter* adapter) {
    if (adapter) {
        CECDestroy(adapter);
    }
    };
std::optional<CEC::ICECCallbacks> g_callbacks; // Optional callbacks for CEC events
std::optional<CEC::libcec_configuration> g_config;
std::unique_ptr<CEC::ICECAdapter, decltype(g_parser_deleter)> g_parser; // Pointer to the CEC parser instance


void CecLogMessage(void* cbParam, const CEC::cec_log_message* message)
{

    const WCHAR* strLevel = L"UNKNOWN: ";
    switch (message->level)
    {
    case CEC::CEC_LOG_ERROR:
        strLevel = L"ERROR:   ";
        break;
    case CEC::CEC_LOG_WARNING:
        strLevel = L"WARNING: ";
        break;
    case CEC::CEC_LOG_NOTICE:
        strLevel = L"NOTICE:  ";
        break;
    case CEC::CEC_LOG_TRAFFIC:
        strLevel = L"TRAFFIC: ";
        break;
    case CEC::CEC_LOG_DEBUG:
        strLevel = L"DEBUG:   ";
        break;
    default:
        break;
    }

    DebugLog(L"%s[%16lld]\t%S", strLevel, message->time, message->message);

}

std::vector<int> EnumerateCECDevices()
{

	std::vector<int> ret;

    g_config = CEC::libcec_configuration{};
    //   g_callbacks = CEC::ICECCallbacks{};
    //   g_callbacks->logMessage = &CecLogMessage;
    //   strncpy(g_config->strDeviceName, "Steam", sizeof(g_config->strDeviceName));
    //   g_config->callbacks = &g_callbacks.value();
       //g_config->deviceTypes.Add(CEC::CEC_DEVICE_TYPE_PLAYBACK_DEVICE);
    //   g_config->iHDMIPort = 3;
    //   g_config->bActivateSource = 1;
    //   g_config->bPowerOffOnStandby = 1;
    //   g_config->powerOffDevices.Set(CEC::CECDEVICE_TV);
    DebugLog(L"Initializing libCEC");
    g_parser = std::unique_ptr<CEC::ICECAdapter, decltype(g_parser_deleter)>(CECInitialise(&g_config.value()), g_parser_deleter);

    CEC::cec_adapter_descriptor devices[16];
    auto iDevicesFound = g_parser->DetectAdapters(devices, 16, nullptr, false);
    for (int8_t iDevicePtr = 0; iDevicePtr < iDevicesFound; iDevicePtr++)
    {
        DebugLog(L"device:              %d", iDevicePtr + 1);
        DebugLog(L"com port:            %S", devices[iDevicePtr].strComName);
        DebugLog(L"vendor id:           %04x", devices[iDevicePtr].iVendorId);
        DebugLog(L"product id:          %04x", devices[iDevicePtr].iProductId);
        DebugLog(L"firmware version:    %d", devices[iDevicePtr].iFirmwareVersion);

        if (devices[iDevicePtr].iFirmwareBuildDate != CEC_FW_BUILD_UNKNOWN)
        {
            time_t buildTime = (time_t)devices[iDevicePtr].iFirmwareBuildDate;
            std::string strDeviceInfo;
            DebugLog(L"firmware build date: %S", asctime(gmtime(&buildTime)));
        }

        if (devices[iDevicePtr].adapterType != CEC::ADAPTERTYPE_UNKNOWN)
        {
            DebugLog(L"type:                %S", g_parser->ToString(devices[iDevicePtr].adapterType));
        }

    }
    //g_parser->SetHDMIPort(CEC::CECDEVICE_TV, 2);
    // g_parser->Open("COM4");
    //g_parser->SetActiveSource();
    //g_parser->SetInactiveView();
    //g_parser->StandbyDevices(CEC::CECDEVICE_TV);

    return ret;

}