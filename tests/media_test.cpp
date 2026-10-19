#include "media.hpp"
#include <fstream>
#include <iostream>
#include <opus.h>
#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#ifdef UU_MEDIA_FAULT_TEST
std::atomic<int> liveFrames{0}, liveContexts{0}, livePackets{0};
std::atomic<bool> failFrame{false}, failScaler{false}, failOpus{false};
std::atomic<bool> failAudioOpen{false}, failAudioQueue{false};
std::atomic<unsigned> queuedBytes{0};
extern "C" SDL_AudioDeviceID __real_SDL_OpenAudioDevice(const char *, int, const SDL_AudioSpec *,
                                                        SDL_AudioSpec *, int);
extern "C" int __real_SDL_QueueAudio(SDL_AudioDeviceID, const void *, Uint32);
extern "C" SDL_AudioDeviceID __wrap_SDL_OpenAudioDevice(const char *name, int capture,
                                                        const SDL_AudioSpec *desired, SDL_AudioSpec *obtained,
                                                        int changes) {
    if (failAudioOpen.exchange(false))
        return 0;
    return __real_SDL_OpenAudioDevice(name, capture, desired, obtained, changes);
}
extern "C" int __wrap_SDL_QueueAudio(SDL_AudioDeviceID device, const void *data, Uint32 size) {
    if (failAudioQueue.exchange(false))
        return -1;
    const auto result = __real_SDL_QueueAudio(device, data, size);
    if (result == 0)
        queuedBytes += size;
    return result;
}
extern "C" AVFrame *__real_av_frame_alloc();
extern "C" void __real_av_frame_free(AVFrame **);
extern "C" AVCodecContext *__real_avcodec_alloc_context3(const AVCodec *);
extern "C" void __real_avcodec_free_context(AVCodecContext **);
extern "C" AVPacket *__real_av_packet_alloc();
extern "C" void __real_av_packet_free(AVPacket **);
extern "C" SwsContext *__real_sws_getCachedContext(SwsContext *, int, int, AVPixelFormat, int, int,
                                                   AVPixelFormat, int, SwsFilter *, SwsFilter *,
                                                   const double *);
extern "C" OpusDecoder *__real_opus_decoder_create(opus_int32, int, int *);
extern "C" AVFrame *__wrap_av_frame_alloc() {
    if (failFrame.exchange(false))
        return nullptr;
    auto *value = __real_av_frame_alloc();
    if (value)
        ++liveFrames;
    return value;
}
extern "C" void __wrap_av_frame_free(AVFrame **value) {
    if (*value)
        --liveFrames;
    __real_av_frame_free(value);
}
extern "C" AVCodecContext *__wrap_avcodec_alloc_context3(const AVCodec *codec) {
    auto *value = __real_avcodec_alloc_context3(codec);
    if (value)
        ++liveContexts;
    return value;
}
extern "C" void __wrap_avcodec_free_context(AVCodecContext **value) {
    if (*value)
        --liveContexts;
    __real_avcodec_free_context(value);
}
extern "C" AVPacket *__wrap_av_packet_alloc() {
    auto *value = __real_av_packet_alloc();
    if (value)
        ++livePackets;
    return value;
}
extern "C" void __wrap_av_packet_free(AVPacket **value) {
    if (*value)
        --livePackets;
    __real_av_packet_free(value);
}
extern "C" SwsContext *__wrap_sws_getCachedContext(SwsContext *context, int sw, int sh, AVPixelFormat sf,
                                                   int dw, int dh, AVPixelFormat df, int flags,
                                                   SwsFilter *src, SwsFilter *dst, const double *params) {
    if (failScaler.exchange(false))
        throw std::bad_alloc();
    return __real_sws_getCachedContext(context, sw, sh, sf, dw, dh, df, flags, src, dst, params);
}
extern "C" OpusDecoder *__wrap_opus_decoder_create(opus_int32 rate, int channels, int *error) {
    if (failOpus.exchange(false)) {
        *error = OPUS_ALLOC_FAIL;
        return nullptr;
    }
    return __real_opus_decoder_create(rate, channels, error);
}
#endif
void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
template <typename Condition> void waitFor(Condition condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(condition(), "Media worker timed out");
}
void feed(uu::Media &media, const uu::Bytes &frame, uint64_t generation) {
    for (int i = 0; i < 4; ++i)
        media.video(frame.data(), frame.size(), generation);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "Missing fixture");
        SDL_SetMainReady();
        check(SDL_Init(SDL_INIT_AUDIO) == 0, SDL_GetError());
        std::ifstream input(argv[1], std::ios::binary);
        uu::Bytes frame((std::istreambuf_iterator<char>(input)), {});
        check(!frame.empty(), "Empty H.264 fixture");
        {
            uu::Media media;
            auto generation = media.generation();
            feed(media, frame, generation);
            waitFor([&] { return media.picture(0).serial != 0; });
            auto picture = media.picture(0);
            check(picture.width == 16 && picture.height == 16 && picture.pixels[0] > 200 &&
                      picture.pixels[1] < 30 && picture.pixels[2] < 30,
                  "H.264 to RGBA output mismatch");
            media.reset();
            feed(media, frame, generation);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            check(media.picture(0).serial == 0, "Old generation published a picture");
            feed(media, frame, media.generation());
            waitFor([&] { return media.picture(picture.serial).serial > picture.serial; });
            const uint8_t silence[] = {0xf8, 0xff, 0xfe};
            media.audio(silence, sizeof(silence), media.generation());
#ifdef UU_MEDIA_FAULT_TEST
            check(queuedBytes == 960 * 2 * sizeof(int16_t), "Opus PCM did not reach SDL audio queue");
            const auto bytes = queuedBytes.load();
            media.audio(silence, sizeof(silence), generation);
            check(queuedBytes == bytes, "Old connection audio reached playback");
            failAudioQueue = true;
            media.audio(silence, sizeof(silence), media.generation());
            check(!failAudioQueue && media.status().find("音频播放失败") != std::string::npos,
                  "Audio queue failure was hidden");
            media.audio(silence, sizeof(silence), media.generation());
            check(media.status().empty(), "Successful audio retry retained stale error");
#endif
        }
#ifdef UU_MEDIA_FAULT_TEST
        failAudioOpen = true;
        {
            uu::Media media;
            check(!failAudioOpen && media.status().find("音频设备不可用") != std::string::npos,
                  "Audio device failure was hidden");
            feed(media, frame, media.generation());
            waitFor([&] { return media.picture(0).serial != 0; });
        }
        failFrame = true;
        {
            uu::Media media;
            waitFor([&] { return !media.status().empty(); });
            check(!failFrame, "Frame failure was not injected");
        }
        check(liveFrames == 0 && liveContexts == 0 && livePackets == 0,
              "Initialization leaked decoder resources");
        failScaler = true;
        {
            uu::Media media;
            feed(media, frame, media.generation());
            waitFor([&] { return !media.status().empty(); });
            check(!failScaler, "Worker exception was not injected");
        }
        check(liveFrames == 0 && liveContexts == 0 && livePackets == 0,
              "Worker exception leaked decoder resources");
        failOpus = true;
        bool rejected = false;
        try {
            uu::Media media;
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        check(rejected && !failOpus, "Opus failure did not propagate");
        // The dummy audio driver allows one playback device; a leaked device
        // from the failed constructor prevents this subsequent open.
        SDL_AudioSpec spec{};
        spec.freq = 48000;
        spec.format = AUDIO_S16SYS;
        spec.channels = 2;
        spec.samples = 960;
        const auto device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
        check(device != 0, "Failed media constructor leaked audio device");
        SDL_CloseAudioDevice(device);
#endif
        SDL_Quit();
        std::cout << "Real H.264 decode, generation reset and failure cleanup passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
