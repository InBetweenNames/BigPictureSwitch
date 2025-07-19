
#include "BigPictureSwitch.h"
#include "BigPictureSwitchAudio.h"

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


LPCWSTR GetSelectedAudioDevice()
{
    return g_selectedAudioDeviceId.has_value() ? g_selectedAudioDeviceId->c_str() : nullptr;
}