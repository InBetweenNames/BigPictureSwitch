#include "framework.h"

#include "BigPictureSwitch.h"
#include "BigPictureSwitchCEC.h"

#include <vector>
#include <optional>
#include <memory>
#include <format>


void CecLogMessage(void* cbParam, const CEC::cec_log_message* message)
{
	// NOTE: cbParam contains the BigPictureSwitchCEC instance

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



BigPictureSwitchCEC::BigPictureSwitchCEC(std::optional<uint16_t> hdmiAddress)
{
    
    m_config = CEC::libcec_configuration{};
    m_callbacks = CEC::ICECCallbacks{};

    m_callbacks.logMessage = &CecLogMessage;

    m_config.callbacks = &m_callbacks;

    // NOTE: autodetection is broken on Windows, so don't rely on it.

    strncpy_s(m_config.strDeviceName, "Steam", sizeof(m_config.strDeviceName));

    m_config.deviceTypes.Add(CEC::CEC_DEVICE_TYPE_PLAYBACK_DEVICE);
    m_config.powerOffDevices.Set(CEC::CECDEVICE_TV);

    if (hdmiAddress)
    {
		m_config.iPhysicalAddress = *hdmiAddress;
    }

    m_config.cecVersion = CEC::CEC_VERSION_1_2;
    m_config.bPowerOffOnStandby = 0;
    m_config.bActivateSource = 0;
    m_config.bAutoPowerOn = 0;
    m_config.bAutoWakeAVR = 0;

    DebugLog(L"Initializing libCEC");

    // Fortunately, CECInitialise seems to be infalliable, but still take care to do this right
    auto tmp = CECInitialise(&m_config);

    if (tmp == nullptr)
    {
        DebugLog(L"Failed to initialize libCEC -- this should never happen");
        throw std::runtime_error("Failed to initialize libCEC");
    }

    m_parser = std::unique_ptr<CEC::ICECAdapter, decltype(ICECAdapter_deleter)>(tmp, ICECAdapter_deleter);

    auto adapters = EnumerateCECAdapters();
    if (adapters.empty())
    {
        DebugLog(L"No CEC adapters found");
        // The safe deleter will take care of uninitializing libCEC
		m_parser->Close();

        throw std::runtime_error("Failed to find adapter");
	}

    m_adapter = adapters[0];

	DebugLog(L"Using CEC adapter: %S", m_adapter.strComName);

	// Open the first adapter found
    if (!OpenAdapter(m_adapter))
    {
        DebugLog(L"Failed to open CEC adapter: %S", m_adapter.strComName);
        throw std::runtime_error("Failed to open adapter");
	}

    // Populate the TV vendor string
    std::string tmpStr = std::format("{} [{}] ({})", GetTVVendorString(), m_parser->ToString(m_adapter.adapterType), m_adapter.strComName).c_str();
    m_menuString = std::wstring(tmpStr.begin(), tmpStr.end());
    

	//// close the adapter -- otherwise we will burn cycles for no reason
	//m_parser->Close();
}

std::vector<CEC::cec_adapter_descriptor> BigPictureSwitchCEC::EnumerateCECAdapters()
{


    std::vector<CEC::cec_adapter_descriptor> devices(16);
    ptrdiff_t iDevicesFound = m_parser->DetectAdapters(devices.data(), 16, nullptr, false);
    for (ptrdiff_t iDevicePtr = 0; iDevicePtr < iDevicesFound; iDevicePtr++)
    {
        DebugLog(L"device:              %d", iDevicePtr + 1);
        DebugLog(L"com port:            %S", devices[iDevicePtr].strComPath);
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
            DebugLog(L"type:                %S", m_parser->ToString(devices[iDevicePtr].adapterType));
        }

    }
	devices.resize(iDevicesFound);

    return devices;

}

// State change; bind this exact interface
bool BigPictureSwitchCEC::OpenAdapter(const CEC::cec_adapter_descriptor& adapter)
{
    if (!m_opened) {
        if (!m_parser->Open(adapter.strComName, 10000)) {
            DebugLog(L"Failed to open CEC adapter: %S", adapter.strComName);
            return false;
        }
        DebugLog(L"Opened CEC adapter: %S", adapter.strComName);

        m_opened = true;
    }

    return true;
}

const char* BigPictureSwitchCEC::GetTVVendorString()
{

	auto vendorId = m_parser->GetDeviceVendorId(CEC::CECDEVICE_TV);
    auto vendorString = m_parser->VendorIdToString(vendorId);

    return vendorString;

}

void BigPictureSwitchCEC::WakeAndSwitch()
{
    if (OpenAdapter(m_adapter)) {
        // Wake the TV
        //m_parser->PowerOnDevices();

        // Switch to our input
        m_parser->SetActiveSource();

        // close the adapter after sending the command
        //m_parser->Close();
    }
}

void BigPictureSwitchCEC::StandbyAndSwitch()
{
    if (OpenAdapter(m_adapter)) {
        // Set our source to inactive
        m_parser->SetInactiveView();

  //      // If there is no active source on the TV, turn it off
  //      auto activeSource = m_parser->GetActiveSource();

		//DebugLog(L"Active source: %d", activeSource);

  //      if (activeSource == CEC::CECDEVICE_UNKNOWN || activeSource == CEC::CECDEVICE_TV) {
  //          // If there is no active source, turn the TV off.
		//	m_parser->StandbyDevices(CEC::CECDEVICE_TV);
  //      }

        // close the adapter after sending the command
        //m_parser->Close();
    }
}

void BigPictureSwitchCEC::TurnOff()
{
    if (OpenAdapter(m_adapter)) {
        DebugLog(L"BigPictureSwitchCEC::TurnOff: Sending power off");

        m_parser->SendKeypress(CEC::CECDEVICE_TV, CEC::CEC_USER_CONTROL_CODE_POWER_OFF_FUNCTION, true);
        m_parser->StandbyDevices(CEC::CECDEVICE_TV);

        //m_parser->Close();
    }
}

std::wstring BigPictureSwitchCEC::ToWString()
{
    return m_menuString;
}

PortInfo BigPictureSwitchCEC::PortInfo() { 
    CEC::libcec_configuration config;
    m_parser->GetCurrentConfiguration(&config);

    // If the physical address is set, we can derive the HDMI port from it
    return ::PortInfo{ .physAddr = config.iPhysicalAddress };

}

void BigPictureSwitchCEC::SetPhysicalAddress(uint16_t physicalAddress) {
	m_parser->SetPhysicalAddress(physicalAddress);
}


BigPictureSwitchCEC::~BigPictureSwitchCEC()
{
    if (m_opened) {
        m_parser->Close();
    }
    DebugLog(L"Closed CEC adapter");
}
