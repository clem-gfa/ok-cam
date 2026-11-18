/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// RecordingName.h — names of the recording files: a user-chosen prefix + the start date/time.
//
//   <prefix>_<YYYYMMDD>_<HHMMSS>_video.mp4          video (+ _video.csv, _video_info.txt)
//   <prefix>_<YYYYMMDD>_<HHMMSS>_000.wav / .csv     audio segments (with the video, same session name)
//   <prefix>_audio_<YYYYMMDD>_<HHMMSS>_000.wav      audio alone
//
// RecordingSettings is the prefix + folder shared by the main window and the microphone window (GUI thread only): editing
// either control updates the other, and the owner persists it.

#pragma once

#include <cctype>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace okcli {

// A prefix safe as the start of a file name on Windows, macOS and Linux: letters, digits, '.', '_', '-' and any non-ASCII
// (UTF-8) character are kept; spaces become '_'; characters Windows forbids (<>:"/\|?*) and control characters are dropped;
// leading dots/underscores and trailing dots/underscores are trimmed; at most 64 bytes. Empty result -> `fallback`.
inline std::string sanitizeFilePrefix(const std::string& in, const std::string& fallback = "okcam") {
    std::string out;
    for (unsigned char c : in) {
        if (c == ' ') out += '_';
        else if (c >= 0x80 || std::isalnum(c) || c == '.' || c == '_' || c == '-') out += char(c);
        // everything else (path separators, <>:"|?*, control characters) is dropped
    }
    // do not cut in the middle of a UTF-8 sequence
    if (out.size() > 64) {
        std::size_t n = 64;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) n--;
        out.resize(n);
    }
    while (!out.empty() && (out.front() == '.' || out.front() == '_')) out.erase(out.begin());
    while (!out.empty() && (out.back() == '.' || out.back() == '_' || out.back() == ' ')) out.pop_back();
    return out.empty() ? fallback : out;
}

// Session name (prefix of every file of a recording). `audioOnly` adds "audio" so that audio-only recordings are recognisable.
inline std::string recordingSession(const std::string& prefix, bool audioOnly, std::time_t t) {
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", std::localtime(&t));
    return sanitizeFilePrefix(prefix) + (audioOnly ? "_audio_" : "_") + stamp;
}

struct RecordingSettings {
    std::string prefix = "okcam";
    std::string dir;                                   // last folder used (empty: ask)
    int audioDecimation = 1;                           // audio files: 1, 2, 4, 8 (sample rate / N)
    int audioBits = 24;                                // audio files: 24 or 16 bits
    std::vector<std::function<void()>> listeners;      // called after a change (GUI thread)
    std::function<void()> persist;                     // called when a recording starts: the owner saves the settings

    void setPrefix(const std::string& p) {
        if (p == prefix) return;
        prefix = p;
        notify();
    }
    void setDir(const std::string& d) {
        if (d == dir) return;
        dir = d;
        notify();
    }
    void setAudioFormat(int decimation, int bits) {
        if (decimation == audioDecimation && bits == audioBits) return;
        audioDecimation = decimation;
        audioBits = bits;
        notify();
    }
    void notify() { for (auto& l : listeners) l(); }
};

}  // namespace okcli
