#include "media.hpp"
#include <opus.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}
#include <stdexcept>

namespace uu {
struct Media::Impl {
    std::mutex mutex, audioMutex;
    std::condition_variable cv;
    std::deque<Bytes> queue;
    Picture latest;
    bool stop = false, clear = false;
    std::thread thread;
    SDL_AudioDeviceID audioDevice = 0;
    OpusDecoder *opus = nullptr;
    std::string error;
    uint64_t serial = 0;
    std::atomic<uint64_t> generation{1};
    Impl() {
        SDL_AudioSpec spec{};
        spec.freq = 48000;
        spec.format = AUDIO_S16SYS;
        spec.channels = 2;
        spec.samples = 960;
        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
        int code = 0;
        opus = opus_decoder_create(48000, 2, &code);
        if (code != OPUS_OK)
            throw std::runtime_error("Opus decoder initialization failed");
        if (audioDevice)
            SDL_PauseAudioDevice(audioDevice, 0);
        thread = std::thread([this] { decode(); });
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
    static AVPixelFormat chooseFormat(AVCodecContext *context, const AVPixelFormat *formats) {
#ifdef __SWITCH__
        for (auto p = formats; *p != AV_PIX_FMT_NONE; ++p)
            if (context->hw_device_ctx && *p == AV_PIX_FMT_NVTEGRA)
                return *p;
#else
        (void)context;
#endif
        for (auto p = formats; *p != AV_PIX_FMT_NONE; ++p)
            if (*p == AV_PIX_FMT_YUV420P || *p == AV_PIX_FMT_NV12)
                return *p;
        return formats[0];
    }
    void decode() {
        const auto *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        auto *ctx = avcodec_alloc_context3(codec);
        auto *frame = av_frame_alloc();
        auto *cpu = av_frame_alloc();
        auto *packet = av_packet_alloc();
        SwsContext *sws = nullptr;
        if (!ctx || !frame || !cpu || !packet) {
            std::lock_guard lock(mutex);
            error = "视频解码器内存不足";
            goto done;
        }
        ctx->thread_count = 2;
        ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
#ifdef __SWITCH__
        ctx->get_format = chooseFormat;
        if (av_hwdevice_ctx_create(&ctx->hw_device_ctx, AV_HWDEVICE_TYPE_NVTEGRA, nullptr, nullptr, 0) < 0) {
            std::lock_guard lock(mutex);
            error = "NVDEC 不可用，使用软件解码";
        }
#endif
        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            std::lock_guard lock(mutex);
            error = "无法初始化 H.264 解码器";
            goto done;
        }
        while (true) {
            Bytes data;
            uint64_t currentGeneration;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return stop || clear || !queue.empty(); });
                if (stop)
                    break;
                if (clear) {
                    avcodec_flush_buffers(ctx);
                    clear = false;
                }
                if (queue.empty())
                    continue;
                data = std::move(queue.front());
                queue.pop_front();
                currentGeneration = generation.load();
            }
            av_packet_unref(packet);
            if (av_new_packet(packet, int(data.size())) < 0)
                continue;
            std::copy(data.begin(), data.end(), packet->data);
            auto drain = [&] {
                while (avcodec_receive_frame(ctx, frame) == 0) {
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
                    if (source->width <= 0 || source->height <= 0 || source->width > 4096 ||
                        source->height > 2160) {
                        av_frame_unref(frame);
                        continue;
                    }
                    Picture p;
                    p.width = source->width;
                    p.height = source->height;
                    p.pixels.resize(size_t(p.width) * p.height * 4);
                    sws = sws_getCachedContext(sws, p.width, p.height, AVPixelFormat(source->format), p.width,
                                               p.height, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr,
                                               nullptr);
                    if (sws) {
                        uint8_t *dst[] = {p.pixels.data()};
                        int stride[] = {p.width * 4};
                        sws_scale(sws, source->data, source->linesize, 0, p.height, dst, stride);
                        std::lock_guard lock(mutex);
                        if (currentGeneration == generation.load()) {
                            p.serial = ++serial;
                            latest = std::move(p);
                        }
                    }
                    av_frame_unref(frame);
                }
            };
            int result = avcodec_send_packet(ctx, packet);
            if (result == AVERROR(EAGAIN)) {
                drain();
                result = avcodec_send_packet(ctx, packet);
            }
            if (result >= 0)
                drain();
        }
    done:
        sws_freeContext(sws);
        av_packet_free(&packet);
        av_frame_free(&cpu);
        av_frame_free(&frame);
        avcodec_free_context(&ctx);
    }
};
Media::Media() : impl_(std::make_unique<Impl>()) {}
Media::~Media() = default;
void Media::video(const uint8_t *data, size_t size, uint64_t generation) {
    if (!size || size > 2 * 1024 * 1024)
        return;
    {
        std::lock_guard lock(impl_->mutex);
        if (generation != impl_->generation.load())
            return;
        if (impl_->queue.size() >= 8) {
            impl_->queue.clear();
            impl_->clear = true;
        }
        impl_->queue.emplace_back(data, data + size);
    }
    impl_->cv.notify_one();
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
        SDL_QueueAudio(impl_->audioDevice, pcm.data(), samples * 4);
    }
}
Picture Media::picture(uint64_t after) {
    std::lock_guard lock(impl_->mutex);
    return impl_->latest.serial > after ? impl_->latest : Picture{};
}
void Media::reset() {
    impl_->generation.fetch_add(1);
    {
        std::lock_guard lock(impl_->mutex);
        impl_->queue.clear();
        impl_->latest = {};
        impl_->clear = true;
    }
    impl_->cv.notify_one();
    std::lock_guard lock(impl_->audioMutex);
    if (impl_->audioDevice)
        SDL_ClearQueuedAudio(impl_->audioDevice);
    opus_decoder_ctl(impl_->opus, OPUS_RESET_STATE);
}
uint64_t Media::generation() const {
    return impl_->generation.load();
}
std::string Media::status() {
    std::lock_guard lock(impl_->mutex);
    return impl_->error;
}
} // namespace uu
