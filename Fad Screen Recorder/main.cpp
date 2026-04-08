#include "UIManager.h"
#include "CaptureEngine.h"
#include "AudioCaptureEngine.h"
#include "WebcamEngine.h"
#include "EncoderEngine.h"
#include "ConfigManager.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <shobjidl.h> 
#include <objbase.h> 
#include <algorithm> 

namespace fs = std::filesystem;

std::atomic<bool> isRecording{ false };
std::atomic<bool> isPaused{ false };
std::atomic<int64_t> currentFileSize{ 0 };
std::atomic<int64_t> recordedSeconds{ 0 };

std::string GenerateUniqueFilename(const std::string& folder) {
    auto t = std::time(nullptr);
    struct tm tm;
    localtime_s(&tm, &t);

    std::ostringstream oss;
    oss << "FadRec_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".mp4";

    fs::path p(folder);
    p /= oss.str();
    return p.string();
}

void RecordingWorker(std::string finalOutputPath, ConfigManager config) {
    CaptureEngine capture;
    AudioCaptureEngine primaryAudio;
    AudioCaptureEngine secondaryAudio;
    WebcamEngine webcam;

    bool hasPrimaryAudio = false;
    bool hasSecondaryAudio = false;

    if (!capture.Initialize()) { isRecording = false; return; }
    if (config.showWebcam) webcam.Initialize();

    if (config.recordAudio) {
        if (config.audioSource == 0 || config.audioSource == 2) {
            hasPrimaryAudio = primaryAudio.Initialize(0); // Speakers
        }
        if (config.audioSource == 1 || config.audioSource == 2) {
            if (config.audioSource == 1) hasPrimaryAudio = primaryAudio.Initialize(1); // Mic Only
            else hasSecondaryAudio = secondaryAudio.Initialize(1); // Mixed Mic
        }
    }

    HDC hdc = GetDC(NULL);
    int screenW = GetDeviceCaps(hdc, DESKTOPHORZRES);
    int screenH = GetDeviceCaps(hdc, DESKTOPVERTRES);
    ReleaseDC(NULL, hdc);

    int sampleRate = 48000;
    int channels = 2;
    int bitsPerSample = 16;
    bool isFloat = false;

    if (hasPrimaryAudio) {
        sampleRate = primaryAudio.GetFormat()->nSamplesPerSec;
        channels = primaryAudio.GetFormat()->nChannels;
        bitsPerSample = primaryAudio.GetFormat()->wBitsPerSample;
        WAVEFORMATEX* wf = primaryAudio.GetFormat();
        if (wf && wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) isFloat = true;
        else if (wf && wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            WAVEFORMATEXTENSIBLE* pExt = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wf);
            const GUID SUBTYPE_IEEE_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
            if (memcmp(&pExt->SubFormat, &SUBTYPE_IEEE_FLOAT, sizeof(GUID)) == 0) isFloat = true;
        }
    }

    EncoderEngine encoder;
    if (!encoder.Initialize(screenW, screenH, config.fps, config.videoQuality, sampleRate, channels, bitsPerSample, isFloat, finalOutputPath.c_str())) {
        isRecording = false; return;
    }

    if (hasSecondaryAudio) {
        int sRate2 = secondaryAudio.GetFormat()->nSamplesPerSec;
        int sChannels2 = secondaryAudio.GetFormat()->nChannels;
        int sBits2 = secondaryAudio.GetFormat()->wBitsPerSample;
        bool sFloat2 = false;
        WAVEFORMATEX* wf2 = secondaryAudio.GetFormat();
        if (wf2 && wf2->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) sFloat2 = true;
        else if (wf2 && wf2->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            WAVEFORMATEXTENSIBLE* pExt = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wf2);
            const GUID SUBTYPE_IEEE_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
            if (memcmp(&pExt->SubFormat, &SUBTYPE_IEEE_FLOAT, sizeof(GUID)) == 0) sFloat2 = true;
        }
        encoder.InitializeSecondaryAudio(sRate2, sChannels2, sBits2, sFloat2);
    }

    auto startTime = std::chrono::steady_clock::now();
    auto lastFrameTime = startTime;
    int64_t totalAudioSamplesPushed = 0;
    int videoFramesEncoded = 0;
    int frameDelayMs = 1000 / config.fps;

    int64_t totalPauseDurationMs = 0;
    bool wasPaused = false;
    std::chrono::steady_clock::time_point pauseStartTime;

    while (isRecording) {
        auto now = std::chrono::steady_clock::now();

        if (isPaused) {
            if (!wasPaused) { pauseStartTime = now; wasPaused = true; }
            BYTE* dummyData; UINT32 dummyFrames; bool dummySilent;
            if (hasPrimaryAudio) while (primaryAudio.AcquireAudio(&dummyData, &dummyFrames, &dummySilent)) primaryAudio.ReleaseAudioBuffer(dummyFrames);
            if (hasSecondaryAudio) while (secondaryAudio.AcquireAudio(&dummyData, &dummyFrames, &dummySilent)) secondaryAudio.ReleaseAudioBuffer(dummyFrames);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        else if (wasPaused) {
            totalPauseDurationMs += std::chrono::duration_cast<std::chrono::milliseconds>(now - pauseStartTime).count();
            wasPaused = false;
            lastFrameTime = now;
        }

        int64_t effectiveElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count() - totalPauseDurationMs;
        recordedSeconds = effectiveElapsedMs / 1000;

        BYTE* audioData = nullptr;
        UINT32 framesAvailable = 0;
        bool isSilent = false;
        bool pulledAudioThisTick = false;

        if (hasPrimaryAudio) {
            while (primaryAudio.AcquireAudio(&audioData, &framesAvailable, &isSilent)) {
                pulledAudioThisTick = true;
                if (framesAvailable > 0) {
                    if (!isSilent && audioData) encoder.PushAudioData(audioData, (int)framesAvailable);
                    else encoder.InjectSilence((int)framesAvailable);
                    totalAudioSamplesPushed += framesAvailable;
                }
                primaryAudio.ReleaseAudioBuffer(framesAvailable);
            }
        }
        if (hasSecondaryAudio) {
            while (secondaryAudio.AcquireAudio(&audioData, &framesAvailable, &isSilent)) {
                if (framesAvailable > 0 && !isSilent && audioData) {
                    encoder.PushSecondaryAudioData(audioData, (int)framesAvailable);
                }
                secondaryAudio.ReleaseAudioBuffer(framesAvailable);
            }
        }

        int64_t expectedSamples = (effectiveElapsedMs * sampleRate) / 1000;
        int64_t missingSamples = expectedSamples - totalAudioSamplesPushed;

        if ((!pulledAudioThisTick || !config.recordAudio) && missingSamples > (sampleRate / 10)) {
            encoder.InjectSilence((int)missingSamples);
            totalAudioSamplesPushed += missingSamples;
        }

        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count() >= frameDelayMs) {
            ID3D11Texture2D* frameTexture = nullptr;
            if (capture.AcquireFrame(&frameTexture)) {
                uint8_t* rawPixels = nullptr;
                int rowPitch = 0;

                // CRITICAL FIX: Pass the cursor toggle to the capture engine
                if (capture.CopyFrameToCPU(frameTexture, &rawPixels, &rowPitch, config.recordCursor)) {

                    // Hook the live webcam straight into the CPU pixel buffer
                    if (config.showWebcam && rawPixels) {
                        int camW = webcam.GetWidth();
                        int camH = webcam.GetHeight();
                        if (camW > 0 && camH > 0) {
                            int pad = 30;
                            int sX = 0, sY = 0;
                            if (config.webcamPosition == 0) { sX = screenW - camW - pad; sY = pad; }
                            else if (config.webcamPosition == 1) { sX = screenW - camW - pad; sY = screenH - camH - pad; }
                            else if (config.webcamPosition == 2) { sX = pad; sY = screenH - camH - pad; }
                            else if (config.webcamPosition == 3) { sX = pad; sY = pad; }

                            webcam.GetFrame(rawPixels, screenW, screenH, rowPitch, sX, sY, config.webcamShape);
                        }
                    }

                    encoder.EncodeVideoFrame(rawPixels, rowPitch, effectiveElapsedMs);
                    capture.DoneWithCPUFrame();
                }
                frameTexture->Release();
                capture.ReleaseCurrentFrame();
            }
            else {
                encoder.EncodeVideoFrame(nullptr, 0, effectiveElapsedMs);
            }
            lastFrameTime = now;
            videoFramesEncoded++;

            if (videoFramesEncoded % 30 == 0) currentFileSize = encoder.GetCurrentFileSize();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::string OpenFolderPicker(HWND owner) {
    std::string result = "";
    IFileDialog* pfd;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
        if (SUCCEEDED(pfd->Show(owner))) {
            IShellItem* psi;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR pszPath;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, NULL, 0, NULL, NULL);
                    std::string utf8Path(size_needed - 1, 0);
                    WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, &utf8Path[0], size_needed, NULL, NULL);
                    result = utf8Path;
                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    return result;
}

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HDC hdc = GetDC(NULL);
    int screenW = GetDeviceCaps(hdc, DESKTOPHORZRES);
    int screenH = GetDeviceCaps(hdc, DESKTOPVERTRES);
    ReleaseDC(NULL, hdc);

    DEVMODE dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm);
    int maxFPS = dm.dmDisplayFrequency;
    if (maxFPS < 30) maxFPS = 60;

    std::vector<int> fpsValues = { 15, 30 };
    if (maxFPS >= 60) fpsValues.push_back(60);
    if (maxFPS >= 120) fpsValues.push_back(120);
    if (maxFPS >= 144) fpsValues.push_back(144);
    if (maxFPS >= 240) fpsValues.push_back(240);

    if (std::find(fpsValues.begin(), fpsValues.end(), maxFPS) == fpsValues.end()) {
        fpsValues.push_back(maxFPS);
        std::sort(fpsValues.begin(), fpsValues.end());
    }

    std::vector<std::string> fpsLabelStrings;
    std::vector<const char*> fpsLabelCStrs;
    for (int v : fpsValues) {
        if (v == maxFPS) fpsLabelStrings.push_back(std::to_string(v) + " FPS (Max)");
        else fpsLabelStrings.push_back(std::to_string(v) + " FPS");
    }
    for (const auto& str : fpsLabelStrings) { fpsLabelCStrs.push_back(str.c_str()); }

    const char* qualityLabels[] = { "Ultra High (Lossless / Huge File)", "High (Default / Good Balance)", "Medium (Smaller File)", "Low (Blurry / Smallest File)" };
    int qualityValues[] = { 18, 23, 28, 33 };

    UIManager ui;
    ConfigManager config;

    if (!ui.Initialize("Fad Recorder Pro", screenW / 2, screenH / 2)) {
        CoUninitialize();
        return -1;
    }

    std::thread backgroundWorker;

    bool lastF6 = false, lastF7 = false, lastF8 = false;

    auto StartOrResumeRecording = [&]() {
        if (!isRecording) {
            if (backgroundWorker.joinable()) backgroundWorker.join();
            std::string filepath = GenerateUniqueFilename(config.outputFolder);
            isRecording = true;
            isPaused = false;
            currentFileSize = 0;
            recordedSeconds = 0;
            ui.SetMiniMode(true, config.showOverlay);

            backgroundWorker = std::thread(RecordingWorker, filepath, config);
        }
        else if (isPaused) {
            isPaused = false;
        }
        };

    auto PauseRecording = [&]() { if (isRecording && !isPaused) isPaused = true; };

    auto StopRecording = [&]() {
        if (isRecording) {
            isRecording = false;
            isPaused = false;
            if (backgroundWorker.joinable()) backgroundWorker.join();
            ui.SetMiniMode(false);
        }
        };

    while (ui.ProcessMessages()) {
        if (ui.IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        bool f6Pressed = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        bool f7Pressed = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        bool f8Pressed = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;

        if (f6Pressed && !lastF6) StartOrResumeRecording();
        if (f7Pressed && !lastF7) PauseRecording();
        if (f8Pressed && !lastF8) StopRecording();

        lastF6 = f6Pressed; lastF7 = f7Pressed; lastF8 = f8Pressed;

        ui.BeginRender();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        if (isRecording && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) StopRecording();

        if (isRecording) {
            if (config.showOverlay) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("MiniWidget", ImVec2(0, 0), false);

                float blinkAlpha = (std::sin((float)ImGui::GetTime() * 8.0f) + 1.0f) * 0.5f;
                if (isPaused) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, blinkAlpha), "[ PAUSED ]");
                else ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, blinkAlpha), "[ REC ]");

                ImGui::SameLine(0, 15);
                int64_t secs = recordedSeconds.load();
                ImGui::Text("%02d:%02d:%02d", (int)(secs / 3600), (int)((secs % 3600) / 60), (int)(secs % 60));

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                if (ImGui::Button("STOP RECORDING (F8)", ImVec2(ImGui::GetWindowWidth() - 15, 45))) StopRecording();
                ImGui::PopStyleColor();
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }
        else {
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Fad Recorder Pro Engine");
            ImGui::Separator();
            ImGui::Spacing(); ImGui::Spacing();

            if (ImGui::BeginTabBar("MainTabs")) {

                if (ImGui::BeginTabItem("Capture Engine")) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Output Directory:");
                    ImGui::TextWrapped("%s", config.outputFolder.c_str());
                    ImGui::Spacing();

                    if (ImGui::Button("Change Folder")) {
                        std::string newFolder = OpenFolderPicker(ui.GetHWND());
                        if (!newFolder.empty()) { config.outputFolder = newFolder; config.Save(); }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Open Folder")) ShellExecuteA(NULL, "explore", config.outputFolder.c_str(), NULL, NULL, SW_SHOWNORMAL);

                    ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Hotkeys: F6 (Start/Resume) | F7 (Pause) | F8 (Stop)");
                    ImGui::Spacing();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.15f, 1.0f));
                    if (ImGui::Button("START RECORDING", ImVec2(ImGui::GetWindowWidth() - 20, 80))) StartOrResumeRecording();
                    ImGui::PopStyleColor();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Video Gallery")) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Double-click to play. Right-click for options.");
                    ImGui::BeginChild("GalleryList", ImVec2(0, 0), true);

                    if (fs::exists(config.outputFolder)) {
                        for (const auto& entry : fs::directory_iterator(config.outputFolder)) {
                            if (entry.path().extension() == ".mp4") {
                                std::string filename = entry.path().filename().string();
                                double mbSize = entry.file_size() / (1024.0 * 1024.0);

                                ImGui::PushID(filename.c_str());
                                if (ImGui::Selectable(filename.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                                    if (ImGui::IsMouseDoubleClicked(0)) ShellExecuteA(NULL, "open", entry.path().string().c_str(), NULL, NULL, SW_SHOWNORMAL);
                                }
                                ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                                ImGui::Text("%.1f MB", mbSize);

                                if (ImGui::BeginPopupContextItem("FileOptionsPopup")) {
                                    if (ImGui::Selectable("Play Video")) ShellExecuteA(NULL, "open", entry.path().string().c_str(), NULL, NULL, SW_SHOWNORMAL);
                                    if (ImGui::Selectable("Open File Location")) {
                                        std::string arg = "/select,\"" + entry.path().string() + "\"";
                                        ShellExecuteA(NULL, "open", "explorer.exe", arg.c_str(), NULL, SW_SHOWNORMAL);
                                    }
                                    if (ImGui::Selectable("Delete File")) fs::remove(entry.path());
                                    ImGui::EndPopup();
                                }
                                ImGui::PopID();
                            }
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Settings")) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Recording Configuration");
                    ImGui::Spacing(); ImGui::Spacing();

                    bool changed = false;

                    if (ImGui::Checkbox("Show Recording Overlay Mini-widget", &config.showOverlay)) changed = true;
                    if (ImGui::Checkbox("Capture Mouse Cursor", &config.recordCursor)) changed = true;

                    ImGui::Spacing();

                    // --- AUDIO ROUTING UI ---
                    if (ImGui::Checkbox("Record Audio Tracks", &config.recordAudio)) changed = true;
                    if (config.recordAudio) {
                        ImGui::Indent();
                        const char* audioSources[] = { "System Speakers (Loopback)", "Microphone Capture", "Speakers + Mic (Dual Init)" };
                        if (ImGui::Combo("Audio Routing", &config.audioSource, audioSources, IM_ARRAYSIZE(audioSources))) changed = true;
                        ImGui::Unindent();
                    }

                    ImGui::Spacing();

                    // --- WEBCAM OVERLAY UI ---
                    if (ImGui::Checkbox("Show Live Camera Overlay", &config.showWebcam)) changed = true;
                    if (config.showWebcam) {
                        ImGui::Indent();
                        const char* shapes[] = { "Square Cutout", "Circular Mask" };
                        if (ImGui::Combo("Overlay Shape", &config.webcamShape, shapes, IM_ARRAYSIZE(shapes))) changed = true;

                        const char* positions[] = { "Top-Right", "Bottom-Right", "Bottom-Left", "Top-Left" };
                        if (ImGui::Combo("Screen Position", &config.webcamPosition, positions, IM_ARRAYSIZE(positions))) changed = true;
                        ImGui::Unindent();
                    }

                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.6f);

                    int currentFpsIndex = -1;
                    for (size_t i = 0; i < fpsValues.size(); i++) { if (config.fps == fpsValues[i]) currentFpsIndex = (int)i; }
                    if (currentFpsIndex == -1) { currentFpsIndex = 1; config.fps = fpsValues[currentFpsIndex]; changed = true; }

                    if (ImGui::Combo("Target FPS", &currentFpsIndex, fpsLabelCStrs.data(), (int)fpsLabelCStrs.size())) {
                        config.fps = fpsValues[currentFpsIndex]; changed = true;
                    }

                    ImGui::Spacing(); ImGui::Spacing();
                    ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.6f);

                    int currentQualityIndex = -1;
                    for (int i = 0; i < 4; i++) { if (config.videoQuality == qualityValues[i]) currentQualityIndex = i; }
                    if (currentQualityIndex == -1) { currentQualityIndex = 1; config.videoQuality = qualityValues[currentQualityIndex]; changed = true; }

                    if (ImGui::Combo("Video Quality", &currentQualityIndex, qualityLabels, IM_ARRAYSIZE(qualityLabels))) {
                        config.videoQuality = qualityValues[currentQualityIndex]; changed = true;
                    }

                    if (changed) config.Save();

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }

        ImGui::End();
        ui.EndRender();
    }

    if (isRecording) StopRecording();
    CoUninitialize();
    return 0;
}