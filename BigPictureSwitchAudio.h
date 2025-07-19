#pragma once
#include "framework.h"

#include <vector>
#include <string>
#include <optional>

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

struct AudioDevice
{
    std::wstring id;
    std::wstring name;
};

// Audio related functions
void                SetSelectedAudioDevice(LPCWSTR deviceId);
LPCWSTR             GetSelectedAudioDevice();
void                InitializeAudio();
void                CleanupAudio();
HRESULT             GetCurrentAudioDevice(std::wstring& ppwszDeviceId, std::wstring& ppwszDeviceName);
HRESULT             EnumerateAudioDevices(std::vector<AudioDevice>& devices);
HRESULT             SetDefaultAudioDevice(LPCWSTR deviceId);


// Audio-related globals to be migrated to RAII
inline IMMDeviceEnumerator* g_pEnumerator = nullptr;
// GUID for the PolicyConfig interface
inline IPolicyConfig* g_pPolicyConfig = nullptr; // Pointer to the PolicyConfig interface for setting default audio device

// Audio-related globals
inline std::vector<AudioDevice> g_audioDevices; // Store enumerated audio devices
inline std::optional<std::wstring> g_selectedAudioDeviceId = {}; // Store the user's selected audio device
inline std::wstring g_origAudioDeviceId; // Store the main audio device name
