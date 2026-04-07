#include "UIManager.h"
#include "CaptureEngine.h"
#include "AudioCaptureEngine.h"
#include "EncoderEngine.h"
#include <thread>
#include <atomic>
#include <chrono>

std::atomic<bool> isRecording{ false };

void RecordingWorker() {
    std::cout << "[Worker] Booting A/V Engines..." << std::endl;
    CaptureEngine capture;
    AudioCaptureEngine audio;
    if (!capture.Initialize() || !audio.Initialize()) return;

    int screenW = 1920, screenH = 1080;
    ID3D11Texture2D* tempFrame = nullptr;
    while (!capture.AcquireFrame(&tempFrame) && isRecording) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!isRecording) return;

    D3D11_TEXTURE2D_DESC desc;
    tempFrame->GetDesc(&desc);
    screenW = desc.Width; screenH = desc.Height;
    tempFrame->Release();
    capture.ReleaseCurrentFrame();

    int sampleRate = audio.GetFormat()->nSamplesPerSec;
    int channels = audio.GetFormat()->nChannels;
    int bitsPerSample = audio.GetFormat()->wBitsPerSample;

    EncoderEngine encoder;
    if (!encoder.Initialize(screenW, screenH, 60, sampleRate, channels, bitsPerSample, "FadRecording_Final.mp4")) return;

    std::cout << "[Worker] Recording Started! Sync locked to Master Clock." << std::endl;

    auto startTime = std::chrono::steady_clock::now();
    auto lastFrameTime = startTime;

    int64_t totalAudioSamplesPushed = 0;

    while (isRecording) {
        auto now = std::chrono::steady_clock::now();
        int64_t elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();

        // --- 1. DRAIN THE WASAPI BUFFER ---
        BYTE* audioData = nullptr;
        UINT32 framesAvailable = 0;
        bool isSilent = false;
        bool pulledAudioThisTick = false;

        // FIX: MUST loop until Windows is totally empty, otherwise audio falls behind video!
        while (audio.AcquireAudio(&audioData, &framesAvailable, &isSilent)) {
            pulledAudioThisTick = true;
            if (framesAvailable > 0) {
                if (!isSilent && audioData != nullptr) {
                    encoder.PushAudioData(audioData, framesAvailable);
                }
                else {
                    encoder.InjectSilence(framesAvailable); // Flawless static-free silence
                }
                totalAudioSamplesPushed += framesAvailable;
            }
            audio.ReleaseAudioBuffer(framesAvailable);
        }

        // --- 2. SILENCE SAFETY NET ---
        int64_t expectedSamples = (elapsedMilliseconds * sampleRate) / 1000;
        int64_t missingSamples = expectedSamples - totalAudioSamplesPushed;

        // If Windows has given us NO audio for >100ms, it is truly asleep. Inject silence.
        if (!pulledAudioThisTick && missingSamples > (sampleRate / 10)) {
            encoder.InjectSilence(missingSamples);
            totalAudioSamplesPushed += missingSamples;
        }

        // --- 3. VIDEO CAPTURE (Strict ~60 FPS Pacing) ---
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count() >= 16) {
            ID3D11Texture2D* frameTexture = nullptr;

            if (capture.AcquireFrame(&frameTexture)) {
                uint8_t* rawPixels = nullptr;
                int rowPitch = 0;
                if (capture.CopyFrameToCPU(frameTexture, &rawPixels, &rowPitch)) {
                    // Send the new frame
                    encoder.EncodeVideoFrame(rawPixels, rowPitch, elapsedMilliseconds);
                    capture.DoneWithCPUFrame();
                }
                frameTexture->Release();
                capture.ReleaseCurrentFrame();
            }
            else {
                // FIX: Screen didn't change! Repeat the previous image with a NEW timestamp 
                // so the video file doesn't abruptly end.
                encoder.EncodeVideoFrame(nullptr, 0, elapsedMilliseconds);
            }
            lastFrameTime = now;
        }

        // Let the CPU breathe
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "[Worker] Recording Stopped. Saving and flushing MP4..." << std::endl;
}

// ---------------------------------------------------------
// MAIN APPLICATION (UI THREAD)
// ---------------------------------------------------------
int main() {
    UIManager ui;

    if (!ui.Initialize("Fad Screen Recorder", 400, 250)) return -1;

    std::thread backgroundWorker;

    while (ui.ProcessMessages()) {
        ui.BeginRender();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        ImGui::TextWrapped("Fad Recorder X - Professional Engine Active");
        ImGui::Separator();
        ImGui::Spacing(); ImGui::Spacing();

        if (!isRecording) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));

            if (ImGui::Button("START RECORDING", ImVec2(ImGui::GetWindowWidth(), 60))) {
                isRecording = true;
                backgroundWorker = std::thread(RecordingWorker);
            }
            ImGui::PopStyleColor(2);
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));

            if (ImGui::Button("STOP RECORDING", ImVec2(ImGui::GetWindowWidth(), 60))) {
                isRecording = false;
                if (backgroundWorker.joinable()) backgroundWorker.join();
            }
            ImGui::PopStyleColor(2);

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Recording in progress... DO NOT close window.");
        }

        ImGui::End();
        ui.EndRender();
    }

    if (isRecording) {
        isRecording = false;
        if (backgroundWorker.joinable()) backgroundWorker.join();
    }

    return 0;
}