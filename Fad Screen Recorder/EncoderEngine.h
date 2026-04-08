#pragma once
#include <iostream>
#include <string>
#include <vector>

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
    SwrContext* swrCtx = nullptr;
    AVAudioFifo* audioFifo = nullptr;
    AVFrame* audioFrame = nullptr;
    int64_t audioFrameCounter = 0;

    AVPacket* packet = nullptr;

    void DrainAudioFifo() {
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

public:
    EncoderEngine() = default;
    ~EncoderEngine() { Cleanup(); }

    // CRITICAL FIX: Added 'crf' to control video quality dynamically
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
                // Apply the dynamically passed Video Quality setting
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
        if (isFloat) {
            inSampleFmt = AV_SAMPLE_FMT_FLT;
        }
        else {
            if (inBitsPerSample == 32) inSampleFmt = AV_SAMPLE_FMT_S32;
            else if (inBitsPerSample == 24) inSampleFmt = AV_SAMPLE_FMT_S32;
            else if (inBitsPerSample == 16) inSampleFmt = AV_SAMPLE_FMT_S16;
        }

        AVChannelLayout in_ch_layout;
        av_channel_layout_default(&in_ch_layout, inChannels);

        swr_alloc_set_opts2(&swrCtx,
            &audioCodecCtx->ch_layout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate,
            &in_ch_layout, inSampleFmt, sampleRate,
            0, nullptr);
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

        int numChannels = audioCodecCtx->ch_layout.nb_channels;
        int outExpected = swr_get_out_samples(swrCtx, numFrames);

        uint8_t* converted[2] = { nullptr, nullptr };
        av_samples_alloc(converted, nullptr, numChannels, outExpected, audioCodecCtx->sample_fmt, 0);

        const uint8_t* inData[1] = { rawData };
        int realOut = swr_convert(swrCtx, converted, outExpected, inData, numFrames);

        if (realOut > 0) av_audio_fifo_write(audioFifo, (void**)converted, realOut);
        av_freep(&converted[0]);

        DrainAudioFifo();
    }

    void InjectSilence(int numFrames) {
        if (numFrames <= 0) return;

        int numChannels = audioCodecCtx->ch_layout.nb_channels;
        uint8_t* silenceData[2] = { nullptr, nullptr };

        av_samples_alloc(silenceData, nullptr, numChannels, numFrames, audioCodecCtx->sample_fmt, 0);
        av_samples_set_silence(silenceData, 0, numFrames, numChannels, audioCodecCtx->sample_fmt);

        av_audio_fifo_write(audioFifo, (void**)silenceData, numFrames);
        av_freep(&silenceData[0]);

        DrainAudioFifo();
    }

    int64_t GetCurrentFileSize() {
        if (formatCtx && formatCtx->pb) {
            return avio_tell(formatCtx->pb);
        }
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
        if (audioFifo) { av_audio_fifo_free(audioFifo); audioFifo = nullptr; }
    }
};