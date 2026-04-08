#pragma once
#include <string>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

class ConfigManager {
public:
    std::string outputFolder;
    int fps;
    int videoQuality;
    bool showOverlay;
    bool recordAudio;

    // NEW SETTINGS
    int audioSource;    // 0: Speakers, 1: Microphone, 2: Both
    bool showWebcam;    // Overlay toggle
    int webcamShape;    // 0: Square, 1: Circle
    int webcamPosition; // 0: Top-Right, 1: Bottom-Right, 2: Bottom-Left, 3: Top-Left

    ConfigManager() {
        outputFolder = fs::current_path().string() + "\\FadRecordings";
        fps = 60;
        videoQuality = 23;
        showOverlay = true;
        recordAudio = true;
        audioSource = 0;
        showWebcam = false;
        webcamShape = 1;    // Default to Circular
        webcamPosition = 1; // Default to Bottom-Right
        Load();

        if (!fs::exists(outputFolder)) {
            fs::create_directories(outputFolder);
        }
    }

    void Load() {
        if (fs::exists("config.json")) {
            std::ifstream f("config.json");
            try {
                json data = json::parse(f);
                outputFolder = data.value("OutputFolder", outputFolder);
                fps = data.value("FPS", fps);
                videoQuality = data.value("VideoQuality", videoQuality);
                showOverlay = data.value("ShowOverlay", showOverlay);
                recordAudio = data.value("RecordAudio", recordAudio);
                audioSource = data.value("AudioSource", audioSource);
                showWebcam = data.value("ShowWebcam", showWebcam);
                webcamShape = data.value("WebcamShape", webcamShape);
                webcamPosition = data.value("WebcamPosition", webcamPosition);
            }
            catch (...) {}
        }
    }

    void Save() {
        json data;
        data["OutputFolder"] = outputFolder;
        data["FPS"] = fps;
        data["VideoQuality"] = videoQuality;
        data["ShowOverlay"] = showOverlay;
        data["RecordAudio"] = recordAudio;
        data["AudioSource"] = audioSource;
        data["ShowWebcam"] = showWebcam;
        data["WebcamShape"] = webcamShape;
        data["WebcamPosition"] = webcamPosition;
        std::ofstream f("config.json");
        f << data.dump(4);
    }
};