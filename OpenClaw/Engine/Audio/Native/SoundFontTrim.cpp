#ifndef __EMSCRIPTEN__

#include "SoundFontTrim.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <bitset>
#include <cstring>
#include <map>
#include <set>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

// Declarations only: the implementations are compiled in SDL2MixerSystem.cpp.
#define uint8 stbv_uint8
#define int8 stbv_int8
#define uint16 stbv_uint16
#define int16 stbv_int16
#define uint32 stbv_uint32
#define int32 stbv_int32
#define STB_VORBIS_HEADER_ONLY
#include "../../../../ThirdParty/TinySoundFont/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
#undef uint8
#undef int8
#undef uint16
#undef int16
#undef uint32
#undef int32
#include "../../../../ThirdParty/TinySoundFont/tml.h"

#ifndef SOUNDFONT_TRIM_STANDALONE
#include "../../GameApp/BaseGameApp.h"
#include "../../Resource/ResourceMgr.h"
#include "../../Resource/Loaders/MidiLoader.h"
#include "../../Logger/Logger.h"
#include "../../Platform/Platform.h"
#define TRIM_LOG(msg) LOG_WARNING(msg)   // (warnings reach reclaw.log)
#define TRIM_WARN(msg) LOG_WARNING(msg)
#else
#include <cstdio>
#define TRIM_LOG(msg) printf("%s\n", std::string(msg).c_str())
#define TRIM_WARN(msg) printf("WARNING: %s\n", std::string(msg).c_str())
#endif

namespace SoundFontTrim
{
    namespace
    {
        const unsigned kMaxRate = 22050;
        const char* kSignature = "reclaw-trim 2 src=";   // 2: samples at most 22050 Hz

        // What the music plays: per selected (drums, bank, program) the keys.
        struct Selection
        {
            bool drums;
            int bank;
            int program;
            bool operator<(const Selection& o) const
            {
                if (drums != o.drums) return drums < o.drums;
                if (bank != o.bank) return bank < o.bank;
                return program < o.program;
            }
        };
        std::map<Selection, std::bitset<128>> g_Usage;
        SDL_atomic_t g_UsageState;   // 0 not yet, 1 collected, 2 nothing found
        std::string g_UsageFor;      // full soundfont the usage was collected for

        // ---- little-endian helpers ----
        inline unsigned U16(const unsigned char* p) { return p[0] | (p[1] << 8); }
        inline unsigned U32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
        inline void P16(std::vector<unsigned char>& v, unsigned x) { v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF); }
        inline void P32(std::vector<unsigned char>& v, unsigned x) { P16(v, x & 0xFFFF); P16(v, x >> 16); }
        inline void Put16(unsigned char* p, unsigned x) { p[0] = x & 0xFF; p[1] = (x >> 8) & 0xFF; }
        inline void Put32(unsigned char* p, unsigned x) { Put16(p, x & 0xFFFF); Put16(p + 2, x >> 16); }

        Sint64 FileSize(const std::string& path)
        {
            SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
            if (!rw) return -1;
            Sint64 size = SDL_RWsize(rw);
            SDL_RWclose(rw);
            return size;
        }

        void MakeDirsFor(const std::string& file)
        {
            for (size_t i = 1; i < file.size(); ++i)
            {
                if (file[i] == '/' && file[i - 1] != ':')
                {
#ifdef _WIN32
                    _mkdir(file.substr(0, i).c_str());
#else
                    mkdir(file.substr(0, i).c_str(), 0777);
#endif
                }
            }
        }

        // ---- the hydra (preset/instrument/sample tables) ----
        struct Phdr { char name[20]; unsigned preset, bank, bag, library, genre, morph; };
        struct Bag { unsigned gen, mod; };
        struct Gen { unsigned oper, amount; };
        struct Inst { char name[20]; unsigned bag; };
        struct Shdr { char name[20]; unsigned start, end, loopStart, loopEnd, rate; unsigned char pitch; signed char corr; unsigned link, type; };

        struct Font
        {
            std::vector<Phdr> phdr;
            std::vector<Bag> pbag, ibag;
            std::vector<Gen> pgen, igen;
            std::vector<Inst> inst;
            std::vector<Shdr> shdr;
            Sint64 smplOffset = -1;
            Uint32 smplSize = 0;
        };

        bool ReadChunk(SDL_RWops* rw, char id[4], Uint32& size)
        {
            unsigned char h[8];
            if (SDL_RWread(rw, h, 1, 8) != 8) return false;
            memcpy(id, h, 4);
            size = U32(h + 4);
            return true;
        }

        bool ReadFont(SDL_RWops* rw, Font& f)
        {
            char id[4]; Uint32 size;
            unsigned char form[4];
            if (!ReadChunk(rw, id, size) || memcmp(id, "RIFF", 4) || SDL_RWread(rw, form, 1, 4) != 4 || memcmp(form, "sfbk", 4)) return false;
            const Sint64 end = 8 + (Sint64)size;
            while (SDL_RWtell(rw) + 12 <= end)
            {
                if (!ReadChunk(rw, id, size)) break;
                Sint64 next = SDL_RWtell(rw) + size + (size & 1);
                unsigned char listType[4] = {0};
                if (!memcmp(id, "LIST", 4) && SDL_RWread(rw, listType, 1, 4) == 4)
                {
                    Sint64 listEnd = next;
                    while (SDL_RWtell(rw) + 8 <= listEnd)
                    {
                        char sub[4]; Uint32 subSize;
                        if (!ReadChunk(rw, sub, subSize)) break;
                        Sint64 subNext = SDL_RWtell(rw) + subSize + (subSize & 1);
                        if (!memcmp(listType, "sdta", 4) && !memcmp(sub, "smpl", 4))
                        {
                            f.smplOffset = SDL_RWtell(rw);
                            f.smplSize = subSize;
                        }
                        else if (!memcmp(listType, "pdta", 4))
                        {
                            std::vector<unsigned char> d(subSize);
                            if (subSize && SDL_RWread(rw, d.data(), 1, subSize) != subSize) return false;
                            const unsigned char* p = d.data();
                            if (!memcmp(sub, "phdr", 4))
                                for (Uint32 i = 0; i + 38 <= subSize; i += 38)
                                {
                                    Phdr h; memcpy(h.name, p + i, 20);
                                    h.preset = U16(p + i + 20); h.bank = U16(p + i + 22); h.bag = U16(p + i + 24);
                                    h.library = U32(p + i + 26); h.genre = U32(p + i + 30); h.morph = U32(p + i + 34);
                                    f.phdr.push_back(h);
                                }
                            else if (!memcmp(sub, "pbag", 4) || !memcmp(sub, "ibag", 4))
                                for (Uint32 i = 0; i + 4 <= subSize; i += 4)
                                    (sub[0] == 'p' ? f.pbag : f.ibag).push_back(Bag{ U16(p + i), U16(p + i + 2) });
                            else if (!memcmp(sub, "pgen", 4) || !memcmp(sub, "igen", 4))
                                for (Uint32 i = 0; i + 4 <= subSize; i += 4)
                                    (sub[0] == 'p' ? f.pgen : f.igen).push_back(Gen{ U16(p + i), U16(p + i + 2) });
                            else if (!memcmp(sub, "inst", 4))
                                for (Uint32 i = 0; i + 22 <= subSize; i += 22)
                                {
                                    Inst h; memcpy(h.name, p + i, 20); h.bag = U16(p + i + 20);
                                    f.inst.push_back(h);
                                }
                            else if (!memcmp(sub, "shdr", 4))
                                for (Uint32 i = 0; i + 46 <= subSize; i += 46)
                                {
                                    Shdr h; memcpy(h.name, p + i, 20);
                                    h.start = U32(p + i + 20); h.end = U32(p + i + 24);
                                    h.loopStart = U32(p + i + 28); h.loopEnd = U32(p + i + 32);
                                    h.rate = U32(p + i + 36); h.pitch = p[i + 40]; h.corr = (signed char)p[i + 41];
                                    h.link = U16(p + i + 42); h.type = U16(p + i + 44);
                                    f.shdr.push_back(h);
                                }
                        }
                        SDL_RWseek(rw, subNext, RW_SEEK_SET);
                    }
                }
                SDL_RWseek(rw, next, RW_SEEK_SET);
            }
            return f.phdr.size() >= 2 && f.pbag.size() >= 1 && f.pgen.size() >= 1 && f.inst.size() >= 2 &&
                   f.ibag.size() >= 1 && f.igen.size() >= 1 && f.shdr.size() >= 2 && f.smplOffset >= 0;
        }

        // TinySoundFont's choice of preset (tsf_channel_set_presetnumber).
        int FindPreset(const Font& f, int bank, int program)
        {
            for (size_t i = 0; i + 1 < f.phdr.size(); ++i)
                if ((int)f.phdr[i].preset == program && (int)f.phdr[i].bank == bank) return (int)i;
            return -1;
        }
        int Resolve(const Font& f, const Selection& s)
        {
            int i = -1;
            if (s.drums)
            {
                i = FindPreset(f, 128 | s.bank, s.program);
                if (i < 0) i = FindPreset(f, 128, s.program);
                if (i < 0) i = FindPreset(f, 128, 0);
                if (i < 0) i = FindPreset(f, s.bank, s.program);
            }
            else i = FindPreset(f, s.bank, s.program);
            if (i < 0) i = FindPreset(f, 0, s.program);
            return i;
        }

        std::bitset<128> KeyRange(const std::vector<Gen>& gens, unsigned from, unsigned to)
        {
            std::bitset<128> r;
            unsigned lo = 0, hi = 127;
            for (unsigned g = from; g < to && g < gens.size(); ++g)
                if (gens[g].oper == 43) { lo = gens[g].amount & 0xFF; hi = (gens[g].amount >> 8) & 0xFF; }
            for (unsigned k = lo; k <= hi && k < 128; ++k) r.set(k);
            return r;
        }
        // The generator that ends a zone (instrument / sampleID), or -1 for a global zone.
        int ZoneTarget(const std::vector<Gen>& gens, unsigned from, unsigned to, unsigned oper)
        {
            for (unsigned g = from; g < to && g < gens.size(); ++g)
                if (gens[g].oper == oper) return (int)gens[g].amount;
            return -1;
        }

        // One sample as 16-bit PCM (decoded if Ogg). Loop points relative to its start.
        bool ReadSample(SDL_RWops* rw, const Font& f, const Shdr& s, std::vector<short>& out, unsigned& loopStart, unsigned& loopEnd)
        {
            out.clear();
            if (s.type & 0x30)
            {
                if (s.end <= s.start || s.end > f.smplSize) return false;
                std::vector<unsigned char> ogg(s.end - s.start);
                SDL_RWseek(rw, f.smplOffset + s.start, RW_SEEK_SET);
                if (SDL_RWread(rw, ogg.data(), 1, ogg.size()) != ogg.size()) return false;
                int channels = 0, rate = 0; short* pcm = nullptr;
                int n = stb_vorbis_decode_memory(ogg.data(), (int)ogg.size(), &channels, &rate, &pcm);
                if (n <= 0 || !pcm) { free(pcm); return false; }
                out.resize(n);
                for (int i = 0; i < n; ++i) out[i] = pcm[i * channels];
                free(pcm);
                loopStart = s.loopStart;   // SF3: relative to the sample already
                loopEnd = s.loopEnd;
            }
            else
            {
                unsigned last = std::max(s.end, s.loopEnd);
                if (last <= s.start || (Uint64)last * 2 > f.smplSize) last = std::min<unsigned>(std::max(s.end, s.start), f.smplSize / 2);
                if (last <= s.start) return false;
                std::vector<unsigned char> raw((last - s.start) * 2);
                SDL_RWseek(rw, f.smplOffset + (Sint64)s.start * 2, RW_SEEK_SET);
                if (SDL_RWread(rw, raw.data(), 1, raw.size()) != raw.size()) return false;
                unsigned n = std::max(s.end, s.start) - s.start;
                out.resize(n);
                for (unsigned i = 0; i < n; ++i) out[i] = (short)U16(raw.data() + i * 2);
                loopStart = s.loopStart - s.start;
                loopEnd = s.loopEnd - s.start;
            }
            if (loopEnd > out.size()) loopEnd = (unsigned)out.size();
            if (loopStart > loopEnd) loopStart = loopEnd;
            return true;
        }

        void Name(std::vector<unsigned char>& v, const char name[20])
        {
            for (int i = 0; i < 20; ++i) v.push_back((unsigned char)name[i]);
        }
        void Name(std::vector<unsigned char>& v, const char* text, bool)
        {
            char n[20] = {0};
            strncpy(n, text, 19);
            Name(v, n);
        }

        bool WriteAll(SDL_RWops* out, const std::vector<unsigned char>& v)
        {
            return v.empty() || SDL_RWwrite(out, v.data(), 1, v.size()) == v.size();
        }
        void Chunk(std::vector<unsigned char>& v, const char* id, const std::vector<unsigned char>& data)
        {
            v.insert(v.end(), id, id + 4);
            P32(v, (unsigned)data.size());
            v.insert(v.end(), data.begin(), data.end());
            if (data.size() & 1) v.push_back(0);
        }

        bool Trim(const std::string& fullPath, const std::string& smallPath, const std::function<void(int, int)>& progress)
        {
            SDL_RWops* rw = SDL_RWFromFile(fullPath.c_str(), "rb");
            if (!rw) return false;
            Font f;
            if (!ReadFont(rw, f)) { SDL_RWclose(rw); TRIM_WARN("Soundfont trim: could not read " + fullPath); return false; }

            // Presets the music selects, with the keys played on them.
            std::map<int, std::bitset<128>> presetKeys;
            for (const auto& u : g_Usage)
            {
                int p = Resolve(f, u.first);
                if (p >= 0 && u.second.any()) presetKeys[p] |= u.second;
            }
            // Their zones: instruments, with the keys that reach them.
            std::map<int, std::bitset<128>> instKeys;
            for (const auto& pk : presetKeys)
            {
                const unsigned b0 = f.phdr[pk.first].bag, b1 = f.phdr[pk.first + 1].bag;
                for (unsigned b = b0; b < b1 && b + 1 < f.pbag.size(); ++b)
                {
                    int inst = ZoneTarget(f.pgen, f.pbag[b].gen, f.pbag[b + 1].gen, 41);
                    if (inst < 0 || inst + 1 >= (int)f.inst.size()) continue;
                    std::bitset<128> keys = pk.second & KeyRange(f.pgen, f.pbag[b].gen, f.pbag[b + 1].gen);
                    if (keys.any()) instKeys[inst] |= keys;
                }
            }
            // Their zones: samples.
            std::set<int> samples;
            for (const auto& ik : instKeys)
            {
                const unsigned b0 = f.inst[ik.first].bag, b1 = f.inst[ik.first + 1].bag;
                for (unsigned b = b0; b < b1 && b + 1 < f.ibag.size(); ++b)
                {
                    int smp = ZoneTarget(f.igen, f.ibag[b].gen, f.ibag[b + 1].gen, 53);
                    if (smp < 0 || smp + 1 >= (int)f.shdr.size()) continue;
                    if ((ik.second & KeyRange(f.igen, f.ibag[b].gen, f.ibag[b + 1].gen)).any()) samples.insert(smp);
                }
            }
            if (presetKeys.empty() || samples.empty())
            {
                SDL_RWclose(rw);
                TRIM_WARN("Soundfont trim: none of the music's instruments are in " + fullPath);
                return false;
            }

            const std::string tmpPath = smallPath + ".tmp";
            MakeDirsFor(smallPath);
            SDL_RWops* out = SDL_RWFromFile(tmpPath.c_str(), "wb");
            if (!out) { SDL_RWclose(rw); TRIM_WARN("Soundfont trim: cannot write " + tmpPath); return false; }

            // RIFF header and INFO (the signature tells later starts what it was made from).
            std::vector<unsigned char> head;
            {
                std::vector<unsigned char> info, d;
                info.insert(info.end(), { 'I', 'N', 'F', 'O' });
                d.clear(); P16(d, 2); P16(d, 1); Chunk(info, "ifil", d);
                const std::string isng = "EMU8000";
                d.assign(isng.begin(), isng.end()); d.push_back(0); Chunk(info, "isng", d);
                const std::string inam = "ReClaw music (trimmed)";
                d.assign(inam.begin(), inam.end()); d.push_back(0); Chunk(info, "INAM", d);
                const std::string icmt = std::string(kSignature) + std::to_string((long long)FileSize(fullPath));
                d.assign(icmt.begin(), icmt.end()); d.push_back(0); Chunk(info, "ICMT", d);
                head.insert(head.end(), { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 's', 'f', 'b', 'k' });
                Chunk(head, "LIST", info);
                // sdta LIST header; sizes are filled in at the end.
                head.insert(head.end(), { 'L', 'I', 'S', 'T', 0, 0, 0, 0, 's', 'd', 't', 'a', 's', 'm', 'p', 'l', 0, 0, 0, 0 });
            }
            const size_t sdtaSizeAt = head.size() - 16, smplSizeAt = head.size() - 4;
            bool ok = WriteAll(out, head);

            // Samples one at a time: decoded, written, freed.
            std::map<int, int> newSample;
            std::vector<Shdr> shdrOut;
            Uint32 pos = 0;
            std::vector<short> pcm;
            std::vector<unsigned char> bytes;
            int doneSamples = 0;
            for (int s : samples)
            {
                if (!ok) break;
                if (progress) progress(doneSamples++, (int)samples.size());
                unsigned ls = 0, le = 0;
                if (!ReadSample(rw, f, f.shdr[s], pcm, ls, le) || pcm.empty()) continue;
                Shdr h = f.shdr[s];
                // The game mixes at 22050 Hz: a sample recorded faster only
                // takes memory. Averaged down to 22050 Hz (pitch unchanged:
                // the sample's rate says how fast it plays).
                const unsigned origLoop = le > ls ? le - ls : 0;
                if (h.rate > kMaxRate + kMaxRate / 8)
                {
                    const double r = (double)h.rate / kMaxRate;
                    const size_t n = (size_t)(pcm.size() / r);
                    std::vector<short> down(n);
                    for (size_t i = 0; i < n; ++i)
                    {
                        size_t a = (size_t)(i * r), b = std::max(a + 1, std::min(pcm.size(), (size_t)((i + 1) * r)));
                        long sum = 0;
                        for (size_t k = a; k < b; ++k) sum += pcm[k];
                        down[i] = (short)(sum / (long)(b - a));
                    }
                    pcm.swap(down);
                    ls = std::min((unsigned)pcm.size(), (unsigned)(ls / r + 0.5));
                    le = std::min((unsigned)pcm.size(), (unsigned)(le / r + 0.5));
                    if (ls > le) ls = le;
                    // The rate that keeps a loop exactly as long as before
                    // (a loop of a few hundred samples rounded by half a
                    // sample would be out of tune).
                    h.rate = kMaxRate;
                    if (le > ls && origLoop > 0)
                        h.rate = (unsigned)((double)h.rate * (double)(le - ls) * r / origLoop + 0.5);
                }
                if (pcm.empty()) continue;
                bytes.clear();
                bytes.reserve((pcm.size() + 46) * 2);
                for (short v : pcm) P16(bytes, (unsigned short)v);
                for (int z = 0; z < 46; ++z) P16(bytes, 0);   // the zero points the format wants after each sample
                ok = WriteAll(out, bytes);
                h.start = pos;
                h.end = pos + (Uint32)pcm.size();
                h.loopStart = pos + ls;
                h.loopEnd = pos + le;
                h.type &= ~0x30u;
                newSample[s] = (int)shdrOut.size();
                shdrOut.push_back(h);
                pos += (Uint32)pcm.size() + 46;
            }
            SDL_RWclose(rw);
            if (shdrOut.empty()) ok = false;
            // Stereo partners: the new index, or a mono sample when the partner is gone.
            for (auto& h : shdrOut)
            {
                auto it = newSample.find((int)h.link);
                if ((h.type & 0x0E) && it != newSample.end()) h.link = (unsigned)it->second;
                else { h.link = 0; h.type = (h.type & 0x8000) | 1; }
            }

            // Instruments: kept zones only (global zone included), samples renumbered.
            std::vector<unsigned char> instD, ibagD, igenD;
            std::map<int, int> newInst;
            unsigned nIbag = 0, nIgen = 0;
            for (const auto& ik : instKeys)
            {
                newInst[ik.first] = (int)(instD.size() / 22);
                Name(instD, f.inst[ik.first].name);
                P16(instD, nIbag);
                const unsigned b0 = f.inst[ik.first].bag, b1 = f.inst[ik.first + 1].bag;
                for (unsigned b = b0; b < b1 && b + 1 < f.ibag.size(); ++b)
                {
                    const unsigned g0 = f.ibag[b].gen, g1 = f.ibag[b + 1].gen;
                    int smp = ZoneTarget(f.igen, g0, g1, 53);
                    bool global = (b == b0 && smp < 0);
                    if (!global)
                    {
                        if (smp < 0 || !newSample.count(smp)) continue;
                        if (!(ik.second & KeyRange(f.igen, g0, g1)).any()) continue;
                    }
                    P16(ibagD, nIgen); P16(ibagD, 0); ++nIbag;
                    for (unsigned g = g0; g < g1 && g < f.igen.size(); ++g)
                    {
                        unsigned amount = f.igen[g].amount;
                        if (f.igen[g].oper == 53) amount = (unsigned)newSample[smp];
                        P16(igenD, f.igen[g].oper); P16(igenD, amount); ++nIgen;
                    }
                }
            }
            Name(instD, "EOI", true); P16(instD, nIbag);
            P16(ibagD, nIgen); P16(ibagD, 0);
            P32(igenD, 0);

            // Presets: kept zones only, instruments renumbered.
            std::vector<unsigned char> phdrD, pbagD, pgenD;
            unsigned nPbag = 0, nPgen = 0;
            for (const auto& pk : presetKeys)
            {
                const Phdr& ph = f.phdr[pk.first];
                Name(phdrD, ph.name);
                P16(phdrD, ph.preset); P16(phdrD, ph.bank); P16(phdrD, nPbag);
                P32(phdrD, ph.library); P32(phdrD, ph.genre); P32(phdrD, ph.morph);
                const unsigned b0 = ph.bag, b1 = f.phdr[pk.first + 1].bag;
                for (unsigned b = b0; b < b1 && b + 1 < f.pbag.size(); ++b)
                {
                    const unsigned g0 = f.pbag[b].gen, g1 = f.pbag[b + 1].gen;
                    int inst = ZoneTarget(f.pgen, g0, g1, 41);
                    bool global = (b == b0 && inst < 0);
                    if (!global)
                    {
                        if (inst < 0 || !newInst.count(inst)) continue;
                        if (!(pk.second & KeyRange(f.pgen, g0, g1)).any()) continue;
                    }
                    P16(pbagD, nPgen); P16(pbagD, 0); ++nPbag;
                    for (unsigned g = g0; g < g1 && g < f.pgen.size(); ++g)
                    {
                        unsigned amount = f.pgen[g].amount;
                        if (f.pgen[g].oper == 41) amount = (unsigned)newInst[inst];
                        P16(pgenD, f.pgen[g].oper); P16(pgenD, amount); ++nPgen;
                    }
                }
            }
            Name(phdrD, "EOP", true); P16(phdrD, 0); P16(phdrD, 0); P16(phdrD, nPbag); P32(phdrD, 0); P32(phdrD, 0); P32(phdrD, 0);
            P16(pbagD, nPgen); P16(pbagD, 0);
            P32(pgenD, 0);

            std::vector<unsigned char> shdrD;
            for (const Shdr& h : shdrOut)
            {
                Name(shdrD, h.name);
                P32(shdrD, h.start); P32(shdrD, h.end); P32(shdrD, h.loopStart); P32(shdrD, h.loopEnd);
                P32(shdrD, h.rate); shdrD.push_back(h.pitch); shdrD.push_back((unsigned char)h.corr);
                P16(shdrD, h.link); P16(shdrD, h.type);
            }
            {
                Name(shdrD, "EOS", true);
                for (int i = 0; i < 26; ++i) shdrD.push_back(0);
            }
            const std::vector<unsigned char> noMods(10, 0);   // TinySoundFont ignores modulators

            std::vector<unsigned char> pdta = { 'p', 'd', 't', 'a' };
            Chunk(pdta, "phdr", phdrD); Chunk(pdta, "pbag", pbagD); Chunk(pdta, "pmod", noMods);
            Chunk(pdta, "pgen", pgenD); Chunk(pdta, "inst", instD); Chunk(pdta, "ibag", ibagD);
            Chunk(pdta, "imod", noMods); Chunk(pdta, "igen", igenD); Chunk(pdta, "shdr", shdrD);
            std::vector<unsigned char> tail;
            Chunk(tail, "LIST", pdta);
            if (ok) ok = WriteAll(out, tail);

            // Sizes.
            if (ok)
            {
                const Uint32 smplBytes = pos * 2;
                unsigned char b[4];
                Put32(b, smplBytes); SDL_RWseek(out, (Sint64)smplSizeAt, RW_SEEK_SET); ok = SDL_RWwrite(out, b, 1, 4) == 4;
                Put32(b, smplBytes + 12); SDL_RWseek(out, (Sint64)sdtaSizeAt, RW_SEEK_SET); ok = ok && SDL_RWwrite(out, b, 1, 4) == 4;
                const Sint64 total = (Sint64)head.size() + smplBytes + (Sint64)tail.size();
                Put32(b, (Uint32)(total - 8)); SDL_RWseek(out, 4, RW_SEEK_SET); ok = ok && SDL_RWwrite(out, b, 1, 4) == 4;
            }
            if (SDL_RWclose(out) != 0) ok = false;
            if (!ok)
            {
                remove(tmpPath.c_str());
                TRIM_WARN("Soundfont trim: writing " + smallPath + " failed");
                return false;
            }
            remove(smallPath.c_str());
            if (rename(tmpPath.c_str(), smallPath.c_str()) != 0)
            {
                remove(tmpPath.c_str());
                TRIM_WARN("Soundfont trim: cannot rename " + tmpPath);
                return false;
            }
            TRIM_LOG("Soundfont trim: " + smallPath + " written: " + std::to_string(presetKeys.size()) + " presets, " +
                     std::to_string(instKeys.size()) + " instruments, " + std::to_string(shdrOut.size()) + " samples, " +
                     std::to_string((long long)pos * 2 / 1024) + " KB of samples");
            return true;
        }
    }

    // Reads one MIDI song into the usage (TinySoundFont's channel state).
    void AddMidiUsage(const tml_message* msg)
    {
        struct Channel { int bank = 0; Selection sel{ false, 0, 0 }; };
        Channel ch[16];
        for (int c = 0; c < 16; ++c) ch[c].sel = Selection{ c == 9, 0, 0 };
        for (; msg; msg = msg->next)
        {
            Channel& c = ch[msg->channel & 15];
            switch (msg->type)
            {
                case TML_CONTROL_CHANGE:
                    if (msg->control == 0) c.bank = 0x8000 | msg->control_value;
                    else if (msg->control == 32) c.bank = ((c.bank & 0x8000) ? ((c.bank & 0x7F) << 7) : 0) | msg->control_value;
                    else if (msg->control == 121) c.bank = 0;
                    break;
                case TML_PROGRAM_CHANGE:
                    c.sel = Selection{ (msg->channel & 15) == 9, c.bank & 0x7FFF, msg->program & 0x7F };
                    break;
                case TML_NOTE_ON:
                    if (msg->velocity > 0) g_Usage[c.sel].set(msg->key & 0x7F);
                    break;
                default:
                    break;
            }
        }
    }

    std::string SmallPath(const std::string& fullPath)
    {
        (void)fullPath;
#ifndef SOUNDFONT_TRIM_STANDALONE
        return Platform::RootDir() + "music/soundfont_small.sf2";
#else
        return "soundfont_small.sf2";
#endif
    }

    bool SmallIsValid(const std::string& fullPath)
    {
        Sint64 fullSize = FileSize(fullPath);
        if (fullSize <= 0) return false;
        SDL_RWops* rw = SDL_RWFromFile(SmallPath(fullPath).c_str(), "rb");
        if (!rw) return false;
        char head[512] = {0};
        size_t n = SDL_RWread(rw, head, 1, sizeof(head) - 1);
        SDL_RWclose(rw);
        const std::string want = std::string(kSignature) + std::to_string((long long)fullSize);
        for (size_t i = 0; i + want.size() < n; ++i)
            if (!memcmp(head + i, want.c_str(), want.size()) && head[i + want.size()] == 0) return true;
        return false;
    }

#ifndef SOUNDFONT_TRIM_STANDALONE
    static std::vector<std::string> g_FullPaths;
    void SetFullPaths(const std::vector<std::string>& fullPaths) { g_FullPaths = fullPaths; }

    void CollectUsage()
    {
        if (g_FullPaths.empty()) return;
        std::string full;
        for (const std::string& p : g_FullPaths)
            if (FileSize(p) > 0) { full = p; break; }
        if (full.empty() || SmallIsValid(full))
        {
            SDL_AtomicSet(&g_UsageState, 2);
            return;
        }
        g_Usage.clear();
        g_UsageFor = full;
        int songs = 0;
        if (g_pApp && g_pApp->GetResourceMgr())
        {
            for (std::string name : g_pApp->GetResourceMgr()->VMatch("*.xmi"))
            {
                if (name.empty()) continue;
                if (name[0] != '/') name = "/" + name;
                std::transform(name.begin(), name.end(), name.begin(), ::toupper);
                shared_ptr<MidiFile> midi = MidiResourceLoader::LoadAndReturnMidiFile(name.c_str());
                if (!midi || !midi->data || midi->size == 0) continue;
                tml_message* msgs = tml_load_memory(midi->data, (int)midi->size);
                if (!msgs) continue;
                AddMidiUsage(msgs);
                tml_free(msgs);
                ++songs;
            }
        }
        size_t keys = 0;
        for (const auto& u : g_Usage) keys += u.second.count();
        LOG_WARNING("Soundfont trim: " + std::to_string(songs) + " songs of the game use " + std::to_string(g_Usage.size()) +
            " instruments, " + std::to_string(keys) + " notes");
        SDL_AtomicSet(&g_UsageState, songs > 0 ? 1 : 2);
    }
#endif

    bool MakeSmall(const std::string& fullPath, int timeoutMs)
    {
        if (SmallIsValid(fullPath)) return true;
        // 0: not yet, 3: the main thread is making it (MakeSmallNow)
        // (while the main thread is making it - state 3 - as long as it takes:
        // on the 3DS that is a few minutes)
        for (int waited = 0; SDL_AtomicGet(&g_UsageState) == 3 || (SDL_AtomicGet(&g_UsageState) == 0 && waited < timeoutMs); waited += 100)
            SDL_Delay(100);
        if (SmallIsValid(fullPath)) return true;
        if (SDL_AtomicGet(&g_UsageState) != 1) return false;
        if (!g_UsageFor.empty() && g_UsageFor != fullPath) return false;
        TRIM_LOG("Soundfont trim: making " + SmallPath(fullPath) + " from " + fullPath + " (first start)");
        return Trim(fullPath, SmallPath(fullPath), nullptr);
    }

#ifndef SOUNDFONT_TRIM_STANDALONE
    bool MakeSmallNow(const std::function<void(int, int)>& progress)
    {
        if (SDL_AtomicGet(&g_UsageState) != 1 || g_UsageFor.empty()) return false;
        SDL_AtomicSet(&g_UsageState, 3);
        TRIM_LOG("Soundfont trim: making " + SmallPath(g_UsageFor) + " from " + g_UsageFor + " (first start)");
        const Uint32 t0 = SDL_GetTicks();
        bool ok = Trim(g_UsageFor, SmallPath(g_UsageFor), progress);
        TRIM_LOG("Soundfont trim: " + std::string(ok ? "done" : "failed") + " in " + std::to_string(SDL_GetTicks() - t0) + " ms");
        SDL_AtomicSet(&g_UsageState, 2);   // the loader finds the file (or nothing to do)
        return ok;
    }
#endif

#ifdef SOUNDFONT_TRIM_STANDALONE
    // Test tool: the usage of the given MIDI files, then the small soundfont.
    bool TrimForMidis(const std::string& fullPath, const std::vector<std::string>& midis, const std::string& outPath)
    {
        g_Usage.clear();
        for (const std::string& m : midis)
            if (tml_message* msgs = tml_load_filename(m.c_str())) { AddMidiUsage(msgs); tml_free(msgs); }
        return Trim(fullPath, outPath, nullptr);
    }
#endif
}

#endif
