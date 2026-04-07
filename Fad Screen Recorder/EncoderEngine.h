#pragma once
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

class EncoderEngine {
private:
    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVStream* videoStream = nullptr;
    const AVCodec* codec = nullptr;

    std::string selectedEncoderName;

public:
    EncoderEngine() = default;
    ~EncoderEngine() { Cleanup(); }

    bool Initialize(int width, int height, int fps, const char* outputFilename) {
        std::cout << "\n--- Initializing Universal FFmpeg Encoder ---" << std::endl;

        // 1. The Fallback Array: Try NVENC -> AMF -> QSV -> CPU
        std::vector<std::string> encoderNames = { "h264_nvenc", "h264_amf", "h264_qsv", "libx264" };

        for (const auto& name : encoderNames) {
            codec = avcodec_find_encoder_by_name(name.c_str());
            if (codec) {
                selectedEncoderName = name;
                std::cout << "[+] Successfully acquired hardware encoder: " << name << std::endl;
                break;
            }
        }

        if (!codec) {
            std::cerr << "[-] FATAL: Could not find ANY h264 encoder. Is FFmpeg installed?" << std::endl;
            return false;
        }

        // 2. Allocate the MP4 File Container
        avformat_alloc_output_context2(&formatCtx, nullptr, "mp4", outputFilename);
        if (!formatCtx) return false;

        // 3. Create the Video Stream inside the MP4
        videoStream = avformat_new_stream(formatCtx, codec);
        if (!videoStream) return false;

        // 4. Configure the Encoder Settings
        codecCtx = avcodec_alloc_context3(codec);
        codecCtx->width = width;
        codecCtx->height = height;
        codecCtx->time_base = { 1, fps };
        videoStream->time_base = codecCtx->time_base;
        codecCtx->framerate = { fps, 1 };

        // NV12 is the standard pixel format required by almost all hardware encoders
        codecCtx->pix_fmt = AV_PIX_FMT_NV12;

        // Set Bitrate: 5,000,000 bits = 5 Mbps (Good quality for 1080p60)
        codecCtx->bit_rate = 5000000;

        // 5. Open the Hardware Encoder
        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            std::cerr << "[-] Error: Could not boot the encoder: " << selectedEncoderName << std::endl;
            return false;
        }

        // 6. Copy settings to the MP4 stream
        avcodec_parameters_from_context(videoStream->codecpar, codecCtx);

        // 7. Create the physical file on the disk
        if (!(formatCtx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&formatCtx->pb, outputFilename, AVIO_FLAG_WRITE) < 0) {
                std::cerr << "[-] Error: Could not write to disk. Check permissions." << std::endl;
                return false;
            }
        }

        // 8. Write the MP4 Header (Starts the file)
        if (avformat_write_header(formatCtx, nullptr) < 0) {
            std::cerr << "[-] Error: Could not write MP4 header." << std::endl;
            return false;
        }

        std::cout << "[+] Encoder configured! Empty file created at: " << outputFilename << std::endl;
        return true;
    }

    void Cleanup() {
        if (formatCtx) {
            // Write the MP4 trailer to safely close the video file
            av_write_trailer(formatCtx);
            if (!(formatCtx->oformat->flags & AVFMT_NOFILE) && formatCtx->pb) {
                avio_closep(&formatCtx->pb);
            }
            avformat_free_context(formatCtx);
            formatCtx = nullptr;
        }
        if (codecCtx) {
            avcodec_free_context(&codecCtx);
            codecCtx = nullptr;
        }
    }
};