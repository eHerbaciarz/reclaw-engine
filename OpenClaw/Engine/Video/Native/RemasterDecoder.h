#pragma once

// Decoders for the remastered movies in <ReClaw folder>/movies/:
//   movie_XX.mp4  H.264 + AAC (FFmpeg; only where the build has it,
//                 OPENCLAW_HAVE_FFMPEG)
//   movie_XX.mpg  MPEG-1 video + MP2 audio (pl_mpeg; every platform)
// Format guidelines: docs/FILMY.md.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace RemasterMovies
{
    // One decoded picture, YUV 4:2:0 planes (valid until the next call).
    struct Frame
    {
        int width = 0, height = 0;
        const uint8_t* y = nullptr;
        const uint8_t* u = nullptr;
        const uint8_t* v = nullptr;
        int yPitch = 0, uPitch = 0, vPitch = 0;
        double time = 0.0;          // seconds from the start of the movie
    };

    // Decoded sound: interleaved float samples, `frames` per channel.
    typedef std::function<void(const float* samples, int frames)> AudioSink;

    class Decoder
    {
    public:
        virtual ~Decoder() {}
        virtual bool Open(const std::string& path) = 0;
        // 0 when the movie has no sound.
        virtual int AudioRate() const = 0;
        virtual int AudioChannels() const = 0;
        // Decodes up to the next picture; the sound decoded on the way (a little
        // ahead of the picture) goes to `audio`. False at the end of the movie.
        virtual bool NextFrame(Frame& out, const AudioSink& audio) = 0;
    };

    std::unique_ptr<Decoder> CreateMpeg1Decoder();
    // nullptr when the build has no FFmpeg.
    std::unique_ptr<Decoder> CreateFfmpegDecoder();
    bool HasFfmpeg();
}
