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
        // FIXED: Explicitly check if waveFormat is null to satisfy the static analyzer
        if (FAILED(hr) || !waveFormat) return false;

        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, waveFormat, nullptr);

        if (FAILED(hr)) {
            std::cout << "[*] Native format rejected. Engaging 32-bit Float Sanitizer..." << std::endl;
            WAVEFORMATEX* floatFmt = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));

            // FIXED: Check for NULL allocation
            if (!floatFmt) return false;

            floatFmt->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            floatFmt->nChannels = waveFormat->nChannels;
            floatFmt->nSamplesPerSec = waveFormat->nSamplesPerSec;
            floatFmt->wBitsPerSample = 32;
            floatFmt->nBlockAlign = (floatFmt->nChannels * floatFmt->wBitsPerSample) / 8;
            floatFmt->nAvgBytesPerSec = floatFmt->nSamplesPerSec * floatFmt->nBlockAlign;
            floatFmt->cbSize = 0;

            CoTaskMemFree(waveFormat);
            waveFormat = floatFmt;

            DWORD autoFlags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
            hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, autoFlags, 10000000, 0, waveFormat, nullptr);
        }

        if (FAILED(hr)) {
            std::cout << "[*] 32-bit float rejected. Engaging 16-bit PCM Nuclear Fallback..." << std::endl;
            WAVEFORMATEX* intFmt = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));

            // FIXED: Check for NULL allocation
            if (!intFmt) return false;

            intFmt->wFormatTag = WAVE_FORMAT_PCM;
            intFmt->nChannels = 2;
            intFmt->nSamplesPerSec = 48000;
            intFmt->wBitsPerSample = 16;
            intFmt->nBlockAlign = (intFmt->nChannels * intFmt->wBitsPerSample) / 8;
            intFmt->nAvgBytesPerSec = intFmt->nSamplesPerSec * intFmt->nBlockAlign;
            intFmt->cbSize = 0;

            CoTaskMemFree(waveFormat);
            waveFormat = intFmt;

            DWORD autoFlags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
            hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, autoFlags, 10000000, 0, waveFormat, nullptr);
        }

        if (FAILED(hr)) {
            std::cerr << "[-] Absolute Audio Failure. Error Code: 0x" << std::hex << hr << std::endl;
            return false;
        }

        hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
        if (FAILED(hr)) return false;

        hr = audioClient->Start();
        return SUCCEEDED(hr);
    }

    bool AcquireAudio(BYTE** audioData, UINT32* numFramesAvailable, bool* isSilent) {
        if (!captureClient) return false;
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