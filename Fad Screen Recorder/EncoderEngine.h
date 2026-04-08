#pragma once
#define NOMINMAX // Explicitly block Windows macros here
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
}

class EncoderEngine {
private:
    AVFormatContext* formatCtx = nullptr;

    AVCodecContext* videoCodecCtx = nullptr;
    AVStream* videoStream = nullptr;
    SwsContext* swsCtx = nullptr;
    AVFrame* videoFrame = nullptr;

    AVCodecContext* audioCodecCtx = nullptr;
    AVStream* audioStream = nullptr;

    // Primary Audio (Speakers)
    SwrContext* swrCtx = nullptr;
    AVAudioFifo* audioFifo = nullptr;

    // Secondary Audio (Microphone Mixer)
    SwrContext* swrCtx2 = nullptr;
    AVAudioFifo* secondaryFifo = nullptr;
    bool hasSecondaryAudio = false;

    AVFrame* audioFrame = nullptr;
    int64_t audioFrameCounter = 0;

    AVPacket* packet = nullptr;

    void DrainAudioFifos() {
        if (!hasSecondaryAudio) {
            while (av_audio_fifo_size(audioFifo) >= audioCodecCtx->frame_size) {
                av_audio_fifo_read(audioFifo, (void**)audioFrame->data, audioCodecCtx->frame_size);

                audioFrame->pts = audioFrameCounter;
                audioFrameCounter += audioFrame->nb_samples;

                avcodec_send_frame(audioCodecCtx, audioFrame);
                while (avcodec_receive_packet(audioCodecCtx, packet) == 0) {
                    av_packet_rescale_ts(packet, audioCodecCtx->time_base, audioStream->time_base);
                    packet->stream_index = audioStream->index;
                    av_interleaved_write_frame(formatCtx, packet);
                    av_packet_unref(packet);
                }
            }
        }
        else {
            // NATIVE FLOAT AUDIO MIXER: Perfectly mixes mic and speakers mathematically
            int minFrames = std::min(av_audio_fifo_size(audioFifo), av_audio_fifo_size(secondaryFifo));
            while (minFrames >= audioCodecCtx->frame_size) {
                int fs = audioCodecCtx->frame_size;

                float* buf1[2];
                float* buf2[2];
                av_samples_alloc((uint8_t**)buf1, nullptr, 2, fs, AV_SAMPLE_FMT_FLTP, 0);
                av_samples_alloc((uint8_t**)buf2, nullptr, 2, fs, AV_SAMPLE_FMT_FLTP, 0);

                av_audio_fifo_read(audioFifo, (void**)buf1, fs);
                av_audio_fifo_read(secondaryFifo, (void**)buf2, fs);

                float* outL = (float*)audioFrame->data[0];
                float* outR = (float*)audioFrame->data[1];

                for (int i = 0; i < fs; i++) {
                    outL[i] = buf1[0][i] + buf2[0][i];
                    outR[i] = buf1[1][i] + buf2[1][i]; // Mixes cleanly even if Mic is mono mapped to stereo

                    // Hard clipper to prevent static blowout
                    if (outL[i] > 1.0f) outL[i] = 1.0f; else if (outL[i] < -1.0f) outL[i] = -1.0f;
                    if (outR[i] > 1.0f) outR[i] = 1.0f; else if (outR[i] < -1.0f) outR[i] = -1.0f;
                }

                av_freep(&buf1[0]);
                av_freep(&buf2[0]);

                audioFrame->pts = audioFrameCounter;
                audioFrameCounter += audioFrame->nb_samples;
                avcodec_send_frame(audioCodecCtx, audioFrame);
                while (avcodec_receive_packet(audioCodecCtx, packet) == 0) {
                    av_packet_rescale_ts(packet, audioCodecCtx->time_base, audioStream->time_base);
                    packet->stream_index = audioStream->index;
                    av_interleaved_write_frame(formatCtx, packet);
                    av_packet_unref(packet);
                }
                minFrames -= fs;
            }
        }
    }

public:
    EncoderEngine() = default;
    ~EncoderEngine() { Cleanup(); }

    bool Initialize(int width, int height, int fps, int crf, int sampleRate, int inChannels, int inBitsPerSample, bool isFloat, const char* outputFilename) {
        avformat_alloc_output_context2(&formatCtx, nullptr, "mp4", outputFilename);
        if (!formatCtx) return false;

        std::vector<std::string> encoderNames = { "h264_nvenc", "h264_amf", "h264_qsv", "libx264" };
        const AVCodec* vCodec = nullptr;
        for (const auto& name : encoderNames) {
            vCodec = avcodec_find_encoder_by_name(name.c_str());
            if (!vCodec) continue;

            videoCodecCtx = avcodec_alloc_context3(vCodec);
            videoCodecCtx->width = width;
            videoCodecCtx->height = height;
            videoCodecCtx->time_base = { 1, 1000 };
            videoCodecCtx->framerate = { fps, 1 };
            videoCodecCtx->pix_fmt = AV_PIX_FMT_NV12;

            if (name == "libx264") {
                av_opt_set(videoCodecCtx->priv_data, "preset", "veryfast", 0);
                av_opt_set(videoCodecCtx->priv_data, "crf", std::to_string(crf).c_str(), 0);
            }

            if (avcodec_open2(videoCodecCtx, vCodec, nullptr) >= 0) break;
            avcodec_free_context(&videoCodecCtx);
        }
        if (!videoCodecCtx) return false;

        videoStream = avformat_new_stream(formatCtx, vCodec);
        avcodec_parameters_from_context(videoStream->codecpar, videoCodecCtx);

        const AVCodec* aCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        audioCodecCtx = avcodec_alloc_context3(aCodec);
        audioCodecCtx->sample_rate = sampleRate;
        av_channel_layout_default(&audioCodecCtx->ch_layout, 2);
        audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        audioCodecCtx->bit_rate = 192000;
        audioCodecCtx->time_base = { 1, sampleRate };

        if (avcodec_open2(audioCodecCtx, aCodec, nullptr) < 0) return false;

        audioStream = avformat_new_stream(formatCtx, aCodec);
        avcodec_parameters_from_context(audioStream->codecpar, audioCodecCtx);

        AVSampleFormat inSampleFmt = AV_SAMPLE_FMT_S16;
        if (isFloat) inSampleFmt = AV_SAMPLE_FMT_FLT;
        else {
            if (inBitsPerSample == 32 || inBitsPerSample == 24) inSampleFmt = AV_SAMPLE_FMT_S32;
            else if (inBitsPerSample == 16) inSampleFmt = AV_SAMPLE_FMT_S16;
        }

        AVChannelLayout in_ch_layout;
        av_channel_layout_default(&in_ch_layout, inChannels);

        swr_alloc_set_opts2(&swrCtx, &audioCodecCtx->ch_layout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate,
            &in_ch_layout, inSampleFmt, sampleRate, 0, nullptr);
        swr_init(swrCtx);

        audioFifo = av_audio_fifo_alloc(audioCodecCtx->sample_fmt, audioCodecCtx->ch_layout.nb_channels, 1);

        if (!(formatCtx->oformat->flags & AVFMT_NOFILE)) avio_open(&formatCtx->pb, outputFilename, AVIO_FLAG_WRITE);
        avformat_write_header(formatCtx, nullptr);

        videoFrame = av_frame_alloc();
        videoFrame->format = videoCodecCtx->pix_fmt;
        videoFrame->width = videoCodecCtx->width;
        videoFrame->height = videoCodecCtx->height;
        av_frame_get_buffer(videoFrame, 0);

        audioFrame = av_frame_alloc();
        audioFrame->nb_samples = audioCodecCtx->frame_size;
        audioFrame->format = audioCodecCtx->sample_fmt;
        av_channel_layout_copy(&audioFrame->ch_layout, &audioCodecCtx->ch_layout);
        audioFrame->sample_rate = audioCodecCtx->sample_rate;
        av_frame_get_buffer(audioFrame, 0);

        swsCtx = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        packet = av_packet_alloc();

        return true;
    }

    bool InitializeSecondaryAudio(int sampleRate, int inChannels, int inBitsPerSample, bool isFloat) {
        AVSampleFormat inSampleFmt = AV_SAMPLE_FMT_S16;
        if (isFloat) inSampleFmt = AV_SAMPLE_FMT_FLT;
        else {
            if (inBitsPerSample == 32 || inBitsPerSample == 24) inSampleFmt = AV_SAMPLE_FMT_S32;
            else if (inBitsPerSample == 16) inSampleFmt = AV_SAMPLE_FMT_S16;
        }

        AVChannelLayout in_ch_layout;
        av_channel_layout_default(&in_ch_layout, inChannels);

        swr_alloc_set_opts2(&swrCtx2, &audioCodecCtx->ch_layout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate,
            &in_ch_layout, inSampleFmt, sampleRate, 0, nullptr);

        if (swr_init(swrCtx2) < 0) return false;

        secondaryFifo = av_audio_fifo_alloc(audioCodecCtx->sample_fmt, audioCodecCtx->ch_layout.nb_channels, 1);
        hasSecondaryAudio = true;
        return true;
    }

    void EncodeVideoFrame(uint8_t* bgraPixels, int rowPitch, int64_t msTimestamp) {
        if (bgraPixels) {
            const uint8_t* inData[1] = { bgraPixels };
            int inLinesize[1] = { rowPitch };
            sws_scale(swsCtx, inData, inLinesize, 0, videoCodecCtx->height, videoFrame->data, videoFrame->linesize);
        }

        videoFrame->pts = msTimestamp;
        avcodec_send_frame(videoCodecCtx, videoFrame);

        while (avcodec_receive_packet(videoCodecCtx, packet) == 0) {
            av_packet_rescale_ts(packet, videoCodecCtx->time_base, videoStream->time_base);
            packet->stream_index = videoStream->index;
            av_interleaved_write_frame(formatCtx, packet);
            av_packet_unref(packet);
        }
    }

    void PushAudioData(uint8_t* rawData, int numFrames) {
        if (numFrames <= 0 || !rawData) return;
        int outExpected = swr_get_out_samples(swrCtx, numFrames);
        uint8_t* converted[2] = { nullptr, nullptr };
        av_samples_alloc(converted, nullptr, audioCodecCtx->ch_layout.nb_channels, outExpected, audioCodecCtx->sample_fmt, 0);

        const uint8_t* inData[1] = { rawData };
        int realOut = swr_convert(swrCtx, converted, outExpected, inData, numFrames);

        if (realOut > 0) av_audio_fifo_write(audioFifo, (void**)converted, realOut);
        av_freep(&converted[0]);
        DrainAudioFifos();
    }

    void PushSecondaryAudioData(uint8_t* rawData, int numFrames) {
        if (numFrames <= 0 || !rawData || !hasSecondaryAudio) return;
        int outExpected = swr_get_out_samples(swrCtx2, numFrames);
        uint8_t* converted[2] = { nullptr, nullptr };
        av_samples_alloc(converted, nullptr, audioCodecCtx->ch_layout.nb_channels, outExpected, audioCodecCtx->sample_fmt, 0);

        const uint8_t* inData[1] = { rawData };
        int realOut = swr_convert(swrCtx2, converted, outExpected, inData, numFrames);

        if (realOut > 0) av_audio_fifo_write(secondaryFifo, (void**)converted, realOut);
        av_freep(&converted[0]);
        DrainAudioFifos();
    }

    void InjectSilence(int numFrames) {
        if (numFrames <= 0) return;
        int numChannels = audioCodecCtx->ch_layout.nb_channels;
        uint8_t* silenceData[2] = { nullptr, nullptr };

        av_samples_alloc(silenceData, nullptr, numChannels, numFrames, audioCodecCtx->sample_fmt, 0);
        av_samples_set_silence(silenceData, 0, numFrames, numChannels, audioCodecCtx->sample_fmt);

        av_audio_fifo_write(audioFifo, (void**)silenceData, numFrames);
        if (hasSecondaryAudio && secondaryFifo) {
            av_audio_fifo_write(secondaryFifo, (void**)silenceData, numFrames);
        }

        av_freep(&silenceData[0]);
        DrainAudioFifos();
    }

    int64_t GetCurrentFileSize() {
        if (formatCtx && formatCtx->pb) return avio_tell(formatCtx->pb);
        return 0;
    }

    void FlushEncoders() {
        avcodec_send_frame(videoCodecCtx, nullptr);
        while (avcodec_receive_packet(videoCodecCtx, packet) == 0) {
            av_packet_rescale_ts(packet, videoCodecCtx->time_base, videoStream->time_base);
            packet->stream_index = videoStream->index;
            av_interleaved_write_frame(formatCtx, packet);
            av_packet_unref(packet);
        }
        avcodec_send_frame(audioCodecCtx, nullptr);
        while (avcodec_receive_packet(audioCodecCtx, packet) == 0) {
            av_packet_rescale_ts(packet, audioCodecCtx->time_base, audioStream->time_base);
            packet->stream_index = audioStream->index;
            av_interleaved_write_frame(formatCtx, packet);
            av_packet_unref(packet);
        }
    }

    void Cleanup() {
        if (formatCtx) {
            FlushEncoders();
            av_write_trailer(formatCtx);
            if (!(formatCtx->oformat->flags & AVFMT_NOFILE) && formatCtx->pb) avio_closep(&formatCtx->pb);
            avformat_free_context(formatCtx); formatCtx = nullptr;
        }
        if (videoCodecCtx) { avcodec_free_context(&videoCodecCtx); videoCodecCtx = nullptr; }
        if (audioCodecCtx) { av_channel_layout_uninit(&audioCodecCtx->ch_layout); avcodec_free_context(&audioCodecCtx); audioCodecCtx = nullptr; }
        if (videoFrame) { av_frame_free(&videoFrame); videoFrame = nullptr; }
        if (audioFrame) { av_frame_free(&audioFrame); audioFrame = nullptr; }
        if (packet) { av_packet_free(&packet); packet = nullptr; }
        if (swsCtx) { sws_freeContext(swsCtx); swsCtx = nullptr; }
        if (swrCtx) { swr_free(&swrCtx); swrCtx = nullptr; }
        if (swrCtx2) { swr_free(&swrCtx2); swrCtx2 = nullptr; }
        if (audioFifo) { av_audio_fifo_free(audioFifo); audioFifo = nullptr; }
        if (secondaryFifo) { av_audio_fifo_free(secondaryFifo); secondaryFifo = nullptr; }
    }
};