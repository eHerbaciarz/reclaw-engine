// PS3 (newlib 1.20): libstdc++ was built without the C99 parts, so some
// standard C++11 functions are missing. Force-included in C++ files
// (platform/ps3/CMakeLists.txt) to fill them in.
#pragma once
#include "ps3_sdl_screen.h"
#ifdef __cplusplus
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <string>
#include <stdexcept>

namespace std {
#if !defined(_GLIBCXX_USE_C99_MATH_TR1)
using ::lround; using ::lroundf; using ::llround; using ::llroundf;
using ::round; using ::roundf; using ::trunc; using ::truncf;
using ::lrint; using ::lrintf; using ::rint; using ::rintf;
using ::fmin; using ::fminf; using ::fmax; using ::fmaxf;
using ::cbrt; using ::cbrtf; using ::hypot; using ::hypotf;
using ::log2; using ::log2f; using ::exp2; using ::exp2f;
using ::copysign; using ::copysignf; using ::nearbyint; using ::remainder;
inline long lround(float x) { return ::lroundf(x); }
inline float round(float x) { return ::roundf(x); }
inline float trunc(float x) { return ::truncf(x); }
inline float fmin(float a, float b) { return ::fminf(a, b); }
inline float fmax(float a, float b) { return ::fmaxf(a, b); }
inline float hypot(float a, float b) { return ::hypotf(a, b); }
inline float cbrt(float x) { return ::cbrtf(x); }
inline float log2(float x) { return ::log2f(x); }
inline float copysign(float a, float b) { return ::copysignf(a, b); }
#endif

#if !(_GLIBCXX_USE_C99_STDLIB + 0)
namespace ps3_compat {
template <typename T, typename F>
inline T conv(const char* name, const std::string& s, size_t* idx, F f)
{
    const char* b = s.c_str();
    char* e = nullptr;
    errno = 0;
    T v = f(b, &e);
    if (e == b) throw std::invalid_argument(name);
    if (errno == ERANGE) throw std::out_of_range(name);
    if (idx) *idx = (size_t)(e - b);
    return v;
}
}
inline int stoi(const string& s, size_t* i = nullptr, int base = 10)
{ return (int)ps3_compat::conv<long>("stoi", s, i, [base](const char* b, char** e) { return ::strtol(b, e, base); }); }
inline long stol(const string& s, size_t* i = nullptr, int base = 10)
{ return ps3_compat::conv<long>("stol", s, i, [base](const char* b, char** e) { return ::strtol(b, e, base); }); }
inline unsigned long stoul(const string& s, size_t* i = nullptr, int base = 10)
{ return ps3_compat::conv<unsigned long>("stoul", s, i, [base](const char* b, char** e) { return ::strtoul(b, e, base); }); }
inline long long stoll(const string& s, size_t* i = nullptr, int base = 10)
{ return ps3_compat::conv<long long>("stoll", s, i, [base](const char* b, char** e) { return ::strtoll(b, e, base); }); }
inline unsigned long long stoull(const string& s, size_t* i = nullptr, int base = 10)
{ return ps3_compat::conv<unsigned long long>("stoull", s, i, [base](const char* b, char** e) { return ::strtoull(b, e, base); }); }
inline float stof(const string& s, size_t* i = nullptr)
{ return ps3_compat::conv<float>("stof", s, i, [](const char* b, char** e) { return ::strtof(b, e); }); }
inline double stod(const string& s, size_t* i = nullptr)
{ return ps3_compat::conv<double>("stod", s, i, [](const char* b, char** e) { return ::strtod(b, e); }); }
#endif

#if !(_GLIBCXX_USE_C99_STDIO + 0)
inline string to_string(double v) { char b[64]; snprintf(b, sizeof b, "%f", v); return b; }
inline string to_string(float v) { return to_string((double)v); }
inline string to_string(long double v) { return to_string((double)v); }
#endif
}
#endif
