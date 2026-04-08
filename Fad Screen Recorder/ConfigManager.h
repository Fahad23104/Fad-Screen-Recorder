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
    int videoQuality; // CRF value
    bool showOverlay;
    bool recordAudio;

    ConfigManager() {
        outputFolder = fs::current_path().string() + "\\FadRecordings";
        fps = 60;
        videoQuality = 23;
        showOverlay = true; // Overlay enabled by default
        recordAudio = true; // Audio enabled by default
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
        std::ofstream f("config.json");
        f << data.dump(4);
    }
};