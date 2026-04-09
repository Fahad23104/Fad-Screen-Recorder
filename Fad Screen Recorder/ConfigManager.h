#pragma once
#include <string>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <shlobj.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

class ConfigManager {
public:
    std::string outputFolder;
    int fps;
    int videoQuality;
    bool showOverlay;
    bool recordAudio;
    bool recordCursor;
    bool showMouseClicks; // NEW SETTING

    int audioSource;
    bool showWebcam;
    int webcamShape;
    int webcamPosition;

    ConfigManager() {
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, path))) {
            outputFolder = std::string(path) + "\\Fad Screen Recorder";
        }
        else {
            outputFolder = fs::current_path().string() + "\\Fad Screen Recorder";
        }

        fps = 60;
        videoQuality = 23;
        showOverlay = true;
        recordAudio = true;
        recordCursor = true;
        showMouseClicks = true; // Click effects enabled by default
        audioSource = 0;
        showWebcam = false;
        webcamShape = 1;
        webcamPosition = 1;
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
                recordCursor = data.value("RecordCursor", recordCursor);
                showMouseClicks = data.value("ShowMouseClicks", showMouseClicks);
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
        data["RecordCursor"] = recordCursor;
        data["ShowMouseClicks"] = showMouseClicks;
        data["AudioSource"] = audioSource;
        data["ShowWebcam"] = showWebcam;
        data["WebcamShape"] = webcamShape;
        data["WebcamPosition"] = webcamPosition;
        std::ofstream f("config.json");
        f << data.dump(4);
    }
};