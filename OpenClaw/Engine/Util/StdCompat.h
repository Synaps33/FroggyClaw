#ifndef OPENCLAW_STDCOMPAT_H
#define OPENCLAW_STDCOMPAT_H

#include <string>
#include <cstdlib>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined(__mips__) || !defined(_GLIBCXX_USE_C99)
namespace std {

inline int stoi(const char* s, size_t* idx = 0, int base = 10) {
    char* end;
    long val = strtol(s, &end, base);
    if (idx) *idx = (size_t)(end - s);
    return (int)val;
}

inline int stoi(const std::string& s, size_t* idx = 0, int base = 10) {
    return stoi(s.c_str(), idx, base);
}

inline float stof(const char* s, size_t* idx = 0) {
    char* end;
    float val = strtof(s, &end);
    if (idx) *idx = (size_t)(end - s);
    return val;
}

inline float stof(const std::string& s, size_t* idx = 0) {
    return stof(s.c_str(), idx);
}

inline double stod(const char* s, size_t* idx = 0) {
    char* end;
    double val = strtod(s, &end);
    if (idx) *idx = (size_t)(end - s);
    return val;
}

inline double stod(const std::string& s, size_t* idx = 0) {
    return stod(s.c_str(), idx);
}

inline unsigned long stoul(const char* s, size_t* idx = 0, int base = 10) {
    char* end;
    unsigned long val = strtoul(s, &end, base);
    if (idx) *idx = (size_t)(end - s);
    return val;
}

inline unsigned long stoul(const std::string& s, size_t* idx = 0, int base = 10) {
    return stoul(s.c_str(), idx, base);
}

inline long stol(const char* s, size_t* idx = 0, int base = 10) {
    char* end;
    long val = strtol(s, &end, base);
    if (idx) *idx = (size_t)(end - s);
    return val;
}

inline long stol(const std::string& s, size_t* idx = 0, int base = 10) {
    return stol(s.c_str(), idx, base);
}

inline string to_string(int val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    return string(buf);
}

inline string to_string(long val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", val);
    return string(buf);
}

inline string to_string(unsigned int val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", val);
    return string(buf);
}

inline string to_string(unsigned long val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", val);
    return string(buf);
}

inline string to_string(float val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%f", (double)val);
    return string(buf);
}

inline string to_string(double val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%f", val);
    return string(buf);
}

} // namespace std
#endif

/* ------------------------------------------------------------------ threads
 *
 * The frog toolchain's soft-float multilib is built without thread support, so
 * _GLIBCXX_HAS_GTHREADS is undefined and <mutex> defines no mutex type at all
 * (std::lock_guard still exists and just needs something lockable). OpenClaw
 * runs single-threaded on the console, driven from retro_run(), so the one
 * place that locks -- libwap guarding REZ reads -- needs no real exclusion.
 */
#if defined(__mips__) && !defined(_GLIBCXX_HAS_GTHREADS)
namespace std {

class mutex {
public:
    typedef int native_handle_type;
    void lock() {}
    void unlock() {}
    bool try_lock() { return true; }
    native_handle_type native_handle() { return 0; }
};

class recursive_mutex : public mutex {
};

} // namespace std
#endif

#endif // OPENCLAW_STDCOMPAT_H
