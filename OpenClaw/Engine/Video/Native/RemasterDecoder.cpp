#include "RemasterDecoder.h"

#ifndef __EMSCRIPTEN__

#include <algorithm>
#include <cstring>
#include <vector>

#include "../../Logger/Logger.h"

#define PL_MPEG_IMPLEMENTATION
#include "../../../../ThirdParty/pl_mpeg/pl_mpeg.h"

#if defined(OPENCLAW_HAVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}
#endif

namespace RemasterMovies
{
    //------------------------------------------------------------ MPEG-1 / MP2
    namespace
    {
        class Mpeg1Decoder : public Decoder
        {
        public:
            ~Mpeg1Decoder() override { if (m_Plm) plm_destroy(m_Plm); }

            bool Open(const std::string& path) override
            {
                m_Plm = plm_create_with_filename(path.c_str());
                if (!m_Plm) return false;
                if (!plm_probe(m_Plm, 5000 * 1024) || plm_get_num_video_streams(m_Plm) < 1)
                {
                    LOG_WARNING("Not an MPEG-1 movie: " + path);
                    return false;
                }
                m_HasAudio = plm_get_num_audio_streams(m_Plm) > 0;
                plm_set_audio_enabled(m_Plm, m_HasAudio);
                plm_set_loop(m_Plm, 0);
                return true;
            }

            int AudioRate() const override { return m_HasAudio ? plm_get_samplerate(m_Plm) : 0; }
            int AudioChannels() const override { return m_HasAudio ? 2 : 0; }

            bool NextFrame(Frame& out, const AudioSink& audio) override
            {
                plm_frame_t* f = plm_decode_video(m_Plm);
                if (!f) return false;
                // Sound up to a little past this picture.
                while (m_HasAudio && !m_AudioEnded && m_AudioTime < f->time + 0.25)
                {
                    plm_samples_t* s = plm_decode_audio(m_Plm);
                    if (!s) { m_AudioEnded = true; break; }
                    m_AudioTime = s->time;
                    if (audio) audio(s->interleaved, (int)s->count);
                }
                out.width = (int)f->width;
                out.height = (int)f->height;
                out.y = f->y.data;  out.yPitch = (int)f->y.width;
                out.u = f->cb.data; out.uPitch = (int)f->cb.width;
                out.v = f->cr.data; out.vPitch = (int)f->cr.width;
                out.time = f->time;
                return true;
            }

        private:
            plm_t* m_Plm = nullptr;
            bool m_HasAudio = false;
            bool m_AudioEnded = false;
            double m_AudioTime = -1.0;
        };
    }

    std::unique_ptr<Decoder> CreateMpeg1Decoder() { return std::unique_ptr<Decoder>(new Mpeg1Decoder()); }

    //------------------------------------------------------------ H.264 / AAC
#if defined(OPENCLAW_HAVE_FFMPEG)
    namespace
    {
        class FfmpegDecoder : public Decoder
        {
        public:
            ~FfmpegDecoder() override
            {
                av_frame_free(&m_Frame);
                av_frame_free(&m_AudioFrame);
                av_packet_free(&m_Packet);
                avcodec_free_context(&m_Video);
                avcodec_free_context(&m_Audio);
                if (m_Format) avformat_close_input(&m_Format);
            }

            bool Open(const std::string& path) override
            {
                if (avformat_open_input(&m_Format, path.c_str(), nullptr, nullptr) < 0) return false;
                if (avformat_find_stream_info(m_Format, nullptr) < 0) return false;
                m_VideoIndex = av_find_best_stream(m_Format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
                if (m_VideoIndex < 0 || !OpenCodec(m_VideoIndex, &m_Video)) return false;
                if (m_Video->pix_fmt != AV_PIX_FMT_YUV420P && m_Video->pix_fmt != AV_PIX_FMT_YUVJ420P)
                {
                    LOG_WARNING("Movie " + path + ": the picture must be yuv420p (8 bit 4:2:0)");
                    return false;
                }
                m_AudioIndex = av_find_best_stream(m_Format, AVMEDIA_TYPE_AUDIO, -1, m_VideoIndex, nullptr, 0);
                if (m_AudioIndex >= 0 && !OpenCodec(m_AudioIndex, &m_Audio)) m_AudioIndex = -1;
                m_Frame = av_frame_alloc();
                m_AudioFrame = av_frame_alloc();
                m_Packet = av_packet_alloc();
                m_TimeBase = av_q2d(m_Format->streams[m_VideoIndex]->time_base);
                return m_Frame && m_AudioFrame && m_Packet;
            }

            int AudioRate() const override { return m_Audio ? m_Audio->sample_rate : 0; }
            int AudioChannels() const override
            {
                if (!m_Audio) return 0;
#if LIBAVCODEC_VERSION_MAJOR >= 60
                return std::min(2, m_Audio->ch_layout.nb_channels);
#else
                return std::min(2, m_Audio->channels);
#endif
            }

            bool NextFrame(Frame& out, const AudioSink& audio) override
            {
                for (;;)
                {
                    int r = avcodec_receive_frame(m_Video, m_Frame);
                    if (r == 0)
                    {
                        out.width = m_Frame->width;
                        out.height = m_Frame->height;
                        out.y = m_Frame->data[0]; out.yPitch = m_Frame->linesize[0];
                        out.u = m_Frame->data[1]; out.uPitch = m_Frame->linesize[1];
                        out.v = m_Frame->data[2]; out.vPitch = m_Frame->linesize[2];
                        int64_t pts = m_Frame->best_effort_timestamp;
                        out.time = pts == AV_NOPTS_VALUE ? m_LastTime : pts * m_TimeBase;
                        if (m_StartTime < 0) m_StartTime = out.time;
                        out.time -= m_StartTime;
                        m_LastTime = out.time;
                        return true;
                    }
                    if (r == AVERROR_EOF) return false;
                    if (r != AVERROR(EAGAIN)) return false;

                    // The decoder needs more data.
                    if (m_Ended) return false;
                    if (av_read_frame(m_Format, m_Packet) < 0)
                    {
                        m_Ended = true;
                        avcodec_send_packet(m_Video, nullptr);   // flush the last pictures
                        continue;
                    }
                    if (m_Packet->stream_index == m_VideoIndex)
                    {
                        avcodec_send_packet(m_Video, m_Packet);
                    }
                    else if (m_Packet->stream_index == m_AudioIndex)
                    {
                        DecodeAudio(audio);
                    }
                    av_packet_unref(m_Packet);
                }
            }

        private:
            bool OpenCodec(int index, AVCodecContext** ctx)
            {
                AVCodecParameters* par = m_Format->streams[index]->codecpar;
                const AVCodec* codec = avcodec_find_decoder(par->codec_id);
                if (!codec) return false;
                *ctx = avcodec_alloc_context3(codec);
                if (!*ctx || avcodec_parameters_to_context(*ctx, par) < 0) return false;
                (*ctx)->thread_count = 0;   // as many threads as the CPU has
                return avcodec_open2(*ctx, codec, nullptr) == 0;
            }

            void DecodeAudio(const AudioSink& audio)
            {
                if (avcodec_send_packet(m_Audio, m_Packet) < 0) return;
                while (avcodec_receive_frame(m_Audio, m_AudioFrame) == 0)
                {
                    int n = m_AudioFrame->nb_samples, ch = AudioChannels();
                    if (n <= 0 || ch <= 0) continue;
                    m_Mix.resize((size_t)n * ch);
                    AVSampleFormat fmt = (AVSampleFormat)m_AudioFrame->format;
                    bool planar = av_sample_fmt_is_planar(fmt);
                    const int frameChannels = AudioChannelsInFrame();
                    for (int c = 0; c < ch; ++c)
                    {
                        for (int i = 0; i < n; ++i)
                        {
                            float v = 0.0f;
                            const uint8_t* const* d = m_AudioFrame->extended_data;
                            size_t idx = planar ? (size_t)i : (size_t)i * frameChannels + c;
                            const uint8_t* p = planar ? d[c] : d[0];
                            switch (fmt)
                            {
                            case AV_SAMPLE_FMT_FLT: case AV_SAMPLE_FMT_FLTP: v = ((const float*)p)[idx]; break;
                            case AV_SAMPLE_FMT_S16: case AV_SAMPLE_FMT_S16P: v = ((const int16_t*)p)[idx] / 32768.0f; break;
                            case AV_SAMPLE_FMT_S32: case AV_SAMPLE_FMT_S32P: v = ((const int32_t*)p)[idx] / 2147483648.0f; break;
                            case AV_SAMPLE_FMT_DBL: case AV_SAMPLE_FMT_DBLP: v = (float)((const double*)p)[idx]; break;
                            default: break;
                            }
                            m_Mix[(size_t)i * ch + c] = v;
                        }
                    }
                    if (audio) audio(m_Mix.data(), n);
                }
            }

            int AudioChannelsInFrame() const
            {
#if LIBAVCODEC_VERSION_MAJOR >= 60
                return m_AudioFrame->ch_layout.nb_channels;
#else
                return m_AudioFrame->channels;
#endif
            }

            AVFormatContext* m_Format = nullptr;
            AVCodecContext* m_Video = nullptr;
            AVCodecContext* m_Audio = nullptr;
            AVFrame* m_Frame = nullptr;
            AVFrame* m_AudioFrame = nullptr;
            AVPacket* m_Packet = nullptr;
            int m_VideoIndex = -1, m_AudioIndex = -1;
            double m_TimeBase = 0.0, m_StartTime = -1.0, m_LastTime = 0.0;
            bool m_Ended = false;
            std::vector<float> m_Mix;
        };
    }

    std::unique_ptr<Decoder> CreateFfmpegDecoder() { return std::unique_ptr<Decoder>(new FfmpegDecoder()); }
    bool HasFfmpeg() { return true; }
#else
    std::unique_ptr<Decoder> CreateFfmpegDecoder() { return nullptr; }
    bool HasFfmpeg() { return false; }
#endif
}

#endif
