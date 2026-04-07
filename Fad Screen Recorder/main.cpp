#include "CaptureEngine.h"
#include "EncoderEngine.h"

int main() {
    std::cout << "--- Starting Fad Recorder Core Engine ---" << std::endl;

    // 1. Boot Capture Engine
    CaptureEngine capture;
    if (!capture.Initialize()) {
        std::cerr << "Capture initialization failed." << std::endl;
        return -1;
    }

    // Pull exactly ONE frame just to get the monitor's exact resolution
    int screenWidth = 1920;
    int screenHeight = 1080;
    ID3D11Texture2D* tempFrame = nullptr;

    std::cout << "Probing monitor resolution..." << std::endl;
    while (!capture.AcquireFrame(&tempFrame)) {
        // Loop until we get the first frame (move mouse if stuck)
    }

    D3D11_TEXTURE2D_DESC desc;
    tempFrame->GetDesc(&desc);
    screenWidth = desc.Width;
    screenHeight = desc.Height;
    tempFrame->Release();
    capture.ReleaseCurrentFrame();

    std::cout << "Detected Monitor Resolution: " << screenWidth << "x" << screenHeight << std::endl;

    // 2. Boot Universal Encoder Engine with the detected resolution
    EncoderEngine encoder;
    if (!encoder.Initialize(screenWidth, screenHeight, 60, "FadRecording_Test.mp4")) {
        std::cerr << "Encoder initialization failed." << std::endl;
        return -1;
    }

    std::cout << "\n[!] System is primed. We have GPU access and an open MP4 file." << std::endl;
    std::cout << "Press Enter to shut down and save the empty file...";
    std::cin.get();

    return 0;
}