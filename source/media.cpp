#include "media.hpp"
#include "diagnostics.hpp"
#include <opus.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}
#include <chrono>
#include <stdexcept>
#include <utility>

namespace uu {
namespace {
struct DecoderResources {
    AVCodecContext *context = nullptr;
    AVFrame *frame = nullptr, *cpu = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *scaler = nullptr;
    ~DecoderResources() {
        sws_freeContext(scaler);
        av_packet_free(&packet);
        av_frame_free(&cpu);
        av_frame_free(&frame);
        avcodec_free_context(&context);
    }
};
} // namespace
struct Media::Impl {
    std::mutex mutex, audioMutex;
    std::condition_variable cv;
    std::deque<Bytes> queue;
    std::shared_ptr<const Picture> latest;
    bool stop = false, clear = false, recovering = false;
    std::chrono::steady_clock::time_point nextKeyframeRequest{};
    std::thread thread;
    SDL_AudioDeviceID audioDevice = 0;
    OpusDecoder *opus = nullptr;
    std::string error;
    uint64_t serial = 0;
    std::atomic<uint64_t> generation{1};
    std::atomic<bool> decoderFailed{false};
    enum class AudioStatus { Ready, Unavailable, QueueFailed };
    std::atomic<AudioStatus> audioStatus{AudioStatus::Ready};
    explicit Impl(VideoDecoder videoDecoder) {
        try {
            SDL_AudioSpec spec{};
            spec.freq = 48000;
            spec.format = AUDIO_S16SYS;
            spec.channels = 2;
            spec.samples = 960;
            audioDevice = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
            if (!audioDevice)
                audioStatus = AudioStatus::Unavailable;
            int code = 0;
            opus = opus_decoder_create(48000, 2, &code);
            if (code != OPUS_OK)
                throw std::runtime_error("Opus decoder initialization failed");
            if (audioDevice)
                SDL_PauseAudioDevice(audioDevice, 0);
            thread = std::thread([this, videoDecoder] {
                try {
                    decode(videoDecoder);
                } catch (...) {
                    decoderFailed = true;
                    diagnosticException("video-decode-exception", std::current_exception());
                }
            });
        } catch (...) {
            if (audioDevice)
                SDL_CloseAudioDevice(audioDevice);
            opus_decoder_destroy(opus);
            throw;
        }
    }
    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stop = true;
        }
        cv.notify_one();
        thread.join();
        if (audioDevice)
            SDL_CloseAudioDevice(audioDevice);
        opus_decoder_destroy(opus);
    }
#ifdef __SWITCH__
    static AVPixelFormat chooseFormat(AVCodecContext *context, const AVPixelFormat *formats) {
        for (auto p = formats; *p != AV_PIX_FMT_NONE; ++p)
            if (*p == AV_PIX_FMT_NVTEGRA)
                return *p;
        return avcodec_default_get_format(context, formats);
    }
#endif
    void discardVideo() {
        queue.clear();
        clear = true;
        recovering = true;
    }
    bool takeKeyframeRequest() {
        if (!recovering || decoderFailed.load())
            return false;
        const auto now = std::chrono::steady_clock::now();
        if (now < nextKeyframeRequest)
            return false;
        nextKeyframeRequest = now + std::chrono::seconds(1);
        return true;
    }
    void decoderLoss(uint64_t currentGeneration) {
        {
            std::lock_guard lock(mutex);
            if (clear || recovering || currentGeneration != generation.load())
                return;
            discardVideo();
        }
        diagnostic("video-decoder-bad-frame");
    }
    void decode(VideoDecoder videoDecoder) {
        const auto *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        DecoderResources resources;
        auto *ctx = resources.context = avcodec_alloc_context3(codec);
        auto *frame = resources.frame = av_frame_alloc();
        auto *cpu = resources.cpu = av_frame_alloc();
        auto *packet = resources.packet = av_packet_alloc();
        auto *&sws = resources.scaler;
        if (!ctx || !frame || !cpu || !packet) {
            std::lock_guard lock(mutex);
            error = "视频解码器内存不足";
            decoderFailed = true;
            return;
        }
        ctx->thread_count = 2;
        ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
#ifdef __SWITCH__
        if (videoDecoder == VideoDecoder::Automatic) {
            if (av_hwdevice_ctx_create(&ctx->hw_device_ctx, AV_HWDEVICE_TYPE_NVTEGRA, nullptr, nullptr, 0) >= 0) {
                ctx->get_format = chooseFormat;
            } else {
                std::lock_guard lock(mutex);
                error = "NVDEC 不可用，使用软件解码";
            }
        }
#else
        (void)videoDecoder;
#endif
        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            std::lock_guard lock(mutex);
            error = "无法初始化 H.264 解码器";
            decoderFailed = true;
            return;
        }
        while (true) {
            Bytes data;
            uint64_t currentGeneration;
            bool flush, needsKeyframe;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return stop || clear || !queue.empty(); });
                if (stop)
                    break;
                flush = std::exchange(clear, false);
                if (!queue.empty()) {
                    data = std::move(queue.front());
                    queue.pop_front();
                }
                currentGeneration = generation.load();
                needsKeyframe = recovering;
            }
            if (flush)
                avcodec_flush_buffers(ctx);
            if (data.empty())
                continue;
            av_packet_unref(packet);
            if (av_new_packet(packet, int(data.size())) < 0) {
                decoderLoss(currentGeneration);
                continue;
            }
            std::copy(data.begin(), data.end(), packet->data);
            auto drain = [&] {
                int result;
                while ((result = avcodec_receive_frame(ctx, frame)) == 0) {
                    const bool keyframe = (frame->flags & AV_FRAME_FLAG_KEY) != 0;
                    if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags > 0) {
                        av_frame_unref(frame);
                        return false;
                    }
                    if (needsKeyframe && !keyframe) {
                        av_frame_unref(frame);
                        continue;
                    }
                    AVFrame *source = frame;
#ifdef __SWITCH__
                    if (frame->format == AV_PIX_FMT_NVTEGRA) {
                        av_frame_unref(cpu);
                        if (av_hwframe_transfer_data(cpu, frame, 0) < 0) {
                            av_frame_unref(frame);
                            continue;
                        }
                        source = cpu;
                    }
#endif
                    const int width = source->width, height = source->height;
                    if (width <= 0 || height <= 0 || width > 4096 || height > 2160) {
                        av_frame_unref(frame);
                        continue;
                    }
                    sws = sws_getCachedContext(sws, width, height, AVPixelFormat(source->format), width,
                                               height, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr,
                                               nullptr);
                    if (sws) {
                        auto picture = std::make_shared<Picture>();
                        picture->width = width;
                        picture->height = height;
                        picture->pixels.resize(size_t(width) * height * 4);
                        uint8_t *dst[4] = {picture->pixels.data()};
                        int stride[4] = {width * 4};
                        if (sws_scale(sws, source->data, source->linesize, 0, height, dst, stride) == height) {
                            std::lock_guard lock(mutex);
                            // A discard may invalidate an in-flight frame during conversion.
                            if (!clear && currentGeneration == generation.load()) {
                                picture->serial = ++serial;
                                latest = std::move(picture);
                                needsKeyframe = false;
                                recovering = false;
                            }
                        }
                    }
                    av_frame_unref(frame);
                }
                return result == AVERROR(EAGAIN);
            };
            int result = avcodec_send_packet(ctx, packet);
            if (result == AVERROR(EAGAIN) && drain())
                result = avcodec_send_packet(ctx, packet);
            if (result < 0 || !drain())
                decoderLoss(currentGeneration);
        }
    }
};
Media::Media(VideoDecoder videoDecoder) : impl_(std::make_unique<Impl>(videoDecoder)) {}
Media::~Media() = default;
bool Media::video(const uint8_t *data, size_t size, uint64_t generation) {
    if (impl_->decoderFailed.load())
        return false;
    const bool dropped = size == 0 || size > 2 * 1024 * 1024;
    bool requestKeyframe, startedRecovery;
    {
        std::lock_guard lock(impl_->mutex);
        if (generation != impl_->generation.load())
            return false;
        startedRecovery = dropped && !impl_->recovering;
        if (dropped || impl_->queue.size() >= 8)
            impl_->discardVideo();
        if (!dropped)
            impl_->queue.emplace_back(data, data + size);
        requestKeyframe = impl_->takeKeyframeRequest();
    }
    impl_->cv.notify_one();
    if (startedRecovery)
        diagnostic("video-rtp-frame-dropped");
    return requestKeyframe;
}
bool Media::takeVideoKeyframeRequest() {
    std::lock_guard lock(impl_->mutex);
    return impl_->takeKeyframeRequest();
}
void Media::audio(const uint8_t *data, size_t size, uint64_t generation) {
    if (!size || size > 4096)
        return;
    std::lock_guard lock(impl_->audioMutex);
    if (generation != impl_->generation.load())
        return;
    std::array<int16_t, 5760 * 2> pcm{};
    int samples = opus_decode(impl_->opus, data, int(size), pcm.data(), 5760, 0);
    if (samples > 0 && impl_->audioDevice) {
        if (SDL_GetQueuedAudioSize(impl_->audioDevice) > 48000 * 4 / 5)
            SDL_ClearQueuedAudio(impl_->audioDevice);
        impl_->audioStatus = SDL_QueueAudio(impl_->audioDevice, pcm.data(), samples * 4) == 0
                                 ? Impl::AudioStatus::Ready
                                 : Impl::AudioStatus::QueueFailed;
    }
}
std::shared_ptr<const Picture> Media::picture(uint64_t after) {
    std::lock_guard lock(impl_->mutex);
    return impl_->latest && impl_->latest->serial > after ? impl_->latest : nullptr;
}
void Media::reset() {
    {
        std::lock_guard lock(impl_->mutex);
        // The decoder reads the generation while removing queued frames under
        // this lock. Advancing it earlier could relabel an old queued frame.
        impl_->generation.fetch_add(1);
        impl_->queue.clear();
        impl_->latest.reset();
        impl_->clear = true;
        impl_->recovering = false;
        impl_->nextKeyframeRequest = {};
    }
    impl_->cv.notify_one();
    std::lock_guard lock(impl_->audioMutex);
    if (impl_->audioDevice) {
        SDL_ClearQueuedAudio(impl_->audioDevice);
        impl_->audioStatus = Impl::AudioStatus::Ready;
    }
    opus_decoder_ctl(impl_->opus, OPUS_RESET_STATE);
}
uint64_t Media::generation() const {
    return impl_->generation.load();
}
std::string Media::status() {
    std::lock_guard lock(impl_->mutex);
    auto status = impl_->error;
    if (impl_->decoderFailed.load())
        status = "视频解码已停止，请退出程序后重试";
    else if (impl_->recovering)
        status = "正在恢复视频，等待关键帧";
    const auto audio = impl_->audioStatus.load();
    if (audio != Impl::AudioStatus::Ready) {
        if (!status.empty())
            status += " · ";
        status += audio == Impl::AudioStatus::Unavailable ? "音频设备不可用，请退出程序后重试"
                                                          : "音频播放失败，正在重试";
    }
    return status;
}
} // namespace uu
