#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <iostream>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib") // CRITICAL FIX: Required for MFEnumDeviceSources

class WebcamEngine {
private:
    IMFSourceReader* pReader = nullptr;
    int width = 0;
    int height = 0;
    bool isInitialized = false;

public:
    WebcamEngine() = default;
    ~WebcamEngine() { Cleanup(); }

    bool Initialize() {
        HRESULT hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) return false;

        IMFAttributes* pConfig = nullptr;
        MFCreateAttributes(&pConfig, 1);
        pConfig->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        IMFActivate** ppDevices = nullptr;
        UINT32 count = 0;
        hr = MFEnumDeviceSources(pConfig, &ppDevices, &count);
        if (FAILED(hr) || count == 0) { pConfig->Release(); return false; }

        IMFMediaSource* pSource = nullptr;
        hr = ppDevices[0]->ActivateObject(IID_PPV_ARGS(&pSource));
        for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        pConfig->Release();

        if (FAILED(hr)) return false;

        hr = MFCreateSourceReaderFromMediaSource(pSource, nullptr, &pReader);
        pSource->Release();
        if (FAILED(hr)) return false;

        // Force RGB32 so we can easily blend it with DXGI's BGRA format
        IMFMediaType* pType = nullptr;
        MFCreateMediaType(&pType);
        pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType);
        pType->Release();

        IMFMediaType* pCurrentType = nullptr;
        pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
        MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, (UINT32*)&width, (UINT32*)&height);
        pCurrentType->Release();

        isInitialized = true;
        return true;
    }

    int GetWidth() { return width; }
    int GetHeight() { return height; }

    bool GetFrame(uint8_t* outPixels, int destWidth, int destHeight, int destRowPitch, int sX, int sY, int shape) {
        if (!isInitialized || !pReader) return false;

        IMFSample* pSample = nullptr;
        DWORD streamIndex, flags;
        LONGLONG llTimeStamp;
        HRESULT hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &pSample);

        if (FAILED(hr) || !pSample) return false;

        IMFMediaBuffer* pBuffer = nullptr;
        pSample->ConvertToContiguousBuffer(&pBuffer);

        BYTE* pData = nullptr;
        DWORD cbMaxLength = 0, cbCurrentLength = 0;
        pBuffer->Lock(&pData, &cbMaxLength, &cbCurrentLength);

        float cx = width / 2.0f;
        float cy = height / 2.0f;
        float r2 = (cx < cy ? cx : cy) * (cx < cy ? cx : cy);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int dx = sX + x;
                int dy = sY + (height - 1 - y); // MF RGB32 is mapped bottom-up in memory

                if (dx < 0 || dx >= destWidth || dy < 0 || dy >= destHeight) continue;

                bool draw = true;
                float dist = (x - cx) * (x - cx) + (y - cy) * (y - cy);

                if (shape == 1 && dist > r2) draw = false; // Circular Mask

                if (draw) {
                    int srcIdx = (y * (width * 4)) + (x * 4);
                    int dstIdx = (dy * destRowPitch) + (dx * 4);

                    outPixels[dstIdx] = pData[srcIdx];       // B
                    outPixels[dstIdx + 1] = pData[srcIdx + 1];   // G
                    outPixels[dstIdx + 2] = pData[srcIdx + 2];   // R
                }
            }
        }

        pBuffer->Unlock();
        pBuffer->Release();
        pSample->Release();
        return true;
    }

    void Cleanup() {
        if (pReader) { pReader->Release(); pReader = nullptr; }
        if (isInitialized) { MFShutdown(); isInitialized = false; }
    }
};