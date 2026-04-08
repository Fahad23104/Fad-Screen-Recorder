#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <iostream>
#include <cmath>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib") 

class WebcamEngine {
private:
    IMFSourceReader* pReader = nullptr;
    int width = 0;
    int height = 0;
    LONG stride = 0;
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

        IMFAttributes* pReaderAttributes = nullptr;
        MFCreateAttributes(&pReaderAttributes, 1);
        pReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1);

        hr = MFCreateSourceReaderFromMediaSource(pSource, pReaderAttributes, &pReader);
        pReaderAttributes->Release();
        pSource->Release();
        if (FAILED(hr)) return false;

        IMFMediaType* pType = nullptr;
        MFCreateMediaType(&pType);
        pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType);
        pType->Release();

        if (FAILED(hr)) {
            Cleanup();
            return false;
        }

        IMFMediaType* pCurrentType = nullptr;
        pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
        MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, (UINT32*)&width, (UINT32*)&height);

        hr = pCurrentType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&stride);
        if (FAILED(hr)) {
            MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride);
        }

        pCurrentType->Release();
        isInitialized = true;
        return true;
    }

    int GetWidth() { return width; }
    int GetHeight() { return height; }

    // CRITICAL FIX: Accepts targetW and targetH to dynamically scale the camera down
    bool GetFrame(uint8_t* outPixels, int destWidth, int destHeight, int destRowPitch, int targetW, int targetH, int sX, int sY, int shape) {
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

        // Circular math for styling
        float cx = targetW / 2.0f;
        float cy = targetH / 2.0f;
        float rOuter = cx < cy ? cx : cy;
        float rInner = rOuter - 3.0f; // 3 pixel border width
        float rOuter2 = rOuter * rOuter;
        float rInner2 = rInner * rInner;

        int absStride = std::abs(stride);

        // Render loop over the target SCALED dimensions, not the native camera dimensions
        for (int y = 0; y < targetH; y++) {
            for (int x = 0; x < targetW; x++) {
                int dx = sX + x;
                int dy = sY + y; // FIXED: No longer flips upside down!

                if (dx < 0 || dx >= destWidth || dy < 0 || dy >= destHeight) continue;

                bool draw = true;
                bool isBorder = false;
                float dist = (x - cx) * (x - cx) + (y - cy) * (y - cy);

                if (shape == 1) {
                    if (dist > rOuter2) draw = false; // Outside circle
                    else if (dist > rInner2) isBorder = true; // Border ring
                }
                else {
                    isBorder = (x < 3 || y < 3 || x > targetW - 4 || y > targetH - 4); // Square border
                }

                if (draw) {
                    // Fast Nearest-Neighbor Scaling Algorithm
                    int srcX = (x * width) / targetW;
                    int srcY = (y * height) / targetH;

                    int srcIdx = (srcY * absStride) + (srcX * 4);
                    int dstIdx = (dy * destRowPitch) + (dx * 4);

                    if (srcIdx + 2 < (int)cbCurrentLength) {
                        if (isBorder) {
                            outPixels[dstIdx] = 200; outPixels[dstIdx + 1] = 50; outPixels[dstIdx + 2] = 50; // Deep Red Border
                        }
                        else {
                            outPixels[dstIdx] = pData[srcIdx];       // B
                            outPixels[dstIdx + 1] = pData[srcIdx + 1];   // G
                            outPixels[dstIdx + 2] = pData[srcIdx + 2];   // R
                        }
                    }
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