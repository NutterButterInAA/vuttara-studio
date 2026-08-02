#include "AudioDeviceEnumerator.hpp"

#include <QSet>

#include <Windows.h>
#include <mmdeviceapi.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <wrl/client.h>

namespace Vuttara {
namespace {

using Microsoft::WRL::ComPtr;

class ComApartment final
{
public:
    ComApartment()
        : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
    {
    }

    ~ComApartment()
    {
        if (result_ == S_OK || result_ == S_FALSE) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool usable() const
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_ = E_FAIL;
};

QString friendlyName(IMMDevice* device)
{
    if (device == nullptr) {
        return {};
    }

    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()))) {
        return {};
    }

    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT result = properties->GetValue(PKEY_Device_FriendlyName, &value);
    const QString name = SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal != nullptr
        ? QString::fromWCharArray(value.pwszVal)
        : QString{};
    PropVariantClear(&value);
    return name;
}

QString deviceId(IMMDevice* device)
{
    LPWSTR value = nullptr;
    if (device == nullptr || FAILED(device->GetId(&value)) || value == nullptr) {
        return {};
    }

    const QString id = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return id;
}

QVector<AudioDeviceInfo> enumerateDevices(EDataFlow flow, ERole defaultRole)
{
    QVector<AudioDeviceInfo> result;
    ComApartment apartment;
    if (!apartment.usable()) {
        return result;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(enumerator.GetAddressOf())))) {
        return result;
    }

    QString defaultName = QStringLiteral("Windows default device");
    QString defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, defaultRole, defaultDevice.GetAddressOf()))) {
        const QString discoveredName = friendlyName(defaultDevice.Get());
        if (!discoveredName.isEmpty()) {
            defaultName = QStringLiteral("Default — %1").arg(discoveredName);
        }
        defaultId = deviceId(defaultDevice.Get());
    }

    result.append(AudioDeviceInfo{defaultName, QStringLiteral("default"), true});

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.GetAddressOf()))) {
        return result;
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        return result;
    }

    QSet<QString> seenIds;
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, device.GetAddressOf()))) {
            continue;
        }

        const QString id = deviceId(device.Get());
        if (id.isEmpty() || seenIds.contains(id)) {
            continue;
        }
        seenIds.insert(id);

        QString name = friendlyName(device.Get());
        if (name.isEmpty()) {
            name = QStringLiteral("Unnamed Windows audio device");
        }

        result.append(AudioDeviceInfo{name, id, id == defaultId});
    }

    return result;
}

}

QVector<AudioDeviceInfo> enumerateDesktopAudioDevices()
{
    return enumerateDevices(eRender, eConsole);
}

QVector<AudioDeviceInfo> enumerateMicrophoneDevices()
{
    return enumerateDevices(eCapture, eCommunications);
}

}
