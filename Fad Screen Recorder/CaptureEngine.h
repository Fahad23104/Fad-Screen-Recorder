#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class CaptureEngine {
private:
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    IDXGIOutputDuplication* deskDupl = nullptr;
    ID3D11Texture2D* stagingTexture = nullptr; // NEW: RAM buffer

public:
    CaptureEngine() = default;
    ~CaptureEngine() { Cleanup(); }

    bool Initialize() {
        HRESULT hr;
        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);
        if (FAILED(hr)) return false;

        IDXGIDevice* dxgiDevice = nullptr;
        d3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
        IDXGIAdapter* dxgiAdapter = nullptr;
        dxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&dxgiAdapter));
        dxgiDevice->Release();

        IDXGIOutput* dxgiOutput = nullptr;
        dxgiAdapter->EnumOutputs(0, &dxgiOutput);
        dxgiAdapter->Release();

        IDXGIOutput1* dxgiOutput1 = nullptr;
        dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&dxgiOutput1));
        dxgiOutput->Release();

        hr = dxgiOutput1->DuplicateOutput(d3dDevice, &deskDupl);
        dxgiOutput1->Release();

        return SUCCEEDED(hr);
    }

    bool AcquireFrame(ID3D11Texture2D** acquiredTexture) {
        if (!deskDupl) return false;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        IDXGIResource* desktopResource = nullptr;
        HRESULT hr = deskDupl->AcquireNextFrame(0, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
        if (FAILED(hr)) return false;

        hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(acquiredTexture));
        desktopResource->Release();
        if (FAILED(hr)) { deskDupl->ReleaseFrame(); return false; }
        return true;
    }

    // NEW: Copies the GPU frame to CPU RAM so FFmpeg can read it
    bool CopyFrameToCPU(ID3D11Texture2D* frameTexture, uint8_t** outData, int* outRowPitch) {
        D3D11_TEXTURE2D_DESC desc;
        frameTexture->GetDesc(&desc);

        // Create the staging texture once
        if (!stagingTexture) {
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            if (FAILED(d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTexture))) {
                return false;
            }
        }

        // Copy from GPU to RAM
        d3dContext->CopyResource(stagingTexture, frameTexture);

        // Lock the RAM buffer to give FFmpeg a pointer to the raw pixels
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) {
            *outData = (uint8_t*)mapped.pData;
            *outRowPitch = mapped.RowPitch;
            return true;
        }
        return false;
    }

    // NEW: Unlock the RAM buffer
    void DoneWithCPUFrame() {
        if (stagingTexture) d3dContext->Unmap(stagingTexture, 0);
    }

    void ReleaseCurrentFrame() {
        if (deskDupl) deskDupl->ReleaseFrame();
    }

    void Cleanup() {
        if (stagingTexture) { stagingTexture->Release(); stagingTexture = nullptr; }
        if (deskDupl) { deskDupl->Release(); deskDupl = nullptr; }
        if (d3dContext) { d3dContext->Release(); d3dContext = nullptr; }
        if (d3dDevice) { d3dDevice->Release(); d3dDevice = nullptr; }
    }
};