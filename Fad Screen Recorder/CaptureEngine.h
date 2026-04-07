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

public:
    CaptureEngine() = default;
    ~CaptureEngine() {
        Cleanup();
    }

    bool Initialize() {
        HRESULT hr;

        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);

        if (FAILED(hr)) return false;

        IDXGIDevice* dxgiDevice = nullptr;
        hr = d3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
        if (FAILED(hr)) return false;

        IDXGIAdapter* dxgiAdapter = nullptr;
        hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&dxgiAdapter));
        dxgiDevice->Release();
        if (FAILED(hr)) return false;

        IDXGIOutput* dxgiOutput = nullptr;
        hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
        dxgiAdapter->Release();
        if (FAILED(hr)) return false;

        IDXGIOutput1* dxgiOutput1 = nullptr;
        hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&dxgiOutput1));
        dxgiOutput->Release();
        if (FAILED(hr)) return false;

        hr = dxgiOutput1->DuplicateOutput(d3dDevice, &deskDupl);
        dxgiOutput1->Release();

        if (FAILED(hr)) return false;

        return true;
    }

    // NEW: Pulls the raw frame from the GPU
    bool AcquireFrame(ID3D11Texture2D** acquiredTexture) {
        if (!deskDupl) return false;

        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        IDXGIResource* desktopResource = nullptr;

        // Wait up to 100ms for a screen update. 
        // If the screen is perfectly still, DXGI saves power and returns a timeout.
        HRESULT hr = deskDupl->AcquireNextFrame(100, &frameInfo, &desktopResource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return false; // Normal behavior if screen is static
        }
        if (FAILED(hr)) {
            std::cerr << "Capture error: " << std::hex << hr << std::endl;
            return false;
        }

        // Convert the generic resource into a 2D Texture we can encode later
        hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(acquiredTexture));
        desktopResource->Release();

        if (FAILED(hr)) {
            deskDupl->ReleaseFrame();
            return false;
        }

        return true;
    }

    // NEW: Must be called after we are done with a frame so the GPU can recycle it
    void ReleaseCurrentFrame() {
        if (deskDupl) deskDupl->ReleaseFrame();
    }

    void Cleanup() {
        if (deskDupl) { deskDupl->Release(); deskDupl = nullptr; }
        if (d3dContext) { d3dContext->Release(); d3dContext = nullptr; }
        if (d3dDevice) { d3dDevice->Release(); d3dDevice = nullptr; }
    }
};