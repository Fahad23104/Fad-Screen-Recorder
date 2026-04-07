#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <iostream>

#pragma comment(lib, "ole32.lib")

class AudioCaptureEngine {
private:
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    WAVEFORMATEX* waveFormat = nullptr;

public:
    AudioCaptureEngine() = default;
    ~AudioCaptureEngine() { Cleanup(); }

    bool Initialize() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
        if (FAILED(hr)) return false;

        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) return false;

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
        if (FAILED(hr)) return false;

        hr = audioClient->GetMixFormat(&waveFormat);
        if (FAILED(hr)) return false;

        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, waveFormat, nullptr);
        if (FAILED(hr)) return false;

        hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
        if (FAILED(hr)) return false;

        hr = audioClient->Start();
        return SUCCEEDED(hr);
    }

    // FIX: Returns a boolean indicating if Windows flagged the chunk as pure silence
    bool AcquireAudio(BYTE** audioData, UINT32* numFramesAvailable, bool* isSilent) {
        HRESULT hr;
        DWORD flags;
        UINT64 devicePosition, qpcPosition;

        hr = captureClient->GetNextPacketSize(numFramesAvailable);
        if (FAILED(hr) || *numFramesAvailable == 0) return false;

        hr = captureClient->GetBuffer(audioData, numFramesAvailable, &flags, &devicePosition, &qpcPosition);
        if (FAILED(hr)) return false;

        *isSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        return true;
    }

    void ReleaseAudioBuffer(UINT32 numFramesRead) {
        if (captureClient) captureClient->ReleaseBuffer(numFramesRead);
    }

    WAVEFORMATEX* GetFormat() { return waveFormat; }

    void Cleanup() {
        if (audioClient) audioClient->Stop();
        if (waveFormat) { CoTaskMemFree(waveFormat); waveFormat = nullptr; }
        if (captureClient) { captureClient->Release(); captureClient = nullptr; }
        if (audioClient) { audioClient->Release(); audioClient = nullptr; }
        if (device) { device->Release(); device = nullptr; }
        if (enumerator) { enumerator->Release(); enumerator = nullptr; }
        CoUninitialize();
    }
};